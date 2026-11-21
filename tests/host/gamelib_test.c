/* Host test for the Games Library's games (apps/shared/gamedb.c, gamelib.c): the list of known games (every
 * entry whole, no game twice for the same files); a made-up tree of game folders gone through: known games by
 * their files (the one with the more telling companion first), unknown ones by their program (named as the
 * folder, a batch file, the largest; set-up programs apart), folders within folders, folders with no game; the
 * file written and read back the same; a second scan (nothing new, a game gone marked, the user's title kept);
 * games added by hand and removed; time played counted; the orders and the search.
 *
 *     gamelib_test OUT-DIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/gamedb.h"
#include "../../apps/shared/gamelib.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

/* The made-up disk: each line a file (a size after it) or, ending in a backslash, a folder. */
static const char *tree[64] = {
    "C:\\GAMES\\DOOM\\DOOM.EXE 715493", "C:\\GAMES\\DOOM\\DOOM1.WAD 4196020", "C:\\GAMES\\DOOM\\SETUP.EXE 43000",
    "C:\\GAMES\\DOOM\\DWANGO.EXE 90000", "C:\\GAMES\\DOOM\\README.EXE 20000",
    "C:\\GAMES\\FINAL\\doom2.exe 700000", "C:\\GAMES\\FINAL\\tnt.wad 18000000", "C:\\GAMES\\FINAL\\DOOM2.WAD 14000000",
    "C:\\GAMES\\QUAKE\\QUAKE.EXE 400000", "C:\\GAMES\\QUAKE\\ID1\\", "C:\\GAMES\\QUAKE\\ID1\\PAK0.PAK 18000000",
    "C:\\GAMES\\NOQUAKE\\QUAKE.EXE 400000",
    "C:\\GAMES\\ZAPPER\\INSTALL.EXE 90000", "C:\\GAMES\\ZAPPER\\ZAP.EXE 50000", "C:\\GAMES\\ZAPPER\\ZAPPER.EXE 30000",
    "C:\\GAMES\\ZAPPER\\DOS4GW.EXE 265000",
    "C:\\GAMES\\BLOB_WARS\\MAIN.EXE 100000", "C:\\GAMES\\BLOB_WARS\\EDITOR.EXE 200000", "C:\\GAMES\\BLOB_WARS\\GO.BAT 40",
    "C:\\GAMES\\BIGGEST\\A.EXE 1000", "C:\\GAMES\\BIGGEST\\B.COM 90000", "C:\\GAMES\\BIGGEST\\C.EXE 5000",
    "C:\\GAMES\\ID\\WOLF\\WOLF3D.EXE 110000", "C:\\GAMES\\ID\\WOLF\\VSWAP.WL1 700000",
    "C:\\GAMES\\ID\\KEEN\\KEEN4E.EXE 105000",
    "C:\\GAMES\\ID\\DEEP\\ER\\DOOM.EXE 715493", "C:\\GAMES\\ID\\DEEP\\ER\\DOOM.WAD 11000000",
    "C:\\GAMES\\DOCS\\README.TXT 2000", "C:\\GAMES\\DOCS\\SETUP.EXE 5000",
    "C:\\GAMES\\EMPTY\\",
    "C:\\GAMES\\LOOSE.EXE 1234",
    "D:\\MORE\\TYCOON\\TYCOON.EXE 500000", "D:\\MORE\\TYCOON\\TTDX.EXE 900000",
};
static int nlists;

