/* GDT, IDT, TSS and the #DF task; trap dispatch and panic (supervisor.md §3,
 * §6). M1 runs only ring-0 code, so every gate is DPL 0 except INT3/INTO;
 * the DPL-3 client gates of §3.2 arrive with the DPMI host (M4). */
#include "arch.h"
#include "io.h"
#include "kprintf.h"

struct tss {
    u32 link, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags, eax, ecx, edx, ebx;
    u32 esp, ebp, esi, edi, es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap;
} __attribute__((packed));

extern char stub_base[];
extern void df_entry(void);
extern u32 fixup_eip, fixup_vec;
extern struct { u32 off; u16 sel; } __attribute__((packed)) ret_farptr;

static u32 gdt[2 * 12] __attribute__((aligned(8)));     /* selectors 00h-58h */
static u32 idt[2 * 256] __attribute__((aligned(8)));
/* The TSS's fixed part, the 32-byte VME redirection bitmap, the 8 KB I/O
   permission bitmap and its mandatory trailing FFh byte: three pages
   (supervisor.md §3.3). */
static u8 tss_area[3 * 4096] __attribute__((aligned(4096)));
#define TSS ((struct tss *)tss_area)
#define TSS_REDIR (tss_area + 104)
#define TSS_IOPB  (tss_area + 136)
#define TSS_LIMIT (136 + 8192)
_Static_assert(104 + 32 == 136, "the redirection bitmap ends where the I/O map starts");
_Static_assert(TSS_LIMIT + 1 <= sizeof tss_area, "the I/O map's trailing FFh byte is inside the TSS area");
static struct tss dftss __attribute__((aligned(16)));
static u8 df_stack[4096] __attribute__((aligned(16)));
static u8 trap_stack[8192] __attribute__((aligned(16)));

#define EFLAGS_VM 0x00020000u

static void (*irq_fn[16])(struct trapframe *);
static int (*trap_fn[32])(struct trapframe *);

u32 cpu_cr4_bits;
int cpu_has_cr4;

static void set_desc(int sel, u32 base, u32 limit, u8 access, u8 flags)
{
    u32 *e = &gdt[sel / 4];
    e[0] = (limit & 0xFFFF) | (base << 16);
    e[1] = ((base >> 16) & 0xFF) | ((u32)access << 8) | (limit & 0xF0000) | ((u32)(flags & 0xF0) << 16)
         | (base & 0xFF000000);
}

static void set_gate(int v, u32 off, u16 sel, u8 type)
{
    idt[v * 2] = (off & 0xFFFF) | ((u32)sel << 16);
    idt[v * 2 + 1] = (off & 0xFFFF0000) | ((u32)type << 8);
}

void cpu_init(u32 cs16_base, u32 ds16_base, u32 ret_off)
{
    struct { u16 limit; u32 base; } __attribute__((packed)) d;
    int v;

    set_desc(SEL_KCODE, 0, 0xFFFFF, 0x9A, 0xC0);
    set_desc(SEL_KDATA, 0, 0xFFFFF, 0x92, 0xC0);
    set_desc(SEL_TSS, (u32)tss_area, TSS_LIMIT, 0x89, 0x00);
    set_desc(SEL_DFTSS, (u32)&dftss, sizeof dftss - 1, 0x89, 0x00);
    set_desc(SEL_CODE16, cs16_base, 0xFFFF, 0x9A, 0x00);
    set_desc(SEL_DATA16, ds16_base, 0xFFFF, 0x92, 0x00);
    d.limit = sizeof gdt - 1;
    d.base = (u32)gdt;
    __asm__ volatile("lgdt %0\n"
                     "ljmp $0x08, $1f\n"
                     "1: movw $0x10, %%ax\n movw %%ax, %%ds\n movw %%ax, %%es\n movw %%ax, %%ss\n"
                     "movw %%ax, %%fs\n movw %%ax, %%gs" :: "m"(d) : "eax", "memory");

    for (v = 0; v < 256; v++)
        set_gate(v, (u32)stub_base + v * 16, SEL_KCODE, (v == 3 || v == 4) ? 0xEE : 0x8E);
    set_gate(8, 0, SEL_DFTSS, 0x85);                    /* #DF: a task gate */
    d.limit = sizeof idt - 1;
    d.base = (u32)idt;
    __asm__ volatile("lidt %0" :: "m"(d));

    TSS->ss0 = SEL_KDATA;
    TSS->esp0 = (u32)trap_stack + sizeof trap_stack;
    TSS->iomap = 136;
    memset(TSS_REDIR, 0xFF, 32);                        /* no software INT redirected (VME is off) */
    memset(TSS_IOPB, 0x00, 8192);                       /* every port passed through ... */
    TSS_IOPB[8192] = 0xFF;                              /* ... then the trailing byte */
    dftss.cr3 = read_cr3();
    dftss.eip = (u32)df_entry;
    dftss.esp = (u32)df_stack + sizeof df_stack;
    dftss.eflags = 2;
    dftss.cs = SEL_KCODE;
    dftss.ds = dftss.es = dftss.ss = dftss.fs = dftss.gs = SEL_KDATA;
    dftss.iomap = sizeof dftss;
    __asm__ volatile("ltr %w0" :: "r"(SEL_TSS));

    ret_farptr.off = ret_off;
    ret_farptr.sel = SEL_CODE16;
}

