/* The skiing game's pictures, made by rule (art.h): spans of pixels set in one-bit layers by a few shapes
 * (rectangles, ellipses, triangles, thick lines), in whole numbers. The colours are ones the 8-bit palette has
 * (its colour cube and its greys), so that the game looks the same at every depth. Drawn for GLOS: none is
 * taken from any other game. */
#include <stdlib.h>
#include <string.h>

#include "art.h"

struct art art_thing[ART_COUNT];
struct art art_skier[POSE_COUNT][32];

/* sin of n * 11.25 degrees, in 1024ths: a quarter turn; the rest by symmetry. */
static const short SIN9[9] = { 0, 200, 392, 569, 724, 851, 946, 1004, 1024 };

static int sin32(int o)
{
    o &= 31;
    return o <= 8 ? SIN9[o] : o <= 16 ? SIN9[16 - o] : o <= 24 ? -SIN9[o - 16] : -SIN9[32 - o];
}

static int cos32(int o) { return sin32(o + 8); }

static int isqrt(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v)
        r++;
    return r;
}

static void begin(struct art *a, int w, int h, int ox, int oy)
{
    int i;
    for (i = 0; i < a->n; i++)
        free(a->l[i].bits);
    memset(a, 0, sizeof *a);
    a->w = w, a->h = h, a->ox = ox, a->oy = oy;
    a->stride = (w + 7) / 8;
}

static void colour(struct art *a, unsigned rgb)        /* what is drawn next is a new layer in this colour */
{
    if (a->n == 8)
        abort();
    a->l[a->n].rgb = rgb;
    if (!(a->l[a->n].bits = calloc((size_t)a->stride, (size_t)a->h)))
        abort();
    a->n++;
}

static void span(struct art *a, int x0, int x1, int y)  /* x0 up to, not with, x1 */
{
    unsigned char *row;
    if (y < 0 || y >= a->h)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 > a->w)
        x1 = a->w;
    row = a->l[a->n - 1].bits + y * a->stride;
    for (; x0 < x1; x0++)
        row[x0 >> 3] |= (unsigned char)(0x80 >> (x0 & 7));
}

static void rect(struct art *a, int x, int y, int w, int h)
{
    int i;
    for (i = 0; i < h; i++)
        span(a, x, x + w, y + i);
}

static void ellipse(struct art *a, int cx, int cy, int rx, int ry)
{
    int dy;
    for (dy = -ry; dy <= ry; dy++) {
        int half = ry ? rx * isqrt(ry * ry - dy * dy) / ry : rx;
        span(a, cx - half, cx + half + 1, cy + dy);
    }
}

static void tri(struct art *a, int x0, int y0, int x1, int y1, int x2, int y2)
{
    int y, t;
#define SWAP(p, q) (t = p, p = q, q = t)
    if (y1 < y0)
        SWAP(x0, x1), SWAP(y0, y1);
    if (y2 < y0)
        SWAP(x0, x2), SWAP(y0, y2);
    if (y2 < y1)
        SWAP(x1, x2), SWAP(y1, y2);
#undef SWAP
    for (y = y0; y <= y2; y++) {
        int xa = y2 == y0 ? x0 : x0 + (x2 - x0) * (y - y0) / (y2 - y0), xb;
        if (y < y1)
            xb = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
        else
            xb = y2 == y1 ? x1 : x1 + (x2 - x1) * (y - y1) / (y2 - y1);
        if (xa > xb)
            t = xa, xa = xb, xb = t;
        span(a, xa, xb + 1, y);
    }
}

/* A line `thick` pixels wide between two points given in 1/16 pixel. */
static void line(struct art *a, int x0, int y0, int x1, int y1, int thick)
{
    int dx = x1 - x0, dy = y1 - y0, steps = (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) / 8 + 1, i;
    for (i = 0; i <= steps; i++) {
        int x = (x0 + dx * i / steps) / 16, y = (y0 + dy * i / steps) / 16;
        rect(a, x - (thick - 1) / 2, y - (thick - 1) / 2, thick, thick);
    }
}

#define SHADOW 0xC6C6C6u
#define GREEN  0x006633u
#define LEAF   0x339966u
#define TRUNK  0x663300u
#define SNOW   0xFFFFFFu

