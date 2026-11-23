/* VIA's VT82C686A/B AC'97 controller (GLOS v1, phase H tier 1, 2026-11-23; docs/v1/hardware-audit.md): the onboard
 * sound of VIA's Apollo Pro and KT133/KX133 boards (function 5 of the south bridge, 1106:3058), with its AC'97
 * codec (ac97.c), as a card for GLOS's mixer: playback by its SGD read channel and recording by its write channel,
 * bus masters going over a table of two entries (the buffer's halves), 16-bit stereo at the codec's variable rates.
 *
 * And the VT8233's, VT8235's and VT8237's (function 5 of device 17, 1106:3059; bench G's board, a VT8235: added
 * the same evening), from VIA's VT8235M data sheet (revision 2.03, March 2005; bitsavers' copy, sha256
 * 4e39e2e4...480a), "Device 17 Function 5 Registers". Its AC-link's configuration and its codec register are the
 * 686's. Its channels are others:
 *   00h-3Fh four DXS channels (playback, mixed by the chip), 40h the multichannel one, 60h and 70h two write
 *   channels (recording). A channel: +0 status (as the 686's), +1 control (80h start, 40h terminate, 20h start
 *   again at EOL, 02h an interrupt at EOL, 01h at FLAG), +4 the table's address, +8 its stop index (31:24, FFh:
 *   none) and format (21:20: 11b 16-bit stereo).
 *   A DXS channel has its own volume (+2 left, +3 right: 0 is 0 dB, 3Fh, as it is after a reset, muted) and a rate
 *   converter to the codec's 48 kHz: +8 bits 19:0 are 2^20 x rate / 48000. So any rate plays, with no need of
 *   the codec's variable rates; DXS channel 0 is the one used.
 *   Write channel 0: +2 40h (its FIFO on), +3 0 (the primary codec's slots 3 and 4: what the codec's record
 *   select chose), at the codec's ADC rate (variable rates needed for one that isn't 48000).
 * Proof owed: bench G (86Box has no model of it).
 *
 * From VIA's VT82C686A data sheet (revision 1.1, April 1999; the ALSA project's copy, 686a.pdf, sha256
 * 2089efca...e5ed68), function 5:
 *   configuration 40h: bit 0 the codec ready; 41h the AC-link: 80h enabled, 40h its reset released, 08h variable
 *   rates on demand, 04h the SGD read channel's PCM out
 *   I/O base 0: 00h-0Fh the read channel (playback), 10h-1Fh the write channel (recording): +0 status (01h
 *   FLAG, 02h EOL, 04h stopped: cleared by writing them; a block with FLAG pauses until its FLAG is cleared),
 *   +1 control (80h start, 40h terminate), +2 type (80h start again at EOL, 20h 16-bit, 10h stereo, 02h an
 *   interrupt at EOL, 01h at FLAG), +4 the table's address
 *   80h the codec: 15:0 data, 22:16 the register, 23 read, 24 busy, 25 the primary codec's data valid
 * A table entry: the block's address, then its count of bytes (23:0) with EOL (bit 31) or FLAG (bit 30): the first
 * half ends with FLAG, the second with EOL, and the channel starts again at the table's start by itself.
 * Loop A's machines have no VIA south bridge: its proof is a bench's (bench E's, if its controller is a 686).
 * Log: GLOS-VIA found io=XXXX irq=N rev=R | refused no-io|no-irq|no-codec; GLOS-VIA start|stop|left. */
#include <string.h>
#include "ac97.h"
#include "devlist.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "pci.h"
#include "vm.h"

#define CH_PLAY   0x00
#define CH_REC    0x10
#define C_STATUS  0x00
#define C_CONTROL 0x01
#define C_TYPE    0x02
#define C_TABLE   0x04
#define ST_FLAG   0x01u
#define ST_EOL    0x02u
#define ST_STOP   0x04u
#define CT_START  0x80u
#define CT_TERM   0x40u
#define TY_AUTO   0x80u
#define TY_16     0x20u
#define TY_STEREO 0x10u
#define TY_INTS   0x03u
#define CODEC     0x80
#define CD_BUSY   (1u << 24)
#define CD_VALID  (1u << 25)
#define CD_READ   (1u << 23)
#define TBL_EOL   0x80000000u
#define TBL_FLAG  0x40000000u

