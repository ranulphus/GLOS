/* RECORDER: the Sound Recorder (GLOS v1, phase F; docs/v1/apps/sound-recorder.md). A take is recorded from the
 * sound card's input (the one the Mixer has chosen), shown as a waveform, played, edited and saved as a WAV file.
 *
 *   Record (Ctrl+R)   from AUDIO's stream in, onto the take's end, at the take's rate and channels (a new take:
 *                     Format's); the level beside the waveform, "Clipped" in the status bar once a sample was at
 *                     the top
 *   Play (F5)         the selection, or from the cursor to the end; Stop (F6, Esc)
 *   the waveform      a press sets the cursor, a drag selects; Left and Right move the cursor (Shift: select),
 *                     Home and End; Ctrl+A all of it
 *   Edit              Undo (one step), Cut, Copy, Paste, Delete, Trim to the selection
 *   Effects           on the selection, or on all of it when nothing is selected: louder, softer, as loud as
 *                     fits, fades, backwards, silence
 *   Format            one channel or two, the rate (the take is made over), and how Save writes it: 16 bits,
 *                     8 bits, or ADPCM (a quarter of the size)
 *
 * The take is in memory (apps/shared/wave.c): a recording stops when there is no more of it for it.
 *
 * RECORDER -demo: no card is asked for; Record puts a second of the test's own sound on the take at once, and Play
 * is over at once: the GUI scripts' pictures are the same on every PC and on the host.
 *
 * "RECORDER ..." lines on stdout (the suite's). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "../shared/wave.h"

enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_EXIT, C_UNDO, C_CUT, C_COPY, C_PASTE, C_DELETE, C_TRIM, C_ALL,
       C_LOUDER, C_SOFTER, C_NORMALISE, C_FADEIN, C_FADEOUT, C_REVERSE, C_SILENCE, C_MONO, C_STEREO,
       C_RATE, C_RATE_END = C_RATE + 4, C_KIND, C_KIND_END = C_KIND + 3, C_RECORD, C_PLAY, C_STOP,
       C_ZOOMIN, C_ZOOMOUT, C_ZOOMALL };

static const unsigned rates[4] = { 8000, 11025, 22050, 44100 };
static const char *const kinds[3] = { GUI_N("16 bits"), GUI_N("8 bits"), GUI_N("ADPCM (small)") };
static const char *const kind_words[3] = { "pcm16", "pcm8", "adpcm" };

static struct gui_window *win;
static struct gui_widget *canvas, *meter, *status;
static struct wave take, undo, clip;
static int have_undo, have_clip, changed, demo;
static unsigned long sel_a, sel_b;              /* the selection's two ends: where it began, and the cursor */
static unsigned long view_from, view_n;         /* the frames the waveform shows */
static char path[260];
static unsigned new_rate = 22050, new_ch = 2;   /* a new take's */
static int save_kind;
static unsigned long max_bytes = 24ul << 20;    /* a take at most (-max N: kilobytes, for the tests) */

