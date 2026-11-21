/* The skiing game's mountains (skimount.h). The rule:
 *
 *   - the piste's middle wanders across the slope and its width swells and narrows, each from a value every few
 *     hundred pixels with an easy curve between (more wander and less width the harder the grade);
 *   - one line down the piste is kept clear (skimount_line): in a slalom it swings from side to side through
 *     the gates, in a free run it wanders; nothing is ever put within a skier's width of it, so every mountain
 *     can be skied (the tests look for a way down each mountain on their own, and for one through every gate);
 *   - beside the piste a band of powder, then forest; on the piste, a piece at a time: rocks, lone firs, moguls,
 *     ramps, short fences, and (from blue up) patches of ice.
 *
 * Every value comes from the mountain's number, a stream's number and a place's number mixed: a piece can be
 * made without the ones before it. */
#include "skimount.h"

typedef unsigned u32;                   /* (32 bits on every machine GLOS and its tests are built for) */

enum { S_CENTRE = 1, S_HALF, S_LINE, S_CHUNK, S_ICE, S_LEN, S_SIDE };

static const struct {
    short wander;                       /* how far the piste's middle strays from the slope's */
    short half, half_var;               /* half its width, and how much that changes */
    short things;                       /* the most things in a piece of piste */
    short ice;                          /* pieces in a hundred with a patch of ice */
    short gap, gate_half;               /* slalom: from gate to gate; half a gate */
    short length;
} G[4] = {
    { 110, 170, 20, 1, 0, 520, 44, 7000 },
    { 150, 142, 24, 2, 8, 460, 38, 8000 },
    { 180, 118, 24, 3, 20, 400, 34, 9000 },
    { 200, 98, 22, 4, 32, 340, 30, 10000 },
};

#define POWDER 36                       /* the band of powder at each side of the piste */
#define CLEAR  24                       /* the clear line's half width, a thing's own reach apart */

static u32 mix(u32 a, u32 b)
{
    a ^= b * 0x9E3779B1u;
    a ^= a >> 15;
    a *= 0x85EBCA6Bu;
    a ^= a >> 13;
    a *= 0xC2B2AE35u;
    a ^= a >> 16;
    return a;
}

/* A stream's value at place i: -1024 to 1024. */
static int ctl(const struct skimount *m, int stream, int i)
{
    return (int)(mix(mix(m->seed, (u32)stream * 0x10001u), (u32)i) % 2049u) - 1024;
}

/* From a to b as t goes from 0 to 256, slowly at both ends. */
static int ease(int a, int b, int t)
{
    int s = (t * t * (768 - 2 * t)) >> 16;
    return a + (b - a) * s / 256;
}

void skimount_init(struct skimount *m, unsigned seed, int grade, int mode)
{
    m->seed = seed;
    m->grade = grade < 0 ? 0 : grade > 3 ? 3 : grade;
    m->mode = mode == SKI_SLALOM ? SKI_SLALOM : SKI_FREE;
    m->length = G[m->grade].length + (int)(mix(seed, S_LEN) % 1000u);
    m->gate_half = G[m->grade].gate_half;
    if (m->mode == SKI_SLALOM) {
        m->gate_first = 700;
        m->gate_gap = G[m->grade].gap;
        m->ngates = (m->length - 300 - m->gate_first) / m->gate_gap + 1;
    } else {
        m->gate_first = 512;            /* (no gates: the places the clear line wanders between) */
        m->gate_gap = 384;
        m->ngates = 0;
    }
}

int skimount_centre(const struct skimount *m, int y)
{
    int i, a, b;
    if (y < 0)
        y = 0;
    i = y / 768;
    a = i < 1 ? 0 : ctl(m, S_CENTRE, i);        /* (straight out of the summit hut) */
    b = ctl(m, S_CENTRE, i + 1);
    return SKI_WORLD_W / 2 + G[m->grade].wander * ease(a, b, (y % 768) / 3) / 1024;
}

int skimount_half(const struct skimount *m, int y)
{
    int i;
    if (y < 0)
        y = 0;
    i = y / 640;
    return G[m->grade].half + G[m->grade].half_var * ease(ctl(m, S_HALF, i), ctl(m, S_HALF, i + 1), (y % 640) * 256 / 640) / 1024;
}

