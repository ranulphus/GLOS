/* Crystal CS423x audio (CS4232 to CS4238B; docs/crystal-audio.md): the chip
 * GLOS sets up itself, then plays the mixer through its Windows Sound System
 * codec, as sb16.c plays it through an SB16.
 *
 * Set-up, from Crystal's CS4236B data sheet (DS212PP4, "Bypassing Plug and
 * Play"): the Crystal Key, 32 bytes to the Plug and Play address port 279h,
 * puts the part in configuration mode; a card select number; each logical
 * device's resources written directly (15h device, 47h/48h/42h I/O bases,
 * 22h IRQ, 2Ah/25h DMA, 33h activate); 79h activates the part. Logical
 * device 0 is the WSS codec, the FM synthesiser and the SB Pro interface,
 * which share the IRQ and the playback DMA channel; 1 the game port; 2 the
 * control port. With no hostload from a BIOS and no EEPROM the part doesn't
 * answer, and nothing is set up. One Crystal part per machine (the key can't
 * tell two apart). The part is left set up and active when GLOS leaves (the
 * owner's choice): DOS programs after GLOS find its SB Pro.
 *
 * The codec, from the CS4231A data sheet (DS139PP2) and the CS4236B's: four
 * ports at WSSbase, R0 the index (80h while it initialises; MCE, 40h, while
 * the format or the interface is being changed), R1 the indexed data, R2 the
 * status (INT, bit 0, cleared by any write). MODE 2 (I12 = 40h); I8 16-bit
 * little-endian stereo at the mixer's rate; the DAC outputs I6/I7, muted
 * after a reset, at 0 dB; I14/I15 the playback count, a half's frames less
 * one; I10's IEN; I9's PEN. The 8237's low controller plays the buffer by
 * auto-initialised single transfers (byte counts, within one 64 KB block
 * below 16 MB), and the codec interrupts at the end of each half, when the
 * mixer fills it again.
 *
 * Mode switching between the SB Pro interface and the codec is the chip's
 * own ("transparent to the host", p.57): a session's program plays through
 * the SB Pro side while the mixer has stopped, and the mixer programs the
 * codec from the start when it plays again. */
#include <string.h>

#include "glos/bootinfo.h"
#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "pci.h"
#include "vm.h"

static const u8 crystal_key[32] = {
    0x96, 0x35, 0x9A, 0xCD, 0xE6, 0xF3, 0x79, 0xBC, 0x5E, 0xAF, 0x57, 0x2B, 0x15, 0x8A, 0xC5, 0xE2,
    0xF1, 0xF8, 0x7C, 0x3E, 0x9F, 0x4F, 0x27, 0x13, 0x09, 0x84, 0x42, 0xA1, 0xD0, 0x68, 0x34, 0x1A
};

#define PNP_ADDR 0x279

static struct {
    u16 wss, syn, sb, game, ctrl;               /* the resources: the data sheet's defaults unless [sound] crystal */
    u8 irq, dma, cap;
    u8 found, running, game_on;
    u8 *buf;                                    /* the DMA buffer: two halves */
    u32 phys, frames, half, irqs;
    u32 errors;                                 /* halves in which the codec reported a sample error (SER) */
    mixer_fill fill;
    char blaster[32];
} cs = { .wss = 0x534, .syn = 0x388, .sb = 0x220, .game = 0x200, .ctrl = 0x120, .irq = 5, .dma = 1, .cap = 0 };

static void pause(u32 n)
{
    while (n--)
        inb(0x80);
}

static void wr(u8 idx, u8 v)
{
    outb(cs.wss, idx);
    outb((u16)(cs.wss + 1), v);
}

static u8 rd(u8 idx)
{
    outb(cs.wss, idx);
    return inb((u16)(cs.wss + 1));
}

/* The codec ready (R0 not 80h): 0, or -1 after about a tenth of a second. */
static int ready(void)
{
    u32 n;
    for (n = 0; n < 100000; n++)
        if (!(inb(cs.wss) & 0x80))
            return 0;
    return -1;
}

