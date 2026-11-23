/* STUDIO: MIDI Studio (GLOS v1, phase F; docs/v1/apps/midi-studio.md): its first part. A song of tracks, each an
 * instrument of General MIDI's on a channel (apps/shared/song.c); the chosen track's notes on a piano roll, put
 * in, moved, stretched and taken out with the mouse; the song played through AUDIO's MIDI port (the card's FM
 * chip or an MPU-401: apps/shared/midiplay.c), and read from and written as a standard MIDI file.
 *
 *   the tracks    a row a track: its name, its instrument, its channel, whether it is muted or solo
 *   the roll      time across (a line a beat, a darker one a bar), the keys down it (a C's row is named):
 *                 a click on nothing puts a note there (on the grid, a sixteenth; as long as the last one
 *                 made), and the drag that follows moves it; a click on a note chooses it (Ctrl: one more or
 *                 one fewer), a drag moves what is chosen, a drag of a note's right end stretches it; the
 *                 right button, or Del for what is chosen, takes notes out
 *   playing       F5 plays what is to be heard from the start (a muted track is not; where a track is solo,
 *                 those alone), a line going along the roll; F6 stops. A thread of its own feeds the port, so
 *                 that the music goes on whatever the window does
 *
 * Every change to a track's notes is a step of the history (Ctrl+Z, Ctrl+Y). STUDIO -demo: no MIDI device is
 * asked for and no time passes by itself: F9 is a second heard (the GUI scripts' way).
 *
 * "STUDIO ..." lines on stdout (the suite's). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "../shared/midiplay.h"
#include "../shared/song.h"

enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_EXIT, C_UNDO, C_REDO, C_DELETE, C_SELALL, C_TRACK, C_DRUMS, C_UNTRACK, C_INSTR,
       C_MUTE, C_SOLO, C_PLAY, C_STOP, C_TEMPO, C_ZOOMIN, C_ZOOMOUT, C_RECORD, C_CLICK, C_COUNTIN, C_STEP, C_STEPLEN,
       C_QUANT = C_STEPLEN + 4, C_UP, C_DOWN, C_OCTUP, C_OCTDOWN, C_METER, C_VELOCITY, C_ARRANGE, C_BCOPY, C_BPASTE, C_LOOP };
                                        /* (C_STEPLEN + n: a sixteenth, an eighth, a quarter, a half) */

#define ROW 8                           /* a key's row on the roll: pixels */
#define GRID (SONG_PPQ / 4)             /* notes are put and moved on sixteenths */
#define FILE_MAX (4ul << 20)

static const char *const gm[128] = {
    "Acoustic Grand Piano", "Bright Acoustic Piano", "Electric Grand Piano", "Honky-tonk Piano", "Electric Piano 1",
    "Electric Piano 2", "Harpsichord", "Clavinet", "Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone",
    "Tubular Bells", "Dulcimer", "Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion",
    "Harmonica", "Tango Accordion", "Acoustic Guitar (nylon)", "Acoustic Guitar (steel)", "Electric Guitar (jazz)",
    "Electric Guitar (clean)", "Electric Guitar (muted)", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
    "Acoustic Bass", "Electric Bass (finger)", "Electric Bass (pick)", "Fretless Bass", "Slap Bass 1", "Slap Bass 2",
    "Synth Bass 1", "Synth Bass 2", "Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings",
    "Orchestral Harp", "Timpani", "String Ensemble 1", "String Ensemble 2", "Synth Strings 1", "Synth Strings 2", "Choir Aahs",
    "Voice Oohs", "Synth Voice", "Orchestra Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section",
    "Synth Brass 1", "Synth Brass 2", "Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon",
    "Clarinet", "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina", "Lead 1 (square)",
    "Lead 2 (sawtooth)", "Lead 3 (calliope)", "Lead 4 (chiff)", "Lead 5 (charang)", "Lead 6 (voice)", "Lead 7 (fifths)",
    "Lead 8 (bass + lead)", "Pad 1 (new age)", "Pad 2 (warm)", "Pad 3 (polysynth)", "Pad 4 (choir)", "Pad 5 (bowed)",
    "Pad 6 (metallic)", "Pad 7 (halo)", "Pad 8 (sweep)", "FX 1 (rain)", "FX 2 (soundtrack)", "FX 3 (crystal)", "FX 4 (atmosphere)",
    "FX 5 (brightness)", "FX 6 (goblins)", "FX 7 (echoes)", "FX 8 (sci-fi)", "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba",
    "Bagpipe", "Fiddle", "Shanai", "Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum",
    "Reverse Cymbal", "Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause",
    "Gunshot" };

static struct gui_window *win;
static struct gui_widget *tracks, *roll, *status, *clock;
static struct song *song;
static struct gui_undo *history;
static int cur;                         /* the track the roll shows */
static char path[260];                  /* the song's file ("": none yet) */
static int changed, demo, setting;
static int zoom = 8;                    /* ticks to a pixel across */
static unsigned long new_len = SONG_PPQ;        /* how long the next note put in is */

/* what plays */
static struct smf *heard;
static struct midiplay mp;
static glos_handle lock, thread;
static int playing, quit, over;         /* over: the thread saw it play out */
static unsigned long demo_ms;
/* (2026-11-23) A loop: the bars chosen in the arrangement played over and over (from, to: the song's ms) */
static int loop_on, loops;
static unsigned long loop_from, loop_to;
static long line = -1;                  /* the tick the playing is at (-1: it isn't) */
/* (phase F) Step entry: a note at the cursor for each key played, the cursor on by the step's length after it. */
static int stepping;                    /* on (Edit, Step Entry: F8) */
static unsigned long step_at;           /* the cursor: a tick */
static int step_len = 1;                /* 0 a sixteenth, 1 an eighth, 2 a quarter, 3 a half */
static glos_handle step_out, step_in;   /* the notes heard as they are put; a MIDI keyboard's */