static glos_handle in_h;                        /* recording: the stream in */
static struct glos_audio_ring *in_ring;
static int clipped;
static unsigned long rec_from;
static glos_handle out_h;                       /* playing: the stream out */
static struct glos_audio_ring *out_ring;
static unsigned long play_from, play_to, play_at;       /* the frames to play; the next to be written */
static unsigned long play_pos;                  /* the frame being heard (the line on the waveform) */

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("RECORDER ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- what is shown */

static void sel(unsigned long *from, unsigned long *to)         /* the selection, in order */
{
    *from = sel_a < sel_b ? sel_a : sel_b;
    *to = sel_a < sel_b ? sel_b : sel_a;
}

static void span(unsigned long *from, unsigned long *to)        /* what an effect works on: the selection, or all */
{
    sel(from, to);
    if (*from == *to)
        *from = 0, *to = take.frames;
}

static void time_text(unsigned long frames, char *out, size_t room)
{
    unsigned long cs = take.rate ? (unsigned long)((unsigned long long)frames * 100 / take.rate) : 0;
    snprintf(out, room, "%lu:%02lu.%02lu", cs / 6000, cs / 100 % 60, cs % 100);
}

static void show(void)
{
    char t[200], a[24], b[24], c[24], title[300];
    unsigned long from, to;
    const char *name = path[0] ? gui_path_name(path) : GUI_T("New take");
    sel(&from, &to);
    time_text(from, a, sizeof a);
    time_text(to, b, sizeof b);
    time_text(take.frames, c, sizeof c);
    if (in_h)
        snprintf(t, sizeof t, GUI_T("Recording: %s%s"), c, clipped ? GUI_T("   Clipped") : "");
    else if (!take.frames)
        snprintf(t, sizeof t, "%s", GUI_T("Nothing recorded yet. Record: Ctrl+R."));
    else if (from == to)
        snprintf(t, sizeof t, GUI_T("%s of %s   %u Hz, %s   %lu KB%s"), a, c, take.rate,
                 take.channels == 2 ? GUI_T("stereo") : GUI_T("mono"), (take.frames * take.channels * 2 + 1023) / 1024,
                 clipped ? GUI_T("   Clipped") : "");
    else
        snprintf(t, sizeof t, GUI_T("%s to %s of %s   %u Hz, %s   %lu KB%s"), a, b, c, take.rate,
                 take.channels == 2 ? GUI_T("stereo") : GUI_T("mono"), (take.frames * take.channels * 2 + 1023) / 1024,
                 clipped ? GUI_T("   Clipped") : "");
    gui_set_text(status, t);
    snprintf(title, sizeof title, "%s%s - %s", name, changed ? "*" : "", GUI_T("Sound Recorder"));
    gui_title(win, title);
    gui_damage(canvas);
}

static void view_all(void)
{
    view_from = 0, view_n = take.frames;
}

static void view_keep(unsigned long at)         /* the view moved so that a frame is in it */
{
    if (view_n > take.frames)
        view_n = take.frames;
    if (view_from + view_n > take.frames)
        view_from = take.frames - view_n;
    if (at < view_from)
        view_from = at;
    else if (view_n && at >= view_from + view_n)
        view_from = at - view_n + 1 > take.frames - view_n ? take.frames - view_n : at - view_n + 1;
}

static void canvas_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct gfx_rect r = w->r;
    unsigned long from, to;
    unsigned c;
    int x, lane = r.h / (int)(take.channels ? take.channels : 1);
    gfx_fill(s, r.x, r.y, r.w, r.h, GUI_FIELD);
    gfx_line(s, r.x, r.y, r.x + r.w - 1, r.y, GUI_LINE);
    gfx_line(s, r.x, r.y + r.h - 1, r.x + r.w - 1, r.y + r.h - 1, GUI_LINE);
    if (!take.frames || !view_n || r.w < 2)
        return;
    sel(&from, &to);
    for (x = 0; x < r.w; x++) {
        unsigned long f0 = view_from + (unsigned long)((unsigned long long)x * view_n / (unsigned)r.w);
        unsigned long f1 = view_from + (unsigned long)((unsigned long long)(x + 1) * view_n / (unsigned)r.w);
        int chosen = from != to && f0 < to && f1 > from;
        if (f1 <= f0)
            f1 = f0 + 1;
        if (chosen)
            gfx_fill(s, r.x + x, r.y + 1, 1, r.h - 2, GUI_CHOSEN);
        for (c = 0; c < take.channels; c++) {
            int mid = r.y + lane * (int)c + lane / 2, half = lane / 2 - 2, y0, y1;
            short lo, hi;
            wave_range(&take, f0, f1, c, &lo, &hi);
            y0 = mid - hi * half / 32768, y1 = mid - lo * half / 32768;
            gfx_fill(s, r.x + x, y0, 1, y1 - y0 + 1, chosen ? GUI_CHOSEN_TEXT : GUI_FIELD_TEXT);
        }
    }
    for (c = 1; c < take.channels; c++)
        gfx_line(s, r.x, r.y + lane * (int)c, r.x + r.w - 1, r.y + lane * (int)c, GUI_SHADE);
    {                                           /* the cursor, and where it is playing */
        unsigned long at = out_h ? play_pos : sel_b;
        if (at >= view_from && at <= view_from + view_n) {
            x = (int)((unsigned long long)(at - view_from) * (unsigned)(r.w - 1) / view_n);
            gfx_fill(s, r.x + x, r.y + 1, 1, r.h - 2, GUI_MARK);
        }
    }
}

