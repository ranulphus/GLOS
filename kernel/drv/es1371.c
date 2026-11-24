/* Ensoniq AudioPCI 97: the ES1371, the ES1373 and Creative's CT5880 (GLOS v1, phase H tier 1, 2026-11-23;
 * docs/v1/hardware-audit.md), the chips of Creative's SB PCI 64 and 128 and the SB16 PCI and of many boards, as a
 * card for GLOS's mixer: playback by its DAC2 channel and recording by its ADC channel, both by bus master, 16-bit
 * stereo; its sample rate converter set for the mixer's rate (any rate from 4000 to 48000); the AC'97 codec's
 * mixer (ac97.c).
 *
 * From Ensoniq's "AudioPCI 97 Specification" (rev 1.1, October 1997; bitsavers.org's ES1371_199710.pdf, sha256
 * 3d65a0be...19ec53a3): its I/O space (BAR 0):
 *   00h control   DAC2_EN (5), ADC_EN (4), UART_EN (3), SYNC_RES (14: the AC'97 link's warm reset)
 *   04h status    DAC2 (1), ADC (0), UART (3), INTR (31)
 *   0Ch memory page (3:0): 30h-3Fh show that page: Ch the DACs' frames (DAC2's address 38h, its size 3Ch, in
 *       longwords less one), Dh the ADC's (30h, 34h)
 *   10h the sample rate converter's RAM: address 31:25, write 24, busy 23, disables 22 (all), 21 (DAC1), 20
 *       (DAC2), 19 (ADC); data 15:0
 *   14h the AC'97 codec: a write starts a read (bit 23) or a write; read back: RDY (31), WIP (30), data 15:0
 *   20h serial control: DAC2's format 3:2 and the ADC's 5:4 (3: 16-bit stereo), interrupt enables 9 (DAC2) and
 *       10 (ADC: each cleared by turning it off and on), loop 14/15 (0: go on), DAC2's end increment 21:19 (2 for
 *       16 bits)
 *   28h DAC2's and 2Ch the ADC's sample count less one (an interrupt after so many, then again)
 * The converter's RAM: a channel's registers at 74h (DAC2) and 78h (the ADC): +0 its truncation and N, +1 the
 * integer part of its frequency and accumulator, +3 the frequency's fraction; volumes at 6Ch (the ADC's), 7Eh
 * (DAC2's), unity 1000h. The arithmetic of the rates (the frequency in 1/32768ths of a 3000 Hz step for a DAC;
 * N, the truncation and the frequency for the ADC) is as FreeBSD's es137x driver (BSD licence) has it, written
 * here anew.
 *
 * The chip's own MIDI UART and its legacy SB (by trapping, which needs a DOS TSR) are not used. A bus master is
 * stopped at glos exit.
 * Log: GLOS-ES found io=XXXX irq=N id=DDDD rev=R | refused no-io|no-irq|no-codec; GLOS-ES start|stop; GLOS-ES left. */
#include <string.h>
#include "ac97.h"
#include "devlist.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "pci.h"
#include "vm.h"

#define CONTROL   0x00
#define STATUS    0x04
#define MEMPAGE   0x0C
#define SRC       0x10
#define CODEC     0x14
#define LEGACY    0x18
#define SERIAL    0x20
#define DAC2_SCOUNT 0x28
#define ADC_SCOUNT  0x2C

#define C_DAC2    0x20u
#define C_ADC     0x10u
#define C_SYNCRES 0x4000u
#define ST_DAC2   0x02u
#define ST_ADC    0x01u
#define S_P2INT   0x200u
#define S_R1INT   0x400u

#define SRC_BUSY  (1u << 23)
#define SRC_WE    (1u << 24)
#define SRC_DIS   (1u << 22)
#define SRC_DIS_P1 (1u << 21)
#define SRC_DIS_P2 (1u << 20)
#define SRC_DIS_R1 (1u << 19)
#define SRC_KEEP  (SRC_DIS | SRC_DIS_P1 | SRC_DIS_P2 | SRC_DIS_R1)

