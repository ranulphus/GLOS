/* API 2.0's AUDIO (include/glos/api2.h; docs/v1/phase-b.md step 10): streams out through GLOS's mixer
 * (kernel/drv/mixer.c), their properties, the mixer for the Mixer app and the shell's volume icon (the master,
 * every app's streams, the chip's own channels), levels for meters, the devices.
 *
 * A stream is 1.x's: a ring in pages the app and the mixer share. Here it is a handle, whose object is the
 * stream's number in the mixer. */
#include <string.h>

#include "api2int.h"
#include "dpmi.h"
#include "glos/bootinfo.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "midi.h"
#include "mixer.h"
#include "mixgain.h"
#include "mm.h"
#include "vm.h"

static int out_call(struct app *a, struct trapframe *tf, int in)
{
    struct glos_audio_out q;
    u32 size, id, lin, f;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if ((r = (in ? mixer_open_in : mixer_open)(a, q.rate, q.channels, q.bytes, &id, &lin)) != 0)
        return a2_fail(tf, (u32)r);             /* (1.x's numbers: NO_AUDIO 9, ARGUMENT 3, MEMORY 7) */
    f = irq_save();
    q.handle = handle_new(&a->v2->h, GLOS_HK_STREAM, (void *)id);
    irq_restore(f);
    q.ring = lin;
    q.id = id;
    if (!q.handle || a2_wr(tf, &q, sizeof q, size) != 0) {
        if (q.handle) {
            f = irq_save();
            handle_close(&a->v2->h, q.handle);
            irq_restore(f);
        }
        mixer_close(a, id);
        return a2_fail(tf, q.handle ? GLOS_E_ARGUMENT : GLOS_E_MEMORY);
    }
    return a2_ok(tf, 0);
}

void api2_audio_close(struct app *a, void *obj) { mixer_close(a, (u32)obj); }

/* ---- a stream's properties */

static int s_get(struct app *a, void *obj, s32 *v, int what)
{
    int r = mixer_get(a, (u32)obj, what, v);
    return r == 0 ? 0 : r == -2 ? GLOS_E_PROPERTY : GLOS_E_HANDLE;     /* (-2: a stream in has no such thing) */
}

static int s_set(struct app *a, void *obj, s32 v, int what, s32 lo, s32 hi)
{
    int r;
    if (v < lo || v > hi)
        return GLOS_E_ARGUMENT;
    r = mixer_set(a, (u32)obj, what, v);
    return r == 0 ? 0 : r == -2 ? GLOS_E_PROPERTY : GLOS_E_HANDLE;
}

#define GETTER(name, what) \
    static int name(struct app *a, void *obj, s32 *v, char *text, u32 *len) \
    { (void)text, (void)len; return s_get(a, obj, v, what); }
#define SETTER(name, what, lo, hi) \
    static int name(struct app *a, void *obj, s32 v, const char *text, u32 len) \
    { (void)text, (void)len; return s_set(a, obj, v, what, lo, hi); }
GETTER(vol_get, MX_VOLUME) SETTER(vol_set, MX_VOLUME, 0, 256)
GETTER(bal_get, MX_BALANCE) SETTER(bal_set, MX_BALANCE, -256, 256)
GETTER(mute_get, MX_MUTE) SETTER(mute_set, MX_MUTE, 0, 1)
GETTER(pause_get, MX_PAUSE) SETTER(pause_set, MX_PAUSE, 0, 1)
GETTER(pos_get, MX_POSITION)
GETTER(queued_get, MX_QUEUED)
GETTER(notify_get, MX_THRESHOLD) SETTER(notify_set, MX_THRESHOLD, 0, 65536)

static int id_get(struct app *a, void *obj, s32 *v, char *text, u32 *len)
{
    (void)a, (void)text, (void)len;
    *v = (s32)(u32)obj;
    return 0;
}

static int no_set(struct app *a, void *obj, s32 v, const char *text, u32 len)
{
    (void)a, (void)obj, (void)v, (void)text, (void)len;
    return GLOS_E_PROPERTY;
}

const struct propdef api2_audio_props[] = {
    { GLOS_PROP_STREAM_VOLUME, P_NUM, vol_get, vol_set },
    { GLOS_PROP_STREAM_BALANCE, P_NUM, bal_get, bal_set },
    { GLOS_PROP_STREAM_MUTE, P_NUM, mute_get, mute_set },
    { GLOS_PROP_STREAM_PAUSED, P_NUM, pause_get, pause_set },
    { GLOS_PROP_STREAM_POSITION, P_NUM | P_RO, pos_get, no_set },
    { GLOS_PROP_STREAM_QUEUED, P_NUM | P_RO, queued_get, no_set },
    { GLOS_PROP_STREAM_NOTIFY, P_NUM, notify_get, notify_set },
    { GLOS_PROP_STREAM_ID, P_NUM | P_RO, id_get, no_set },
};
const u32 api2_audio_nprops = ARRAY_SIZE(api2_audio_props);