/* The clear line's place k: how far from the piste's middle it is there. */
static int knot(const struct skimount *m, int k)
{
    int y, room;
    if (k < 0 || (m->mode == SKI_SLALOM && k >= m->ngates))
        return 0;
    y = m->gate_first + k * m->gate_gap;
    room = skimount_half(m, y) - 46;
    if (m->mode == SKI_SLALOM) {        /* from side to side, a gate each */
        int side = ((k ^ (int)(mix(m->seed, S_SIDE) & 1u)) & 1) ? 1 : -1;
        int far = 450 + (ctl(m, S_LINE, k) + 1024) * 450 / 2048;       /* 45% to 90% of the way out */
        return side * far * room / 1000;
    }
    return ctl(m, S_LINE, k) * 6 / 10 * room / 1024;
}

int skimount_line(const struct skimount *m, int y)
{
    int d = y - m->gate_first, k, t, off, most;
    if (d >= 0) {
        k = d / m->gate_gap;
        t = d % m->gate_gap;
    } else {                            /* (before the first: from the piste's middle to it) */
        k = -1;
        t = d + m->gate_gap;
        if (t < 0)
            t = 0;
    }
    off = ease(knot(m, k), knot(m, k + 1), t * 256 / m->gate_gap);
    most = skimount_half(m, y) - 40;
    if (off > most)
        off = most;
    if (off < -most)
        off = -most;
    return skimount_centre(m, y) + off;
}

/* Piece j's patch of ice, if it has one: 1, with its middle and its two half widths. */
int skimount_ice(const struct skimount *m, int j, int *cx, int *cy, int *rx, int *ry)
{
    u32 u = mix(mix(m->seed, S_ICE * 0x10001u), (u32)j);
    int y = j * SKI_CHUNK + SKI_CHUNK / 2, half;
    if (j < 3 || y > m->length - 200 || (int)(u % 100u) >= G[m->grade].ice)
        return 0;
    half = skimount_half(m, y);
    *cx = skimount_centre(m, y) - (half - 30) + (int)((u >> 8) % (u32)(2 * half - 60));
    *cy = y;
    *rx = 34 + (int)((u >> 16) % 36u);
    *ry = 50 + (int)((u >> 22) % 60u);
    return 1;
}

int skimount_snow(const struct skimount *m, int x, int y)
{
    int c = skimount_centre(m, y), half = skimount_half(m, y), d = x > c ? x - c : c - x, j, k;
    if (d > half + POWDER)
        return SKI_SNOW_DEEP;
    if (d > half)
        return SKI_SNOW_POWDER;
    j = (y < 0 ? 0 : y) / SKI_CHUNK;
    for (k = j - 1; k <= j + 1; k++) {
        int cx, cy, rx, ry, dx, dy;
        if (!skimount_ice(m, k, &cx, &cy, &rx, &ry))
            continue;
        dx = x > cx ? x - cx : cx - x;
        dy = y > cy ? y - cy : cy - y;
        if (dx < rx && dy < ry && dx * dx * ry * ry + dy * dy * rx * rx < rx * rx * ry * ry)
            return SKI_SNOW_ICE;
    }
    return SKI_SNOW_PISTE;
}

int skimount_gate(const struct skimount *m, int k, int *x, int *y)
{
    if (m->mode != SKI_SLALOM || k < 0 || k >= m->ngates)
        return 0;
    *y = m->gate_first + k * m->gate_gap;
    *x = skimount_line(m, *y);
    return 1;
}

/* Does a thing of reach r at x, y leave the clear line alone? (The line bends: asked a little above and below.) */
static int off_line(const struct skimount *m, int x, int y, int r)
{
    int dy;
    for (dy = -28; dy <= 28; dy += 28) {
        int l = skimount_line(m, y + dy), d = x > l ? x - l : l - x;
        if (d < CLEAR + r)
            return 0;
    }
    return 1;
}

static void put(struct ski_thing *out, int *n, int room, int kind, int r, int x, int y)
{
    int i;
    if (*n >= room)
        return;
    for (i = *n; i > 0 && out[i - 1].y > y; i--)        /* kept sorted by y */
        out[i] = out[i - 1];
    out[i].kind = (short)kind;
    out[i].r = (short)r;
    out[i].x = x;
    out[i].y = y;
    ++*n;
}

