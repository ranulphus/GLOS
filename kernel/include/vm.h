/* The system VM (supervisor.md §9, §10, §16): DOS and its programs in
 * virtual-8086 mode at IOPL 0 under the kernel. M2 runs it from trap
 * context alone (no threads yet) and without VME: every IOPL-sensitive
 * instruction and software INT traps to the monitor in v86.c. */
#ifndef K_VM_H
#define K_VM_H
#include "arch.h"
#include "mm.h"
#include "sched.h"
#include "vpic.h"

#define VM_STACK_PAGES 2                /* RM_SLOTS (dpmi.h): pages for the host's real-mode stacks */

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
    u8 mode, leds;                      /* the BIOS video mode and the lock bits (M4e) */
};

struct vm {
    struct bootinfo *bi;
    u16 loader_cs, loader_psp;
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
    u8 idle_cmd;                        /* the HLT waited in is a COMMAND.COM of GLOS's (shell/cmdcom.c) */
    const char *kill_reason;            /* hotkey, agent, priv or crash (M4e) */
    u32 kill_since;
    /* PCI configuration mechanism 1 */
    u32 pci_addr;
    u8 cf9;
    /* the vif-stuck watchdog */
    u32 stuck_since;
    u32 stuck_n;                        /* ... the timer's interrupts that have come since */
    u8 stuck_warned;
    u8 bios_slow;                       /* a BIOS call of GLOS's own that keeps interrupts off for long (the monitor's
                                           EDID by DDC, 4F15h): not a program's, so not warned of (gfx/display.c) */
    /* counts for the leave line */
    u32 n_gp, n_int, n_irq;
    /* the VM thread and its waits */
    struct thread *thread;
    struct waitq waitq;
    u32 wait_deadline;
    u8 waiting;
    u8 idle_agent;                      /* the halt is the stub's: an agent command ends it too */
    u8 notice_due;                      /* the idle screen's notice has something new to say (vcon.c) */
    u8 reboot_req;                      /* Ctrl-Alt-Del at the idle screen: the PC restarts */
    u8 shell_state;                     /* as the shell: 0 start, 1 AUTOEXEC.BAT running, 2 the console */
    volatile u8 autoexec;               /* as the shell with GLOS's own (S4d): 1 AUTOEXEC.BAT is still to be run by
                                           the first session, 2 it runs, 0 done (or there is none) */
    /* the stub in a page of upper memory (docs/upper-memory.md) */
    u16 page_seg;                       /* its segment, 0: the stub is after the PSP */
    u32 page_frame;                     /* the frame mapped there */
    u16 stk_seg[VM_STACK_PAGES];        /* the DPMI host's real-mode stacks' pages (U3), a 4 KB slot each */
    u32 stk_frame[VM_STACK_PAGES];
    u32 nstk;
    u32 leave_code;                     /* what GLOS leaves with once the stub is down (NEXT 4) */
    u8 down_refused;                    /* DOS had no room for it: /RUN's GLOS stays */
};

extern struct vm vm;

/* Work for the VM thread from another one (a screenshot, M5): fn runs on it
   with interrupts off at a return to the program, where the program would
   take an interrupt (its virtual IF is on), or wherever it is once `patience`
   ticks have passed. The program is then stopped between two instructions,
   and fn may make real-mode calls (rm_call). fn returns 0, or 1 to be called
   again at a later return. One piece of work at a time. */
enum { VW_IDLE, VW_POSTED, VW_RUNNING, VW_DONE };
struct vm_work {
    int (*fn)(struct vm_work *w, struct trapframe *tf);
    void (*done)(void);                 /* after fn's 0, interrupts off: a wake for the poster */
    u32 patience;                       /* ticks */
    u32 posted;                         /* the tick it was posted at */
    volatile u8 state;                  /* VW_ */
};
int vm_post(struct vm_work *w);         /* 0, or -1: another is waiting */
int vm_unpost(struct vm_work *w);       /* 1: withdrawn; 0: it has run or is running */
int vm_work_due(void);                  /* posted work could run now */
int vm_work_running(void);
void vm_work_turn(struct trapframe *tf);        /* at a return to user mode */