static unsigned long frame_at(struct gui_widget *w, int x)
{
    long px = x - w->r.x;
    unsigned long f;
    if (px < 0)
        px = 0;
    if (px > w->r.w)
        px = w->r.w;
    f = view_from + (unsigned long)((unsigned long long)px * view_n / (unsigned)(w->r.w > 0 ? w->r.w : 1));
    return f > take.frames ? take.frames : f;
}

static int canvas_event(struct gui_widget *w, struct gui_ev *e)
{
    static int dragging;
    if (e->type == GUI_EV_PRESS && e->button == 1) {
        sel_b = frame_at(w, e->x);
        if (!(e->mods & GLOS_MOD_SHIFT))
            sel_a = sel_b;
        dragging = 1;
        show();
        return 1;
    }
    if (e->type == GUI_EV_MOVE && dragging && (e->buttons & 1)) {
        sel_b = frame_at(w, e->x);
        show();
        return 1;
    }
    if (e->type == GUI_EV_RELEASE) {
        dragging = 0;
        return 1;
    }
    return 0;
}

/* ---- recording and playing */

static short demo_sample(unsigned long i, unsigned c)   /* the test's own sound: a triangle that swells */
{
    long period = c == 0 ? 200 : 316, k = (long)(i % (unsigned long)period);
    long tri = k < period / 2 ? k : period - k;
    long swell = 2000 + (long)((i / 64) % 97) * 250;
    return (short)(tri * 2 * swell / (period / 2) - swell);
}

static void halt(void)                          /* the streams closed (and no more: the window may be gone) */
{
    if (in_h) {
        glos_close(in_h);
        in_h = 0, in_ring = NULL;
        sel_a = sel_b = take.frames;
        view_all();
        say("recorded %lu at %u Hz %u channels peak %u", take.frames - rec_from, take.rate, take.channels,
            wave_peak(&take, rec_from, take.frames));
    }
    if (out_h) {
        glos_close(out_h);
        out_h = 0, out_ring = NULL;
        say("played %lu to %lu", play_from, play_to);
    }
}

static void stop(void)
{
    int was = in_h != 0;
    halt();
    if (was)
        gui_meter_set(meter, 0, 0);
    show();
}

static void keep_undo(void);

static void record(void)
{
    unsigned id = 0;
    stop();
    if (!take.frames) {                         /* a new take: of the format chosen */
        wave_free(&take);
        wave_init(&take, new_rate, new_ch);
    }
    keep_undo();
    rec_from = take.frames;
    if (demo) {                                 /* a second of the test's sound, at once */
        unsigned long i;
        short f[2];
        for (i = 0; i < take.rate; i++) {
            f[0] = demo_sample(rec_from + i, 0), f[1] = demo_sample(rec_from + i, 1);
            if (wave_append(&take, f, 1) != WAVE_OK)
                break;
        }
        changed = 1;
        sel_a = sel_b = take.frames;
        view_all();
        say("recorded %lu at %u Hz %u channels peak %u", take.frames - rec_from, take.rate, take.channels,
            wave_peak(&take, rec_from, take.frames));
        show();
        return;
    }
    in_h = glos_audio_in(take.rate, take.channels, 65536, &in_ring, &id);
    if (!in_h) {
        int e = glos_error();
        say("refused record %d", e);
        gui_message(win, GUI_T("Sound Recorder"), e == GLOS_E_NO_AUDIO
                    ? GUI_T("There is no sound card to record from, or this one can't record while it plays.")
                    : GUI_T("The recording couldn't be started."), GUI_MB_OK);
        return;
    }
    changed = 1;
    show();
}

static void play(void)
{
    unsigned id = 0;
    stop();
    if (!take.frames)
        return;
    sel(&play_from, &play_to);
    if (play_from == play_to)                   /* (nothing selected: from the cursor to the end; at the end: all) */
        play_to = take.frames, play_from = play_from >= take.frames ? 0 : play_from;
    play_at = play_pos = play_from;
    if (demo) {
        say("played %lu to %lu", play_from, play_to);
        return;
    }
    out_h = glos_audio_out(take.rate, take.channels, 32768, &out_ring, &id);
    if (!out_h) {
        say("refused play %d", glos_error());
        gui_message(win, GUI_T("Sound Recorder"), GUI_T("There is no sound card to play through."), GUI_MB_OK);
        return;
    }
    show();
}

