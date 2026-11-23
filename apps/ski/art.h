/* The skiing game's pictures (apps/ski/art.c): made by rule when the game starts, not kept in a file. Each is a
   few one-colour shapes laid one over another, so that they draw the same at 8, 16 and 32 bits and nothing
   round them is covered. */
#ifndef SKI_ART_H
#define SKI_ART_H

#include <glos/gfx.h>

struct art_layer {
    unsigned rgb;
    unsigned char *bits;                /* h rows of `stride` bytes, the leftmost pixel the highest bit */
};

struct art {
    int w, h, stride;
    int ox, oy;                         /* the place in it that is drawn at the thing's own place (its foot) */
    int n;
    struct art_layer l[8];
};

enum { ART_FIR, ART_PINE, ART_ROCK, ART_POST, ART_NET, ART_RAMP, ART_MOGUL, ART_HUT, ART_GATE_L, ART_GATE_R, ART_FINISH,
       ART_SHADOW, ART_FALLEN, ART_PYLON, ART_CHAIR, ART_CHAIR_SHADOW, ART_HARE_R, ART_HARE_L, ART_GROOMER_R, ART_GROOMER_L,
       ART_COUNT };
/* (phase F: the chair lift's pylon and a chair, at the cable's height, and its shadow on the snow; the mountain
   hare, running rightwards and leftwards) */
enum { POSE_SKI, POSE_TUCK, POSE_PLOUGH, POSE_COUNT };

extern struct art art_thing[ART_COUNT];
extern struct art art_skier[POSE_COUNT][32];    /* by pose, and by where the skis point: 32nds of a turn, clockwise
                                                   from straight down the slope */
#define OTHER_JACKETS 3
extern struct art art_other[OTHER_JACKETS][32]; /* (phase F) the other skiers: skiers, in jackets of their own */

enum { FIGURE_SKIER, FIGURE_RACER, FIGURE_BOARDER, FIGURE_SITTER, FIGURE_COUNT };      /* (the owner's: 2026-11-22) */

void art_make(int figure, unsigned jacket);     /* every picture (again, for another figure or jacket) */
void art_draw(struct gfx_surface *s, const struct art *a, int x, int y);
void art_draw_plain(struct gfx_surface *s, const struct art *a, int x, int y, unsigned rgb);   /* its shape, in one colour */

#endif
