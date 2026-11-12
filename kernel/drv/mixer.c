/* GLOS's mixer (PRD §10.5; docs/milestones-m8.md step E): the apps' streams
 * of sound into the card: a Sound Blaster 16 (sb16.c), found at the start
 * from BLASTER, or else a Crystal CS423x's codec (crystal.c), which GLOS sets
 * up once GLOS.CFG has been read ([sound] crystal; docs/crystal-audio.md).
 *
 * A stream is a ring of signed 16-bit samples in pages shared with its app
 * (struct glos_audio_ring, include/glos/api.h): the app writes, the mixer reads,
 * each moving its own count. The card plays 16-bit stereo at the mixer's
 * rate ([sound] rate, 22050 by default) from two halves of a period each
 * (256 frames); at the end of each half its interrupt has the mixer fill that
 * half again, from every stream at once: the stream's rate converted with a
 * step in 16.16 fixed point (linear between neighbouring samples), mono
 * doubled, its volume applied, the sum clipped. All in integers, in the
 * interrupt: a period's worth is little work, and refilling never waits for
 * an app to run.
 *
 * A stream begins with its first samples. A period in which a stream that has
 * begun has fewer samples than the period needs is an underrun: what there is
 * is played, then silence. GLOS counts both in the stream's header, where the
 * app (SDL's GLOS driver) reads them.
 *
 * While a session runs the card is the session's (session.c): the mixer stops
 * it and starts it again after; the apps are paused meanwhile, and their
 * streams with them. */
#include <string.h>

#include "kapi.h"
#include "glos/bootinfo.h"
#include "app.h"
#include "arch.h"
#include "dpmi.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "midi.h"
#include "mixer.h"
#include "mixgain.h"
#include "mm.h"
#include "sched.h"
#include "timer.h"

#define STREAMS   8
#define PERIOD    256                           /* frames */
#define HEADER    64                            /* the ring's offset in the stream's pages */

static struct strm {
    struct app *a;                              /* 0: free */
    u32 id;
    struct glos_audio_ring *h;                      /* the kernel's view (its pages: mm_win_alloc) */
    u32 pages;
    u8 *ring;
    u32 step, frac;                             /* 16.16: stream frames a mixer frame */
    u8 begun, frame_bytes;
    u32 lin;                                    /* where its app has it */
    u32 short_run, bursts;                      /* underruns in a row now; bursts logged */
    /* API 2.0's (AUDIO's OUT and MIXER) */
    s16 balance;                                /* -256 all left to 256 all right */
    u8 mute, paused;
    u8 want;                                    /* it has fallen below its threshold: its app is to be told, once */
    u16 level[2];                               /* its peaks in the period played last, as it went into the sum */
    u32 threshold;                              /* bytes queued below which its app wants to know (0: it doesn't) */
} st[STREAMS];

static struct {
    u8 on;                                      /* the card found and the mixer wanted */
    u8 out;                                     /* a session has the card */
    u8 found;                                   /* the card is there (sb16_init, crystal_init) */
    u8 desk;                                    /* the desktop wants it playing (mixer_desktop) */
    const struct bootinfo *bi;
    const struct sound_card *card;
    u32 rate, next_id, periods;
    s32 acc[PERIOD * 2];
    u32 master;                                 /* the master's gain, 0 to MIX_UNITY ([sound] volume) ... */
    u8 mute;                                    /* ... and its mute ([sound] mute) */
    u16 level[2];                               /* the output's peaks in the period played last */
} mx;

static void fill(s16 *out, u32 frames, const struct trapframe *tf);

/* Streams in (API 2.0's AUDIO IN): what the card records, into each one's ring at its own rate and shape. The
   ring is a stream out's the other way round: the mixer writes and moves `write`, the app reads and moves
   `read`. One with no room loses what came (counted in its header's `underruns`). */
#define IN_STREAMS 4
static struct instrm {
    struct app *a;                              /* 0: free */
    u32 id;
    struct glos_audio_ring *h;
    u32 pages;
    u8 *ring;
    u32 step, frac;                             /* 16.16: recorded frames a stream frame */
    u8 frame_bytes, want;
    u32 lin, threshold;
    u16 level[2];
} in[IN_STREAMS];
static u8 recording;                            /* the card is recording */
static u16 in_level[2];                         /* what was recorded last: its peaks */