static int a_list(void *ctx, const char *dir, struct gamelib_entry **out)
{
    struct gamelib_entry *e = calloc(64, sizeof *e);
    size_t dl = strlen(dir);
    int n = 0, i, k, found = 0;
    (void)ctx;
    nlists++;
    for (i = 0; i < 64 && tree[i]; i++) {
        const char *rest, *end;
        if (strncmp(tree[i], dir, dl) || tree[i][dl] != '\\')
            continue;
        found = 1;
        rest = tree[i] + dl + 1;
        if (!*rest)
            continue;                           /* (the folder's own line) */
        end = rest + strcspn(rest, "\\ ");
        for (k = 0; k < n; k++)
            if (strlen(e[k].name) == (size_t)(end - rest) && !strncmp(e[k].name, rest, (size_t)(end - rest)))
                break;
        if (k < n)
            continue;
        memcpy(e[n].name, rest, (size_t)(end - rest));
        e[n].dir = *end == '\\';
        e[n].size = *end == ' ' ? strtoul(end + 1, NULL, 10) : 0;
        n++;
    }
    if (!found) {
        free(e);
        return -1;
    }
    *out = e;
    return n;
}

static struct game *by_title(struct gamelib *l, const char *title)
{
    unsigned i;
    for (i = 0; i < l->n; i++)
        if (!strcmp(l->g[i].title, title))
            return &l->g[i];
    return NULL;
}

