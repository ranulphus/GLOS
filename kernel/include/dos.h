/* The DOS server (kernel/dos/dos.c): INT 21h calls for the kernel's users,
 * made by the resident stub while it is idle. */
#ifndef K_DOS_H
#define K_DOS_H
#include "types.h"

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
};

/* The ssh thread's side. */
void dos_init(void (*kick)(void));              /* kick: a request has finished */
void dos_hold(void);                            /* a user (an SFTP session) begins ... */
void dos_release(void);                         /* ... and ends: the buffer goes with the last */
int dos_post(struct dos_req *q);                /* queue it; -1 without a DOS server (not headless) */
u32 dos_xfer_size(void);                        /* the transfer buffer's size (32 KB, or less) */

/* The VM thread's side (trap context). */
int dos_vm_pending(void);                       /* a request (or a free) waits for the idle stub */
int dos_vm_next(int ran, u32 regs_lin);         /* ran: NEXT 3 came back; 1: make the call in regs_lin */

#endif