#define SR_DAC1   0x70
#define SR_DAC2   0x74
#define SR_ADC    0x78
#define SR_VOL_ADC  0x6C
#define SR_VOL_DAC1 0x7C
#define SR_VOL_DAC2 0x7E

#define CODEC_RDY 0x80000000u
#define CODEC_WIP 0x40000000u
#define CODEC_RD  0x00800000u

static struct {
    int found, running, rec;
    struct pci_dev *pci;
    u16 io;
    u8 irq, claimed;
    u32 control, serial;
    u32 srcdis;                         /* the converter's disable bits as last written (never read back: see below) */
    struct ac97 ac;
    u8 *buf, *rbuf;
    u32 phys, rphys, frames, rframes, irqs, half, rhalf;
    mixer_fill fill;
    mixer_got got;
} es;

static u32 rd32(u32 r) { return inl((u16)(es.io + r)); }
static void wr32(u32 r, u32 v) { outl((u16)(es.io + r), v); }

static void us(u32 n)                   /* about n microseconds (a port read takes about one) */
{
    while (n--)
        inb(0x80);
}

/* ---- the sample rate converter's RAM
   Its disable bits (22-19) are kept here, as last written, not read back from the register: a real CT5880 (bench
   F, 2026-11-24, 1274:5880 rev 2) read 00470000h whatever was written (bit 22 among them), and every write that
   kept the disables as read turned the converter off: DAC2 never ran, not one interrupt. 86Box reads 0 there. */
static int src_ready(void)
{
    u32 i;
    for (i = 0; i < 4096; i++) {
        if (!(rd32(SRC) & SRC_BUSY))
            return 1;
        us(1);
    }
    kprintf("GLOS-ES src busy\n");
    return 0;
}

static void src_wr(u32 addr, u32 data)
{
    src_ready();
    wr32(SRC, es.srcdis | ((addr & 0x7F) << 25) | SRC_WE | (data & 0xFFFF));
}

static u32 src_rd(u32 addr)
{
    src_ready();
    wr32(SRC, es.srcdis | ((addr & 0x7F) << 25));
    src_ready();
    return rd32(SRC) & 0xFFFF;
}

static void src_disables(u32 keep)      /* the disable bits set to these */
{
    es.srcdis = keep & SRC_KEEP;
    src_ready();
    wr32(SRC, es.srcdis);
}

/* A DAC's rate (DAC1's at SR_DAC1, DAC2's at SR_DAC2): its frequency in 1/32768ths of 3000 Hz, set with that
   DAC's conversion paused; the converter's other disables, the global one among them, left as they were (at
   set-up it stays off until every rate is set). */
static void dac_rate(u32 base, u32 dis, u32 rate)
{
    u32 freq = ((rate << 15) + 1500) / 3000, was = es.srcdis;
    src_disables(was | dis);
    src_wr(base + 1, (src_rd(base + 1) & 0x00FF) | ((freq >> 5) & 0xFC00));
    src_wr(base + 3, freq & 0x7FFF);
    src_disables(was & ~dis);                   /* (its own disable cleared) */
}

static void dac2_rate(u32 rate) { dac_rate(SR_DAC2, SRC_DIS_P2, rate); }

/* The ADC's rate: N (the 48 kHz input's step, odd ones of 3, 9, 11, 13 and 15 avoided), the truncation, the
   frequency; its volume N's. */
static void adc_rate(u32 rate)
{
    u32 n = rate / 3000, truncm, freq;
    if ((1u << n) & ((1u << 15) | (1u << 13) | (1u << 11) | (1u << 9)))
        n--;
    if (!n)
        n = 1;
    truncm = (21 * n - 1) | 1;
    freq = ((48000u << 15) / rate) * n;
    if (rate >= 24000) {
        if (truncm > 239)
            truncm = 239;
        src_wr(SR_ADC, (((239 - truncm) >> 1) << 9) | (n << 4));
    } else {
        if (truncm > 119)
            truncm = 119;
        src_wr(SR_ADC, 0x8000 | (((119 - truncm) >> 1) << 9) | (n << 4));
    }
    src_wr(SR_ADC + 1, (src_rd(SR_ADC + 1) & 0x00FF) | ((freq >> 5) & 0xFC00));
    src_wr(SR_ADC + 3, freq & 0x7FFF);
    src_wr(SR_VOL_ADC, n << 8);
    src_wr(SR_VOL_ADC + 1, n << 8);
}