/* a drag on the roll */
enum { D_NONE, D_MOVE, D_STRETCH };
struct step {                           /* a track's notes before a change and after it */
    int track;
    struct song_note *before, *after;
    unsigned long nbefore, nafter;
};
static int drag, drag_x, drag_key, drag_added;
static struct step *drag_step;
static struct song_note *drag_from;     /* the notes as the drag began */

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("STUDIO ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void take(void) { if (lock) glos_take(lock, 0xFFFFFFFFu); }
static void give(void) { if (lock) glos_give(lock); }

/* ---- what shows */

static const char *instrument(const struct song_track *t)
{
    return t->channel == SONG_DRUMS ? GUI_T("Drums") : gm[t->program & 127];
}

static void show_tracks(void)
{
    const char *cells[4];
    char name[40], ch[8];
    unsigned i;
    setting = 1;
    gui_list_clear(tracks);
    for (i = 0; i < song->ntracks; i++) {
        const struct song_track *t = &song->track[i];
        if (t->name[0])
            snprintf(name, sizeof name, "%s", t->name);
        else
            snprintf(name, sizeof name, GUI_T("Track %u"), i + 1);
        snprintf(ch, sizeof ch, "%u", t->channel + 1);
        cells[0] = name, cells[1] = instrument(t), cells[2] = ch;
        cells[3] = t->solo ? GUI_T("Solo") : t->mute ? GUI_T("Muted") : "";
        gui_list_add(tracks, cells, NULL, NULL);
    }
    gui_list_set(tracks, cur);
    setting = 0;
}

static void show_status(void)
{
    char t[200], n1[40], n2[40];
    unsigned long notes = 0;
    unsigned i;
    for (i = 0; i < song->ntracks; i++)
        notes += song->track[i].nnotes;
    snprintf(n1, sizeof n1, song->ntracks == 1 ? GUI_T("%u track") : GUI_T("%u tracks"), song->ntracks);
    snprintf(n2, sizeof n2, notes == 1 ? GUI_T("%lu note") : GUI_T("%lu notes"), notes);
    snprintf(t, sizeof t, GUI_T("%s%s: %s, %s, %u quarter notes a minute"), path[0] ? gui_path_name(path) : GUI_T("A new song"),
             changed ? "*" : "", n1, n2, song_tempo_at(song, 0));
    gui_set_text(status, t);
}

static void show_clock(void)            /* where the playing is: bar, beat */
{
    char t[40];
    unsigned long beat = SONG_PPQ * 4ul / (song->unit ? song->unit : 4), at = line < 0 ? 0 : (unsigned long)line;
    unsigned beats = song->beats ? song->beats : 4;
    snprintf(t, sizeof t, "%03lu.%02lu", at / (beat * beats) + 1, at / beat % beats + 1);
    gui_set_text(clock, t);
}

/* (2026-11-23) The arrangement: the roll's canvas shows every track as a row, the bars along it, each bar's notes
   small in it; bars of a track chosen (a press, a drag along) are copied (Ctrl+C), pasted from a bar chosen
   (Ctrl+V: onto that track, at that bar) or deleted (Del). View, Arrangement (F4) and back. */
#define AROW 24                         /* a track's row: pixels */
static int arrange, asel_t = -1, adrag;
static unsigned long asel_a, asel_b, adrag_from;        /* the bars chosen: from, to (both in it) */
static struct song_note *bclip;         /* (Copy Bars) their notes, from the first bar's start */
static unsigned long nbclip, bclip_bars;

static unsigned long bar_ticks(void)
{
    return SONG_PPQ * 4ul / (song->unit ? song->unit : 4) * (song->beats ? song->beats : 4);
}

static void size_roll(void)             /* the roll's document: the song and eight bars, 32 at least; the keys */
{
    unsigned long bar = bar_ticks();
    unsigned long w = song_ticks(song) + 8 * bar;
    if (w < 32 * bar)
        w = 32 * bar;
    if (w > 0x3FFFFFFul)
        w = 0x3FFFFFFul;
    gui_cview_size(roll, (int)w, arrange ? (int)(song->ntracks ? song->ntracks : 1) * AROW * zoom : 128 * ROW * zoom);
    gui_cview_scale(roll, 1, zoom);
}

static void show_all(void)
{
    if (cur >= (int)song->ntracks)
        cur = (int)song->ntracks - 1;
    show_tracks();
    show_status();
    show_clock();
    size_roll();
    gui_damage(roll);
}

/* ---- the roll */

static int black(int key)
{
    static const char is[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };
    return is[key % 12];
}

/* (2026-11-23) A note's colour by its velocity: the accent's at 127, paler towards the field's as it is softer
   (a third of the way at 1). */
static unsigned soft(unsigned rgb, unsigned force)
{
    unsigned field = GUI_ROLE(GLOS_ROLE_FIELD), out = 0, k = 127 + (127 - (force > 127 ? 127 : force)) / 2;
    int sh;
    for (sh = 0; sh < 24; sh += 8) {
        unsigned a = (rgb >> sh) & 0xFF, b = (field >> sh) & 0xFF;
        out |= ((a * 127 + b * (k - 127)) / k) << sh;
    }
    return out;
}

static void arrange_draw(struct gfx_surface *s, const struct gui_viewport *v)
{
    unsigned long bar = bar_ticks(), i;
    unsigned tr;
    long tk;
#define AX(tick) (v->ox + (int)((long)(tick) / zoom))
    for (tr = 0; tr < song->ntracks; tr++) {
        int y = v->oy + (int)tr * AROW;
        const struct song_track *t = &song->track[tr];
        gfx_fill(s, v->area.x, y, v->area.w, AROW, GUI_ROLE(tr == (unsigned)cur ? GLOS_ROLE_FIELD : GLOS_ROLE_FACE));
        if ((int)tr == asel_t)                  /* the bars chosen */
            gfx_fill(s, AX(asel_a * bar), y, AX((asel_b + 1) * bar) - AX(asel_a * bar), AROW,
                     soft(GUI_ROLE(GLOS_ROLE_CHOSEN), 30));
        for (i = 0; i < t->nnotes; i++) {       /* its notes, small: the row's height the keys' 128 */
            const struct song_note *n = &t->notes[i];
            int x0 = AX(n->at), x1 = AX(n->at + n->len), ny = y + 2 + (127 - n->key) * (AROW - 4) / 128;
            if ((long)(n->at + n->len) < v->doc.x || (long)n->at > v->doc.x + v->doc.w)
                continue;
            gfx_fill(s, x0, ny, x1 > x0 + 1 ? x1 - x0 : 2, 2, soft(GUI_ROLE(GLOS_ROLE_MARK), n->force));
        }
        gfx_fill(s, v->area.x, y + AROW - 1, v->area.w, 1, GUI_ROLE(GLOS_ROLE_SHADE));
    }
    for (tk = (long)(v->doc.x / (long)bar * (long)bar); tk <= v->doc.x + v->doc.w; tk += (long)bar)
        if (tk >= 0)
            gfx_fill(s, AX(tk), v->area.y, 1, v->area.h, GUI_ROLE(GLOS_ROLE_LINE));
    if (line >= 0)
        gfx_fill(s, AX(line), v->area.y, 1, v->area.h, 0xD02020);
#undef AX
}

static void roll_draw(struct gui_widget *w, struct gfx_surface *s, const struct gui_viewport *v)
{
    const struct song_track *t = &song->track[cur];
    if (arrange) {
        (void)w;
        arrange_draw(s, v);
        return;
    }
    const struct gfx_font *f = gui_font_get(GUI_FONT_SANS, 0);
    unsigned long beat = SONG_PPQ * 4ul / (song->unit ? song->unit : 4), bar = beat * (song->beats ? song->beats : 4), i;
    long tk;
    int row, first = v->doc.y / (ROW * zoom), last = (v->doc.y + v->doc.h) / (ROW * zoom) + 1;
    (void)w;
#define X(tick) (v->ox + (int)((long)(tick) / zoom))
    for (row = first < 0 ? 0 : first; row <= last && row < 128; row++) {
        int key = 127 - row, y = v->oy + row * ROW;
        gfx_fill(s, v->area.x, y, v->area.w, ROW, GUI_ROLE(black(key) ? GLOS_ROLE_FACE : GLOS_ROLE_FIELD));
        if (key % 12 == 0)                      /* (a line under each C) */
            gfx_fill(s, v->area.x, y + ROW - 1, v->area.w, 1, GUI_ROLE(GLOS_ROLE_SHADE));
    }
    for (tk = (long)(v->doc.x / (long)beat * (long)beat); tk <= v->doc.x + v->doc.w; tk += (long)beat)
        if (tk >= 0)
            gfx_fill(s, X(tk), v->area.y, 1, v->area.h, GUI_ROLE((unsigned long)tk % bar ? GLOS_ROLE_SHADE : GLOS_ROLE_LINE));
    for (i = 0; i < t->nnotes; i++) {
        const struct song_note *n = &t->notes[i];
        int x0, x1, y;
        if ((long)(n->at + n->len) < v->doc.x || (long)n->at > v->doc.x + v->doc.w)
            continue;
        x0 = X(n->at), x1 = X(n->at + n->len), y = v->oy + (127 - n->key) * ROW;
        if (x1 < x0 + 3)
            x1 = x0 + 3;
        gfx_fill(s, x0, y, x1 - x0, ROW - 1, n->sel ? GUI_ROLE(GLOS_ROLE_CHOSEN) : soft(GUI_ROLE(GLOS_ROLE_MARK), n->force));
        gfx_frame(s, x0, y, x1 - x0, ROW - 1, GUI_ROLE(GLOS_ROLE_LINE));
    }
    for (row = first < 0 ? 0 : first; f && row <= last && row < 128; row++)     /* the C's names, where they show */
        if ((127 - row) % 12 == 0) {
            char name[8];
            snprintf(name, sizeof name, "C%d", (127 - row) / 12 - 1);
            gfx_text(s, v->area.x + 2, v->oy + row * ROW - 2, f, name, -1, GUI_ROLE(GLOS_ROLE_DIM));
        }
    if (line >= 0)
        gfx_fill(s, X(line), v->area.y, 1, v->area.h, 0xD02020);
    if (stepping)                               /* step entry's cursor */
        gfx_fill(s, X(step_at), v->area.y, 2, v->area.h, 0x2060D0);
#undef X
}

static long note_at(const struct song_track *t, int x, int key)        /* the one on top there, or -1 */
{
    unsigned long i;
    for (i = t->nnotes; i-- > 0; )
        if (t->notes[i].key == key && (long)t->notes[i].at <= x && x < (long)(t->notes[i].at + t->notes[i].len))
            return (long)i;
    return -1;
}

/* ---- the history: a track's notes before a change and after it */

static struct song_note *notes_copy(const struct song_track *t)
{
    struct song_note *n = malloc((t->nnotes + 1) * sizeof *n);
    if (n)
        memcpy(n, t->notes, t->nnotes * sizeof *n);
    return n;
}

static void notes_put(int track, const struct song_note *n, unsigned long count)
{
    struct song_track *t = &song->track[track];
    struct song_note *m = malloc((count + 1) * sizeof *m);
    if (!m)
        return;
    memcpy(m, n, count * sizeof *m);
    free(t->notes);
    t->notes = m, t->nnotes = count, t->note_room = count + 1;
    changed = 1;
    show_status();
    size_roll();
    gui_damage(roll);
}

static void step_undo(void *p)
{
    struct step *st = p;
    notes_put(st->track, st->before, st->nbefore);
    say("undone %lu", song->track[st->track].nnotes);
}

static void step_redo(void *p)
{
    struct step *st = p;
    notes_put(st->track, st->after, st->nafter);
    say("redone %lu", song->track[st->track].nnotes);
}

static void step_drop(void *p)
{
    struct step *st = p;
    free(st->before);
    free(st->after);
    free(st);
}

static struct step *step_begin(void)    /* the chosen track's notes as they are, kept */
{
    struct step *st = calloc(1, sizeof *st);
    if (!st)
        return NULL;
    st->track = cur;
    st->before = notes_copy(&song->track[cur]), st->nbefore = song->track[cur].nnotes;
    return st;
}

static int same_notes(const struct song_note *a, const struct song_note *b, unsigned long n)   /* (but for which are chosen) */
{
    unsigned long i;
    for (i = 0; i < n; i++)
        if (a[i].at != b[i].at || a[i].len != b[i].len || a[i].key != b[i].key || a[i].force != b[i].force)
            return 0;
    return 1;
}

/* ... and as they have become: a step of the history, if they differ (1). */
static int step_end(struct step *st, const char *name)
{
    struct song_track *t = &song->track[cur];
    if (!st)
        return 0;
    song_sort(t);
    st->after = notes_copy(t), st->nafter = t->nnotes;
    if (!st->before || !st->after || (st->nbefore == st->nafter && same_notes(st->before, st->after, st->nafter))) {
        step_drop(st);
        return 0;
    }
    gui_undo_push(history, name, step_undo, step_redo, st, (unsigned)((st->nbefore + st->nafter) * sizeof st->after[0]), step_drop,
                  NULL, 0);
    changed = 1;
    show_status();
    size_roll();
    return 1;
}

static void chord_reset(void);           /* (step entry's chords: below) */
static unsigned char midi_held[128];     /* (step entry: a MIDI keyboard's keys held, for chords) */

static int arrange_event(struct gui_widget *w, struct gui_ev *e, int x, int y)
{
    unsigned long b = x < 0 ? 0 : (unsigned long)x / bar_ticks();
    int tr = y < 0 ? -1 : y / (AROW * zoom);
    if (e->type == GUI_EV_PRESS && e->button == 1 && tr >= 0 && tr < (int)song->ntracks) {
        asel_t = tr, asel_a = asel_b = adrag_from = b, adrag = 1;
        if (cur != tr) {
            cur = tr;
            show_tracks();
        }
        gui_damage(w);
        return 1;
    }
    if (e->type == GUI_EV_MOVE && adrag) {      /* (along the track: the bars from the first to this) */
        asel_a = b < adrag_from ? b : adrag_from, asel_b = b < adrag_from ? adrag_from : b;
        gui_damage(w);
        return 1;
    }
    if (e->type == GUI_EV_RELEASE && adrag) {
        adrag = 0;
        say("bars chosen track=%d from=%lu to=%lu", asel_t + 1, asel_a + 1, asel_b + 1);
        return 1;
    }
    return e->type == GUI_EV_PRESS || e->type == GUI_EV_MOVE || e->type == GUI_EV_RELEASE;
}

static int roll_event(struct gui_widget *w, struct gui_ev *e, int x, int y)
{
    struct song_track *t = &song->track[cur];
    int key = 127 - y / (ROW * zoom);
    unsigned long i;
    long hit;
    if (arrange)
        return arrange_event(w, e, x, y);
    switch (e->type) {
    case GUI_EV_PRESS:
        if (drag || x < 0 || y < 0 || key < 0)
            return 1;
        if (stepping && e->button == 1) {       /* (step entry: the cursor there, on a sixteenth) */
            step_at = (unsigned long)x / GRID * GRID;
            chord_reset();
            say("step at=%lu", step_at);
            gui_damage(w);
            return 1;
        }
        hit = note_at(t, x, key);
        if (e->button == 2) {                   /* the right button: the note there out */
            if (hit >= 0) {
                struct step *st = step_begin();
                song_note_remove(t, (unsigned long)hit);
                step_end(st, GUI_T("Remove Note"));
                say("note removed %lu", t->nnotes);
                gui_damage(w);
            }
            return 1;
        }
        if (e->button != 1)
            return 0;
        if (hit >= 0 && t->notes[hit].sel && (e->mods & GLOS_MOD_CTRL)) {      /* one fewer chosen */
            t->notes[hit].sel = 0;
            gui_damage(w);
            return 1;
        }
        drag_step = step_begin();
        drag_added = 0;
        if (hit < 0 || !t->notes[hit].sel) {
            if (!(e->mods & GLOS_MOD_CTRL) || hit < 0)
                for (i = 0; i < t->nnotes; i++)
                    t->notes[i].sel = 0;
            if (hit < 0) {                      /* nothing there: a note put in, on the grid */
                hit = song_note_add(t, (unsigned long)x / GRID * GRID, new_len, (unsigned)key, 100);
                if (hit < 0) {
                    step_end(drag_step, "");
                    drag_step = NULL;
                    return 1;
                }
                drag_added = 1;
            }
            t->notes[hit].sel = 1;
        }
        drag = !drag_added && x >= (int)(t->notes[hit].at + t->notes[hit].len) - 4 * zoom ? D_STRETCH : D_MOVE;
        drag_x = x, drag_key = key;
        drag_from = notes_copy(t);
        gui_damage(w);
        return 1;
    case GUI_EV_MOVE:
        if (!drag || !drag_from)
            return 0;
        {
            long dx = (x - drag_x) / (long)GRID * (long)GRID;
            int dk = key - drag_key;
            for (i = 0; i < t->nnotes; i++) {
                const struct song_note *o = &drag_from[i];
                struct song_note *n = &t->notes[i];
                if (!o->sel)
                    continue;
                if (drag == D_MOVE) {
                    long at = (long)o->at + dx, k = (long)o->key + dk;
                    n->at = at < 0 ? 0 : (unsigned long)at;
                    n->key = (unsigned char)(k < 0 ? 0 : k > 127 ? 127 : k);
                } else {
                    long len = (long)o->len + dx;
                    n->len = len < (long)GRID ? GRID : (unsigned long)len;
                }
            }
        }
        gui_damage(w);
        return 1;
    case GUI_EV_RELEASE:
        if (!drag)
            return 0;
        for (i = 0; i < t->nnotes; i++)         /* (the next note put in: as long as this one) */
            if (t->notes[i].sel) {
                new_len = t->notes[i].len;
                if (drag_added)
                    say("note added key=%u at=%lu len=%lu", t->notes[i].key, t->notes[i].at, t->notes[i].len);
            }
        if (step_end(drag_step, drag_added ? GUI_T("Add Note") : drag == D_MOVE ? GUI_T("Move Notes") : GUI_T("Stretch Notes"))
            && !drag_added)                     /* (a click that only chose a note is no step) */
            say(drag == D_MOVE ? "notes moved" : "notes stretched");
        free(drag_from);
        drag_from = NULL, drag_step = NULL, drag = D_NONE;
        gui_damage(w);
        return 1;
    }
    return 0;
}

/* ---- playing */

static void stop_playing(void)
{
    take();
    mp_stop(&mp);
    playing = 0, over = 0;
    give();
    smf_free(heard);
    heard = NULL;
    line = -1;
    show_clock();
    gui_damage(roll);
}

static void play(void)
{
    unsigned char *bytes = NULL;
    unsigned long size = 0;
    int err = 0;
    stop_playing();
    if (song_write(song, 1, &bytes, &size) != SONG_OK)
        return;
    heard = smf_open(bytes, size, &err);
    free(bytes);
    if (!heard || !smf_count(heard)) {
        smf_free(heard);
        heard = NULL;
        say("nothing to play");
        return;
    }
    take();
    mp_init(&mp, heard, smf_length(heard) + 500);
    demo_ms = 0;
    err = demo ? 0 : mp_play(&mp);
    playing = err == 0;
    give();
    if (err) {
        say("refused midi");
        smf_free(heard);
        heard = NULL;
        gui_message(win, GUI_T("MIDI Studio"), GUI_T("There is no MIDI device to play through: no FM chip and no MPU-401, "
                                                     "or another program is using it."), GUI_MB_OK);
        return;
    }
    say("playing %lu", smf_count(heard));
    if (loop_on) {                              /* (a loop: from its start) */
        take();
        if (demo)
            demo_ms = loop_from;
        else
            mp_seek(&mp, loop_from);
        give();
        loops = 0;
    }
}

static void feeder(void *arg)
{
    (void)arg;
    for (;;) {
        take();
        if (quit) {
            give();
            return;
        }
        if (playing && !demo && mp_feed(&mp))
            playing = 0, over = 1;
        give();
        glos_sleep(15);
    }
}

static void rec_poll(void);
static void rec_stop(void);
static void step_note(unsigned key, unsigned force);

static void tick(void *arg)             /* the line along the roll; the end seen */
{
    unsigned long ms;
    int ended;
    (void)arg;
    rec_poll();
    if (!heard)
        return;
    take();
    ms = demo ? demo_ms : mp_where(&mp);
    if (loop_on && playing && ms >= loop_to && loop_to > loop_from) {   /* (the loop's end: its start again) */
        unsigned long past = (ms - loop_to) % (loop_to - loop_from);
        if (demo)
            demo_ms = loop_from + past;
        else
            mp_seek(&mp, loop_from + past);
        ms = loop_from + past;
        loops++;
        say("looped %d", loops);
    }
    if (demo && playing && demo_ms >= mp.length)
        playing = 0, over = 1;
    ended = over;
    give();
    if (ended) {
        stop_playing();
        say("ended");
        return;
    }
    line = (long)song_tick(song, ms);
    show_clock();
    gui_damage(roll);
}

/* ---- recording: from a MIDI keyboard (the MPU-401's input: AUDIO's MIDI port in) and from the PC's keys, two
   rows as a piano (Z S X D C V G B H N J M: C4 to B4; Q 2 W 3 E R 5 T 6 Y 7 U: C5 to B5). The song plays meanwhile
   (if it has notes), and each note played goes into the chosen track where it was played, its start on the
   nearest sixteenth, a sixteenth long at least; the notes are heard as they are played. One step of the
   history for the whole take. In the suite's -demo the time is the demo's (F9 a second), so a take is the same
   on every PC. */

static const unsigned char pc_keys[24] = { 0x2C, 0x1F, 0x2D, 0x20, 0x2E, 0x2F, 0x22, 0x30, 0x23, 0x31, 0x24, 0x32,
                                           0x10, 0x03, 0x11, 0x04, 0x12, 0x13, 0x06, 0x14, 0x07, 0x15, 0x08, 0x16 };
#define PC_BASE 60                      /* the first's key: middle C */
#define GRID (SONG_PPQ / 4)             /* a sixteenth */

static int recording, rec_n;
static glos_handle rec_in, rec_out;
static unsigned rec_t0;
static unsigned long rec_demo_ms;

/* (phase F) The click and the count-in: a click on each beat while recording (GM's percussion channel: the
   high wood block on a bar's first beat, the low one on the others; queued a moment ahead with GLOS's times,
   which the kernel keeps), and, if asked for, a bar of clicks before the take (the song, if it plays, begins
   after it). A beat is the song's first tempo's (a change of tempo in the song is not followed); a bar four. */
static int click_on = 1, count_in;      /* (the user's: Record's Click and Count In) */
static unsigned long count_ms;          /* this take's count-in: a bar, or 0 */
static unsigned long next_beat;         /* the next beat to be queued, from the take's start (the count-in's first) */
static int song_waits;                  /* the song begins when the count-in ends */
static struct { unsigned char on, pc, force; unsigned long ms; } held[128];
static struct step *rec_step;
static char rec_keys[200], rec_ats[200];

static unsigned long rec_raw(void)     /* the take's time, its count-in's too */
{
    unsigned long ms;
    if (demo)
        return playing ? demo_ms + count_ms : rec_demo_ms;
    if (!playing)
        return glos_ticks() - rec_t0;
    take();
    ms = mp_where(&mp);
    give();
    return ms + count_ms;
}

static unsigned long rec_now(void)     /* the song's time: where a note played now goes (0 in the count-in) */
{
    unsigned long raw = rec_raw();
    return raw > count_ms ? raw - count_ms : 0;
}

/* (2026-11-23) The clicks follow the song: a beat is its time signature's unit, a bar its beats (the first one
   accented), each beat at the song's own time for it (its tempo's changes); the count-in is a bar at the opening
   tempo. */
static unsigned bar_beats(void) { return song->beats ? song->beats : 4; }

static unsigned long beat_ms(void)      /* the opening tempo's beat */
{
    unsigned t = song_tempo_at(song, 0);
    return 60000ul / (t ? t : 120) * 4 / (song->unit ? song->unit : 4);
}

static unsigned long click_at(unsigned long b, int *first)     /* the take's beat b (the count-in's first): when */
{
    unsigned long nci = count_ms ? bar_beats() : 0;
    if (b < nci) {
        *first = b == 0;
        return b * beat_ms();
    }
    b -= nci;
    *first = b % bar_beats() == 0;
    return count_ms + song_ms(song, b * (SONG_PPQ * 4ul / (song->unit ? song->unit : 4)));
}

/* The clicks of the beats from now to a moment ahead queued; the song begun when the count-in is over. */
static void click_poll(void)
{
    unsigned long raw, at;
    int first;
    if (!recording)
        return;
    raw = rec_raw();
    if (song_waits && raw >= count_ms) {
        song_waits = 0;
        play();                                 /* (its clock from here: rec_raw adds the count-in) */
    }
    while ((at = click_at(next_beat, &first)) <= raw + 1500) {
        glos_handle h = playing ? mp.port : rec_out;
        if (demo && (click_on || at < count_ms))
            say("click at=%lu%s", at, first ? " bar" : "");    /* (the suite's: the clicks as they are queued) */
        if ((click_on || at < count_ms) && h && !demo) {
            struct glos_midi_msg m[2];
            memset(m, 0, sizeof m);
            m[0].time = rec_t0 + (unsigned)at, m[0].status = 0x99, m[0].data1 = first ? 76 : 77, m[0].data2 = first ? 110 : 80;
            m[1].time = m[0].time + 60, m[1].status = 0x89, m[1].data1 = m[0].data1;
            take();
            glos_midi_send(h, m, 2, NULL, NULL);
            give();
        }
        next_beat++;
    }
}

static unsigned clicks_heard(void)     /* (the suite's) the beats clicked so far: their times are past */
{
    unsigned long raw = rec_raw(), k, n = 0, at;
    int first;
    for (k = 0; (at = click_at(k, &first)) <= raw; k++)
        n += click_on || at < count_ms;
    return (unsigned)n;
}

static void rec_sound(unsigned status, unsigned key, unsigned force)
{
    struct glos_midi_msg m;
    glos_handle h = playing ? mp.port : rec_out;
    if (!h || demo)
        return;
    memset(&m, 0, sizeof m);
    m.status = (unsigned char)(status | song->track[cur].channel), m.data1 = (unsigned char)key, m.data2 = (unsigned char)force;
    take();
    glos_midi_send(h, &m, 1, NULL, NULL);
    give();
}

static void rec_on(unsigned key, unsigned force, int pc)
{
    if (!recording || key > 127 || held[key].on)
        return;
    held[key].on = 1, held[key].pc = (unsigned char)pc, held[key].force = (unsigned char)(force ? force : 100);
    held[key].ms = rec_now();                   /* (played in the count-in: at the song's start) */
    rec_sound(0x90, key, held[key].force);
}

static void rec_off(unsigned key)
{
    unsigned long a, b, len;
    size_t k;
    if (!recording || key > 127 || !held[key].on)
        return;
    held[key].on = 0;
    rec_sound(0x80, key, 0);
    a = song_tick(song, held[key].ms), b = song_tick(song, rec_now());
    len = b > a ? (b - a + GRID / 2) / GRID * GRID : 0;
    a = (a + GRID / 2) / GRID * GRID;
    if (song_note_add(&song->track[cur], a, len < GRID ? GRID : len, key, held[key].force) < 0)
        return;
    rec_n++;
    k = strlen(rec_keys);
    if (k + 8 < sizeof rec_keys)
        snprintf(rec_keys + k, sizeof rec_keys - k, "%s%u", k ? "," : "", key);
    k = strlen(rec_ats);
    if (k + 12 < sizeof rec_ats)
        snprintf(rec_ats + k, sizeof rec_ats - k, "%s%lu", k ? "," : "", a);
    size_roll();
    gui_damage(roll);
}

static int pc_key(unsigned code)        /* the key a PC key plays, or -1 */
{
    int i;
    for (i = 0; i < 24; i++)
        if (pc_keys[i] == code)
            return PC_BASE + i;
    return -1;
}

/* The PC's keys as they are held (an idle's look): a key let go ends its note. */
static void rec_poll(void)
{
    struct glos_keys k;
    int i;
    memset(&k, 0, sizeof k);
    k.size = sizeof k;
    click_poll();
    if (!recording || glos_keys(&k) != 0)
        return;
    for (i = 0; i < 24; i++) {
        unsigned key = PC_BASE + (unsigned)i;
        int down = GLOS_KEY_DOWN(&k, pc_keys[i]);
        if (down && !held[key].on)
            rec_on(key, GLOS_KEY_DOWN(&k, 0x2A) || GLOS_KEY_DOWN(&k, 0x36) ? 127 : 100, 1);   /* (Shift: an accent) */
        else if (!down && held[key].on && held[key].pc)
            rec_off(key);
    }
}

static void rec_event(const struct glos_ev *ev)         /* a message of the MIDI keyboard's */
{
    const struct glos_ev_midi *m = (const struct glos_ev_midi *)ev;
    if (ev->kind == GLOS_EV_MIDI && stepping && !recording) {  /* (step entry: a key down is a note at the cursor) */
        if ((m->status & 0xF0) == 0x90 && m->data2) {
            step_note(m->data1 & 0x7F, m->data2);
            midi_held[m->data1 & 0x7F] = 1;
        } else if ((m->status & 0xF0) == 0x80 || (m->status & 0xF0) == 0x90) {
            midi_held[m->data1 & 0x7F] = 0;
        }
        return;
    }
    if (ev->kind != GLOS_EV_MIDI || !recording)
        return;
    if ((m->status & 0xF0) == 0x90 && m->data2)
        rec_on(m->data1 & 0x7F, m->data2, 0);
    else if ((m->status & 0xF0) == 0x80 || (m->status & 0xF0) == 0x90)
        rec_off(m->data1 & 0x7F);
}

static unsigned rec_clicks;

static void rec_stop(void)
{
    unsigned i;
    if (!recording)
        return;
    for (i = 0; i < 128; i++)
        rec_off(i);
    rec_clicks = clicks_heard();
    recording = 0;
    song_waits = 0;
    if (rec_in)
        glos_close(rec_in);
    if (rec_out)
        glos_close(rec_out);
    rec_in = rec_out = 0;
    if (heard)
        stop_playing();
    step_end(rec_step, GUI_T("Record"));
    rec_step = NULL;
    say("recorded %d notes keys=%s at=%s clicks=%u", rec_n, rec_keys[0] ? rec_keys : "-", rec_ats[0] ? rec_ats : "-", rec_clicks);
    show_status();
}

static void rec_start(void)
{
    rec_stop();
    memset(held, 0, sizeof held);
    rec_n = 0, rec_keys[0] = rec_ats[0] = 0;
    rec_step = step_begin();
    rec_in = demo ? 0 : glos_midi_in(GLOS_MIDI_PORT_MPU);   /* (none: the PC's keys alone) */
    count_ms = count_in ? bar_beats() * beat_ms() : 0, next_beat = 0, song_waits = 0;
    if (song_ticks(song) && !count_ms)
        play();                         /* (the song as it is, heard while the new notes are played) */
    else if (song_ticks(song))
        song_waits = 1;                 /* (... after the count-in) */
    if (!playing && !demo)
        rec_out = mp_open_out();
    rec_t0 = glos_ticks();
    rec_demo_ms = 0;
    recording = 1;
    click_poll();
    say("recording in=%s click=%d count=%lu", rec_in ? "mpu+keys" : "keys", click_on, count_ms);
    gui_focus(roll);
}

/* ---- step entry (phase F): Edit, Step Entry (F8). A key of the PC's piano (or a MIDI keyboard's) puts a note
   at the cursor in the chosen track, as long as the step, heard as it is put, and the cursor moves on by the step;
   Space a rest; Backspace back a step, what begins there taken away; Ctrl+1 to Ctrl+4 the step (a sixteenth, an
   eighth, a quarter, a half); a click on the roll the cursor there. Each note one step of the history. */

static unsigned long step_ticks(void)
{
    static const unsigned m[4] = { 1, 2, 4, 8 };
    return (unsigned long)GRID * m[step_len & 3];
}

static void step_sound(unsigned key, unsigned force)
{
    struct glos_midi_msg m[2];
    if (!step_out || demo)
        return;
    memset(m, 0, sizeof m);
    m[0].time = glos_ticks(), m[0].status = (unsigned char)(0x90 | song->track[cur].channel), m[0].data1 = (unsigned char)key;
    m[0].data2 = (unsigned char)force;
    m[1].time = m[0].time + 300, m[1].status = (unsigned char)(0x80 | song->track[cur].channel), m[1].data1 = (unsigned char)key;
    take();
    glos_midi_send(step_out, m, 2, NULL, NULL);
    give();
}

/* (2026-11-23) A chord: a note whose key goes down while a key that put a note at the step before is still held
   goes at that step too, and the cursor stays past it. The PC's piano keys are seen held by glos_keys, a MIDI
   keyboard's by its note-ons and offs. */
static unsigned long chord_at = (unsigned long)-1;
static unsigned char chord_keys[128];   /* the notes put at chord_at (their keys may still be held) */

static void chord_reset(void)
{
    chord_at = (unsigned long)-1;
    memset(chord_keys, 0, sizeof chord_keys);
}

static int key_held(unsigned key, const struct glos_keys *k)
{
    if (midi_held[key])
        return 1;
    return key >= PC_BASE && key < PC_BASE + 24 && GLOS_KEY_DOWN(k, pc_keys[key - PC_BASE]);
}

static void step_note(unsigned key, unsigned force)
{
    struct step *st;
    struct glos_keys k;
    unsigned long len = step_ticks(), at;
    unsigned i;
    int chord = 0;
    if (!stepping || key > 127)
        return;
    memset(&k, 0, sizeof k);
    k.size = sizeof k;
    if (glos_keys(&k) != 0)
        memset(k.down, 0, sizeof k.down);
    for (i = 0; i < 128; i++)                   /* (the chord's keys let go of are its no longer) */
        if (chord_keys[i] && i != key) {
            if (key_held(i, &k))
                chord = 1;
            else
                chord_keys[i] = 0;
        }
    chord = chord && chord_at != (unsigned long)-1;
    at = chord ? chord_at : step_at;
    st = step_begin();
    if (song_note_add(&song->track[cur], at, len, key, force ? force : 100) < 0) {
        step_end(st, "");
        return;
    }
    step_end(st, GUI_T("Step Note"));
    step_sound(key, force ? force : 100);
    say("step note %u at=%lu%s%s", key, at, force > 100 ? " accent" : "", chord ? " chord" : "");
    if (!chord) {
        chord_reset();
        chord_at = step_at;
        step_at += len;
    }
    chord_keys[key] = 1;
    size_roll();
    gui_damage(roll);
}

static void step_rest(void)
{
    chord_reset();
    step_at += step_ticks();
    say("step rest at=%lu", step_at);
    gui_damage(roll);
}

static void step_back(void)
{
    struct song_track *t = &song->track[cur];
    struct step *st;
    unsigned long len = step_ticks(), i, gone = 0;
    step_at = step_at >= len ? step_at - len : 0;
    chord_reset();
    st = step_begin();
    for (i = t->nnotes; i-- > 0;)
        if (t->notes[i].at == step_at)
            song_note_remove(t, i), gone++;
    step_end(st, GUI_T("Step Back"));
    say("step back at=%lu removed=%lu", step_at, gone);
    gui_damage(roll);
}

static void step_toggle(void)
{
    stepping = !stepping;
    chord_reset();
    if (stepping) {
        rec_stop();
        stop_playing();
        if (!demo) {
            step_out = mp_open_out();
            step_in = glos_midi_in(GLOS_MIDI_PORT_MPU);     /* (none: the PC's keys alone) */
        }
        say("step on at=%lu len=%lu", step_at, step_ticks());
        gui_focus(roll);
    } else {
        if (step_out)
            glos_close(step_out);
        if (step_in)
            glos_close(step_in);
        step_out = step_in = 0;
        say("step off notes=%lu", song->track[cur].nnotes);
    }
    gui_damage(roll);
}

/* ---- files */

static void fresh(struct song *s, const char *file)
{
    stop_playing();
    song_free(song);
    song = s;
    snprintf(path, sizeof path, "%s", file);
    cur = 0, changed = 0;
    gui_undo_clear(history);
    show_all();
    gui_cview_scroll(roll, 0, (127 - 72) * ROW * zoom);         /* (the roll's top at the C above the middle one) */
}

static int open_file(const char *file)
{
    FILE *f = gui_fopen(file, "rb");
    unsigned char *bytes;
    struct song *s;
    unsigned long notes = 0;
    long size;
    unsigned i;
    int err = SONG_E_KIND;
    char t[300];
    if (!f || fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) <= 0 || (unsigned long)size > FILE_MAX || fseek(f, 0, SEEK_SET) != 0
        || !(bytes = malloc((size_t)size))) {
        if (f)
            fclose(f);
        say("refused %s %d", gui_path_name(file), -9);
        snprintf(t, sizeof t, GUI_T("%s couldn't be read."), gui_path_name(file));
        gui_message(win, GUI_T("MIDI Studio"), t, GUI_MB_OK);
        return -1;
    }
    s = fread(bytes, 1, (size_t)size, f) == (size_t)size ? song_read(bytes, (unsigned long)size, &err) : NULL;
    fclose(f);
    free(bytes);
    if (!s) {
        say("refused %s %d", gui_path_name(file), err);
        snprintf(t, sizeof t, err == SONG_E_FORMAT ? GUI_T("%s holds several songs side by side (a type 2 MIDI file): that kind isn't read here.")
                 : GUI_T("%s isn't a MIDI file that can be read."), gui_path_name(file));
        gui_message(win, GUI_T("MIDI Studio"), t, GUI_MB_OK);
        return -1;
    }
    fresh(s, file);
    for (i = 0; i < song->ntracks; i++)
        notes += song->track[i].nnotes;
    say("opened %s tracks=%u notes=%lu", gui_path_name(file), song->ntracks, notes);
    return 0;
}