static void things(void)
{
    struct art *a;

    a = &art_thing[ART_FIR];            /* a fir: three tiers, snow on each */
    begin(a, 27, 42, 13, 39);
    colour(a, SHADOW), ellipse(a, 15, 39, 10, 2);
    colour(a, TRUNK), rect(a, 12, 33, 3, 7);
    colour(a, GREEN);
    tri(a, 13, 2, 6, 15, 20, 15), tri(a, 13, 9, 3, 25, 23, 25), tri(a, 13, 17, 0, 35, 26, 35);
    colour(a, LEAF);
    tri(a, 13, 4, 8, 14, 12, 14), tri(a, 13, 12, 5, 24, 11, 24), tri(a, 13, 21, 3, 34, 10, 34);
    colour(a, SNOW);
    tri(a, 13, 2, 10, 8, 16, 8), tri(a, 13, 10, 9, 15, 17, 15), tri(a, 13, 19, 8, 24, 18, 24);

    a = &art_thing[ART_PINE];           /* a pine: a round crown */
    begin(a, 27, 38, 13, 35);
    colour(a, SHADOW), ellipse(a, 15, 35, 10, 2);
    colour(a, TRUNK), rect(a, 12, 24, 3, 12);
    colour(a, GREEN), ellipse(a, 13, 14, 11, 12);
    colour(a, LEAF), ellipse(a, 10, 12, 6, 7);
    colour(a, SNOW), ellipse(a, 13, 4, 6, 2), ellipse(a, 7, 9, 3, 1), ellipse(a, 19, 10, 3, 1);

    a = &art_thing[ART_ROCK];
    begin(a, 26, 18, 13, 13);
    colour(a, SHADOW), ellipse(a, 15, 14, 10, 3);
    colour(a, 0x626262u), ellipse(a, 13, 9, 11, 7);
    colour(a, 0x8A8A8Au), ellipse(a, 11, 8, 8, 5);
    colour(a, 0xB2B2B2u), ellipse(a, 9, 6, 4, 2);
    colour(a, SNOW), ellipse(a, 13, 3, 6, 1);

    a = &art_thing[ART_POST];           /* a fence's post */
    begin(a, 5, 16, 2, 14);
    colour(a, SHADOW), rect(a, 2, 14, 3, 1);
    colour(a, TRUNK), rect(a, 1, 1, 3, 14);
    colour(a, SNOW), rect(a, 1, 0, 3, 1);

    a = &art_thing[ART_NET];            /* ... and the net from it to the next, 16 pixels on */
    begin(a, 16, 12, 0, 12);
    colour(a, 0xFF6600u);
    rect(a, 0, 1, 16, 1), rect(a, 0, 5, 16, 1), rect(a, 0, 9, 16, 1);
    rect(a, 3, 1, 1, 9), rect(a, 7, 1, 1, 9), rect(a, 11, 1, 1, 9);

    a = &art_thing[ART_RAMP];           /* a ramp of snow: its lip is its lower edge */
    begin(a, 34, 20, 17, 13);
    colour(a, SHADOW), rect(a, 3, 17, 30, 3);
    colour(a, 0xCCCCFFu), tri(a, 10, 2, 3, 16, 17, 16), tri(a, 23, 2, 17, 16, 30, 16), rect(a, 10, 2, 14, 15);
    colour(a, SNOW), rect(a, 11, 3, 12, 9);
    colour(a, 0x6699CCu), rect(a, 3, 15, 28, 2);

    a = &art_thing[ART_MOGUL];          /* a bump: lit from above, shaded below */
    begin(a, 24, 14, 12, 7);
    colour(a, SHADOW), ellipse(a, 12, 8, 10, 4);
    colour(a, SNOW), ellipse(a, 11, 6, 10, 4);
    colour(a, 0xE4E4E4u), ellipse(a, 13, 7, 5, 1);

    a = &art_thing[ART_HUT];            /* the summit hut */
    begin(a, 58, 46, 29, 41);
    colour(a, SHADOW), rect(a, 8, 41, 48, 3);
    colour(a, 0x996633u), rect(a, 7, 19, 44, 22);
    colour(a, TRUNK), rect(a, 7, 26, 44, 1), rect(a, 7, 33, 44, 1), rect(a, 2, 19, 54, 2);
    colour(a, 0x333333u), rect(a, 25, 28, 9, 13);
    colour(a, 0xFFCC33u), rect(a, 12, 27, 8, 6), rect(a, 39, 27, 8, 6);
    colour(a, SNOW), tri(a, 29, 2, 2, 19, 56, 19);
    colour(a, 0xDADADAu), tri(a, 29, 6, 29, 18, 50, 18);

    a = &art_thing[ART_GATE_L];         /* a gate's poles: red at the left, blue at the right, a flag on each */
    begin(a, 10, 22, 1, 21);
    colour(a, SHADOW), rect(a, 1, 21, 4, 1);
    colour(a, 0xCC0000u), rect(a, 0, 1, 2, 20), rect(a, 2, 2, 7, 6);
    a = &art_thing[ART_GATE_R];
    begin(a, 10, 22, 8, 21);
    colour(a, SHADOW), rect(a, 8, 21, 2, 1);
    colour(a, 0x0033CCu), rect(a, 8, 1, 2, 20), rect(a, 1, 2, 7, 6);

    a = &art_thing[ART_FINISH];         /* one of the banner's posts */
    begin(a, 7, 40, 3, 38);
    colour(a, SHADOW), rect(a, 3, 38, 4, 2);
    colour(a, 0x333333u), rect(a, 2, 0, 3, 39);
    colour(a, 0xCC0000u), rect(a, 2, 0, 3, 3);

    a = &art_thing[ART_SHADOW];         /* the skier's shadow on the snow */
    begin(a, 16, 8, 8, 4);
    colour(a, SHADOW), ellipse(a, 8, 4, 7, 3);

    a = &art_thing[ART_PYLON];          /* a chair lift's pylon: a steel mast, its crossbar and the wheels at its
                                           ends that carry the cable, up (left) and down (right), 8 pixels out */
    begin(a, 20, 36, 10, 34);
    colour(a, SHADOW), rect(a, 9, 34, 9, 2);
    colour(a, 0x6E7780u), rect(a, 9, 3, 3, 32), rect(a, 1, 2, 19, 2);
    colour(a, 0x30353Bu), rect(a, 1, 4, 3, 2), rect(a, 17, 4, 3, 2);
    colour(a, SNOW), rect(a, 9, 1, 3, 1), rect(a, 1, 1, 19, 1);

    a = &art_thing[ART_CHAIR];          /* a chair hanging from the cable (its place: the cable's) */
    begin(a, 12, 12, 6, 0);
    colour(a, 0x30353Bu), rect(a, 5, 0, 2, 7), rect(a, 1, 7, 10, 2);
    colour(a, 0x2E5FA8u), rect(a, 1, 9, 10, 3);

    a = &art_thing[ART_CHAIR_SHADOW];   /* ... and its shadow on the snow */
    begin(a, 12, 4, 6, 2);
    colour(a, SHADOW), rect(a, 1, 0, 10, 4);

    {                                   /* the mountain hare, white for the winter, black at the ears' tips */
        int k;
        for (k = 0; k < 2; k++) {       /* (k 0 rightwards; 1 the same, mirrored) */
#define HX(x, w) (k ? 18 - (x) - (w) : (x))
            a = &art_thing[k ? ART_HARE_L : ART_HARE_R];
            begin(a, 18, 14, 9, 13);
            colour(a, SHADOW), rect(a, HX(3, 11), 12, 11, 2);
            colour(a, 0x8A8070u), rect(a, HX(2, 11), 5, 11, 7), rect(a, HX(11, 6), 2, 6, 6), rect(a, HX(12, 3), 0, 3, 3);
            colour(a, 0xF2EEE8u), rect(a, HX(3, 9), 6, 9, 5), rect(a, HX(12, 4), 3, 4, 4);
            colour(a, 0x222222u), rect(a, HX(12, 3), 0, 3, 1), rect(a, HX(14, 1), 4, 1, 1);
#undef HX
        }
    }
}

