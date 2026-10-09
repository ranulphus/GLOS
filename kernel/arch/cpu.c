/* GDT, IDT, TSS and the #DF task; trap dispatch and panic (supervisor.md §3,
 * §6). Exceptions and the physical IRQs have DPL-0 gates (INT3 and INTO
 * DPL 3); the other vectors are DPL-3 "fast gates" for DPMI clients' software
 * interrupts (M4a), so INT 21h and INT 31h from ring 3 arrive directly. */
#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "random.h"
#include "sched.h"

struct tss {
    u32 link, esp0, ss0, esp1, ss1, esp2, ss2, cr3, eip, eflags, eax, ecx, edx, ebx;
    u32 esp, ebp, esi, edi, es, cs, ss, ds, fs, gs, ldt;
    u16 trap, iomap;
} __attribute__((packed));

extern char stub_base[];
extern void df_entry(void);
extern u32 fixup_eip, fixup_vec;
extern struct { u32 off; u16 sel; } __attribute__((packed)) ret_farptr;
extern u32 espfix_pending;
extern struct { u32 esp; u16 sel; } __attribute__((packed)) espfix_ptr;
static const u32 *ldt_base;
static u32 ldt_limit;

static u32 gdt[2 * 13] __attribute__((aligned(8)));     /* selectors 00h-60h */
static u32 idt[2 * 256] __attribute__((aligned(8)));
static u32 gdt_copy[ARRAY_SIZE(gdt)];                   /* for the #DF report */
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

/* DPL 3 for INT3, INTO and the software-interrupt vectors (20h-4Fh,
   60h-FFh); DPL 0 for exceptions and the IRQs at 50h-5Fh, so INT 0-1Fh or
   50h-5Fh from ring 3 is a #GP the host decodes (supervisor.md §3.2). */
static u8 gate_type(int v)
{
    if (v == 3 || v == 4 || (v >= 0x20 && v < 0x50) || v >= 0x60)
        return 0xEE;
    return 0x8E;
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

    set_desc(SEL_ESPFIX, 0, 0xFFFFF, 0x92, 0xC0);       /* flat; the base moves for each espfix return */
    set_desc(SEL_TRAMP & ~3, TRAMP_LIN, 0xFFF, 0xFA, 0x40);     /* ring-3 code, 32-bit */
    set_desc(SEL_TRAMPD & ~3, TRAMP_LIN, 0xFFF, 0xF2, 0x40);
    set_desc(SEL_BIOS, 0x400, 0xFFFF, 0xF2, 0x00);
    set_desc(SEL_BIOS5B & ~3, 0x400, 0xFFFF, 0xF2, 0x00);
    for (v = 0; v < 256; v++)
        set_gate(v, (u32)stub_base + v * 16, SEL_KCODE, gate_type(v));
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
    memcpy(gdt_copy, gdt, sizeof gdt);
    /* FPU errors as IRQ13 (CR0.NE clear, the PC's way), as DOS programs, DJGPP's
       INT 75h handler and CWSDPMI expect: the FPU has one user, the system VM,
       until GLOS apps (supervisor.md §14.7). */
    write_cr0(read_cr0() & ~0x20u);

    ret_farptr.off = ret_off;
    ret_farptr.sel = SEL_CODE16;
}

/* 38h follows GLOS.EXE's resident stub when it moves (supervisor.md §2.2). */
void cpu_set_code16_base(u32 base)
{
    set_desc(SEL_CODE16, base, 0xFFFF, 0x9A, 0x00);
    memcpy(gdt_copy, gdt, sizeof gdt);
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

void cpu_set_ldt(const void *base, u32 limit)
{
    ldt_base = base;
    ldt_limit = limit;
    if (!limit) {
        __asm__ volatile("lldt %w0" :: "r"(0));
        return;
    }
    set_desc(SEL_LDT, (u32)base, limit, 0x82, 0x00);
    __asm__ volatile("lldt %w0" :: "r"(SEL_LDT));
}

u32 cpu_desc_hi(u32 sel)
{
    u32 i = (sel & 0xFFFF) >> 3;
    if (sel & 4)
        return ldt_base && i * 8 + 7 <= ldt_limit ? ldt_base[i * 2 + 1] : 0;
    return i < ARRAY_SIZE(gdt) / 2 ? gdt[i * 2 + 1] : 0;
}

/* VME's interrupt redirection bitmap: with CR4.VME, INT n in V86 mode goes
   straight through the IVT when its bit is clear, and traps when set. */
void cpu_int_redirect(u32 vec, int redirect)
{
    if (redirect) TSS_REDIR[vec >> 3] &= (u8)~(1u << (vec & 7));
    else TSS_REDIR[vec >> 3] |= (u8)(1u << (vec & 7));
}

void cpu_set_cr4(u32 set, u32 clear)
{
    u32 v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    __asm__ volatile("mov %0, %%cr4" :: "r"((v & ~clear) | set) : "memory");
}

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
    kprintf_direct();
    dump(why, tf);
    for (;;)
        hlt();
}

/* The #DF task runs on its own stack and the kernel's page directory, so it
   can report what the fault left: the interrupted context (saved in the
   main TSS by the task switch), ESP0 and the page under it, any gate or
   descriptor that changed since cpu_init, and every thread's stack. */
static u32 pte_of(u32 lin)
{
    u32 pde = *(volatile u32 *)(0xFFFFF000u + (lin >> 22) * 4);
    return (pde & 1) ? *(volatile u32 *)(0xFFC00000u + (lin >> 12) * 4) : 0;
}

