/* Host test for a drag's states (kernel/wm/dnd.c; API 2.0 step 6). */
#include <stdio.h>
#include <string.h>
#include "dnd.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static struct dnd d;
static struct dnd_note o[DND_NOTES];

static int is(const struct dnd_note *n, int kind, u32 app, u32 win, u32 effect)
{
    return n->kind == kind && n->app == app && n->win == win && n->effect == effect;
}

int main(void)
{
    struct dnd_target a = { 2, 20, DND_COPY | DND_MOVE }, b = { 3, 30, DND_LINK }, c = { 3, 31, DND_MOVE };
    struct dnd_target none = { 4, 40, 0 };
    u32 n;

    /* Nothing under way: nothing happens. */
    CHECK(dnd_move(&d, &a, 1, 1, o) == 0 && dnd_end(&d, 1, o) == 0 && dnd_answer(&d, 2, 20, DND_COPY) == -1);
    CHECK(dnd_begin(&d, 1, 10, 0) == -1 && dnd_begin(&d, 0, 10, DND_COPY) == -1 && dnd_begin(&d, 1, 10, 8) == -1);

    /* A drag that allows copy and move: over nothing, into a target, about in it, out, into another. */
    CHECK(dnd_begin(&d, 1, 10, DND_COPY | DND_MOVE) == 0 && d.on);
    CHECK(dnd_begin(&d, 5, 50, DND_COPY) == -1);                        /* one at a time */
    CHECK(dnd_move(&d, 0, 5, 5, o) == 0);
    n = dnd_move(&d, &a, 10, 10, o);
    CHECK(n == 1 && is(&o[0], DND_ENTER, 2, 20, DND_COPY | DND_MOVE) && o[0].x == 10 && o[0].y == 10);
    CHECK(d.answer == DND_COPY);                                        /* until it says another */
    n = dnd_move(&d, &a, 12, 10, o);
    CHECK(n == 1 && is(&o[0], DND_OVER, 2, 20, DND_COPY | DND_MOVE) && o[0].x == 12);
    CHECK(dnd_move(&d, &a, 12, 10, o) == 0);                            /* not moved: nothing to tell */
    CHECK(dnd_answer(&d, 2, 20, DND_MOVE) == 0 && d.answer == DND_MOVE);
    CHECK(dnd_answer(&d, 2, 20, DND_LINK) == -1 && dnd_answer(&d, 2, 20, DND_COPY | DND_MOVE) == -1);  /* not allowed; two */
    CHECK(dnd_answer(&d, 2, 21, DND_COPY) == -1 && dnd_answer(&d, 3, 20, DND_COPY) == -1 && d.answer == DND_MOVE);
    n = dnd_move(&d, &b, 20, 20, o);                                    /* b takes links only: no target for this */
    CHECK(n == 1 && is(&o[0], DND_LEAVE, 2, 20, 0) && !d.t_app);
    CHECK(dnd_answer(&d, 3, 30, DND_LINK) == -1);
    n = dnd_move(&d, &c, 30, 30, o);                                    /* the same app's other window: move only */
    CHECK(n == 1 && is(&o[0], DND_ENTER, 3, 31, DND_COPY | DND_MOVE) && d.answer == DND_MOVE);
    n = dnd_move(&d, &a, 31, 30, o);                                    /* straight from one target into another */
    CHECK(n == 2 && is(&o[0], DND_LEAVE, 3, 31, 0) && is(&o[1], DND_ENTER, 2, 20, DND_COPY | DND_MOVE));
    CHECK(d.answer == DND_COPY);                                        /* (the answer isn't carried over) */

    /* The drop: the target is told the effect, and the source. */
    n = dnd_end(&d, 1, o);
    CHECK(n == 2 && is(&o[0], DND_DROP, 2, 20, DND_COPY) && is(&o[1], DND_DONE, 1, 10, DND_COPY) && !d.on);
    CHECK(o[0].x == 31 && o[0].y == 30);
    CHECK(dnd_move(&d, &a, 1, 1, o) == 0 && dnd_end(&d, 1, o) == 0);

    /* A drop where the target says no: it is told the drag left, the source that nothing was done. */
    CHECK(dnd_begin(&d, 1, 10, DND_COPY | DND_MOVE | DND_LINK) == 0);
    dnd_move(&d, &a, 10, 10, o);
    CHECK(dnd_answer(&d, 2, 20, 0) == 0);
    n = dnd_end(&d, 1, o);
    CHECK(n == 2 && is(&o[0], DND_LEAVE, 2, 20, 0) && is(&o[1], DND_DONE, 1, 10, 0));
    /* A drop over nothing, and over a window that is no target. */
    CHECK(dnd_begin(&d, 1, 10, DND_COPY) == 0);
    dnd_move(&d, &none, 3, 3, o);
    n = dnd_end(&d, 1, o);
    CHECK(n == 1 && is(&o[0], DND_DONE, 1, 10, 0));
    /* Given up (Esc) over a willing target. */
    CHECK(dnd_begin(&d, 1, 10, DND_LINK) == 0);
    n = dnd_move(&d, &b, 9, 9, o);
    CHECK(n == 1 && is(&o[0], DND_ENTER, 3, 30, DND_LINK) && d.answer == DND_LINK);
    n = dnd_end(&d, 0, o);
    CHECK(n == 2 && is(&o[0], DND_LEAVE, 3, 30, 0) && is(&o[1], DND_DONE, 1, 10, 0));

    /* The target takes other effects than it did (its TARGET call during the drag). */
    CHECK(dnd_begin(&d, 1, 10, DND_COPY | DND_MOVE) == 0);
    dnd_move(&d, &a, 10, 10, o);
    dnd_answer(&d, 2, 20, DND_MOVE);
    a.effects = DND_COPY;
    n = dnd_move(&d, &a, 11, 10, o);
    CHECK(n == 1 && o[0].kind == DND_OVER && d.answer == DND_COPY);     /* (move is no longer its to answer) */
    a.effects = DND_COPY | DND_MOVE;

    /* The target's window goes: as if the pointer had left; its app goes; the source goes. */
    CHECK(dnd_gone(&d, 2, 21, o) == 0 && d.t_app == 2);                 /* (another window of its) */
    CHECK(dnd_gone(&d, 2, 20, o) == 0 && !d.t_app && d.on);
    n = dnd_end(&d, 1, o);
    CHECK(n == 1 && is(&o[0], DND_DONE, 1, 10, 0));
    CHECK(dnd_begin(&d, 1, 10, DND_COPY) == 0);
    dnd_move(&d, &a, 10, 10, o);
    CHECK(dnd_gone(&d, 2, 0, o) == 0 && !d.t_app && d.on);
    dnd_move(&d, &a, 10, 10, o);
    n = dnd_gone(&d, 1, 0, o);                                          /* the source: the target is told it left */
    CHECK(n == 1 && is(&o[0], DND_LEAVE, 2, 20, 0) && !d.on);
    CHECK(dnd_begin(&d, 1, 10, DND_COPY) == 0);
    CHECK(dnd_gone(&d, 1, 11, o) == 0 && d.on && dnd_gone(&d, 1, 10, o) == 0 && !d.on);

    printf("dnd_test: %d failures\n", fails);
    return fails != 0;
}
