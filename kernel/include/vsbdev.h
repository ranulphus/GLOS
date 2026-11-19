/* GLOS's virtual Sound Blaster in the system VM (kernel/vm/vsbdev.c; docs/v1/vsb.md): the card of vsb.c and the
 * DMA channels of vdma.c behind the ports a DOS program uses, its interrupt in the virtual interrupt
 * controller, the program's memory under its DMA channel, and what it plays into GLOS's mixer. */
#ifndef K_VSBDEV_H
#define K_VSBDEV_H
#include "types.h"

struct bootinfo;

void vsbdev_init(const struct bootinfo *bi);    /* once the mixer has its card and GLOS.CFG is read: on, unless
                                                   [sound] virtual = off or the mixer has no card */
int vsbdev_on(void);                            /* programs have it now (not: a session in direct mode has the screen,
                                                   and the real card with it) */
const char *vsbdev_blaster(void);               /* BLASTER for programs ("A220 I5 D1 H5 T6"); 0: not theirs now */
int vsbdev_port(u16 port);                      /* one of the ports it answers (the card's, the DMA controllers') */
u8 vsbdev_in(u16 port);
void vsbdev_out(u16 port, u8 val);
void vsbdev_mix(s32 *acc, u32 frames, u32 rate);        /* the mixer's period (interrupt time): what it plays, added */
void vsbdev_leave(void);                        /* GLOS is leaving: off, so that the mixer lets the real card go */
void vsbdev_session_end(void);                  /* a session has ended: the card as after a reset, and a line in
                                                   the log of what it played */

#endif
