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
       C_MUTE, C_SOLO, C_PLAY, C_STOP, C_TEMPO, C_ZOOMIN, C_ZOOMOUT };

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
static long line = -1;                  /* the tick the playing is at (-1: it isn't) */

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

static void size_roll(void)             /* the roll's document: the song and eight bars, 32 at least; the keys */
{
    unsigned long bar = SONG_PPQ * 4ul / (song->unit ? song->unit : 4) * (song->beats ? song->beats : 4);
    unsigned long w = song_ticks(song) + 8 * bar;
    if (w < 32 * bar)
        w = 32 * bar;
    if (w > 0x3FFFFFFul)
        w = 0x3FFFFFFul;
    gui_cview_size(roll, (int)w, 128 * ROW * zoom);
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

static void roll_draw(struct gui_widget *w, struct gfx_surface *s, const struct gui_viewport *v)
{
    const struct song_track *t = &song->track[cur];
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
        gfx_fill(s, x0, y, x1 - x0, ROW - 1, GUI_ROLE(n->sel ? GLOS_ROLE_CHOSEN : GLOS_ROLE_MARK));
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

static int roll_event(struct gui_widget *w, struct gui_ev *e, int x, int y)
{
    struct song_track *t = &song->track[cur];
    int key = 127 - y / (ROW * zoom);
    unsigned long i;
    long hit;
    switch (e->type) {
    case GUI_EV_PRESS:
        if (drag || x < 0 || y < 0 || key < 0)
            return 1;
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

static void tick(void *arg)             /* the line along the roll; the end seen */
{
    unsigned long ms;
    int ended;
    (void)arg;
    if (!heard)
        return;
    take();
    ms = demo ? demo_ms : mp_where(&mp);
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

static struct gui_widget *dlg_list, *dlg_spin;
static int dlg_value;

static void dlg_done(struct gui_widget *b, int id)
{
    if (id == 1)                                /* (read while the dialog is there: gui_modal closes it) */
        dlg_value = dlg_list ? gui_list_get(dlg_list) : gui_value(dlg_spin);
    gui_end_modal(b->win, id == 1 ? GUI_OK : GUI_CANCEL_R);
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
    if (gui_modal(d) == GUI_OK && dlg_value >= 0 && dlg_value < 128) {
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
    dlg_list = NULL;
    dlg_spin = gui_spin(gui_row(form, GUI_T("&Quarter notes a minute:"), NULL), 20, 400, (int)song_tempo_at(song, 0));
    dlg_buttons(d, box);
    gui_focus(dlg_spin);
    dlg_value = 0;
    if (gui_modal(d) == GUI_OK && dlg_value >= 20 && dlg_value <= 400) {
        song_tempo_set(song, 0, (unsigned)dlg_value);
        changed = 1;
        say("tempo %u", song_tempo_at(song, 0));
        show_status();
    }
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
    case C_DELETE:
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
        play();
        break;
    case C_STOP:
        if (heard) {
            stop_playing();
            say("stopped");
        }
        break;
    case C_TEMPO:
        pick_tempo();
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
    gui_command_enable(w, C_DELETE, chosen > 0);
    gui_command_enable(w, C_SELALL, t->nnotes > 0);
    gui_command_enable(w, C_UNTRACK, song->ntracks > 1);
    gui_command_enable(w, C_STOP, heard != NULL);
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
    if (demo && e->key == 0x43) {               /* F9: a second heard */
        take();
        if (playing)
            demo_ms += 1000;
        give();
        tick(NULL);
        return 1;
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
    gui_command(win, C_TRACK, GUI_T("&New Track"), "Ctrl+T", gui_glyph("add"), GUI_T("A new track"));
    gui_command(win, C_DRUMS, GUI_T("New &Drum Track"), NULL, NULL, NULL);
    gui_command(win, C_UNTRACK, GUI_T("&Remove Track"), NULL, NULL, NULL);
    gui_command(win, C_INSTR, GUI_T("&Instrument..."), "Ctrl+I", NULL, NULL);
    gui_command(win, C_MUTE, GUI_T("&Mute"), "Ctrl+M", NULL, NULL);
    gui_command(win, C_SOLO, GUI_T("&Solo"), "Ctrl+L", NULL, NULL);
    gui_command(win, C_PLAY, GUI_T("&Play"), "F5", gui_glyph("play"), GUI_T("Play from the start"));
    gui_command(win, C_STOP, GUI_T("&Stop"), "F6", gui_glyph("stop"), GUI_T("Stop"));
    gui_command(win, C_TEMPO, GUI_T("&Tempo..."), NULL, NULL, NULL);
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
    gui_menu_command(m, C_DELETE), gui_menu_command(m, C_SELALL);
    m = gui_menu(bar, GUI_T("&Track"));
    gui_menu_command(m, C_TRACK), gui_menu_command(m, C_DRUMS), gui_menu_command(m, C_UNTRACK);
    gui_menu_sep(m);
    gui_menu_command(m, C_INSTR), gui_menu_command(m, C_MUTE), gui_menu_command(m, C_SOLO);
    m = gui_menu(bar, GUI_T("Tra&nsport"));
    gui_menu_command(m, C_PLAY), gui_menu_command(m, C_STOP);
    gui_menu_sep(m);
    gui_menu_command(m, C_TEMPO);
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_command(m, C_ZOOMIN), gui_menu_command(m, C_ZOOMOUT);
    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_NEW), gui_toolbar_command(tb, C_OPEN), gui_toolbar_command(tb, C_SAVE);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_PLAY), gui_toolbar_command(tb, C_STOP);
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
    gui_idle(tick, NULL, 50);
    gui_run();
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
