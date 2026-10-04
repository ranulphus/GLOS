/* The DPMI host (supervisor.md §12-§15; M4a). A DOS program in the system
 * VM switches to protected mode through INT 2Fh 1687h's entry and then runs
 * at ring 3 on the VM thread, in a context of its own: an address space, an
 * LDT and a virtual IDT. Its software interrupts arrive through DPL-3 gates
 * (as #GP for vectors 0-1Fh and 50h-5Fh), INT 31h is the API, and its
 * real-mode calls and reflected interrupts run nested on the same thread.
 *   host.c    contexts, the mode switch and the end, traps from ring 3,
 *             the virtual IDT and the trampolines
 *   deliver.c handlers the host calls: IRQs, INTs passed up, exceptions,
 *             real-mode callbacks, on the locked stack (M4b)
 *   ldt.c     descriptors and selectors
 *   mem.c     linear and physical memory, DOS memory
 *   rmcall.c  nested real-mode execution, 0300h-0302h, the raw switch
 *   int31.c   the INT 31h functions */
#ifndef K_DPMI_H
#define K_DPMI_H
#include "arch.h"

#define LDT_ENTRIES    8192
#define SEL_FIRST      16               /* 0000h's first index: selectors 04h-7Ch stay free for 000Dh */
#define RM_STACK_PARAS 0x80             /* 1687h's SI: the host's real-mode stack, 2 KB of the client's */
#define NRMCB          16
#define NSEGSEL        64               /* 0002h's selectors */
#define USER_BASE      0x00400000u      /* the user region (supervisor.md §4) */
#define USER_END       0xC0000000u
#define PHYS_WINDOW    0xE0000000u      /* 0800h: linear = physical from here to FFBFFFFFh */
#define LSTACK_SIZE    0x4000u          /* the locked host stack (§14.2) ... */
#define LSTACK_LIN     (USER_END - 0x10000u)    /* ... at the top of the user region, a hole below it */
#define NENTRY         32               /* handlers the host has called and not seen return */
#define EXC_NEST_MAX   5                /* exceptions inside exception handlers (CWSDPMI's rule) */

/* The trampoline page (SEL_TRAMP:offset), each entry a HLT that faults. */
#define TR_VEC       0x000u             /* + vector: the host's default PM handler for it */
#define TR_EXC       0x100u             /* + exception: the host's default exception handler */
#define TR_RAW       0x120u             /* 0306h: raw switch to real mode */
#define TR_SAVE      0x121u             /* 0305h: protected-mode state save/restore */
#define TR_VENDOR    0x122u             /* 0A00h "GLOS": the vendor API entry */
#define TR_RET       0x140u             /* + entry: a handler the host called returns (deliver.c) */
#define TR_COUNT     (TR_RET + NENTRY)

struct farptr {
    u32 off;
    u16 sel;
};

enum { BK_MEM, BK_PHYS, BK_HOST };

struct block {                          /* 0501h, 0800h mappings and the host's own */
    u32 handle, lin, size;              /* size: a whole number of pages */
    u8 kind;                            /* BK_PHYS: not ours to free; BK_HOST: no handle */
    struct block *next;
};

struct dosblk {                         /* 0100h */
    u16 seg, sel, nsel;
    struct dosblk *next;
};

struct rmcb {                           /* 0303h */
    u8 used;
    u16 stack_sel;                      /* DS for the handler: the real-mode stack */
    struct farptr pm, regs;
};

/* A handler the host called (deliver.c), until it returns to TR_RET + its
   index: what it interrupted, and how to go back. */
enum { PE_IRQ, PE_INT, PE_EXC, PE_RMCB };
struct pmentry {
    u8 kind;
    u8 switched;                        /* it moved to the locked stack */
    u8 vif;                             /* the virtual IF it interrupted */
    u8 frame10;                         /* PE_EXC: a 0212h handler, which returns through the 1.0 frame */
    struct trapframe *at;               /* the frame it ran in (the same for every trap meanwhile) */
    struct trapframe saved;             /* what it interrupted */
};

struct dpmi_ctx {
    u32 cr3;                            /* its page directory */
    u32 *ldt;                           /* LDT_ENTRIES descriptors (two dwords each) */
    u8 *ldt_used;
    u8 bits32;                          /* a 32-bit client */
    u16 psp, env_seg;                   /* real-mode segments */
    u16 psp_sel, env_sel;
    u16 rm_seg;                         /* the host's real-mode stack: the client's 1687h block */
    u32 rm_sp;
    struct farptr vidt[256];            /* 0204h/0205h; sel 0: the host's own handler */
    struct farptr exc[32];              /* 0202h/0203h, 0210h/0211h; sel 0: the host's own */
    u8 exc10[32];                       /* set by 0212h: the handler returns through the 1.0 frame */
    u32 rm_prev[256];                   /* the real-mode vector that a PM hook or an RMCB took over (§14.5) */
    struct rmcb rmcb[NRMCB];
    u16 lsel;                           /* the locked stack */
    u32 lstack_use;                     /* entries that moved onto it */
    struct pmentry pe[NENTRY];
    u32 npe, exc_depth;
    u32 cr2;                            /* the last page fault's address */
    u8 ending;                          /* a crash report is being written: no more handlers */
    struct { u16 seg, sel; } segsel[NSEGSEL];   /* 0002h */
    struct block *blocks;
    u32 next_handle;
    u32 lin_floor;                      /* the first 0501h block: the rest go above it (§12.3) */
    u32 frames;                         /* physical frames held */
    struct dosblk *dosblks;
};

