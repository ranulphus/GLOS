/* GAMES: the Games Library (GLOS v1, phase F; docs/v1/apps/games-library.md): its first part. The DOS games on
 * this PC: found in the folders of games (C:\GAMES to begin with), known by their files where they are well
 * known (apps/shared/gamedb.c), shown with their pictures, and played with the time counted.
 *
 *   the games     a grid of pictures (or a list): Enter or a double click plays; typing in Find narrows them
 *                 (titles, makers, kinds); sorted by title, by when last played, or by the time played;
 *                 favourites alone
 *   a game        its page at the right: what is known of it, how long it was played and when, the user's
 *                 notes; Play, and Set Up where it has a set-up program
 *   File          Add Game... (any program, by hand), Add Folder... (another folder of games), Scan Now (F5)
 *   Game          Favourite (Ctrl+D), Properties... (its title, its picture, notes; and how its program runs: the
 *                 whole screen or a window, its memory, the Sound Blaster it is told of, direct mode: GLOS.CFG's
 *                 [program NAME.EXE]), Remove (from the library:
 *                 nothing on the disk is touched)
 *
 * The library is GAMES.CFG in GLOS's folder (apps/shared/gamelib.c; GAMES -library FILE: another). A game runs
 * as any program of DOS's does under GLOS, by its profile: full-screen when it wants the screen.
 *
 * "GAMES ..." lines on stdout (the suite's). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/gui.h>

#include "../shared/gamelib.h"
#include "../shared/thumbs.h"

enum { C_PLAY = 1, C_SETUP, C_ADD, C_ADDFOLDER, C_SCAN, C_EXIT, C_FAV, C_PROPS, C_REMOVE, C_PICS, C_LIST, C_BY_TITLE, C_BY_LAST,
       C_BY_TIME, C_ONLY_FAV, C_FIND };

static struct gui_window *win;
static struct gui_widget *list, *search, *status, *d_title, *d_about, *d_where, *d_played, *d_notes, *b_play, *b_setup;
static struct gamelib lib;
static char lib_path[300];
static unsigned long *order;            /* the games shown, in their order: ids */
static unsigned norder;
static int sort_by, only_fav, as_list;
static char finding[64];
static glos_handle job;                 /* the game (or its set-up program) that is running */
static unsigned long playing;
static int playing_setup;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("GAMES ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- the games' pictures: a small one each (apps/shared/thumbs.c makes and keeps them), as they come to be shown */

static struct picture {
    unsigned long id;
    char path[128];
    unsigned *px;                       /* NULL: none could be made */
    int w, h;
} pictures[48];
static unsigned npictures, next_picture;

static const unsigned *picture_of(const struct game *g, int *w, int *h)
{
    struct picture *p;
    struct gui_dirent *e = NULL;
    char dir[128];
    const char *name = gui_path_name(g->picture);
    unsigned i;
    int n, made = 0;
    if (!g->picture[0])
        return NULL;
    for (i = 0; i < npictures; i++)
        if (pictures[i].id == g->id && !strcmp(pictures[i].path, g->picture)) {
            *w = pictures[i].w, *h = pictures[i].h;
            return pictures[i].px;
        }
    p = npictures < 48 ? &pictures[npictures++] : &pictures[next_picture++ % 48];      /* (the oldest gives way) */
    free(p->px);
    memset(p, 0, sizeof *p);
    p->id = g->id;
    snprintf(p->path, sizeof p->path, "%s", g->picture);
    snprintf(dir, sizeof dir, "%.*s", (int)(name - g->picture), g->picture);
    if ((n = gui_dir_read(dir, &e)) > 0)
        for (i = 0; i < (unsigned)n; i++)
            if (!e[i].dir && !strcmp(e[i].name, name))
                p->px = thumb_make(g->picture, &e[i], &p->w, &p->h, &made);
    free(e);
    *w = p->w, *h = p->h;
    return p->px;
}

static void pictures_forget(unsigned long id)
{
    unsigned i;
    for (i = 0; i < npictures; i++)
        if (pictures[i].id == id)
            pictures[i].path[0] = 0;            /* (asked for again when it is next shown) */
}

/* ---- the list's rows: the games in their order */

static void played_text(const struct game *g, char *out, size_t room)
{
    unsigned long m = g->played / 60;
    if (!g->times)
        snprintf(out, room, "%s", GUI_T("Never played"));
    else if (m >= 60)
        snprintf(out, room, GUI_T("%lu h %lu min"), m / 60, m % 60);
    else
        snprintf(out, room, GUI_T("%lu min"), m);
}

static int m_count(void *ctx) { (void)ctx; return (int)norder; }

static void m_row(void *ctx, int row, struct gui_vrow *out)
{
    static char year[8], time[40];
    struct game *g = row >= 0 && (unsigned)row < norder ? gamelib_find(&lib, order[row]) : NULL;
    (void)ctx;
    memset(out, 0, sizeof *out);
    if (!g)
        return;
    snprintf(year, sizeof year, g->year ? "%u" : "", (unsigned)g->year);
    played_text(g, time, sizeof time);
    out->cell[0] = g->title, out->cell[1] = year, out->cell[2] = g->maker, out->cell[3] = g->genre, out->cell[4] = time;
    out->icon = gui_icon(g->favourite ? "ok" : "app-games", 0);
    out->big = gui_icon("app-games", 1);
    out->id = g->id;
    out->thumb = picture_of(g, &out->thumb_w, &out->thumb_h);
}

static int m_find(void *ctx, unsigned long id)
{
    unsigned i;
    (void)ctx;
    for (i = 0; i < norder; i++)
        if (order[i] == id)
            return (int)i;
    return -1;
}

static int m_match(void *ctx, const char *typed, int from)
{
    size_t n = strlen(typed);
    unsigned i, k;
    (void)ctx;
    for (i = 0; i < norder; i++) {
        const struct game *g = gamelib_find(&lib, order[(i + (unsigned)from) % norder]);
        for (k = 0; g && k < n; k++) {
            int a = g->title[k], b = typed[k];
            a = a >= 'a' && a <= 'z' ? a - 32 : a, b = b >= 'a' && b <= 'z' ? b - 32 : b;
            if (!a || a != b)
                break;
        }
        if (g && k == n)
            return (int)((i + (unsigned)from) % norder);
    }
    return -1;
}

static const struct gui_model model = { m_count, m_row, m_find, m_match, NULL };

static struct game *current(void)
{
    unsigned long id = gui_vlist_id(list);
    return id ? gamelib_find(&lib, id) : NULL;
}

/* ---- a game's page */

static void date_text(unsigned long when, char *out, size_t room)      /* seconds since 1970, local: its day */
{
    unsigned long days = when / 86400, y = 1970, m;
    static const unsigned char len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    for (;; y++) {
        unsigned long in = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        if (days < in)
            break;
        days -= in;
    }
    for (m = 0; m < 12; m++) {
        unsigned long in = len[m] + (m == 1 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1u : 0u);
        if (days < in)
            break;
        days -= in;
    }
    snprintf(out, room, "%lu-%02lu-%02lu", y, m + 1, days + 1);
}

static void show_page(void)
{
    struct game *g = current();
    char t[300], a[60], b[40];
    if (!g) {
        gui_set_text(d_title, lib.n ? GUI_T("No game is chosen.") : GUI_T("No games yet."));
        gui_set_text(d_about, lib.n ? "" : GUI_T("File, Add Folder... or Add Game..."));
        gui_set_text(d_where, ""), gui_set_text(d_played, ""), gui_set_text(d_notes, "");
    } else {
        gui_set_text(d_title, g->title);
        snprintf(a, sizeof a, g->year ? "%u" : "", (unsigned)g->year);
        snprintf(t, sizeof t, "%s%s%s%s%s", a, a[0] && g->maker[0] ? ", " : "", g->maker, (a[0] || g->maker[0]) && g->genre[0] ? ", " : "",
                 g->genre);
        gui_set_text(d_about, t);
        gui_path_join(t, sizeof t, g->dir, g->run);
        gui_set_text(d_where, t);
        played_text(g, a, sizeof a);
        if (g->gone) {
            snprintf(t, sizeof t, "%s", GUI_T("Its program is no longer there."));
        } else if (g->times) {
            date_text(g->last, b, sizeof b);
            if (g->times != 1)
                snprintf(t, sizeof t, GUI_T("%s, %u times, last on %s"), a, g->times, b);
            else
                snprintf(t, sizeof t, GUI_T("%s, once, on %s"), a, b);
        } else {
            snprintf(t, sizeof t, "%s", a);
        }
        gui_set_text(d_played, t);
        gui_set_text(d_notes, g->notes);
    }
    gui_enable(b_play, g && !g->gone && !job);
    gui_enable(b_setup, g && g->setup[0] && !g->gone && !job);
}

static void show_status(void)
{
    char t[120];
    if (norder == lib.n)
        snprintf(t, sizeof t, lib.n == 1 ? GUI_T("%u game") : GUI_T("%u games"), lib.n);
    else
        snprintf(t, sizeof t, GUI_T("%u of %u games"), norder, lib.n);
    gui_set_text(status, t);
}

static void reorder(void)               /* the games shown, anew: what is looked for, the order, favourites */
{
    unsigned long *o = realloc(order, (lib.n ? lib.n : 1) * sizeof *o);
    if (!o)
        return;
    order = o;
    norder = gamelib_order(&lib, sort_by, only_fav ? 1 : 0, finding, order);
    gui_vlist_changed(list);
    show_page();
    show_status();
}

static void keep(void)                  /* the library's file written, if anything changed */
{
    if (lib.changed && lib_path[0] && gamelib_write(&lib, lib_path, gui_fopen) != 0)
        say("not kept");
}

/* ---- the folders gone through */

static int a_list(void *ctx, const char *dir, struct gamelib_entry **out)
{
    struct gui_dirent *e = NULL;
    struct gamelib_entry *g;
    int n = gui_dir_read(dir, &e), i;
    (void)ctx;
    if (n < 0)
        return -1;
    if (!(g = calloc((size_t)(n ? n : 1), sizeof *g))) {
        free(e);
        return -1;
    }
    for (i = 0; i < n; i++) {
        snprintf(g[i].name, sizeof g[i].name, "%s", e[i].name);
        g[i].size = e[i].size, g[i].dir = e[i].dir;
    }
    free(e);
    *out = g;
    return n;
}

/* (phase F) The scan off the window's thread: the folders' listings are the slow part (a DOS call or more each).
   The walk depends on them alone, not on what the library holds, so a worker scans an empty library of its own
   and keeps every listing it read; the window's thread then scans the library itself from those, at once. A
   folder taken out of the library meanwhile is not gone through. */
struct listing { char *dir; struct gamelib_entry *e; int n; };
struct scan_job {
    char folder[GAMELIB_FOLDERS][128];
    unsigned nfolders;
    struct listing *ls;
    int nls, room;
};
static struct gui_task *scanning;
static int scan_again;                  /* (a scan asked for while one ran: another after it) */

static int recorded(void *ctx, const char *dir, struct gamelib_entry **out)    /* (the worker's) */
{
    struct scan_job *j = ctx;
    int n = a_list(NULL, dir, out);
    struct listing *l;
    if (j->nls == j->room) {
        int room = j->room ? j->room * 2 : 64;
        struct listing *m = realloc(j->ls, (size_t)room * sizeof *m);
        if (!m)
            return n;
        j->ls = m, j->room = room;
    }
    l = &j->ls[j->nls];
    l->n = n, l->e = NULL;
    if (!(l->dir = malloc(strlen(dir) + 1)))
        return n;
    strcpy(l->dir, dir);
    if (n > 0 && !(l->e = malloc((size_t)n * sizeof *l->e))) {
        free(l->dir);
        return n;
    }
    if (n > 0)
        memcpy(l->e, *out, (size_t)n * sizeof *l->e);
    j->nls++;
    return n;
}

static int replayed(void *ctx, const char *dir, struct gamelib_entry **out)    /* (the window's) */
{
    struct scan_job *j = ctx;
    int i;
    for (i = 0; i < j->nls; i++)
        if (!strcmp(j->ls[i].dir, dir)) {
            if (j->ls[i].n < 0)
                return -1;
            if (!(*out = malloc((size_t)(j->ls[i].n ? j->ls[i].n : 1) * sizeof **out)))
                return -1;
            if (j->ls[i].n)
                memcpy(*out, j->ls[i].e, (size_t)j->ls[i].n * sizeof **out);
            return j->ls[i].n;
        }
    return a_list(NULL, dir, out);              /* (not read by the worker: read now) */
}

static void scan_work(struct gui_task *t, void *arg)
{
    struct scan_job *j = arg;
    struct gamelib scratch;
    unsigned i;
    gamelib_init(&scratch);
    for (i = 0; i < j->nfolders && !(t && gui_task_cancelled(t)); i++)
        gamelib_scan(&scratch, j->folder[i], recorded, j);
    gamelib_free(&scratch);
}

static void scan(void);

static void scan_done(struct gui_window *owner, void *arg, int cancelled)
{
    struct scan_job *j = arg;
    unsigned i, k;
    int added = 0, n;
    (void)owner;
    scanning = NULL;
    for (i = 0; !cancelled && i < j->nfolders; i++) {
        for (k = 0; k < lib.nfolders && strcmp(lib.folder[k], j->folder[i]); k++)
            ;
        if (k < lib.nfolders && (n = gamelib_scan(&lib, j->folder[i], replayed, j)) > 0)
            added += n;
    }
    for (n = 0; n < j->nls; n++)
        free(j->ls[n].dir), free(j->ls[n].e);
    free(j->ls);
    free(j);
    say("scanned folders=%u games=%u added=%d", lib.nfolders, lib.n, added);
    keep();
    reorder();
    if (norder && !current())
        gui_vlist_set(list, 0), show_page();
    if (scan_again) {
        scan_again = 0;
        scan();
    }
}

static void scan(void)
{
    struct scan_job *j;
    if (scanning) {
        scan_again = 1;
        return;
    }
    if (!(j = calloc(1, sizeof *j)))
        return;
    memcpy(j->folder, lib.folder, sizeof j->folder);
    j->nfolders = lib.nfolders;
    gui_set_text(status, GUI_T("Looking for games..."));
    if (!(scanning = gui_task_start(win, NULL, scan_work, j, scan_done, 0))) {
        scan_work(NULL, j);                     /* (no threads to be had: here) */
        scan_done(win, j, 0);
    }
}

/* ---- playing */

static void play(int setup)
{
    struct game *g = current();
    char program[300], t[300];
    if (!g || job || g->gone || (setup && !g->setup[0]))
        return;
    gui_path_join(program, sizeof program, g->dir, setup ? g->setup : g->run);
    job = glos_job_run(program, g->dir, NULL, GLOS_RUN_HIDDEN, 0, NULL);
    if (!job) {
        say("refused %s %d", g->title, glos_error());
        snprintf(t, sizeof t, GUI_T("%s couldn't be started."), program);
        gui_message(win, GUI_T("Games"), t, GUI_MB_OK);
        return;
    }
    playing = g->id, playing_setup = setup;
    say("%s %s", setup ? "setting up" : "playing", g->title);
    snprintf(t, sizeof t, setup ? GUI_T("Setting up %s...") : GUI_T("Playing %s..."), g->title);
    gui_set_text(status, t);
    show_page();
}

static void events(const struct glos_ev *ev)   /* the game ended: its time counted */
{
    const struct glos_ev_job *j = (const struct glos_ev_job *)ev;
    struct glos_job_status st;
    struct glos_time now;
    struct game *g;
    if (ev->kind != GLOS_EV_JOB || !job || ev->handle != job || j->state != GLOS_JS_DONE)
        return;
    memset(&st, 0, sizeof st), memset(&now, 0, sizeof now);
    st.size = sizeof st, now.size = sizeof now;
    glos_job_status(job, &st);
    glos_time(&now);
    glos_close(job);
    job = 0;
    if ((g = gamelib_find(&lib, playing)) != NULL) {
        if (j->code == 126 && st.ms < 2000) {   /* (JOB's code for a program that could not be run: no play) */
            char t[300];
            say("failed %s", g->title);
            reorder();
            snprintf(t, sizeof t, GUI_T("%s couldn't be run."), playing_setup ? g->setup : g->run);
            gui_message(win, GUI_T("Games"), t, GUI_MB_OK);
            return;
        }
        if (!playing_setup)
            gamelib_played(&lib, g, st.ms / 1000, (unsigned long)now.utc + (unsigned long)now.offset);
        say("ended %s code=%u times=%u", g->title, j->code, g->times);
    }
    keep();
    reorder();
}

/* ---- a game's properties */

static struct gui_widget *p_title, *p_picture, *p_notes, *p_fav, *p_screen, *p_direct, *p_memory, *p_blaster, *p_joymap;
static struct { char title[80], picture[128], notes[200]; int fav, screen, direct; char memory[12], blaster[48], joymap[48]; } p_got;

/* ---- a game's profile: its program's own settings (GLOS.CFG's [program NAME.EXE], as the Control Panel's Programs
   page has them): the whole screen or a window, direct mode, its memory, the Sound Blaster it is told of (BLASTER
   in its environment), its pad mapping (joymap: [joystick] map's words, the virtual game port's while it runs).
   For every copy of a program of that name. */

struct profile { int screen, direct; char memory[12], blaster[48], env[200], joymap[48]; };

/* A pad mapping as kernel/vm/vjoy.c takes one: eight words, four axes (lx ly rx ry, "-" before one, or none) then
   four buttons (1 to 16, or none); or nothing. 1 if so. */
static int joymap_ok(const char *m)
{
    int k = 0;
    while (*m) {
        char w[8];
        size_t n = 0;
        while (*m == ' ')
            m++;
        if (!*m)
            break;
        while (*m && *m != ' ')
            if (n < sizeof w - 1)
                w[n++] = *m++;
            else
                return 0;
        w[n] = 0;
        if (k < 4) {
            const char *q = w + (w[0] == '-');
            if (strcmp(q, "lx") && strcmp(q, "ly") && strcmp(q, "rx") && strcmp(q, "ry") && strcmp(w, "none"))
                return 0;
        } else if (k < 8) {
            char *e;
            long b = strtol(w, &e, 10);
            if (strcmp(w, "none") && (*e || e == w || b < 1 || b > 16))
                return 0;
        } else {
            return 0;
        }
        k++;
    }
    return k == 0 || k == 8;
}

static void profile_read(const char *run, struct profile *pr)
{
    char sec[40], v[200];
    const char *b;
    int set = 0;
    memset(pr, 0, sizeof *pr);
    snprintf(sec, sizeof sec, "program %s", run);
    if (glos_setting_get(sec, "screen", v, sizeof v, &set) == 0)
        pr->screen = !strcmp(v, "full");
    if (glos_setting_get(sec, "direct", v, sizeof v, &set) == 0)
        pr->direct = !strcmp(v, "1");
    if (glos_setting_get(sec, "memory", v, sizeof v, &set) == 0 && strcmp(v, "0"))
        snprintf(pr->memory, sizeof pr->memory, "%s", v);
    if (glos_setting_get(sec, "env", pr->env, sizeof pr->env, &set) != 0)
        pr->env[0] = 0;
    if (glos_setting_get(sec, "joymap", pr->joymap, sizeof pr->joymap, &set) != 0)
        pr->joymap[0] = 0;
    for (b = pr->env; b && *b; b = strchr(b, '\n') ? strchr(b, '\n') + 1 : NULL)   /* (its lines: NAME=VALUE) */
        if (!strncasecmp(b, "BLASTER=", 8)) {
            size_t n = strcspn(b + 8, "\n");
            snprintf(pr->blaster, sizeof pr->blaster, "%.*s", (int)(n < sizeof pr->blaster ? n : sizeof pr->blaster - 1), b + 8);
        }
}

/* The settings that differ written; the environment's other lines kept as they are. 0, or the words of a refusal. */
static const char *profile_write(const char *run, const struct profile *was, const struct profile *now)
{
    static char why[120];
    char sec[40], env[220];
    const char *b;
    size_t n = 0;
    snprintf(sec, sizeof sec, "program %s", run);
    if (now->screen != was->screen && glos_setting_set(sec, "screen", now->screen ? "full" : "window", NULL, why, sizeof why))
        return why;
    if (now->direct != was->direct && glos_setting_set(sec, "direct", now->direct ? "1" : "0", NULL, why, sizeof why))
        return why;
    if (strcmp(now->memory, was->memory)
        && glos_setting_set(sec, "memory", now->memory[0] ? now->memory : NULL, NULL, why, sizeof why))
        return why;
    if (strcmp(now->joymap, was->joymap)
        && glos_setting_set(sec, "joymap", now->joymap[0] ? now->joymap : NULL, NULL, why, sizeof why))
        return why;
    if (strcmp(now->blaster, was->blaster)) {
        env[0] = 0;
        for (b = was->env; b && *b; b = strchr(b, '\n') ? strchr(b, '\n') + 1 : NULL) {
            size_t k = strcspn(b, "\n");
            if (strncasecmp(b, "BLASTER=", 8) && n + k + 2 < sizeof env)
                n += (size_t)snprintf(env + n, sizeof env - n, "%s%.*s", n ? "\n" : "", (int)k, b);
        }
        if (now->blaster[0] && n + strlen(now->blaster) + 10 < sizeof env)
            n += (size_t)snprintf(env + n, sizeof env - n, "%sBLASTER=%s", n ? "\n" : "", now->blaster);
        if (glos_setting_set(sec, "env", n ? env : NULL, NULL, why, sizeof why))
            return why;
    }
    return NULL;
}
static const char *const pictures_kinds[] = { "Pictures|*.PNG;*.JPG;*.BMP;*.GIF;*.PCX;*.TGA", "All files|*.*", NULL };

static void props_done(struct gui_widget *b, int what)
{
    (void)what;                         /* (the button's own number is b->id) */
    if (b->id == 3) {                              /* its picture chosen */
        struct gui_file_opts o;
        char p[300];
        memset(&o, 0, sizeof o);
        o.filters = pictures_kinds;
        snprintf(p, sizeof p, "%s", gui_get_text(p_picture));
        if (gui_file_dialog_ex(b->win, GUI_T("A Picture for the Game"), 0, p, sizeof p, &o))
            gui_set_text(p_picture, p);
        return;
    }
    if (b->id == 1) {                              /* (read while the dialog is there: gui_modal closes it) */
        snprintf(p_got.title, sizeof p_got.title, "%s", gui_get_text(p_title));
        snprintf(p_got.picture, sizeof p_got.picture, "%s", gui_get_text(p_picture));
        snprintf(p_got.notes, sizeof p_got.notes, "%s", gui_get_text(p_notes));
        p_got.fav = gui_checked(p_fav);
        p_got.screen = gui_value(p_screen);
        p_got.direct = gui_checked(p_direct);
        snprintf(p_got.memory, sizeof p_got.memory, "%s", gui_get_text(p_memory));
        snprintf(p_got.blaster, sizeof p_got.blaster, "%s", gui_get_text(p_blaster));
        snprintf(p_got.joymap, sizeof p_got.joymap, "%s", gui_get_text(p_joymap));
        if (!joymap_ok(p_got.joymap)) {         /* (said before the dialog goes: it stays to be put right) */
            say("properties joymap refused %s", p_got.joymap);
            gui_message(b->win, GUI_T("Pad Mapping"),
                        GUI_T("A pad mapping is eight words: four axes (lx, ly, rx, ry, one with a minus before it the "
                              "other way round, or none), then four buttons by number (1 to 16, or none). Or nothing, "
                              "for the mapping every program has."), GUI_MB_OK);
            gui_focus(p_joymap);
            return;
        }
    }
    gui_end_modal(b->win, b->id == 1 ? GUI_OK : GUI_CANCEL_R);
}

static void properties(void)
{
    struct game *g = current();
    struct gui_window *d;
    struct gui_widget *box, *form, *row;
    struct profile was, now;
    char run[16], head[80];
    unsigned long id;
    if (!g)
        return;
    id = g->id;
    snprintf(run, sizeof run, "%s", g->run);
    profile_read(run, &was);
    d = gui_window(GUI_T("Properties"), 420, 0, GUI_W_DIALOG);
    d->owner = win;
    box = gui_vbox(d->root, 6, 8);
    form = gui_form(box);
    p_title = gui_field(gui_row(form, GUI_T("&Title:"), NULL), 40);
    gui_set_text(p_title, g->title);
    row = gui_row(form, GUI_T("&Picture:"), NULL);
    p_picture = gui_field(row, 30);
    gui_set_text(p_picture, g->picture);
    gui_button(row, GUI_T("&Choose..."), props_done, 3);
    p_notes = gui_field(gui_row(form, GUI_T("&Notes:"), NULL), 40);
    gui_set_text(p_notes, g->notes);
    p_fav = gui_check(box, GUI_T("A &favourite"), g->favourite);
    snprintf(head, sizeof head, GUI_T("How %s runs (every copy of it):"), run);
    gui_label(box, head);
    form = gui_form(box);
    p_screen = gui_choice(gui_row(form, GUI_T("&Screen:"), NULL), 16);
    gui_choice_add(p_screen, GUI_T("In a window"));
    gui_choice_add(p_screen, GUI_T("The whole screen"));
    gui_set_value(p_screen, was.screen);
    p_memory = gui_field(gui_row(form, GUI_T("&Memory (KB):"), NULL), 8);
    gui_tip(p_memory, GUI_T("Empty: all there is"));
    gui_set_text(p_memory, was.memory);
    p_blaster = gui_field(gui_row(form, GUI_T("Sound &Blaster:"), NULL), 24);
    gui_tip(p_blaster, GUI_T("As BLASTER has it: A220 I5 D1 H5 T6. Empty: as GLOS tells every program"));
    gui_set_text(p_blaster, was.blaster);
    p_joymap = gui_field(gui_row(form, GUI_T("Pad m&apping:"), NULL), 24);
    gui_tip(p_joymap, GUI_T("The pad's axes and buttons for its game port: lx ly rx ry 1 2 3 4 as a rule. Empty: as "
                            "every program has it"));
    gui_set_text(p_joymap, was.joymap);
    p_direct = gui_check(box, GUI_T("&Direct mode: the PC's ports are the game's (for one that won't run otherwise)"),
                         was.direct);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), props_done, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), props_done, 2)->flags |= GUI_CANCEL;
    gui_focus(p_title);
    if (gui_modal(d) != GUI_OK) {
        say("properties left");
        return;
    }
    now = was;
    now.screen = p_got.screen, now.direct = p_got.direct;
    snprintf(now.memory, sizeof now.memory, "%s", strcmp(p_got.memory, "0") ? p_got.memory : "");
    snprintf(now.blaster, sizeof now.blaster, "%s", p_got.blaster);
    snprintf(now.joymap, sizeof now.joymap, "%s", p_got.joymap);
    if (now.screen != was.screen || now.direct != was.direct || strcmp(now.memory, was.memory) || strcmp(now.blaster, was.blaster)
        || strcmp(now.joymap, was.joymap)) {
        const char *why = profile_write(run, &was, &now);
        if (why) {
            char t[200];
            snprintf(t, sizeof t, GUI_T("%s's settings weren't all kept: %s."), run, why);
            gui_message(win, GUI_T("Games"), t, GUI_MB_OK);
            say("profile refused %s %s", run, why);
        } else {
            say("profile %s screen=%s direct=%d memory=%s blaster=%s joymap=%s", run, now.screen ? "full" : "window", now.direct,
                now.memory[0] ? now.memory : "all", now.blaster[0] ? now.blaster : "none", now.joymap[0] ? now.joymap : "none");
        }
    }
    if (!(g = gamelib_find(&lib, id)))
        return;
    if (p_got.title[0] && strcmp(g->title, p_got.title)) {
        snprintf(g->title, sizeof g->title, "%s", p_got.title);
        g->by_hand = 1;
    }
    if (strcmp(g->picture, p_got.picture))
        pictures_forget(g->id);
    snprintf(g->picture, sizeof g->picture, "%s", p_got.picture);
    snprintf(g->notes, sizeof g->notes, "%s", p_got.notes);
    g->favourite = (unsigned char)p_got.fav;
    lib.changed = 1;
    say("changed %s favourite=%d picture=%s", g->title, g->favourite, g->picture[0] ? gui_path_name(g->picture) : "none");
    keep();
    reorder();
}

