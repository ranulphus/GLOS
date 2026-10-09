/* CPU tables, traps and the way back to the loader (supervisor.md §3, §6). */
#ifndef K_ARCH_H
#define K_ARCH_H
#include "types.h"

/* Selectors, fixed (supervisor.md §3.1). */
#define SEL_KCODE    0x08
#define SEL_KDATA    0x10
#define SEL_TSS      0x18
#define SEL_DFTSS    0x20
#define SEL_LDT      0x28
#define SEL_ESPFIX   0x30
#define SEL_CODE16   0x38
#define SEL_BIOS     0x40       /* ring-3 data at 400h: the 0040h that Windows-era code loads (GTA, M4c) */
#define SEL_TRAMP    0x4B       /* ring-3 code: the host's trampolines (kernel/dpmi) */
#define SEL_TRAMPD   0x53       /* its data alias */
#define SEL_BIOS5B   0x5B       /* the same as 40h (M1-M4b's BIOS selector) */
#define SEL_DATA16   0x60       /* 16-bit data at the resident stub, to leave protected mode */
#define TRAMP_LIN    0x3FF000u  /* the trampoline page, in PDE 0's table, so in every address space */

/* The physical PIC's vectors (supervisor.md §3.2). */
#define IRQ_BASE_MASTER 0x50
#define IRQ_BASE_SLAVE  0x58

struct trapframe {
    u32 edi, esi, ebp, kesp, ebx, edx, ecx, eax;        /* pushal */
    u32 gs, fs, es, ds;
    u32 vec, err;
    u32 eip, cs, eflags;
    u32 esp, ss;                                        /* privilege change only */
    u32 v86_es, v86_ds, v86_fs, v86_gs;                 /* from V86 only */
};

void cpu_init(u32 cs16_base, u32 ds16_base, u32 ret_off);
void cpu_features(void);
void set_irq_handler(int irq, void (*fn)(struct trapframe *));
void cpu_io_trap(u32 port, int trap);
void cpu_set_esp0(u32 esp0);
void cpu_set_code16_base(u32 base);
void cpu_int_redirect(u32 vec, int redirect);
void cpu_set_cr4(u32 set, u32 clear);   /* only when cpu_has_cr4 */
void cpu_set_ldt(const void *base, u32 limit);  /* the current context's LDT (selector 28h); 0 limit: none */
u32 cpu_desc_hi(u32 sel);               /* the high dword of a GDT or LDT descriptor (0 if out of range) */
void set_trap_handler(int vec, int (*fn)(struct trapframe *));    /* returns 1 if handled */
void panic(const char *why, struct trapframe *tf) __attribute__((noreturn));

/* vm/v86.c: traps from the system VM (EFLAGS.VM set in the frame) */
void vm_user_entry(struct trapframe *tf);       /* every entry from V86 mode or ring 3 */
void vm_user_exit(struct trapframe *tf);        /* every way out (trap_exit), to any ring */
void vm_exception(struct trapframe *tf);
void vm_return(struct trapframe *tf);

extern u32 cpu_cr4_bits;        /* CR4 bits that stick (0 without CR4) */
extern int cpu_has_cr4;

/* kernel/dpmi: traps from a DPMI client (ring 3), and the way back to it */
void dpmi_trap(struct trapframe *tf);
void dpmi_return(struct trapframe *tf);
int dpmi_db_hit(void);                  /* kernel/dpmi: a #DB was a client watchpoint (noted) */

/* stubs.S */
u32 try_rd_cr4(u32 *v);
u32 try_wr_cr4(u32 v);
u32 try_rd32(u32 addr, u32 *v);
u32 try_wr8(u32 addr, u32 v);
u32 ucopy(void *dst, const void *src, u32 n);   /* 0, or 1 + the vector of a fault */
u32 nest_enter(struct trapframe *f, u32 *save);
void nest_leave(u32 saved_esp, u32 value) __attribute__((noreturn));
void frame_resume(struct trapframe *f) __attribute__((noreturn));
void leave_to_loader(u32 code, u32 loader_cr3) __attribute__((noreturn));

#endif
