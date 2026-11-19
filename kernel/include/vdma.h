/* The system VM's virtual 8237A channels (docs/v1/vsb.md): the DMA channels GLOS's virtual Sound Blaster plays
 * from. A program sets such a channel up as it would the chip's (address, count, page, mode, mask), and the
 * virtual card takes the bytes from the program's memory itself (vdma_take): no real transfer is made, and the
 * real chip's channel of that number, which GLOS's own sound driver may be using, is never touched.
 *
 * Pure state and no I/O, so tests/host/vdma_test.c runs it on the host. The other channels are the real chip's
 * (the floppy's): what a program writes for them is passed on (kernel/vm/vdev.c), and ports that all of a
 * controller's channels share are passed on with the virtual channels' part taken out. */
#ifndef K_VDMA_H
#define K_VDMA_H
#include "types.h"

struct vdma_chan {
    u16 base_addr, base_count;          /* as written: in bytes (channels 0 to 3) or words (5 to 7) */
    u16 cur_addr, cur_count;            /* as they stand: the count is one less than what is left */
    u8 page;
    u8 mode;                            /* the mode register's byte: bit 4 auto-initialise, bits 2-3 the direction */
    u8 masked;
    u8 tc;                              /* it reached its terminal count since the status was read */
};

struct vdma {
    struct vdma_chan ch[8];
    u8 ff[2];                           /* each controller's byte flip-flop (0: the low byte next) */
    u8 virt;                            /* bit n: channel n is virtual */
    u8 real_masked;                     /* bit n (a virtual channel): the real chip's channel n is to stay masked
                                           (GLOS's sound driver is not using it) */
};

/* What is to be written to the real chip for a program's write, in order (vdma_out fills it). */
struct vdma_real {
    int n;
    u16 port[6];
    u8 val[6];
};

void vdma_reset(struct vdma *d, u8 virt, u8 real_masked);       /* every virtual channel masked, nothing set */
int vdma_port(u16 port);                /* 1: one of the two controllers' ports or a page register */

/* A program's write: what of it goes to the real chip (possibly nothing) is in *real. */
void vdma_out(struct vdma *d, u16 port, u8 val, struct vdma_real *real);

/* A program's read. 1: *val is the answer (a virtual channel's register). 0: the real chip's port is read.
   2: the real chip's port is read, and *val, with *mask the bits that are the virtual channels', takes their
   place in it (a controller's status). */
int vdma_in(struct vdma *d, u16 port, u8 *val, u8 *mask);

/* The virtual card's side. vdma_take: up to `bytes` of the transfer under way on channel ch: how many there
   are (0: the channel is masked, or done), *phys where they are; the channel moves on by them, and at its
   terminal count starts again (auto-initialise) or is masked. A piece ends at the end of the transfer and at
   the end of the channel's 64 KB (128 KB) of addresses, so a caller takes pieces until it has enough. */
u32 vdma_take(struct vdma *d, int ch, u32 bytes, u32 *phys);
int vdma_running(const struct vdma *d, int ch);        /* a virtual channel, not masked */

#endif