static void got(const s16 *smp, u32 frames);
static void rec_update(void);

int mixer_init(const struct bootinfo *bi)
{
    mx.rate = bi->sound_rate ? bi->sound_rate : 22050;
    mx.next_id = 1;
    mx.master = MIX_UNITY;
    mx.out = 1;                                 /* (until the desktop is up) */
    mx.bi = bi;
    if ((bi->sound_flags & BI_SOUND_OFF) || sb16_init(bi) != 0)
        return -1;
    mx.card = &sb16_card;
    mx.found = mx.on = 1;
    return 0;
}

/* GLOS.CFG read (config.c): with no SB16, a Crystal chip set up ([sound] crystal), even with the mixer off, so
   that programs find its SB Pro; the mixer plays through its codec when it is on. */
static void late(void);
void mixer_late(void)
{
    late();
    midi_late();                                /* (a Crystal chip's FM chip and MPU-401: midi.c) */
}

static void late(void)
{
    char v[96];
    if (mx.card)
        return;
    v[0] = 0;                                   /* (kcfg_get leaves it alone for a key the schema lacks) */
    kcfg_get("sound", "crystal", v, sizeof v);
    if (crystal_init(v) != 0)
        return;
    mx.card = &crystal_card;
    if (mx.bi->sound_flags & BI_SOUND_OFF)
        return;
    mx.found = mx.on = 1;
    if (mx.desk)
        mixer_desktop(1);
}

/* [sound] mixer changed (M9): the card found now if it wasn't looked for, and played if the desktop has it. */
void mixer_enable(int on)
{
    if (on && !mx.on) {
        if (!mx.found && !mx.card) {
            if (sb16_init(mx.bi) != 0)
                return;
            mx.card = &sb16_card;
        }
        mx.found = mx.on = 1;
        kprintf("GLOS-AUDIO mixer on\n");
        if (mx.desk)
            mixer_desktop(1);
    } else if (!on && mx.on) {
        mx.card->stop();
        mx.out = 1;
        mx.on = 0;
        kprintf("GLOS-AUDIO mixer off\n");
    }
}

/* [sound] rate changed (M9): the card started again at the new rate, every stream's step made anew. */
void mixer_set_rate(u32 rate)
{
    u32 i, f, playing = mx.card && mx.card->running();
    if (rate < 4000 || rate > 48000 || rate == mx.rate)
        return;
    if (recording && mx.card->rec_stop) {
        mx.card->rec_stop();
        recording = 0;
    }
    if (playing)
        mx.card->stop();
    f = irq_save();
    mx.rate = rate;
    for (i = 0; i < STREAMS; i++)
        if (st[i].a) {
            st[i].step = (st[i].h->rate << 16) / rate;
            st[i].h->mixer_rate = rate;
        }
    for (i = 0; i < IN_STREAMS; i++)
        if (in[i].a) {
            in[i].step = (rate << 16) / in[i].h->rate;
            in[i].h->mixer_rate = rate;
        }
    irq_restore(f);
    kprintf("GLOS-AUDIO rate=%u\n", rate);
    if (playing && mx.card->start(mx.rate, PERIOD, fill) != 0)
        mx.on = 0;
    rec_update();
}

int mixer_on(void) { return mx.on && !mx.out; }
int mixer_found(void) { return mx.on; }

/* One stream's samples for the period, added into mx.acc. tf: what the
   card's interrupt broke into; an underrun while it was the stream's own app
   running its code says where in it (the first few of each burst: where a
   program keeps its sound waiting). */
