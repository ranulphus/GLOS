/* The skiing game's runs (GLOS v1, phase F; docs/v1/apps/skiing.md): the skier's motion down a mountain
   (skimount.h) in fixed steps, sixty a second, from the keys held at each step and nothing else: a run is its
   mountain's number and its keys. Whole numbers only: the same run ends at the same step with the same score
   on every build, which is what the demonstration, a best run skied again beside you, and the tests rest on. */
#ifndef SKIRUN_H
#define SKIRUN_H

#include "skimount.h"

#define SKI_STEPS 60                    /* steps a second */

/* The keys held at a step. */
#define SKI_K_LEFT   1
#define SKI_K_RIGHT  2
#define SKI_K_TUCK   4                  /* crouched: faster, and slower to turn */
#define SKI_K_PLOUGH 8                  /* the skis' tips together: brakes */
#define SKI_K_WIND   16                 /* (phase F) not a key: the wind blows at this step. Gusts come by rule from
                                           the mountain's number and the step, so that a windy run is still its
                                           mountain and its keys (a recording keeps when the wind blew) */

enum { SKI_S_SKIING, SKI_S_AIR, SKI_S_FALLEN, SKI_S_DONE };

/* What happened in a step (for the sounds and the words on the screen): bits. */
#define SKI_EV_GATE   1                 /* through a gate */
#define SKI_EV_MISS   2                 /* past one */
#define SKI_EV_JUMP   4                 /* off the ground */
#define SKI_EV_LAND   8                 /* and down again, upright */
#define SKI_EV_FALL   16
#define SKI_EV_NEAR   32                /* past something by a hand's breadth */
#define SKI_EV_FINISH 64

#define SKI_MISS_STEPS (5 * SKI_STEPS)  /* a gate missed costs five seconds */
#define SKI_GUST_STEPS (5 * SKI_STEPS)  /* the wind (phase F): a spell of five seconds, a gust in its first half or calm */

#define SKI_PIECES 8
#define SKI_OTHERS 96                   /* (phase F) the pieces that may have another skier: 12,288 pixels of mountain */
struct ski_piece {                      /* a piece of the mountain as made (skimount_chunk), kept while the skier is near */
    int index, n;
    struct ski_thing t[48];
};

struct skirun {
    struct skimount m;
    int state;                          /* SKI_S_ */
    int x, y;                           /* where the skier is: 1/256 pixel */
    int vx, vy;                         /* how far a step takes them */
    int heading;                        /* -8 (across the slope, leftwards) .. 0 (straight down) .. 8 */
    int turn_wait;                      /* steps before the next turn of the skis */
    int air, air_total;                 /* steps left in the air, and of the whole jump */
    int spin;                           /* turned in the air: sixteenths of a turn, rightwards */
    int down;                           /* steps left on the ground after a fall */
    int grace;                          /* steps left, once up again, in which nothing is a fall (so that the
                                           thing just hit is skied away from, not hit again) */
    unsigned steps;                     /* since the start */
    unsigned finish_steps;              /* the step the finish was crossed */
    int next_gate, gates_passed, gates_missed;
    int falls, jumps, turns;            /* turns: half turns landed */
    long score;                         /* a free run's points */
    int run_acc, streak;                /* distance towards the next points; hundreds of pixels without a fall */
    int near[4], near_at;               /* the things last passed close by (not counted twice) */
    unsigned ev;                        /* SKI_EV_ of the last step */
    int wind;                           /* the last step's SKI_K_WIND: gusts pushed across the slope */
    struct ski_piece piece[SKI_PIECES]; /* (skirun_piece's) */
    struct {                            /* (phase F) another skier a piece, at most: worked out when first asked for */
        short y0;                       /* where it waits (its lane's top) */
        signed char side;               /* the clear line's side its lane is on */
        unsigned char known, has;       /* worked out; there is one */
        unsigned char speed;            /* 256ths of a pixel a step, from 128 */
        unsigned short start;           /* the step it set off (0: not yet: it waits for the skier to come near) */
    } other[SKI_OTHERS];
};

void skirun_start(struct skirun *r, unsigned seed, int grade, int mode);
void skirun_step(struct skirun *r, unsigned keys);
/* A piece of the run's mountain and how many things it has: made once and kept while it is among the last
   few asked for (the motion asks for those round the skier each step, the picture for those in sight). */
const struct ski_thing *skirun_piece(struct skirun *r, int index, int *n);
/* The keys a careful skier would hold now: down the mountain's clear line, braking when too fast. The
   demonstration's skier, and the tests'. */
unsigned skirun_auto(const struct skirun *r);
int skirun_speed(const struct skirun *r);       /* pixels a second */
/* (phase F) Piece c's other skier, if it has one: a slow skier who zigzags down a lane of its own beside the clear
   line (never on it, nothing in its way: chosen so when the mountain is made), setting off once the skier is
   within 400 pixels above it, and stopping at the side at its lane's end. 1 and where it is (pixels), whether it
   moves, its heading (32nds of a turn, as the skier's); 0: none. Skied into, it is a fall (flown over, nothing). */
int skirun_other(struct skirun *r, int c, int *x, int *y, int *moving, int *heading);
/* The wind's push across the slope at this step: 1/256 pixel a step, rightwards (0: calm, or no wind). */
int skirun_gust(const struct skirun *r);
int skirun_height(const struct skirun *r);      /* in the air: pixels off the ground */
/* The run's time, with its missed gates' seconds: milliseconds (once done; before: so far). */
unsigned long skirun_ms(const struct skirun *r);

#endif