/* ---- the AC'97 codec, through 14h */
static int codec_idle(void)
{
    u32 i;
    for (i = 0; i < 4096; i++) {
        if (!(rd32(CODEC) & CODEC_WIP))
            return 1;
        us(1);
    }
    return 0;
}

/* A codec access in the converter's safe window, as FreeBSD's es137x makes it (its es1371_wrcd and _rdcd, BSD
   licence; the source read 2026-11-23, sha256 0e8da270...d698): the SRC's state put on its mux (bit 16, the
   disables kept), a wait until bits 16-18 and 23 say 0, then until they say 1 (0x00010000), the access, and the
   SRC's disables put back once it is ready again. An access outside that window can be lost on some of the
   chips. In 86Box those bits always read 0 (its model drops them): the second wait runs its 4096 reads out. */
static u32 codec_window(void)
{
    u32 orig, i;
    src_ready();
    orig = rd32(SRC);
    wr32(SRC, es.srcdis | 0x00010000u);
    for (i = 0; i < 4096 && (rd32(SRC) & 0x00870000u) != 0; i++)
        ;
    for (i = 0; i < 4096 && (rd32(SRC) & 0x00870000u) != 0x00010000u; i++)
        ;
    return orig;
}

static void codec_window_end(u32 orig)
{
    (void)orig;                                 /* (its disables as written, not as read: src_ready's note) */
    src_ready();
    wr32(SRC, es.srcdis);
}

static u16 codec_rd(void *ctx, u8 reg)
{
    u32 i, v = 0, orig;
    (void)ctx;
    codec_idle();
    orig = codec_window();
    wr32(CODEC, CODEC_RD | ((u32)(reg & 0x7F) << 16));
    codec_window_end(orig);
    for (i = 0; i < 4096; i++) {
        v = rd32(CODEC);
        if ((v & CODEC_RDY) && ((v >> 16) & 0x7F) == (reg & 0x7Fu))
            return (u16)v;
        us(1);
    }
    return 0xFFFF;
}

static void codec_wr(void *ctx, u8 reg, u16 v)
{
    u32 orig;
    (void)ctx;
    codec_idle();
    orig = codec_window();
    wr32(CODEC, ((u32)(reg & 0x7F) << 16) | v);
    codec_window_end(orig);
}

static void codec_pause(void *ctx, u32 ms)
{
    (void)ctx;
    us(ms * 1000);
}