struct chan {
    u32 *table;                         /* two entries of two longwords */
    u32 table_phys;
    u8 *buf;
    u32 phys, frames;
};

static struct {
    int found, running, rec;
    int v8233;                          /* 1106:3059: the VT8233's channels */
    u16 ch_rec;                         /* the recording channel: 10h, or 60h */
    struct pci_dev *pci;
    u16 io;
    u8 irq, claimed;
    u32 link_was;                       /* configuration 40h-43h as they were */
    struct ac97 ac;
    struct chan play, record;
    u32 irqs;
    mixer_fill fill;
    mixer_got got;
} via;

static void us(u32 n)
{
    while (n--)
        inb(0x80);
}

static int codec_idle(void)
{
    u32 i;
    for (i = 0; i < 4096; i++) {
        if (!(inl((u16)(via.io + CODEC)) & CD_BUSY))
            return 1;
        us(1);
    }
    return 0;
}

static u16 codec_rd(void *ctx, u8 reg)
{
    u32 i, v;
    (void)ctx;
    codec_idle();
    outl((u16)(via.io + CODEC), CD_READ | ((u32)(reg & 0x7F) << 16));
    for (i = 0; i < 4096; i++) {
        v = inl((u16)(via.io + CODEC));         /* (a write of the index would start another access: the
                                                   answer is known by its register's number instead) */
        if (!(v & CD_BUSY) && (v & CD_VALID) && ((v >> 16) & 0x7F) == (reg & 0x7Fu))
            return (u16)v;
        us(1);
    }
    return 0xFFFF;
}

static void codec_wr(void *ctx, u8 reg, u16 v)
{
    (void)ctx;
    codec_idle();
    outl((u16)(via.io + CODEC), ((u32)(reg & 0x7F) << 16) | v);
}

static void codec_pause(void *ctx, u32 ms)
{
    (void)ctx;
    us(ms * 1000);
}

static void chan_stop(u16 ch)
{
    outb((u16)(via.io + ch + C_CONTROL), CT_TERM);
    us(10);
    if (via.v8233)
        outb((u16)(via.io + ch + C_CONTROL), 0);        /* (its interrupts and its starting again: off) */
    else
        outb((u16)(via.io + ch + C_TYPE), 0);
    outb((u16)(via.io + ch + C_STATUS), ST_FLAG | ST_EOL | ST_STOP);
}

int via686_init(void)
{
    struct pci_dev *pd = pci_find(0x1106, 0x3058, 0);
    const char *name = "via686";
    char w[8], r[48];
    u32 cmd, link, i;
    if (!pd && (pd = pci_find(0x1106, 0x3059, 0)) != 0)
        via.v8233 = 1, name = "via8233";
    if (!pd || dev_pci_off(pd, name))
        return -1;
    via.ch_rec = via.v8233 ? 0x60 : CH_REC;
    if (!(pd->bar[0] & 1) || !(pd->bar[0] & ~3u)) {
        kprintf("GLOS-VIA refused no-io\n");
        dev_note(pci_where(pd, w), name, DEVS_REFUSED, "no-io", 0);
        return -1;
    }
    if (!pd->pin || pd->irq == 0 || pd->irq > 15) {
        kprintf("GLOS-VIA refused no-irq\n");
        dev_note(pci_where(pd, w), name, DEVS_REFUSED, "no-irq", 0);
        return -1;
    }
    via.pci = pd;
    via.io = (u16)(pd->bar[0] & 0xFFFCu);
    via.irq = pd->irq;
    cmd = pci_read32(pd->bus, pd->dev, pd->fn, 0x04);
    pci_write32(pd->bus, pd->dev, pd->fn, 0x04, (cmd & 0xFFFF) | 0x05);       /* (I/O and bus master) */
    chan_stop(CH_PLAY);
    chan_stop(via.ch_rec);
    if (via.v8233)                              /* (the other playback channels and the second write channel, if
                                                   the BIOS or another system left one going) */
        for (i = 0x10; i <= 0x70; i += 0x10)
            if (i != 0x50 && i != 0x60)
                chan_stop((u16)i);
    via.link_was = link = pci_read32(pd->bus, pd->dev, pd->fn, 0x40);
    if ((link & 0xC000) != 0xC000) {            /* the AC-link: reset, then enabled and out of reset */
        pci_write32(pd->bus, pd->dev, pd->fn, 0x40, link & ~0xFF00u);
        us(100);
        pci_write32(pd->bus, pd->dev, pd->fn, 0x40, (link & ~0xFF00u) | 0xC000);
        us(100);
    }
    link = pci_read32(pd->bus, pd->dev, pd->fn, 0x40);
    pci_write32(pd->bus, pd->dev, pd->fn, 0x40, (link & ~0xFF00u) | 0xCC00);  /* + variable rates, PCM out */
    for (i = 0; i < 500 && !(pci_read32(pd->bus, pd->dev, pd->fn, 0x40) & 1); i++)
        us(1000);
    via.ac.rd = codec_rd, via.ac.wr = codec_wr, via.ac.pause = codec_pause, via.ac.ctx = 0;
    if (!(pci_read32(pd->bus, pd->dev, pd->fn, 0x40) & 1) || ac97_start(&via.ac) != 0) {
        kprintf("GLOS-VIA refused no-codec\n");
        dev_note(pci_where(pd, w), name, DEVS_REFUSED, "no-codec", 0);
        return -1;
    }
    via.found = 1;
    kprintf("GLOS-VIA found io=%x irq=%u rev=%u chip=%s\n", via.io, via.irq, pd->rev, name);
    ksnprintf(r, sizeof r, "io=%04x-%04x irq=%u", via.io, via.io + 0xFF, via.irq);
    dev_note(pci_where(pd, w), name, DEVS_ON, "", r);
    return 0;
}

