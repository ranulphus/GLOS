/* PLAYER: the music player (GLOS v1, phase F; docs/v1/apps/audio-player.md): its first part. A list of files, one
 * of them playing through a stream of its own (engine.c: a thread feeds it, so that the music goes on whatever
 * the window does); WAV files and MOD modules (apps/shared/source.c).
 *
 *   the list      a row a file: its title (the song's own name, where the file has one), its length, its kind;
 *                 Enter or a double click plays the row; Del removes it; the one playing has the play mark
 *   Play (F5)     the chosen row, or what was playing; again: pause, and on. Stop (F6); Previous and Next
 *                 (Ctrl+Left, Ctrl+Right); when a file ends the next begins, the first again after the last if
 *                 Repeat is on, any other one if Shuffle is
 *   the slider    where it is in the file: moved, it goes there
 *   Volume        its own stream's, as the Mixer shows it
 *   File          Add Files..., Add Folder... (every file of it that is played here), Open Playlist... and
 *                 Save Playlist... (M3U: a path a line), Remove, Clear
 *
 * PLAYER FILE... adds the files (a playlist's, for an .M3U) and plays the first (Files' "open").
 *
 * PLAYER -demo: no card is asked for and no time passes by itself: F9 is ten seconds heard (the GUI scripts'
 * pictures are the same on every PC and on the host).
 *
 * "PLAYER ..." lines on stdout (the suite's). */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "../shared/wave.h"
#include "engine.h"

enum { C_ADD = 1, C_ADDDIR, C_OPENLIST, C_SAVELIST, C_REMOVE, C_CLEAR, C_EXIT, C_PLAY, C_STOP, C_PREV, C_NEXT, C_REPEAT,
       C_SHUFFLE };

struct item {
    char path[260];
    char title[64];                     /* what the list shows */
    char kind[8], detail[40];
    unsigned long seconds;
    unsigned rate;
};