/* The figures seen from above, heading along o (32nds of a turn clockwise from straight down). Places are said
   along the heading and across it, in 1/16 pixel from the figure's middle. Four figures (the owner's choice of
   2026-11-22: a few to choose from), each in the jacket's colour: the skier (a bobble hat), the racer (a helmet,
   narrower, poles bent back), the snowboarder (one board, standing across it; braking: the board turned
   across), the sit-skier (one ski under a seat, two short outriggers in the hands). */
#define PX(along, across) (cx + ((along) * dx + (across) * px) / 1024)
#define PY(along, across) (cy + ((along) * dy + (across) * py) / 1024)
#define HELMET 0xCCCCCCu
#define DARK   0x333333u
#define POLE   0x808080u

static void skier(struct art *a, int pose, int o, unsigned jacket, int racer)
{
    int dx = sin32(o), dy = cos32(o);   /* along the skis, 1024ths */
    int px = dy, py = -dx;              /* across them */
    int cx = 15 * 16, cy = 15 * 16, side, head = pose == POSE_TUCK ? 48 : 16, gap = racer ? 40 : 48;
    begin(a, 31, 31, 15, 15);
    colour(a, DARK);                    /* the skis: side by side, or in a plough their tips together */
    for (side = -1; side <= 1; side += 2) {
        int back = (pose == POSE_PLOUGH ? 88 : gap) * side, front = (pose == POSE_PLOUGH ? 16 : gap) * side;
        line(a, PX(-144, back), PY(-144, back), PX(160, front), PY(160, front), 2);
    }
    if (pose != POSE_TUCK) {            /* the poles, held out and back (tucked: under the arms, not seen); the
                                           racer's bent back along the body */
        colour(a, POLE);
        for (side = -1; side <= 1; side += 2)
            if (racer)
                line(a, PX(8, side * 72), PY(8, side * 72), PX(-128, side * 80), PY(-128, side * 80), 1);
            else
                line(a, PX(16, side * 96), PY(16, side * 96), PX(-112, side * 120), PY(-112, side * 120), 1);
    }
    colour(a, jacket);                  /* the body and the arms */
    ellipse(a, PX(0, 0) / 16, PY(0, 0) / 16, pose == POSE_TUCK || racer ? 4 : 5, pose == POSE_TUCK || racer ? 4 : 5);
    if (pose != POSE_TUCK)
        for (side = -1; side <= 1; side += 2)
            line(a, PX(0, side * 48), PY(0, side * 48), PX(16, side * (racer ? 72 : 96)), PY(16, side * (racer ? 72 : 96)), 3);
    if (racer) {                        /* the helmet, its visor at the front */
        colour(a, HELMET);
        ellipse(a, PX(head, 0) / 16, PY(head, 0) / 16, 3, 3);
        colour(a, DARK);
        rect(a, PX(head + 40, 0) / 16, PY(head + 40, 0) / 16, 1, 1);
    } else {
        colour(a, DARK);                /* the hat */
        ellipse(a, PX(head, 0) / 16, PY(head, 0) / 16, 2, 2);
        colour(a, SNOW);                /* ... and its bobble */
        rect(a, PX(head, 0) / 16, PY(head, 0) / 16, 1, 1);
    }
}

