/* The Games Library's games (phase F; docs/v1/apps/games-library.md): the list kept in a file, the folders gone
   through for games, and what is counted as they are played. No GUI here: one code for the app and for the
   host's tests. */
#ifndef GAMELIB_H
#define GAMELIB_H

struct game {
    unsigned long id;                   /* its own, for as long as it is in the library (never 0) */
    char title[80];                     /* UTF-8 */
    char dir[128];                      /* its folder (DOS's bytes) */
    char run[16];                       /* the program in it that runs it */
    char setup[16];                     /* its setup program, or "" */
    char maker[48], genre[24];          /* "" where not known */
    unsigned short year;                /* 0: not known */
    char picture[128];                  /* its picture's file, or "" */
    char notes[200];                    /* the user's (one line) */
    unsigned char favourite;
    unsigned char by_hand;              /* added, or changed, by the user: a scan leaves its title and program */
    unsigned char gone;                 /* its program was not there at the last scan */
    unsigned long played;               /* seconds, all told */
    unsigned long last;                 /* when it was last played: seconds since 1970, local time (0: never) */
    unsigned times;                     /* how often */
};

#define GAMELIB_FOLDERS 8
struct gamelib {
    struct game *g;
    unsigned n, room;
    unsigned long next_id;
    char folder[GAMELIB_FOLDERS][128];  /* the folders that are gone through for games */
    unsigned nfolders;
    int changed;                        /* since it was read or written */
};

void gamelib_init(struct gamelib *l);
void gamelib_free(struct gamelib *l);

/* The library's file: lines of text. Read: 0; -1: no such file (the library is empty); what can't be made out
   is passed over. Written whole: 0, or -1. `open` is fopen's like (NULL: fopen). */
int gamelib_read(struct gamelib *l, const char *path, void *(*open)(const char *, const char *));
int gamelib_write(struct gamelib *l, const char *path, void *(*open)(const char *, const char *));

/* What a folder holds, for a scan: the caller's (the app reads the disk; a test has a made-up tree). */
struct gamelib_entry {
    char name[64];
    unsigned long size;
    unsigned char dir;
};
/* dir's entries, malloc'd, *out (freed by the scan): how many; -1: it can't be read. */
typedef int (*gamelib_list_fn)(void *ctx, const char *dir, struct gamelib_entry **out);

/* A folder of games gone through: each folder in it is a game if it holds a known game's files (gamedb.c), or
   a program that looks like one (named as its folder, or the only one, or the largest: set-up programs and
   the like apart); a folder with none but with folders is looked into, once. A game found that the library has
   (by its folder) keeps what the user gave it and what was counted; one whose program has gone is marked.
   How many were added; -1: the folder can't be read. */
int gamelib_scan(struct gamelib *l, const char *folder, gamelib_list_fn list, void *ctx);

/* A game added by hand (its folder, its program, a title: "" for the folder's name): it, or NULL. One that is
   there (the same folder and program) is given back. */
struct game *gamelib_add(struct gamelib *l, const char *dir, const char *run, const char *title);
void gamelib_remove(struct gamelib *l, unsigned long id);
struct game *gamelib_find(struct gamelib *l, unsigned long id);
int gamelib_folder_add(struct gamelib *l, const char *folder);  /* 0; -1: no room, or it is there */
void gamelib_folder_remove(struct gamelib *l, unsigned i);

/* A game was played: for so many seconds, ending at `when` (seconds since 1970, local time). */
void gamelib_played(struct gamelib *l, struct game *g, unsigned long seconds, unsigned long when);

/* The games in an order, into ids (room for l->n): by title (0), by when last played, the latest first (1), by
   time played, the most first (2); only favourites (flags & 1); only those whose title, maker or genre has
   `find` in it (any case; "" or NULL: all). How many. */
unsigned gamelib_order(const struct gamelib *l, int by, unsigned flags, const char *find, unsigned long *ids);

#endif
