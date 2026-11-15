/* The parallel link's wire (kernel/drv/plip.c; docs/v1/cable-network.md, N2): Ethernet frames over a LapLink
 * cable, by PLIP. The net thread's side; the wire has a thread of its own. */
#ifndef K_PLIP_H
#define K_PLIP_H
#include "types.h"

struct plip_counts {
    u32 rx, tx;                         /* frames each way */
    u32 rx_errors, tx_errors;           /* a packet that stopped part way, or whose sum was wrong */
    u32 rx_dropped;                     /* received with the ring full */
    u32 tx_dropped;                     /* given up: nobody answered at the other end */
    u32 tx_full;                        /* refused: the ring was full */
    u32 collisions;                     /* both ends began a packet at once */
    u32 irqs;
};

int plip_open(u32 lpt, void (*kick)(void));     /* LPT1 to 3 taken from DOS: 0; -1 no port there; -2 in use.
                                                   kick: a frame has come (any context) */
void plip_close(void);                  /* the port DOS's again (any context) */
int plip_send(const u8 *frame, u32 len);        /* a frame to go: 0, or -1 (the ring is full, or the link is off) */
u32 plip_recv(u8 *frame, u32 room);     /* a frame that came: its length, 0 none */
int plip_info(u32 *lpt, u16 *base, u32 *irq, struct plip_counts *c);    /* 0, or -1 when off */

#endif
