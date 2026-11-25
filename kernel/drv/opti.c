/* OPTi 82C929 (MAD16 Pro): ISA sound chips of 1994-96 on many cards and boards (the AcerMagic S20, miro's and
 * Reveal's among them), a Sound Blaster Pro and a Windows Sound System side with a CS4231-class codec (phase H,
 * tier 2; the owner's choice, 2026-11-25).
 *
 * Its configuration registers MC1-MC6 are at F8Dh-F92h, each read or write preceded by the password, E3h, written
 * to F8Fh: without it they read back otherwise (FFh), and a write is not taken. That is how the chip is found: MC1
 * read with the password and without differs, and a bit written with it reads back changed (put back at once).
 * MC1: bit 7 the Windows Sound System side on (the Sound Blaster side off), bits 5-4 its base (530h, E80h, F40h,
 * 604h). MC3: the Sound Blaster side's base (bit 2: 240h, else 220h), its DMA channel (bits 5-4: 1, 0, 3) and its
 * interrupt (bits 7-6: 7, 10, 5). The Windows Sound System side has a configuration register at its base, the
 * codec's four at base + 4: bits 5-3 its interrupt (5, 7, 9, 10, 11, 12, 14, 15), bits 1-0 its DMA channel (0, 1,
 * 3 for 1, 2, 3), bit 2 a second channel for recording (the other of 0 and 1). (OPTi's register map as 86Box models
 * it and as OPTi's chips are documented; behaviour only, no code from either.)
 *
 * GLOS puts the chip in WSS mode at 530h while its mixer plays, with the interrupt and channel the Sound Blaster
 * side had (BLASTER's, or MC3's), and its codec is played by crystal.c's codec functions (crystal_codec_at). When
 * the mixer stops, and when GLOS leaves, MC1 is as GLOS found it: DOS programs find the chip's Sound Blaster Pro.
 *
 * The 82C930 (2026-11-25): its password is E4h, still to F8Fh, and its registers are behind an index and data pair
 * at MCBase, E0Eh after a reset (an index of n is MCn). MC1: the Sound Blaster side's base (bit 7: 240h, else
 * 220h), the Windows Sound System base (bits 5-4, as the 929's). MC3 one interrupt and DMA channel for both sides
 * (bits 5-3: 7, 9, 10, 11, 5 for 1 to 5; bits 1-0: 0, 1, 3 for 1, 2, 3; bit 2 the second channel). MC4 bit 2: the
 * Sound Blaster side's output (WSS's off). MC5 bit 5: the codec's MODE 2. The 930's master volume is the codec's
 * own I22 and I23 (MODE 2; bit 7 mutes, bits 4-0 attenuate in steps of 1.5, 3, 6, 12, 24 dB), muted after a reset:
 * set at 0 dB. GLOS sets MC3 as the Sound Blaster side had it, MODE 2, the master and WSS's output while the
 * mixer plays, and puts MC4 back when it stops.
 * The 82C931 (Plug and Play, its password at F8Dh) is not done: 86Box's needs a ROM RetroGPU hasn't, and no bench
 * has one. */
#include "devlist.h"
#include "glos/bootinfo.h"
#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"

#define MC1 0xF8D
#define MC3 0xF8F
#define PW  0xF8F
#define PASSWORD 0xE3
#define WSS 0x530

static const u8 wss_irqs[8] = { 5, 7, 9, 10, 11, 12, 14, 15 };

#define MCBASE 0xE0E                            /* (the 930's, after a reset) */

static struct {
    u8 found, mc1, irq, dma, cap, code;
    u8 chip;                                    /* 29 or 30 */
    u8 mc4, mc5;                                /* (the 930's, as found) */
    u16 sb;
    char res[48];
} op;

static u8 mc_rd(u16 port)
{
    outb(PW, PASSWORD);
    return inb(port);
}

static void mc_wr(u16 port, u8 v)
{
    outb(PW, PASSWORD);
    outb(port, v);
}

/* (The 93x's index first, then the password, then the data: the order its own drivers use. The password is
   taken only while the registers are locked; one written while they are open would set MCBase instead.) */
static u8 mc30_rd(u8 n)
{
    outb(MCBASE, n);
    outb(PW, 0xE4);
    return inb(MCBASE + 1);
}