/* ---- commands */

static const char *const programs[] = { "Programs|*.EXE;*.COM;*.BAT", "All files|*.*", NULL };

static void command(struct gui_window *w, int id)
{
    struct gui_file_opts o;
    struct game *g = current();
    char p[300], t[300];
    (void)w;
    switch (id) {
    case C_PLAY:
    case C_SETUP:
        play(id == C_SETUP);
        break;
    case C_ADD:                                 /* any program: its folder and its name */
        memset(&o, 0, sizeof o);
        o.filters = programs;
        snprintf(p, sizeof p, "%s", lib.nfolders ? lib.folder[0] : "C:\\");
        if (gui_file_dialog_ex(win, GUI_T("Add a Game: Its Program"), 0, p, sizeof p, &o)) {
            const char *name = gui_path_name(p);
            snprintf(t, sizeof t, "%.*s", name > p + 3 ? (int)(name - p - 1) : (int)(name - p), p);
            if ((g = gamelib_add(&lib, t, name, "")) != NULL) {
                say("added %s", g->title);
                keep();
                reorder();
                gui_vlist_set(list, m_find(NULL, g->id));
                show_page();
            }
        }
        break;
    case C_ADDFOLDER:
        snprintf(p, sizeof p, "%s", "C:\\");
        if (gui_folder_dialog(win, GUI_T("Add a Folder of Games"), p, sizeof p) && gamelib_folder_add(&lib, p) == 0) {
            say("folder %s", p);
            scan();
        }
        break;
    case C_SCAN:
        scan();
        break;
    case C_EXIT:
        gui_quit(0);
        break;
    case C_FAV:
        if (g) {
            g->favourite = !g->favourite, lib.changed = 1;
            say("favourite %s %d", g->title, g->favourite);
            keep();
            reorder();
        }
        break;
    case C_PROPS:
        properties();
        break;
    case C_REMOVE:
        if (g && !job) {
            snprintf(t, sizeof t, GUI_T("Remove %s from the library? Nothing on the disk is touched."), g->title);
            if (gui_message(win, GUI_T("Remove"), t, GUI_MB_YESNO) == GUI_YES) {
                say("removed %s", g->title);
                gamelib_remove(&lib, g->id);
                keep();
                reorder();
            }
        }
        break;
    case C_PICS:
    case C_LIST:
        as_list = id == C_LIST;
        gui_vlist_view(list, as_list ? GUI_VIEW_DETAIL : GUI_VIEW_THUMBS);
        say("view %s", as_list ? "list" : "pictures");
        break;
    case C_BY_TITLE:
    case C_BY_LAST:
    case C_BY_TIME:
        sort_by = id - C_BY_TITLE;
        say("sorted %d", sort_by);
        reorder();
        break;
    case C_ONLY_FAV:
        only_fav = !only_fav;
        reorder();
        say("favourites %d shown=%u", only_fav, norder);
        break;
    case C_FIND:
        gui_focus(search);
        break;
    }
}