static int save_file(const char *file)
{
    unsigned char *bytes = NULL;
    unsigned long size = 0;
    FILE *f;
    int ok;
    if (song_write(song, 0, &bytes, &size) != SONG_OK || !(f = gui_fopen(file, "wb"))) {
        free(bytes);
        gui_message(win, GUI_T("MIDI Studio"), GUI_T("The song couldn't be written there."), GUI_MB_OK);
        say("not saved %s", gui_path_name(file));
        return -1;
    }
    ok = fwrite(bytes, 1, size, f) == size;
    ok = fclose(f) == 0 && ok;
    free(bytes);
    if (!ok) {
        gui_message(win, GUI_T("MIDI Studio"), GUI_T("The song couldn't be written there: is the disk full?"), GUI_MB_OK);
        say("not saved %s", gui_path_name(file));
        return -1;
    }
    snprintf(path, sizeof path, "%s", file);
    changed = 0;
    show_status();
    say("saved %s %lu", gui_path_name(file), size);
    return 0;
}

static const char *const kinds[] = { "MIDI files|*.MID;*.RMI", "All files|*.*", NULL };

static int save_as(void)
{
    struct gui_file_opts o;
    char p[300];
    memset(&o, 0, sizeof o);
    o.filters = kinds;
    snprintf(p, sizeof p, "%s", path);
    if (!gui_file_dialog_ex(win, GUI_T("Save Song"), 1, p, sizeof p, &o))
        return -1;
    if (!strchr(gui_path_name(p), '.') && strlen(p) + 5 < sizeof p)
        strcat(p, ".MID");
    return save_file(p);
}