int es1371_init(void)
{
    static const u16 ids[] = { 0x1371, 0x5880 };        /* (1274:1371 the ES1371 and ES1373, 1274:5880 the CT5880) */
    struct pci_dev *pd = 0;
    char w[8], r[48];
    u32 cmd, i;
    int ct5880, ok;
    for (i = 0; i < sizeof ids / sizeof ids[0] && !pd; i++)
        pd = pci_find(0x1274, ids[i], 0);
    if (!pd || dev_pci_off(pd, "es1371"))
        return -1;
    if (!(pd->bar[0] & 1) || !(pd->bar[0] & ~3u)) {
        kprintf("GLOS-ES refused no-io\n");
        dev_note(pci_where(pd, w), "es1371", DEVS_REFUSED, "no-io", 0);
        return -1;
    }
    if (!pd->pin || pd->irq == 0 || pd->irq > 15) {
        kprintf("GLOS-ES refused no-irq\n");
        dev_note(pci_where(pd, w), "es1371", DEVS_REFUSED, "no-irq", 0);
        return -1;
    }
    if (mixer_pci_line(pd, "es1371", "ES") != 0)
        return -1;
    es.pci = pd;
    es.io = (u16)(pd->bar[0] & 0xFFFCu);
    es.irq = pd->irq;
    cmd = pci_read32(pd->bus, pd->dev, pd->fn, 0x04);
    pci_write32(pd->bus, pd->dev, pd->fn, 0x04, (cmd & 0xFFFF) | 0x05);       /* (I/O and bus master) */
    es.control = 0, es.serial = 0;
    wr32(CONTROL, 0);                           /* every channel stopped, ... */
    wr32(SERIAL, 0);
    wr32(LEGACY, 0);                            /* ... no SB trapping */
    if (pd->device == 0x5880 && pd->rev >= 2 && pd->rev <= 4) {
        /* The CT5880's revisions C to E: STATUS bit 29 is their AC'97 codec's reset line, 0 (as at power-on) holding
           it in reset. Pulsed on every start, then 20 ms for the codec to wake, before the link's warm reset (as
           Linux's ens1371 sets it, its behaviour, not its code). Bench F, 2026-11-24: never set, the codec never
           answered (all ones); set once, it answered after a power cycle but not after GLOS's own restart, with
           the bit already set (no edge: all zeros). */
        wr32(STATUS, 0);
        us(100);
        wr32(STATUS, 1u << 29);
        us(20000);
    }
    wr32(CONTROL, C_SYNCRES);                   /* the AC'97 link's warm reset */
    us(20);
    wr32(CONTROL, 0);
    us(2000);
    /* The converter: off while its RAM is cleared and set (on without its parameters it can stop the chip) */
    src_disables(SRC_DIS);
    for (i = 0; i < 0x80; i++)
        src_wr(i, 0);
    src_wr(SR_DAC1, 16 << 4), src_wr(SR_DAC1 + 1, 16 << 10);
    src_wr(SR_DAC2, 16 << 4), src_wr(SR_DAC2 + 1, 16 << 10);
    src_wr(SR_VOL_ADC, 1 << 12), src_wr(SR_VOL_ADC + 1, 1 << 12);
    src_wr(SR_VOL_DAC1, 1 << 12), src_wr(SR_VOL_DAC1 + 1, 1 << 12);
    src_wr(SR_VOL_DAC2, 1 << 12), src_wr(SR_VOL_DAC2 + 1, 1 << 12);
    adc_rate(22050);                            /* (every rate set before the converter runs: on with one unset it
                                                   can stop the chip until the PC's power is cut, FreeBSD's
                                                   es137x says) */
    dac_rate(SR_DAC1, SRC_DIS_P1, 22050);
    dac2_rate(22050);
    src_disables(SRC_DIS_P1 | SRC_DIS_R1);      /* on: DAC2 only (DAC1 and the ADC until they are used) */
    es.ac.rd = codec_rd, es.ac.wr = codec_wr, es.ac.pause = codec_pause, es.ac.ctx = 0;
    ct5880 = pd->device == 0x5880 && pd->rev >= 2 && pd->rev <= 4;
    ok = 0;
    for (i = 0; ct5880 && i < 3 && !(ok = ac97_start(&es.ac) == 0); i++)
        if (i < 2) {
            /* (bench F's CT5880, about one GLOS start in three: the codec answers, its ready bits still clear
               after the half second ac97_start waits. Its reset pulsed again, a longer wake, the link reset
               again, and another try: three in all) */
            kprintf("GLOS-ES codec again try=%u\n", i + 2);
            wr32(STATUS, 0);
            us(1000);
            wr32(STATUS, 1u << 29);
            us(100000);
            wr32(CONTROL, C_SYNCRES);
            us(20);
            wr32(CONTROL, 0);
            us(2000);
        }
    if (!ct5880)
        ok = ac97_start(&es.ac) == 0;
    if (!ok) {
        kprintf("GLOS-ES refused no-codec\n");
        dev_note(pci_where(pd, w), "es1371", DEVS_REFUSED, "no-codec", 0);
        return -1;
    }
    es.found = 1;
    pci_claim(pd);                              /* (GLOS's: DOS no longer finds it on the PCI bus) */
    kprintf("GLOS-ES found io=%x irq=%u id=%04x rev=%u\n", es.io, es.irq, pd->device, pd->rev);
    ksnprintf(r, sizeof r, "io=%04x-%04x irq=%u", es.io, es.io + 0x3F, es.irq);
    dev_note(pci_where(pd, w), "es1371", DEVS_ON, "", r);
    return 0;
}