static struct gui_window *win;
static struct gui_widget *list, *title, *detail, *times, *seek, *volume, *status;
static struct item *items;
static int nitems, room;
static int current = -1;                /* the row that is open in the engine (-1: none) */
static int repeat, shuffle, demo, setting;
static int loud = 100;                  /* the volume slider's number (kept in PLAYER.CFG) */
static unsigned long seed = 20261120ul;
static char home[260];

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("PLAYER ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

static void clock_text(unsigned long seconds, char *out, size_t n)
{
    snprintf(out, n, "%lu:%02lu", seconds / 60, seconds % 60);
}

/* ---- the list */

static void show_list(int choose)
{
    static const unsigned char *mark;
    const char *cells[3];
    char len[16], t[80];
    unsigned long all = 0;
    int i;
    if (!mark)
        mark = gui_glyph("play");
    gui_list_clear(list);
    for (i = 0; i < nitems; i++) {
        clock_text(items[i].seconds, len, sizeof len);
        cells[0] = items[i].title, cells[1] = len, cells[2] = items[i].kind;
        gui_list_add(list, cells, i == current ? mark : NULL, NULL);
        all += items[i].seconds;
    }
    if (choose >= 0 && choose < nitems)
        gui_list_set(list, choose);
    clock_text(all, len, sizeof len);
    snprintf(t, sizeof t, nitems == 1 ? GUI_T("%d file, %s") : GUI_T("%d files, %s"), nitems, len);
    gui_set_text(status, nitems ? t : GUI_T("No files yet. File, Add Files..."));
}

static int playable(const char *name)   /* by its name's end (a folder's files; the dialog's filter) */
{
    const char *dot = strrchr(name, '.');
    static const char *const ends[] = { ".WAV", ".MOD", ".MP3", ".MP2", ".OGG", NULL };
    int i;
    for (i = 0; dot && ends[i]; i++) {
        const char *a = dot, *b = ends[i];
        while (*a && *b && toupper((unsigned char)*a) == *b)
            a++, b++;
        if (!*a && !*b)
            return 1;
    }
    return 0;
}

/* A file onto the list's end: 0, or why not (a SOURCE_E_). */
static int add(const char *path)
{
    struct source *s;
    const struct source_info *in;
    struct item *it;
    int err;
    if (!(s = source_open(path, 44100, &err))) {
        say("refused %s %d", gui_path_name(path), err);
        return err;
    }
    if (nitems == room) {
        struct item *m = realloc(items, (size_t)(room ? room * 2 : 32) * sizeof *m);
        if (!m) {
            source_close(s);
            return SOURCE_E_MEMORY;
        }
        items = m, room = room ? room * 2 : 32;
    }
    in = source_info(s);
    it = &items[nitems++];
    memset(it, 0, sizeof *it);
    snprintf(it->path, sizeof it->path, "%s", path);
    snprintf(it->title, sizeof it->title, "%s", in->title[0] ? in->title : gui_path_name(path));
    snprintf(it->kind, sizeof it->kind, "%s", in->kind);
    snprintf(it->detail, sizeof it->detail, "%s", in->detail);
    it->seconds = in->rate ? in->frames / in->rate : 0;
    it->rate = in->rate;
    {
        char len[16];
        clock_text(it->seconds, len, sizeof len);
        say("added %s %s %s", gui_path_name(path), it->kind, len);
    }
    source_close(s);
    return SOURCE_OK;
}

static void add_said(const char *path)  /* ... and the user told if it could not be */
{
    char t[300];
    int err = add(path);
    if (err != SOURCE_OK) {
        snprintf(t, sizeof t, GUI_T("%s can't be played: %s."), gui_path_name(path), GUI_T(source_error(err)));
        gui_message(win, GUI_T("Player"), t, GUI_MB_OK);
    }
}

/* ---- playing */

static void show_now(void)
{
    char t[120];
    if (current < 0) {
        gui_set_text(title, GUI_T("Nothing is playing"));
        gui_set_text(detail, "");
    } else {
        gui_set_text(title, items[current].title);
        snprintf(t, sizeof t, GUI_T("%s, %s, %u Hz"), items[current].kind, items[current].detail, items[current].rate);
        gui_set_text(detail, t);
    }
}

static void stop(void)
{
    eng_stop();
    say("stopped");
}

static int play_row(int i)              /* 1 if it plays */
{
    struct source_info info;
    char t[300];
    int err;
    if (i < 0 || i >= nitems)
        return 0;
    if ((err = eng_open(items[i].path, &info)) != SOURCE_OK) {
        current = -1;
        show_list(i);
        show_now();
        say("refused %s %d", gui_path_name(items[i].path), err);
        snprintf(t, sizeof t, GUI_T("%s can't be played: %s."), gui_path_name(items[i].path), GUI_T(source_error(err)));
        gui_message(win, GUI_T("Player"), t, GUI_MB_OK);
        return 0;
    }
    items[i].rate = info.rate;                  /* (a module's: the mixer's, which the engine asked) */
    current = i;
    show_list(i);
    show_now();
    if (eng_play() != 0) {
        say("refused card");
        gui_message(win, GUI_T("Player"), GUI_T("There is no sound card to play through."), GUI_MB_OK);
        return 0;
    }
    say("playing %d %s", i + 1, gui_path_name(items[i].path));
    return 1;
}

static void next(int by_itself)         /* the row after the one that plays (by itself: its file ended) */
{
    int i;
    if (!nitems)
        return;
    if (shuffle && nitems > 1) {
        do {
            seed = seed * 1103515245ul + 12345ul;
            i = (int)((seed >> 16) % (unsigned)nitems);
        } while (i == current);
    } else {
        i = current + 1;
        if (i >= nitems) {
            if (by_itself && !repeat) {         /* the list is through */
                say("through");
                return;
            }
            i = 0;
        }
    }
    play_row(i);
}

static void tick(void *arg)
{
    struct eng_now n;
    char a[16], b[16], t[40];
    (void)arg;
    eng_now(&n);
    if (n.slow && current >= 0) {               /* stopped by the engine: said, the list not gone on with */
        say("slow %s", gui_path_name(items[current].path));
        gui_message(win, GUI_T("Player"), GUI_T("This PC can't unpack this file as fast as it plays. MP3 and Ogg Vorbis "
                    "files want a Pentium."), GUI_MB_OK);
        eng_now(&n);
    }
    if (n.ended && current >= 0) {
        say("ended %s", gui_path_name(items[current].path));
        next(1);
        eng_now(&n);
    }
    clock_text(n.rate ? n.at / n.rate : 0, a, sizeof a);
    clock_text(n.rate ? n.frames / n.rate : 0, b, sizeof b);
    snprintf(t, sizeof t, "%s / %s", a, b);
    if (strcmp(t, gui_get_text(times)) != 0)
        gui_set_text(times, t);
    if (win->capture != seek) {                 /* (not under the hand that moves it) */
        int v = n.frames ? (int)((unsigned long long)n.at * 1000 / n.frames) : 0;
        if (v != gui_value(seek)) {
            setting = 1;
            gui_set_value(seek, v);
            setting = 0;
        }
    }
}

static void seek_moved(struct gui_widget *w, int what)
{
    struct eng_now n;
    if (what != GUI_ON_CHANGE || setting)
        return;
    eng_now(&n);
    if (n.frames) {
        eng_seek((unsigned long)((unsigned long long)n.frames * (unsigned)gui_value(w) / 1000));
        say("sought %d", gui_value(w));
    }
}

static void volume_moved(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CHANGE && !setting) {
        loud = gui_value(w);
        eng_volume(loud * 256 / 100);
        say("volume %d", loud);
    }
}

