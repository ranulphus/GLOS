/* PAINT's picture and what its tools do to it (paintdoc.h). */
#include <stdio.h>
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

/* What shows: the layers one over another, on a chequered ground where all are clear (or on `paper`, plain,
   for pd_render). */
static void compose(const struct pd_doc *d, unsigned *to, int x0, int y0, int x1, int y1, int plain, unsigned paper)
{
    int x, y, i;
    for (y = y0; y < y1; y++) {
        unsigned *out = to + (size_t)y * d->w;
        if (d->n == 1 && d->layer[0].visible && d->layer[0].opacity == 255 && !plain) {  /* (the usual: quickly) */
            const unsigned *in = d->layer[0].px + (size_t)y * d->w;
            for (x = x0; x < x1; x++)
                out[x] = (in[x] >> 24) == 255 ? in[x] & 0xFFFFFFu : ((x >> 3) + (y >> 3)) & 1 ? 0xC0C0C0u : 0xFFFFFFu;
            continue;
        }
        for (x = x0; x < x1; x++) {
            unsigned r, g, b, c = plain ? paper : ((x >> 3) + (y >> 3)) & 1 ? 0xC0C0C0u : 0xFFFFFFu;
            r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
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

void pd_flatten(struct pd_doc *d, int x0, int y0, int x1, int y1)
{
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > d->w)
        x1 = d->w;
    if (y1 > d->h)
        y1 = d->h;
    compose(d, d->flat, x0, y0, x1, y1, 0, 0);
}

void pd_render(const struct pd_doc *d, unsigned *out, unsigned paper) { compose(d, out, 0, 0, d->w, d->h, 1, paper); }

unsigned pd_get(const struct pd_doc *d, int x, int y)
{
    return x < 0 || y < 0 || x >= d->w || y >= d->h ? 0 : d->flat[(size_t)y * d->w + x];
}

/* ---- the tools */

/* A tool's colour as a layer's pixel: opaque, or clear for PD_CLEAR. */
static unsigned ink(unsigned c) { return c == PD_CLEAR ? 0 : 0xFF000000u | (c & 0xFFFFFFu); }

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
    tip(d, x, y, ink(colour), size);
    touch(d, x - size / 2, y - size / 2, x - size / 2 + size, y - size / 2 + size);
}

void pd_line(struct pd_doc *d, int xa, int ya, int xb, int yb, unsigned colour, int size)
{
    int dx = abs(xb - xa), dy = -abs(yb - ya), sx = xa < xb ? 1 : -1, sy = ya < yb ? 1 : -1, err = dx + dy;
    int lx = xa < xb ? xa : xb, ly = ya < yb ? ya : yb, hx = xa < xb ? xb : xa, hy = ya < yb ? yb : ya, x = xa, y = ya;
    unsigned argb = ink(colour);
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
    unsigned argb = ink(colour);
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
                span(d, y, xa, xb, ink(fill));
            continue;
        }
        if (size) {
            span(d, y, xa, xa + size - 1, argb);
            span(d, y, xb - size + 1, xb, argb);
        }
        if (fill != PD_NONE)
            span(d, y, xa + size, xb - size, ink(fill));
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
    unsigned argb = ink(colour);
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
                span(d, y, o, xa + xb - o, ink(fill));
        } else if (!has_in) {
            span(d, y, o, xa + xb - o, argb);
        } else {
            span(d, y, o, i - 1, argb);
            span(d, y, xa + xb - i + 1, xa + xb - o, argb);
            if (fill != PD_NONE)
                span(d, y, i, xa + xb - i, ink(fill));
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
    unsigned *px = d->layer[d->cur].px, seed, argb = ink(colour);
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

/* ---- the whole picture turned */

int pd_turn_back(int how) { return how == PD_RIGHT ? PD_LEFT : how == PD_LEFT ? PD_RIGHT : how; }

int pd_turn(struct pd_doc *d, int how)
{
    int quarter = how == PD_RIGHT || how == PD_LEFT, w = d->w, h = d->h, nw = quarter ? h : w, nh = quarter ? w : h;
    unsigned *n[PD_LAYERS], *flat;
    int i, x, y;
    if (how < PD_RIGHT || how > PD_OVER)
        return -1;
    for (i = 0; i < d->n; i++)
        if (!(n[i] = malloc((size_t)nw * (size_t)nh * sizeof **n))) {
            while (i--)
                free(n[i]);
            return -1;
        }
    if (!(flat = malloc((size_t)nw * (size_t)nh * sizeof *flat))) {
        for (i = 0; i < d->n; i++)
            free(n[i]);
        return -1;
    }
    for (i = 0; i < d->n; i++) {
        const unsigned *from = d->layer[i].px;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                unsigned c = from[(size_t)y * w + x];
                switch (how) {
                case PD_RIGHT: n[i][(size_t)x * nw + (h - 1 - y)] = c; break;
                case PD_LEFT: n[i][(size_t)(w - 1 - x) * nw + y] = c; break;
                case PD_ACROSS: n[i][(size_t)y * nw + (w - 1 - x)] = c; break;
                default: n[i][(size_t)(h - 1 - y) * nw + x] = c; break;
                }
            }
        free(d->layer[i].px);
        d->layer[i].px = n[i];
    }
    free(d->flat);
    d->flat = flat;
    d->w = nw, d->h = nh;
    pd_flatten(d, 0, 0, nw, nh);
    pd_clean(d);
    return 0;
}