/* A CS4231-class codec at the WSS base: I12's ID bits read 1010. */
static int codec_there(void)
{
    if (inb(cs.wss) == 0xFF || ready() != 0)
        return 0;
    return (rd(0x0C) & 0x0F) == 0x0A;
}

static int hex(const char *s, u32 *v)
{
    u32 x = 0, n = 0;
    for (; (*s >= '0' && *s <= '9') || ((*s | 32) >= 'a' && (*s | 32) <= 'f'); s++, n++)
        x = x * 16 + (u32)(*s <= '9' ? *s - '0' : (*s | 32) - 'a' + 10);
    *v = x;
    return n ? 0 : -1;
}

/* [sound] crystal: "auto" (or empty), "off", or resources in hex, as
   "wss=534 sb=220 fm=388 irq=5 dma=1 cap=0 game=200 ctrl=120" (game=0: no
   game port). -1 for off or a value that doesn't parse. */
static int parse(const char *v)
{
    if (!strcmp(v, "off"))
        return -1;
    while (*v) {
        const char *k = v, *e;
        u32 x;
        while (*v && *v != '=' && *v != ' ')
            v++;
        if (*v != '=')
            break;
        e = ++v;
        if (hex(e, &x) != 0)
            return -1;
        if (!strncmp(k, "wss=", 4)) cs.wss = (u16)x;
        else if (!strncmp(k, "sb=", 3)) cs.sb = (u16)x;
        else if (!strncmp(k, "fm=", 3)) cs.syn = (u16)x;
        else if (!strncmp(k, "irq=", 4)) cs.irq = (u8)x;
        else if (!strncmp(k, "dma=", 4)) cs.dma = (u8)x;
        else if (!strncmp(k, "cap=", 4)) cs.cap = (u8)x;
        else if (!strncmp(k, "game=", 5)) cs.game = (u16)x;
        else if (!strncmp(k, "ctrl=", 5)) cs.ctrl = (u16)x;
        else return -1;
        while (*v && *v != ' ')
            v++;
        while (*v == ' ')
            v++;
    }
    return cs.irq < 16 && cs.dma < 4 && cs.cap < 4 && cs.dma != cs.cap ? 0 : -1;
}

/* A PCI function using the IRQ or any of the ports: its name for the refusal. */
static const char *conflict(void)
{
    static char why[24];
    const u16 base[] = { cs.wss, cs.syn, cs.sb, cs.ctrl, cs.game }, len[] = { 4, 4, 16, 8, 8 };
    struct pci_dev *d;
    int i, b, k;
    for (i = 0; (d = pci_at(i)) != 0; i++) {
        if (d->pin && d->irq == cs.irq) {
            ksnprintf(why, sizeof why, "irq %04x:%04x", d->vendor, d->device);
            return why;
        }
        for (b = 0; b < 6; b++) {
            u32 lo, sz;
            if (!(d->bar[b] & 1) || !(lo = d->bar[b] & ~3u))
                continue;
            sz = 0x100;                         /* (an I/O BAR is 256 bytes at most) */
            for (k = 0; k < 5; k++)
                if (base[k] && base[k] < lo + sz && base[k] + len[k] > lo) {
                    ksnprintf(why, sizeof why, "port %04x:%04x", d->vendor, d->device);
                    return why;
                }
        }
    }
    return 0;
}

static void key(void)
{
    u32 i;
    outb(PNP_ADDR, 0);                          /* (the key's sequence from its start) */
    outb(PNP_ADDR, 0);
    for (i = 0; i < sizeof crystal_key; i++)
        outb(PNP_ADDR, crystal_key[i]);
}

static void slam(u8 reg, u32 v, int bytes)
{
    outb(PNP_ADDR, reg);
    if (bytes == 2)
        outb(PNP_ADDR, (u8)(v >> 8));
    outb(PNP_ADDR, (u8)v);
}