static int ask_save(void)               /* 1: go on (saved, or not wanted); 0: the user thought better of it */
{
    int r;
    if (!changed)
        return 1;
    r = gui_message(win, GUI_T("MIDI Studio"), GUI_T("The song has changes that aren't saved. Save them?"), GUI_MB_YESNOCANCEL);
    if (r == GUI_YES)
        return (path[0] ? save_file(path) : save_as()) == 0;
    return r == GUI_NO;
}

/* ---- dialogs */

static struct gui_widget *dlg_list, *dlg_spin, *dlg_spin2, *dlg_choice;
static int dlg_value, dlg_value2;

static void dlg_done(struct gui_widget *b, int what)
{
    (void)what;                         /* (the button's own number is b->id) */
    if (b->id == 1) {                              /* (read while the dialog is there: gui_modal closes it) */
        dlg_value = dlg_list ? gui_list_get(dlg_list) : gui_value(dlg_spin);
        dlg_value2 = dlg_spin2 ? gui_value(dlg_spin2) : dlg_choice ? gui_value(dlg_choice) : 0;
    }
    gui_end_modal(b->win, b->id == 1 ? GUI_OK : GUI_CANCEL_R);
}

static void dlg_open(struct gui_widget *l, int what)
{
    if (what == GUI_ON_ACTIVATE) {
        dlg_value = gui_list_get(dlg_list);
        gui_end_modal(l->win, GUI_OK);
    }
}