static u32 calls;                       /* (the handler's calls: said at stop) */

/* The card's interrupt: each channel's said and acknowledged, again until it says none, so that the line is low
   when this returns (an edge-triggered PIC sees no new edge from a line that stayed high: playback stopped for
   good, once in Loop A's first runs). */
static void irq(struct trapframe *tf)
{
    u32 st, rounds;
    calls++;
    for (rounds = 0; rounds < 4 && ((st = rd32(STATUS)) & (ST_ADC | ST_DAC2)); rounds++) {
    if (st & ST_ADC) {                          /* a half recorded */
        wr32(SERIAL, es.serial & ~S_R1INT);
        wr32(SERIAL, es.serial);
        if (es.rec && es.got) {
            es.got((const s16 *)(es.rbuf + es.rhalf * es.rframes * 4), es.rframes);
            es.rhalf ^= 1;
        }
    }
    if (st & ST_DAC2) {                         /* a half played: filled again */
        wr32(SERIAL, es.serial & ~S_P2INT);
        wr32(SERIAL, es.serial);
        if (es.running) {
            es.irqs++;
            es.fill((s16 *)(es.buf + es.half * es.frames * 4), es.frames, tf);
            es.half ^= 1;
        }
    }
    }
}

static void claim(void)
{
    if (!es.claimed) {
        vm_claim_irq(es.irq, irq);
        es.claimed = 1;
    }
}

static int fits(u32 rate, u32 frames) { return rate >= 4000 && rate <= 48000 && frames >= 16 && frames * 2 <= 65536; }

static int es_start(u32 rate, u32 frames, mixer_fill fill)
{
    if (!es.found || !fits(rate, frames)) {
        kprintf("GLOS-ES error=rate-or-size rate=%u frames=%u\n", rate, frames);
        return -1;
    }
    if (!es.buf || frames > es.frames) {
        if (!(es.buf = dma_alloc((frames * 8 + 4095) / 4096, &es.phys))) {
            kprintf("GLOS-ES error=dma-buffer\n");
            return -1;
        }
    }
    es.frames = frames, es.fill = fill, es.half = 0;
    fill((s16 *)es.buf, frames, 0);
    fill((s16 *)(es.buf + frames * 4), frames, 0);
    claim();
    es.control &= ~C_DAC2;
    wr32(CONTROL, es.control);
    dac2_rate(rate);
    wr32(MEMPAGE, 0x0C);                        /* DAC2's frame: the buffer, its longwords less one */
    wr32(0x38, es.phys);
    wr32(0x3C, frames * 2 - 1);
    wr32(DAC2_SCOUNT, frames - 1);              /* an interrupt each half */
    es.serial = (es.serial & ~(0x3Fu << 16 | 0x4000u | 0x1000u | 0x0Cu | 0x40u | S_P2INT)) | (2u << 19) | (3u << 2);
    wr32(SERIAL, es.serial);                    /* (its interrupt off a moment: anything left from before cleared) */
    es.serial |= S_P2INT;
    wr32(SERIAL, es.serial);
    es.running = 1;
    es.control |= C_DAC2;
    wr32(CONTROL, es.control);
    kprintf("GLOS-ES start rate=%u frames=%u buf=%x\n", rate, frames, es.phys);

    return 0;
}