void vm_start(struct bootinfo *bi);              /* sets up and creates the VM thread */
void vm_kick(void);                             /* the VM may have something to do: wake it */
struct trapframe *vm_frame(void);               /* the VM thread's V86 frame */
u32 vm_timer_state(char *buf, u32 max);         /* the PC's timer interrupt as the VM has it, in words (a note's) */
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
void vm_unclaim_irq(int irq);                   /* ... and the system VM's again (the SB16's, for a session) */
void vm_line_dos(int irq);                      /* (step D2) a line of GLOS's a DOS device shares: passed to the VM too */
void vm_lines_tick(void);                       /* ... its storm and stall guards, on the tick */
void vm_kmask(int irq, int masked);             /* a kernel line masked while its driver works */
void vm_reset_req(const char *source);
int vm_idle_headless(void);                     /* the stub waits, nothing runs and no desktop: the idle screen */
u16 vm_current_psp(void);

/* The system VM's low memory (0-10FFFFh), through the kernel's alias of its page table (VMLOW_BASE), so that it
   is the system VM's in every address space: a native machine's own first MB is at 0 only while its thread
   runs (docs/v1/native-dos.md, native-DOS sessions). */
static inline u8 *vm_ptr(u32 lin) { return (u8 *)(VMLOW_BASE + lin); }
static inline u32 vm_lin(u32 seg, u32 off) { return ((seg & 0xFFFF) << 4) + (off & 0xFFFF); }
static inline u8 vm_rd8(u32 lin) { return *vm_ptr(lin); }
static inline u16 vm_rd16(u32 lin) { return (u16)(vm_rd8(lin) | (vm_rd8(lin + 1) << 8)); }
static inline u32 vm_rd32(u32 lin) { return vm_rd16(lin) | ((u32)vm_rd16(lin + 2) << 16); }
static inline void vm_wr8(u32 lin, u8 v) { *vm_ptr(lin) = v; }
static inline void vm_wr16(u32 lin, u16 v) { *(volatile u16 *)vm_ptr(lin) = v; }  /* (one word write: a text cell) */

/* the low 8 and 16 bits of a register in a trapframe */
#define SET16(r, v) ((r) = ((r) & 0xFFFF0000u) | ((v) & 0xFFFFu))
#define SETLO(r, v) ((r) = ((r) & 0xFFFFFF00u) | ((v) & 0xFFu))
#define SETHI(r, v) ((r) = ((r) & 0xFFFF00FFu) | (((v) & 0xFFu) << 8))

/* vdev.c: the trapped ports */
void vdev_init(void);
/* Where things are (no drive is C: by name): the folder GLOS runs from, where its own files are ("C:\\GLOS";
   GLOS.CFG's, as GLOS.EXE found it); and the drive DOS started from, a capital letter: as DOS says it, else
   the drive its command interpreter is on (COMSPEC), else GLOS's own. */
void vm_glos_home(char *out, u32 room);
char vm_boot_drive(void);
void vdev_hide(u16 base, u16 len);              /* ports the kernel owns: FFh to the VM, writes dropped */
void vdev_unhide(u16 base);                     /* ... and the VM's again */
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
void vkbc_vmouse(int on);
int  vkbc_peek(u8 *b);                          /* the output buffer's byte (0: the mouse's) */
int  vkeyb_int09(void);                         /* vkeyb.c: INT 09h's ARPL, 1 if GLOS answered the key */
struct dos_regs86;
struct trapframe;
void mscdex_setup(unsigned seg, int n, const signed char *letters);     /* mscdex.c: GLOS's MSCDEX (N4) */
void mscdex_units(const signed char *letters);
int  vdos_cd_mount(const char *path, char *msg, unsigned room);     /* vdos.c: an image as a CD drive (C7): its
                                                   letter (0 A:), or -1 with msg */
int  vdos_cd_eject(int letter, char *msg, unsigned room);       /* 0, or -1 with msg */
int  vdos_cd_list(char *out, unsigned room);    /* the CD drives, a line each */
/* Disks that come and go (a USB stick; vdos.c): each FAT volume a letter, its sectors (512 bytes) the device's
   callbacks on a thread with interrupts on (they may sleep: vdos_disk_breathe in long waits). attach: the letters
   given (msg says them), 0 none, -1 with msg; detach refuses while a file is open there; gone: 15h at once. */
int  vdos_disk_attach(int (*read)(void *ctx, unsigned long lba, unsigned char *buf, unsigned count),
                      int (*write)(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count), void *ctx,
                      unsigned long sectors, char *msg, unsigned room);