static void list_event(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE)
        play_row(gui_list_get(w));
}

/* ---- playlists: M3U, a path a line ("#" lines are notes); a path with no drive is beside the list's file */

static void list_open(const char *path)
{
    FILE *f = gui_fopen(path, "r");
    char line[300], full[300], dir[260];
    char *slash;
    int n = 0, bad = 0;
    if (!f) {
        gui_message(win, GUI_T("Player"), GUI_T("The playlist couldn't be read."), GUI_MB_OK);
        return;
    }
    snprintf(dir, sizeof dir, "%s", path);
    if ((slash = strrchr(dir, '\\')) != NULL)
        *slash = 0;
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        while (len && (line[len - 1] == '\n' || line[len - 1] == '\r' || line[len - 1] == ' '))
            line[--len] = 0;
        if (!len || line[0] == '#')
            continue;
        if (line[1] == ':' || line[0] == '\\')
            snprintf(full, sizeof full, "%s", line);
        else
            gui_path_join(full, sizeof full, dir, line);
        if (add(full) == SOURCE_OK)
            n++;
        else
            bad++;
    }
    fclose(f);
    say("list opened %d %d", n, bad);
    show_list(nitems - n);
    if (bad) {
        snprintf(line, sizeof line, GUI_T("%d of the playlist's files can't be played."), bad);
        gui_message(win, GUI_T("Player"), line, GUI_MB_OK);
    }
}

static void list_save(const char *path)
{
    FILE *f = gui_fopen(path, "wb");             /* (DOS's line ends, written as they are) */
    int i, ok = f != NULL;
    for (i = 0; ok && i < nitems; i++)
        ok = fprintf(f, "%s\r\n", items[i].path) > 0;
    if (f && fclose(f) != 0)
        ok = 0;
    if (!ok)
        gui_message(win, GUI_T("Player"), GUI_T("The playlist couldn't be written."), GUI_MB_OK);
    else
        say("list saved %s %d", gui_path_name(path), nitems);
}

static void settings_keep(void)
{
    char p[300];
    FILE *f;
    gui_path_join(p, sizeof p, home, "PLAYER.CFG");
    if (home[0] && (f = gui_fopen(p, "w")) != NULL) {  /* (no folder known, as on the host: nothing is kept) */
        fprintf(f, "volume %d\nrepeat %d\nshuffle %d\n", loud, repeat, shuffle);
        fclose(f);
    }
}

static void settings_read(void)
{
    char p[300], line[80];
    FILE *f;
    int v;
    gui_path_join(p, sizeof p, home, "PLAYER.CFG");
    if (!home[0] || !(f = gui_fopen(p, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "volume %d", &v) == 1 && v >= 0 && v <= 100) {
            loud = v;
            setting = 1;
            gui_set_value(volume, v);
            setting = 0;
            eng_volume(v * 256 / 100);
        } else if (sscanf(line, "repeat %d", &v) == 1) {
            repeat = v != 0;
        } else if (sscanf(line, "shuffle %d", &v) == 1) {
            shuffle = v != 0;
        }
    }
    fclose(f);
}

/* ---- commands */

static const char *const sounds[] = { "Music and sounds|*.WAV;*.MOD;*.MP3;*.MP2;*.OGG", "All files|*.*", NULL };
static const char *const lists[] = { "Playlists|*.M3U", "All files|*.*", NULL };