/* ---- adjustments */

static unsigned clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : (unsigned)v; }

void pd_adjust(struct pd_doc *d, int kind, int a, int b, int x, int y, int w, int h)
{
    int r, c;
    if (x < 0)
        w += x, x = 0;
    if (y < 0)
        h += y, y = 0;
    if (x + w > d->w)
        w = d->w - x;
    if (y + h > d->h)
        h = d->h - y;
    if (w <= 0 || h <= 0)
        return;
    for (r = 0; r < h; r++) {
        unsigned *p = d->layer[d->cur].px + (size_t)(y + r) * d->w + x;
        for (c = 0; c < w; c++, p++) {
            int R = (int)(*p >> 16 & 255), G = (int)(*p >> 8 & 255), B = (int)(*p & 255), grey;
            if (!(*p >> 24))
                continue;
            switch (kind) {
            case PD_INVERT:
                R = 255 - R, G = 255 - G, B = 255 - B;
                break;
            case PD_GREY:
                R = G = B = (77 * R + 150 * G + 29 * B + 128) >> 8;
                break;
            case PD_SEPIA:
                grey = R;
                R = (393 * R + 769 * G + 189 * B + 500) / 1000;
                G = (349 * grey + 686 * G + 168 * B + 500) / 1000;
                B = (272 * grey + 534 * (int)(*p >> 8 & 255) + 131 * B + 500) / 1000;
                break;
            default:                            /* brightness, then contrast about the middle grey */
                R = (R + a * 255 / 100 - 128) * (100 + b) / 100 + 128;
                G = (G + a * 255 / 100 - 128) * (100 + b) / 100 + 128;
                B = (B + a * 255 / 100 - 128) * (100 + b) / 100 + 128;
                break;
            }
            *p = (*p & 0xFF000000u) | clamp8(R) << 16 | clamp8(G) << 8 | clamp8(B);
        }
    }
    touch(d, x, y, x + w, y + h);
}

/* ---- the whole picture another size */

/* The layers' new pixels put in place of the old (taken over), the picture nw by nh. */
static int resized(struct pd_doc *d, unsigned **n, int nw, int nh)
{
    unsigned *flat = malloc((size_t)nw * (size_t)nh * sizeof *flat);
    int i;
    if (!flat)
        return -1;
    for (i = 0; i < d->n; i++) {
        free(d->layer[i].px);
        d->layer[i].px = n[i];
    }
    free(d->flat);
    d->flat = flat;
    d->w = nw, d->h = nh;
    pd_flatten(d, 0, 0, nw, nh);
    pd_clean(d);
    return 0;
}