/* The #DF task must use the kernel's own page directory once there is one. */
void cpu_set_df_cr3(u32 cr3) { dftss.cr3 = cr3; }

void cpu_features(void)
{
    static const u32 bits[] = { 0x001, 0x002, 0x004, 0x008, 0x010, 0x040, 0x080, 0x100, 0x200 };
    u32 base, got, i;
    cpu_has_cr4 = try_rd_cr4(&base) == 0;
    if (!cpu_has_cr4)
        return;
    for (i = 0; i < ARRAY_SIZE(bits); i++) {
        if (try_wr_cr4(base | bits[i]) == 0 && try_rd_cr4(&got) == 0 && (got & bits[i]))
            cpu_cr4_bits |= bits[i];
        try_wr_cr4(base);
    }
}

/* Trap (1) or pass through (0) a port for V86 code and ring-3 code below IOPL. */
void cpu_io_trap(u32 port, int trap)
{
    if (trap) TSS_IOPB[port >> 3] |= (u8)(1u << (port & 7));
    else TSS_IOPB[port >> 3] &= (u8)~(1u << (port & 7));
}

void cpu_set_esp0(u32 esp0) { TSS->esp0 = esp0; }

void set_irq_handler(int irq, void (*fn)(struct trapframe *)) { irq_fn[irq] = fn; }
void set_trap_handler(int vec, int (*fn)(struct trapframe *)) { trap_fn[vec] = fn; }

static void dump(const char *why, struct trapframe *tf)
{
    kprintf("GLOS-PANIC why=%s", why);
    if (tf)
        kprintf(" vec=%x err=%x eip=%p cs=%x eflags=%x cr2=%p eax=%p ebx=%p ecx=%p edx=%p esi=%p edi=%p ebp=%p",
                tf->vec, tf->err, tf->eip, tf->cs, tf->eflags, read_cr2(), tf->eax, tf->ebx, tf->ecx, tf->edx,
                tf->esi, tf->edi, tf->ebp);
    kprintf("\n");
}

void panic(const char *why, struct trapframe *tf)
{
    cli();
    dump(why, tf);
    for (;;)
        hlt();
}

void double_fault(void)
{
    kprintf("GLOS-PANIC why=double-fault eip=%p esp=%p\n", TSS->eip, TSS->esp);
    for (;;)
        hlt();
}

void trap_dispatch(struct trapframe *tf)
{
    u32 v = tf->vec;
    if (v >= IRQ_BASE_MASTER && v < IRQ_BASE_MASTER + 16) {
        int irq = (int)(v - IRQ_BASE_MASTER);
        if ((irq == 7 || irq == 15) && !irq_fn[irq]) {     /* spurious unless in service */
            outb(irq == 7 ? 0x20 : 0xA0, 0x0B);
            if (!(inb(irq == 7 ? 0x20 : 0xA0) & 0x80)) {
                if (irq == 15) outb(0x20, 0x20);
                goto out;
            }
        }
        if (irq_fn[irq])
            irq_fn[irq](tf);
        if (irq >= 8) outb(0xA0, 0x20);
        outb(0x20, 0x20);
    out:
        if (tf->eflags & EFLAGS_VM)
            vm_return(tf);
        return;
    }
    if (tf->eflags & EFLAGS_VM) {                       /* from the system VM */
        vm_exception(tf);
        vm_return(tf);
        return;
    }
    if (fixup_eip && (tf->cs & 3) == 0) {               /* a try_* helper faulted */
        tf->eip = fixup_eip;
        fixup_eip = 0;
        fixup_vec = v;
        return;
    }
    if (v < 32 && trap_fn[v] && trap_fn[v](tf))
        return;
    panic(v < 32 ? "exception" : "unexpected-interrupt", tf);
}