static void tick(void *arg)
{
    static short buf[4096];
    static unsigned turn;
    (void)arg;
    turn++;
    if (in_h) {
        unsigned n, i, fb = take.channels * 2, l = 0, r = 0;
        int full = 0;
        while (!full && (n = glos_audio_read(in_ring, buf, sizeof buf - sizeof buf % fb)) >= fb) {
            for (i = 0; i < n / 2; i++)
                if (buf[i] >= 32767 || buf[i] <= -32767)
                    clipped = 1;
            if ((take.frames + n / fb) * fb > max_bytes || wave_append(&take, buf, n / fb) != WAVE_OK)
                full = 1;
        }
        glos_audio_levels(GLOS_LEVELS_INPUT, &l, &r);
        gui_meter_set(meter, (int)(l * 1000 / 32767), (int)(r * 1000 / 32767));
        if (full) {
            stop();
            say("refused more");
            gui_message(win, GUI_T("Sound Recorder"), GUI_T("The recording was stopped: there is no more memory for it."),
                        GUI_MB_OK);
            return;
        }
        if (turn % 5 == 0) {                    /* (the last ten seconds, four times a second: all of a long take
                                                   would be looked through for each picture) */
            view_n = take.frames < take.rate * 10ul ? take.frames : take.rate * 10ul;
            view_from = take.frames - view_n;
            sel_a = sel_b = take.frames;
            show();
        }
    }
    if (out_h) {
        unsigned fb = take.channels * 2, room = glos_audio_room(out_ring) / fb;
        unsigned long left = play_to - play_at;
        int queued = 0;
        if (room > left)
            room = (unsigned)left;
        if (room) {
            glos_audio_write(out_ring, take.pcm + play_at * take.channels, room * fb);
            play_at += room;
        }
        glos_get(out_h, GLOS_PROP_STREAM_QUEUED, &queued);
        play_pos = play_at - (unsigned long)(queued > 0 ? queued : 0) / fb;
        if (play_pos < play_from)
            play_pos = play_from;
        if (play_at >= play_to && queued <= (int)fb) {
            stop();
            return;
        }
        gui_damage(canvas);
    }
}

/* ---- edits */

static void keep_undo(void)
{
    if (have_undo)
        wave_free(&undo);
    have_undo = wave_copy(&undo, &take, 0, take.frames) == WAVE_OK;    /* (no memory for it: no undo of this one) */
}

static void edited(const char *what, unsigned long from, unsigned long to)
{
    changed = 1;
    if (sel_a > take.frames)
        sel_a = take.frames;
    if (sel_b > take.frames)
        sel_b = take.frames;
    view_all();
    say("edit %s %lu %lu -> %lu frames peak %u", what, from, to, take.frames, wave_peak(&take, 0, take.frames));
    show();
}

static void no_memory(void)
{
    say("refused memory");
    gui_message(win, GUI_T("Sound Recorder"), GUI_T("There is not enough memory for that."), GUI_MB_OK);
}

