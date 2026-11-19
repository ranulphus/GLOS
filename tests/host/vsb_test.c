/* Host test for GLOS's virtual Sound Blaster (kernel/vm/vsb.c) with the virtual 8237 under it (vdma.c), as the
 * port sequences a game's sound code sends: how a card is found (the reset, the version, the identification
 * and test bytes, an interrupt asked for, the mixer's interrupt and DMA registers); a single 8-bit block by a
 * time constant; single blocks one after another, each asked for from the interrupt of the one before, with no
 * gap between them; a rate half the mixer's, heard between its samples; auto-initialised blocks, their
 * interrupts, and their end asked for; the SB16's 16-bit stereo; pause; the Pro's stereo; the mixer's volumes;
 * a block of silence; a DMA channel masked under it; a reset while it plays.
 *
 * "Memory" is an array the DMA channels point into; the mixer's period is a call of vsb_mix. The card takes
 * the program's bytes a period before they sound (what a period plays, and as much again, is in hand): a
 * block's interrupt comes that much before its last frame is heard. */
#include <stdio.h>
#include <string.h>
#include "vdma.h"
#include "vsb.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static u8 mem[0x60000];
static struct vdma d;
static struct vsb s;
static int irqs;
static s32 acc[2 * 512];

static u32 dma(void *ctx, int ch, u8 *buf, u32 bytes)
{
    u32 got = 0, phys = 0, n;
    (void)ctx;
    while (got < bytes && (n = vdma_take(&d, ch, bytes - got, &phys)) != 0) {
        memcpy(buf + got, mem + phys, n);
        got += n;
    }
    return got;
}

static void irq(void *ctx)
{
    (void)ctx;
    irqs++;
}

static void dsp(u8 b) { vsb_out(&s, 0xC, b); }
static u8 rd(void) { return vsb_in(&s, 0xA); }

static void mixer(u8 index, u8 v)
{
    vsb_out(&s, 0x4, index);
    vsb_out(&s, 0x5, v);
}

static u8 mixer_get(u8 index)
{
    vsb_out(&s, 0x4, index);
    return vsb_in(&s, 0x5);
}

static void reset(void)
{
    vsb_out(&s, 0x6, 1);
    vsb_out(&s, 0x6, 0);
}

/* A DMA channel set up as a game does it: 8 bits (channel 1) or 16 (channel 5). */
static void dma_setup(int ch, int auto_init, u32 phys, u32 bytes)
{
    struct vdma_real real;
    u8 mode = (u8)(0x48 | (auto_init ? 0x10 : 0) | (ch & 3));
    if (ch < 4) {
        vdma_out(&d, 0x0A, (u8)(4 | ch), &real);
        vdma_out(&d, 0x0C, 0, &real);
        vdma_out(&d, 0x0B, mode, &real);
        vdma_out(&d, (u16)(ch * 2), (u8)phys, &real);
        vdma_out(&d, (u16)(ch * 2), (u8)(phys >> 8), &real);
        vdma_out(&d, 0x83, (u8)(phys >> 16), &real);
        vdma_out(&d, (u16)(ch * 2 + 1), (u8)(bytes - 1), &real);
        vdma_out(&d, (u16)(ch * 2 + 1), (u8)((bytes - 1) >> 8), &real);
        vdma_out(&d, 0x0A, (u8)ch, &real);
    } else {
        u32 words = bytes / 2;
        vdma_out(&d, 0xD4, (u8)(4 | (ch & 3)), &real);
        vdma_out(&d, 0xD8, 0, &real);
        vdma_out(&d, 0xD6, mode, &real);
        vdma_out(&d, (u16)(0xC0 + (ch & 3) * 4), (u8)(phys >> 1), &real);
        vdma_out(&d, (u16)(0xC0 + (ch & 3) * 4), (u8)(phys >> 9), &real);
        vdma_out(&d, 0x8B, (u8)(phys >> 16), &real);
        vdma_out(&d, (u16)(0xC2 + (ch & 3) * 4), (u8)(words - 1), &real);
        vdma_out(&d, (u16)(0xC2 + (ch & 3) * 4), (u8)((words - 1) >> 8), &real);
        vdma_out(&d, 0xD4, (u8)(ch & 3), &real);
    }
}