static void take(struct strm *s, u32 frames, const struct trapframe *tf)
{
    struct glos_audio_ring *h = s->h;
    u32 have = (h->write - h->read) / s->frame_bytes, need, i, mask = h->bytes - 1, gl, gr;
    u32 pos = h->read, frac = s->frac, pl = 0, pr = 0;
    s->level[0] = s->level[1] = 0;
    if (s->paused)
        return;                                 /* (what it has waits; no period of its own goes by) */
    mix_gains(h->volume, s->balance, s->mute, &gl, &gr);
    if (!s->begun) {
        if (!have)
            return;
        s->begun = 1;
    }
    need = (u32)(((unsigned long long)frames * s->step + frac + 0xFFFF) >> 16) + 1;       /* (the last one's neighbour) */
    h->periods++;
    if (have < need) {
        h->underruns++;
        if (tf && s->short_run < 6 && s->bursts < 16) {
            if ((tf->cs & 3) == 3 && app_cur() == s->a)
                kprintf("GLOS-AUDIO stall stream=%u eip=%x\n", s->id, tf->eip);
            else                                /* (the app waits: for whom) */
                kprintf("GLOS-AUDIO stall stream=%u in=%s%s eip=%x\n", s->id, current->name,
                        (tf->eflags & 0x20000) ? ":v86" : (tf->cs & 3) ? ":user" : "", tf->eip);
        }
        s->short_run++;
    } else if (s->short_run) {                  /* a burst of them over: when, and how long (the first 16) */
        if (s->bursts++ < 16)
            kprintf("GLOS-AUDIO short stream=%u periods=%u t=%u\n", s->id, s->short_run, timer_ticks());
        s->short_run = 0;
    }
    for (i = 0; i < frames; i++) {
        u32 at = frac >> 16, f = frac & 0xFFFF;
        s32 l, r;
        if (at + 1 >= have)
            break;                              /* (the rest silence) */
        if (s->frame_bytes == 2) {
            s16 a = *(s16 *)(s->ring + ((pos + at * 2) & mask)), b = *(s16 *)(s->ring + ((pos + at * 2 + 2) & mask));
            l = r = a + (((b - a) * (s32)f) >> 16);
        } else {
            u32 p = pos + at * 4;
            s16 al = *(s16 *)(s->ring + (p & mask)), ar = *(s16 *)(s->ring + ((p + 2) & mask));
            s16 bl = *(s16 *)(s->ring + ((p + 4) & mask)), br = *(s16 *)(s->ring + ((p + 6) & mask));
            l = al + (((bl - al) * (s32)f) >> 16);
            r = ar + (((br - ar) * (s32)f) >> 16);
        }
        l = (l * (s32)gl) >> 8;
        r = (r * (s32)gr) >> 8;
        mx.acc[i * 2] += l;
        mx.acc[i * 2 + 1] += r;
        if ((u32)(l < 0 ? -l : l) > pl)
            pl = (u32)(l < 0 ? -l : l);
        if ((u32)(r < 0 ? -r : r) > pr)
            pr = (u32)(r < 0 ? -r : r);
        frac += s->step;
    }
    s->level[0] = (u16)(pl > 32767 ? 32767 : pl);
    s->level[1] = (u16)(pr > 32767 ? 32767 : pr);
    i = frac >> 16;                             /* frames used up */
    if (i > have)
        i = have;
    h->read = pos + i * s->frame_bytes;
    s->frac = frac & 0xFFFF;
    if (s->threshold && h->write - h->read < s->threshold)
        s->want |= 1;                           /* (told from the app's tick: mixer_wants) */
    else
        s->want = 0;
}

/* The card's interrupt: a half to fill (16-bit stereo). */
static void fill(s16 *out, u32 frames, const struct trapframe *tf)
{
    u32 i;
    memset(mx.acc, 0, frames * 2 * sizeof mx.acc[0]);
    for (i = 0; i < STREAMS && !apps_paused(); i++)    /* (paused apps' streams wait with them) */
        if (st[i].a)
            take(&st[i], frames, tf);
    {
        u16 peak[2] = { 0, 0 };
        mix_master(mx.acc, out, frames, mx.master, mx.mute, peak);
        mx.level[0] = peak[0], mx.level[1] = peak[1];
    }
    mx.periods++;
}

/* The mixer is the desktop's: it plays while the desktop has the screen, and
   a session (or no desktop) has the card. */
void mixer_desktop(int on)
{
    mx.desk = (u8)(on != 0);
    midi_desktop(on);                           /* (the MIDI ports, with or without a mixer) */
    if (!mx.on)
        return;
    if (on) {
        mx.out = 0;
        if (!mx.card->running() && mx.card->start(mx.rate, PERIOD, fill) != 0)
            mx.on = 0;
    } else {
        if (recording && mx.card->rec_stop)
            mx.card->rec_stop();                /* (a session's from here: both ways) */
        recording = 0;
        mx.card->stop();
        mx.out = 1;
    }
    rec_update();
}

