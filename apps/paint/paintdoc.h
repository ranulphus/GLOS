/* PAINT's picture and what its tools do to it (paintdoc.c): no window in it, so that the host tests each tool
   against pictures written out as text (tests/host/paint_test.c).

   A picture is layers, the bottom one first; a layer's pixels are AARRGGBBh (FFh: opaque). What shows is the
   layers one over another on a chequered ground, kept as `flat` (00RRGGBBh, for the toolkit's image view) and
   made again for the part a tool has changed (pd_flatten). The tools draw on the current layer. */
#ifndef PAINTDOC_H
#define PAINTDOC_H

#define PD_LAYERS 16
#define PD_MAX 4096                     /* a side's most pixels */

struct pd_layer {
    unsigned *px;
    char name[24];
    unsigned char visible, opacity;     /* opacity: 255 all of it */
};

struct pd_doc {
    int w, h, n, cur;
    struct pd_layer layer[PD_LAYERS];
    unsigned *flat;
    int x0, y0, x1, y1;                 /* what the tools have changed since pd_clean: x0 <= x < x1; none: x1 <= x0 */
};

struct pd_doc *pd_new(int w, int h, unsigned paper);    /* one layer of that colour (00RRGGBBh); NULL: no memory */
struct pd_doc *pd_from(unsigned *px, int w, int h);     /* a picture read (00RRGGBBh, taken over) as its one layer */
void pd_free(struct pd_doc *d);
void pd_flatten(struct pd_doc *d, int x0, int y0, int x1, int y1);      /* `flat` made again there */
void pd_clean(struct pd_doc *d);        /* nothing changed, from here */

/* The tools: colours are 00RRGGBBh, places are pixels (any, clipped), size is a round tip's width (1 a pixel). */
void pd_dot(struct pd_doc *d, int x, int y, unsigned colour, int size);
void pd_line(struct pd_doc *d, int xa, int ya, int xb, int yb, unsigned colour, int size);
/* A rectangle and an ellipse between two corners (either way round), an outline `size` wide (0: none) in
   colour, filled with `fill` unless that is PD_NONE. */
#define PD_NONE 0xFF000000u
#define PD_CLEAR 0xFE000000u            /* as a colour: the pixels made clear (an eraser's on a layer over another) */
void pd_rect(struct pd_doc *d, int xa, int ya, int xb, int yb, unsigned colour, int size, unsigned fill);
void pd_ellipse(struct pd_doc *d, int xa, int ya, int xb, int yb, unsigned colour, int size, unsigned fill);
/* The pixels that touch (x, y) and are within `tolerance` of its colour in every channel (0: the same) take
   colour: how many did. */
long pd_fill(struct pd_doc *d, int x, int y, unsigned colour, int tolerance);
unsigned pd_get(const struct pd_doc *d, int x, int y);  /* what shows there (00RRGGBBh); outside: 0 */

/* Layers. A new one is clear, over the current one, and current; a copy likewise. Delete takes the current one
   away (not the only one). Move changes it over with the one above (up) or under it. Merge puts the current one
   down onto the one under it, as it shows, and takes it away. 0, or -1 with nothing changed. */
int pd_layer_add(struct pd_doc *d);
int pd_layer_copy(struct pd_doc *d);
int pd_layer_delete(struct pd_doc *d);
int pd_layer_move(struct pd_doc *d, int up);
int pd_layer_merge(struct pd_doc *d);
void pd_layer_set(struct pd_doc *d, int i, int visible, int opacity);  /* shown or not; 0 to 255 (-1: as it is) */
/* The picture as it shows, over a plain ground and not the chequered one (to save it): out is w by h pixels. */
void pd_render(const struct pd_doc *d, unsigned *out, unsigned paper);

/* The whole picture (every layer) turned a quarter right (PD_RIGHT) or left, or flipped across or over. 0, or
   -1 with nothing changed (no memory). pd_turn_back: the turn that undoes one. */
enum { PD_RIGHT = 1, PD_LEFT, PD_ACROSS, PD_OVER };
int pd_turn(struct pd_doc *d, int how);
int pd_turn_back(int how);

/* The whole picture another size. pd_resize: every layer scaled (smooth: each new pixel from the four old ones
   round where it falls; else from the nearest). pd_canvas: the picture as it is on a canvas nw by nh, at the
   place ax, ay say (0 the left or top, 1 the middle, 2 the right or bottom): what falls outside is cut off,
   what is new is `paper` on the bottom layer and clear on the others. 0, or -1 with nothing changed. */
int pd_resize(struct pd_doc *d, int nw, int nh, int smooth);
int pd_canvas(struct pd_doc *d, int nw, int nh, int ax, int ay, unsigned paper);
/* A part of the picture. pd_crop: the picture cut down to the rectangle (clipped to it; -1: nothing of it is
   in the picture). pd_copy: the current layer's pixels there (AARRGGBBh, malloc'd; the rectangle clipped, its
   size as taken in *w, *h; NULL: none). pd_paste: such pixels as a new layer over the current one, their
   corner at x, y (what falls outside is not kept), the rest of the layer clear. */
int pd_crop(struct pd_doc *d, int x, int y, int w, int h);
unsigned *pd_copy(const struct pd_doc *d, int x, int y, int *w, int *h);
int pd_paste(struct pd_doc *d, const unsigned *px, int w, int h, int x, int y);

/* The whole picture kept, for a step that changes all of it (a resize, a canvas, a layer added or taken away):
   pd_keep copies it, its layers and what they are; pd_swap changes what is kept and what the picture is over
   (undo, and redo again). */
struct pd_kept {
    int w, h, n, cur;
    struct pd_layer layer[PD_LAYERS];
};
struct pd_kept *pd_keep(const struct pd_doc *d);
int pd_swap(struct pd_doc *d, struct pd_kept *k);       /* 0, or -1 (no memory for what shows: nothing changed) */
void pd_kept_free(struct pd_kept *k);
unsigned pd_kept_bytes(const struct pd_kept *k);

/* A step to undo: the current layer's pixels in a rectangle, before and after. pd_before copies the layer (as
   a tool begins); pd_step makes the step of what the tools changed since pd_clean (NULL: nothing did, or no
   memory) and frees the copy; pd_apply puts the before (undo) or the after back and flattens. */
struct pd_step {
    int layer, x, y, w, h;
    unsigned *before, *after;
};
unsigned *pd_before(const struct pd_doc *d);
struct pd_step *pd_step(struct pd_doc *d, unsigned *before);
void pd_restore(struct pd_doc *d, const unsigned *before);      /* the copy put back where the tools changed (a
                                           shape being dragged out is drawn anew at each move); then clean */
void pd_apply(struct pd_doc *d, const struct pd_step *s, int redo);
void pd_step_free(struct pd_step *s);
unsigned pd_step_bytes(const struct pd_step *s);
#endif
