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

    is("too large is refused", pd_new(PD_MAX + 1, 1, PAPER) == NULL, 1);
    is("nothing is refused", pd_new(0, 4, PAPER) == NULL, 1);

    if (bad) {
        printf("paint_test: %d failed\n", bad);
        return 1;
    }
    printf("paint_test: PASS\n");
    return 0;
}
