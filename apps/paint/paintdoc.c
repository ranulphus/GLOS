/* PAINT's picture and what its tools do to it (paintdoc.h). */
#include <stdlib.h>
#include <string.h>

#include "paintdoc.h"

/* ---- the picture */

static struct pd_doc *make(int w, int h)
{
    struct pd_doc *d;
    if (w < 1 || h < 1 || w > PD_MAX || h > PD_MAX || !(d = calloc(1, sizeof *d)))
        return NULL;
    d->w = w, d->h = h;
    d->n = 1;
    d->layer[0].visible = 1, d->layer[0].opacity = 255;
    strcpy(d->layer[0].name, "Background");
    if (!(d->flat = malloc((size_t)w * (size_t)h * sizeof *d->flat))) {
        free(d);
        return NULL;
    }
    return d;
}

struct pd_doc *pd_new(int w, int h, unsigned paper)
{
    struct pd_doc *d = make(w, h);
    size_t i, all = (size_t)w * (size_t)h;
    if (!d)
        return NULL;
    if (!(d->layer[0].px = malloc(all * sizeof *d->layer[0].px))) {
        pd_free(d);
        return NULL;
    }
    for (i = 0; i < all; i++)
        d->layer[0].px[i] = 0xFF000000u | (paper & 0xFFFFFFu);
    pd_flatten(d, 0, 0, w, h);
    pd_clean(d);
    return d;
}

struct pd_doc *pd_from(unsigned *px, int w, int h)
{
    struct pd_doc *d = make(w, h);
    size_t i, all = (size_t)w * (size_t)h;
    if (!d)
        return NULL;
    for (i = 0; i < all; i++)
        px[i] |= 0xFF000000u;
    d->layer[0].px = px;
    pd_flatten(d, 0, 0, w, h);
    pd_clean(d);
    return d;
}

void pd_free(struct pd_doc *d)
{
    int i;
    if (!d)
        return;
    for (i = 0; i < d->n; i++)
        free(d->layer[i].px);
    free(d->flat);
    free(d);
}

void pd_clean(struct pd_doc *d) { d->x0 = d->y0 = d->x1 = d->y1 = 0; }

static void touch(struct pd_doc *d, int x0, int y0, int x1, int y1)     /* (x1, y1 past the end; clipped) */
{
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > d->w)
        x1 = d->w;
    if (y1 > d->h)
        y1 = d->h;
    if (x1 <= x0 || y1 <= y0)
        return;
    if (d->x1 <= d->x0) {
        d->x0 = x0, d->y0 = y0, d->x1 = x1, d->y1 = y1;
        return;
    }
    if (x0 < d->x0)
        d->x0 = x0;
    if (y0 < d->y0)
        d->y0 = y0;
    if (x1 > d->x1)
        d->x1 = x1;
    if (y1 > d->y1)
        d->y1 = y1;
}

/* What shows: the layers one over another, the bottom one on a chequered ground where it is clear. */
void pd_flatten(struct pd_doc *d, int x0, int y0, int x1, int y1)
{
    int x, y, i;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > d->w)
        x1 = d->w;
    if (y1 > d->h)
        y1 = d->h;
    for (y = y0; y < y1; y++) {
        unsigned *out = d->flat + (size_t)y * d->w;
        if (d->n == 1 && d->layer[0].visible && d->layer[0].opacity == 255) {    /* (the usual picture: quickly) */
            const unsigned *in = d->layer[0].px + (size_t)y * d->w;
            for (x = x0; x < x1; x++)
                out[x] = (in[x] >> 24) == 255 ? in[x] & 0xFFFFFFu : ((x >> 3) + (y >> 3)) & 1 ? 0xC0C0C0u : 0xFFFFFFu;
            continue;
        }
        for (x = x0; x < x1; x++) {
            unsigned r, g, b, c = ((x >> 3) + (y >> 3)) & 1 ? 0xC0C0C0u : 0xFFFFFFu;
            r = c >> 16, g = c >> 8 & 255, b = c & 255;
            for (i = 0; i < d->n; i++) {
                unsigned p, a;
                if (!d->layer[i].visible)
                    continue;
                p = d->layer[i].px[(size_t)y * d->w + x];
                a = (p >> 24) * d->layer[i].opacity / 255;
                if (a == 255) {
                    r = p >> 16 & 255, g = p >> 8 & 255, b = p & 255;
                } else if (a) {
                    r = ((p >> 16 & 255) * a + r * (255 - a)) / 255;
                    g = ((p >> 8 & 255) * a + g * (255 - a)) / 255;
                    b = ((p & 255) * a + b * (255 - a)) / 255;
                }
            }
            out[x] = r << 16 | g << 8 | b;
        }
    }
}