/* The card records while the mixer plays and a stream in is open. */
static void rec_update(void)
{
    u32 i, n = 0;
    for (i = 0; i < IN_STREAMS; i++)
        n += in[i].a != 0;
    if (!mx.card || !mx.card->rec_start)
        return;
    if (n && !recording && mx.on && !mx.out && mx.card->running()) {
        if (mx.card->rec_start(mx.rate, PERIOD, got) == 0)
            recording = 1;
    } else if (recording && (!n || mx.out || !mx.on)) {
        mx.card->rec_stop();
        recording = 0;
        in_level[0] = in_level[1] = 0;
    }
}

/* A half recorded (the card's interrupt): into every stream in. */
static void got(const s16 *smp, u32 frames)
{
    u32 i, k, pl = 0, pr = 0;
    for (k = 0; k < frames; k++) {
        u32 l = (u32)(smp[k * 2] < 0 ? -smp[k * 2] : smp[k * 2]), r = (u32)(smp[k * 2 + 1] < 0 ? -smp[k * 2 + 1] : smp[k * 2 + 1]);
        if (l > pl)
            pl = l;
        if (r > pr)
            pr = r;
    }
    in_level[0] = (u16)(pl > 32767 ? 32767 : pl);
    in_level[1] = (u16)(pr > 32767 ? 32767 : pr);
    for (i = 0; i < IN_STREAMS; i++) {
        struct instrm *s = &in[i];
        struct glos_audio_ring *h = s->h;
        u32 frac, mask, w;
        if (!s->a)
            continue;
        s->level[0] = in_level[0], s->level[1] = in_level[1];
        frac = s->frac, mask = h->bytes - 1, w = h->write;
        while ((frac >> 16) < frames) {
            u32 at = frac >> 16, f = frac & 0xFFFF, nx = at + 1 < frames ? at + 1 : at;
            s32 l = smp[at * 2] + (((smp[nx * 2] - smp[at * 2]) * (s32)f) >> 16);
            s32 r = smp[at * 2 + 1] + (((smp[nx * 2 + 1] - smp[at * 2 + 1]) * (s32)f) >> 16);
            if (h->bytes - (w - h->read) < s->frame_bytes) {
                h->underruns++;                 /* (no room: the app is behind; what came is lost) */
                break;
            }
            if (s->frame_bytes == 2) {
                *(s16 *)(s->ring + (w & mask)) = (s16)((l + r) / 2);
            } else {
                *(s16 *)(s->ring + (w & mask)) = (s16)l;
                *(s16 *)(s->ring + ((w + 2) & mask)) = (s16)r;
            }
            w += s->frame_bytes;
            frac += s->step;
        }
        if ((frac >> 16) >= frames)
            frac -= frames << 16;
        else
            frac &= 0xFFFF;                     /* (lost: from the next half's start) */
        s->frac = frac;
        h->write = w;
        h->periods++;
        if (s->threshold && w - h->read >= s->threshold)
            s->want |= 1;
        else
            s->want = 0;
    }
}

/* ---- streams (GLOS_FN_AUDIO, on the app's thread) */

int mixer_open(struct app *a, u32 rate, u32 channels, u32 bytes, u32 *id, u32 *lin)
{
    struct strm *s = 0;
    struct glos_audio_ring *h;
    u32 i, f;
    if (!mx.on)
        return GLOS_E_NO_AUDIO;
    if (rate < 4000 || rate > 48000 || (channels != 1 && channels != 2) || bytes < 4096 || bytes > 65536
        || (bytes & (bytes - 1)))
        return GLOS_E_ARGUMENT;
    for (i = 0; i < STREAMS && !s; i++)
        if (!st[i].a)
            s = &st[i];
    if (!s || !(h = mm_win_alloc(HEADER + bytes)))
        return GLOS_E_MEMORY;
    if (win_map(h, HEADER + bytes, lin) != 0) {
        mm_win_free(h, HEADER + bytes);
        return GLOS_E_MEMORY;
    }
    h->size = sizeof *h;
    h->rate = rate;
    h->channels = channels;
    h->bytes = bytes;
    h->mixer_rate = mx.rate;
    h->period_frames = PERIOD;
    h->volume = 256;
    h->ring = HEADER;
    f = irq_save();
    s->h = h;
    s->pages = HEADER + bytes;
    s->ring = (u8 *)h + HEADER;
    s->step = (rate << 16) / mx.rate;          /* (48000 << 16 fits in 32 bits) */
    s->frac = 0;
    s->begun = 0;
    s->frame_bytes = (u8)(channels * 2);
    s->short_run = s->bursts = 0;
    s->balance = 0;
    s->mute = s->paused = s->want = 0;
    s->level[0] = s->level[1] = 0;
    s->threshold = 0;
    s->id = mx.next_id++;
    s->lin = *lin;
    s->a = a;
    irq_restore(f);
    *id = s->id;
    kprintf("GLOS-AUDIO open app=%u stream=%u rate=%u channels=%u ring=%u\n", app_id(a), s->id, rate, channels, bytes);
    return 0;
}