static void mc30_wr(u8 n, u8 v)
{
    outb(MCBASE, n);
    outb(PW, 0xE4);
    outb(MCBASE + 1, v);
}

/* WSS mode at 530h and its configuration: before each start (a session may have had the SB side). */
static void wss_on(void)
{
    if (op.chip == 30) {
        mc30_wr(1, (u8)(op.mc1 & ~0x30));
        mc30_wr(3, op.code);
        mc30_wr(5, (u8)(op.mc5 | 0x20));
        mc30_wr(4, (u8)(op.mc4 & ~0x04));
        return;
    }
    mc_wr(MC1, (u8)((op.mc1 | 0x80) & ~0x30));
    outb(WSS, op.code);
}

/* As found: the Sound Blaster side's (a session's programs, DOS's after GLOS). */
static void sb_back(void)
{
    if (op.chip == 30)
        mc30_wr(4, op.mc4);
    else
        mc_wr(MC1, op.mc1);
}

/* An 82C930 at MCBase: MC1 answers there (not FFh), and its bit 0 written reads back changed (put back at once).
   (Whether the password is needed depends on MCBase's bit 7, which a BIOS or a driver may have set: it is given
   every time, and not relied on to tell the chip.) Its interrupt and channel from MC3 (or BLASTER's), coded for
   MC3. 0; -1 no chip; -2 a chip with resources GLOS can't use. */
static int find930(const struct bootinfo *bi, u8 *irq, u8 *dma)
{
    static const u8 irqs[8] = { 0, 7, 9, 10, 11, 5, 0, 0 };
    u8 v = mc30_rd(1), v2, mc3, k, code_irq = 0;
    if (v == 0xFF)
        return -1;
    mc30_wr(1, (u8)(v ^ 0x01));
    v2 = mc30_rd(1);
    mc30_wr(1, v);
    if (v2 != (u8)(v ^ 0x01))
        return -1;
    mc3 = mc30_rd(3);
    op.chip = 30, op.mc1 = v, op.mc4 = mc30_rd(4), op.mc5 = mc30_rd(5);
    op.sb = (v & 0x80) ? 0x240 : 0x220;
    *irq = irqs[(mc3 >> 3) & 7] ? irqs[(mc3 >> 3) & 7] : 5;
    *dma = (mc3 & 3) == 1 ? 0 : (mc3 & 3) == 3 ? 3 : 1;
    if (bi->sb_port == op.sb && bi->sb_irq && bi->sb_irq <= 15)
        *irq = (u8)bi->sb_irq, *dma = (u8)(bi->sb_dma8 <= 3 ? bi->sb_dma8 : *dma);
    for (k = 1; k < 6; k++)
        if (irqs[k] == *irq)
            code_irq = k;
    if (!code_irq || (*dma != 0 && *dma != 1 && *dma != 3))
        return -2;
    op.code = (u8)(code_irq << 3 | 0x04 | (*dma == 0 ? 1 : *dma == 1 ? 2 : 3));
    kprintf("GLOS-OPTI 82c930 mc1=%02x mc3=%02x mc4=%02x mc5=%02x\n", v, mc3, op.mc4, op.mc5);
    return 0;
}

static void master930(void)
{
    if (op.chip == 30) {
        crystal_codec_wr(22, 0x00);
        crystal_codec_wr(23, 0x00);
    }
}

