/* The Games Library's games (gamelib.h).
 *
 * The file (GAMES.CFG, in GLOS's folder) is lines of text: "folder PATH" for each folder gone through, and for
 * each game "game ID", its lines ("title ...", "dir ...", "run ...", and the rest, one a line), and "end". A
 * line that is not known is passed over, so a newer GLOS's file reads. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gamedb.h"
#include "gamelib.h"

void gamelib_init(struct gamelib *l) { memset(l, 0, sizeof *l), l->next_id = 1; }

void gamelib_free(struct gamelib *l)
{
    free(l->g);
    gamelib_init(l);
}

static void put(char *to, unsigned room, const char *from)     /* cut to its room, a line's end and blanks after it gone */
{
    unsigned n = (unsigned)strlen(from);
    while (n && (from[n - 1] == '\n' || from[n - 1] == '\r' || from[n - 1] == ' '))
        n--;
    n = n < room - 1 ? n : room - 1;
    memcpy(to, from, n);
    to[n] = 0;
}

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

static int same(const char *a, const char *b)          /* whatever the letters' case */
{
    for (; *a && upper((unsigned char)*a) == upper((unsigned char)*b); a++, b++)
        ;
    return !*a && !*b;
}

static struct game *more(struct gamelib *l)
{
    struct game *g;
    if (l->n == l->room) {
        unsigned room = l->room ? l->room * 2 : 32;
        if (room > 4096 || !(g = realloc(l->g, room * sizeof *g)))
            return NULL;
        l->g = g, l->room = room;
    }
    g = &l->g[l->n++];
    memset(g, 0, sizeof *g);
    g->id = l->next_id++;
    return g;
}

struct game *gamelib_find(struct gamelib *l, unsigned long id)
{
    unsigned i;
    for (i = 0; i < l->n; i++)
        if (l->g[i].id == id)
            return &l->g[i];
    return NULL;
}

static struct game *by_dir(struct gamelib *l, const char *dir, const char *run)        /* (run NULL: any program) */
{
    unsigned i;
    for (i = 0; i < l->n; i++)
        if (same(l->g[i].dir, dir) && (!run || same(l->g[i].run, run)))
            return &l->g[i];
    return NULL;
}

/* ---- the file */

int gamelib_read(struct gamelib *l, const char *path, void *(*open)(const char *, const char *))
{
    FILE *f = open ? (FILE *)open(path, "r") : fopen(path, "r");
    char line[400];
    struct game *g = NULL;
    if (!f)
        return -1;
    while (fgets(line, sizeof line, f)) {
        char *v = strchr(line, ' ');
        unsigned long n;
        if (!strncmp(line, "end", 3)) {
            if (g && (!g->dir[0] || !g->run[0]))        /* (a game with no program is none) */
                l->n--;
            g = NULL;
            continue;
        }
        if (!v)
            continue;
        *v++ = 0;
        n = strtoul(v, NULL, 10);
        if (!strcmp(line, "folder")) {
            if (l->nfolders < GAMELIB_FOLDERS)
                put(l->folder[l->nfolders++], sizeof l->folder[0], v);
        } else if (!strcmp(line, "game")) {
            if ((g = more(l)) != NULL && n) {
                g->id = n;
                if (n >= l->next_id)
                    l->next_id = n + 1;
            }
        } else if (!g) {
            continue;
        } else if (!strcmp(line, "title")) {
            put(g->title, sizeof g->title, v);
        } else if (!strcmp(line, "dir")) {
            put(g->dir, sizeof g->dir, v);
        } else if (!strcmp(line, "run")) {
            put(g->run, sizeof g->run, v);
        } else if (!strcmp(line, "setup")) {
            put(g->setup, sizeof g->setup, v);
        } else if (!strcmp(line, "maker")) {
            put(g->maker, sizeof g->maker, v);
        } else if (!strcmp(line, "genre")) {
            put(g->genre, sizeof g->genre, v);
        } else if (!strcmp(line, "picture")) {
            put(g->picture, sizeof g->picture, v);
        } else if (!strcmp(line, "notes")) {
            put(g->notes, sizeof g->notes, v);
        } else if (!strcmp(line, "year")) {
            g->year = n < 3000 ? (unsigned short)n : 0;
        } else if (!strcmp(line, "favourite")) {
            g->favourite = n != 0;
        } else if (!strcmp(line, "hand")) {
            g->by_hand = n != 0;
        } else if (!strcmp(line, "gone")) {
            g->gone = n != 0;
        } else if (!strcmp(line, "played")) {
            g->played = n;
        } else if (!strcmp(line, "last")) {
            g->last = n;
        } else if (!strcmp(line, "times")) {
            g->times = (unsigned)n;
        }
    }
    if (g && (!g->dir[0] || !g->run[0]))                /* (the file ended in a game) */
        l->n--;
    fclose(f);
    l->changed = 0;
    return 0;
}