int mixer_records(void) { return mx.on && mx.card && mx.card->rec_start != 0; }

u32 mixer_streams_in(void)
{
    u32 i, n = 0;
    for (i = 0; i < IN_STREAMS; i++)
        n += in[i].a != 0;
    return n;
}

int mixer_open_in(struct app *a, u32 rate, u32 channels, u32 bytes, u32 *id, u32 *lin)
{
    struct instrm *s = 0;
    struct glos_audio_ring *h;
    u32 i, f;
    if (!mixer_records())
        return GLOS_E_NO_AUDIO;
    if (rate < 4000 || rate > 48000 || (channels != 1 && channels != 2) || bytes < 4096 || bytes > 65536
        || (bytes & (bytes - 1)))
        return GLOS_E_ARGUMENT;
    for (i = 0; i < IN_STREAMS && !s; i++)
        if (!in[i].a)
            s = &in[i];
    if (!s || !(h = mm_win_alloc(HEADER + bytes)))
        return GLOS_E_MEMORY;
    if (win_map(h, HEADER + bytes, lin) != 0) {
        mm_win_free(h, HEADER + bytes);
        return GLOS_E_MEMORY;
    }
    h->size = sizeof *h;
    h->rate = rate;
    h->channels = channels;
    h->bytes = bytes;
    h->mixer_rate = mx.rate;
    h->period_frames = PERIOD;
    h->volume = 256;
    h->ring = HEADER;
    f = irq_save();
    s->h = h;
    s->pages = HEADER + bytes;
    s->ring = (u8 *)h + HEADER;
    s->step = (mx.rate << 16) / rate;           /* (recorded frames a stream frame) */
    s->frac = 0;
    s->frame_bytes = (u8)(channels * 2);
    s->want = 0;
    s->threshold = 0;
    s->level[0] = s->level[1] = 0;
    s->id = mx.next_id++;
    s->lin = *lin;
    s->a = a;
    irq_restore(f);
    *id = s->id;
    rec_update();
    kprintf("GLOS-AUDIO open-in app=%u stream=%u rate=%u channels=%u ring=%u recording=%u\n", app_id(a), s->id, rate, channels,
            bytes, recording);
    return 0;
}

static void drop_in(struct instrm *s)
{
    u32 f = irq_save();
    struct glos_audio_ring *h = s->h;
    u32 pages = s->pages;
    kprintf("GLOS-AUDIO close-in app=%u stream=%u halves=%u lost=%u\n", app_id(s->a), s->id, h->periods, h->underruns);
    s->a = 0;
    irq_restore(f);
    mm_win_free(h, pages);
    rec_update();
}

static void drop(struct strm *s)
{
    u32 f = irq_save();
    struct glos_audio_ring *h = s->h;
    u32 pages = s->pages;
    kprintf("GLOS-AUDIO close app=%u stream=%u periods=%u underruns=%u%s%u\n", app_id(s->a), s->id, h->periods,
            h->underruns, " short-at-close=", s->short_run);
    s->a = 0;
    irq_restore(f);
    mm_win_free(h, pages);                      /* (its mapping in the app: undone by the caller, or gone with it) */
}