static void edit(int id)
{
    unsigned long from, to, a, b;
    int r = WAVE_OK;
    stop();
    sel(&from, &to);
    span(&a, &b);
    switch (id) {
    case C_UNDO:
        if (have_undo) {
            struct wave t = take;
            take = undo, undo = t;              /* (Undo again: done again) */
            sel_a = sel_b = 0;
            changed = 1;
            view_all();
            say("undo -> %lu frames peak %u", take.frames, wave_peak(&take, 0, take.frames));
            show();
        }
        return;
    case C_ALL:
        sel_a = 0, sel_b = take.frames;
        show();
        return;
    case C_COPY:
    case C_CUT:
    case C_DELETE:
        if (from == to)
            return;
        if (id != C_DELETE) {
            struct wave c;
            if (wave_copy(&c, &take, from, to) != WAVE_OK) {
                no_memory();
                return;
            }
            if (have_clip)
                wave_free(&clip);
            clip = c, have_clip = 1;
            if (id == C_COPY) {
                say("copied %lu", to - from);
                return;
            }
        }
        keep_undo();
        wave_cut(&take, from, to, NULL);
        sel_a = sel_b = from;
        edited(id == C_CUT ? "cut" : "delete", from, to);
        return;
    case C_PASTE:
        if (!have_clip)
            return;
        if (!take.frames) {                     /* onto nothing: a take of the piece's kind */
            wave_free(&take);
            wave_init(&take, clip.rate, clip.channels);
        }
        if (clip.rate != take.rate || clip.channels != take.channels) {
            say("refused paste");
            gui_message(win, GUI_T("Sound Recorder"),
                        GUI_T("What was copied has another rate or other channels than this take."), GUI_MB_OK);
            return;
        }
        keep_undo();
        if (from != to)
            wave_cut(&take, from, to, NULL);
        if ((r = wave_insert(&take, from, &clip)) != WAVE_OK) {
            no_memory();
            return;
        }
        sel_a = from, sel_b = from + clip.frames;
        edited("paste", from, from + clip.frames);
        return;
    case C_TRIM:
        if (from == to)
            return;
        keep_undo();
        wave_trim(&take, from, to);
        sel_a = 0, sel_b = take.frames;
        edited("trim", from, to);
        return;
    }
    if (!take.frames)
        return;
    keep_undo();
    switch (id) {
    case C_LOUDER: wave_gain(&take, a, b, 1250); edited("louder", a, b); break;
    case C_SOFTER: wave_gain(&take, a, b, 800); edited("softer", a, b); break;
    case C_NORMALISE: wave_normalise(&take, a, b); edited("normalise", a, b); break;
    case C_FADEIN: wave_fade(&take, a, b, 1); edited("fade-in", a, b); break;
    case C_FADEOUT: wave_fade(&take, a, b, 0); edited("fade-out", a, b); break;
    case C_REVERSE: wave_reverse(&take, a, b); edited("reverse", a, b); break;
    case C_SILENCE: wave_silence(&take, a, b); edited("silence", a, b); break;
    case C_MONO:
    case C_STEREO:
        if ((r = wave_set_channels(&take, id == C_MONO ? 1 : 2)) != WAVE_OK)
            no_memory();
        else
            edited(id == C_MONO ? "mono" : "stereo", 0, take.frames);
        break;
    default:
        if (id >= C_RATE && id < C_RATE_END) {
            unsigned long long was = take.rate;
            if ((r = wave_resample(&take, rates[id - C_RATE])) != WAVE_OK) {
                no_memory();
                break;
            }
            sel_a = (unsigned long)(sel_a * take.rate / was), sel_b = (unsigned long)(sel_b * take.rate / was);
            edited("rate", 0, take.frames);
        }
    }
}

/* ---- files */

static const char *const filters[] = { "Sounds|*.WAV", "All files|*.*", NULL };

static int save(int as);

static int discard(void)                        /* 1: the take may go (saved, or the user says so) */
{
    int r;
    if (!changed || !take.frames)
        return 1;
    r = gui_message(win, GUI_T("Sound Recorder"), GUI_T("Save the changes to this take?"), GUI_MB_YESNOCANCEL);
    if (r == GUI_YES)
        return save(0);
    return r == GUI_NO;
}

static int save(int as)
{
    struct gui_file_opts o;
    char p[260];
    int r;
    stop();
    if (!take.frames)
        return 0;
    snprintf(p, sizeof p, "%s", path);
    if (as || !path[0]) {
        memset(&o, 0, sizeof o);
        o.filters = filters;
        if (!gui_file_dialog_ex(win, GUI_T("Save As"), 1, p, sizeof p, &o))
            return 0;
        if (!strchr(gui_path_name(p), '.') && strlen(p) + 5 < sizeof p)
            strcat(p, ".WAV");
    }
    if ((r = wave_save(p, &take, save_kind)) != WAVE_OK) {
        char t[200];
        say("refused save %d", r);
        snprintf(t, sizeof t, GUI_T("The take couldn't be saved: %s."), GUI_T(wave_error(r)));
        gui_message(win, GUI_T("Sound Recorder"), t, GUI_MB_OK);
        return 0;
    }
    snprintf(path, sizeof path, "%s", p);
    changed = 0;
    say("saved %s %lu %s", gui_path_name(path), take.frames, kind_words[save_kind]);
    show();
    return 1;
}