static void dlg_buttons(struct gui_window *d, struct gui_widget *box)
{
    struct gui_widget *row = gui_hbox(box, 6, 0);
    (void)d;
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), dlg_done, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), dlg_done, 2)->flags |= GUI_CANCEL;
}

static void pick_instrument(void)
{
    static const char *const head[] = { GUI_N("Instrument") };
    static const int width[] = { 230 };
    struct song_track *t = &song->track[cur];
    struct gui_window *d;
    struct gui_widget *box;
    const char *h[1], *cell[1];
    int i;
    if (t->channel == SONG_DRUMS) {
        gui_message(win, GUI_T("MIDI Studio"), GUI_T("A drum track's keys are its instruments: each key is a drum."), GUI_MB_OK);
        return;
    }
    d = gui_window(GUI_T("Instrument"), 270, 300, GUI_W_DIALOG);
    d->owner = win;
    box = gui_vbox(d->root, 6, 8);
    h[0] = GUI_T(head[0]);
    dlg_list = gui_list(box, 1, h, width);
    gui_size(dlg_list, 250, 230);
    for (i = 0; i < 128; i++) {
        cell[0] = gm[i];
        gui_list_add(dlg_list, cell, NULL, NULL);
    }
    gui_list_set(dlg_list, t->program & 127);
    gui_on(dlg_list, dlg_open, 0);
    dlg_buttons(d, box);
    gui_focus(dlg_list);
    dlg_value = -1;
    if (gui_modal(d) != GUI_OK) {
        say("instrument left");
    } else if (dlg_value >= 0 && dlg_value < 128) {
        t->program = (unsigned char)dlg_value;
        changed = 1;
        say("instrument %d %s", cur + 1, gm[dlg_value]);
        show_tracks();
        show_status();
    }
    dlg_list = NULL;
}