static void update(struct gui_window *w)
{
    struct game *g = current();
    gui_command_enable(w, C_PLAY, g && !g->gone && !job);
    gui_command_enable(w, C_SETUP, g && g->setup[0] && !g->gone && !job);
    gui_command_enable(w, C_FAV, g != NULL);
    gui_command_check(w, C_FAV, g && g->favourite);
    gui_command_enable(w, C_PROPS, g != NULL);
    gui_command_enable(w, C_REMOVE, g && !job);
    gui_command_enable(w, C_SCAN, lib.nfolders > 0);
    gui_command_check(w, C_PICS, !as_list), gui_command_check(w, C_LIST, as_list);
    gui_command_check(w, C_BY_TITLE, sort_by == 0), gui_command_check(w, C_BY_LAST, sort_by == 1);
    gui_command_check(w, C_BY_TIME, sort_by == 2), gui_command_check(w, C_ONLY_FAV, only_fav);
}

static void list_on(struct gui_widget *w, int what)
{
    (void)w;
    if (what == GUI_ON_ACTIVATE)
        play(0);
    else if (what == GUI_ON_SELECT)
        show_page();
}

static void search_on(struct gui_widget *w, int what)
{
    if (what != GUI_ON_CHANGE)
        return;
    snprintf(finding, sizeof finding, "%s", gui_get_text(w));
    reorder();
    say("found %u for \"%s\"", norder, finding);
}