static void boarder(struct art *a, int pose, int o, unsigned jacket)
{
    int b = pose == POSE_PLOUGH ? o + 7 : o;    /* braking: the board turned nearly across the way */
    int dx = sin32(o), dy = cos32(o), px = dy, py = -dx;
    int bx = sin32(b), by = cos32(b), qx = by, qy = -bx;        /* along the board, and across it */
    int cx = 15 * 16, cy = 15 * 16, side, crouch = pose == POSE_TUCK, reach = crouch ? 40 : 56;
    (void)px, (void)py;
    begin(a, 31, 31, 15, 15);
    colour(a, DARK);                    /* the board: long, wide, round at its ends */
    line(a, cx - 184 * bx / 1024, cy - 184 * by / 1024, cx + 184 * bx / 1024, cy + 184 * by / 1024, 5);
    colour(a, jacket);                  /* the body along the board, shoulders over the feet; the arms out at its ends */
    line(a, cx - reach * bx / 1024, cy - reach * by / 1024, cx + reach * bx / 1024, cy + reach * by / 1024, crouch ? 5 : 4);
    if (!crouch)
        for (side = -1; side <= 1; side += 2)
            line(a, cx + side * 56 * bx / 1024, cy + side * 56 * by / 1024, cx + side * 104 * bx / 1024 + 32 * qx / 1024,
                 cy + side * 104 * by / 1024 + 32 * qy / 1024, 2);
    colour(a, DARK);                    /* the beanie: over the middle, a little to the side the rider faces */
    ellipse(a, (cx + 24 * qx / 1024) / 16, (cy + 24 * qy / 1024) / 16, 2, 2);
}