static void es_stop(void)
{
    if (!es.found || !es.running)
        return;
    es.control &= ~C_DAC2;
    wr32(CONTROL, es.control);
    es.serial &= ~S_P2INT;
    wr32(SERIAL, es.serial);
    es.running = 0;
    kprintf("GLOS-ES stop irqs=%u calls=%u\n", es.irqs, calls);
}

static int es_running(void) { return es.running; }
/* The mixer's watchdog (mixer_tick: silent for 128 ticks): the status looked at as an interrupt would, and every
   64th time the chip's own words said, so that a card that never interrupts says why (bench F's CT5880,
   2026-11-24: not one interrupt, and none seen by looking either). DAC2's frame register (page 0Ch, 3Ch) and its
   sample count (28h) have the counts in their upper halves: still, DAC2 has not run. */
static void es_kick(void)
{
    static u32 n;
    irq(0);
    if (n++ % 64 == 0) {
        u32 page = rd32(MEMPAGE) & 0x0F, fsz;
        wr32(MEMPAGE, 0x0C);
        fsz = rd32(0x3C);
        wr32(MEMPAGE, page);
        kprintf("GLOS-ES kick n=%u st=%08x ctl=%08x ser=%08x src=%08x frame=%08x count=%08x\n", n, rd32(STATUS),
                rd32(CONTROL), rd32(SERIAL), rd32(SRC), fsz, rd32(DAC2_SCOUNT));
    }
}

static u32 es_irqs(void) { return es.irqs; }
static u32 es_errors(void) { return 0; }

static int es_rec_start(u32 rate, u32 frames, mixer_got got)
{
    if (!es.found || !fits(rate, frames))
        return -1;
    if (!es.rbuf || frames > es.rframes) {
        if (!(es.rbuf = dma_alloc((frames * 8 + 4095) / 4096, &es.rphys)))
            return -1;
    }
    es.rframes = frames, es.got = got, es.rhalf = 0;
    claim();
    es.control &= ~C_ADC;
    wr32(CONTROL, es.control);
    src_disables(SRC_DIS_P1 | SRC_DIS_R1 | (es.running ? 0 : SRC_DIS_P2));
    adc_rate(rate);
    src_disables(SRC_DIS_P1 | (es.running ? 0 : SRC_DIS_P2));  /* (the ADC's own disable cleared) */
    wr32(MEMPAGE, 0x0D);                        /* the ADC's frame */
    wr32(0x30, es.rphys);
    wr32(0x34, frames * 2 - 1);
    wr32(ADC_SCOUNT, frames - 1);
    es.serial = (es.serial & ~(0x8000u | 0x30u)) | (3u << 4) | S_R1INT;
    wr32(SERIAL, es.serial);
    es.rec = 1;
    es.control |= C_ADC;
    wr32(CONTROL, es.control);
    return 0;
}

static void es_rec_stop(void)
{
    if (!es.found || !es.rec)
        return;
    es.control &= ~C_ADC;
    wr32(CONTROL, es.control);
    es.serial &= ~S_R1INT;
    wr32(SERIAL, es.serial);
    es.rec = 0;
}

static int es_chan(int chan, int set, u8 *left, u8 *right, u8 *mute)
{
    if (set && es.found)                        /* (a change said, with how playback stands) */
        kprintf("GLOS-ES chan %d set irqs=%u st=%x sctl=%x\n", chan, es.irqs, rd32(STATUS), rd32(SERIAL));
    return es.found ? ac97_chan(&es.ac, chan, set, left, right, mute) : -1;
}

static int es_input(int set, int *input) { return es.found ? ac97_input(&es.ac, set, input) : -1; }

void es1371_leave(void)                 /* glos exit: both channels stopped (a bus master never left running) */
{
    if (!es.found)
        return;
    wr32(CONTROL, 0);
    wr32(SERIAL, 0);
    es.running = es.rec = 0;
    kprintf("GLOS-ES left\n");
}

const struct sound_card es1371_card = { "es1371", es_start, es_stop, es_running, es_irqs, es_errors, es_chan, es_input,
                                        es_rec_start, es_rec_stop, es_kick };