int mixer_close(struct app *a, u32 id)
{
    u32 i;
    for (i = 0; i < STREAMS; i++)
        if (st[i].a == a && st[i].id == id) {
            phys_unmap(st[i].lin);              /* (its own thread: its own address space) */
            drop(&st[i]);
            return 0;
        }
    for (i = 0; i < IN_STREAMS; i++)
        if (in[i].a == a && in[i].id == id) {
            phys_unmap(in[i].lin);
            drop_in(&in[i]);
            return 0;
        }
    return GLOS_E_ARGUMENT;
}

void mixer_app_gone(struct app *a)
{
    u32 i;
    for (i = 0; i < STREAMS; i++)
        if (st[i].a == a)
            drop(&st[i]);
    for (i = 0; i < IN_STREAMS; i++)
        if (in[i].a == a)
            drop_in(&in[i]);
}

u32 mixer_stat(char *buf, u32 max)
{
    u32 n, i;
    if (!mx.on) {
        n = (u32)ksnprintf(buf, max, "audio off\n");
        return n + midi_stat(buf + n, max - n);
    }
    n = (u32)ksnprintf(buf, max, "audio %s rate=%u period=%u periods=%u irqs=%u card=%s errors=%u\n",
                       mx.out ? "idle" : "on", mx.rate, PERIOD, mx.periods, mx.card->irqs(), mx.card->name,
                       mx.card->errors());
    for (i = 0; i < STREAMS && n < max; i++)
        if (st[i].a)
            n += (u32)ksnprintf(buf + n, max - n, "stream %u app=%u rate=%u channels=%u periods=%u underruns=%u\n",
                                st[i].id, app_id(st[i].a), st[i].h->rate, st[i].h->channels, st[i].h->periods,
                                st[i].h->underruns);
    return n + midi_stat(buf + n, n < max ? max - n : 0);
}

/* For GLOS_FN_STAT (M9): the rate, the streams open, and their underruns all told. */
void mixer_counts(u32 *rate, u32 *streams, u32 *underruns)
{
    u32 i, f = irq_save();
    *rate = mx.on ? mx.rate : 0;
    *streams = *underruns = 0;
    for (i = 0; i < STREAMS; i++)
        if (st[i].a) {
            (*streams)++;
            *underruns += st[i].h->underruns;
        }
    irq_restore(f);
}

/* ---- API 2.0's AUDIO (kernel/app/api2audio.c): a stream's properties, the master, the streams, levels, the
   chip's channels. a: the app that asks for a stream of its own; NULL: any app's (the Mixer's). */

static struct strm *find(struct app *a, u32 id)
{
    u32 i;
    for (i = 0; i < STREAMS; i++)
        if (st[i].a && st[i].id == id && (!a || st[i].a == a))
            return &st[i];
    return 0;
}

static struct instrm *find_in(struct app *a, u32 id)
{
    u32 i;
    for (i = 0; i < IN_STREAMS; i++)
        if (in[i].a && in[i].id == id && (!a || in[i].a == a))
            return &in[i];
    return 0;
}

int mixer_get(struct app *a, u32 id, int what, s32 *v)
{
    u32 f = irq_save();
    struct strm *s = find(a, id);
    struct instrm *n = s ? 0 : find_in(a, id);
    int r = 0;
    if (n) {                                    /* a stream in: how much it has recorded, what waits, its threshold */
        if (what == MX_POSITION)
            *v = (s32)(n->h->write / n->frame_bytes);
        else if (what == MX_QUEUED)
            *v = (s32)(n->h->write - n->h->read);
        else if (what == MX_THRESHOLD)
            *v = (s32)n->threshold;
        else
            r = -2;
        irq_restore(f);
        return r;
    }
    if (!s) {
        irq_restore(f);
        return -1;
    }
    switch (what) {
    case MX_VOLUME: *v = (s32)s->h->volume; break;
    case MX_BALANCE: *v = s->balance; break;
    case MX_MUTE: *v = s->mute; break;
    case MX_PAUSE: *v = s->paused; break;
    case MX_THRESHOLD: *v = (s32)s->threshold; break;
    case MX_POSITION: *v = (s32)(s->h->read / s->frame_bytes); break;
    case MX_QUEUED: *v = (s32)(s->h->write - s->h->read); break;
    default: r = -1;
    }
    irq_restore(f);
    return r;
}

