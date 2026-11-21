/* The hovercraft game's arenas (GLOS v1, phase F; docs/v1/apps/hovercraft.md): an arena is a text file, a
   grid of squares, a letter a square, with a few lines above it. Read here, and held to its rules: a file with
   a square that cannot be read, no dock for a side, or a place nobody can reach or leave is refused, with the
   line said.

       name: The Old Basin
       light: day

       ################
       #A....1111....B#
       #A....>11<....B#
       #..*...~~...*..#
       ################

   The letters:
       #        a wall
       .        floor
       1 2      floor one and two levels up (a quay, a deck)
       ^ v < >  a ramp, rising one level the way it points, from the level of the square behind it
       ~        water: a craft pushed in is put back at its dock
       A B      a side's dock: where its craft start, and where its flag stands
       *        a beacon's place
       !        the lamp's place
       a to h   a lap's buoys, in their order
   A craft goes from a square to the next one if that is no higher (a drop is taken), or up a ramp. */
#ifndef HVARENA_H
#define HVARENA_H

#include <stddef.h>

#define HV_GRID 48                      /* squares a side at most */
#define HV_SPOTS 16

enum { HV_WALL, HV_FLOOR, HV_RAMP, HV_WATER };
enum { HV_N, HV_E, HV_S, HV_W };        /* a ramp's way up: towards the grid's top, right, bottom, left */

struct hv_square {
    unsigned char kind;                 /* HV_ */
    unsigned char level;                /* a floor's level; a ramp's lower end's */
    unsigned char way;                  /* a ramp: HV_N ... */
    unsigned char mark;                 /* what stands there: 0, or its letter ('A', 'B', '*', '!', 'a'...) */
};

struct hv_spot { unsigned char x, y; };

struct hv_arena {
    char name[40];
    int light;                          /* 0 day, 1 dusk, 2 night */
    int w, h;
    struct hv_square sq[HV_GRID][HV_GRID];      /* [y][x] */
    struct hv_spot dock[2][HV_SPOTS];   /* each side's dock squares */
    int ndock[2];
    struct hv_spot beacon[HV_SPOTS];
    int nbeacon;
    struct hv_spot lamp;                /* the lamp's place (the arena's middle floor square if it names none) */
    struct hv_spot buoy[8];             /* a lap's buoys, in order */
    int nbuoy;
};

/* An arena read from a file's text: 0, or -1 and why (a line: "line 7: ..."). */
int hv_arena_read(struct hv_arena *a, const char *text, size_t n, char *why, int room);
/* Can a craft go from square (x, y) to the next one towards `way`? */
int hv_arena_step(const struct hv_arena *a, int x, int y, int way);
/* How far every square is from a place, in squares gone through (the ways a craft can go, towards the place):
   dist[y][x], 0xFFFF for a square the place cannot be reached from. */
void hv_arena_distances(const struct hv_arena *a, int x, int y, unsigned short dist[HV_GRID][HV_GRID]);

#endif