static void open_file(const char *p)
{
    struct wave w;
    int r = wave_load(p, &w);
    if (r != WAVE_OK) {
        char t[200];
        say("refused open %d", r);
        snprintf(t, sizeof t, GUI_T("%s couldn't be opened: %s."), gui_path_name(p), GUI_T(wave_error(r)));
        gui_message(win, GUI_T("Sound Recorder"), t, GUI_MB_OK);
        return;
    }
    wave_free(&take);
    take = w;
    if (have_undo)
        wave_free(&undo);
    have_undo = 0;
    snprintf(path, sizeof path, "%s", p);
    changed = clipped = 0;
    sel_a = sel_b = 0;
    view_all();
    say("opened %s %lu %u %u peak %u", gui_path_name(path), take.frames, take.rate, take.channels, wave_peak(&take, 0, take.frames));
    show();
}

static void command(struct gui_window *w, int id)
{
    char p[260];
    struct gui_file_opts o;
    (void)w;
    switch (id) {
    case C_NEW:
        stop();
        if (!discard())
            return;
        wave_free(&take);
        wave_init(&take, new_rate, new_ch);
        if (have_undo)
            wave_free(&undo);
        have_undo = 0;
        path[0] = 0;
        changed = clipped = 0;
        sel_a = sel_b = 0;
        view_all();
        say("new");
        show();
        return;
    case C_OPEN:
        stop();
        if (!discard())
            return;
        snprintf(p, sizeof p, "%s", path);
        memset(&o, 0, sizeof o);
        o.filters = filters;
        if (gui_file_dialog_ex(win, GUI_T("Open"), 0, p, sizeof p, &o))
            open_file(p);
        return;
    case C_SAVE: save(0); return;
    case C_SAVEAS: save(1); return;
    case C_EXIT:
        stop();
        if (discard())
            gui_close_later(win);
        return;
    case C_RECORD: record(); return;
    case C_PLAY: play(); return;
    case C_STOP: stop(); return;
    case C_ZOOMIN:
        if (view_n > 64) {
            unsigned long at = sel_b;
            view_n /= 2;
            view_from = at > view_n / 2 ? at - view_n / 2 : 0;
            view_keep(at);
            show();
        }
        return;
    case C_ZOOMOUT:
        view_n = view_n * 2 > take.frames ? take.frames : view_n * 2;
        view_keep(sel_b);
        show();
        return;
    case C_ZOOMALL:
        view_all();
        show();
        return;
    }
    if (id >= C_KIND && id < C_KIND_END) {
        save_kind = id - C_KIND;
        say("kind %s", kind_words[save_kind]);
        return;
    }
    if ((id == C_MONO || id == C_STEREO || (id >= C_RATE && id < C_RATE_END)) && !take.frames) {
        if (id == C_MONO || id == C_STEREO)     /* nothing recorded yet: the next take's */
            new_ch = id == C_MONO ? 1 : 2;
        else
            new_rate = rates[id - C_RATE];
        wave_free(&take);
        wave_init(&take, new_rate, new_ch);
        say("format %u Hz %u channels", new_rate, new_ch);
        return;
    }
    edit(id);
}

static void update(struct gui_window *w)
{
    unsigned long from, to;
    int i, some = take.frames != 0, busy = in_h || out_h;
    sel(&from, &to);
    gui_command_enable(w, C_SAVE, some);
    gui_command_enable(w, C_SAVEAS, some);
    gui_command_enable(w, C_UNDO, have_undo && !in_h);
    gui_command_enable(w, C_CUT, from != to);
    gui_command_enable(w, C_COPY, from != to);
    gui_command_enable(w, C_DELETE, from != to);
    gui_command_enable(w, C_TRIM, from != to);
    gui_command_enable(w, C_PASTE, have_clip);
    gui_command_enable(w, C_ALL, some);
    for (i = C_LOUDER; i <= C_SILENCE; i++)
        gui_command_enable(w, i, some);
    gui_command_radio(w, C_MONO, (some ? take.channels : new_ch) == 1);
    gui_command_radio(w, C_STEREO, (some ? take.channels : new_ch) == 2);
    for (i = 0; i < 4; i++)
        gui_command_radio(w, C_RATE + i, (some ? take.rate : new_rate) == rates[i]);
    for (i = 0; i < 3; i++)
        gui_command_radio(w, C_KIND + i, save_kind == i);
    gui_command_enable(w, C_RECORD, !in_h);
    gui_command_enable(w, C_PLAY, some && !in_h);
    gui_command_enable(w, C_STOP, busy);
    gui_command_enable(w, C_ZOOMIN, some && view_n > 64);
    gui_command_enable(w, C_ZOOMOUT, some && view_n < take.frames);
    gui_command_enable(w, C_ZOOMALL, some && view_n < take.frames);
}