static void pick_tempo(void)
{
    struct gui_window *d = gui_window(GUI_T("Tempo"), 240, 0, GUI_W_DIALOG);
    struct gui_widget *box, *form;
    d->owner = win;
    box = gui_vbox(d->root, 6, 8);
    form = gui_form(box);
    dlg_list = NULL, dlg_choice = NULL;
    dlg_spin = gui_spin(gui_row(form, GUI_T("&Quarter notes a minute:"), NULL), 20, 400, (int)song_tempo_at(song, 0));
    dlg_spin2 = gui_spin(gui_row(form, GUI_T("From &bar:"), NULL), 1, 999, 1);    /* (2026-11-23: a change later on) */
    dlg_buttons(d, box);
    gui_focus(dlg_spin);
    dlg_value = 0, dlg_value2 = 1;
    if (gui_modal(d) != GUI_OK) {
        say("tempo left");
    } else if (dlg_value >= 20 && dlg_value <= 400) {
        unsigned long bar = SONG_PPQ * 4ul / (song->unit ? song->unit : 4) * (song->beats ? song->beats : 4);
        unsigned long at = (unsigned long)(dlg_value2 > 1 ? dlg_value2 - 1 : 0) * bar;
        song_tempo_set(song, at, (unsigned)dlg_value);
        changed = 1;
        say("tempo %u at=%lu", song_tempo_at(song, at), at);
        show_status();
    }
    dlg_spin2 = NULL;
}

static void pick_velocity(void)         /* (2026-11-23) the chosen notes' velocity (or all the track's) */
{
    struct song_track *t = &song->track[cur];
    struct gui_window *d = gui_window(GUI_T("Velocity"), 260, 0, GUI_W_DIALOG);
    struct gui_widget *box, *form;
    unsigned long i, n = 0;
    int any = 0, first = -1;
    struct step *st;
    for (i = 0; i < t->nnotes; i++)
        if (t->notes[i].sel) {
            any = 1;
            if (first < 0)
                first = t->notes[i].force;
        }
    d->owner = win;
    box = gui_vbox(d->root, 6, 8);
    form = gui_form(box);
    dlg_list = NULL, dlg_spin2 = NULL, dlg_choice = NULL;
    dlg_spin = gui_spin(gui_row(form, any ? GUI_T("The chosen notes' &velocity:") : GUI_T("Every note's &velocity:"), NULL),
                        1, 127, first > 0 ? first : 100);
    gui_label(box, GUI_T("1 is the softest, 127 the loudest."));
    dlg_buttons(d, box);
    gui_focus(dlg_spin);
    dlg_value = 0;
    if (gui_modal(d) != GUI_OK || dlg_value < 1 || dlg_value > 127) {
        say("velocity left");
        return;
    }
    st = step_begin();
    for (i = 0; i < t->nnotes; i++)
        if (!any || t->notes[i].sel) {
            t->notes[i].force = (unsigned char)dlg_value;
            n++;
        }
    step_end(st, GUI_T("Velocity"));
    say("velocity %d notes=%lu", dlg_value, n);
    gui_damage(roll);
}

static void pick_meter(void)            /* (2026-11-23) the time signature: beats a bar, and the beat's note */
{
    static const unsigned units[4] = { 2, 4, 8, 16 };
    struct gui_window *d = gui_window(GUI_T("Time Signature"), 240, 0, GUI_W_DIALOG);
    struct gui_widget *box, *form;
    int k;
    d->owner = win;
    box = gui_vbox(d->root, 6, 8);
    form = gui_form(box);
    dlg_list = NULL, dlg_spin2 = NULL;
    dlg_spin = gui_spin(gui_row(form, GUI_T("&Beats a bar:"), NULL), 1, 16, (int)(song->beats ? song->beats : 4));
    dlg_choice = gui_choice(gui_row(form, GUI_T("The beat &is a:"), NULL), 12);
    gui_choice_add(dlg_choice, GUI_T("half note"));
    gui_choice_add(dlg_choice, GUI_T("quarter note"));
    gui_choice_add(dlg_choice, GUI_T("eighth note"));
    gui_choice_add(dlg_choice, GUI_T("sixteenth"));
    for (k = 0; k < 4 && units[k] != (song->unit ? song->unit : 4); k++)
        ;
    gui_set_value(dlg_choice, k < 4 ? k : 1);
    dlg_buttons(d, box);
    gui_focus(dlg_spin);
    dlg_value = 0;
    if (gui_modal(d) != GUI_OK || dlg_value < 1 || dlg_value > 16 || dlg_value2 < 0 || dlg_value2 > 3) {
        say("meter left");
    } else {
        song->beats = (unsigned)dlg_value, song->unit = units[dlg_value2];
        changed = 1;
        say("meter %u/%u", song->beats, song->unit);
        show_status();
        gui_damage(roll);
    }
    dlg_choice = NULL;
}

/* ---- commands */

static void new_track(int channel)
{
    int k = song_track_add(song, channel);
    if (k < 0) {
        gui_message(win, GUI_T("MIDI Studio"), GUI_T("There is no room for another track: a song has sixteen channels."), GUI_MB_OK);
        say("refused track");
        return;
    }
    cur = k, changed = 1;
    gui_undo_clear(history);                    /* (the history is of tracks' notes, by their places in the list) */
    say("track %d channel=%u", k + 1, song->track[k].channel + 1);
    show_all();
}