int  vdos_disk_detach(void *ctx, char *msg, unsigned room);
void vdos_disk_gone(void *ctx);
void vdos_disk_breathe(void);
int  vdos_testdisk(const char *how, char *msg, unsigned room);    /* glos testdisk */
struct ems_regs;
int  ems_vm_init(void);                         /* emsvm.c: EMS for the system VM (natively): 0, or -1 off */
int  ems_vm_on(void);
void ems_vm_figures(u32 *total, u32 *free);     /* (MEM) its 16 KB pages, in all and free */
void ems_vm_call(struct ems_regs *r);
int  mscdex_int2f(struct dos_regs86 *r);
void mscdex_request(u32 r, int unit);
void mscdex_strategy(unsigned es, unsigned bx);
void mscdex_interrupt(void);
int  vdos_mscdex(struct trapframe *tf);         /* vdos.c: INT 2Fh 15xxh, 1 if answered */                       /* GLOS's INT 33h takes the mouse (native boots) */

/* vmouse.c: INT 33h of GLOS's own for DOS programs (native boots, vdos.c's vector) */
struct dos_regs86;
void vmouse_int33(struct dos_regs86 *r);
void vmouse_aux(u8 b, int wheel);               /* a PS/2 mouse byte (vkbc.c) */
void vmouse_move(int dx, int dy, int buttons);  /* an injected move (buttons -1: as they were) */
int  vmouse_on(void);
void vmouse_session_end(void);                  /* a session ended: the mouse's cursor taken off the screen */
int  vmouse_event(struct dos_regs86 *r, u32 *handler);  /* INT 74h's ARPL: the routine's call, if one is due */
void vmouse_event_done(void);                   /* ... and its return */
void vdos_mouse_irq(void);                      /* vdos.c: INT 74h to GLOS's ARPL, IRQ 12 unmasked */
void vkbc_direct(int on);                       /* a direct-mode session begins (1) or ends (0) */
void vkbc_typematic(u32 delay_ms, u32 rate);   /* the keyboard's repeat while the desktop has it (M9) */
void vkbc_desk(int on);                         /* the desktop has the keyboard and the mouse (1), or the VM again (0) */
void vkbc_leave(void);
void vkbc_trace(void);                          /* GLOS-KBC lines: its last 64 events (with a vif-stuck warning) */
int vkbc_key(u32 code, int down);               /* injected: a set 1 code (100h up: with E0); 0, or -1: no room */
int vkbc_vm_key(u32 code, int down);            /* ... for the VM's program, whatever has the keyboard */
int vkbc_key_busy(void);                        /* the app the keys go to is behind: they should wait a moment */
int vkbc_mouse(int dx, int dy, int buttons);    /* injected: 0; -1: no room; -2: no program has the mouse on */
int vkbc_mouse_wheel(int dx, int dy, int wheel, int buttons);   /* ... and the wheel's notches (+: away, up) */
int vkbc_room(void);                            /* bytes the input queue still takes */

/* int15.c, xms.c */
int vm_int15(struct trapframe *tf, u32 ret_ip); /* 1: emulated */
void vm_int15_tick(u32 now);
void xms_init(struct bootinfo *bi);
void xms_call(struct trapframe *tf);
void xms_figures(u32 *used_kb, u32 *free_kb);   /* (MEM) handed out, and left */
int phys_copy(u32 dst, u32 src, u32 len);       /* physical addresses, any overlap */
int phys_owned(u32 base, u32 len);              /* 1 if GLOS owns any of it */

void vm_int10_hook(int on);             /* (v86.c) a console job's INT 10h vector is GLOS's stub, or as it was again */
extern const struct trapframe *vdev_from;      /* (vdev.c) the instruction making the port access under way */

/* GLOS is DOS's shell (SHELL= in CONFIG.SYS) in one of two ways: with a COMMAND.COM of the DOS's under it, which
   runs AUTOEXEC.BAT and is the console (no agent: M3), or with the shell its own (S4d): the agent's mode, where
   the first session runs AUTOEXEC.BAT. */
#define vm_dos_shell() ((vm.bi->flags & (BI_F_SHELL | BI_F_AGENT)) == BI_F_SHELL)
#define vm_own_shell() ((vm.bi->flags & (BI_F_SHELL | BI_F_AGENT)) == (BI_F_SHELL | BI_F_AGENT))

#endif
