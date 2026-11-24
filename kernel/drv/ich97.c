/* Intel's ICH AC'97 controller (GLOS v1, phase H tier 1, 2026-11-23; docs/v1/hardware-audit.md): the onboard
 * sound of Intel's i810 to i865 boards (ICH to ICH5, and the ICH6's and ICH7's AC'97 function), with its AC'97
 * codec (ac97.c), as a card for GLOS's mixer: playback by its PCM out box and recording by its PCM in box, both
 * bus masters going over a buffer descriptor list, 16-bit stereo at the codec's variable rates.
 *
 * From Intel's ICH datasheets and their AC'97 programmer's reference: BAR 0 (NAMBAR, I/O) is the codec's
 * register file; BAR 1 (NABMBAR, I/O) the bus masters:
 *   00h PCM in, 10h PCM out: +0 the list's address, +4 CIV (the entry being played), +5 LVI (the last valid
 *       one), +6 SR (status: 01h halted, 02h at the last, 04h LVBCI, 08h BCIS (an entry with IOC done), 10h
 *       FIFO error; the last three cleared by writing them), +8 PICB, +0Ah PIV, +0Bh CR (01h run, 02h reset its
 *       registers, 04h LVBIE, 08h FEIE, 10h IOCE)
 *   2Ch GLOB_CNT (02h: the AC-link out of cold reset), 30h GLOB_STA (100h: the primary codec ready),
 *   34h CAS (the codec access semaphore: a read gives 1 while another access is in progress)
 * A list entry: the buffer's address, then a 16-bit count of 16-bit samples (two a stereo frame) and flags
 * (8000h IOC: an interrupt when it is done). The list here has 32 entries, alternately the buffer's two
 * halves, each with IOC; at each interrupt LVI is moved to the entry before the current one, so the box never
 * reaches its last and goes round for ever while GLOS fills the half just played.
 * The ICH4 and later decode their I/O BARs only with IOSE (configuration 41h, bit 0) set.
 * 86Box has no model of it: its proof is a bench's (bench E's, if its controller is one).
 * Log: GLOS-ICH found io=XXXX,YYYY irq=N id=DDDD | refused no-io|no-irq|no-codec; GLOS-ICH start|stop|left. */
#include <string.h>
#include "ac97.h"
#include "devlist.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "pci.h"
#include "vm.h"

#define BOX_IN    0x00
#define BOX_OUT   0x10
#define B_BDBAR   0x00
#define B_CIV     0x04
#define B_LVI     0x05
#define B_SR      0x06
#define B_CR      0x0B
#define SR_CLEAR  0x1Cu
#define SR_BCIS   0x08u
#define CR_RUN    0x01u
#define CR_RESET  0x02u
#define CR_IOCE   0x10u
#define GLOB_CNT  0x2C
#define GLOB_STA  0x30
#define CAS       0x34
#define ENTRIES   32

static const u16 ids[] = { 0x2415, 0x2425, 0x2445, 0x2485, 0x24C5, 0x24D5, 0x25A6, 0x266E, 0x27DE };

struct box {
    u32 *list;                          /* ENTRIES of two longwords */
    u32 list_phys;
    u8 *buf;
    u32 phys, frames, half;
};

static struct {
    int found, running, rec;
    struct pci_dev *pci;
    u16 mix, bm;
    u8 irq, claimed;
    struct ac97 ac;
    struct box out, in;
    u32 irqs;
    mixer_fill fill;
    mixer_got got;
} ich;

static void us(u32 n)
{
    while (n--)
        inb(0x80);
}

static void cas_wait(void)              /* the codec free (or a millisecond gone: a lost semaphore) */
{
    u32 i;
    for (i = 0; i < 1000 && (inb((u16)(ich.bm + CAS)) & 1); i++)
        us(1);
}

static u16 codec_rd(void *ctx, u8 reg)
{
    (void)ctx;
    cas_wait();
    return inw((u16)(ich.mix + reg));
}

static void codec_wr(void *ctx, u8 reg, u16 v)
{
    (void)ctx;
    cas_wait();
    outw((u16)(ich.mix + reg), v);
}

static void codec_pause(void *ctx, u32 ms)
{
    (void)ctx;
    us(ms * 1000);
}

static void box_reset(u16 box)
{
    u32 i;
    outb((u16)(ich.bm + box + B_CR), 0);
    outb((u16)(ich.bm + box + B_CR), CR_RESET);
    for (i = 0; i < 1000 && (inb((u16)(ich.bm + box + B_CR)) & CR_RESET); i++)
        us(1);
    outw((u16)(ich.bm + box + B_SR), SR_CLEAR);
}