static void command(struct gui_window *w, int id)
{
    struct gui_file_opts o;
    struct gui_dirent *e;
    struct eng_now n;
    char p[300], full[300];
    int i, k, was = nitems;
    (void)w;
    memset(&o, 0, sizeof o);
    p[0] = 0;
    switch (id) {
    case C_ADD:
        o.filters = sounds;
        if (gui_file_dialog_ex(win, GUI_T("Add Files"), 0, p, sizeof p, &o)) {
            add_said(p);
            show_list(nitems - 1);
        }
        break;
    case C_ADDDIR:
        if (gui_folder_dialog(win, GUI_T("Add Folder"), p, sizeof p)) {
            k = gui_dir_read(p, &e);
            for (i = 0; i < k; i++)
                if (!e[i].dir && playable(e[i].name)) {
                    gui_path_join(full, sizeof full, p, e[i].name);
                    add(full);
                }
            if (k >= 0)
                free(e);
            say("folder %d", nitems - was);
            show_list(was < nitems ? was : nitems - 1);
        }
        break;
    case C_OPENLIST:
        o.filters = lists;
        if (gui_file_dialog_ex(win, GUI_T("Open Playlist"), 0, p, sizeof p, &o))
            list_open(p);
        break;
    case C_SAVELIST:
        o.filters = lists;
        if (nitems && gui_file_dialog_ex(win, GUI_T("Save Playlist"), 1, p, sizeof p, &o)) {
            if (!strchr(gui_path_name(p), '.') && strlen(p) + 5 < sizeof p)
                strcat(p, ".M3U");
            list_save(p);
        }
        break;
    case C_REMOVE:
        i = gui_list_get(list);
        if (i < 0 || i >= nitems)
            break;
        if (i == current) {
            eng_close();
            current = -1;
            show_now();
        } else if (i < current) {
            current--;
        }
        memmove(&items[i], &items[i + 1], (size_t)(nitems - i - 1) * sizeof *items);
        nitems--;
        say("removed %d", i + 1);
        show_list(i < nitems ? i : nitems - 1);
        break;
    case C_CLEAR:
        eng_close();
        current = -1, nitems = 0;
        say("cleared");
        show_list(-1);
        show_now();
        break;
    case C_EXIT:
        gui_close_later(win);
        break;
    case C_PLAY:
        eng_now(&n);
        if (n.state == ENG_PLAYING || n.state == ENG_PAUSED) {
            eng_pause();
            say(n.state == ENG_PLAYING ? "paused" : "on");
        } else {
            i = gui_list_get(list);
            play_row(i >= 0 ? i : current >= 0 ? current : 0);
        }
        break;
    case C_STOP:
        stop();
        break;
    case C_PREV:
        if (nitems)
            play_row(current > 0 ? current - 1 : nitems - 1);
        break;
    case C_NEXT:
        next(0);
        break;
    case C_REPEAT:
        repeat = !repeat;
        say("repeat %d", repeat);
        break;
    case C_SHUFFLE:
        shuffle = !shuffle;
        say("shuffle %d", shuffle);
        break;
    }
    tick(NULL);
}

static void update(struct gui_window *w)
{
    struct eng_now n;
    eng_now(&n);
    gui_command(w, C_PLAY, n.state == ENG_PLAYING ? GUI_T("&Pause") : GUI_T("&Play"), "F5",
                gui_glyph(n.state == ENG_PLAYING ? "pause" : "play"), n.state == ENG_PLAYING ? GUI_T("Pause") : GUI_T("Play"));
    gui_command_enable(w, C_PLAY, nitems > 0);
    gui_command_enable(w, C_STOP, n.state != ENG_STOPPED);
    gui_command_enable(w, C_PREV, nitems > 0);
    gui_command_enable(w, C_NEXT, nitems > 0);
    gui_command_enable(w, C_REMOVE, gui_list_get(list) >= 0);
    gui_command_enable(w, C_CLEAR, nitems > 0);
    gui_command_enable(w, C_SAVELIST, nitems > 0);
    gui_command_check(w, C_REPEAT, repeat);
    gui_command_check(w, C_SHUFFLE, shuffle);
}

static int key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    if (demo && e->key == 0x43) {                       /* F9: ten seconds heard */
        struct eng_now n;
        eng_now(&n);
        eng_step(n.rate * 10ul);
        tick(NULL);
        return 1;
    }
    return 0;
}