static void command(struct gui_window *w, int id)
{
    struct song_track *t = &song->track[cur];
    struct gui_file_opts o;
    struct step *st;
    struct song *s;
    unsigned long i, n = 0;
    if (id >= C_STEPLEN && id < C_STEPLEN + 4) {        /* step entry's step */
        step_len = id - C_STEPLEN;
        say("step len=%lu", step_ticks());
        return;
    }
    char p[300];
    (void)w;
    switch (id) {
    case C_NEW:
        if (ask_save() && (s = song_new()) != NULL) {
            fresh(s, "");
            say("new");
        }
        break;
    case C_OPEN:
        if (!ask_save())
            break;
        memset(&o, 0, sizeof o);
        o.filters = kinds;
        snprintf(p, sizeof p, "%s", path);
        if (gui_file_dialog_ex(win, GUI_T("Open Song"), 0, p, sizeof p, &o))
            open_file(p);
        break;
    case C_SAVE:
        if (path[0])
            save_file(path);
        else
            save_as();
        break;
    case C_SAVEAS:
        save_as();
        break;
    case C_EXIT:
        if (ask_save())
            gui_quit(0);
        break;
    case C_UNDO:
        gui_undo(history);
        break;
    case C_REDO:
        gui_redo(history);
        break;
    case C_LOOP:                                /* (2026-11-23) the bars chosen played over and over, or not */
        if (!loop_on && (asel_t < 0 || !arrange)) {
            gui_set_text(status, GUI_T("Choose the bars to loop in the arrangement (View, Arrangement) first."));
            break;
        }
        loop_on = !loop_on;
        if (loop_on)
            loop_from = song_ms(song, asel_a * bar_ticks()), loop_to = song_ms(song, (asel_b + 1) * bar_ticks());
        say("loop %s from=%lu to=%lu", loop_on ? "on" : "off", loop_from, loop_to);
        break;
    case C_ARRANGE:                             /* (2026-11-23) the arrangement, or the roll */
        arrange = !arrange;
        size_roll();
        gui_damage(roll);
        say("arrangement %s", arrange ? "on" : "off");
        break;
    case C_BCOPY:                               /* the bars chosen: their notes kept, from the first bar's start */
        if (!arrange || asel_t < 0 || asel_t >= (int)song->ntracks)
            break;
        {
            const struct song_track *a = &song->track[asel_t];
            unsigned long from = asel_a * bar_ticks(), to = (asel_b + 1) * bar_ticks(), k = 0;
            free(bclip), bclip = NULL, nbclip = 0;
            for (i = 0; i < a->nnotes; i++)
                k += a->notes[i].at >= from && a->notes[i].at < to;
            if (k && !(bclip = malloc(k * sizeof *bclip)))
                break;
            for (i = 0; i < a->nnotes; i++)
                if (a->notes[i].at >= from && a->notes[i].at < to) {
                    bclip[nbclip] = a->notes[i];
                    bclip[nbclip].at -= from, bclip[nbclip++].sel = 0;
                }
            bclip_bars = asel_b - asel_a + 1;
            say("bars copied %lu notes=%lu", bclip_bars, nbclip);
        }
        break;
    case C_BPASTE:                              /* ... onto the track chosen, from the bar chosen */
        if (!arrange || asel_t < 0 || asel_t >= (int)song->ntracks || !bclip_bars)
            break;
        st = step_begin();
        for (i = 0; i < nbclip; i++)
            if (song_note_add(&song->track[asel_t], asel_a * bar_ticks() + bclip[i].at, bclip[i].len, bclip[i].key,
                              bclip[i].force) >= 0)
                n++;
        step_end(st, GUI_T("Paste Bars"));
        say("bars pasted track=%d at=%lu notes=%lu", asel_t + 1, asel_a + 1, n);
        size_roll();
        gui_damage(roll);
        break;
    case C_DELETE:
        if (arrange) {                          /* (the arrangement: the notes of the bars chosen) */
            struct song_track *a;
            unsigned long from = asel_a * bar_ticks(), to = (asel_b + 1) * bar_ticks();
            if (asel_t < 0 || asel_t >= (int)song->ntracks)
                break;
            a = &song->track[asel_t];
            st = step_begin();
            for (i = a->nnotes; i-- > 0; )
                if (a->notes[i].at >= from && a->notes[i].at < to) {
                    song_note_remove(a, i);
                    n++;
                }
            step_end(st, GUI_T("Delete Bars"));
            say("bars deleted %lu", n);
            gui_damage(roll);
            break;
        }
        st = step_begin();
        for (i = t->nnotes; i-- > 0; )
            if (t->notes[i].sel) {
                song_note_remove(t, i);
                n++;
            }
        step_end(st, GUI_T("Delete Notes"));
        say("notes deleted %lu", n);
        gui_damage(roll);
        break;
    case C_SELALL:
        for (i = 0; i < t->nnotes; i++)
            t->notes[i].sel = 1;
        gui_damage(roll);
        break;
    case C_QUANT: {                             /* (2026-11-23) the chosen notes' starts (or all) to the nearest sixteenth */
        int any = 0;
        for (i = 0; i < t->nnotes; i++)
            any |= t->notes[i].sel;
        st = step_begin();
        for (i = 0; i < t->nnotes; i++)
            if (!any || t->notes[i].sel) {
                unsigned long at = (t->notes[i].at + GRID / 2) / GRID * GRID;
                n += at != t->notes[i].at;
                t->notes[i].at = at;
                if (t->notes[i].len < GRID)
                    t->notes[i].len = GRID;
            }
        song_sort(t);
        step_end(st, GUI_T("Quantise"));
        say("quantised %lu", n);
        gui_damage(roll);
        break;
    }
    case C_UP: case C_DOWN: case C_OCTUP: case C_OCTDOWN: {    /* (2026-11-23) the chosen notes (or all) moved */
        int by = id == C_UP ? 1 : id == C_DOWN ? -1 : id == C_OCTUP ? 12 : -12, any = 0, out = 0;
        for (i = 0; i < t->nnotes; i++)
            any |= t->notes[i].sel;
        for (i = 0; i < t->nnotes; i++)
            if ((!any || t->notes[i].sel) && ((int)t->notes[i].key + by < 0 || (int)t->notes[i].key + by > 127))
                out = 1;
        if (out) {                              /* (all or none: no note pushed off the keyboard's end) */
            gui_set_text(status, GUI_T("That would take notes past the keyboard's end."));
            say("transpose refused");
            break;
        }
        st = step_begin();
        for (i = 0; i < t->nnotes; i++)
            if (!any || t->notes[i].sel) {
                t->notes[i].key = (unsigned char)(t->notes[i].key + by);
                n++;
            }
        step_end(st, GUI_T("Transpose"));
        say("transposed %lu by %d", n, by);
        gui_damage(roll);
        break;
    }
    case C_TRACK:
        new_track(-1);
        break;
    case C_DRUMS:
        new_track(SONG_DRUMS);
        break;
    case C_UNTRACK:
        if (song->ntracks > 1) {
            song_track_remove(song, (unsigned)cur);
            changed = 1;
            gui_undo_clear(history);
            say("track removed %u", song->ntracks);
            show_all();
        }
        break;
    case C_INSTR:
        pick_instrument();
        break;
    case C_MUTE:
        t->mute = !t->mute;
        say("mute %d %d", cur + 1, t->mute);
        show_tracks();
        break;
    case C_SOLO:
        t->solo = !t->solo;
        say("solo %d %d", cur + 1, t->solo);
        show_tracks();
        break;
    case C_PLAY:
        rec_stop();
        play();
        break;
    case C_RECORD:
        if (recording)
            rec_stop();
        else
            rec_start();
        break;
    case C_CLICK:
        click_on = !click_on;
        say("click %d", click_on);
        break;
    case C_COUNTIN:
        count_in = !count_in;
        say("count in %d", count_in);
        break;
    case C_STOP:
        if (recording)
            rec_stop();
        else if (heard) {
            stop_playing();
            say("stopped");
        }
        break;
    case C_TEMPO:
        pick_tempo();
        break;
    case C_METER:
        pick_meter();
        break;
    case C_VELOCITY:
        pick_velocity();
        break;
    case C_STEP:
        step_toggle();
        break;
    case C_ZOOMIN:
    case C_ZOOMOUT:
        zoom = id == C_ZOOMIN ? (zoom > 2 ? zoom / 2 : 2) : (zoom < 32 ? zoom * 2 : 32);
        size_roll();
        gui_damage(roll);
        say("zoom %d", zoom);
        break;
    }
}

static void update(struct gui_window *w)
{
    const struct song_track *t = &song->track[cur];
    unsigned long i, chosen = 0;
    for (i = 0; i < t->nnotes; i++)
        chosen += t->notes[i].sel;
    gui_command_enable(w, C_DELETE, arrange ? asel_t >= 0 : chosen > 0);
    gui_command_enable(w, C_SELALL, t->nnotes > 0 && !arrange);
    gui_command_check(w, C_ARRANGE, arrange);
    gui_command_check(w, C_LOOP, loop_on);
    gui_command_enable(w, C_BCOPY, arrange && asel_t >= 0);
    gui_command_enable(w, C_BPASTE, arrange && asel_t >= 0 && bclip_bars > 0);
    gui_command_enable(w, C_UNTRACK, song->ntracks > 1);
    gui_command_enable(w, C_STOP, heard != NULL || recording);
    gui_command_check(w, C_RECORD, recording);
    gui_command_check(w, C_CLICK, click_on);
    gui_command_check(w, C_STEP, stepping);
    {
        int k;
        for (k = 0; k < 4; k++)
            gui_command_check(w, C_STEPLEN + k, step_len == k);
    }
    gui_command_check(w, C_COUNTIN, count_in);
    gui_command_enable(w, C_CLICK, !recording);
    gui_command_enable(w, C_COUNTIN, !recording);
    gui_command_enable(w, C_ZOOMIN, zoom > 2);
    gui_command_enable(w, C_ZOOMOUT, zoom < 32);
    gui_command_check(w, C_MUTE, t->mute);
    gui_command_check(w, C_SOLO, t->solo);
}

static void tracks_event(struct gui_widget *l, int what)
{
    int row = gui_list_get(l);
    if (setting || row < 0 || row >= (int)song->ntracks)
        return;
    if (what == GUI_ON_SELECT && row != cur) {
        cur = row;
        gui_damage(roll);
    } else if (what == GUI_ON_ACTIVATE) {
        cur = row;
        pick_instrument();
    }
}

static int key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    rec_poll();                                 /* (the keys let go since the last look end their notes first, before
                                                   this key moves the time on: the same take on every PC) */
    if (demo && e->key == 0x43) {               /* F9: a second heard */
        take();
        if (playing)
            demo_ms += 1000;
        give();
        rec_demo_ms += 1000;
        tick(NULL);
        if (recording)
            say("take at %lu", rec_raw());      /* (the suite's: each second as it is taken, before the next key) */
        return 1;
    }
    if (recording && !(e->mods & ~GLOS_MOD_SHIFT) && pc_key(e->key) >= 0) {     /* (a key of the PC's piano: its
                                                   note, however short the press; the idle's look at the keys ends it;
                                                   with Shift an accent) */
        rec_on((unsigned)pc_key(e->key), e->mods & GLOS_MOD_SHIFT ? 127 : 100, 1);
        return 1;
    }
    if (stepping && !recording) {               /* step entry's keys */
        if (!(e->mods & ~GLOS_MOD_SHIFT) && pc_key(e->key) >= 0) {
            step_note((unsigned)pc_key(e->key), e->mods & GLOS_MOD_SHIFT ? 127 : 100);   /* (Shift: an accent) */
            return 1;
        }
        if (!e->mods && e->key == 0x39) {       /* Space: a rest */
            step_rest();
            return 1;
        }
        if (!e->mods && e->key == 0x0E) {       /* Backspace: back a step */
            step_back();
            return 1;
        }
        if ((e->mods & GLOS_MOD_CTRL) && e->key >= 0x02 && e->key <= 0x05) {   /* Ctrl+1 to Ctrl+4: the step */
            step_len = e->key - 0x02;
            say("step len=%lu", step_ticks());
            return 1;
        }
    }
    return 0;
}

static void closing(struct gui_window *w)
{
    (void)w;
    if (ask_save())
        gui_quit(0);
}

static void history_changed(struct gui_undo *u, void *ctx)
{
    (void)u, (void)ctx;
    gui_damage(roll);
}