int ich97_init(void)
{
    struct pci_dev *pd = 0;
    char w[8], r[64];
    u32 cmd, i;
    for (i = 0; i < sizeof ids / sizeof ids[0] && !pd; i++)
        pd = pci_find(0x8086, ids[i], 0);
    if (!pd || dev_pci_off(pd, "ich97"))
        return -1;
    if (pd->device >= 0x24C5) {                 /* (ICH4 on: the I/O BARs decoded only with IOSE) */
        u32 v = pci_read32(pd->bus, pd->dev, pd->fn, 0x40);
        pci_write32(pd->bus, pd->dev, pd->fn, 0x40, v | 0x100);
        pd->bar[0] = pci_read32(pd->bus, pd->dev, pd->fn, 0x10);
        pd->bar[1] = pci_read32(pd->bus, pd->dev, pd->fn, 0x14);
    }
    if (!(pd->bar[0] & 1) || !(pd->bar[1] & 1) || !(pd->bar[0] & ~3u) || !(pd->bar[1] & ~3u)) {
        kprintf("GLOS-ICH refused no-io\n");
        dev_note(pci_where(pd, w), "ich97", DEVS_REFUSED, "no-io", 0);
        return -1;
    }
    if (!pd->pin || pd->irq == 0 || pd->irq > 15) {
        kprintf("GLOS-ICH refused no-irq\n");
        dev_note(pci_where(pd, w), "ich97", DEVS_REFUSED, "no-irq", 0);
        return -1;
    }
    if (mixer_pci_line(pd, "ich97", "ICH") != 0)
        return -1;
    ich.pci = pd;
    ich.mix = (u16)(pd->bar[0] & 0xFFFCu);
    ich.bm = (u16)(pd->bar[1] & 0xFFFCu);
    ich.irq = pd->irq;
    cmd = pci_read32(pd->bus, pd->dev, pd->fn, 0x04);
    pci_write32(pd->bus, pd->dev, pd->fn, 0x04, (cmd & 0xFFFF) | 0x05);       /* (I/O and bus master) */
    box_reset(BOX_OUT);
    box_reset(BOX_IN);
    if (!(inl((u16)(ich.bm + GLOB_CNT)) & 0x02)) {       /* the AC-link out of cold reset, ... */
        outl((u16)(ich.bm + GLOB_CNT), 0x02);
        us(1000);
    }
    for (i = 0; i < 1000 && !(inl((u16)(ich.bm + GLOB_STA)) & 0x100); i++)    /* ... the codec ready */
        us(1000);
    ich.ac.rd = codec_rd, ich.ac.wr = codec_wr, ich.ac.pause = codec_pause, ich.ac.ctx = 0;
    if (!(inl((u16)(ich.bm + GLOB_STA)) & 0x100) || ac97_start(&ich.ac) != 0) {
        kprintf("GLOS-ICH refused no-codec\n");
        dev_note(pci_where(pd, w), "ich97", DEVS_REFUSED, "no-codec", 0);
        return -1;
    }
    ich.found = 1;
    pci_claim(pd);                              /* (GLOS's: DOS no longer finds it on the PCI bus) */
    kprintf("GLOS-ICH found io=%x,%x irq=%u id=%04x\n", ich.mix, ich.bm, ich.irq, pd->device);
    ksnprintf(r, sizeof r, "io=%04x-%04x,%04x-%04x irq=%u", ich.mix, ich.mix + 0xFF, ich.bm, ich.bm + 0x3F, ich.irq);
    dev_note(pci_where(pd, w), "ich97", DEVS_ON, "", r);
    return 0;
}

/* A box's list and buffer (two halves of `frames`), made once, larger when asked for more. */
static int box_mem(struct box *b, u32 frames)
{
    u32 i;
    if (!b->list && !(b->list = dma_alloc(1, &b->list_phys)))
        return -1;
    if (!b->buf || frames > b->frames) {
        if (!(b->buf = dma_alloc((frames * 8 + 4095) / 4096, &b->phys)))
            return -1;
    }
    b->frames = frames, b->half = 0;
    for (i = 0; i < ENTRIES; i++) {
        b->list[i * 2] = b->phys + (i & 1) * frames * 4;
        b->list[i * 2 + 1] = (frames * 2) | 0x80000000u;  /* (samples, and IOC in the flags' top bit) */
    }
    return 0;
}