static void build(void)
{
    static const char *const heads[] = { GUI_N("Title"), GUI_N("Length"), GUI_N("Kind") };
    static const int widths[] = { 250, 60, 50 };
    const char *h[3];
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *box, *row, *tb, *l;
    int i;
    win = gui_window(GUI_T("Player"), 420, 300, 0);
    win->on_command = command;
    win->on_update = update;
    win->on_key = key;
    gui_command(win, C_ADD, GUI_T("&Add Files..."), "Ctrl+O", gui_glyph("add"), GUI_T("Add files to the list"));
    gui_command(win, C_ADDDIR, GUI_T("Add &Folder..."), NULL, gui_glyph("folder"), GUI_T("Add a folder's music"));
    gui_command(win, C_OPENLIST, GUI_T("&Open Playlist..."), NULL, NULL, NULL);
    gui_command(win, C_SAVELIST, GUI_T("&Save Playlist..."), "Ctrl+S", NULL, NULL);
    gui_command(win, C_REMOVE, GUI_T("&Remove"), "Del", gui_glyph("remove"), GUI_T("Remove from the list"));
    gui_command(win, C_CLEAR, GUI_T("&Clear"), NULL, NULL, NULL);
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    gui_command(win, C_PLAY, GUI_T("&Play"), "F5", gui_glyph("play"), GUI_T("Play"));
    gui_command(win, C_STOP, GUI_T("&Stop"), "F6", gui_glyph("stop"), GUI_T("Stop"));
    gui_command(win, C_PREV, GUI_T("Pre&vious"), "Ctrl+Left", gui_glyph("back"), GUI_T("Previous"));
    gui_command(win, C_NEXT, GUI_T("&Next"), "Ctrl+Right", gui_glyph("forward"), GUI_T("Next"));
    gui_command(win, C_REPEAT, GUI_T("&Repeat"), NULL, NULL, NULL);
    gui_command(win, C_SHUFFLE, GUI_T("S&huffle"), NULL, NULL, NULL);
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_command(m, C_ADD), gui_menu_command(m, C_ADDDIR);
    gui_menu_sep(m);
    gui_menu_command(m, C_OPENLIST), gui_menu_command(m, C_SAVELIST);
    gui_menu_sep(m);
    gui_menu_command(m, C_REMOVE), gui_menu_command(m, C_CLEAR);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Playback"));
    gui_menu_command(m, C_PLAY), gui_menu_command(m, C_STOP), gui_menu_command(m, C_PREV), gui_menu_command(m, C_NEXT);
    gui_menu_sep(m);
    gui_menu_command(m, C_REPEAT), gui_menu_command(m, C_SHUFFLE);
    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_PREV), gui_toolbar_command(tb, C_PLAY), gui_toolbar_command(tb, C_STOP), gui_toolbar_command(tb, C_NEXT);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_ADD), gui_toolbar_command(tb, C_ADDDIR), gui_toolbar_command(tb, C_REMOVE);

    box = gui_vbox(win->root, 2, 6);
    title = gui_label(box, GUI_T("Nothing is playing"));
    gui_label_style(title, gui_bold, GFX_NONE, -1);
    detail = gui_label(box, "");
    row = gui_hbox(box, 6, 0);
    times = gui_label(row, "0:00 / 0:00");
    gui_size(times, 84, 0);
    seek = gui_slider(row, 0, 1000, 0);
    gui_grow(seek, 1);
    gui_on(seek, seek_moved, 0);
    gui_tip(seek, GUI_T("Where it is in the file"));
    l = gui_label(row, GUI_T("Volume:"));
    (void)l;
    volume = gui_slider(row, 0, 100, 100);
    gui_size(volume, 70, 0);
    gui_on(volume, volume_moved, 0);
    for (i = 0; i < 3; i++)
        h[i] = GUI_T(heads[i]);
    list = gui_list(win->root, 3, h, widths);
    gui_grow(list, 1);
    gui_on(list, list_event, 0);
    status = gui_status(win);
}

int main(int argc, char **argv)
{
    char *q;
    int i, first = -1;
    if (gui_init(argc, argv) != 0) {
        printf("PLAYER no GLOS\n");
        return 1;
    }
    snprintf(home, sizeof home, "%s", argv[0]);         /* its own folder, as DOS writes it: PLAYER.CFG's place */
    for (q = home; *q; q++)
        *q = *q == '/' ? '\\' : (*q >= 'a' && *q <= 'z') ? (char)(*q - 32) : *q;
    if ((q = strrchr(home, '\\')) != NULL)
        *q = 0;
    else
        home[0] = 0;
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-demo"))
            demo = 1;
    source_files(gui_fopen);
    wave_files(gui_fopen, gui_unlink);
    if (eng_begin(demo) != 0)
        say("refused thread");
    build();
    settings_read();
    for (i = 1; i < argc; i++) {
        const char *dot = strrchr(argv[i], '.');
        int was = nitems;
        if (argv[i][0] == '-')
            continue;
        if (dot && (!strcmp(dot, ".M3U") || !strcmp(dot, ".m3u")))
            list_open(argv[i]);                 /* a playlist: its files */
        else
            add(argv[i]);
        if (nitems > was && first < 0)
            first = was;
    }
    show_list(first >= 0 ? first : 0);
    show_now();
    gui_show(win, 1);
    gui_focus(list);
    if (first >= 0)
        play_row(first);
    gui_idle(tick, NULL, 100);
    gui_run();
    settings_keep();
    eng_end();
    printf("PLAYER end\n");
    return 0;
}
