/* The cable link (kernel/net/slip.c; docs/cable.md): IP over a serial cable to another computer, by SLIP. */
#ifndef K_SLIP_H
#define K_SLIP_H
#include "types.h"

/* The net thread's. role: BI_CABLE_SERVER or BI_CABLE_CLIENT (which of the link's two addresses is this PC's);
   com 1 to 4; baud 0: 115200. alone: there is no network card, so everything goes by the cable. 0, or
   uart_open()'s error. */
int slip_start(u32 role, u32 com, u32 baud, int alone);
void slip_stop(void);
void slip_turn(void);                   /* what has come in, to lwIP */
int slip_up(void);
int slip_address(char ip[16]);          /* 1 and this PC's address on the link, or 0 */
u32 slip_stat(char *buf, u32 max);      /* glos net's line for it ("" when off) */
void slip_leave(void);                  /* GLOS leaving (any context, interrupts off): the port as it was found */

#endif