static int chan_mem(struct chan *c, u32 frames)
{
    if (!c->table && !(c->table = dma_alloc(1, &c->table_phys)))
        return -1;
    if (!c->buf || frames > c->frames) {
        if (!(c->buf = dma_alloc((frames * 8 + 4095) / 4096, &c->phys)))
            return -1;
    }
    c->frames = frames;
    c->table[0] = c->phys, c->table[1] = TBL_FLAG | (frames * 4);
    c->table[2] = c->phys + frames * 4, c->table[3] = TBL_EOL | (frames * 4);
    return 0;
}

/* A channel set going over its table. `rate` is the VT8233's DXS channel's (its converter's); 0: a write channel. */
static void chan_go(u16 ch, struct chan *c, u32 rate)
{
    chan_stop(ch);
    outl((u16)(via.io + ch + C_TABLE), c->table_phys);
    if (!via.v8233) {
        outb((u16)(via.io + ch + C_TYPE), TY_AUTO | TY_16 | TY_STEREO | TY_INTS);
        outb((u16)(via.io + ch + C_CONTROL), CT_START);
        return;
    }
    if (rate) {
        u32 f = (rate << 14) / 750;             /* (2^20 x rate / 48000, in 32 bits: 2^14 / 750 is the same) */
        outb((u16)(via.io + ch + 2), 0);        /* its volume: 0 dB, both sides (muted after a reset) */
        outb((u16)(via.io + ch + 3), 0);
        outl((u16)(via.io + ch + 8), 0xFF000000u | (3u << 20) | (f > 0xFFFFFu ? 0xFFFFFu : f));
    } else {
        outb((u16)(via.io + ch + 2), 0x40);     /* the recording FIFO */
        outb((u16)(via.io + ch + 3), 0);        /* the primary codec's slots 3 and 4 */
        outl((u16)(via.io + ch + 8), 0xFF000000u | (3u << 20));
    }
    outb((u16)(via.io + ch + C_CONTROL), CT_START | 0x20 | 0x03);  /* start; again at EOL; both interrupts */
}

/* A channel's status: its FLAG (the first half done) and EOL (the second) cleared, which also lets a block
   paused at its FLAG go on. */
static u8 chan_done(u16 ch)
{
    u8 st = inb((u16)(via.io + ch + C_STATUS)) & (ST_FLAG | ST_EOL | ST_STOP);
    if (st)
        outb((u16)(via.io + ch + C_STATUS), st);
    return st;
}

