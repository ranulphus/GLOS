/* GLOS's virtual Sound Blaster (docs/v1/vsb.md; the owner, 2026-11-06 and 2026-11-19): the card a DOS program
 * in a session finds at BLASTER's ports. It is a Sound Blaster 16's DSP (version 4.05) and mixer chip, with the
 * older cards' commands under it (the 2.0's and the Pro's: time constants, single and auto-initialised blocks,
 * high speed, the Pro's stereo). What the program plays is taken from its memory through a DMA channel (the
 * virtual 8237's, vdma.h) and comes out as 16-bit stereo at the rate GLOS's mixer plays at (vsb_mix), so it
 * sounds on whatever card GLOS has a driver for.
 *
 * Pure state and no I/O, so tests/host/vsb_test.c runs it on the host: the DMA's bytes and the interrupt line
 * are the host's two functions. */
#ifndef K_VSB_H
#define K_VSB_H
#include "types.h"

struct vsb_host {
    void *ctx;
    /* The next bytes of the transfer on DMA channel ch, into buf: how many came (fewer than asked at the
       transfer's end; 0: the channel is masked, or done). */
    u32 (*dma)(void *ctx, int ch, u8 *buf, u32 bytes);
    void (*irq)(void *ctx);                     /* its interrupt line, raised (an edge) */
};

#define VSB_Q 4096                              /* frames in hand at the most (a power of two) */

struct vsb {
    struct vsb_host h;
    u8 irq, dma8, dma16;                        /* as BLASTER says them: I, D, H (H may be D's) */
    /* the DSP's ports */
    u8 reset;                                   /* a 1 was written to the reset port */
    u8 cmd, need, have, arg[3];                 /* a command, and its bytes as they come */
    u8 out[64], out_n, out_at;                  /* what the program has to read */
    u8 speaker, test;
    u8 irq8, irq16;                             /* asked for, not yet acknowledged (the mixer's register 82h) */
    /* what plays */
    u8 active, paused, auto_init, stop_after;   /* stop_after: auto-initialise ends with the block under way */
    u8 xfer;                                    /* the program's bytes are still to come (active without it: what
                                                   is in hand plays out) */
    u8 wide, stereo, sign, silence;             /* 16 bits; two channels; signed; a block of silence (80h) */
    u32 rate;                                   /* frames a second, as it plays */
    u32 set_rate;                               /* as the program set it: 41h's, or the time constant's (bytes a second) */
    u32 block, left;                            /* a block's bytes; those of this block still to come */
    u32 block_set;                              /* 48h's */
    /* The program's frames in hand, taken from its DMA channel a period of the mixer's before they sound:
       a block's interrupt comes while its last frames still play, and a program that asks for its next block
       from that interrupt is heard without a gap. From the program's rate to the mixer's: 16.16 of the way
       from the first frame in hand to the one after it. */
    s16 q[VSB_Q][2];
    u32 q_at, q_n;
    u32 frac;
    u8 buf[64];                                 /* the DMA's bytes not yet made frames */
    u32 buf_n, buf_at;
    u32 begun, fresh;                           /* blocks the program has asked for, and those of them that began
                                                   afresh, not on from the one before (never put back: vsbdev.c's
                                                   log) */
    /* the mixer chip */
    u8 mix_index, mix[256];
    u32 played;                                 /* frames given to the mixer since the reset (the suites') */
};

void vsb_init(struct vsb *s, const struct vsb_host *h, int irq, int dma8, int dma16);
u8 vsb_in(struct vsb *s, unsigned reg);         /* a port of the card: the port less its base (0 to 0Fh) */
void vsb_out(struct vsb *s, unsigned reg, u8 val);
/* A period of the mixer's: what the card plays now, `frames` frames of 16-bit stereo at `rate` a second, added
   into acc (left, right, left ...) at the mixer chip's volumes. 1 if anything played. */
int vsb_mix(struct vsb *s, s32 *acc, u32 frames, u32 rate);
int vsb_playing(const struct vsb *s);

#endif