int gamelib_write(struct gamelib *l, const char *path, void *(*open)(const char *, const char *))
{
    FILE *f = open ? (FILE *)open(path, "w") : fopen(path, "w");
    unsigned i;
    int bad;
    if (!f)
        return -1;
    for (i = 0; i < l->nfolders; i++)
        fprintf(f, "folder %s\n", l->folder[i]);
    for (i = 0; i < l->n; i++) {
        const struct game *g = &l->g[i];
        fprintf(f, "game %lu\ntitle %s\ndir %s\nrun %s\n", g->id, g->title, g->dir, g->run);
        if (g->setup[0])
            fprintf(f, "setup %s\n", g->setup);
        if (g->maker[0])
            fprintf(f, "maker %s\n", g->maker);
        if (g->genre[0])
            fprintf(f, "genre %s\n", g->genre);
        if (g->year)
            fprintf(f, "year %u\n", (unsigned)g->year);
        if (g->picture[0])
            fprintf(f, "picture %s\n", g->picture);
        if (g->notes[0])
            fprintf(f, "notes %s\n", g->notes);
        if (g->favourite)
            fprintf(f, "favourite 1\n");
        if (g->by_hand)
            fprintf(f, "hand 1\n");
        if (g->gone)
            fprintf(f, "gone 1\n");
        if (g->times || g->played)
            fprintf(f, "played %lu\nlast %lu\ntimes %u\n", g->played, g->last, g->times);
        fprintf(f, "end\n");
    }
    bad = ferror(f);
    if (fclose(f) != 0 || bad)
        return -1;
    l->changed = 0;
    return 0;
}

/* ---- games found */

static void join(char *out, unsigned room, const char *dir, const char *name)
{
    unsigned n = (unsigned)strlen(dir);
    snprintf(out, room, "%s%s%s", dir, n && dir[n - 1] != '\\' && dir[n - 1] != '/' ? "\\" : "", name);
}

static const char *ext_of(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot ? dot + 1 : "";
}

static int is_program(const char *name) { return same(ext_of(name), "EXE") || same(ext_of(name), "COM") || same(ext_of(name), "BAT"); }

/* A program that is beside a game, not it: set-up and installing programs, sound choosers, DOS extenders, readers of
   text. By the name before its dot. */
static int beside(const char *name)
{
    static const char *const stems[] = { "SETUP", "INSTALL", "INSTALL2", "SETSOUND", "SNDSETUP", "SOUND", "SETSND", "CONFIG", "SETMAIN",
                                         "SETBLAST", "UNINSTAL", "UNINST", "README", "READ", "HELP", "ORDER", "CATALOG", "REGISTER",
                                         "DOS4GW", "DOS4G", "DOS32A", "CWSDPMI", "PMODEW", "UNIVBE", "UNIVESA", "VESA", "IPXSETUP",
                                         "SERSETUP", "MODEM", "DWANGO", "COMMIT", "PATCH", "UPDATE", "MAKEBOOT", "BOOTDISK", "FIXSAVE",
                                         "LHA", "PKUNZIP", "UNZIP", "ARJ", "GO32", "RTM", "DPMI16BI", "32RTM", "VIEW", "MANUAL" };
    char stem[16];
    unsigned i, n = 0;
    for (; name[n] && name[n] != '.' && n < 15; n++)
        stem[n] = (char)upper((unsigned char)name[n]);
    stem[n] = 0;
    for (i = 0; i < sizeof stems / sizeof stems[0]; i++)
        if (!strcmp(stem, stems[i]))
            return 1;
    return 0;
}

static int starts(const char *name, const char *with)   /* whatever the letters' case */
{
    for (; *with; name++, with++)
        if (upper((unsigned char)*name) != *with)
            return 0;
    return 1;
}

static void tidy_title(char *out, unsigned room, const char *folder)   /* a folder's name as a title: DOOM2 -> Doom2 */
{
    unsigned i;
    put(out, room, folder);
    for (i = 0; out[i]; i++)
        out[i] = (char)(i == 0 || out[i - 1] == ' ' || out[i - 1] == '_' || out[i - 1] == '-'
                        ? upper((unsigned char)out[i]) : out[i] >= 'A' && out[i] <= 'Z' ? out[i] + 32 : out[i]);
    for (i = 0; out[i]; i++)
        if (out[i] == '_')
            out[i] = ' ';
}

