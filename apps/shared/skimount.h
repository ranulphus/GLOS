/* The skiing game's mountains (GLOS v1, phase F; docs/v1/apps/skiing.md): a mountain made from a number by
   rule, the same number always the same mountain, a piece at a time as the skier goes down: nothing of it is
   kept. Whole numbers only, so that every build makes the same mountain. The slope is seen from above: x is
   across it (0 to SKI_WORLD_W), y is down it (0 the summit), both in pixels. */
#ifndef SKIMOUNT_H
#define SKIMOUNT_H

#define SKI_WORLD_W 960                 /* the slope's width: the piste wanders in it, forest at both sides */
#define SKI_CHUNK   128                 /* the mountain is made in pieces this long */

enum { SKI_GREEN, SKI_BLUE, SKI_RED, SKI_BLACK };       /* the grades, as on a real mountain */
enum { SKI_SLALOM, SKI_FREE };                          /* with gates, or without */

enum {                                  /* what is on it */
    SKI_T_FIR = 1, SKI_T_PINE,          /* trees */
    SKI_T_ROCK,
    SKI_T_FENCE,                        /* a post of a fence (three in a row make one) */
    SKI_T_RAMP,                         /* taken fast, it throws the skier into the air */
    SKI_T_MOGUL,                        /* a bump: slows, or at speed a small jump */
    SKI_T_HUT,                          /* the summit hut */
    SKI_T_GATE_L, SKI_T_GATE_R,         /* a gate's two poles (nothing to hit: they give) */
    SKI_T_FINISH_L, SKI_T_FINISH_R,     /* the banner's posts at the valley station */
    SKI_T_PYLON                         /* (phase F) a chair lift's pylon, in the powder at one side of the piste */
};

enum { SKI_SNOW_PISTE, SKI_SNOW_POWDER, SKI_SNOW_ICE, SKI_SNOW_DEEP };

struct ski_thing {
    short kind;                         /* SKI_T_ */
    short r;                            /* how near its middle a skier may not come (0: nothing to hit) */
    int x, y;                           /* its foot */
};

struct skimount {
    unsigned seed;
    int grade, mode;
    int length;                         /* the finish's y */
    int ngates, gate_first, gate_gap, gate_half;        /* slalom: the gates' places and half their width */
};

void skimount_init(struct skimount *m, unsigned seed, int grade, int mode);
int skimount_centre(const struct skimount *m, int y);   /* the piste's middle at y */
int skimount_half(const struct skimount *m, int y);     /* half the piste's width there */
int skimount_line(const struct skimount *m, int y);     /* a way down that nothing stands in: through every gate */
int skimount_snow(const struct skimount *m, int x, int y);     /* SKI_SNOW_ */
/* Piece `index`'s patch of ice, for the picture: 1 if it has one (an ellipse: its middle and half widths). */
int skimount_ice(const struct skimount *m, int index, int *cx, int *cy, int *rx, int *ry);
/* Piece `index` (y from index * SKI_CHUNK): its things, sorted by y; how many (room 48 holds any piece). */
int skimount_chunk(const struct skimount *m, int index, struct ski_thing *out, int room);
int skimount_gate(const struct skimount *m, int k, int *x, int *y);    /* gate k's middle: 1, or 0 if there is none */
/* (phase F) The chair lift: it runs up beside the piste, in the middle of the powder at one side (the mountain's
   own), a pylon every SKI_LIFT_PIECES pieces at SKI_LIFT_Y into the piece. Its x at y. */
#define SKI_LIFT_PIECES 3
#define SKI_LIFT_Y      64
int skimount_lift(const struct skimount *m, int y);
unsigned skimount_day(int year, int month, int day);    /* the number of that day's mountain */

#endif
