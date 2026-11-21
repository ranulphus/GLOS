/* Well-known DOS games, known by their files: for the Games Library (phase F; docs/v1/apps/games-library.md).
   GLOS's own list, written by hand: a game's title, its year, who made it, what kind it is, and the names of
   the program that runs it and of a file that lies beside it. (Names and years are facts; nothing of any game
   is here.) A folder is that game when the program and the companion are both in it. */
#ifndef GAMEDB_H
#define GAMEDB_H

struct game_known {
    const char *title;
    unsigned short year;
    const char *maker;
    const char *genre;                  /* one of a few words: Action, Adventure, Platform, Puzzle, Racing,
                                           Role-playing, Simulation, Sports, Strategy */
    const char *run;                    /* the program to run, as DOS names it (capitals) */
    const char *with;                   /* a file or a folder that must be beside it ("A|B": either); NULL: the
                                           program's name is enough */
    const char *setup;                  /* its setup program, if it is known to have one; NULL */
};

unsigned gamedb_count(void);
const struct game_known *gamedb_at(unsigned i);

/* The known game a folder holds, by the names of what is in it (n of them, files and folders, in any case of
   letters): the first in the list whose program and companion are both there; NULL: none. */
const struct game_known *gamedb_match(const char *const *names, unsigned n);

#endif