/* The game a folder holds: 1, with its program, its setup program and (where it is a known one) the game. 0: none. */
static int game_in(const struct gamelib_entry *e, int n, const char *folder_name, char run[16], char setup[16],
                   const struct game_known **known)
{
    const char **names = malloc((size_t)(n > 0 ? n : 1) * sizeof *names);
    const struct gamelib_entry *best = NULL;
    int i, programs = 0, rank, best_rank = 0;
    run[0] = setup[0] = 0;
    *known = NULL;
    if (!names)
        return 0;
    for (i = 0; i < n; i++)
        names[i] = e[i].name;
    *known = gamedb_match(names, (unsigned)n);
    free(names);
    for (i = 0; i < n; i++) {
        unsigned stem, flen = (unsigned)strlen(folder_name), k;
        int named;
        if (e[i].dir || !is_program(e[i].name) || strlen(e[i].name) > 12)
            continue;
        if (beside(e[i].name)) {                /* (its set-up program, if it is a program: kept for the game's page) */
            if (!setup[0] && !same(ext_of(e[i].name), "BAT") && (starts(e[i].name, "SETUP") || starts(e[i].name, "INSTALL")
                                                                 || starts(e[i].name, "SETSOUND") || starts(e[i].name, "SNDSETUP")))
                put(setup, 16, e[i].name);
            continue;
        }
        programs++;
        /* the likeliest: one named as its folder (ZAPPER.EXE in ZAPPER), or nearly (KEEN4E.EXE in KEEN, ZAP.EXE in
           ZAPPER); then a batch file, which is there to start it; then the largest */
        stem = (unsigned)(strrchr(e[i].name, '.') - e[i].name);
        named = stem >= 2 && flen >= 2;
        for (k = 0; named && k < stem && k < flen; k++)
            if (upper((unsigned char)folder_name[k]) != upper((unsigned char)e[i].name[k]))
                named = 0;
        rank = named && stem == flen ? 4 : named ? 3 : same(ext_of(e[i].name), "BAT") ? 2 : 1;
        if (rank > best_rank || (rank == best_rank && best && e[i].size > best->size))
            best = &e[i], best_rank = rank;
    }
    if (*known) {
        put(run, 16, (*known)->run);
        if ((*known)->setup)
            for (i = 0; i < n; i++)
                if (same(e[i].name, (*known)->setup))
                    put(setup, 16, e[i].name);
        return 1;
    }
    if (!best || !programs)
        return 0;
    put(run, 16, best->name);
    return 1;
}

static const char *last_part(const char *path)
{
    const char *a = strrchr(path, '\\'), *b = strrchr(path, '/');
    a = a && b ? (a > b ? a : b) : a ? a : b;
    return a ? a + 1 : path;
}

static int scan_in(struct gamelib *l, const char *folder, gamelib_list_fn list, void *ctx, int depth, unsigned char *seen)
{
    struct gamelib_entry *e = NULL, *sub;
    int n = list(ctx, folder, &e), i, added = 0;
    if (n < 0)
        return -1;
    for (i = 0; i < n && l->n < 4096; i++) {
        char dir[128], run[16], setup[16];
        const struct game_known *known;
        struct game *g;
        int k;
        if (!e[i].dir || strlen(folder) + strlen(e[i].name) + 2 > sizeof dir)
            continue;
        join(dir, sizeof dir, folder, e[i].name);
        sub = NULL;
        if ((k = list(ctx, dir, &sub)) < 0)
            continue;
        if (game_in(sub, k, e[i].name, run, setup, &known)) {
            if ((g = by_dir(l, dir, NULL)) != NULL) {  /* the library has it: as the user left it, there again */
                if (seen)
                    seen[g - l->g] = 1;
                if (!g->by_hand && !same(g->run, run)) {
                    put(g->run, sizeof g->run, run);
                    l->changed = 1;
                }
                if (g->gone)
                    g->gone = 0, l->changed = 1;
            } else if ((g = more(l)) != NULL) {
                put(g->dir, sizeof g->dir, dir);
                put(g->run, sizeof g->run, run);
                put(g->setup, sizeof g->setup, setup);
                if (known) {
                    put(g->title, sizeof g->title, known->title);
                    put(g->maker, sizeof g->maker, known->maker);
                    put(g->genre, sizeof g->genre, known->genre);
                    g->year = known->year;
                } else {
                    tidy_title(g->title, sizeof g->title, e[i].name);
                }
                added++, l->changed = 1;
            }
        } else if (depth == 0) {                /* no game in it: games in its folders, perhaps (C:\GAMES\ID\DOOM) */
            int more_found = scan_in(l, dir, list, ctx, 1, seen);
            added += more_found > 0 ? more_found : 0;
        }
        free(sub);
    }
    free(e);
    return added;
}