/* ---- MIDI: a port's handle has the port's number and its direction (MIDI_OBJ) */

#define MIDI_OBJ(port, in) ((void *)((port) + 1 + ((in) ? 0x100u : 0u)))
#define MIDI_PORT(obj)     (((u32)(obj) & 0xFFu) - 1)
#define MIDI_IN(obj)       (((u32)(obj) & 0x100u) != 0)

void api2_midi_close(struct app *a, void *obj) { midi_close(a, MIDI_PORT(obj), MIDI_IN(obj)); }

static u32 midi_error(int r)
{
    return r == MIDI_E_NONE ? GLOS_E_NOT_FOUND : r == MIDI_E_BUSY ? GLOS_E_REFUSED : r == MIDI_E_AWAY ? GLOS_E_NO_AUDIO
         : r == MIDI_E_ROOM ? GLOS_E_ROOM : r == MIDI_E_MESSAGE ? GLOS_E_ARGUMENT : GLOS_E_HANDLE;
}

static int midi_call(struct app *a, struct trapframe *tf, int in)
{
    struct glos_midi q;
    struct midi_msg m[32];
    u8 raw[64];
    u32 size, f, port, done, taken;
    void *obj;
    int r = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.what == GLOS_MIDI_OPEN) {
        if ((r = midi_open(a, q.port, in)) != 0)
            return a2_fail(tf, midi_error(r));
        f = irq_save();
        q.handle = handle_new(&a->v2->h, GLOS_HK_MIDI, MIDI_OBJ(q.port, in));
        irq_restore(f);
        q.queued = 0;
        q.now = a2_now_ms();
        if (!q.handle || a2_wr(tf, &q, sizeof q, size) != 0) {
            if (q.handle) {
                f = irq_save();
                handle_close(&a->v2->h, q.handle);
                irq_restore(f);
            }
            midi_close(a, q.port, in);
            return a2_fail(tf, q.handle ? GLOS_E_ARGUMENT : GLOS_E_MEMORY);
        }
        return a2_ok(tf, 0);
    }
    f = irq_save();
    obj = handle_obj(&a->v2->h, q.handle, GLOS_HK_MIDI);
    irq_restore(f);
    if (!obj)
        return a2_fail(tf, GLOS_E_HANDLE);
    if (in || MIDI_IN(obj))                     /* (a port in has no calls but OPEN and CLOSE) */
        return a2_fail(tf, GLOS_E_ARGUMENT);
    port = MIDI_PORT(obj);
    switch (q.what) {
    case GLOS_MIDI_SEND:
        for (done = 0; done < q.count; ) {      /* (a stackful at a time) */
            u32 n = q.count - done < ARRAY_SIZE(m) ? q.count - done : ARRAY_SIZE(m);
            if (user_rd((u16)tf->ds, q.buffer + done * sizeof m[0], m, n * sizeof m[0]) != 0)
                return a2_fail(tf, GLOS_E_ARGUMENT);
            r = midi_send(a, port, m, n, &taken);
            done += taken;
            if (r != 0 || taken < n)            /* (one that is no channel message; or the queue is full) */
                break;
        }
        q.count = done;
        break;
    case GLOS_MIDI_FLUSH:
        r = midi_flush(a, port);
        break;
    case GLOS_MIDI_RAW:
        if (port != MIDI_MPU)
            return a2_fail(tf, GLOS_E_REFUSED);
        if (q.count > sizeof raw || user_rd((u16)tf->ds, q.buffer, raw, q.count) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        r = midi_raw(a, port, raw, q.count);
        break;
    default:
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    q.queued = midi_queued(port);
    q.now = a2_now_ms();
    if (a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return r ? a2_fail(tf, midi_error(r)) : a2_ok(tf, 0);
}

/* Each tick, for an app: a stream of its that fell below its NOTIFY is told of, once; what its MIDI port in
   received, a message an event. */
void api2_audio_tick(struct app *a)
{
    struct glos_ev ev;
    struct glos_ev_midi me;
    struct midi_msg mm;
    u32 id, i = 0, h;
    void *obj;
    while (midi_in_next(a, &mm)) {
        memset(&me, 0, sizeof me);
        while (handle_next(&a->v2->h, &i, &h, &obj) == 0)
            if (GLOS_HANDLE_KIND(h) == GLOS_HK_MIDI && MIDI_IN(obj))
                me.ev.handle = h;
        i = 0;
        me.ev.size = sizeof me;
        me.ev.kind = GLOS_EV_MIDI;
        me.status = mm.status, me.data1 = mm.d1, me.data2 = mm.d2;
        a2_post_at(a, &me.ev, mm.time);
    }
    if (!mixer_wants(a, &id))
        return;
    memset(&ev, 0, sizeof ev);
    while (handle_next(&a->v2->h, &i, &h, &obj) == 0)
        if (GLOS_HANDLE_KIND(h) == GLOS_HK_STREAM && (u32)obj == id)
            ev.handle = h;
    if (!ev.handle)
        return;
    ev.size = sizeof ev;
    ev.kind = GLOS_EV_AUDIO;
    a2_post(a, &ev, 1);
}

/* ---- the mixer */

static void master_from_cfg(u32 *volume, int *mute)     /* (as GLOS.CFG has it: the mixer's own when there is none) */
{
    u32 g;
    mixer_master_get(&g, mute);
    *volume = mix_to_percent(g);
}

static int list(struct trapframe *tf, struct glos_audio_mixer *q, u32 size, const void *buf, u32 n, u32 each)
{
    u32 len = n * each, give = len <= q->room ? len : q->room - q->room % each;
    q->length = len;
    q->count = give / each;
    if ((give && user_wr((u16)tf->ds, q->buffer, buf, give) != 0) || a2_wr(tf, q, sizeof *q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return give < len ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

static int mixer_call(struct app *a, struct trapframe *tf)
{
    static const char *const names[MX_CHANNELS] = { "Master", "Wave", "FM", "CD", "Line", "Mic" };
    struct glos_audio_mixer q;
    u32 size, i, n;
    int mute, in;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!mixer_found())
        return a2_fail(tf, GLOS_E_NO_AUDIO);
    if ((q.what == GLOS_MIXER_MASTER_SET || q.what == GLOS_MIXER_STREAM_SET || q.what == GLOS_MIXER_CHANNEL_SET
         || q.what == GLOS_MIXER_INPUT_SET) && !app_own(a))
        return a2_fail(tf, GLOS_E_REFUSED);     /* (the Mixer's and the shell's to do) */
    switch (q.what) {
    case GLOS_MIXER_MASTER:
        master_from_cfg(&n, &mute);
        q.volume = (int)n;
        q.mute = mute;
        break;
    case GLOS_MIXER_MASTER_SET: {
        char v[8], why[64];
        master_from_cfg(&n, &mute);
        if (q.volume > 100 || q.volume < -1 || q.mute > 1 || q.mute < -1)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (q.volume >= 0)
            n = (u32)q.volume;
        if (q.mute >= 0)
            mute = q.mute;
        ksnprintf(v, sizeof v, "%u", n);        /* through GLOS.CFG, so that it is kept and its watchers are told; */
        if (kcfg_set("sound", "volume", v, why, sizeof why) < 0     /* where there is no file (GLOS /RUN), as it is */
            || kcfg_set("sound", "mute", mute ? "on" : "off", why, sizeof why) < 0)
            mixer_master_set(mix_from_percent(n), mute);
        q.volume = (int)n;
        q.mute = mute;
        break;
    }
    case GLOS_MIXER_STREAMS: {
        struct mixer_stream ms[8];
        struct glos_audio_stream out[8];
        n = mixer_streams(ms, 8);
        for (i = 0; i < n; i++) {
            memset(&out[i], 0, sizeof out[i]);
            out[i].size = sizeof out[i];
            out[i].id = ms[i].id, out[i].app = ms[i].app;
            out[i].rate = ms[i].rate, out[i].channels = ms[i].channels;
            out[i].volume = ms[i].volume, out[i].balance = ms[i].balance;
            out[i].mute = ms[i].mute, out[i].paused = ms[i].paused;
            out[i].left = ms[i].level[0], out[i].right = ms[i].level[1];
        }
        return list(tf, &q, size, out, n, sizeof out[0]);
    }
    case GLOS_MIXER_STREAM_SET:
        if ((q.volume != -1 && (q.volume < 0 || q.volume > 256)) || (q.balance != -1000 && (q.balance < -256 || q.balance > 256))
            || q.mute < -1 || q.mute > 1)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (mixer_get(0, q.id, MX_VOLUME, &in) != 0)
            return a2_fail(tf, GLOS_E_NOT_FOUND);
        if (q.volume != -1)
            mixer_set(0, q.id, MX_VOLUME, q.volume);
        if (q.balance != -1000)
            mixer_set(0, q.id, MX_BALANCE, q.balance);
        if (q.mute != -1)
            mixer_set(0, q.id, MX_MUTE, q.mute);
        break;
    case GLOS_MIXER_CHANNELS: {
        struct glos_audio_channel out[MX_CHANNELS];
        for (i = n = 0; i < MX_CHANNELS; i++) {
            u8 l = 0, r = 0, m = 0;
            if (mixer_chan((int)i, 0, &l, &r, &m) != 0)
                continue;                       /* (the chip has no such channel, or a session has the card) */
            memset(&out[n], 0, sizeof out[n]);
            out[n].size = sizeof out[n];
            out[n].id = i;
            out[n].left = l, out[n].right = r, out[n].mute = m;
            strcpy(out[n].name, names[i]);
            n++;
        }
        return list(tf, &q, size, out, n, sizeof out[0]);
    }
    case GLOS_MIXER_CHANNEL_SET: {
        u8 l = 0, r = 0, m = 0;
        if (q.id >= MX_CHANNELS || q.volume < -1 || q.volume > 255 || (q.balance != -1000 && (q.balance < 0 || q.balance > 255))
            || q.mute < -1 || q.mute > 1)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (mixer_chan((int)q.id, 0, &l, &r, &m) != 0)
            return a2_fail(tf, GLOS_E_NOT_FOUND);
        if (q.volume >= 0)
            l = (u8)q.volume;
        if (q.balance != -1000)
            r = (u8)q.balance;
        if (q.mute >= 0)
            m = (u8)q.mute;
        mixer_chan((int)q.id, 1, &l, &r, &m);
        q.volume = l, q.balance = r, q.mute = m;       /* (as the chip took it: it has fewer steps) */
        break;
    }
    case GLOS_MIXER_INPUT:
    case GLOS_MIXER_INPUT_SET:
        in = (int)q.id;
        if (mixer_input(q.what == GLOS_MIXER_INPUT_SET, &in) != 0)
            return a2_fail(tf, q.what == GLOS_MIXER_INPUT_SET && q.id > GLOS_INPUT_MIX ? GLOS_E_ARGUMENT : GLOS_E_NOT_FOUND);
        q.id = (u32)in;
        break;
    default:
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int levels(struct trapframe *tf)
{
    struct glos_audio_levels q;
    u32 size;
    u16 l, r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!mixer_found())
        return a2_fail(tf, GLOS_E_NO_AUDIO);
    if (mixer_levels(q.id, &l, &r) != 0)
        return a2_fail(tf, GLOS_E_NOT_FOUND);
    q.left = l, q.right = r;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int devices(struct trapframe *tf)
{
    struct glos_sys_stat q;
    struct glos_audio_device d[2 + 2 * MIDI_PORTS];
    const char *name;
    u32 size, n = 0, x, give, port, outs, ins;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    memset(d, 0, sizeof d);
    if (mixer_card()) {                         /* (none: an empty list) */
        d[0].size = sizeof d[0];
        d[0].kind = GLOS_AUDIO_DEV_OUT;
        mixer_counts(&d[0].rate, &d[0].streams, &x);
        strncpy(d[0].name, mixer_card(), sizeof d[0].name - 1);
        n = 1;
        if (mixer_records()) {
            d[1] = d[0];
            d[1].kind = GLOS_AUDIO_DEV_IN;
            d[1].streams = mixer_streams_in();
            n = 2;
        }
    }
    for (port = 0; port < MIDI_PORTS; port++) { /* the MIDI ports: one out each; an MPU-401's in too */
        if (midi_port(port, &name, &outs, &ins) != 0)
            continue;
        d[n].size = sizeof d[0];
        d[n].kind = GLOS_AUDIO_DEV_MIDI_OUT;
        d[n].rate = port;
        d[n].streams = outs;
        strncpy(d[n].name, name, sizeof d[n].name - 1);
        n++;
        if (ins != 0xFFFFFFFFu) {
            d[n] = d[n - 1];
            d[n].kind = GLOS_AUDIO_DEV_MIDI_IN;
            d[n].streams = ins;
            n++;
        }
    }
    q.length = n * sizeof d[0];
    give = q.room >= q.length ? q.length : q.room - q.room % sizeof d[0];
    q.count = give / sizeof d[0];
    if ((give && user_wr((u16)tf->ds, q.buffer, d, give) != 0) || a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return give < q.length ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

int api2_audio(struct app *a, struct trapframe *tf, u32 n)
{
    switch (GLOS_FN(GLOS_AREA_AUDIO, n)) {
    case GLOS_AUDIO_OUT:
        return out_call(a, tf, 0);
    case GLOS_AUDIO_IN:
        return out_call(a, tf, 1);
    case GLOS_AUDIO_MIXER:
        return mixer_call(a, tf);
    case GLOS_AUDIO_LEVELS:
        return levels(tf);
    case GLOS_AUDIO_MIDI_OUT:
        return midi_call(a, tf, 0);
    case GLOS_AUDIO_MIDI_IN:
        return midi_call(a, tf, 1);
    case GLOS_AUDIO_DEVICES:
        return devices(tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