static void box_go(u16 box, struct box *b)
{
    box_reset(box);
    outl((u16)(ich.bm + box + B_BDBAR), b->list_phys);
    outb((u16)(ich.bm + box + B_LVI), ENTRIES - 1);
    outb((u16)(ich.bm + box + B_CR), CR_RUN | CR_IOCE);
}

/* An entry done: cleared, the last valid one moved to just behind the current. 1 if it was this box's. */
static int box_done(u16 box)
{
    u16 sr = inw((u16)(ich.bm + box + B_SR));
    u8 civ;
    if (!(sr & SR_CLEAR))
        return 0;
    outw((u16)(ich.bm + box + B_SR), sr & SR_CLEAR);
    civ = inb((u16)(ich.bm + box + B_CIV));
    outb((u16)(ich.bm + box + B_LVI), (u8)((civ - 1) & (ENTRIES - 1)));
    return (sr & SR_BCIS) != 0;
}

static void irq(struct trapframe *tf)
{
    if (box_done(BOX_IN) && ich.rec && ich.got) {
        ich.got((const s16 *)(ich.in.buf + ich.in.half * ich.in.frames * 4), ich.in.frames);
        ich.in.half ^= 1;
    }
    if (box_done(BOX_OUT) && ich.running) {
        ich.irqs++;
        ich.fill((s16 *)(ich.out.buf + ich.out.half * ich.out.frames * 4), ich.out.frames, tf);
        ich.out.half ^= 1;
    }
}

static void claim(void)
{
    if (!ich.claimed) {
        vm_claim_irq(ich.irq, irq);
        ich.claimed = 1;
    }
}

static int ich_start(u32 rate, u32 frames, mixer_fill fill)
{
    if (!ich.found || frames < 16 || frames * 2 > 0xFFFE || ac97_rate(&ich.ac, rate) != 0) {
        kprintf("GLOS-ICH error=rate-or-size rate=%u frames=%u vra=%d\n", rate, frames, ich.ac.vra);
        return -1;
    }
    if (box_mem(&ich.out, frames) != 0) {
        kprintf("GLOS-ICH error=dma-buffer\n");
        return -1;
    }
    ich.fill = fill;
    fill((s16 *)ich.out.buf, frames, 0);
    fill((s16 *)(ich.out.buf + frames * 4), frames, 0);
    claim();
    ich.running = 1;
    box_go(BOX_OUT, &ich.out);
    kprintf("GLOS-ICH start rate=%u frames=%u buf=%x\n", rate, frames, ich.out.phys);
    return 0;
}

static void ich_stop(void)
{
    if (!ich.found || !ich.running)
        return;
    box_reset(BOX_OUT);
    ich.running = 0;
    kprintf("GLOS-ICH stop irqs=%u\n", ich.irqs);
}

static int ich_running(void) { return ich.running; }
static u32 ich_irqs(void) { return ich.irqs; }
static u32 ich_errors(void) { return 0; }

static int ich_rec_start(u32 rate, u32 frames, mixer_got got)
{
    if (!ich.found || frames < 16 || frames * 2 > 0xFFFE || ac97_rate(&ich.ac, rate) != 0 || box_mem(&ich.in, frames) != 0)
        return -1;
    ich.got = got;
    claim();
    ich.rec = 1;
    box_go(BOX_IN, &ich.in);
    return 0;
}

static void ich_rec_stop(void)
{
    if (!ich.found || !ich.rec)
        return;
    box_reset(BOX_IN);
    ich.rec = 0;
}

static int ich_chan(int chan, int set, u8 *left, u8 *right, u8 *mute)
{
    return ich.found ? ac97_chan(&ich.ac, chan, set, left, right, mute) : -1;
}

static int ich_input(int set, int *input) { return ich.found ? ac97_input(&ich.ac, set, input) : -1; }

void ich97_leave(void)                  /* glos exit: both boxes stopped (bus masters never left running) */
{
    if (!ich.found)
        return;
    box_reset(BOX_OUT);
    box_reset(BOX_IN);
    ich.running = ich.rec = 0;
    kprintf("GLOS-ICH left\n");
}

static void ich_kick(void) { irq(0); }      /* (mixer_tick: a lost edge) */

const struct sound_card ich97_card = { "ich97", ich_start, ich_stop, ich_running, ich_irqs, ich_errors, ich_chan, ich_input,
                                       ich_rec_start, ich_rec_stop, ich_kick };