static int layers_new(const struct pd_doc *d, unsigned **n, int nw, int nh)
{
    int i;
    if (nw < 1 || nh < 1 || nw > PD_MAX || nh > PD_MAX)
        return -1;
    for (i = 0; i < d->n; i++)
        if (!(n[i] = malloc((size_t)nw * (size_t)nh * sizeof **n))) {
            while (i--)
                free(n[i]);
            return -1;
        }
    return 0;
}

int pd_resize(struct pd_doc *d, int nw, int nh, int smooth)
{
    unsigned *n[PD_LAYERS];
    int i, x, y, k, w = d->w, h = d->h;
    if (layers_new(d, n, nw, nh) != 0)
        return -1;
    for (i = 0; i < d->n; i++) {
        const unsigned *from = d->layer[i].px;
        for (y = 0; y < nh; y++)
            for (x = 0; x < nw; x++) {
                if (!smooth) {
                    n[i][(size_t)y * nw + x] = from[(size_t)((long long)y * h / nh) * w + (int)((long long)x * w / nw)];
                } else {                        /* where the new pixel's middle falls among the old ones' middles, in
                                                   256ths; the four round it, each by how near it is */
                    long long fx = ((2LL * x + 1) * w * 256) / (2LL * nw) - 128, fy = ((2LL * y + 1) * h * 256) / (2LL * nh) - 128;
                    int x0, y0, x1, y1, wx, wy;
                    unsigned c = 0;
                    if (fx < 0)
                        fx = 0;
                    if (fy < 0)
                        fy = 0;
                    x0 = (int)(fx >> 8), y0 = (int)(fy >> 8), wx = (int)(fx & 255), wy = (int)(fy & 255);
                    x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
                    for (k = 0; k < 32; k += 8) {
                        unsigned a = from[(size_t)y0 * w + x0] >> k & 255, b = from[(size_t)y0 * w + x1] >> k & 255;
                        unsigned e = from[(size_t)y1 * w + x0] >> k & 255, f = from[(size_t)y1 * w + x1] >> k & 255;
                        unsigned top = a * (256 - wx) + b * wx, bot = e * (256 - wx) + f * wx;
                        c |= ((top * (256 - wy) + bot * wy + 32768) >> 16) << k;
                    }
                    n[i][(size_t)y * nw + x] = c;
                }
            }
    }
    if (resized(d, n, nw, nh) != 0) {
        for (i = 0; i < d->n; i++)
            free(n[i]);
        return -1;
    }
    return 0;
}

/* The picture on a canvas nw by nh, its old corner at ox, oy of the new one. */
static int canvas_at(struct pd_doc *d, int nw, int nh, int ox, int oy, unsigned paper)
{
    unsigned *n[PD_LAYERS];
    int i, x, y, w = d->w, h = d->h;
    if (layers_new(d, n, nw, nh) != 0)
        return -1;
    for (i = 0; i < d->n; i++) {
        unsigned ground = i ? 0 : 0xFF000000u | (paper & 0xFFFFFFu);
        for (y = 0; y < nh; y++)
            for (x = 0; x < nw; x++) {
                int sx = x - ox, sy = y - oy;
                n[i][(size_t)y * nw + x] = sx >= 0 && sy >= 0 && sx < w && sy < h ? d->layer[i].px[(size_t)sy * w + sx]
                                                                                 : ground;
            }
    }
    if (resized(d, n, nw, nh) != 0) {
        for (i = 0; i < d->n; i++)
            free(n[i]);
        return -1;
    }
    return 0;
}

int pd_canvas(struct pd_doc *d, int nw, int nh, int ax, int ay, unsigned paper)
{
    return canvas_at(d, nw, nh, ax == 0 ? 0 : ax == 1 ? (nw - d->w) / 2 : nw - d->w,
                     ay == 0 ? 0 : ay == 1 ? (nh - d->h) / 2 : nh - d->h, paper);
}

