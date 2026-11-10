/* A drag's states (API 2.0's DND; docs/v1/phase-b.md step 6): from the source's DRAG, over windows that take
 * what is offered or don't, to the drop or the cancel. This says who is told what; the desktop's input feeds it
 * the pointer (kernel/wm/deskin.c) and the API sends the notes as events (kernel/app/api2clip.c). Built for the
 * host too (tests/host/dnd_test.c). */
#ifndef K_DND_H
#define K_DND_H
#include "types.h"

#define DND_COPY 1u                     /* the effects (GLOS_DND_COPY, MOVE, LINK) */
#define DND_MOVE 2u
#define DND_LINK 4u

enum { DND_ENTER = 1, DND_OVER, DND_LEAVE, DND_DROP, DND_DONE };

struct dnd_note {                       /* what one app is to be told */
    u8 kind;                            /* DND_ */
    u32 app, win;                       /* whom: the target's window; for DND_DONE the source's */
    s32 x, y;                           /* the pointer on the screen */
    u32 effect;                         /* ENTER, OVER: what the source allows; DROP, DONE: the effect done (DONE 0:
                                           nothing was dropped) */
};
#define DND_NOTES 3                     /* one call makes no more */

struct dnd_target {                     /* the window under the pointer, as a target */
    u32 app, win;
    u32 effects;                        /* those it takes for what is offered (0: it is no target for this) */
};

struct dnd {
    u8 on;
    u32 src_app, src_win;
    u32 allowed;                        /* the effects the source allows */
    u32 t_app, t_win;                   /* the target under the pointer (t_app 0: none) */
    u32 t_effects;                      /* ... and the effects it could have (its own, of those allowed) */
    u32 answer;                         /* ... and the one a drop there would have now (0: none) */
    s32 x, y;
};

int dnd_begin(struct dnd *d, u32 app, u32 win, u32 allowed);    /* 0, or -1: one is under way, or nothing allowed */
/* The pointer is at x, y over t (NULL, or one that takes nothing: no target): the notes. */
u32 dnd_move(struct dnd *d, const struct dnd_target *t, s32 x, s32 y, struct dnd_note *out);
/* The target says what a drop where the pointer is would do (one effect of those it could have, or 0): 0, or
   -1 if that window isn't the drag's target or the effect isn't one. */
int dnd_answer(struct dnd *d, u32 app, u32 win, u32 effect);
/* The button was let go (drop 1) or the drag given up (0): the notes; the drag is over. */
u32 dnd_end(struct dnd *d, int drop, struct dnd_note *out);
/* An app (win 0), or one window of it, is no more: the notes. The source's going ends the drag. */
u32 dnd_gone(struct dnd *d, u32 app, u32 win, struct dnd_note *out);

#endif