void double_fault(void)
{
    u32 v, bad = 0;
    kprintf_direct();
    kprintf("GLOS-PANIC why=double-fault eip=%p esp=%p cs=%x ss=%x eflags=%x cr2=%p esp0=%p ss0=%x pte=%p\n",
            TSS->eip, TSS->esp, TSS->cs, TSS->ss, TSS->eflags, read_cr2(), TSS->esp0, TSS->ss0,
            pte_of(TSS->esp0 - 4));
    for (v = 0; v < 256; v++) {
        u32 off = (u32)stub_base + v * 16;
        u32 lo = (off & 0xFFFF) | ((u32)SEL_KCODE << 16);
        u32 hi = (off & 0xFFFF0000) | ((u32)gate_type((int)v) << 8);
        if (v != 8 && (idt[v * 2] != lo || idt[v * 2 + 1] != hi) && bad++ < 4)
            kprintf("GLOS-PANIC gate=%x is=%p:%p\n", v, idt[v * 2 + 1], idt[v * 2]);
    }
    for (v = 0; v < ARRAY_SIZE(gdt); v++)
        if (gdt[v] != gdt_copy[v] && v / 2 != SEL_TSS / 8 && v / 2 != SEL_DFTSS / 8 && v / 2 != SEL_LDT / 8
            && v / 2 != SEL_ESPFIX / 8 && bad++ < 8)
            kprintf("GLOS-PANIC gdt=%x is=%p was=%p\n", v / 2 * 8, gdt[v], gdt_copy[v]);
    sched_panic_report();
    for (;;)
        hlt();
}

/* Back to user mode: the system VM's V86 code, or a DPMI client at ring 3.
   A trap may have turned one into the other (the mode switch, a raw switch,
   INT 21h 4Ch), so the frame says which. */
static void back_to_user(struct trapframe *tf)
{
    if (tf->eflags & EFLAGS_VM)
        vm_return(tf);
    else
        dpmi_return(tf);
}

static u32 stray_db;                                    /* single steps dropped in ring 0 (86Box) */

static void dispatch(struct trapframe *tf)
{
    u32 v = tf->vec;
    if ((tf->eflags & EFLAGS_VM) || (tf->cs & 3))
        vm_user_entry(tf);
    if (v >= IRQ_BASE_MASTER && v < IRQ_BASE_MASTER + 16) {
        int irq = (int)(v - IRQ_BASE_MASTER);
        random_event(v);
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
        sched_trap_exit(tf);
        if ((tf->eflags & EFLAGS_VM) || (tf->cs & 3))
            back_to_user(tf);
        return;
    }
    if (tf->eflags & EFLAGS_VM) {                       /* from the system VM */
        vm_exception(tf);
        sched_trap_exit(tf);
        back_to_user(tf);
        return;
    }
    if ((tf->cs & 3) == 3) {                            /* from a DPMI client */
        dpmi_trap(tf);
        sched_trap_exit(tf);
        back_to_user(tf);
        return;
    }
    if (v == 14 && lin_fault(read_cr2()))               /* the kernel touched a client's page still to */
        return;                                         /* be backed (kernel/dpmi/mem.c) */
    if (fixup_eip) {                                    /* a try_* helper or ucopy faulted */
        tf->eip = fixup_eip;
        fixup_eip = 0;
        fixup_vec = v;
        return;
    }
    if (v == 1 && dpmi_db_hit())                        /* the kernel touched a client's watchpoint */
        return;
    if (v < 32 && trap_fn[v] && trap_fn[v](tf))
        return;
    if (v == 1) {                                       /* a #DB in ring 0 no one claims: never the kernel's own */
        u32 dr6 = read_dr6();                           /* (it sets no TF, and its DRs are clients' watchpoints). */
        write_dr6(dr6 & ~0x400Fu);                      /* 86Box keeps the single step of an instruction that */
        if (!stray_db++)                                /* faulted into the kernel pending, and takes it in the */
            kprintf("GLOS-CPU stray-db at=%p dr6=%x\n", tf->eip, dr6);  /* handler (silicon doesn't, SDM 17.3.1.4) */
        return;
    }
    panic(v < 32 ? "exception" : "unexpected-interrupt", tf);
}

/* Every trap. On the way back to user mode, ESP0 goes where the next trap
   from there must build its frame: on this one. A ring-3 entry pushes 16
   bytes fewer than a V86 entry (no ES, DS, FS, GS), so its ESP0 is 16 bytes
   lower and the frame sits at the same place in either mode (nested real-
   mode calls rely on it, kernel/dpmi/rmcall.c). A return to a 16-bit ring-3
   stack goes through espfix (supervisor.md §6.3). */
void trap_dispatch(struct trapframe *tf)
{
    u32 top;
    dispatch(tf);
    if (!(tf->eflags & EFLAGS_VM) && !(tf->cs & 3))
        return;
    top = (u32)tf + sizeof *tf;
    if (!(tf->eflags & EFLAGS_VM)) {
        top -= 16;
        if (!(cpu_desc_hi(tf->ss) & (1u << 22))) {
            u32 l = (u32)&tf->eip, e = (tf->esp & 0xFFFF0000u) | (l & 0xFFFF);
            set_desc(SEL_ESPFIX, l - e, 0xFFFFF, 0x92, 0xC0);
            espfix_ptr.esp = e;
            espfix_ptr.sel = SEL_ESPFIX;
            espfix_pending = 1;
        }
    }
    current->esp0 = top;
    cpu_set_esp0(top);
}