int opti_init(const struct bootinfo *bi)
{
    u8 v, raw, v2, mc3, irq, dma, k, code_irq = 0xFF, code_dma;
    char blaster[32];
    if (dev_turned_off("opti"))
        return -1;
    v = mc_rd(MC1);
    if (v == 0xFF) {                            /* (no 82C929: an 82C930?) */
        int r = find930(bi, &irq, &dma);
        if (r == -1)
            return -1;
        if (r == -2) {
            kprintf("GLOS-OPTI refused irq=%u dma=%u\n", irq, dma);
            dev_note("opti", "opti", DEVS_REFUSED, "conflict", 0);
            return -1;
        }
        mc3 = 0;
        goto codec;
    }
    raw = inb(MC1);                             /* (no password: not the register) */
    if (raw == v)
        return -1;
    mc_wr(MC1, (u8)(v ^ 0x01));                 /* (bit 0, the game port's: changed and put back) */
    v2 = mc_rd(MC1);
    mc_wr(MC1, v);
    if (v2 != (u8)(v ^ 0x01)) {
        kprintf("GLOS-OPTI none mc1=%02x raw=%02x again=%02x\n", v, raw, v2);
        return -1;
    }
    mc3 = mc_rd(MC3);
    op.sb = (mc3 & 0x04) ? 0x240 : 0x220;
    irq = ((mc3 >> 6) & 3) == 0 ? 7 : ((mc3 >> 6) & 3) == 1 ? 10 : 5;
    dma = ((mc3 >> 4) & 3) == 0 ? 1 : ((mc3 >> 4) & 3) == 1 ? 0 : 3;
    if (bi->sb_port == op.sb && bi->sb_irq && bi->sb_irq <= 15)        /* (BLASTER says how DOS has it) */
        irq = (u8)bi->sb_irq, dma = (u8)(bi->sb_dma8 <= 3 ? bi->sb_dma8 : dma);
    for (k = 0; k < 8; k++)
        if (wss_irqs[k] == irq)
            code_irq = k;
    if (code_irq == 0xFF || (dma != 0 && dma != 1 && dma != 3)) {
        kprintf("GLOS-OPTI refused irq=%u dma=%u\n", irq, dma);
        dev_note("opti", "opti", DEVS_REFUSED, "conflict", 0);
        return -1;
    }
    code_dma = dma == 0 ? 1 : dma == 1 ? 2 : 3;
    op.chip = 29, op.mc1 = v;
    op.code = (u8)(code_irq << 3 | 0x04 | code_dma);
codec:
    op.irq = irq, op.dma = dma;
    op.cap = dma == 0 ? 1 : 0;                  /* (the second channel: the other of 0 and 1) */
    wss_on();
    ksnprintf(blaster, sizeof blaster, "A%x I%u D%u T3", op.sb, irq, dma);
    if (crystal_codec_at(WSS + 4, irq, dma, op.cap, op.sb, blaster) != 0) {
        sb_back();
        kprintf("GLOS-OPTI no-codec mc1=%02x mc3=%02x\n", v, mc3);
        dev_note("opti", "opti", DEVS_REFUSED, "no-chip", 0);
        return -1;
    }
    op.found = 1;
    master930();
    ksnprintf(op.res, sizeof op.res, "io=%04x-%04x,%04x-%04x irq=%u dma=%u,%u", WSS, WSS + 7, op.sb, op.sb + 15, irq,
              dma, op.cap);
    kprintf("GLOS-OPTI found chip=82c9%u mc1=%02x mc3=%02x sb=%x wss=%x irq=%u dma=%u cap=%u\n", op.chip, op.mc1, mc3,
            op.sb, WSS, irq, dma, op.cap);
    dev_note("opti", "opti", DEVS_ON, "", op.res);
    return 0;
}

static int opti_start(u32 rate, u32 frames, mixer_fill fill)
{
    int r;
    if (!op.found)
        return -1;
    wss_on();
    r = crystal_card.start(rate, frames, fill);
    master930();
    return r;
}

static void opti_stop(void)
{
    if (!op.found)
        return;
    crystal_card.stop();
    sb_back();                                  /* (the Sound Blaster side again, as found: a session's programs) */
}

static int opti_rec_start(u32 rate, u32 frames, mixer_got got)
{
    return op.found ? crystal_card.rec_start(rate, frames, got) : -1;
}

static void opti_rec_stop(void)
{
    if (op.found)
        crystal_card.rec_stop();
}

static int opti_running(void) { return crystal_card.running(); }
static u32 opti_irqs(void) { return crystal_card.irqs(); }
static u32 opti_errors(void) { return crystal_card.errors(); }
static int opti_chan(int chan, int set, u8 *left, u8 *right, u8 *mute) { return crystal_card.chan(chan, set, left, right, mute); }
static int opti_input(int set, int *input) { return crystal_card.input(set, input); }

void opti_leave(void)                           /* glos exit */
{
    if (!op.found)
        return;
    opti_stop();
    kprintf("GLOS-OPTI left mc1=%02x\n", op.mc1);
}

const struct sound_card opti_card = { "opti", opti_start, opti_stop, opti_running, opti_irqs, opti_errors, opti_chan,
                                      opti_input, opti_rec_start, opti_rec_stop, 0 };
