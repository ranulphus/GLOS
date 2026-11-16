/* The DOS server (kernel/dos/dos.c): INT 21h calls for the kernel's users,
 * made by the resident stub while it is idle. */
#ifndef K_DOS_H
#define K_DOS_H
#include "types.h"

struct waitq;

#define DOS_MAX_CALLS 4
#define DOS_XFER 0xFFFF                         /* as DS or ES: the transfer buffer's segment */

enum { DR_IDLE, DR_QUEUED, DR_RUNNING, DR_DONE };

struct dos_regs {
    u16 ax, bx, cx, dx, si, di, ds, es, flags;
};

struct dos_req {
    u32 n;                                      /* calls to make, in order, until one sets CF */
    struct dos_regs r[DOS_MAX_CALLS];           /* in: the registers; out: what came back */
    const u8 *in;                               /* copied into the transfer buffer at in_at first */
    u32 in_len, in_at;
    u8 *out;                                    /* filled from the transfer buffer at out_at after */
    u32 out_len, out_at;
    u32 ran;                                    /* calls made */
    u16 error;                                  /* the failing call's AX (a DOS error), 0 if none */
    volatile u8 state;                          /* DR_* */
    struct dos_req *next;
    struct waitq *waiter;                       /* woken at its end (dos_call); NULL: the ssh thread is told */
    const volatile u8 *gone;                    /* (with via) nonzero there: that program has gone, the request is
                                                   refused */
    void *via;                                  /* who makes its calls: 0 the resident stub; else the COMMAND.COM of
                                                   GLOS's (kernel/shell/cmdcom.c) whose session's request it is. A
                                                   file a session has open is a handle of the process that opened
                                                   it: one executor makes all of a session's calls */
};

/* The ssh thread's side. */
void dos_init(void (*kick)(void));              /* kick: a request has finished */
void dos_hold(void);                            /* a user (an SFTP session) begins ... */
void dos_release(void);                         /* ... and ends: the buffer goes with the last */
int dos_post(struct dos_req *q);                /* queue it; -1 without a DOS server (not headless) */
void dos_vm_gone(void *via);                    /* (the VM's thread) the program that made via's calls has gone: its
                                                   requests end with an error */
int dos_call(struct dos_req *q);                /* for a thread of its own (a shell's session): made and waited
                                                   for. 0 (its DOS error, if any, in q->error), or -1 as dos_post */
u32 dos_xfer_size(void);                        /* the transfer buffer's size (32 KB, or less) */

/* The VM thread's side (trap context). */
int dos_vm_pending(void);                       /* a request (or a free) waits for the idle stub */
int dos_vm_next(int ran, u32 regs_lin);         /* ran: NEXT 3 came back; 1: make the call in regs_lin */
int dos_vm_pending_for(void *via);              /* ... for a COMMAND.COM of GLOS's: a request of its session's */
int dos_vm_next_for(void *via, int ran, u32 regs_lin, u16 seg, u32 paras);    /* (seg, paras: via's own buffer) */

#endif
