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
#define SEL_DATA16   0x40

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
void set_trap_handler(int vec, int (*fn)(struct trapframe *));    /* returns 1 if handled */
void panic(const char *why, struct trapframe *tf) __attribute__((noreturn));

extern u32 cpu_cr4_bits;        /* CR4 bits that stick (0 without CR4) */
extern int cpu_has_cr4;

/* stubs.S */
u32 try_rd_cr4(u32 *v);
u32 try_wr_cr4(u32 v);
u32 try_rd32(u32 addr, u32 *v);
u32 try_wr8(u32 addr, u32 v);
void leave_to_loader(u32 code, u32 loader_cr3) __attribute__((noreturn));

#endif