unsigned pd_get(const struct pd_doc *d, int x, int y)
{
    return x < 0 || y < 0 || x >= d->w || y >= d->h ? 0 : d->flat[(size_t)y * d->w + x];
}

/* ---- the tools */

/* A row's pixels from x0 to x1 (the last one), clipped, on the current layer. */
static void span(struct pd_doc *d, int y, int x0, int x1, unsigned argb)
{
    unsigned *p;
    if (y < 0 || y >= d->h)
        return;
    if (x0 < 0)
        x0 = 0;
    if (x1 >= d->w)
        x1 = d->w - 1;
    for (p = d->layer[d->cur].px + (size_t)y * d->w + x0; x0 <= x1; x0++)
        *p++ = argb;
}

/* A round tip `size` wide with its middle on (x, y): the rows of a disc (for an even size, the middle is
   the pixel right of and under its centre; two wide is a square of four). */
static void tip(struct pd_doc *d, int x, int y, unsigned argb, int size)
{
    int r, i, left = x - size / 2, top = y - size / 2;
    if (size <= 1) {
        span(d, y, x, x, argb);
        return;
    }
    for (r = 0; r < size; r++) {
        int dy = 2 * r + 1 - size, a = 0, b = size - 1;         /* (in half pixels from the centre) */
        for (i = 0; i < size; i++) {
            int dx = 2 * i + 1 - size;
            if (dx * dx + dy * dy < size * size - 1)    /* (a little inside the circle: three wide is a
                                                           cross, four wide has its corners off) */
                break;
        }
        a = i, b = size - 1 - i;
        if (a <= b)
            span(d, top + r, left + a, left + b, argb);
    }
}

void pd_dot(struct pd_doc *d, int x, int y, unsigned colour, int size)
{
    if (size < 1)
        size = 1;
    tip(d, x, y, 0xFF000000u | colour, size);
    touch(d, x - size / 2, y - size / 2, x - size / 2 + size, y - size / 2 + size);
}

void pd_line(struct pd_doc *d, int xa, int ya, int xb, int yb, unsigned colour, int size)
{
    int dx = abs(xb - xa), dy = -abs(yb - ya), sx = xa < xb ? 1 : -1, sy = ya < yb ? 1 : -1, err = dx + dy;
    int lx = xa < xb ? xa : xb, ly = ya < yb ? ya : yb, hx = xa < xb ? xb : xa, hy = ya < yb ? yb : ya, x = xa, y = ya;
    unsigned argb = 0xFF000000u | colour;
    if (size < 1)
        size = 1;
    for (;;) {                                  /* (Bresenham's, a tip at each of its pixels) */
        int e2 = 2 * err;
        if (x + size >= 0 && y + size >= 0 && x - size < d->w && y - size < d->h)       /* (far outside: no work) */
            tip(d, x, y, argb, size);
        if (x == xb && y == yb)
            break;
        if (e2 >= dy)
            err += dy, x += sx;
        if (e2 <= dx)
            err += dx, y += sy;
    }
    touch(d, lx - size / 2, ly - size / 2, hx - size / 2 + size, hy - size / 2 + size);
}

static void order(int *a, int *b)
{
    if (*a > *b) {
        int t = *a;
        *a = *b, *b = t;
    }
}

void pd_rect(struct pd_doc *d, int xa, int ya, int xb, int yb, unsigned colour, int size, unsigned fill)
{
    unsigned argb = 0xFF000000u | colour;
    int y;
    order(&xa, &xb);
    order(&ya, &yb);
    if (size < 0)
        size = 0;
    for (y = ya < 0 ? 0 : ya; y <= yb && y < d->h; y++) {
        if (y < ya + size || y > yb - size || xb - xa + 1 <= 2 * size) {
            if (size)
                span(d, y, xa, xb, argb);
            else if (fill != PD_NONE)
                span(d, y, xa, xb, 0xFF000000u | fill);
            continue;
        }
        if (size) {
            span(d, y, xa, xa + size - 1, argb);
            span(d, y, xb - size + 1, xb, argb);
        }
        if (fill != PD_NONE)
            span(d, y, xa + size, xb - size, 0xFF000000u | fill);
    }
    touch(d, xa, ya, xb + 1, yb + 1);
}

