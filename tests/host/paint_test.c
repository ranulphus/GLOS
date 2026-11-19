/* PAINT's picture and tools (apps/paint/paintdoc.c) on the host: each tool on a small picture, which is then
   written out as text ('.' paper, '#' the first colour, 'o' the second, '?' anything else) and compared with
   what it should be; a flood's count; steps undone and done again; layers one over another. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/paint/paintdoc.h"

#define PAPER 0xFFFFFFu
#define INK 0x000000u
#define RED 0xFF0000u

static int bad;

static void show(const struct pd_doc *d, char *out)
{
    int x, y;
    for (y = 0; y < d->h; y++) {
        for (x = 0; x < d->w; x++) {
            unsigned c = pd_get(d, x, y);
            *out++ = c == PAPER ? '.' : c == INK ? '#' : c == RED ? 'o' : '?';
        }
        *out++ = '\n';
    }
    *out = 0;
}

static void check(const char *name, struct pd_doc *d, const char *want)
{
    char got[4096];
    pd_flatten(d, 0, 0, d->w, d->h);
    show(d, got);
    if (strcmp(got, want)) {
        printf("paint_test: %s:\n%swanted:\n%s", name, got, want);
        bad++;
    }
}

static void is(const char *name, long got, long want)
{
    if (got != want) {
        printf("paint_test: %s: %ld, wanted %ld\n", name, got, want);
        bad++;
    }
}

int main(void)
{
    struct pd_doc *d;
    unsigned *before;
    struct pd_step *s;

    d = pd_new(8, 5, PAPER);                    /* dots: one pixel, and tips two, three and four wide */
    pd_dot(d, 0, 0, INK, 1);
    pd_dot(d, 3, 1, INK, 2);
    pd_dot(d, 6, 3, INK, 3);
    check("dots", d,
          "#.##....\n"
          "..##....\n"
          "......#.\n"
          ".....###\n"
          "......#.\n");
    is("dots changed x0", d->x0, 0);
    is("dots changed x1", d->x1, 8);
    is("dots changed y1", d->y1, 5);
    pd_free(d);

    d = pd_new(9, 9, PAPER);
    pd_dot(d, 4, 4, INK, 5);
    check("a tip five wide", d,
          ".........\n"
          ".........\n"
          "...###...\n"
          "..#####..\n"
          "..#####..\n"
          "..#####..\n"
          "...###...\n"
          ".........\n"
          ".........\n");
    pd_free(d);

    d = pd_new(8, 6, PAPER);                    /* lines: level, upright, slanting both ways, off the picture */
    pd_line(d, 0, 0, 7, 0, INK, 1);
    pd_line(d, 0, 2, 0, 5, INK, 1);
    pd_line(d, 2, 2, 5, 5, RED, 1);
    pd_line(d, 7, 2, 4, 5, INK, 1);
    pd_line(d, -5, -5, -1, -1, RED, 1);
    pd_line(d, 6, 5, 20, 5, RED, 1);
    check("lines", d,
          "########\n"
          "........\n"
          "#.o....#\n"
          "#..o..#.\n"
          "#...o#..\n"
          "#...#ooo\n");
    pd_free(d);

    d = pd_new(8, 4, PAPER);
    pd_line(d, 1, 1, 6, 2, INK, 1);
    check("a shallow line", d,
          "........\n"
          ".###....\n"
          "....###.\n"
          "........\n");
    pd_free(d);

    d = pd_new(10, 7, PAPER);                   /* rectangles: an outline, a thick one filled, corners the other way */
    pd_rect(d, 0, 0, 3, 2, INK, 1, PD_NONE);
    pd_rect(d, 9, 6, 4, 1, INK, 2, RED);
    check("rectangles", d,
          "####......\n"
          "#..#######\n"
          "##########\n"
          "....##oo##\n"
          "....##oo##\n"
          "....######\n"
          "....######\n");
    pd_free(d);

    d = pd_new(6, 4, PAPER);
    pd_rect(d, 1, 1, 4, 2, INK, 0, RED);
    check("a rectangle filled, no outline", d,
          "......\n"
          ".oooo.\n"
          ".oooo.\n"
          "......\n");
    pd_free(d);

    d = pd_new(11, 7, PAPER);                   /* ellipses */
    pd_ellipse(d, 0, 0, 10, 6, INK, 1, PD_NONE);
    check("an ellipse", d,
          "...#####...\n"
          ".##.....##.\n"
          "#.........#\n"
          "#.........#\n"
          "#.........#\n"
          ".##.....##.\n"
          "...#####...\n");
    pd_free(d);

    d = pd_new(9, 9, PAPER);
    pd_ellipse(d, 8, 8, 0, 0, INK, 2, RED);
    check("a circle, a thick outline, filled", d,
          "..#####..\n"
          ".#######.\n"
          "###ooo###\n"
          "##ooooo##\n"
          "##ooooo##\n"
          "##ooooo##\n"
          "###ooo###\n"
          ".#######.\n"
          "..#####..\n");
    pd_free(d);

    d = pd_new(8, 6, PAPER);                    /* floods: inside a rectangle, then the paper round it */
    pd_rect(d, 1, 1, 6, 4, INK, 1, PD_NONE);
    pd_clean(d);
    is("a flood's pixels", pd_fill(d, 3, 3, RED, 0), 8);
    is("the flood changed x0", d->x0, 2);
    is("the flood changed x1", d->x1, 6);
    check("a flood inside", d,
          "........\n"
          ".######.\n"
          ".#oooo#.\n"
          ".#oooo#.\n"
          ".######.\n"
          "........\n");
    is("the same colour again", pd_fill(d, 3, 3, RED, 0), 8);
    is("the paper round it", pd_fill(d, 0, 0, INK, 0), 48 - 24);
    is("outside the picture", pd_fill(d, 9, 9, INK, 0), 0);
    check("a flood outside", d,
          "########\n"
          "########\n"
          "##oooo##\n"
          "##oooo##\n"
          "########\n"
          "########\n");
    pd_free(d);

    d = pd_new(6, 3, PAPER);                    /* a flood round corners, and one with a tolerance */
    pd_line(d, 1, 0, 1, 1, INK, 1);
    pd_line(d, 3, 2, 3, 1, INK, 1);
    pd_line(d, 5, 0, 5, 1, INK, 1);
    is("round corners", pd_fill(d, 0, 0, RED, 0), 12);
    check("a flood round corners", d,
          "o#ooo#\n"
          "o#o#o#\n"
          "ooo#oo\n");
    pd_free(d);
    d = pd_new(4, 1, PAPER);
    pd_dot(d, 1, 0, 0xFFFFF0u, 1);
    pd_dot(d, 2, 0, 0xFFFFC0u, 1);
    is("no tolerance", pd_fill(d, 0, 0, RED, 0), 1);
    is("a tolerance of 16", pd_fill(d, 3, 0, INK, 16), 1);
    pd_free(d);
    d = pd_new(4, 1, PAPER);
    pd_dot(d, 1, 0, 0xFFFFF0u, 1);
    pd_dot(d, 2, 0, 0xFFFFC0u, 1);
    is("within the tolerance", pd_fill(d, 0, 0, RED, 16), 2);
    pd_free(d);

    d = pd_new(6, 4, PAPER);                    /* a step: undone, done again; a shape dragged out anew */
    before = pd_before(d);
    pd_clean(d);
    pd_rect(d, 1, 1, 3, 2, INK, 1, PD_NONE);
    s = pd_step(d, before);
    is("a step", s != NULL, 1);
    is("its place", s ? s->x * 100 + s->y * 10 + s->w : -1, 113);
    pd_apply(d, s, 0);
    check("undone", d,
          "......\n"
          "......\n"
          "......\n"
          "......\n");
    pd_apply(d, s, 1);
    check("done again", d,
          "......\n"
          ".###..\n"
          ".###..\n"
          "......\n");
    pd_step_free(s);
    before = pd_before(d);
    pd_clean(d);
    pd_line(d, 0, 0, 5, 0, RED, 1);
    pd_restore(d, before);
    pd_line(d, 0, 3, 5, 3, RED, 1);
    check("a shape drawn anew", d,
          "......\n"
          ".###..\n"
          ".###..\n"
          "oooooo\n");
    s = pd_step(d, before);
    is("its step is the last shape's", s ? s->y * 10 + s->h : -1, 31);
    pd_step_free(s);
    is("nothing changed, no step", pd_step(d, (pd_clean(d), pd_before(d))) == NULL, 1);
    pd_free(d);

    {                                           /* a picture read: its pixels are the one layer's, opaque */
        unsigned *px = malloc(4 * sizeof *px);
        px[0] = INK, px[1] = RED, px[2] = PAPER, px[3] = INK;
        d = pd_from(px, 2, 2);
        check("a picture read", d,
              "#o\n"
              ".#\n");
        is("its layer is opaque", d->layer[0].px[1] >> 24, 255);
        d->layer[0].px[2] = 0;                  /* clear: the chequered ground shows (white, at the corner) */
        pd_flatten(d, 0, 0, 2, 2);
        is("clear shows the ground", (long)pd_get(d, 0, 1), 0xFFFFFF);
        pd_free(d);
    }

    d = pd_new(3, 2, PAPER);                    /* the whole picture turned and flipped */
    pd_dot(d, 0, 0, INK, 1);
    pd_dot(d, 2, 0, RED, 1);
    pd_dot(d, 2, 1, RED, 1);
    check("before turning", d,
          "#.o\n"
          "..o\n");
    is("turned right", pd_turn(d, PD_RIGHT), 0);
    is("its size", d->w * 10 + d->h, 23);
    check("a quarter right", d,
          ".#\n"
          "..\n"
          "oo\n");
    is("and back", pd_turn(d, pd_turn_back(PD_RIGHT)), 0);
    check("back again", d,
          "#.o\n"
          "..o\n");
    pd_turn(d, PD_LEFT);
    check("a quarter left", d,
          "oo\n"
          "..\n"
          "#.\n");
    pd_turn(d, PD_RIGHT);
    pd_turn(d, PD_ACROSS);
    check("flipped across", d,
          "o.#\n"
          "o..\n");
    pd_turn(d, PD_OVER);
    check("and over", d,
          "o..\n"
          "o.#\n");
    is("no such turn", pd_turn(d, 9), -1);
    pd_free(d);

    {                                           /* the whole picture another size, and kept for undoing that */
        struct pd_kept *k;
        d = pd_new(2, 2, PAPER);
        pd_dot(d, 0, 0, INK, 1);
        pd_dot(d, 1, 1, RED, 1);
        k = pd_keep(d);
        is("resized", pd_resize(d, 4, 4, 0), 0);
        check("twice as large, by the nearest", d,
              "##..\n"
              "##..\n"
              "..oo\n"
              "..oo\n");
        is("kept, and changed over", k && pd_swap(d, k) == 0, 1);
        check("as it was", d,
              "#.\n"
              ".o\n");
        is("and over again", pd_swap(d, k), 0);
        is("its size again", d->w * 10 + d->h, 44);
        pd_kept_free(k);
        is("made smaller", pd_resize(d, 2, 2, 0), 0);
        check("half as large", d,
              "#.\n"
              ".o\n");
        pd_free(d);
        d = pd_new(2, 1, PAPER);                /* smooth: a black and a white pixel, four wide */
        pd_dot(d, 0, 0, INK, 1);
        pd_resize(d, 4, 1, 1);
        is("smooth: the ends as they were", (long)(pd_get(d, 0, 0) == INK && pd_get(d, 3, 0) == PAPER), 1);
        is("smooth: a dark grey", (long)pd_get(d, 1, 0), 0x404040);
        is("smooth: a light grey", (long)pd_get(d, 2, 0), 0xBFBFBF);
        pd_free(d);
        d = pd_new(2, 2, PAPER);                /* a canvas: larger about the middle, then cut to a corner */
        pd_rect(d, 0, 0, 1, 1, INK, 0, INK);
        is("a larger canvas", pd_canvas(d, 4, 4, 1, 1, RED), 0);
        check("the picture in its middle", d,
              "oooo\n"
              "o##o\n"
              "o##o\n"
              "oooo\n");
        is("a smaller canvas", pd_canvas(d, 2, 3, 2, 2, PAPER), 0);
        check("its bottom right corner kept", d,
              "#o\n"
              "#o\n"
              "oo\n");
        is("no such size", pd_resize(d, 0, 5, 0), -1);
        pd_free(d);
    }

    {                                           /* layers */
        struct pd_kept *k;
        unsigned out[2];
        d = pd_new(3, 2, PAPER);
        pd_dot(d, 0, 0, INK, 1);
        is("a layer added", pd_layer_add(d), 0);
        is("it is the current one, of two", d->cur * 10 + d->n, 12);
        is("its name", strcmp(d->layer[1].name, "Layer 2"), 0);
        check("clear: the one under shows", d,
              "#..\n"
              "...\n");
        pd_dot(d, 0, 0, RED, 1);
        pd_dot(d, 1, 0, RED, 1);
        check("drawn on it, over the other", d,
              "oo.\n"
              "...\n");
        pd_dot(d, 0, 0, PD_CLEAR, 1);
        check("made clear again there", d,
              "#o.\n"
              "...\n");
        pd_layer_set(d, 1, 0, -1);
        check("not shown", d,
              "#..\n"
              "...\n");
        pd_layer_set(d, 1, 1, -1);
        is("copied", pd_layer_copy(d), 0);
        is("the copy is the current one, of three", d->cur * 10 + d->n, 23);
        is("moved down", pd_layer_move(d, 0), 0);
        is("and up again", pd_layer_move(d, 1), 0);
        is("no further than the top", pd_layer_move(d, 1), -1);
        k = pd_keep(d);
        is("merged down", pd_layer_merge(d), 0);
        is("and again", pd_layer_merge(d), 0);
        is("one layer", d->n * 10 + d->cur, 10);
        check("as it showed", d,
              "#o.\n"
              "...\n");
        is("the bottom one merges onto nothing", pd_layer_merge(d), -1);
        is("the only one stays", pd_layer_delete(d), -1);
        is("undone", k && pd_swap(d, k) == 0, 1);
        is("three layers again", d->n * 10 + d->cur, 32);
        check("as it showed before", d,
              "#o.\n"
              "...\n");
        is("the top one taken away", pd_layer_delete(d), 0);
        is("two left", d->n * 10 + d->cur, 21);
        pd_kept_free(k);
        pd_free(d);
        d = pd_new(2, 1, PAPER);                /* half shown; and clear to the ground */
        pd_layer_add(d);
        pd_dot(d, 0, 0, INK, 1);
        pd_layer_set(d, 1, 1, 128);
        is("half of black over white", (long)pd_get(d, 0, 0), 0x7F7F7F);
        d->cur = 0;
        pd_dot(d, 1, 0, PD_CLEAR, 1);
        pd_flatten(d, 0, 0, 2, 1);
        is("clear shows the chequered ground", (long)pd_get(d, 1, 0), 0xFFFFFF);
        pd_render(d, out, RED);
        is("and a plain one when it is saved", (long)out[1], RED);
        is("the rest as it shows", (long)out[0], 0x7F7F7F);
        pd_free(d);
    }

    {                                           /* a part of the picture: copied, pasted as a layer, cut down to */
        unsigned *part;
        int w = 2, h = 2;
        d = pd_new(4, 3, PAPER);
        pd_rect(d, 1, 0, 2, 1, INK, 0, INK);
        pd_dot(d, 3, 2, RED, 1);
        check("before", d,
              ".##.\n"
              ".##.\n"
              "...o\n");
        part = pd_copy(d, 2, 1, &w, &h);
        is("a part copied", part != NULL && w == 2 && h == 2, 1);
        is("its pixels", part ? (long)((part[0] & 0xFFFFFF) == INK && (part[3] & 0xFFFFFF) == RED) : 0, 1);
        is("pasted", pd_paste(d, part, w, h, 0, 1), 0);
        is("as a layer of its own", d->n * 10 + d->cur, 21);
        check("over the picture, its corner where asked", d,
              ".##.\n"
              "#.#.\n"
              ".o.o\n");
        free(part);
        w = 9, h = 9;
        part = pd_copy(d, 3, 2, &w, &h);        /* (clipped to the picture: one pixel, the pasted layer's: clear) */
        is("clipped", part != NULL && w == 1 && h == 1 && part[0] == 0, 1);
        free(part);
        w = 2, h = 2;
        is("nothing outside it", pd_copy(d, 7, 7, &w, &h) == NULL, 1);
        is("cut down", pd_crop(d, 1, 1, 9, 9), 0);
        is("its size", d->w * 10 + d->h, 32);
        check("the part that was there", d,
              ".#.\n"
              "o.o\n");
        is("nothing to cut down to", pd_crop(d, 5, 5, 2, 2), -1);
        pd_free(d);
    }

    is("too large is refused", pd_new(PD_MAX + 1, 1, PAPER) == NULL, 1);
    is("nothing is refused", pd_new(0, 4, PAPER) == NULL, 1);

    if (bad) {
        printf("paint_test: %d failed\n", bad);
        return 1;
    }
    printf("paint_test: PASS\n");
    return 0;
}