/* A rectangle clipped to the picture: 0 if nothing of it is left. */
static int clip_rect(const struct pd_doc *d, int *x, int *y, int *w, int *h)
{
    if (*x < 0)
        *w += *x, *x = 0;
    if (*y < 0)
        *h += *y, *y = 0;
    if (*x + *w > d->w)
        *w = d->w - *x;
    if (*y + *h > d->h)
        *h = d->h - *y;
    return *w > 0 && *h > 0;
}

int pd_crop(struct pd_doc *d, int x, int y, int w, int h)
{
    if (!clip_rect(d, &x, &y, &w, &h))
        return -1;
    return canvas_at(d, w, h, -x, -y, 0xFFFFFF);
}

unsigned *pd_copy(const struct pd_doc *d, int x, int y, int *w, int *h)
{
    unsigned *out;
    int r;
    if (!clip_rect(d, &x, &y, w, h) || !(out = malloc((size_t)*w * (size_t)*h * sizeof *out)))
        return NULL;
    for (r = 0; r < *h; r++)
        memcpy(out + (size_t)r * *w, d->layer[d->cur].px + (size_t)(y + r) * d->w + x, (size_t)*w * sizeof *out);
    return out;
}

static int layer_new(struct pd_doc *d, const char *name);

int pd_paste(struct pd_doc *d, const unsigned *px, int w, int h, int x, int y)
{
    int r, c;
    if (layer_new(d, "Pasted") != 0)
        return -1;
    memset(d->layer[d->cur].px, 0, (size_t)d->w * (size_t)d->h * sizeof(unsigned));
    for (r = 0; r < h; r++)
        for (c = 0; c < w; c++)
            if (x + c >= 0 && y + r >= 0 && x + c < d->w && y + r < d->h)
                d->layer[d->cur].px[(size_t)(y + r) * d->w + x + c] = px[(size_t)r * w + c];
    pd_flatten(d, 0, 0, d->w, d->h);
    pd_clean(d);
    return 0;
}

struct pd_kept *pd_keep(const struct pd_doc *d)
{
    struct pd_kept *k = calloc(1, sizeof *k);
    size_t all = (size_t)d->w * (size_t)d->h * sizeof(unsigned);
    int i;
    if (!k)
        return NULL;
    k->w = d->w, k->h = d->h, k->cur = d->cur;
    for (i = 0; i < d->n; i++) {
        k->layer[i] = d->layer[i];
        if (!(k->layer[i].px = malloc(all))) {
            pd_kept_free(k);
            return NULL;
        }
        memcpy(k->layer[i].px, d->layer[i].px, all);
        k->n = i + 1;
    }
    return k;
}

int pd_swap(struct pd_doc *d, struct pd_kept *k)
{
    struct pd_layer t;
    unsigned *flat;
    int i, w = d->w, h = d->h, n = d->n, cur = d->cur;
    if (!(flat = malloc((size_t)k->w * (size_t)k->h * sizeof *flat)))
        return -1;
    for (i = 0; i < PD_LAYERS; i++)
        t = d->layer[i], d->layer[i] = k->layer[i], k->layer[i] = t;
    free(d->flat);
    d->flat = flat;
    d->w = k->w, d->h = k->h, d->n = k->n, d->cur = k->cur;
    k->w = w, k->h = h, k->n = n, k->cur = cur;
    pd_flatten(d, 0, 0, d->w, d->h);
    pd_clean(d);
    return 0;
}

void pd_kept_free(struct pd_kept *k)
{
    int i;
    if (!k)
        return;
    for (i = 0; i < k->n; i++)
        free(k->layer[i].px);
    free(k);
}

unsigned pd_kept_bytes(const struct pd_kept *k)
{
    return (unsigned)(sizeof *k + (size_t)k->w * (size_t)k->h * (size_t)k->n * sizeof(unsigned));
}

/* ---- layers */

static void all_again(struct pd_doc *d)
{
    pd_flatten(d, 0, 0, d->w, d->h);
    pd_clean(d);
}

/* A place made over the current layer for one more, which is the current one then: its pixels are the caller's
   to fill. */