static void button(struct gui_widget *b, int what)
{
    (void)what;                         /* (the button's own number is b->id) */
    command(win, b->id);
}

static void build(void)
{
    static const char *const heads[] = { GUI_N("Title"), GUI_N("Year"), GUI_N("By"), GUI_N("Kind"), GUI_N("Played") };
    static const int widths[] = { 132, 36, 92, 60, 70 };
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *tb, *row, *split, *page;
    win = gui_window(GUI_T("Games"), 620, 440, 0);
    win->on_command = command;
    win->on_update = update;
    gui_command(win, C_PLAY, GUI_T("&Play"), "Enter", gui_glyph("play"), GUI_T("Play the game"));
    gui_command(win, C_SETUP, GUI_T("&Set Up"), NULL, NULL, NULL);
    gui_command(win, C_ADD, GUI_T("&Add Game..."), "Ctrl+N", gui_glyph("add"), GUI_T("Add a game by its program"));
    gui_command(win, C_ADDFOLDER, GUI_T("Add &Folder..."), NULL, NULL, NULL);
    gui_command(win, C_SCAN, GUI_T("&Scan Now"), "F5", NULL, NULL);
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    gui_command(win, C_FAV, GUI_T("&Favourite"), "Ctrl+D", gui_glyph("star"), GUI_T("A favourite, or not"));
    gui_command(win, C_PROPS, GUI_T("P&roperties..."), "Alt+Enter", NULL, NULL);
    gui_command(win, C_REMOVE, GUI_T("&Remove"), "Del", NULL, NULL);
    gui_command(win, C_PICS, GUI_T("&Pictures"), "Ctrl+1", NULL, NULL);
    gui_command(win, C_LIST, GUI_T("&List"), "Ctrl+2", NULL, NULL);
    gui_command(win, C_BY_TITLE, GUI_T("By &Title"), NULL, NULL, NULL);
    gui_command(win, C_BY_LAST, GUI_T("By When Last &Played"), NULL, NULL, NULL);
    gui_command(win, C_BY_TIME, GUI_T("By Ti&me Played"), NULL, NULL, NULL);
    gui_command(win, C_ONLY_FAV, GUI_T("&Favourites Only"), "Ctrl+B", NULL, NULL);
    gui_command(win, C_FIND, GUI_T("F&ind"), "Ctrl+F", NULL, NULL);
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_command(m, C_ADD), gui_menu_command(m, C_ADDFOLDER), gui_menu_command(m, C_SCAN);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Game"));
    gui_menu_command(m, C_PLAY), gui_menu_command(m, C_SETUP);
    gui_menu_sep(m);
    gui_menu_command(m, C_FAV), gui_menu_command(m, C_PROPS);
    gui_menu_sep(m);
    gui_menu_command(m, C_REMOVE);
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_command(m, C_PICS), gui_menu_command(m, C_LIST);
    gui_menu_sep(m);
    gui_menu_command(m, C_BY_TITLE), gui_menu_command(m, C_BY_LAST), gui_menu_command(m, C_BY_TIME);
    gui_menu_sep(m);
    gui_menu_command(m, C_ONLY_FAV), gui_menu_command(m, C_FIND);
    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_PLAY);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_ADD), gui_toolbar_command(tb, C_FAV);
    row = gui_hbox(win->root, 6, 4);
    gui_label(row, GUI_T("Find:"));
    search = gui_field(row, 24);
    gui_on(search, search_on, 0);
    split = gui_split(win->root, 0, -200);      /* the games | a game's page, which keeps its width */
    gui_grow(split, 1);
    list = gui_vlist(split, 5, heads, widths, &model);
    gui_vlist_view(list, GUI_VIEW_THUMBS);
    gui_on(list, list_on, 0);
    page = gui_vbox(split, 6, 8);
    d_title = gui_label(page, "");
    gui_label_style(d_title, gui_font_get(GUI_FONT_BOLD, 1), GFX_NONE, -1);
    d_about = gui_label(page, "");
    d_where = gui_label(page, "");
    d_played = gui_label(page, "");
    d_notes = gui_label(page, "");
    row = gui_hbox(page, 6, 0);
    b_play = gui_button(row, GUI_T("&Play"), button, C_PLAY);
    b_setup = gui_button(row, GUI_T("&Set Up"), button, C_SETUP);
    gui_space(page, 0, 0);
    status = gui_status(win);
}