static int mix(u32 frames, u32 rate)
{
    memset(acc, 0, sizeof acc);
    return vsb_mix(&s, acc, frames, rate);
}

int main(void)
{
    static const struct vsb_host host = { 0, dma, irq };
    u32 i, before, fresh0;
    int n, gaps;

    vdma_reset(&d, 1 << 1 | 1 << 5, 1 << 1);
    vsb_init(&s, &host, 5, 1, 5);

    /* How a card is found. */
    CHECK(!(vsb_in(&s, 0xE) & 0x80));           /* nothing to read before a reset */
    reset();
    CHECK(vsb_in(&s, 0xE) & 0x80);
    CHECK(rd() == 0xAA);
    CHECK(!(vsb_in(&s, 0xE) & 0x80));
    CHECK(!(vsb_in(&s, 0xC) & 0x80));           /* (it can take a command) */
    dsp(0xE1);
    CHECK(rd() == 4 && rd() == 5);
    dsp(0xE0);
    dsp(0x55);
    CHECK(rd() == 0xAA);
    dsp(0xE4);
    dsp(0x5A);
    dsp(0xE8);
    CHECK(rd() == 0x5A);
    dsp(0xD1);
    dsp(0xD8);
    CHECK(rd() == 0xFF);
    dsp(0xD3);
    dsp(0xD8);
    CHECK(rd() == 0x00);
    dsp(0xE3);
    CHECK(rd() == 'C' && rd() == 'O' && rd() == 'P');
    reset();
    CHECK(rd() == 0xAA);                        /* (what was left to read went with the reset) */
    dsp(0xF2);
    CHECK(irqs == 1 && mixer_get(0x82) == 0x01);
    vsb_in(&s, 0xE);
    CHECK(mixer_get(0x82) == 0x00);
    dsp(0xF3);
    CHECK(irqs == 2 && mixer_get(0x82) == 0x02);
    vsb_in(&s, 0xF);
    CHECK(mixer_get(0x82) == 0x00);
    CHECK(mixer_get(0x80) == 0x02 && mixer_get(0x81) == 0x22);  /* interrupt 5; channels 1 and 5 */

    /* The mixer chip: the older cards' volumes are the SB16's top bits; a reset puts them back. */
    CHECK(mixer_get(0x30) == 0xF8 && mixer_get(0x22) == 0xFF);
    mixer(0x22, 0xF8);
    CHECK(mixer_get(0x30) == 0xF8 && mixer_get(0x31) == 0x88 && mixer_get(0x22) == 0xF8);
    mixer(0x04, 0xFF);
    CHECK(mixer_get(0x32) == 0xF8 && mixer_get(0x33) == 0xF8);
    mixer(0x00, 0);
    CHECK(mixer_get(0x30) == 0xF8 && mixer_get(0x04) == 0xFF);
    mixer(0x22, 0xFF);                          /* (everything at its loudest, for the numbers below) */
    mixer(0x04, 0xFF);

    /* One block of a hundred bytes, 8 bits, by a time constant: as loud as it gets, on both sides; its
       interrupt when its last bytes have been taken (a period before they sound: with the first fifty frames,
       here); then nothing. */
    irqs = 0;
    memset(mem + 0x24000, 0xFF, 100);
    dma_setup(1, 0, 0x24000, 100);
    dsp(0x40);
    dsp(165);                                   /* 256 - 1000000 / 10989 */
    dsp(0x14);
    dsp(99);
    dsp(0);
    CHECK(vsb_playing(&s) && s.rate == 10989);
    CHECK(mix(50, 10989) == 1 && s.played == 50 && irqs == 1 && vsb_playing(&s));
    CHECK(acc[0] == 127 * 256 && acc[1] == 127 * 256 && acc[98] == 127 * 256 && acc[100] == 0);
    CHECK(mix(60, 10989) == 1 && s.played == 100 && irqs == 1 && !vsb_playing(&s));
    CHECK(acc[98] == 127 * 256 && acc[100] == 0);
    CHECK(mixer_get(0x82) == 0x01 && !vdma_running(&d, 1));
    vsb_in(&s, 0xE);
    CHECK(mix(10, 10989) == 0);

    /* Single blocks of fifty bytes one after another, each asked for when the one before has had its
       interrupt, as many games play (the handler runs after the mixer's period, and sets the channel and the
       block up again): every period of thirty-two frames is whole, with no gap where a block ends. */
    reset();
    rd();
    mixer(0x22, 0xFF);
    mixer(0x04, 0xFF);
    irqs = 0;
    before = s.begun;                           /* (the blocks asked for are counted from the card's start) */
    fresh0 = s.fresh;
    memset(mem + 0x24000, 0xFF, 50);
    dma_setup(1, 0, 0x24000, 50);
    dsp(0x40);
    dsp(165);
    dsp(0x14);
    dsp(49);
    dsp(0);
    for (n = 0, gaps = 0; n < 20; n++) {
        int was = irqs;
        if (mix(32, 10989) != 1)
            gaps++;
        for (i = 0; i < 32; i++)
            if (acc[i * 2] != 127 * 256 || acc[i * 2 + 1] != 127 * 256)
                gaps++;
        if (irqs != was) {
            vsb_in(&s, 0xE);
            dma_setup(1, 0, 0x24000, 50);
            dsp(0x14);
            dsp(49);
            dsp(0);
        }
    }
    CHECK(gaps == 0 && s.played == 20 * 32);
    CHECK(irqs >= 13 && irqs <= 14 && s.begun - before == (u32)irqs + 1);       /* (640 frames played, 65 more in hand
                                                                                   at the most) */
    CHECK(s.fresh - fresh0 == 1);               /* (one sound: the blocks after the first went on from the one before) */
    /* ... and with no next block asked for, what is in hand plays out and it ends. */
    before = s.played;
    for (n = 0; n < 4; n++)
        mix(32, 10989);
    CHECK(!vsb_playing(&s) && s.played > before && s.played <= before + 100 && s.played % 50 == 0);

    /* Half the mixer's rate: every other frame of the mixer's is half-way between two of the program's. */
    reset();
    rd();
    mixer(0x22, 0xFF);
    mixer(0x04, 0xFF);
    mem[0x24000] = 128, mem[0x24001] = 228, mem[0x24002] = 128, mem[0x24003] = 28;
    dma_setup(1, 0, 0x24000, 4);
    dsp(0x41);
    dsp(11025 >> 8);
    dsp(11025 & 0xFF);
    dsp(0xC0);                                  /* the SB16's 8-bit command: unsigned, one channel */
    dsp(0x00);
    dsp(3);
    dsp(0);
    CHECK(s.rate == 11025);
    mix(8, 22050);
    CHECK(acc[0] == 0 && acc[2] == 12800 && acc[4] == 25600 && acc[6] == 12800 && acc[8] == 0 && acc[10] == -12800
          && acc[12] == -25600);
    CHECK(!vsb_playing(&s));

    /* Auto-initialised blocks of 32 bytes from a buffer of 64: an interrupt a block, the DMA channel going
       round; the end asked for (DAh) comes with the block under way. */
    reset();
    rd();
    irqs = 0;
    for (i = 0; i < 64; i++)
        mem[0x24000 + i] = (u8)(128 + i);
    dma_setup(1, 1, 0x24000, 64);
    dsp(0x41);
    dsp(11025 >> 8);
    dsp(11025 & 0xFF);
    dsp(0x48);
    dsp(31);
    dsp(0);
    dsp(0x1C);
    CHECK(mix(160, 11025) == 1 && s.played == 160);
    CHECK(irqs == 10);                          /* (five blocks played, and as much again and a frame in hand:
                                                   ten blocks' bytes taken, and a byte of the eleventh) */
    CHECK(vsb_playing(&s) && vdma_running(&d, 1));
    dsp(0xDA);
    before = s.played;
    CHECK(mix(100, 11025) == 1 && vsb_playing(&s) && irqs == 11);       /* (the eleventh, under way, to its end) */
    CHECK(mix(100, 11025) == 1 && !vsb_playing(&s) && irqs == 11);
    CHECK(s.played - before == 192);            /* (what was in hand, and the rest of the eleventh; no twelfth) */

    /* The SB16's 16 bits, two channels, signed, by channel 5. */
    reset();
    rd();
    irqs = 0;
    for (i = 0; i < 32; i++) {
        mem[0x30000 + i * 4] = 1000 & 0xFF, mem[0x30000 + i * 4 + 1] = 1000 >> 8;
        mem[0x30000 + i * 4 + 2] = (u8)(-2000 & 0xFF), mem[0x30000 + i * 4 + 3] = (u8)((-2000 >> 8) & 0xFF);
    }
    dma_setup(5, 1, 0x30000, 128);
    dsp(0x41);
    dsp(22050 >> 8);
    dsp(22050 & 0xFF);
    dsp(0xB6);
    dsp(0x30);
    dsp(63);                                    /* 64 words a block */
    dsp(0);
    CHECK(mix(40, 22050) == 1 && acc[0] == 1000 && acc[1] == -2000 && acc[78] == 1000 && acc[79] == -2000);
    CHECK(irqs == 2 && mixer_get(0x82) == 0x02);     /* (a block is 32 frames: 81 are in hand, two blocks whole) */
    vsb_in(&s, 0xF);
    CHECK(mixer_get(0x82) == 0x00);

    /* A whole swing between two frames, heard at four thirds of the rate: three quarters of the way from one to
       the other is three quarters of the swing (the sum overflowed thirty-two bits once), and so on. */
    reset();
    rd();
    mem[0x30000] = 0x00, mem[0x30001] = 0x80, mem[0x30002] = 0xFF, mem[0x30003] = 0x7F;         /* -32768, 32767 */
    mem[0x30004] = 0x00, mem[0x30005] = 0x80, mem[0x30006] = 0xFF, mem[0x30007] = 0x7F;
    dma_setup(5, 0, 0x30000, 8);
    dsp(0x41);
    dsp(11025 >> 8);
    dsp(11025 & 0xFF);
    dsp(0xB0);
    dsp(0x10);                                  /* signed, one channel */
    dsp(3);
    dsp(0);
    mix(4, 14700);
    CHECK(acc[0] == -32768 && acc[2] >= 16380 && acc[2] <= 16385 && acc[4] >= -2 && acc[4] <= 1 && acc[6] >= -16388
          && acc[6] <= -16380);

    /* The SB16's 16 bits again, block after block, for what follows. */
    reset();
    rd();
    irqs = 0;
    dma_setup(5, 1, 0x30000, 128);
    for (i = 0; i < 32; i++) {
        mem[0x30000 + i * 4] = 1000 & 0xFF, mem[0x30000 + i * 4 + 1] = 1000 >> 8;
        mem[0x30000 + i * 4 + 2] = (u8)(-2000 & 0xFF), mem[0x30000 + i * 4 + 3] = (u8)((-2000 >> 8) & 0xFF);
    }
    dsp(0x41);
    dsp(22050 >> 8);
    dsp(22050 & 0xFF);
    dsp(0xB6);
    dsp(0x30);
    dsp(63);
    dsp(0);
    mix(8, 22050);

    /* Paused, nothing comes and nothing moves; on again from where it was. */
    dsp(0xD5);
    before = s.played;
    CHECK(mix(16, 22050) == 0 && s.played == before && acc[0] == 0);
    dsp(0xD6);
    CHECK(mix(16, 22050) == 1 && acc[0] == 1000);

    /* The mixer's volumes: the master all the way down is silence, while it plays on. */
    mixer(0x30, 0x00);
    before = s.played;
    CHECK(mix(16, 22050) == 1 && acc[0] == 0 && acc[1] == -2000 && s.played == before + 16);
    mixer(0x30, 0xF8);
    mixer(0x33, 0xE8);                          /* the voice's right side 4 dB down: 162 of 256 */
    CHECK(mix(4, 22050) == 1 && acc[0] == 1000 && acc[1] == (-2000 * 162) / 256 - 1);

    /* The Pro's stereo: the older command's bytes come in pairs, at half the frames a second. */
    reset();
    rd();
    mixer(0x00, 0);
    mixer(0x22, 0xFF);
    mixer(0x04, 0xFF);
    mixer(0x0E, 0x02);
    for (i = 0; i < 8; i += 2)
        mem[0x24000 + i] = 0xFF, mem[0x24000 + i + 1] = 0x00;
    dma_setup(1, 0, 0x24000, 8);
    dsp(0x40);
    dsp(211);                                   /* 22222 bytes a second */
    dsp(0x14);
    dsp(7);
    dsp(0);
    CHECK(s.rate == 11111 && s.stereo);
    CHECK(mix(4, 11111) == 1 && acc[0] == 127 * 256 && acc[1] == -128 * 256 && acc[6] == 127 * 256);
    mixer(0x0E, 0x00);

    /* A block of silence: its interrupt after so many samples, and the DMA channel not asked. */
    reset();
    rd();
    irqs = 0;
    dma_setup(1, 0, 0x24000, 8);
    dsp(0x41);
    dsp(11025 >> 8);
    dsp(11025 & 0xFF);
    dsp(0x80);
    dsp(19);
    dsp(0);
    CHECK(mix(30, 11025) == 1 && irqs == 1 && !vsb_playing(&s) && vdma_running(&d, 1));
    CHECK(acc[0] == 0 && acc[20] == 0);

    /* A DMA channel masked under it: nothing comes, and it waits; unmasked, it goes on. */
    reset();
    rd();
    mixer(0x22, 0xFF);
    mixer(0x04, 0xFF);
    irqs = 0;
    memset(mem + 0x24000, 0xFF, 64);
    dma_setup(1, 0, 0x24000, 64);
    {
        struct vdma_real real;
        vdma_out(&d, 0x0A, 0x05, &real);
        dsp(0x14);
        dsp(63);
        dsp(0);
        CHECK(mix(16, 11025) == 0 && vsb_playing(&s) && irqs == 0);
        vdma_out(&d, 0x0A, 0x01, &real);
    }
    CHECK(mix(16, 11025) == 1 && acc[0] == 127 * 256);

    /* A reset while it plays: it stops, and AAh is there to read. */
    reset();
    CHECK(!vsb_playing(&s) && rd() == 0xAA && mix(8, 11025) == 0);

    /* Recording and ADPCM are not done: the program is not kept waiting for their interrupt. */
    irqs = 0;
    dsp(0x74);
    dsp(10);
    dsp(0);
    CHECK(irqs == 1 && !vsb_playing(&s));
    dsp(0xC8);                                  /* (the SB16's recording: taken, and nothing) */
    dsp(0x00);
    dsp(10);
    dsp(0);
    CHECK(!vsb_playing(&s));
    dsp(0xE1);                                  /* (and the bytes after are commands again) */
    CHECK(rd() == 4 && rd() == 5);

    printf("vsb_test: %d failures\n", fails);
    return fails != 0;
}
