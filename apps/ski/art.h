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
       ART_SHADOW, ART_FALLEN, ART_COUNT };
enum { POSE_SKI, POSE_TUCK, POSE_PLOUGH, POSE_COUNT };

extern struct art art_thing[ART_COUNT];
extern struct art art_skier[POSE_COUNT][32];    /* by pose, and by where the skis point: 32nds of a turn, clockwise
                                                   from straight down the slope */

void art_make(unsigned jacket);         /* every picture (again, for another jacket) */
void art_draw(struct gfx_surface *s, const struct art *a, int x, int y);
void art_draw_plain(struct gfx_surface *s, const struct art *a, int x, int y, unsigned rgb);   /* its shape, in one colour */

#endif