int skimount_chunk(const struct skimount *m, int index, struct ski_thing *out, int room)
{
    u32 s = mix(mix(m->seed, S_CHUNK * 0x10001u), (u32)index);
    int y0 = index * SKI_CHUNK, n = 0, side, i, k;
#define NEXT() (s = s * 1664525u + 1013904223u, (int)(s >> 8))
    if (index < 0)
        return 0;
    if (index == 0)                     /* the summit hut, beside the start */
        put(out, &n, room, SKI_T_HUT, 26, skimount_centre(m, 64) - skimount_half(m, 64) + 30, 72);
    if (m->length >= y0 && m->length < y0 + SKI_CHUNK) {        /* the banner's two posts */
        int c = skimount_centre(m, m->length), half = skimount_half(m, m->length);
        put(out, &n, room, SKI_T_FINISH_L, 5, c - half - 6, m->length);
        put(out, &n, room, SKI_T_FINISH_R, 5, c + half + 6, m->length);
    }
    for (side = -1; side <= 1; side += 2) {     /* the forest at each side */
        int count = 5 + NEXT() % 4;
        for (i = 0; i < count; i++) {
            int y = y0 + NEXT() % SKI_CHUNK, far = POWDER + 10 + NEXT() % 220, pine = NEXT() % 3 == 0;
            int x = skimount_centre(m, y) + side * (skimount_half(m, y) + far);
            if (x >= 10 && x <= SKI_WORLD_W - 10)
                put(out, &n, room, pine ? SKI_T_PINE : SKI_T_FIR, 9, x, y);
        }
    }
    if (y0 >= 256 && y0 + SKI_CHUNK <= m->length - 140) {       /* what is on the piste */
        int count = NEXT() % (G[m->grade].things + 1), tries;
        if (m->grade == SKI_BLACK && count == 0)
            count = 1;
        for (i = 0; i < count; i++) {
            for (tries = 0; tries < 5; tries++) {
                int y = y0 + 8 + NEXT() % (SKI_CHUNK - 16), half = skimount_half(m, y), roll = NEXT() % 100;
                int x = skimount_centre(m, y) - half + 18 + NEXT() % (2 * half - 36);
                if (roll >= 85) {       /* a fence: three posts across the slope */
                    if (x - 16 < skimount_centre(m, y) - half + 10 || x + 16 > skimount_centre(m, y) + half - 10
                        || !off_line(m, x - 16, y, 7) || !off_line(m, x, y, 7) || !off_line(m, x + 16, y, 7))
                        continue;
                    put(out, &n, room, SKI_T_FENCE, 7, x - 16, y);
                    put(out, &n, room, SKI_T_FENCE, 7, x, y);
                    put(out, &n, room, SKI_T_FENCE, 7, x + 16, y);
                } else {
                    int kind = roll < 28 ? SKI_T_ROCK : roll < 50 ? SKI_T_FIR : roll < 70 ? SKI_T_MOGUL : SKI_T_RAMP;
                    int r = kind == SKI_T_ROCK ? 10 : kind == SKI_T_FIR ? 9 : kind == SKI_T_MOGUL ? 11 : 14;
                    if (!off_line(m, x, y, r))
                        continue;
                    put(out, &n, room, kind, r, x, y);
                }
                break;
            }
        }
    }
    k = y0 <= m->gate_first ? 0 : (y0 - m->gate_first + m->gate_gap - 1) / m->gate_gap;
    for (; k < m->ngates; k++) {        /* the gates that stand in this piece */
        int x, y;
        if (!skimount_gate(m, k, &x, &y) || y >= y0 + SKI_CHUNK)
            break;
        put(out, &n, room, SKI_T_GATE_L, 0, x - m->gate_half, y);
        put(out, &n, room, SKI_T_GATE_R, 0, x + m->gate_half, y);
    }
#undef NEXT
    return n;
}

unsigned skimount_day(int year, int month, int day)
{
    return mix((u32)(year * 10000 + month * 100 + day), 0xDA7ED00u);
}