static int layer_new(struct pd_doc *d, const char *name)
{
    size_t all = (size_t)d->w * (size_t)d->h;
    unsigned *px;
    int i;
    if (d->n >= PD_LAYERS || !(px = malloc(all * sizeof *px)))
        return -1;
    for (i = d->n; i > d->cur + 1; i--)
        d->layer[i] = d->layer[i - 1];
    d->n++, d->cur++;
    memset(&d->layer[d->cur], 0, sizeof d->layer[0]);
    d->layer[d->cur].px = px;
    d->layer[d->cur].visible = 1, d->layer[d->cur].opacity = 255;
    strncpy(d->layer[d->cur].name, name, sizeof d->layer[0].name - 1);
    return 0;
}

int pd_layer_add(struct pd_doc *d)
{
    char name[24];
    int k = d->n + 1, i, taken;
    do {                                        /* ("Layer 2", or the first number no layer has) */
        sprintf(name, "Layer %d", k++);
        for (taken = 0, i = 0; i < d->n; i++)
            taken |= !strcmp(d->layer[i].name, name);
    } while (taken);
    if (layer_new(d, name) != 0)
        return -1;
    memset(d->layer[d->cur].px, 0, (size_t)d->w * (size_t)d->h * sizeof(unsigned));
    all_again(d);
    return 0;
}

int pd_layer_copy(struct pd_doc *d)
{
    char name[24];
    int from = d->cur;
    sprintf(name, "%.17s copy", d->layer[from].name);
    if (layer_new(d, name) != 0)
        return -1;
    memcpy(d->layer[d->cur].px, d->layer[from].px, (size_t)d->w * (size_t)d->h * sizeof(unsigned));
    d->layer[d->cur].visible = d->layer[from].visible, d->layer[d->cur].opacity = d->layer[from].opacity;
    all_again(d);
    return 0;
}

int pd_layer_delete(struct pd_doc *d)
{
    int i;
    if (d->n < 2)
        return -1;
    free(d->layer[d->cur].px);
    for (i = d->cur; i + 1 < d->n; i++)
        d->layer[i] = d->layer[i + 1];
    d->n--;
    memset(&d->layer[d->n], 0, sizeof d->layer[0]);
    if (d->cur >= d->n)
        d->cur = d->n - 1;
    all_again(d);
    return 0;
}

int pd_layer_move(struct pd_doc *d, int up)
{
    int to = d->cur + (up ? 1 : -1);
    struct pd_layer t;
    if (to < 0 || to >= d->n)
        return -1;
    t = d->layer[d->cur], d->layer[d->cur] = d->layer[to], d->layer[to] = t;
    d->cur = to;
    all_again(d);
    return 0;
}

int pd_layer_merge(struct pd_doc *d)
{
    struct pd_layer *top = &d->layer[d->cur], *low;
    size_t i, all = (size_t)d->w * (size_t)d->h;
    if (d->cur < 1)
        return -1;
    low = top - 1;
    for (i = 0; top->visible && i < all; i++) { /* (one not shown adds nothing: it only goes) */
        unsigned p = top->px[i], q = low->px[i], a = (p >> 24) * top->opacity / 255, qa = q >> 24, oa, k;
        unsigned out = 0;
        if (!a)
            continue;
        if (a == 255 || !qa) {
            low->px[i] = a << 24 | (p & 0xFFFFFFu);
            continue;
        }
        oa = a + qa * (255 - a) / 255;         /* the one over the other: its alpha, then each colour by the two's */
        for (k = 0; k < 24; k += 8) {
            unsigned pc = p >> k & 255, qc = q >> k & 255;
            out |= ((pc * a * 255 + qc * qa * (255 - a)) / (oa * 255)) << k;
        }
        low->px[i] = oa << 24 | out;
    }
    return pd_layer_delete(d) == 0 ? 0 : -1;
}

void pd_layer_set(struct pd_doc *d, int i, int visible, int opacity)
{
    if (i < 0 || i >= d->n)
        return;
    if (visible >= 0)
        d->layer[i].visible = visible != 0;
    if (opacity >= 0)
        d->layer[i].opacity = (unsigned char)(opacity > 255 ? 255 : opacity);
    all_again(d);
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
