/* ESS AudioDrive (GLOS v1, phase H tier 2, 2026-11-23; docs/v1/hardware-audit.md): the ES688, ES1688, ES1868,
 * ES1869, ES1878, ES1879, ES1887 and ES1888, ISA chips on many cards and boards of 1995-1999 (bench D has an
 * ES1887); 86Box's ess_es688, ess_es1688 and the PnP ess_ess0100_pnp and ess_ess0102_pnp. As a Sound Blaster
 * they are a Pro (DSP 3.01), which sb16.c refuses; this plays them in ESS's own Extended Mode: 16-bit stereo,
 * any rate to 48 kHz, by auto-initialised DMA on the 8-bit channel BLASTER names (D).
 *
 * Written from ESS's ES1868 data sheet ("ES1868 AudioDrive", DS1868B; the copy at the ALSA project's mirror of
 * ESS's manuals, sha256 f939b532...1c73), its sections "Extended Mode Programming" and "Programming the FIFO for
 * DMA Playback" (Table 11), the General Control Extension registers and Table 14:
 *   - reset: 3 to 2x6h (bit 1 clears the FIFO), then 0, then AAh to be read; command C6h turns Extended Mode on
 *     after every reset; a reset with 1 leaves the chip a Sound Blaster Pro again;
 *   - E7h says what it is: 68h and 8xh for the AudioDrives with Extended Mode (48h: the ES488, 8-bit only);
 *   - the extended registers are written as a command (the register, A0h-BFh) and a data byte, read with C0h and
 *     the register;
 *   - playback: B8h 04h (auto-initialised, to the DACs); A8h bits 1-0 01b (stereo); B9h 2 (demand DMA, four bytes
 *     a request); A1h the rate (397.7 kHz / (128 - x), or with bit 7 set 795.5 kHz / (256 - x) above 22 kHz);
 *     A2h the filter (7.16 MHz / (256 - x), 82 times 80% of half the rate); A4h/A5h the count of bytes between
 *     interrupts, in two's complement; B6h 00h, B7h 71h, B7h BCh (16-bit signed stereo); B1h and B2h bits 6 and 4
 *     set, 7 and 5 clear, the rest kept (the IRQ and DRQ the card was given); D1h the voice to the mixer; B8h bit
 *     0 starts it;
 *   - the interrupt, at each half, is acknowledged by reading 2xEh; no command is sent from it;
 *   - the mixer's Extended Access registers (four bits a side): the master 32h, the voice 14h, FM 36h, the CD 38h,
 *     the line 3Eh, the microphone 1Ah; the master, the voice and FM set full when the chip is found.
 * Found where BLASTER says; with no BLASTER, as a Plug and Play card (pnpbus.c): its audio device used as a BIOS
 * set it up, or given its preferred resources and turned on.
 * Not yet: recording (the first DMA channel plays or records, not both; full duplex wants the second's), the
 * ESS mixer's finer registers, the chip's FM (ESFM) and MPU-401 for midi.c.
 * Proof: Loop A (86Box's ess_es1688); bench D's ES1887 is owed. */
#include "devlist.h"
#include "glos/bootinfo.h"
#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "pnpbus.h"
#include "vm.h"

static struct {
    u16 base;
    u8 irq, dma;                                /* the 8-bit channel (0, 1 or 3) */
    u8 found, running, id;
    u8 *buf;                                    /* two halves */
    u32 phys, frames, half, irqs;
    mixer_fill fill;
} es;

static const u8 page8[4] = { 0x87, 0x83, 0x81, 0x82 };

static int dsp_write(u8 v)
{
    u32 n;
    for (n = 0; n < 100000; n++)
        if (!(inb((u16)(es.base + 0x0C)) & 0x80)) {
            outb((u16)(es.base + 0x0C), v);
            return 0;
        }
    return -1;
}

static int dsp_read(void)
{
    u32 n;
    for (n = 0; n < 100000; n++)
        if (inb((u16)(es.base + 0x0E)) & 0x80)
            return inb((u16)(es.base + 0x0A));
    return -1;
}

static int reset_to(u8 how)             /* 1: a Sound Blaster Pro; 3: the FIFO cleared too, for Extended Mode */
{
    u32 i;
    outb((u16)(es.base + 6), how);
    for (i = 0; i < 100; i++)
        inb(0x80);
    outb((u16)(es.base + 6), 0);
    for (i = 0; i < 1000; i++)
        if (dsp_read() == 0xAA)
            return 0;
    return -1;
}

static void reg_wr(u8 reg, u8 v)
{
    dsp_write(reg);
    dsp_write(v);
}

static int reg_rd(u8 reg)
{
    dsp_write(0xC0);
    dsp_write(reg);
    return dsp_read();
}