int main(int argc, char **argv)
{
    const char *home;
    char p[300];
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("GAMES no GLOS\n");
        return 1;
    }
    gamelib_init(&lib);
    home = glos_home();
    if (home && home[0]) {
        gui_path_join(lib_path, sizeof lib_path, home, "GAMES.CFG");
        thumb_home(home);
    }
    for (i = 1; i + 1 < argc; i++)              /* -library FILE: another library's file (the suite's) */
        if (!strcmp(argv[i], "-library"))
            snprintf(lib_path, sizeof lib_path, "%s", argv[i + 1]);
    if (!lib_path[0] || gamelib_read(&lib, lib_path, gui_fopen) != 0) {
        /* no library yet: the folders of games to begin with are GAMES on the drive DOS started from, and on C: */
        gui_path_join(p, sizeof p, glos_boot_root(), "GAMES");
        if (gui_is_dir(p))
            gamelib_folder_add(&lib, p);
        if (gui_is_dir("C:\\GAMES"))
            gamelib_folder_add(&lib, "C:\\GAMES");
    }
    build();
    gui_on_event(events);
    gui_show(win, 1);
    gui_focus(list);
    scan();
    if (norder)
        gui_vlist_set(list, 0);
    show_page();
    gui_run();
    keep();
    gamelib_free(&lib);
    printf("GAMES end\n");
    return 0;
}