int gamelib_scan(struct gamelib *l, const char *folder, gamelib_list_fn list, void *ctx)
{
    unsigned before = l->n, i, flen = (unsigned)strlen(folder);
    unsigned char *seen = calloc(before ? before : 1, 1);
    int added = scan_in(l, folder, list, ctx, 0, seen);
    /* a game of this folder's that the scan didn't meet has gone (its folder or its program): marked, not let go:
       its times and notes are the user's */
    for (i = 0; seen && added >= 0 && i < before; i++) {
        struct game *g = &l->g[i];
        unsigned k;
        int under = strlen(g->dir) > flen;
        for (k = 0; under && k < flen; k++)
            if (upper((unsigned char)g->dir[k]) != upper((unsigned char)folder[k]))
                under = 0;
        if (under && !seen[i] && !g->gone && !g->by_hand)
            g->gone = 1, l->changed = 1;
    }
    free(seen);
    return added;
}

struct game *gamelib_add(struct gamelib *l, const char *dir, const char *run, const char *title)
{
    struct game *g = by_dir(l, dir, run);
    if (g || !dir[0] || !run[0] || strlen(run) > 12 || !(g = more(l)))
        return g;
    put(g->dir, sizeof g->dir, dir);
    put(g->run, sizeof g->run, run);
    if (title && title[0])
        put(g->title, sizeof g->title, title);
    else
        tidy_title(g->title, sizeof g->title, last_part(dir));
    g->by_hand = 1;
    l->changed = 1;
    return g;
}

void gamelib_remove(struct gamelib *l, unsigned long id)
{
    struct game *g = gamelib_find(l, id);
    if (!g)
        return;
    memmove(g, g + 1, (size_t)(l->g + l->n - (g + 1)) * sizeof *g);
    l->n--;
    l->changed = 1;
}

int gamelib_folder_add(struct gamelib *l, const char *folder)
{
    unsigned i;
    for (i = 0; i < l->nfolders; i++)
        if (same(l->folder[i], folder))
            return -1;
    if (l->nfolders == GAMELIB_FOLDERS || !folder[0] || strlen(folder) >= sizeof l->folder[0])
        return -1;
    put(l->folder[l->nfolders++], sizeof l->folder[0], folder);
    l->changed = 1;
    return 0;
}

void gamelib_folder_remove(struct gamelib *l, unsigned i)
{
    if (i >= l->nfolders)
        return;
    memmove(l->folder[i], l->folder[i + 1], (size_t)(l->nfolders - i - 1) * sizeof l->folder[0]);
    l->nfolders--;
    l->changed = 1;
}

void gamelib_played(struct gamelib *l, struct game *g, unsigned long seconds, unsigned long when)
{
    g->played += seconds;
    g->last = when;
    g->times++;
    l->changed = 1;
}

/* ---- in an order */

static int has_text(const char *in, const char *find)   /* find is in it, whatever the letters' case */
{
    size_t n = strlen(find), i, k;
    for (i = 0; in[i]; i++) {
        for (k = 0; k < n && in[i + k] && upper((unsigned char)in[i + k]) == upper((unsigned char)find[k]); k++)
            ;
        if (k == n)
            return 1;
    }
    return !n;
}

static const struct gamelib *sorting;
static int sort_by;

static int in_order(const void *a, const void *b)
{
    const struct game *x = NULL, *y = NULL;
    unsigned i;
    int c;
    for (i = 0; i < sorting->n; i++) {
        if (sorting->g[i].id == *(const unsigned long *)a)
            x = &sorting->g[i];
        if (sorting->g[i].id == *(const unsigned long *)b)
            y = &sorting->g[i];
    }
    if (!x || !y)
        return 0;
    if (sort_by == 1 && x->last != y->last)
        return x->last > y->last ? -1 : 1;
    if (sort_by == 2 && x->played != y->played)
        return x->played > y->played ? -1 : 1;
    for (i = 0; x->title[i] && upper((unsigned char)x->title[i]) == upper((unsigned char)y->title[i]); i++)
        ;
    c = upper((unsigned char)x->title[i]) - upper((unsigned char)y->title[i]);
    return c ? c : x->id < y->id ? -1 : x->id > y->id;
}

unsigned gamelib_order(const struct gamelib *l, int by, unsigned flags, const char *find, unsigned long *ids)
{
    unsigned i, n = 0;
    for (i = 0; i < l->n; i++) {
        const struct game *g = &l->g[i];
        if ((flags & 1) && !g->favourite)
            continue;
        if (find && find[0] && !has_text(g->title, find) && !has_text(g->maker, find) && !has_text(g->genre, find))
            continue;
        ids[n++] = g->id;
    }
    sorting = l, sort_by = by;
    qsort(ids, n, sizeof ids[0], in_order);
    return n;
}