int mixer_set(struct app *a, u32 id, int what, s32 v)
{
    u32 f = irq_save();
    struct strm *s = find(a, id);
    struct instrm *n = s ? 0 : find_in(a, id);
    int r = 0;
    if (n) {
        if (what == MX_THRESHOLD) {
            n->threshold = v < 0 ? 0 : (u32)v > n->h->bytes ? n->h->bytes : (u32)v;
            n->want = 0;
        } else {
            r = -2;
        }
        irq_restore(f);
        return r;
    }
    if (!s) {
        irq_restore(f);
        return -1;
    }
    switch (what) {
    case MX_VOLUME: s->h->volume = v < 0 ? 0 : v > 256 ? 256 : (u32)v; break;
    case MX_BALANCE: s->balance = (s16)(v < -256 ? -256 : v > 256 ? 256 : v); break;
    case MX_MUTE: s->mute = (u8)(v != 0); break;
    case MX_PAUSE: s->paused = (u8)(v != 0); break;
    case MX_THRESHOLD: s->threshold = v < 0 ? 0 : (u32)v > s->h->bytes ? s->h->bytes : (u32)v; s->want = 0; break;
    default: r = -1;
    }
    irq_restore(f);
    return r;
}

u32 mixer_streams(struct mixer_stream *out, u32 max)
{
    u32 i, n = 0, f = irq_save();
    for (i = 0; i < STREAMS && n < max; i++)
        if (st[i].a) {
            out[n].id = st[i].id;
            out[n].app = app_id(st[i].a);
            out[n].rate = st[i].h->rate;
            out[n].channels = st[i].h->channels;
            out[n].volume = st[i].h->volume;
            out[n].balance = st[i].balance;
            out[n].mute = st[i].mute;
            out[n].paused = st[i].paused;
            out[n].level[0] = st[i].level[0], out[n].level[1] = st[i].level[1];
            n++;
        }
    irq_restore(f);
    return n;
}

void mixer_master_get(u32 *volume, int *mute)
{
    *volume = mx.master;
    *mute = mx.mute;
}

void mixer_master_set(u32 volume, int mute)
{
    mx.master = volume > MIX_UNITY ? MIX_UNITY : volume;
    mx.mute = (u8)(mute != 0);
}

/* The peaks of the period played last: the output's (id 0) or a stream's. 0, or -1: no such stream. */
int mixer_levels(u32 id, u16 *left, u16 *right)
{
    u32 f = irq_save();
    struct strm *s = id ? find(0, id) : 0;
    int playing = mx.on && !mx.out && mx.card && mx.card->running();
    if (id == 0xFFFFFFFFu || (id && !s && find_in(0, id))) {   /* what is recorded (every stream in has the same) */
        *left = recording ? in_level[0] : 0;
        *right = recording ? in_level[1] : 0;
        irq_restore(f);
        return 0;
    }
    if (id && !s) {
        irq_restore(f);
        return -1;
    }
    *left = !playing ? 0 : s ? s->level[0] : mx.level[0];
    *right = !playing ? 0 : s ? s->level[1] : mx.level[1];
    irq_restore(f);
    return 0;
}

/* A stream of a's has fallen below its threshold since its app was last told: 1 and its id (once each time). */
int mixer_wants(struct app *a, u32 *id)
{
    u32 i;
    for (i = 0; i < STREAMS; i++)
        if (st[i].a == a && st[i].want == 1) {
            st[i].want = 3;                     /* (told: not again until it has been above and fallen once more) */
            *id = st[i].id;
            return 1;
        }
    for (i = 0; i < IN_STREAMS; i++)
        if (in[i].a == a && in[i].want == 1) {
            in[i].want = 3;                     /* (a stream in: it has that much to read) */
            *id = in[i].id;
            return 1;
        }
    return 0;
}

const char *mixer_card(void) { return mx.on && mx.card ? mx.card->name : 0; }

/* The chip's own channels (MX_CH_), through the card's driver: only while the mixer has the card. 0, or -1. */
int mixer_chan(int chan, int set, u8 *left, u8 *right, u8 *mute)
{
    if (!mx.on || mx.out || !mx.card || !mx.card->chan)
        return -1;
    return mx.card->chan(chan, set, left, right, mute);
}

int mixer_input(int set, int *input)
{
    if (!mx.on || mx.out || !mx.card || !mx.card->input)
        return -1;
    return mx.card->input(set, input);
}