static void build(void)
{
    static const char *const heads[] = { GUI_N("Track"), GUI_N("Instrument"), GUI_N("Channel"), GUI_N("Heard") };
    static const int widths[] = { 150, 220, 70, 80 };
    const char *h[4];
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *tb, *row;
    int i;
    win = gui_window(GUI_T("MIDI Studio"), 620, 430, 0);
    win->on_command = command;
    win->on_update = update;
    win->on_key = key;
    win->on_close = closing;
    gui_command(win, C_NEW, GUI_T("&New"), "Ctrl+N", gui_glyph("new"), GUI_T("A new song"));
    gui_command(win, C_OPEN, GUI_T("&Open..."), "Ctrl+O", gui_glyph("open"), GUI_T("Open a MIDI file"));
    gui_command(win, C_SAVE, GUI_T("&Save"), "Ctrl+S", gui_glyph("save"), GUI_T("Save the song"));
    gui_command(win, C_SAVEAS, GUI_T("Save &As..."), NULL, NULL, NULL);
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    gui_command(win, C_UNDO, GUI_T("&Undo"), "Ctrl+Z", gui_glyph("undo"), GUI_T("Undo"));
    gui_command(win, C_REDO, GUI_T("&Redo"), "Ctrl+Y", gui_glyph("redo"), GUI_T("Redo"));
    gui_command(win, C_DELETE, GUI_T("&Delete Notes"), "Del", gui_glyph("remove"), GUI_T("Delete the chosen notes"));
    gui_command(win, C_SELALL, GUI_T("Select &All"), "Ctrl+A", NULL, NULL);
    gui_command(win, C_QUANT, GUI_T("Quan&tise"), "Ctrl+Q", NULL, GUI_T("The chosen notes' starts (or all of them) to the nearest sixteenth"));
    gui_command(win, C_UP, GUI_T("Trans&pose Up"), "Ctrl+Up", NULL, GUI_T("The chosen notes (or all of them) a semitone up"));
    gui_command(win, C_DOWN, GUI_T("Transpose Do&wn"), "Ctrl+Down", NULL, GUI_T("... a semitone down"));
    gui_command(win, C_OCTUP, GUI_T("An &Octave Up"), "Ctrl+Shift+Up", NULL, NULL);
    gui_command(win, C_OCTDOWN, GUI_T("An Octave Dow&n"), "Ctrl+Shift+Down", NULL, NULL);
    gui_command(win, C_VELOCITY, GUI_T("&Velocity..."), "Ctrl+E", NULL, GUI_T("The chosen notes' velocity (or every note's)"));
    gui_command(win, C_TRACK, GUI_T("&New Track"), "Ctrl+T", gui_glyph("add"), GUI_T("A new track"));
    gui_command(win, C_DRUMS, GUI_T("New &Drum Track"), NULL, NULL, NULL);
    gui_command(win, C_UNTRACK, GUI_T("&Remove Track"), NULL, NULL, NULL);
    gui_command(win, C_INSTR, GUI_T("&Instrument..."), "Ctrl+I", NULL, NULL);
    gui_command(win, C_MUTE, GUI_T("&Mute"), "Ctrl+M", NULL, NULL);
    gui_command(win, C_SOLO, GUI_T("&Solo"), "Ctrl+L", NULL, NULL);
    gui_command(win, C_PLAY, GUI_T("&Play"), "F5", gui_glyph("play"), GUI_T("Play from the start"));
    gui_command(win, C_STOP, GUI_T("&Stop"), "F6", gui_glyph("stop"), GUI_T("Stop"));
    gui_command(win, C_RECORD, GUI_T("&Record"), "F7", NULL, GUI_T("Record from a MIDI keyboard or the PC's keys"));
    gui_command(win, C_STEP, GUI_T("Step &Entry"), "F8", NULL, GUI_T("A note at the cursor for each key played"));
    {
        static const char *const lens[4] = { GUI_N("Step: a Si&xteenth"), GUI_N("Step: an E&ighth"), GUI_N("Step: a &Quarter"),
                                             GUI_N("Step: a &Half") };
        static const char *const keys[4] = { "Ctrl+1", "Ctrl+2", "Ctrl+3", "Ctrl+4" };
        int k;
        for (k = 0; k < 4; k++)
            gui_command(win, C_STEPLEN + k, GUI_T(lens[k]), keys[k], NULL, NULL);
    }
    gui_command(win, C_CLICK, GUI_T("C&lick While Recording"), NULL, NULL, GUI_T("A click on each beat while recording"));
    gui_command(win, C_COUNTIN, GUI_T("Count &In a Bar"), NULL, NULL, GUI_T("A bar of clicks before recording begins"));
    gui_command(win, C_TEMPO, GUI_T("&Tempo..."), NULL, NULL, NULL);
    gui_command(win, C_METER, GUI_T("Time Si&gnature..."), NULL, NULL, NULL);
    gui_command(win, C_ARRANGE, GUI_T("&Arrangement"), "F4", NULL, GUI_T("Every track as a row of bars, or the notes of one"));
    gui_command(win, C_LOOP, GUI_T("L&oop the Chosen Bars"), "Ctrl+R", NULL, GUI_T("Play the bars chosen in the arrangement over and over"));
    gui_command(win, C_BCOPY, GUI_T("&Copy Bars"), "Ctrl+C", NULL, GUI_T("(The arrangement) the bars chosen"));
    gui_command(win, C_BPASTE, GUI_T("Paste &Bars"), "Ctrl+V", NULL, GUI_T("(The arrangement) onto the bar chosen"));
    gui_command(win, C_ZOOMIN, GUI_T("Zoom &In"), "Ctrl++", gui_glyph("zoom-in"), GUI_T("Zoom in"));
    gui_command(win, C_ZOOMOUT, GUI_T("Zoom &Out"), "Ctrl+-", gui_glyph("zoom-out"), GUI_T("Zoom out"));
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_command(m, C_NEW), gui_menu_command(m, C_OPEN), gui_menu_command(m, C_SAVE), gui_menu_command(m, C_SAVEAS);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_command(m, C_UNDO), gui_menu_command(m, C_REDO);
    gui_menu_sep(m);
    gui_menu_command(m, C_BCOPY), gui_menu_command(m, C_BPASTE);
    gui_menu_command(m, C_DELETE), gui_menu_command(m, C_SELALL);
    gui_menu_sep(m);
    gui_menu_command(m, C_QUANT), gui_menu_command(m, C_UP), gui_menu_command(m, C_DOWN);
    gui_menu_command(m, C_OCTUP), gui_menu_command(m, C_OCTDOWN), gui_menu_command(m, C_VELOCITY);
    gui_menu_sep(m);
    gui_menu_command(m, C_STEP);
    {
        int k;
        for (k = 0; k < 4; k++)
            gui_menu_command(m, C_STEPLEN + k);
    }
    m = gui_menu(bar, GUI_T("&Track"));
    gui_menu_command(m, C_TRACK), gui_menu_command(m, C_DRUMS), gui_menu_command(m, C_UNTRACK);
    gui_menu_sep(m);
    gui_menu_command(m, C_INSTR), gui_menu_command(m, C_MUTE), gui_menu_command(m, C_SOLO);
    m = gui_menu(bar, GUI_T("Tra&nsport"));
    gui_menu_command(m, C_PLAY), gui_menu_command(m, C_STOP), gui_menu_command(m, C_RECORD), gui_menu_command(m, C_LOOP);
    gui_menu_sep(m);
    gui_menu_command(m, C_CLICK), gui_menu_command(m, C_COUNTIN);
    gui_menu_sep(m);
    gui_menu_command(m, C_TEMPO), gui_menu_command(m, C_METER);
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_command(m, C_ARRANGE);
    gui_menu_sep(m);
    gui_menu_command(m, C_ZOOMIN), gui_menu_command(m, C_ZOOMOUT);
    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_NEW), gui_toolbar_command(tb, C_OPEN), gui_toolbar_command(tb, C_SAVE);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_PLAY), gui_toolbar_command(tb, C_STOP), gui_toolbar_command(tb, C_RECORD);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_UNDO), gui_toolbar_command(tb, C_REDO), gui_toolbar_command(tb, C_DELETE);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_TRACK), gui_toolbar_command(tb, C_ZOOMIN), gui_toolbar_command(tb, C_ZOOMOUT);

    history = gui_undo_new(0);
    gui_undo_commands(history, win, C_UNDO, C_REDO);
    gui_undo_watch(history, history_changed, NULL);
    row = gui_hbox(win->root, 6, 4);
    gui_label(row, GUI_T("Bar and beat:"));
    clock = gui_label(row, "001.01");
    gui_label_style(clock, gui_bold, GFX_NONE, -1);
    for (i = 0; i < 4; i++)
        h[i] = GUI_T(heads[i]);
    tracks = gui_list(win->root, 4, h, widths);
    gui_size(tracks, 0, 96);
    gui_on(tracks, tracks_event, 0);
    roll = gui_canvas_view(win->root, 32 * 4 * (int)SONG_PPQ, 128 * ROW * zoom, roll_draw, roll_event);
    gui_cview_style(roll, GUI_CV_RING, 0);
    gui_grow(roll, 1);
    status = gui_status(win);
}

int main(int argc, char **argv)
{
    const char *file = NULL;
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("STUDIO no GLOS\n");
        return 1;
    }
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-demo"))
            demo = 1;
        else if (argv[i][0] != '-')
            file = argv[i];
    if (!(song = song_new())) {
        printf("STUDIO no memory\n");
        return 1;
    }
    if (!demo && (!(lock = glos_lock()) || !(thread = glos_thread(feeder, NULL, 64 * 1024, 0))))
        say("refused thread");
    build();
    show_all();
    gui_cview_scroll(roll, 0, (127 - 72) * ROW * zoom);         /* (the roll's top at the C above the middle one) */
    gui_show(win, 1);
    if (file)
        open_file(file);
    gui_focus(roll);
    gui_on_event(rec_event);                    /* (a MIDI keyboard's messages) */
    gui_idle(tick, NULL, 50);
    gui_run();
    rec_stop();
    stop_playing();
    if (thread) {
        take();
        quit = 1;
        give();
        glos_join(thread, 2000, NULL);
    }
    printf("STUDIO end\n");
    return 0;
}