extern struct dpmi_ctx *dctx;           /* the context, or 0 */

/* host.c */
void dpmi_init(void);
void dpmi_1687(struct trapframe *tf);   /* INT 2Fh AX=1687h from V86 mode */
int dpmi_v86_bp(struct trapframe *tf);  /* #UD at one of the stub's ARPLs: 1 if it was the host's */
void dpmi_dos_exit(struct trapframe *tf);       /* V86 INT 21h 4Ch/00h: the client's program may be ending */
void dpmi_to_pm(struct trapframe *tf, u16 cs, u32 eip, u16 ss, u32 esp, u16 ds, u16 es);
void dpmi_to_v86(struct trapframe *tf, u16 cs, u32 ip, u16 ss, u32 sp, u16 ds, u16 es);
void dpmi_iret(struct trapframe *tf);   /* pop an interrupt frame from the client's stack */
void dpmi_unimpl(struct trapframe *tf, const char *what);
void dpmi_end(struct trapframe *tf, u8 code, int killed);       /* the client ends: its context, then DOS */
u32 sp_of(const struct trapframe *tf);  /* ESP, or SP on a 16-bit stack */
void set_sp(struct trapframe *tf, u32 sp);
void pm_soft_int(struct trapframe *tf, u8 n, u32 next); /* INT n in protected mode, through the virtual IDT */

/* deliver.c */
int dpmi_irq(struct trapframe *tf, u8 vec);     /* an IRQ to the client's PM handler: 1 if it has one */
int dpmi_passup(struct trapframe *tf, u8 n, u32 next);  /* V86 INT 1Ch/23h/24h: 1 if a PM handler takes it */
void dpmi_exception(struct trapframe *tf);      /* a fault in protected mode: the client's handler, or the end */
void dpmi_rmcb(struct trapframe *tf, u32 i);    /* V86 code called RMCB i */
void dpmi_entry_return(struct trapframe *tf, u32 i);    /* SEL_TRAMP:TR_RET + i */
void dpmi_exc_default(struct trapframe *tf, u32 v);     /* SEL_TRAMP:TR_EXC + v: chained to the host's */
void dpmi_exc_unhandled(struct trapframe *tf, u32 v, u32 err);  /* no client handler: the default action */
u32 rm_vector(u8 vec);                  /* the IVT's vector, past the client's own RMCB (§14.5) */
void crash_report(struct trapframe *tf, const char *why);       /* kernel/dbg/crash.c */

/* ldt.c */
void ldt_init(struct dpmi_ctx *c);
int ldt_alloc(u32 n);                   /* the first of n free consecutive indices, or -1 */
u16 ldt_new(u32 base, u32 limit, u8 access, u8 flags);  /* 0 if the LDT is full */
int ldt_free(u16 sel);
int ldt_valid(u16 sel);                 /* one of the client's allocated selectors */
u32 sel_base(u16 sel);
u32 sel_limit(u16 sel);                 /* in bytes, granularity applied */
void sel_set_base(u16 sel, u32 base);
void sel_set_limit(u16 sel, u32 limit);
int sel_set_desc(u16 sel, u32 lo, u32 hi);      /* validated (DPL 3, code or data) */
void sel_get_desc(u16 sel, u32 *lo, u32 *hi);
int sel_lin(u16 sel, u32 off, u32 len, u32 *lin);       /* sel:off..+len inside the limit: its linear address */
int user_rd(u16 sel, u32 off, void *dst, u32 n);        /* 0, or -1 if outside or not mapped */
int user_wr(u16 sel, u32 off, const void *src, u32 n);

/* mem.c */
void lin_free_all(struct dpmi_ctx *c);
int lin_alloc(u32 size, struct block **out);    /* 0, or a DPMI error */
int lin_free(u32 handle);
int lin_resize(u32 handle, u32 size, struct block **out);
void lin_info(u32 *out12);              /* 0500h's twelve dwords */
int phys_map(u32 phys, u32 size, u32 *lin);
int phys_unmap(u32 lin);
int lin_host(u32 lin, u32 size);        /* host memory in the context: committed, no handle */
int page_attr(u32 handle, u32 off, u32 n, u16 *attr, int set, u32 *done);      /* 0506h/0507h */

/* rmcall.c */
struct rmregs {                         /* 0300h's structure ([DPMI0.9]) */
    u32 edi, esi, ebp, reserved, ebx, edx, ecx, eax;
    u16 flags, es, ds, fs, gs, ip, cs, sp, ss;
} __attribute__((packed));
enum { RM_INT, RM_FAR, RM_IRET };
int rm_call(struct trapframe *from, struct rmregs *r, int kind, u8 vec, const u16 *words, u32 nwords);
                                        /* from: the PM frame that asks (or 0); 0, or -1 */
struct trapframe *rm_pm_caller(void);   /* the PM frame the innermost real-mode call came from, or 0 */
int rm_nest_return(struct trapframe *tf);       /* #UD at the nest breakpoint: 1 if it was one */
void rm_reflect(struct trapframe *tf, u8 vec);  /* a PM software INT to real mode, general registers through */
void rm_irq(struct trapframe *tf, u8 vec);     /* an IRQ while the client runs: its real-mode handler, nested */
void rm_raw_to_pm(struct trapframe *tf);        /* the stub's raw-switch ARPL */
void rm_raw_to_rm(struct trapframe *tf);        /* SEL_TRAMP:TR_RAW */
int rm_nesting(void);
void rm_unwind(struct trapframe *tf) __attribute__((noreturn));  /* the client ended inside a nested call */

/* int31.c */
void int31(struct trapframe *tf);

#endif
