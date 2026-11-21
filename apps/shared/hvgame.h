/* The hovercraft game's truth (GLOS v1, phase F; docs/v1/apps/hovercraft.md): the craft's motion, their bumps,
   and the four kinds of game, in fixed steps (thirty a second) from every craft's controls at each step and
   nothing else. Whole numbers only: a game is its arena and its controls, the same on every build and on every
   PC of a network game. Also the computer's driving (hv_auto): the controls a craft's driver of a given
   strength would hold now.

   Places are in 1/256 of a square, x rightwards and y down the grid; speeds in 1/65536 of a square a step; a
   heading is 0 to 1023, 0 towards the grid's top, 256 to its right. */
#ifndef HVGAME_H
#define HVGAME_H

#include "hvarena.h"

#define HV_STEPS  30                    /* steps a second */
#define HV_CRAFT  8
#define HV_RADIUS 80                    /* a craft's, in 1/256 square */
#define HV_TANK   90                    /* the boost's tank: steps of boost when full */

/* A craft's controls at a step. */
#define HV_C_FWD   1
#define HV_C_BACK  2
#define HV_C_LEFT  4                    /* turn */
#define HV_C_RIGHT 8
#define HV_C_SLIDE_L 16                 /* the side fans */
#define HV_C_SLIDE_R 32
#define HV_C_BOOST 64
#define HV_C_DOWN  128                  /* set down on the skirt: stops, and cannot be pushed */

enum { HV_FLAGS, HV_BEACONS, HV_LAMP, HV_LAPS };

/* What happened in a step (bits), for the sounds and the words on the screen. */
#define HV_EV_BUMP   1                  /* craft against craft */
#define HV_EV_WALL   2
#define HV_EV_SPLASH 4                  /* a craft in the water */
#define HV_EV_TAKEN  8                  /* a flag or the lamp taken */
#define HV_EV_DROP   16                 /* a flag dropped */
#define HV_EV_SCORE  32                 /* a flag brought home; a lap made */
#define HV_EV_BEACON 64                 /* a beacon turned */

struct hv_craft {
    int side;                           /* 0 or 1: its dock, and its side in the games that have sides */
    int x, y;                           /* 1/256 square */
    int vx, vy;                         /* 1/65536 square a step */
    int heading;                        /* 0 to 1023 */
    int tank;                           /* the boost left */
    int down;                           /* set down */
    int wet;                            /* steps until it is put back at its dock (0: it is in the game) */
    int carrying;                       /* the other side's flag, or the lamp */
    int numb;                           /* steps left in which it takes no flag: it has just had one knocked loose */
    long score;                         /* the lamp's seconds; laps */
    int buoy;                           /* laps: the next buoy */
    unsigned lap_start, best_lap;       /* laps: the step this lap began; the best lap's steps (0: none yet) */
};

struct hv_flag {
    int x, y;                           /* where it is when nobody carries it */
    int holder;                         /* the craft that carries it, or -1 */
    int away;                           /* dropped: steps until it goes home by itself (0: it is at home) */
};

struct hv_game {
    const struct hv_arena *a;
    int kind, ncraft;
    struct hv_craft c[HV_CRAFT];
    unsigned steps;
    int score[2];                       /* the sides' (flags, beacons) */
    struct hv_flag flag[2];             /* [side]: that side's own flag */
    signed char beacon[HV_SPOTS];       /* whose each beacon is: -1 nobody's */
    int lamp_x, lamp_y, lamp_holder;    /* the lamp: where, and who has it (-1: nobody) */
    unsigned ev;                        /* HV_EV_ of the last step */
    /* (the computer's driving: how far every square is from the places its craft go for; made when first
       wanted, and again when a place has moved to another square) */
    struct { int x, y; unsigned short d[HV_GRID][HV_GRID]; } way[12];
    unsigned way_next;
};

/* A game begun: the craft at their docks (craft 0, 2, 4, 6 side A; 1, 3, 5, 7 side B). 0; -1: the arena has not
   what the kind needs (a lap wants two buoys, beacons at least one), or ncraft is not 1 to 8. */
int hv_game_start(struct hv_game *g, const struct hv_arena *a, int kind, int ncraft);
void hv_game_step(struct hv_game *g, const unsigned char *controls);    /* controls[ncraft] */
/* The controls the computer would hold for a craft: strength 0 (mild) to 2 (keen). */
unsigned hv_auto(struct hv_game *g, int craft, int strength);
int hv_sin(int heading);                /* 16384ths; hv_sin(256) is 16384 */
int hv_cos(int heading);
int hv_craft_level(const struct hv_game *g, int craft);         /* its height off the lowest floor: 256ths of a level */

#endif