int main(int argc, char **argv)
{
    const char *out = argc > 1 ? argv[1] : ".";
    char path[600];
    struct gamelib l, m;
    struct game *g;
    unsigned long ids[64];
    unsigned i, k, n;
    int ok;

    /* the list: every entry whole; no two the same program and companion (the second would never be met) */
    for (i = 0, ok = gamedb_count() > 60; i < gamedb_count(); i++) {
        const struct game_known *a = gamedb_at(i);
        ok = ok && a->title[0] && a->maker[0] && a->genre[0] && a->run[0] && strlen(a->run) <= 12 && strchr(a->run, '.')
             && (a->year == 0 || (a->year >= 1980 && a->year <= 2030)) && (!a->setup || strlen(a->setup) <= 12);
        for (k = 0; k < i; k++) {
            const struct game_known *b = gamedb_at(k);
            if (!strcmp(a->run, b->run) && (!b->with || (a->with && !strcmp(a->with, b->with)))) {
                printf("  %s is never met: %s comes first\n", a->title, b->title);
                ok = 0;
            }
        }
    }
    CHECK(ok && !gamedb_at(gamedb_count()), "the list of %u known games", gamedb_count());
    {
        static const char *const doom[] = { "readme.txt", "Doom.exe", "DOOM1.WAD" }, *const two[] = { "DOOM2.EXE", "DOOM2.WAD", "TNT.WAD" };
        static const char *const none[] = { "DOOM.EXE" }, *const quake[] = { "ID1", "QUAKE.EXE" };
        CHECK(gamedb_match(doom, 3) && !strcmp(gamedb_match(doom, 3)->title, "Doom") && gamedb_match(two, 3)
              && !strcmp(gamedb_match(two, 3)->title, "Final Doom: TNT Evilution") && !gamedb_match(none, 1) && !gamedb_match(none, 0)
              && gamedb_match(quake, 2) && gamedb_match(quake, 2)->year == 1996, "known games by their files");
    }

    /* a folder of games gone through */
    gamelib_init(&l);
    CHECK(gamelib_folder_add(&l, "C:\\GAMES") == 0 && gamelib_folder_add(&l, "c:\\games") == -1 && l.nfolders == 1, "the folders");
    n = (unsigned)gamelib_scan(&l, "C:\\GAMES", a_list, NULL);
    CHECK(n == 9 && l.n == 9 && l.changed, "the scan: %u games", n);
    g = by_title(&l, "Doom");
    CHECK(g && !strcmp(g->dir, "C:\\GAMES\\DOOM") && !strcmp(g->run, "DOOM.EXE") && !strcmp(g->setup, "SETUP.EXE") && g->year == 1993
          && !strcmp(g->maker, "id Software") && !strcmp(g->genre, "Action") && !g->by_hand && !g->gone, "a known game, its set-up program");
    g = by_title(&l, "Final Doom: TNT Evilution");
    CHECK(g && !strcmp(g->run, "DOOM2.EXE") && !g->setup[0], "the game with the more telling companion, whatever the names' case");
    CHECK(by_title(&l, "Quake") && !by_title(&l, "Doom II: Hell on Earth"), "a companion that is a folder");
    g = by_title(&l, "Noquake");                        /* (QUAKE.EXE with no ID1: not the known game; a program all the same) */
    CHECK(g && !strcmp(g->run, "QUAKE.EXE") && !g->year && !g->maker[0], "a known program without its companion");
    g = by_title(&l, "Zapper");
    CHECK(g && !strcmp(g->run, "ZAPPER.EXE") && !strcmp(g->setup, "INSTALL.EXE"), "an unknown game: the program named as its folder");
    g = by_title(&l, "Blob Wars");
    CHECK(g && !strcmp(g->run, "GO.BAT"), "... a batch file to start it: %s", g ? g->run : "none");
    g = by_title(&l, "Biggest");
    CHECK(g && !strcmp(g->run, "B.COM"), "... the largest program: %s", g ? g->run : "none");
    g = by_title(&l, "Wolfenstein 3D");
    CHECK(g && !strcmp(g->dir, "C:\\GAMES\\ID\\WOLF") && by_title(&l, "Commander Keen 4: Secret of the Oracle"),
          "games in a folder's folders");
    CHECK(!by_title(&l, "Docs") && !by_title(&l, "Empty") && !by_title(&l, "Er") && !by_title(&l, "Deep") && !by_title(&l, "Loose"),
          "no game: a folder of text and a set-up program, an empty one, one too deep, a program that is in no folder");
    CHECK(gamelib_scan(&l, "E:\\NONE", a_list, NULL) == -1 && l.n == 9, "a folder that can't be read");

    /* what the user does: a title changed, a favourite, a game played, one added by hand */
    g = by_title(&l, "Zapper");
    snprintf(g->title, sizeof g->title, "Zapper II: The Zappening"), g->by_hand = 1, g->favourite = 1;
    snprintf(g->notes, sizeof g->notes, "Level 3 code: ZAP-777");
    gamelib_played(&l, g, 754, 0x5B75A000ul);
    gamelib_played(&l, g, 46, 0x5B75B000ul);
    gamelib_played(&l, by_title(&l, "Doom"), 3600, 0x5B740000ul);
    CHECK(g->played == 800 && g->times == 2 && g->last == 0x5B75B000ul, "time played counted");
    g = gamelib_add(&l, "D:\\OTHER\\THING", "START.BAT", "");
    CHECK(g && !strcmp(g->title, "Thing") && g->by_hand && gamelib_add(&l, "d:\\other\\thing", "start.bat", "x") == g && l.n == 10
          && !gamelib_add(&l, "D:\\X", "", "x") && !gamelib_add(&l, "D:\\X", "FARTOOLONGNAME.EXE", "x"), "a game added by hand, once");
    g = gamelib_add(&l, "D:\\OTHER\\NAMED", "N.EXE", "The Named One");
    CHECK(g && !strcmp(g->title, "The Named One") && l.n == 11, "... with its title");

    /* the file: written, and read back the same */
    snprintf(path, sizeof path, "%s/GAMES.CFG", out);
    CHECK(gamelib_write(&l, path, NULL) == 0 && !l.changed, "the file written");
    gamelib_init(&m);
    CHECK(gamelib_read(&m, path, NULL) == 0 && m.n == l.n && m.nfolders == 1 && !strcmp(m.folder[0], "C:\\GAMES")
          && m.next_id == l.next_id, "the file read: %u games", m.n);
    for (i = 0, ok = m.n == l.n; ok && i < l.n; i++)
        ok = !memcmp(&l.g[i], &m.g[i], sizeof l.g[i]);
    CHECK(ok, "every game as it was written");
    gamelib_free(&m);
    gamelib_init(&m);
    CHECK(gamelib_read(&m, "/no/such/file", NULL) == -1 && !m.n, "no file: an empty library");
    {                                           /* a file with what is not known in it, and a game with no program */
        FILE *f = fopen(path, "w");
        fprintf(f, "wibble 7\nfolder C:\\G\ngame 5\ntitle Half\ndir C:\\G\\HALF\nend\ngame 9\ntitle Whole\nnewer thing\ndir C:\\G\\W\n"
                   "run W.EXE\nyear 99999\nend\ngame 0\ntitle Cut\ndir C:\\G\\CUT\nrun C.EXE\n");
        fclose(f);
        CHECK(gamelib_read(&m, path, NULL) == 0 && m.n == 2 && !strcmp(m.g[0].title, "Whole") && m.g[0].id == 9 && !m.g[0].year
              && !strcmp(m.g[1].title, "Cut") && m.g[1].id == 10 && m.next_id == 11, "a file of another GLOS's, and a damaged one");
    }
    gamelib_free(&m);

    /* scanned again: nothing new; the user's title and program kept; a game whose folder has gone is marked */
    tree[12] = tree[13] = tree[14] = tree[15] = "C:\\GONE\\";
    tree[8] = "C:\\GAMES\\QUAKE\\QUAKE.BAK 1";
    n = (unsigned)gamelib_scan(&l, "C:\\GAMES", a_list, NULL);
    g = by_title(&l, "Zapper II: The Zappening");
    CHECK(n == 0 && l.n == 11 && g && !g->gone && g->favourite && g->played == 800, "scanned again: nothing new, the user's game as it was");
    g = by_title(&l, "Quake");
    CHECK(g && g->gone && !by_title(&l, "Doom")->gone && !by_title(&l, "Thing")->gone, "a game whose program has gone is marked");
    tree[8] = "C:\\GAMES\\QUAKE\\QUAKE.EXE 400000";
    gamelib_scan(&l, "C:\\GAMES", a_list, NULL);
    CHECK(!by_title(&l, "Quake")->gone, "... and found again");
    CHECK(gamelib_scan(&l, "D:\\MORE", a_list, NULL) == 1 && by_title(&l, "Transport Tycoon Deluxe") && !by_title(&l, "Doom")->gone,
          "another folder's games; the first folder's are not its to mark");

    /* in an order; searched; favourites */
    n = gamelib_order(&l, 0, 0, NULL, ids);
    for (i = 1, ok = n == 12; ok && i < n; i++)
        ok = strcmp(gamelib_find(&l, ids[i - 1])->title, gamelib_find(&l, ids[i])->title) <= 0;
    CHECK(ok && !strcmp(gamelib_find(&l, ids[0])->title, "Biggest"), "by title: %u", n);
    n = gamelib_order(&l, 1, 0, "", ids);
    CHECK(n == 12 && !strcmp(gamelib_find(&l, ids[0])->title, "Zapper II: The Zappening") && !strcmp(gamelib_find(&l, ids[1])->title, "Doom"),
          "by when last played");
    n = gamelib_order(&l, 2, 0, NULL, ids);
    CHECK(!strcmp(gamelib_find(&l, ids[0])->title, "Doom") && !strcmp(gamelib_find(&l, ids[1])->title, "Zapper II: The Zappening"),
          "by time played");
    CHECK(gamelib_order(&l, 0, 1, NULL, ids) == 1 && gamelib_order(&l, 0, 0, "ID SOFT", ids) == 4 && gamelib_order(&l, 0, 0, "strategy", ids) == 1
          && gamelib_order(&l, 0, 0, "zappening", ids) == 1 && gamelib_order(&l, 0, 0, "nothing such", ids) == 0
          && gamelib_order(&l, 0, 1, "doom", ids) == 0, "favourites; searched by title, maker and kind");

    /* removed */
    g = by_title(&l, "Thing");
    gamelib_remove(&l, g->id);
    gamelib_remove(&l, 99999);
    CHECK(l.n == 11 && !by_title(&l, "Thing") && by_title(&l, "The Named One") && !gamelib_find(&l, 99999), "a game removed");
    gamelib_folder_remove(&l, 0);
    gamelib_folder_remove(&l, 5);
    CHECK(l.nfolders == 0, "a folder removed");
    gamelib_free(&l);
    printf("gamelib_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