static int key(struct gui_window *w, struct gui_ev *e)
{
    unsigned long step = view_n / 100 ? view_n / 100 : 1;
    (void)w;
    if (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
        return 0;
    switch (e->key) {
    case GUI_K_ESC:
        if (!in_h && !out_h)
            return 0;
        stop();
        return 1;
    case GUI_K_LEFT: sel_b = sel_b > step ? sel_b - step : 0; break;
    case GUI_K_RIGHT: sel_b = sel_b + step > take.frames ? take.frames : sel_b + step; break;
    case GUI_K_HOME: sel_b = 0; break;
    case GUI_K_END: sel_b = take.frames; break;
    default: return 0;
    }
    if (!(e->mods & GLOS_MOD_SHIFT))
        sel_a = sel_b;
    view_keep(sel_b);
    show();
    return 1;
}

static void closing(struct gui_window *w)
{
    stop();
    if (discard())
        gui_close(w);
}

static void build(void)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *row, *tb;
    char t[40];
    int i;
    win = gui_window(GUI_T("Sound Recorder"), 520, 250, 0);
    win->on_command = command;
    win->on_update = update;
    win->on_key = key;
    win->on_close = closing;
    gui_command(win, C_NEW, GUI_T("&New"), "Ctrl+N", gui_glyph("new"), GUI_T("A new take"));
    gui_command(win, C_OPEN, GUI_T("&Open..."), "Ctrl+O", gui_glyph("open"), GUI_T("Open a WAV file"));
    gui_command(win, C_SAVE, GUI_T("&Save"), "Ctrl+S", gui_glyph("save"), GUI_T("Save the take"));
    gui_command(win, C_SAVEAS, GUI_T("Save &As..."), NULL, NULL, NULL);
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    gui_command(win, C_UNDO, GUI_T("&Undo"), "Ctrl+Z", gui_glyph("undo"), GUI_T("Undo"));
    gui_command(win, C_CUT, GUI_T("Cu&t"), "Ctrl+X", gui_glyph("cut"), GUI_T("Cut"));
    gui_command(win, C_COPY, GUI_T("&Copy"), "Ctrl+C", gui_glyph("copy"), GUI_T("Copy"));
    gui_command(win, C_PASTE, GUI_T("&Paste"), "Ctrl+V", gui_glyph("paste"), GUI_T("Paste"));
    gui_command(win, C_DELETE, GUI_T("&Delete"), "Del", NULL, NULL);
    gui_command(win, C_TRIM, GUI_T("T&rim to Selection"), "Ctrl+T", NULL, NULL);
    gui_command(win, C_ALL, GUI_T("Select &All"), "Ctrl+A", NULL, NULL);
    gui_command(win, C_LOUDER, GUI_T("&Louder"), NULL, NULL, NULL);
    gui_command(win, C_SOFTER, GUI_T("&Softer"), NULL, NULL, NULL);
    gui_command(win, C_NORMALISE, GUI_T("As Loud as &Fits"), NULL, NULL, NULL);
    gui_command(win, C_FADEIN, GUI_T("Fade &In"), NULL, NULL, NULL);
    gui_command(win, C_FADEOUT, GUI_T("Fade &Out"), NULL, NULL, NULL);
    gui_command(win, C_REVERSE, GUI_T("&Backwards"), NULL, NULL, NULL);
    gui_command(win, C_SILENCE, GUI_T("Si&lence"), NULL, NULL, NULL);
    gui_command(win, C_MONO, GUI_T("&One Channel"), NULL, NULL, NULL);
    gui_command(win, C_STEREO, GUI_T("&Two Channels"), NULL, NULL, NULL);
    for (i = 0; i < 4; i++) {
        snprintf(t, sizeof t, GUI_T("%u Hz"), rates[i]);
        gui_command(win, C_RATE + i, t, NULL, NULL, NULL);
    }
    for (i = 0; i < 3; i++) {
        snprintf(t, sizeof t, GUI_T("Save as %s"), GUI_T(kinds[i]));
        gui_command(win, C_KIND + i, t, NULL, NULL, NULL);
    }
    gui_command(win, C_RECORD, GUI_T("&Record"), "Ctrl+R", NULL, GUI_T("Record onto the take's end"));
    gui_command(win, C_PLAY, GUI_T("&Play"), "F5", gui_glyph("play"), GUI_T("Play"));
    gui_command(win, C_STOP, GUI_T("&Stop"), "F6", gui_glyph("stop"), GUI_T("Stop"));
    gui_command(win, C_ZOOMIN, GUI_T("Zoom &In"), "Ctrl++", gui_glyph("zoom-in"), GUI_T("Zoom in"));
    gui_command(win, C_ZOOMOUT, GUI_T("Zoom &Out"), "Ctrl+-", gui_glyph("zoom-out"), GUI_T("Zoom out"));
    gui_command(win, C_ZOOMALL, GUI_T("&All of It"), "Ctrl+0", NULL, NULL);

    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_command(m, C_NEW), gui_menu_command(m, C_OPEN), gui_menu_command(m, C_SAVE), gui_menu_command(m, C_SAVEAS);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_command(m, C_UNDO);
    gui_menu_sep(m);
    gui_menu_command(m, C_CUT), gui_menu_command(m, C_COPY), gui_menu_command(m, C_PASTE), gui_menu_command(m, C_DELETE);
    gui_menu_sep(m);
    gui_menu_command(m, C_TRIM), gui_menu_command(m, C_ALL);
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_command(m, C_ZOOMIN), gui_menu_command(m, C_ZOOMOUT), gui_menu_command(m, C_ZOOMALL);
    m = gui_menu(bar, GUI_T("Effec&ts"));
    for (i = C_LOUDER; i <= C_SILENCE; i++)
        gui_menu_command(m, i);
    m = gui_menu(bar, GUI_T("F&ormat"));
    gui_menu_command(m, C_MONO), gui_menu_command(m, C_STEREO);
    gui_menu_sep(m);
    for (i = 0; i < 4; i++)
        gui_menu_command(m, C_RATE + i);
    gui_menu_sep(m);
    for (i = 0; i < 3; i++)
        gui_menu_command(m, C_KIND + i);
    m = gui_menu(bar, GUI_T("&Control"));
    gui_menu_command(m, C_RECORD), gui_menu_command(m, C_PLAY), gui_menu_command(m, C_STOP);

    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_NEW), gui_toolbar_command(tb, C_OPEN), gui_toolbar_command(tb, C_SAVE);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_RECORD), gui_toolbar_command(tb, C_PLAY), gui_toolbar_command(tb, C_STOP);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_UNDO), gui_toolbar_command(tb, C_CUT), gui_toolbar_command(tb, C_COPY), gui_toolbar_command(tb, C_PASTE);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_ZOOMIN), gui_toolbar_command(tb, C_ZOOMOUT);

    row = gui_hbox(win->root, 4, 4);
    gui_grow(row, 1);
    canvas = gui_canvas(row, 0, 0, canvas_draw, canvas_event);
    gui_grow(canvas, 1);
    meter = gui_meter(row, 2, 1);
    gui_size(meter, 14, 0);
    status = gui_status(win);
}

int main(int argc, char **argv)
{
    const char *file = NULL;
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("RECORDER no GLOS\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-demo"))
            demo = 1;
        else if (!strcmp(argv[i], "-max") && i + 1 < argc)
            max_bytes = strtoul(argv[++i], NULL, 10) * 1024;
        else if (argv[i][0] != '-')
            file = argv[i];
    }
    wave_files(gui_fopen, gui_unlink);
    wave_init(&take, new_rate, new_ch);
    build();
    gui_show(win, 1);
    show();
    if (file)
        open_file(file);
    gui_idle(tick, NULL, 50);
    gui_run();
    halt();
    printf("RECORDER end\n");
    return 0;
}