int crystal_init(const char *cfg)
{
    const char *why;
    u8 id, ver;
    if (parse(cfg ? cfg : "") != 0) {
        kprintf("GLOS-SOUND crystal off%s\n", cfg && strcmp(cfg, "off") ? " reason=config" : "");
        return -1;
    }
    if ((why = conflict()) != 0) {
        kprintf("GLOS-SOUND crystal refuse reason=%s\n", why);
        return -1;
    }
    cs.game_on = cs.game && inb((u16)(cs.game + 1)) == 0xFF;  /* (nothing answering there yet) */
    key();
    slam(0x06, 0x01, 1);                        /* CSN 1 */
    slam(0x15, 0, 1);                           /* logical device 0: WSS, FM, SB Pro */
    slam(0x47, cs.wss, 2);
    slam(0x48, cs.syn, 2);
    slam(0x42, cs.sb, 2);
    slam(0x22, cs.irq, 1);
    slam(0x2A, cs.dma, 1);
    slam(0x25, cs.cap, 1);
    slam(0x33, 1, 1);
    if (cs.game_on) {
        slam(0x15, 1, 1);                       /* 1: the game port */
        slam(0x47, cs.game, 2);
        slam(0x33, 1, 1);
    }
    slam(0x15, 2, 1);                           /* 2: the control port */
    slam(0x47, cs.ctrl, 2);
    slam(0x33, 1, 1);
    outb(PNP_ADDR, 0x79);                       /* the part active */
    pause(1000);
    if (!codec_there()) {
        kprintf("GLOS-SOUND crystal none wss=%x\n", cs.wss);
        return -1;
    }
    id = rd(0x0C);
    wr(0x0C, 0x40);                             /* MODE 2: I16-I31 (I25, the version) */
    ver = rd(0x19);
    cs.found = 1;
    ksnprintf(cs.blaster, sizeof cs.blaster, "A%x I%u D%u T4", cs.sb, cs.irq, cs.dma);
    kprintf("GLOS-SOUND crystal wss=%x sb=%x fm=%x irq=%u dma=%u cap=%u game=%x ctrl=%x id=%02x ver=%02x\n", cs.wss,
            cs.sb, cs.syn, cs.irq, cs.dma, cs.cap, cs.game_on ? cs.game : 0, cs.ctrl, id, ver);
    return 0;
}

u16 crystal_sb(void) { return cs.found ? cs.sb : 0; }
const char *crystal_blaster(void) { return cs.found ? cs.blaster : 0; }

/* ---- the codec under the mixer */

/* How far the 8237 has read into the buffer this lap, in bytes (its count
   reads one less than the bytes left). */
static u32 dma_pos(void)
{
    u32 lo, hi;
    outb(0x0C, 0);                              /* the flip-flop */
    lo = inb((u16)(cs.dma * 2 + 1));
    hi = inb((u16)(cs.dma * 2 + 1));
    return (cs.frames * 8 - (((hi << 8) | lo) + 1) % (cs.frames * 8)) % (cs.frames * 8);
}

/* The half the DMA isn't reading is filled: the codec's count runs on its
   own, and the first after a set-up came one sample in (86Box: the halves
   filled one behind from then on, a click every half, until a session's
   stop and start put them right; heard on bench-g400 as a light crackle).
   Where the DMA is says which half is free whatever the count did. */
static void irq(struct trapframe *tf)
{
    u8 st = inb((u16)(cs.wss + 2));
    outb((u16)(cs.wss + 2), 0);                 /* (INT cleared) */
    if (!(st & 1) || !cs.running)
        return;
    cs.irqs++;
    if (st & 0x10)                              /* SER: an underrun (I11's PUR) since R2 was last read: the DAC */
        cs.errors++;                            /* held its last sample (DACZ 0) */
    cs.half = dma_pos() < cs.frames * 4 ? 1 : 0;
    cs.fill((s16 *)(cs.buf + cs.half * cs.frames * 4), cs.frames, tf);
}

static const u8 page_port[4] = { 0x87, 0x83, 0x81, 0x82 };

static void dma_stop(void) { outb(0x0A, (u8)(4 | cs.dma)); }

/* I8's rate bits: a divider (CFS) of the 24.576 or 16.9344 MHz clock (C2SL),
   the nearest the codec has. */