static const char *res(void)
{
    static char r[40];
    ksnprintf(r, sizeof r, "io=%04x-%04x irq=%u dma=%u", es.base, es.base + 15, es.irq, es.dma);
    return r;
}

static void mix_wr(u8 reg, u8 v);

/* BLASTER's A, I and D (an AudioDrive has no 16-bit channel: no H), the chip's answers to E1h and E7h. */
/* An AudioDrive at es.base: 1, its DSP's version and its E7h answer given. */
static int probe(int *hi, int *lo, int *id)
{
    int a, b;
    if (reset_to(1) != 0 || dsp_write(0xE1) != 0 || (*hi = dsp_read()) < 0 || (*lo = dsp_read()) < 0)
        return 0;
    if (dsp_write(0xE7) != 0 || (a = dsp_read()) < 0 || (b = dsp_read()) < 0 || a != 0x68 || (b & 0xF0) != 0x80) {
        reset_to(1);
        return 0;                               /* (not an AudioDrive with Extended Mode) */
    }
    *id = b;
    return 1;
}

int ess_init(const struct bootinfo *bi)
{
    int hi = 0, lo = 0, b = 0, there = 0;
    if (dev_turned_off("ess")) {
        if (bi->sb_port) {
            es.base = (u16)bi->sb_port, es.irq = (u8)bi->sb_irq, es.dma = (u8)bi->sb_dma8;
            dev_note("ess", "ess", DEVS_OFF, "", res());
        }
        return -1;
    }
    if (bi->sb_port && bi->sb_irq <= 15 && bi->sb_dma8 <= 3 && bi->sb_dma8 != 2) {    /* where BLASTER says */
        es.base = (u16)bi->sb_port, es.irq = (u8)bi->sb_irq, es.dma = (u8)bi->sb_dma8;
        there = probe(&hi, &lo, &b);
    }
    if (!there && !bi->sb_port) {
        /* No BLASTER at all: a Plug and Play AudioDrive (ES1868, ES1869 and their kin) that nothing has set up,
           or that a BIOS has without saying. Its audio device is the one of ESS's with ports, an interrupt and
           a DMA channel (the others: its game port, its MPU-401, a CD-ROM's IDE). */
        struct pnp_dev *d = 0;
        while (!there && (d = pnpbus_find(PNP_VENDOR(PNP_ID('E', 'S', 'S', 0)), 1, 1, 1, d)) != 0) {
            if (pnpbus_on(d) != 0 || !d->io[0] || !d->irq[0] || d->dma[0] > 3 || d->dma[0] == 2)
                continue;
            es.base = d->io[0], es.irq = d->irq[0], es.dma = d->dma[0];
            there = probe(&hi, &lo, &b);
        }
    }
    if (!there)
        return -1;
    es.id = (u8)b;
    es.found = 1;
    mix_wr(0x32, 0xFF);                         /* the master, the voice and FM full (88h after a reset: some 30 dB
                                                   down in all; GLOS's mixer sets the level in software) */
    mix_wr(0x14, 0xFF);
    mix_wr(0x36, 0xFF);
    kprintf("GLOS-ESS found port=%x irq=%u dma=%u dsp=%u.%02u id=68%02x\n", es.base, es.irq, es.dma, hi, lo, b);
    dev_note("ess", "ess", DEVS_ON, "", res());
    return 0;
}

static void irq(struct trapframe *tf)
{
    inb((u16)(es.base + 0x0E));                 /* (acknowledged) */
    if (!es.running)
        return;
    es.irqs++;
    es.fill((s16 *)(es.buf + es.half * es.frames * 4), es.frames, tf);
    es.half ^= 1;
}

static void dma_mask(void) { outb(0x0A, (u8)(4 | es.dma)); }