/* Is the pixel (x, y) inside the ellipse that fills the box x0..x1, y0..y1? Its centre against the ellipse's
   equation, in half pixels and whole numbers. */
static int in_oval(int x, int y, int x0, int y0, int x1, int y1)
{
    long long a = x1 - x0 + 1, b = y1 - y0 + 1, dx = 2 * (long long)x - (x0 + x1), dy = 2 * (long long)y - (y0 + y1);
    return dx * dx * b * b + dy * dy * a * a <= a * a * b * b;
}

/* The ellipse's row y: its first pixel (the last is its mirror), or 0 if the row has none of it. */
static int oval_row(int y, int x0, int y0, int x1, int y1, int *first)
{
    int lo = x0, hi = (x0 + x1) / 2;
    if (y < y0 || y > y1 || x1 < x0 || !in_oval(hi, y, x0, y0, x1, y1))
        return 0;
    while (lo < hi) {                           /* (the leftmost pixel inside: the row is inside from it to the middle) */
        int mid = (lo + hi) / 2;
        if (in_oval(mid, y, x0, y0, x1, y1))
            hi = mid;
        else
            lo = mid + 1;
    }
    *first = lo;
    return 1;
}

void pd_ellipse(struct pd_doc *d, int xa, int ya, int xb, int yb, unsigned colour, int size, unsigned fill)
{
    unsigned argb = 0xFF000000u | colour;
    int y;
    order(&xa, &xb);
    order(&ya, &yb);
    if (size < 0)
        size = 0;
    for (y = ya < 0 ? 0 : ya; y <= yb && y < d->h; y++) {
        int o = 0, i = 0, has_in;
        if (!oval_row(y, xa, ya, xb, yb, &o))
            continue;
        has_in = oval_row(y, xa + size, ya + size, xb - size, yb - size, &i);
        if (!size) {
            if (fill != PD_NONE)
                span(d, y, o, xa + xb - o, 0xFF000000u | fill);
        } else if (!has_in) {
            span(d, y, o, xa + xb - o, argb);
        } else {
            span(d, y, o, i - 1, argb);
            span(d, y, xa + xb - i + 1, xa + xb - o, argb);
            if (fill != PD_NONE)
                span(d, y, i, xa + xb - i, 0xFF000000u | fill);
        }
    }
    touch(d, xa, ya, xb + 1, yb + 1);
}

static int near(unsigned a, unsigned b, int tolerance)
{
    int k;
    if (a == b)
        return 1;
    if (!tolerance)
        return 0;
    for (k = 0; k < 32; k += 8) {
        int da = (int)(a >> k & 255) - (int)(b >> k & 255);
        if (da > tolerance || -da > tolerance)
            return 0;
    }
    return 1;
}

/* A flood by rows: a stack of rows' pieces to look above and below; a pixel that has taken the colour is
   marked in a map, so that a fill with a colour within the tolerance of what it fills ends. */
