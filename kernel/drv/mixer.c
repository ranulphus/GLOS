/* GLOS's mixer (PRD §10.5; docs/milestones-m8.md step E): the apps' streams
 * of sound into the Sound Blaster 16 (sb16.c).
 *
 * A stream is a ring of signed 16-bit samples in pages shared with its app
 * (struct glos_stream, include/glos/api.h): the app writes, the mixer reads,
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

#define GLOS_KERNEL
#include "glos/api.h"
#include "glos/bootinfo.h"
#include "app.h"
#include "arch.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "sched.h"
#include "timer.h"

#define STREAMS   8
#define PERIOD    256                           /* frames */
#define HEADER    64                            /* the ring's offset in the stream's pages */

static struct strm {
    struct app *a;                              /* 0: free */
    u32 id;
    struct glos_stream *h;                      /* the kernel's view (its pages: mm_win_alloc) */
    u32 pages;
    u8 *ring;
    u32 step, frac;                             /* 16.16: stream frames a mixer frame */
    u8 begun, frame_bytes;
    u32 lin;                                    /* where its app has it */
    u32 short_run, bursts;                      /* underruns in a row now; bursts logged */
} st[STREAMS];

static struct {
    u8 on;                                      /* the card found and the mixer wanted */
    u8 out;                                     /* a session has the card */
    u32 rate, next_id, periods;
    s32 acc[PERIOD * 2];
} mx;

int mixer_init(const struct bootinfo *bi)
{
    mx.rate = bi->sound_rate ? bi->sound_rate : 22050;
    mx.next_id = 1;
    mx.out = 1;                                 /* (until the desktop is up) */
    if ((bi->sound_flags & BI_SOUND_OFF) || sb16_init(bi) != 0)
        return -1;
    mx.on = 1;
    return 0;
}

int mixer_on(void) { return mx.on && !mx.out; }
int mixer_found(void) { return mx.on; }

/* One stream's samples for the period, added into mx.acc. tf: what the
   card's interrupt broke into; an underrun while it was the stream's own app
   running its code says where in it (the first few of each burst: where a
   program keeps its sound waiting). */
static void take(struct strm *s, u32 frames, const struct trapframe *tf)
{
    struct glos_stream *h = s->h;
    u32 have = (h->write - h->read) / s->frame_bytes, need, i, vol = h->volume, mask = h->bytes - 1;
    u32 pos = h->read, frac = s->frac;
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
        mx.acc[i * 2] += (l * (s32)vol) >> 8;
        mx.acc[i * 2 + 1] += (r * (s32)vol) >> 8;
        frac += s->step;
    }
    i = frac >> 16;                             /* frames used up */
    if (i > have)
        i = have;
    h->read = pos + i * s->frame_bytes;
    s->frac = frac & 0xFFFF;
}

/* The card's interrupt: a half to fill (16-bit stereo). */
static void fill(s16 *out, u32 frames, const struct trapframe *tf)
{
    u32 i;
    memset(mx.acc, 0, frames * 2 * sizeof mx.acc[0]);
    for (i = 0; i < STREAMS && !apps_paused(); i++)    /* (paused apps' streams wait with them) */
        if (st[i].a)
            take(&st[i], frames, tf);
    for (i = 0; i < frames * 2; i++)
        out[i] = (s16)(mx.acc[i] > 32767 ? 32767 : mx.acc[i] < -32768 ? -32768 : mx.acc[i]);
    mx.periods++;
}

/* The mixer is the desktop's: it plays while the desktop has the screen, and
   a session (or no desktop) has the card. */
void mixer_desktop(int on)
{
    if (!mx.on)
        return;
    if (on) {
        mx.out = 0;
        if (!sb16_running() && sb16_start(mx.rate, PERIOD, fill) != 0)
            mx.on = 0;
    } else {
        sb16_stop();
        mx.out = 1;
    }
}

/* ---- streams (GLOS_FN_AUDIO, on the app's thread) */

int mixer_open(struct app *a, u32 rate, u32 channels, u32 bytes, u32 *id, u32 *lin)
{
    struct strm *s = 0;
    struct glos_stream *h;
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
    s->id = mx.next_id++;
    s->lin = *lin;
    s->a = a;
    irq_restore(f);
    *id = s->id;
    kprintf("GLOS-AUDIO open app=%u stream=%u rate=%u channels=%u ring=%u\n", app_id(a), s->id, rate, channels, bytes);
    return 0;
}

static void drop(struct strm *s)
{
    u32 f = irq_save();
    struct glos_stream *h = s->h;
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
    return GLOS_E_ARGUMENT;
}

int mixer_volume(struct app *a, u32 id, u32 volume)
{
    u32 i;
    for (i = 0; i < STREAMS; i++)
        if (st[i].a == a && st[i].id == id) {
            st[i].h->volume = volume > 256 ? 256 : volume;
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
}

u32 mixer_stat(char *buf, u32 max)
{
    u32 n, i;
    if (!mx.on)
        return (u32)ksnprintf(buf, max, "audio off\n");
    n = (u32)ksnprintf(buf, max, "audio %s rate=%u period=%u periods=%u irqs=%u\n", mx.out ? "idle" : "on", mx.rate,
                       PERIOD, mx.periods, sb16_irqs());
    for (i = 0; i < STREAMS && n < max; i++)
        if (st[i].a)
            n += (u32)ksnprintf(buf + n, max - n, "stream %u app=%u rate=%u channels=%u periods=%u underruns=%u\n",
                                st[i].id, app_id(st[i].a), st[i].h->rate, st[i].h->channels, st[i].h->periods,
                                st[i].h->underruns);
    return n;
}
