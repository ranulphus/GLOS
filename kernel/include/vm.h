/* The system VM (supervisor.md §9, §10, §16): DOS and its programs in
 * virtual-8086 mode at IOPL 0 under the kernel. M2 runs it from trap
 * context alone (no threads yet) and without VME: every IOPL-sensitive
 * instruction and software INT traps to the monitor in v86.c. */
#ifndef K_VM_H
#define K_VM_H
#include "arch.h"
#include "sched.h"
#include "vpic.h"

struct bootinfo;

#define FL_CF    0x00000001u
#define FL_ZF    0x00000040u
#define FL_ARITH 0x00000CD5u            /* CF PF AF ZF SF DF OF */
#define FL_TF    0x00000100u
#define FL_IF    0x00000200u
#define FL_HI    0x00007000u            /* IOPL and NT */
#define FL_NT    0x00004000u            /* virtual in V86 mode, as the program set it */
#define FL_IOPL3 0x00003000u
#define FL_VM    0x00020000u
#define FL_AC    0x00040000u
#define FL_VIF   0x00080000u
#define FL_VIP   0x00100000u
#define FL_ID    0x00200000u

#define SNAP_LEVELS 8

/* What a program found when DOS started it (INT 21h 4B00h): a kill puts it back. */
struct vm_snap {
    u16 parent_psp;                     /* the PSP that EXECed it; 0 = slot unused */
    u32 ivt[256];
    struct vpic pic;
    u8 a20, rtc_a, rtc_b, kbc_cmd;
};

struct vm {
    struct bootinfo *bi;
    u16 loader_cs, loader_psp;
    u8 vif;                             /* the virtual interrupt flag (EFLAGS.VIF in V86 mode with VME) */
    u8 vme;                             /* CR4.VME on: CLI, STI, PUSHF, POPF, IRET and most INTs in hardware */
    u8 direct;                          /* a direct-mode session: IOPL 3, the real IF (supervisor.md §9.7) */
    u8 direct_rtc;                      /* user entries since the RTC was last checked (direct mode) */
    u32 vflags_hi;                      /* NT */
    struct vpic pic;
    u16 phys_mask;                      /* what the physical PIC's IMR holds */
    u8 a20;
    /* the virtual RTC (vdev.c) */
    u8 rtc_index, rtc_nmi, rtc_a, rtc_b, rtc_c;
    u32 rtc_acc;
    /* INT 15h 86h and 83h (int15.c) */
    u32 wait_key, wait_until;
    u32 event_lin, event_until;
    /* DOS */
    u32 indos, sda;                     /* linear addresses, 0 unknown */
    u32 int2f_prev;                     /* INT 2Fh's vector under GLOS's own (v86.c vm_int2f) */
    struct vm_snap snap[SNAP_LEVELS];
    u8 kill_req;
    u32 kill_since;
    /* PCI configuration mechanism 1 */
    u32 pci_addr;
    u8 cf9;
    /* the vif-stuck watchdog */
    u32 stuck_since;
    u8 stuck_warned;
    /* counts for the leave line */
    u32 n_gp, n_int, n_irq;
    /* the VM thread and its waits */
    struct thread *thread;
    struct waitq waitq;
    u32 wait_deadline;
    u8 waiting;
    u8 idle_agent;                      /* the halt is the stub's: an agent command ends it too */
    u8 shell_state;                     /* as the shell: 0 start, 1 AUTOEXEC.BAT running, 2 the console */
};

extern struct vm vm;

void vm_start(struct bootinfo *bi);              /* sets up and creates the VM thread */
void vm_kick(void);                             /* the VM may have something to do: wake it */
struct trapframe *vm_frame(void);               /* the VM thread's V86 frame */
void selftest_report(void);                     /* core/main.c, /SELFTEST */
void vm_exception(struct trapframe *tf);        /* a CPU exception from V86 mode */
void vm_return(struct trapframe *tf);           /* last thing before IRET to V86 mode */
void vm_int(struct trapframe *tf, u8 n, u32 ret_ip);    /* real-mode INT n through the IVT */
void vm_kill_now(struct trapframe *tf);         /* the kill (Ctrl-Alt-Shift-Esc's), now */
int vm_restore_child(void);                     /* after DOS aborted the current PSP's child: as a kill leaves it */
void vm_idle(u32 until);                        /* block until a deliverable IRQ, a kill or tick `until` (0: none) */
void vm_set_a20(int on);
void vm_sync_mask(void);                        /* the physical IMR from the virtual one */
void vm_direct(int on);                         /* a direct-mode session begins (1) or ends (0) */
void vm_claim_irq(int irq, void (*fn)(struct trapframe *));    /* the line becomes the kernel's */
void vm_kmask(int irq, int masked);             /* a kernel line masked while its driver works */
void vm_reset_req(const char *source);
u16 vm_current_psp(void);

/* low memory (identity-mapped, 0-10FFFFh) */
static inline u8 *vm_ptr(u32 lin) { u8 *p; __asm__("" : "=r"(p) : "0"(lin)); return p; }
static inline u32 vm_lin(u32 seg, u32 off) { return ((seg & 0xFFFF) << 4) + (off & 0xFFFF); }
static inline u8 vm_rd8(u32 lin) { return *vm_ptr(lin); }
static inline u16 vm_rd16(u32 lin) { return (u16)(vm_rd8(lin) | (vm_rd8(lin + 1) << 8)); }
static inline u32 vm_rd32(u32 lin) { return vm_rd16(lin) | ((u32)vm_rd16(lin + 2) << 16); }
static inline void vm_wr8(u32 lin, u8 v) { *vm_ptr(lin) = v; }

/* the low 8 and 16 bits of a register in a trapframe */
#define SET16(r, v) ((r) = ((r) & 0xFFFF0000u) | ((v) & 0xFFFFu))
#define SETLO(r, v) ((r) = ((r) & 0xFFFFFF00u) | ((v) & 0xFFu))
#define SETHI(r, v) ((r) = ((r) & 0xFFFF00FFu) | (((v) & 0xFFu) << 8))

/* vdev.c: the trapped ports */
void vdev_init(void);
void vdev_hide(u16 base, u16 len);              /* ports the kernel owns: FFh to the VM, writes dropped */
u32 vdev_in(u16 port, int width);
void vdev_out(u16 port, u32 val, int width);
void vdev_tick(struct trapframe *tf, u8 c);     /* every kernel tick, with RTC register C */
void vdev_leave(void);

/* vkbc.c: the 8042 */
void vkbc_init(void);
u8 vkbc_in(u16 port);
void vkbc_out(u16 port, u8 val);
void vkbc_irq(struct trapframe *tf);
void vkbc_tick(void);
u8 vkbc_cmd(void);
void vkbc_restore(u8 cmd);
void vkbc_leave(void);

/* int15.c, xms.c */
int vm_int15(struct trapframe *tf, u32 ret_ip); /* 1: emulated */
void vm_int15_tick(u32 now);
void xms_init(struct bootinfo *bi);
void xms_call(struct trapframe *tf);
int phys_copy(u32 dst, u32 src, u32 len);       /* physical addresses, any overlap */
int phys_owned(u32 base, u32 len);              /* 1 if GLOS owns any of it */

#endif