static u8 rate_bits(u32 rate, u32 *got)
{
    static const u32 hz[16] = { 8000, 5513, 16000, 11025, 27429, 18900, 32000, 22050,
                                0, 37800, 0, 44100, 48000, 33075, 9600, 6615 };
    u32 i, best = 0, d, bd = ~0u;
    for (i = 0; i < 16; i++) {
        if (!hz[i])
            continue;
        d = hz[i] > rate ? hz[i] - rate : rate - hz[i];
        if (d < bd)
            bd = d, best = i;
    }
    *got = hz[best];
    return (u8)best;                            /* (the table's order: CFS2-0 in bits 3-1, C2SL bit 0) */
}

static int crystal_start(u32 rate, u32 frames, mixer_fill fill)
{
    u32 bytes = frames * 2 * 4, got, i;
    u8 fmt;
    if (!cs.found)
        return -1;
    if (!cs.buf) {
        u32 phys;
        u8 *p = dma_alloc((bytes + 4095) / 4096, &phys);
        if (!p || phys + bytes > 0x1000000u || (phys >> 16) != ((phys + bytes - 1) >> 16)) {
            kprintf("GLOS-SOUND error=dma-buffer phys=%x\n", phys);
            return -1;
        }
        cs.buf = p, cs.phys = phys;
    }
    cs.frames = frames;
    cs.fill = fill;
    cs.half = 0;
    fill((s16 *)cs.buf, frames, 0);
    fill((s16 *)(cs.buf + frames * 4), frames, 0);
    if (ready() != 0)
        return -1;
    wr(0x0C, 0x40);                             /* MODE 2 */
    fmt = (u8)(0x40 | 0x10 | rate_bits(rate, &got));   /* 16-bit little-endian, stereo */
    outb(cs.wss, 0x40 | 0x08);                  /* MCE: the format and the interface */
    outb((u16)(cs.wss + 1), fmt);
    outb(cs.wss, 0x40 | 0x09);
    outb((u16)(cs.wss + 1), 0x00);              /* DMA, two channels, no calibration, playback off */
    outb(cs.wss, 0x00);                         /* MCE off: the codec takes the new rate */
    if (ready() != 0)
        return -1;
    for (i = 0; i < 100000 && (rd(0x0B) & 0x20); i++) ;        /* (I11's ACI: calibration done) */
    wr(0x06, 0x00);                             /* the DACs: 0 dB, not muted */
    wr(0x07, 0x00);
    wr(0x0E, (u8)((frames - 1) >> 8));          /* a half's frames less one */
    wr(0x0F, (u8)(frames - 1));
    dma_stop();
    outb(0x0C, 0);                              /* the flip-flop */
    outb(0x0B, (u8)(0x58 | cs.dma));            /* single, auto-initialised, memory to the card */
    outb((u16)(cs.dma * 2), (u8)cs.phys);
    outb((u16)(cs.dma * 2), (u8)(cs.phys >> 8));
    outb(page_port[cs.dma], (u8)(cs.phys >> 16));
    outb((u16)(cs.dma * 2 + 1), (u8)(bytes - 1));
    outb((u16)(cs.dma * 2 + 1), (u8)((bytes - 1) >> 8));
    outb(0x0A, cs.dma);                         /* unmasked */
    vm_claim_irq(cs.irq, irq);
    outb((u16)(cs.wss + 2), 0);
    wr(0x0A, 0x02);                             /* IEN */
    cs.running = 1;
    wr(0x09, 0x01);                             /* PEN */
    kprintf("GLOS-SOUND start rate=%u frames=%u buf=%x\n", got, frames, cs.phys);
    return 0;
}

static void crystal_stop(void)
{
    if (!cs.running)
        return;
    wr(0x09, 0x00);
    wr(0x0A, 0x00);
    dma_stop();
    outb((u16)(cs.wss + 2), 0);
    cs.running = 0;
    vm_unclaim_irq(cs.irq);
    kprintf("GLOS-SOUND stop irqs=%u errors=%u\n", cs.irqs, cs.errors);
}

static int crystal_running(void) { return cs.running; }
static u32 crystal_irqs(void) { return cs.irqs; }
static u32 crystal_errors(void) { return cs.errors; }

const struct sound_card crystal_card = { "crystal", crystal_start, crystal_stop, crystal_running, crystal_irqs,
                                         crystal_errors };