static void sitter(struct art *a, int pose, int o, unsigned jacket)
{
    int dx = sin32(o), dy = cos32(o), px = dy, py = -dx;
    int cx = 15 * 16, cy = 15 * 16, side, out = pose == POSE_PLOUGH ? 112 : pose == POSE_TUCK ? 72 : 96;
    begin(a, 31, 31, 15, 15);
    colour(a, DARK);                    /* the ski, long and wide, under the seat */
    line(a, PX(-160, 0), PY(-160, 0), PX(176, 0), PY(176, 0), 3);
    colour(a, POLE);                    /* the outriggers: short skis on the hands' sticks; braking, their tails out */
    for (side = -1; side <= 1; side += 2) {
        line(a, PX(16, side * 40), PY(16, side * 40), PX(16, side * out), PY(16, side * out), 1);
        line(a, PX(-40, side * (out + (pose == POSE_PLOUGH ? 24 : 0))), PY(-40, side * (out + (pose == POSE_PLOUGH ? 24 : 0))),
             PX(72, side * out), PY(72, side * out), 2);
    }
    colour(a, 0x666666u);               /* the seat's frame */
    ellipse(a, PX(-24, 0) / 16, PY(-24, 0) / 16, 4, 4);
    colour(a, jacket);                  /* the body in the seat, the legs forward along the ski */
    ellipse(a, PX(-16, 0) / 16, PY(-16, 0) / 16, pose == POSE_TUCK ? 3 : 4, pose == POSE_TUCK ? 3 : 4);
    line(a, PX(0, 0), PY(0, 0), PX(80, 0), PY(80, 0), 4);
    colour(a, HELMET);                  /* the helmet */
    ellipse(a, PX(-32, 0) / 16, PY(-32, 0) / 16, 2, 2);
}

static void figure(struct art *a, int fig, int pose, int o, unsigned jacket)
{
    if (fig == FIGURE_BOARDER)
        boarder(a, pose, o, jacket);
    else if (fig == FIGURE_SITTER)
        sitter(a, pose, o, jacket);
    else
        skier(a, pose, o, jacket, fig == FIGURE_RACER);
}

void art_make(int fig, unsigned jacket)
{
    struct art *a;
    int pose, o;
    things();
    for (pose = 0; pose < POSE_COUNT; pose++)
        for (o = 0; o < 32; o++)
            figure(&art_skier[pose][o], fig, pose, o, jacket);
    a = &art_thing[ART_FALLEN];         /* down: what it rides crossed or on its side, the figure beside it */
    begin(a, 31, 31, 15, 15);
    colour(a, DARK);
    if (fig == FIGURE_BOARDER) {
        line(a, 5 * 16, 22 * 16, 25 * 16, 8 * 16, 4);
    } else if (fig == FIGURE_SITTER) {
        line(a, 4 * 16, 20 * 16, 26 * 16, 12 * 16, 3);
        colour(a, POLE), line(a, 8 * 16, 6 * 16, 14 * 16, 4 * 16, 2), line(a, 18 * 16, 26 * 16, 24 * 16, 24 * 16, 2);
    } else {
        line(a, 4 * 16, 8 * 16, 26 * 16, 22 * 16, 2), line(a, 6 * 16, 24 * 16, 24 * 16, 6 * 16, 2);
    }
    colour(a, jacket), ellipse(a, 15, 17, 7, 4);
    if (fig == FIGURE_RACER || fig == FIGURE_SITTER) {
        colour(a, HELMET), ellipse(a, 23, 15, 3, 3);
    } else {
        colour(a, DARK), ellipse(a, 23, 15, 2, 2);
        colour(a, SNOW), rect(a, 23, 15, 1, 1);
    }
}
#undef PX
#undef PY

void art_draw(struct gfx_surface *s, const struct art *a, int x, int y)
{
    int i;
    for (i = 0; i < a->n; i++)
        gfx_mono(s, x - a->ox, y - a->oy, a->l[i].bits, a->w, a->h, (unsigned)a->stride, a->l[i].rgb, GFX_NONE);
}

void art_draw_plain(struct gfx_surface *s, const struct art *a, int x, int y, unsigned rgb)
{
    int i;
    for (i = 0; i < a->n; i++)
        gfx_mono(s, x - a->ox, y - a->oy, a->l[i].bits, a->w, a->h, (unsigned)a->stride, rgb, GFX_NONE);
}