long pd_fill(struct pd_doc *d, int x, int y, unsigned colour, int tolerance)
{
    struct piece {
        int y, x0, x1, dy;
    } *st = NULL, *ns;
    size_t n = 0, room = 0;
    unsigned char *seen;
    unsigned *px = d->layer[d->cur].px, seed, argb = 0xFF000000u | colour;
    long done = 0;
    int w = d->w, h = d->h, lx = x, hx = x, ly = y, hy = y;
    if (x < 0 || y < 0 || x >= w || y >= h || !(seen = calloc((size_t)w * (size_t)h, 1)))
        return 0;
    seed = px[(size_t)y * w + x];
#define OPEN(xx, yy) (!seen[(size_t)(yy) * w + (xx)] && near(px[(size_t)(yy) * w + (xx)], seed, tolerance))
#define PUSH(yy, a, b, dd)                                                              \
    do {                                                                                \
        if (n == room) {                                                                \
            if (!(ns = realloc(st, (room = room * 2 + 256) * sizeof *st)))              \
                goto out;                                                               \
            st = ns;                                                                    \
        }                                                                               \
        st[n].y = (yy), st[n].x0 = (a), st[n].x1 = (b), st[n].dy = (dd), n++;           \
    } while (0)
    PUSH(y, x, x, 1);
    PUSH(y - 1, x, x, -1);
    while (n) {
        struct piece p = st[--n];
        int cx = p.x0, a;
        if (p.y < 0 || p.y >= h)
            continue;
        while (cx <= p.x1) {
            int b;
            while (cx <= p.x1 && !OPEN(cx, p.y))
                cx++;
            if (cx > p.x1)
                break;
            for (a = cx; a > 0 && OPEN(a - 1, p.y); a--)
                ;
            for (b = cx; b + 1 < w && OPEN(b + 1, p.y); b++)
                ;
            for (cx = a; cx <= b; cx++) {
                seen[(size_t)p.y * w + cx] = 1;
                px[(size_t)p.y * w + cx] = argb;
            }
            done += b - a + 1;
            if (a < lx)
                lx = a;
            if (b > hx)
                hx = b;
            if (p.y < ly)
                ly = p.y;
            if (p.y > hy)
                hy = p.y;
            PUSH(p.y + p.dy, a, b, p.dy);       /* on the way it was going, the whole piece */
            if (a < p.x0)
                PUSH(p.y - p.dy, a, p.x0 - 1, -p.dy);   /* and back, what sticks out past where it came from */
            if (b > p.x1)
                PUSH(p.y - p.dy, p.x1 + 1, b, -p.dy);
            cx = b + 1;
        }
    }
out:
#undef OPEN
#undef PUSH
    free(st);
    free(seen);
    if (done)
        touch(d, lx, ly, hx + 1, hy + 1);
    return done;
}

/* ---- steps to undo */

unsigned *pd_before(const struct pd_doc *d)
{
    size_t all = (size_t)d->w * (size_t)d->h * sizeof(unsigned);
    unsigned *b = malloc(all);
    if (b)
        memcpy(b, d->layer[d->cur].px, all);
    return b;
}

void pd_restore(struct pd_doc *d, const unsigned *before)
{
    int y;
    for (y = d->y0; y < d->y1 && d->x1 > d->x0; y++)
        memcpy(d->layer[d->cur].px + (size_t)y * d->w + d->x0, before + (size_t)y * d->w + d->x0,
               (size_t)(d->x1 - d->x0) * sizeof *before);
    pd_flatten(d, d->x0, d->y0, d->x1, d->y1);
    pd_clean(d);
}

struct pd_step *pd_step(struct pd_doc *d, unsigned *before)
{
    struct pd_step *s = NULL;
    int y, w = d->x1 - d->x0, h = d->y1 - d->y0;
    if (before && w > 0 && h > 0 && (s = calloc(1, sizeof *s))) {
        s->layer = d->cur, s->x = d->x0, s->y = d->y0, s->w = w, s->h = h;
        s->before = malloc((size_t)w * (size_t)h * sizeof(unsigned));
        s->after = malloc((size_t)w * (size_t)h * sizeof(unsigned));
        if (!s->before || !s->after) {
            pd_step_free(s);
            s = NULL;
        }
        for (y = 0; s && y < h; y++) {
            memcpy(s->before + (size_t)y * w, before + (size_t)(s->y + y) * d->w + s->x, (size_t)w * sizeof(unsigned));
            memcpy(s->after + (size_t)y * w, d->layer[d->cur].px + (size_t)(s->y + y) * d->w + s->x,
                   (size_t)w * sizeof(unsigned));
        }
    }
    free(before);
    return s;
}

void pd_apply(struct pd_doc *d, const struct pd_step *s, int redo)
{
    const unsigned *from = redo ? s->after : s->before;
    int y;
    if (s->layer >= d->n)
        return;
    for (y = 0; y < s->h; y++)
        memcpy(d->layer[s->layer].px + (size_t)(s->y + y) * d->w + s->x, from + (size_t)y * s->w,
               (size_t)s->w * sizeof(unsigned));
    pd_flatten(d, s->x, s->y, s->x + s->w, s->y + s->h);
}

void pd_step_free(struct pd_step *s)
{
    if (s) {
        free(s->before);
        free(s->after);
        free(s);
    }
}

unsigned pd_step_bytes(const struct pd_step *s)
{
    return (unsigned)(sizeof *s + (size_t)s->w * (size_t)s->h * 2 * sizeof(unsigned));
}