static void irq(struct trapframe *tf)
{
    u8 r = chan_done(via.ch_rec), p = chan_done(CH_PLAY);
    if (via.rec && via.got) {
        if (r & ST_FLAG)
            via.got((const s16 *)via.record.buf, via.record.frames);
        if (r & ST_EOL)
            via.got((const s16 *)(via.record.buf + via.record.frames * 4), via.record.frames);
    }
    if (via.running) {
        if (p & ST_FLAG)
            via.irqs++, via.fill((s16 *)via.play.buf, via.play.frames, tf);
        if (p & ST_EOL)
            via.irqs++, via.fill((s16 *)(via.play.buf + via.play.frames * 4), via.play.frames, tf);
    }
}

static void claim(void)
{
    if (!via.claimed) {
        vm_claim_irq(via.irq, irq);
        via.claimed = 1;
    }
}

static int via_start(u32 rate, u32 frames, mixer_fill fill)
{
    if (!via.found || frames < 16 || frames * 4 > 0xFFFFFF || rate < 4000 || rate > 48000
        || (via.v8233 ? ac97_rates(&via.ac, 48000, 0) : ac97_rate(&via.ac, rate)) != 0) {
        kprintf("GLOS-VIA error=rate-or-size rate=%u frames=%u vra=%d\n", rate, frames, via.ac.vra);
        return -1;
    }
    if (chan_mem(&via.play, frames) != 0) {
        kprintf("GLOS-VIA error=dma-buffer\n");
        return -1;
    }
    via.fill = fill;
    fill((s16 *)via.play.buf, frames, 0);
    fill((s16 *)(via.play.buf + frames * 4), frames, 0);
    claim();
    via.running = 1;
    chan_go(CH_PLAY, &via.play, rate);
    kprintf("GLOS-VIA start rate=%u frames=%u buf=%x\n", rate, frames, via.play.phys);
    return 0;
}

static void via_stop(void)
{
    if (!via.found || !via.running)
        return;
    chan_stop(CH_PLAY);
    via.running = 0;
    kprintf("GLOS-VIA stop irqs=%u\n", via.irqs);
}

static int via_running(void) { return via.running; }
static u32 via_irqs(void) { return via.irqs; }
static u32 via_errors(void) { return 0; }

static int via_rec_start(u32 rate, u32 frames, mixer_got got)
{
    if (!via.found || frames < 16 || (via.v8233 ? ac97_rates(&via.ac, 0, rate) : ac97_rate(&via.ac, rate)) != 0
        || chan_mem(&via.record, frames) != 0)
        return -1;
    via.got = got;
    claim();
    via.rec = 1;
    chan_go(via.ch_rec, &via.record, 0);
    return 0;
}

static void via_rec_stop(void)
{
    if (!via.found || !via.rec)
        return;
    chan_stop(via.ch_rec);
    via.rec = 0;
}

static int via_chan(int chan, int set, u8 *left, u8 *right, u8 *mute)
{
    return via.found ? ac97_chan(&via.ac, chan, set, left, right, mute) : -1;
}

static int via_input(int set, int *input) { return via.found ? ac97_input(&via.ac, set, input) : -1; }

void via686_leave(void)                 /* glos exit: both channels stopped, the AC-link's bits as they were */
{
    if (!via.found)
        return;
    chan_stop(CH_PLAY);
    chan_stop(via.ch_rec);
    via.running = via.rec = 0;
    pci_write32(via.pci->bus, via.pci->dev, via.pci->fn, 0x40,
                (pci_read32(via.pci->bus, via.pci->dev, via.pci->fn, 0x40) & ~0xFF00u) | (via.link_was & 0xFF00u));
    kprintf("GLOS-VIA left\n");
}

static void via_kick(void) { irq(0); }      /* (mixer_tick: a lost edge) */

const struct sound_card via686_card = { "via686", via_start, via_stop, via_running, via_irqs, via_errors, via_chan,
                                        via_input, via_rec_start, via_rec_stop, via_kick };
const struct sound_card via8233_card = { "via8233", via_start, via_stop, via_running, via_irqs, via_errors, via_chan,
                                         via_input, via_rec_start, via_rec_stop, via_kick };
int via686_is8233(void) { return via.v8233; }