static int ess_start(u32 rate, u32 frames, mixer_fill fill)
{
    u32 bytes = frames * 4, div, filt;
    if (!es.found || rate < 4000 || rate > 48000 || bytes > 32768)
        return -1;
    if (!es.buf) {
        u32 phys;
        u8 *p = dma_alloc((bytes * 2 + 4095) / 4096, &phys);  /* (below 16 MB, never across 64 KB: the low 8237) */
        if (!p || phys + bytes * 2 > 0x1000000u || (phys >> 16) != ((phys + bytes * 2 - 1) >> 16)) {
            kprintf("GLOS-ESS error=dma-buffer phys=%x\n", phys);
            return -1;
        }
        es.buf = p, es.phys = phys;
    }
    es.frames = frames, es.fill = fill, es.half = 0;
    fill((s16 *)es.buf, frames, 0);
    fill((s16 *)(es.buf + bytes), frames, 0);
    if (reset_to(3) != 0 || dsp_write(0xC6) != 0)
        return -1;
    reg_wr(0xB8, 0x04);                         /* auto-initialised, to the DACs */
    reg_wr(0xA8, (u8)((reg_rd(0xA8) & ~3) | 1));        /* stereo */
    reg_wr(0xB9, 2);                            /* demand DMA, four bytes a request */
    if (rate > 22000)
        div = 256 - (795500 + rate / 2) / rate, reg_wr(0xA1, (u8)(0x80 | div));
    else
        div = 128 - (397700 + rate / 2) / rate, reg_wr(0xA1, (u8)div);
    filt = 256 - (7160000u * 20 + 82u * 8 * rate / 2) / (82u * 8 * rate);       /* (82 x 80% of half the rate) */
    reg_wr(0xA2, (u8)filt);
    reg_wr(0xA4, (u8)(0x10000 - bytes));        /* a half's bytes, two's complement */
    reg_wr(0xA5, (u8)((0x10000 - bytes) >> 8));
    reg_wr(0xB6, 0x00);                         /* 16-bit signed stereo (Table 11) */
    reg_wr(0xB7, 0x71);
    reg_wr(0xB7, 0xBC);
    reg_wr(0xB1, (u8)((reg_rd(0xB1) & 0x0F) | 0x50));  /* its interrupt on, the line it was given kept */
    reg_wr(0xB2, (u8)((reg_rd(0xB2) & 0x0F) | 0x50));  /* its DMA requests on, the channel kept */
    dma_mask();
    outb(0x0C, 0);                              /* the flip-flop */
    outb(0x0B, (u8)(0x58 | es.dma));            /* single, auto-initialised, memory to the card */
    outb((u16)(es.dma * 2), (u8)es.phys);
    outb((u16)(es.dma * 2), (u8)(es.phys >> 8));
    outb(page8[es.dma], (u8)(es.phys >> 16));
    outb((u16)(es.dma * 2 + 1), (u8)(bytes * 2 - 1));
    outb((u16)(es.dma * 2 + 1), (u8)((bytes * 2 - 1) >> 8));
    outb(0x0A, es.dma);                         /* unmasked */
    vm_claim_irq(es.irq, irq);
    es.running = 1;
    dsp_write(0xD1);                            /* the voice to the mixer */
    reg_wr(0xB8, 0x05);                         /* started */
    kprintf("GLOS-ESS start rate=%u frames=%u buf=%x a1=%x a2=%x\n", rate, frames, es.phys,
            rate > 22000 ? 0x80 | div : div, filt);
    return 0;
}

static void ess_stop(void)
{
    if (!es.running)
        return;
    reg_wr(0xB8, 0x04);                         /* stopped */
    dma_mask();
    es.running = 0;
    dsp_write(0xD3);
    reset_to(1);                                /* (a Sound Blaster Pro again, for DOS and its programs) */
    vm_unclaim_irq(es.irq);
    kprintf("GLOS-ESS stop irqs=%u\n", es.irqs);
}

void ess_leave(void) { ess_stop(); }   /* glos exit: stopped, a Sound Blaster Pro again */

static int ess_running(void) { return es.running; }
static u32 ess_irqs(void) { return es.irqs; }
static u32 ess_errors(void) { return 0; }

/* The mixer's Extended Access registers (the data sheet's "Extended Access": an address with bit 4 set): four bits
   a side, the left in the high nibble: the master 32h, the voice (Audio 1) 14h, FM 36h, the CD (AuxA) 38h, the
   line 3Eh, the microphone 1Ah. (Their Sound Blaster Pro aliases 22h, 04h, 26h, 28h, 2Eh and 0Ah have three.) */
static u8 mix_rd(u8 reg)
{
    outb((u16)(es.base + 4), reg);
    return inb((u16)(es.base + 5));
}

static void mix_wr(u8 reg, u8 v)
{
    outb((u16)(es.base + 4), reg);
    outb((u16)(es.base + 5), v);
}

static int ess_chan(int chan, int set, u8 *left, u8 *right, u8 *mute)
{
    static const u8 reg[MX_CHANNELS] = { 0x32, 0x14, 0x36, 0x38, 0x3E, 0x1A };
    u32 f;
    u8 v;
    if (!es.found || chan < 0 || chan >= MX_CHANNELS)
        return -1;
    f = irq_save();
    if (set)
        mix_wr(reg[chan], (u8)((*left & 0xF0) | (*right >> 4)));
    v = mix_rd(reg[chan]);
    *left = (u8)((v & 0xF0) | (v >> 4));        /* (four bits as 0 to 255) */
    *right = (u8)(((v & 0x0F) << 4) | (v & 0x0F));
    *mute = 0;                                  /* (no switches: turned down, not muted) */
    irq_restore(f);
    return 0;
}

const struct sound_card ess_card = { "ess", ess_start, ess_stop, ess_running, ess_irqs, ess_errors, ess_chan, 0, 0, 0,
                                     0 };
