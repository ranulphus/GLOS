/* PDF pages drawn into pixels (pdfdraw.h).
 *
 * A page's content stream is words: numbers and names, then an operator that uses them ("10 20 m": move to
 * 10, 20). They build paths (lines and curves) and paint them, set colours and the "current transformation
 * matrix" (how the page's own points become the device's), save and restore all that (q, Q), and place
 * pictures and other streams of the same kind (Do).
 *
 * Here: a path is kept as straight pieces in device pixels (curves cut up as they are met). Filling adds each
 * edge's signed area into a row of sums a pixel wide (the exact share of every pixel the edge crosses), a band
 * of rows at a time; the running sum along a row is how much of each pixel is inside, by either rule (nonzero,
 * even-odd). A stroke is filled as its outline: a box a piece, and round joins where the line is thick enough
 * to show them. Clipping is to boxes: a path set as the clip clips to the box around it (most are boxes).
 * Pictures are drawn by going through the device's pixels under them and asking the picture for each.
 *
 * Text: each code of a shown string is a glyph's outline (apps/shared/pdffont.c: from the PDF's own TrueType
 * font, or a stand-in), drawn through the text matrix, the font's size and the page's matrix, and filled as any
 * path; the text matrix then moves on by the code's width and the spacings.
 *
 * Not yet: dashes, shadings and patterns (a flat gray), clipping to shapes, soft masks other than a picture's
 * own, blending; text as a clip. Written from the specification (ISO 32000-1). */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "pdfdraw.h"
#include "pdffont.h"

#define BAND 64                         /* rows filled at a time */
#define MAX_PICTURE (16ul << 20)        /* a picture's pixels at most */

static pdf_jpeg_fn jpeg;
void pdf_draw_jpeg(pdf_jpeg_fn fn) { jpeg = fn; }

struct gstate {
    double ctm[6];
    unsigned fill, stroke;              /* 00RRGGBBh */
    double fill_alpha, stroke_alpha;
    double width;
    int cap, join;
    double clip[4];                     /* device pixels: x0, y0, x1, y1 */
    struct pdf_font *font;              /* text: the font and its size, the spacings, the rise, how it is painted */
    double size, tc, tw, th, tl, rise;
    int mode;
};

struct path {
    double *xy;                         /* device pixels, two a point */
    unsigned char *first;               /* 1: the point begins a piece; 3: ... which is closed */
    unsigned n, room;
    double cx, cy, sx, sy;              /* the current point and the piece's first, in the page's own units */
    int open;                           /* a piece is being made */
};

struct ctx {
    struct pdf *d;
    struct pdf_canvas *c;
    struct gstate g, stack[32];
    int depth;
    struct path p;
    int clip_next;                      /* W or W* was said: the path is the clip after the next painting */
    float *acc;                         /* the filler's sums: BAND rows */
    int acc_w;
    int forms;                          /* Do within Do */
    double tm[6], tlm[6];               /* the text matrix, and the line's */
    struct { struct pdf_obj *dict; struct pdf_font *f; } fonts[64];     /* the page's fonts, opened as they are named */
    int nfonts;
    struct path tp;                     /* a glyph's outline, in device pixels */
    double gm[6], gx, gy;               /* ... the matrix it is drawn through, and where its last piece ended */
};

/* ---- matrices: x' = a x + c y + e, y' = b x + d y + f */

static void concat(double *ctm, const double *m)        /* ctm becomes m, then ctm */
{
    double r[6];
    r[0] = m[0] * ctm[0] + m[1] * ctm[2], r[1] = m[0] * ctm[1] + m[1] * ctm[3];
    r[2] = m[2] * ctm[0] + m[3] * ctm[2], r[3] = m[2] * ctm[1] + m[3] * ctm[3];
    r[4] = m[4] * ctm[0] + m[5] * ctm[2] + ctm[4], r[5] = m[4] * ctm[1] + m[5] * ctm[3] + ctm[5];
    memcpy(ctm, r, sizeof r);
}

static void to_device(const double *m, double x, double y, double *dx, double *dy)
{
    *dx = m[0] * x + m[2] * y + m[4], *dy = m[1] * x + m[3] * y + m[5];
}

/* ---- paths */

static int path_point(struct path *p, double x, double y, int first)
{
    if (!(x > -1e7 && x < 1e7 && y > -1e7 && y < 1e7))         /* (not a number, or out of all reason) */
        return -1;
    if (p->n == p->room) {
        unsigned room = p->room ? p->room * 2 : 256;
        double *xy = realloc(p->xy, room * 2 * sizeof *xy);
        unsigned char *f;
        if (!xy)
            return -1;
        p->xy = xy;
        if (!(f = realloc(p->first, room)))
            return -1;
        p->first = f, p->room = room;
    }
    p->xy[2 * p->n] = x, p->xy[2 * p->n + 1] = y;
    p->first[p->n++] = (unsigned char)first;
    return 0;
}

static void path_move(struct ctx *c, double x, double y)
{
    double dx, dy;
    to_device(c->g.ctm, x, y, &dx, &dy);
    path_point(&c->p, dx, dy, 1);
    c->p.cx = c->p.sx = x, c->p.cy = c->p.sy = y;
    c->p.open = 1;
}

static void path_line(struct ctx *c, double x, double y)
{
    double dx, dy;
    if (!c->p.open) {
        path_move(c, x, y);
        return;
    }
    to_device(c->g.ctm, x, y, &dx, &dy);
    path_point(&c->p, dx, dy, 0);
    c->p.cx = x, c->p.cy = y;
}

static void path_curve(struct ctx *c, double x1, double y1, double x2, double y2, double x3, double y3)
{
    double p[8], dx, dy, len;
    int i, n;
    if (!c->p.open)
        path_move(c, x1, y1);
    to_device(c->g.ctm, c->p.cx, c->p.cy, &p[0], &p[1]);
    to_device(c->g.ctm, x1, y1, &p[2], &p[3]);
    to_device(c->g.ctm, x2, y2, &p[4], &p[5]);
    to_device(c->g.ctm, x3, y3, &p[6], &p[7]);
    len = fabs(p[2] - p[0]) + fabs(p[3] - p[1]) + fabs(p[4] - p[2]) + fabs(p[5] - p[3]) + fabs(p[6] - p[4]) + fabs(p[7] - p[5]);
    n = len < 4 ? 2 : len > 1024 ? 64 : (int)(sqrt(len) * 2);   /* (pieces: more the longer it is on the screen) */
    for (i = 1; i <= n; i++) {
        double t = (double)i / n, u = 1 - t;
        dx = u * u * u * p[0] + 3 * u * u * t * p[2] + 3 * u * t * t * p[4] + t * t * t * p[6];
        dy = u * u * u * p[1] + 3 * u * u * t * p[3] + 3 * u * t * t * p[5] + t * t * t * p[7];
        path_point(&c->p, dx, dy, 0);
    }
    c->p.cx = x3, c->p.cy = y3;
}

static void path_close(struct ctx *c)
{
    unsigned i;
    if (!c->p.open)
        return;
    for (i = c->p.n; i-- > 0; )
        if (c->p.first[i]) {
            c->p.first[i] = 3;
            break;
        }
    c->p.cx = c->p.sx, c->p.cy = c->p.sy;
    c->p.open = 0;
}

static void path_clear(struct path *p)
{
    p->n = 0, p->open = 0;
}

static int path_box(const struct path *p, double box[4])        /* the box around it: 0 if it has no points */
{
    unsigned i;
    if (!p->n)
        return 0;
    box[0] = box[2] = p->xy[0], box[1] = box[3] = p->xy[1];
    for (i = 1; i < p->n; i++) {
        double x = p->xy[2 * i], y = p->xy[2 * i + 1];
        box[0] = x < box[0] ? x : box[0], box[2] = x > box[2] ? x : box[2];
        box[1] = y < box[1] ? y : box[1], box[3] = y > box[3] ? y : box[3];
    }
    return 1;
}

/* ---- filling */

static void blend(unsigned *px, unsigned rgb, int a)    /* a: 0 to 256 */
{
    unsigned p = *px;
    int r = (int)(p >> 16 & 255), g = (int)(p >> 8 & 255), b = (int)(p & 255);
    r += (((int)(rgb >> 16 & 255) - r) * a) >> 8;
    g += (((int)(rgb >> 8 & 255) - g) * a) >> 8;
    b += (((int)(rgb & 255) - b) * a) >> 8;
    *px = (unsigned)(r << 16 | g << 8 | b);
}

/* An edge's signed area added into the band's sums: rows y0 to y0 + rows of the device, columns from x0. */
static void edge(float *acc, int w, int rows, double ax, double ay, double bx, double by)
{
    double dir = 1, dxdy, x;
    int y, y_from, y_to;
    if (ay == by)
        return;
    if (ay > by) {
        double t = ax;
        ax = bx, bx = t;
        t = ay, ay = by, by = t;
        dir = -1;
    }
    if (by <= 0 || ay >= rows)
        return;
    dxdy = (bx - ax) / (by - ay);
    x = ax;
    if (ay < 0)
        x -= ay * dxdy, ay = 0;
    y_from = (int)ay, y_to = by > rows ? rows : (int)ceil(by);
    for (y = y_from; y < y_to; y++) {
        float *row = acc + (size_t)y * (size_t)w;
        double top = ay > y ? ay : y, bottom = by < y + 1 ? by : y + 1, dy = bottom - top, xn = x + dxdy * dy, d = dy * dir;
        double xa = x < xn ? x : xn, xb = x < xn ? xn : x, xaf, xbc;
        int ia, ib;
        xa = xa < 0 ? 0 : xa > w - 2 ? w - 2 : xa;
        xb = xb < 0 ? 0 : xb > w - 2 ? w - 2 : xb;
        xaf = floor(xa), xbc = ceil(xb);
        ia = (int)xaf, ib = (int)xbc;
        if (ib <= ia + 1) {                     /* within one pixel: its share by where the middle is */
            double mid = 0.5 * (xa + xb) - xaf;
            row[ia] += (float)(d - d * mid);
            row[ia + 1] += (float)(d * mid);
        } else {
            double s = 1 / (xb - xa), f0 = xa - xaf, a0 = 0.5 * s * (1 - f0) * (1 - f0), f1 = xb - xbc + 1, am = 0.5 * s * f1 * f1;
            row[ia] += (float)(d * a0);
            if (ib == ia + 2) {
                row[ia + 1] += (float)(d * (1 - a0 - am));
            } else {
                double a1 = s * (1.5 - f0);
                int i;
                row[ia + 1] += (float)(d * (a1 - a0));
                for (i = ia + 2; i < ib - 1; i++)
                    row[i] += (float)(d * s);
                row[ib - 1] += (float)(d * (1 - (a1 + (ib - ia - 3) * s) - am));
            }
            row[ib] += (float)(d * am);
        }
        x = xn;
    }
}

/* The path filled (each piece closed) with a colour: within the clip and the canvas. */
static void fill(struct ctx *c, const struct path *p, int evenodd, unsigned rgb, double alpha)
{
    double box[4];
    int x0, y0, x1, y1, w, band, y, x;
    unsigned i;
    if (!path_box(p, box) || alpha <= 0)
        return;
    x0 = (int)floor(box[0] > c->g.clip[0] ? box[0] : c->g.clip[0]), y0 = (int)floor(box[1] > c->g.clip[1] ? box[1] : c->g.clip[1]);
    x1 = (int)ceil(box[2] < c->g.clip[2] ? box[2] : c->g.clip[2]), y1 = (int)ceil(box[3] < c->g.clip[3] ? box[3] : c->g.clip[3]);
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > c->c->w ? c->c->w : x1, y1 = y1 > c->c->h ? c->c->h : y1;
    if (x1 <= x0 || y1 <= y0)
        return;
    w = x1 - x0 + 2;
    if (w > c->acc_w) {
        float *a = realloc(c->acc, (size_t)w * BAND * sizeof *a);
        if (!a)
            return;
        c->acc = a, c->acc_w = w;
    }
    for (band = y0; band < y1; band += BAND) {
        int rows = y1 - band < BAND ? y1 - band : BAND;
        unsigned from = 0;
        memset(c->acc, 0, (size_t)w * (size_t)rows * sizeof c->acc[0]);
        for (i = 1; i <= p->n; i++)             /* each piece's edges, and the one that closes it */
            if (i == p->n || p->first[i]) {
                unsigned k;
                for (k = from; k < i; k++) {
                    unsigned next = k + 1 < i ? k + 1 : from;
                    edge(c->acc, w, rows, p->xy[2 * k] - x0, p->xy[2 * k + 1] - band, p->xy[2 * next] - x0, p->xy[2 * next + 1] - band);
                }
                from = i;
            }
        for (y = 0; y < rows; y++) {
            const float *row = c->acc + (size_t)y * (size_t)w;
            unsigned *px = c->c->px + (size_t)(band + y) * (size_t)c->c->w + x0;
            double sum = 0;
            for (x = 0; x < x1 - x0; x++) {
                double cover;
                sum += row[x];
                cover = fabs(sum);
                if (evenodd) {
                    cover -= 2 * floor(cover / 2);
                    cover = cover > 1 ? 2 - cover : cover;
                } else if (cover > 1) {
                    cover = 1;
                }
                if (cover > 0.004)
                    blend(&px[x], rgb, (int)(cover * alpha * 256 + 0.5));
            }
        }
    }
}

/* ---- stroking: the line's outline, filled */

static void outline_piece(struct path *o, const double *q, int n)       /* a closed piece, turned one way round */
{
    double area = 0;
    int i;
    for (i = 0; i < n; i++) {
        int j = (i + 1) % n;
        area += q[2 * i] * q[2 * j + 1] - q[2 * j] * q[2 * i + 1];
    }
    for (i = 0; i < n; i++) {
        int k = area >= 0 ? i : n - 1 - i;
        path_point(o, q[2 * k], q[2 * k + 1], i == 0 ? 3 : 0);
    }
}

static void outline_round(struct path *o, double x, double y, double r)
{
    double q[2 * 16];
    int i, n = r < 3 ? 8 : 16;
    for (i = 0; i < n; i++)
        q[2 * i] = x + r * cos(6.283185307179586 * i / n), q[2 * i + 1] = y + r * sin(6.283185307179586 * i / n);
    outline_piece(o, q, n);
}

static void stroke(struct ctx *c)
{
    const struct path *p = &c->p;
    struct path o;
    double scale = sqrt(fabs(c->g.ctm[0] * c->g.ctm[3] - c->g.ctm[1] * c->g.ctm[2])), hw = c->g.width * scale / 2;
    unsigned i, from = 0;
    memset(&o, 0, sizeof o);
    if (hw < 0.5)                               /* (never thinner than a pixel) */
        hw = 0.5;
    for (i = 1; i <= p->n; i++) {
        unsigned k, count;
        int closed;
        if (i < p->n && !p->first[i])
            continue;
        count = i - from, closed = p->first[from] == 3;
        for (k = 0; k < count; k++) {
            unsigned a = from + k, b = from + (k + 1) % count;
            double ax = p->xy[2 * a], ay = p->xy[2 * a + 1], bx = p->xy[2 * b], by = p->xy[2 * b + 1], dx = bx - ax, dy = by - ay;
            double len = sqrt(dx * dx + dy * dy), nx, ny, q[8];
            if (k + 1 == count && !closed)
                break;
            if (len < 1e-6) {
                if (count == 1 || (count == 2 && k == 0))       /* (a dot: drawn where the caps are round) */
                    if (c->g.cap == 1)
                        outline_round(&o, ax, ay, hw);
                continue;
            }
            nx = -dy / len * hw, ny = dx / len * hw;
            if (c->g.cap == 2 && !closed) {     /* square ends: the line longer by half its width */
                if (k == 0)
                    ax -= dx / len * hw, ay -= dy / len * hw;
                if (k + 2 == count)
                    bx += dx / len * hw, by += dy / len * hw;
            }
            q[0] = ax + nx, q[1] = ay + ny, q[2] = bx + nx, q[3] = by + ny;
            q[4] = bx - nx, q[5] = by - ny, q[6] = ax - nx, q[7] = ay - ny;
            outline_piece(&o, q, 4);
            if (hw >= 1 && (k + 2 < count || closed))           /* the corner after it, rounded */
                outline_round(&o, p->xy[2 * b], p->xy[2 * b + 1], hw);
            if (hw >= 1 && c->g.cap == 1 && !closed) {
                if (k == 0)
                    outline_round(&o, p->xy[2 * a], p->xy[2 * a + 1], hw);
                if (k + 2 == count)
                    outline_round(&o, p->xy[2 * b], p->xy[2 * b + 1], hw);
            }
        }
        from = i;
    }
    fill(c, &o, 0, c->g.stroke, c->g.stroke_alpha);
    free(o.xy);
    free(o.first);
}

/* ---- colours */

static unsigned colour(const double *v, int n)  /* by how many numbers it has: gray, RGB, CMYK */
{
    int c[3], i;
    if (n == 1)
        c[0] = c[1] = c[2] = (int)(v[0] * 255 + 0.5);
    else if (n == 3)
        for (i = 0; i < 3; i++)
            c[i] = (int)(v[i] * 255 + 0.5);
    else if (n == 4)
        for (i = 0; i < 3; i++)
            c[i] = (int)((1 - v[i]) * (1 - v[3]) * 255 + 0.5);
    else
        c[0] = c[1] = c[2] = 128;               /* (a pattern or a space not known here) */
    for (i = 0; i < 3; i++)
        c[i] = c[i] < 0 ? 0 : c[i] > 255 ? 255 : c[i];
    return (unsigned)(c[0] << 16 | c[1] << 8 | c[2]);
}

/* ---- pictures */

struct picture {
    unsigned *px;                       /* 00RRGGBBh */
    unsigned char *alpha;               /* or NULL: all of it shows */
    int w, h;
};

/* How many numbers a colour of the space has, and (an Indexed one) its table: *base numbers a colour, *table
   the colours' bytes (the caller frees *own if it is set). 0: not known. */
static int space(struct pdf *d, struct pdf_obj *cs, int *base, const unsigned char **table, unsigned *ntable, unsigned char **own)
{
    const char *n = pdf_name(cs);
    *base = 0, *table = NULL, *ntable = 0, *own = NULL;
    if (pdf_is(cs, PDF_ARRAY)) {
        n = pdf_name(pdf_item(d, cs, 0));
        if (!strcmp(n, "ICCBased"))
            return (int)pdf_int(pdf_get(d, pdf_item(d, cs, 1), "N"), 3);
        if (!strcmp(n, "Indexed") || !strcmp(n, "I")) {
            struct pdf_obj *look = pdf_item(d, cs, 3);
            const unsigned char *t2;
            unsigned n2;
            unsigned char *o2;
            int b2;
            *base = space(d, pdf_item(d, cs, 1), &b2, &t2, &n2, &o2);
            free(o2);
            if (pdf_is(look, PDF_STRING)) {
                *table = (const unsigned char *)pdf_string(look, ntable);
            } else {
                unsigned long len = 0;
                *own = pdf_stream(d, look, &len, NULL, NULL);
                *table = *own, *ntable = (unsigned)len;
            }
            return *base && *table ? 1 : 0;
        }
    }
    if (!strcmp(n, "DeviceGray") || !strcmp(n, "G") || !strcmp(n, "CalGray") || !strcmp(n, "Separation"))
        return 1;
    if (!strcmp(n, "DeviceRGB") || !strcmp(n, "RGB") || !strcmp(n, "CalRGB") || !strcmp(n, "Lab"))
        return 3;
    if (!strcmp(n, "DeviceCMYK") || !strcmp(n, "CMYK"))
        return 4;
    return 0;
}

static unsigned sample(const unsigned char *row, unsigned long i, int bpc)      /* sample i of a row: 0 to 255 */
{
    switch (bpc) {
    case 1: return (row[i >> 3] >> (7 - (i & 7)) & 1) * 255u;
    case 2: return (row[i >> 2] >> (6 - 2 * (i & 3)) & 3) * 85u;
    case 4: return (row[i >> 1] >> (4 - 4 * (i & 1)) & 15) * 17u;
    case 16: return row[2 * i];
    default: return row[i];
    }
}

/* A picture's stream as pixels. mask: it is a stencil (its 1 bits, or 0 bits, take the fill colour `ink`).
   0, or -1: it can't be made out. */
static int picture(struct ctx *c, struct pdf_obj *o, int mask, unsigned ink, struct picture *pic, int deep)
{
    struct pdf *d = c->d;
    long w = pdf_int(pdf_get(d, o, "Width"), 0), h = pdf_int(pdf_get(d, o, "Height"), 0);
    long bpc = mask ? 1 : pdf_int(pdf_get(d, o, "BitsPerComponent"), 8);
    struct pdf_obj *decode = pdf_get(d, o, "Decode"), *smask;
    const unsigned char *table = NULL;
    unsigned char *bytes, *own = NULL;
    unsigned long len = 0, stride, x, y;
    unsigned ntable = 0;
    const char *last = "";
    int n = 1, base = 0, flip = pdf_num(pdf_item(d, decode, 0), 0) > pdf_num(pdf_item(d, decode, 1), 1);
    memset(pic, 0, sizeof *pic);
    if (!(bytes = pdf_stream(d, o, &len, &last, NULL)))
        return -1;
    if (!strcmp(last, "DCTDecode") || !strcmp(last, "DCT")) {
        pic->px = jpeg ? jpeg(bytes, len, &pic->w, &pic->h) : NULL;
        free(bytes);
        if (!pic->px || pic->w < 1 || pic->h < 1 || (unsigned long)pic->w * (unsigned long)pic->h > MAX_PICTURE) {
            free(pic->px);
            pic->px = NULL;
            return -1;
        }
    } else {
        if (*last || w < 1 || h < 1 || (unsigned long)w > MAX_PICTURE / (unsigned long)h
            || (bpc != 1 && bpc != 2 && bpc != 4 && bpc != 8 && bpc != 16)
            || (!mask && !(n = space(d, pdf_get(d, o, "ColorSpace"), &base, &table, &ntable, &own)))) {
            free(bytes);
            free(own);
            return -1;
        }
        stride = ((unsigned long)w * (unsigned long)n * (unsigned long)bpc + 7) / 8;
        pic->w = (int)w, pic->h = (int)h;
        pic->px = malloc((size_t)w * (size_t)h * sizeof *pic->px);
        if (mask)
            pic->alpha = malloc((size_t)w * (size_t)h);
        if (!pic->px || (mask && !pic->alpha) || len / stride < (unsigned long)h) {    /* (too few bytes for it) */
            free(pic->px);
            free(pic->alpha);
            free(bytes);
            free(own);
            pic->px = NULL, pic->alpha = NULL;
            return -1;
        }
        for (y = 0; y < (unsigned long)h; y++) {
            const unsigned char *row = bytes + y * stride;
            unsigned *out = pic->px + y * (unsigned long)w;
            for (x = 0; x < (unsigned long)w; x++) {
                double v[4];
                int k;
                if (mask) {                     /* (a 0 bit is paint, unless Decode turns it round) */
                    out[x] = ink;
                    pic->alpha[y * (unsigned long)w + x] = (unsigned char)((sample(row, x, 1) != 0) == flip ? 255 : 0);
                    continue;
                }
                if (table) {                    /* (a number into the table of colours) */
                    unsigned at = (bpc == 8 ? row[x] : sample(row, x, (int)bpc) / (255u / ((1u << bpc) - 1))) * (unsigned)base;
                    for (k = 0; k < base && k < 4; k++)
                        v[k] = at + (unsigned)k < ntable ? table[at + (unsigned)k] / 255.0 : 0;
                    out[x] = colour(v, base);
                    continue;
                }
                for (k = 0; k < n && k < 4; k++)
                    v[k] = sample(row, x * (unsigned long)n + (unsigned long)k, (int)bpc) / 255.0;
                if (n == 1 && flip)
                    v[0] = 1 - v[0];
                out[x] = colour(v, n);
            }
        }
        free(bytes);
        free(own);
    }
    /* its own soft mask: another picture, of grays, as how much of each pixel shows */
    smask = mask || deep ? NULL : pdf_get(d, o, "SMask");
    if (pdf_is(smask, PDF_DICT)) {
        struct picture m;
        if (picture(c, smask, 0, 0, &m, 1) == 0) {
            if ((pic->alpha = malloc((size_t)pic->w * (size_t)pic->h)) != NULL)
                for (y = 0; y < (unsigned long)pic->h; y++)
                    for (x = 0; x < (unsigned long)pic->w; x++)
                        pic->alpha[y * (unsigned long)pic->w + x] =
                            (unsigned char)(m.px[(y * (unsigned long)m.h / (unsigned long)pic->h) * (unsigned long)m.w
                                                 + x * (unsigned long)m.w / (unsigned long)pic->w] & 255);
            free(m.px);
            free(m.alpha);
        }
    }
    return 0;
}

/* A picture drawn into the unit square as the matrix has it now. */
static void draw_picture(struct ctx *c, const struct picture *pic)
{
    const double *m = c->g.ctm;
    double det = m[0] * m[3] - m[1] * m[2], box[4], cx[4], cy[4];
    int x0, y0, x1, y1, x, y, i, a = (int)(c->g.fill_alpha * 256 + 0.5);
    if (fabs(det) < 1e-9)
        return;
    to_device(m, 0, 0, &cx[0], &cy[0]), to_device(m, 1, 0, &cx[1], &cy[1]);
    to_device(m, 1, 1, &cx[2], &cy[2]), to_device(m, 0, 1, &cx[3], &cy[3]);
    box[0] = box[2] = cx[0], box[1] = box[3] = cy[0];
    for (i = 1; i < 4; i++) {
        box[0] = cx[i] < box[0] ? cx[i] : box[0], box[2] = cx[i] > box[2] ? cx[i] : box[2];
        box[1] = cy[i] < box[1] ? cy[i] : box[1], box[3] = cy[i] > box[3] ? cy[i] : box[3];
    }
    x0 = (int)floor(box[0] > c->g.clip[0] ? box[0] : c->g.clip[0]), y0 = (int)floor(box[1] > c->g.clip[1] ? box[1] : c->g.clip[1]);
    x1 = (int)ceil(box[2] < c->g.clip[2] ? box[2] : c->g.clip[2]), y1 = (int)ceil(box[3] < c->g.clip[3] ? box[3] : c->g.clip[3]);
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > c->c->w ? c->c->w : x1, y1 = y1 > c->c->h ? c->c->h : y1;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {             /* the pixel's middle, back in the picture's square */
            double px = x + 0.5 - m[4], py = y + 0.5 - m[5], u = (m[3] * px - m[2] * py) / det, v = (m[0] * py - m[1] * px) / det;
            int ix, iy, al;
            if (u < 0 || u >= 1 || v < 0 || v >= 1)
                continue;
            ix = (int)(u * pic->w), iy = (int)((1 - v) * pic->h);
            ix = ix >= pic->w ? pic->w - 1 : ix, iy = iy >= pic->h ? pic->h - 1 : iy;
            al = pic->alpha ? pic->alpha[(size_t)iy * (size_t)pic->w + (size_t)ix] * a / 255 : a;
            if (al > 0)
                blend(&c->c->px[(size_t)y * (size_t)c->c->w + (size_t)x], pic->px[(size_t)iy * (size_t)pic->w + (size_t)ix], al);
        }
}

/* ---- text */

static struct pdf_font *font_for(struct ctx *c, struct pdf_obj *resources, const char *name)
{
    struct pdf_obj *dict = pdf_get(c->d, pdf_get(c->d, resources, "Font"), name);
    int i;
    if (!pdf_is(dict, PDF_DICT))
        return NULL;
    for (i = 0; i < c->nfonts; i++)
        if (c->fonts[i].dict == dict)
            return c->fonts[i].f;
    if (c->nfonts == 64)
        return NULL;
    c->fonts[c->nfonts].dict = dict;
    return c->fonts[c->nfonts++].f = pdf_font_open(c->d, dict);
}

static void g_move(void *v, double x, double y)
{
    struct ctx *c = v;
    to_device(c->gm, x, y, &c->gx, &c->gy);
    path_point(&c->tp, c->gx, c->gy, 3);
}

static void g_line(void *v, double x, double y)
{
    struct ctx *c = v;
    to_device(c->gm, x, y, &c->gx, &c->gy);
    path_point(&c->tp, c->gx, c->gy, 0);
}

static void g_curve(void *v, double cx, double cy, double x, double y)  /* (one control point: cut by its length) */
{
    struct ctx *c = v;
    double ax, ay, bx, by, len;
    int i, n;
    to_device(c->gm, cx, cy, &ax, &ay);
    to_device(c->gm, x, y, &bx, &by);
    len = fabs(ax - c->gx) + fabs(ay - c->gy) + fabs(bx - ax) + fabs(by - ay);
    n = len < 1.5 ? 1 : len < 6 ? 2 : len < 20 ? 4 : len < 80 ? 8 : 16;
    for (i = 1; i <= n; i++) {
        double t = (double)i / n, u = 1 - t;
        path_point(&c->tp, u * u * c->gx + 2 * u * t * ax + t * t * bx, u * u * c->gy + 2 * u * t * ay + t * t * by, 0);
    }
    c->gx = bx, c->gy = by;
}

/* A string shown: each code's glyph through the text matrix, which then moves on by its width. */
static void show(struct ctx *c, const unsigned char *s, unsigned len)
{
    const struct ttf_sink sink = { g_move, g_line, g_curve, c };
    struct pdf_font *f = c->g.font;
    unsigned code, n;
    if (!f)
        return;
    while ((n = pdf_font_next(f, s, len, &code)) != 0) {
        double tx, m[6] = { c->g.size * c->g.th, 0, 0, c->g.size, 0, c->g.rise }, ox, oy, em;
        s += n, len -= n;
        memcpy(c->gm, c->g.ctm, sizeof c->gm);
        concat(c->gm, c->tm);
        concat(c->gm, m);
        to_device(c->gm, 0, 0, &ox, &oy);
        em = fabs(c->gm[0]) + fabs(c->gm[1]) + fabs(c->gm[2]) + fabs(c->gm[3]);        /* (no smaller than it is) */
        if ((c->g.mode & 3) != 3 && ox + 2 * em >= c->g.clip[0] && ox - 2 * em <= c->g.clip[2] && oy + 2 * em >= c->g.clip[1]
            && oy - 2 * em <= c->g.clip[3]) {   /* (not the unseen kind, nor out of sight) */
            c->tp.n = 0;
            if (pdf_font_outline(f, code, &sink) == 0 && c->tp.n) {
                if ((c->g.mode & 3) == 0 || (c->g.mode & 3) == 2)
                    fill(c, &c->tp, 0, c->g.fill, c->g.fill_alpha);
                if ((c->g.mode & 3) == 1 || (c->g.mode & 3) == 2) {
                    struct path kept = c->p;
                    c->p = c->tp;
                    stroke(c);
                    c->tp = c->p, c->p = kept;
                }
            }
        }
        tx = (pdf_font_width(f, code) / 1000 * c->g.size + c->g.tc + (pdf_font_is_space(f, code) ? c->g.tw : 0)) * c->g.th;
        c->tm[4] += tx * c->tm[0], c->tm[5] += tx * c->tm[1];
    }
}

static void next_line(struct ctx *c, double tx, double ty)
{
    double m[6] = { 1, 0, 0, 1, 0, 0 };
    m[4] = tx, m[5] = ty;
    concat(c->tlm, m);
    memcpy(c->tm, c->tlm, sizeof c->tm);
}

/* ---- the words */

static void run(struct ctx *c, const unsigned char *bytes, unsigned long len, struct pdf_obj *resources);

static void paint(struct ctx *c, int fill_it, int evenodd, int stroke_it)
{
    if (fill_it)
        fill(c, &c->p, evenodd, c->g.fill, c->g.fill_alpha);
    if (stroke_it)
        stroke(c);
    if (c->clip_next) {                         /* the clip from here on: within the box around this path */
        double box[4];
        if (path_box(&c->p, box)) {
            c->g.clip[0] = box[0] > c->g.clip[0] ? box[0] : c->g.clip[0], c->g.clip[1] = box[1] > c->g.clip[1] ? box[1] : c->g.clip[1];
            c->g.clip[2] = box[2] < c->g.clip[2] ? box[2] : c->g.clip[2], c->g.clip[3] = box[3] < c->g.clip[3] ? box[3] : c->g.clip[3];
        }
        c->clip_next = 0;
    }
    path_clear(&c->p);
}

static void do_object(struct ctx *c, struct pdf_obj *resources, const char *name)
{
    struct pdf *d = c->d;
    struct pdf_obj *o = pdf_get(d, pdf_get(d, resources, "XObject"), name);
    const char *kind = pdf_name(pdf_get(d, o, "Subtype"));
    if (!pdf_is(o, PDF_DICT) || !o->stream)
        return;
    if (!strcmp(kind, "Image")) {
        struct picture pic;
        int mask = pdf_num(pdf_get(d, o, "ImageMask"), 0) != 0 || (pdf_is(pdf_get(d, o, "ImageMask"), PDF_BOOL)
                                                                      && pdf_get(d, o, "ImageMask")->u.num != 0);
        if (picture(c, o, mask, c->g.fill, &pic, 0) == 0) {
            draw_picture(c, &pic);
            free(pic.px);
            free(pic.alpha);
        } else {                                /* (not made out: a gray box where it would be) */
            struct picture none;
            unsigned gray = 0xC0C0C0;
            none.px = &gray, none.alpha = NULL, none.w = none.h = 1;
            draw_picture(c, &none);
        }
    } else if (!strcmp(kind, "Form") && c->forms < 12 && c->depth < 31) {      /* words of its own, drawn here */
        struct pdf_obj *matrix = pdf_get(d, o, "Matrix"), *res = pdf_get(d, o, "Resources");
        unsigned long len = 0;
        unsigned char *bytes = pdf_stream(d, o, &len, NULL, NULL);
        struct path kept = c->p;
        if (!bytes)
            return;
        c->stack[c->depth++] = c->g;
        memset(&c->p, 0, sizeof c->p);
        if (pdf_is(matrix, PDF_ARRAY) && matrix->n >= 6) {
            double m[6];
            int i;
            for (i = 0; i < 6; i++)
                m[i] = pdf_num(pdf_item(d, matrix, (unsigned)i), i == 0 || i == 3);
            concat(c->g.ctm, m);
        }
        c->forms++;
        run(c, bytes, len, res ? res : resources);
        c->forms--;
        free(bytes);
        free(c->p.xy);
        free(c->p.first);
        c->p = kept;
        c->g = c->stack[--c->depth];
    }
}

static void run(struct ctx *c, const unsigned char *bytes, unsigned long len, struct pdf_obj *resources)
{
    struct pdf_words w = { bytes, bytes + len, c->d };
    struct pdf_obj *arg[32], *o;
    char op[16];
    double v[32];
    int n = 0, i, kind, base = c->depth;
    while ((kind = pdf_word(&w, &o, op)) != 0) {
        if (kind == 1) {
            if (n == 32) {                      /* (more than any operator takes: the oldest forgotten) */
                pdf_free(arg[0]);
                memmove(arg, arg + 1, 31 * sizeof arg[0]);
                n--;
            }
            arg[n++] = o;
            continue;
        }
        for (i = 0; i < n; i++)
            v[i] = pdf_num(arg[i], 0);
#define OP(s) (!strcmp(op, s))
        if (OP("q")) {
            if (c->depth < 32)
                c->stack[c->depth++] = c->g;
        } else if (OP("Q")) {
            if (c->depth > base)
                c->g = c->stack[--c->depth];
        } else if (OP("cm") && n >= 6) {
            concat(c->g.ctm, v);
        } else if (OP("m") && n >= 2) {
            path_move(c, v[0], v[1]);
        } else if (OP("l") && n >= 2) {
            path_line(c, v[0], v[1]);
        } else if (OP("c") && n >= 6) {
            path_curve(c, v[0], v[1], v[2], v[3], v[4], v[5]);
        } else if (OP("v") && n >= 4) {
            path_curve(c, c->p.cx, c->p.cy, v[0], v[1], v[2], v[3]);
        } else if (OP("y") && n >= 4) {
            path_curve(c, v[0], v[1], v[2], v[3], v[2], v[3]);
        } else if (OP("h")) {
            path_close(c);
        } else if (OP("re") && n >= 4) {
            path_move(c, v[0], v[1]);
            path_line(c, v[0] + v[2], v[1]);
            path_line(c, v[0] + v[2], v[1] + v[3]);
            path_line(c, v[0], v[1] + v[3]);
            path_close(c);
        } else if (OP("S")) {
            paint(c, 0, 0, 1);
        } else if (OP("s")) {
            path_close(c);
            paint(c, 0, 0, 1);
        } else if (OP("f") || OP("F")) {
            paint(c, 1, 0, 0);
        } else if (OP("f*")) {
            paint(c, 1, 1, 0);
        } else if (OP("B")) {
            paint(c, 1, 0, 1);
        } else if (OP("B*")) {
            paint(c, 1, 1, 1);
        } else if (OP("b")) {
            path_close(c);
            paint(c, 1, 0, 1);
        } else if (OP("b*")) {
            path_close(c);
            paint(c, 1, 1, 1);
        } else if (OP("n")) {
            paint(c, 0, 0, 0);
        } else if (OP("W") || OP("W*")) {
            c->clip_next = 1;
        } else if (OP("w") && n >= 1) {
            c->g.width = v[0] < 0 ? 0 : v[0];
        } else if (OP("J") && n >= 1) {
            c->g.cap = (int)v[0];
        } else if (OP("j") && n >= 1) {
            c->g.join = (int)v[0];
        } else if (OP("g") && n >= 1) {
            c->g.fill = colour(v, 1);
        } else if (OP("G") && n >= 1) {
            c->g.stroke = colour(v, 1);
        } else if (OP("rg") && n >= 3) {
            c->g.fill = colour(v, 3);
        } else if (OP("RG") && n >= 3) {
            c->g.stroke = colour(v, 3);
        } else if (OP("k") && n >= 4) {
            c->g.fill = colour(v, 4);
        } else if (OP("K") && n >= 4) {
            c->g.stroke = colour(v, 4);
        } else if (OP("sc") || OP("scn")) {     /* (by how many numbers: a name among them is a pattern) */
            c->g.fill = n && pdf_is(arg[n - 1], PDF_NAME) ? 0x808080 : colour(v, n);
        } else if (OP("SC") || OP("SCN")) {
            c->g.stroke = n && pdf_is(arg[n - 1], PDF_NAME) ? 0x808080 : colour(v, n);
        } else if (OP("cs")) {
            c->g.fill = 0;
        } else if (OP("CS")) {
            c->g.stroke = 0;
        } else if (OP("gs") && n >= 1) {        /* a named set of settings: how much shows, the line's width */
            struct pdf_obj *e = pdf_get(c->d, pdf_get(c->d, resources, "ExtGState"), pdf_name(arg[0]));
            c->g.fill_alpha = pdf_num(pdf_get(c->d, e, "ca"), c->g.fill_alpha);
            c->g.stroke_alpha = pdf_num(pdf_get(c->d, e, "CA"), c->g.stroke_alpha);
            c->g.width = pdf_num(pdf_get(c->d, e, "LW"), c->g.width);
        } else if (OP("Do") && n >= 1) {
            do_object(c, resources, pdf_name(arg[0]));
        } else if (OP("BT")) {
            static const double same[6] = { 1, 0, 0, 1, 0, 0 };
            memcpy(c->tm, same, sizeof same), memcpy(c->tlm, same, sizeof same);
        } else if (OP("Tf") && n >= 2) {
            c->g.font = font_for(c, resources, pdf_name(arg[0]));
            c->g.size = v[1];
        } else if (OP("Td") && n >= 2) {
            next_line(c, v[0], v[1]);
        } else if (OP("TD") && n >= 2) {
            c->g.tl = -v[1];
            next_line(c, v[0], v[1]);
        } else if (OP("Tm") && n >= 6) {
            memcpy(c->tm, v, sizeof c->tm), memcpy(c->tlm, v, sizeof c->tlm);
        } else if (OP("T*")) {
            next_line(c, 0, -c->g.tl);
        } else if ((OP("Tj") || OP("'") || OP("\"")) && n >= 1 && pdf_is(arg[n - 1], PDF_STRING)) {
            unsigned sl = 0;
            const char *str = pdf_string(arg[n - 1], &sl);
            if (op[0] == '"' && n >= 3)
                c->g.tw = v[0], c->g.tc = v[1];
            if (op[0] != 'T')
                next_line(c, 0, -c->g.tl);
            show(c, (const unsigned char *)str, sl);
        } else if (OP("TJ") && n >= 1 && pdf_is(arg[0], PDF_ARRAY)) {  /* strings, and numbers that close them up */
            unsigned k;
            for (k = 0; k < arg[0]->n; k++) {
                const struct pdf_obj *e = arg[0]->u.item[k];
                if (pdf_is(e, PDF_STRING)) {
                    show(c, (const unsigned char *)e->u.s, e->n);
                } else if (pdf_is(e, PDF_NUMBER)) {
                    double tx = -e->u.num / 1000 * c->g.size * c->g.th;
                    c->tm[4] += tx * c->tm[0], c->tm[5] += tx * c->tm[1];
                }
            }
        } else if (OP("Tc") && n >= 1) {
            c->g.tc = v[0];
        } else if (OP("Tw") && n >= 1) {
            c->g.tw = v[0];
        } else if (OP("Tz") && n >= 1) {
            c->g.th = v[0] / 100;
        } else if (OP("TL") && n >= 1) {
            c->g.tl = v[0];
        } else if (OP("Ts") && n >= 1) {
            c->g.rise = v[0];
        } else if (OP("Tr") && n >= 1) {
            c->g.mode = (int)v[0] & 7;
        } else if (OP("BI")) {                  /* a picture among the words: passed over, to after its "EI" */
            const unsigned char *p = w.p;
            while (p + 2 < w.end && !(p[0] == 'I' && p[1] == 'D' && (p[2] == 32 || p[2] == 10 || p[2] == 13)))
                p++;
            for (p += 2; p + 3 < w.end; p++)
                if ((p[0] == 32 || p[0] == 10 || p[0] == 13) && p[1] == 'E' && p[2] == 'I' && (p[3] == 32 || p[3] == 10 || p[3] == 13))
                    break;
            w.p = p + 3 < w.end ? p + 3 : w.end;
        }
#undef OP
        for (i = 0; i < n; i++)
            pdf_free(arg[i]);
        n = 0;
    }
    for (i = 0; i < n; i++)
        pdf_free(arg[i]);
    if (c->depth > base)                        /* (more saved than restored: back to where it began) */
        c->g = c->stack[base], c->depth = base;
}

/* ---- pages */

static int page_box(struct pdf *d, struct pdf_obj *page, double box[4], int *turn)
{
    struct pdf_obj *b = pdf_page_attr(d, page, "CropBox");
    int i;
    if (!pdf_is(b, PDF_ARRAY) || b->n < 4)
        b = pdf_page_attr(d, page, "MediaBox");
    box[0] = box[1] = 0, box[2] = 612, box[3] = 792;
    if (pdf_is(b, PDF_ARRAY) && b->n >= 4)
        for (i = 0; i < 4; i++)
            box[i] = pdf_num(pdf_item(d, b, (unsigned)i), box[i]);
    if (box[0] > box[2]) {
        double t = box[0];
        box[0] = box[2], box[2] = t;
    }
    if (box[1] > box[3]) {
        double t = box[1];
        box[1] = box[3], box[3] = t;
    }
    if (!(box[2] - box[0] >= 1 && box[2] - box[0] < 20000 && box[3] - box[1] >= 1 && box[3] - box[1] < 20000))
        box[0] = box[1] = 0, box[2] = 612, box[3] = 792;
    *turn = (int)(((pdf_int(pdf_page_attr(d, page, "Rotate"), 0) % 360) + 360) % 360) / 90;
    return 0;
}

int pdf_page_size(struct pdf *d, unsigned n, double *w, double *h)
{
    struct pdf_obj *page = pdf_page(d, n);
    double box[4];
    int turn;
    *w = 612, *h = 792;
    if (!page)
        return PDF_E_DAMAGED;
    page_box(d, page, box, &turn);
    *w = turn & 1 ? box[3] - box[1] : box[2] - box[0];
    *h = turn & 1 ? box[2] - box[0] : box[3] - box[1];
    return PDF_OK;
}

int pdf_draw(struct pdf *d, unsigned n, double scale, int ox, int oy, struct pdf_canvas *cv)
{
    struct pdf_obj *page = pdf_page(d, n), *contents, *resources;
    struct ctx *c;
    double box[4], w, h;
    int turn;
    unsigned i, count;
    if (!page)
        return PDF_E_DAMAGED;
    if (!(c = calloc(1, sizeof *c)))
        return PDF_E_MEMORY;
    c->d = d, c->c = cv;
    page_box(d, page, box, &turn);
    w = box[2] - box[0], h = box[3] - box[1];
    /* the page's points to the device's pixels: its top left at the top left, as it is turned */
    switch (turn) {
    case 1:
        c->g.ctm[0] = 0, c->g.ctm[1] = scale, c->g.ctm[2] = scale, c->g.ctm[3] = 0;
        c->g.ctm[4] = -box[1] * scale, c->g.ctm[5] = -box[0] * scale;
        break;
    case 2:
        c->g.ctm[0] = -scale, c->g.ctm[1] = 0, c->g.ctm[2] = 0, c->g.ctm[3] = scale;
        c->g.ctm[4] = box[2] * scale, c->g.ctm[5] = -box[1] * scale;
        break;
    case 3:
        c->g.ctm[0] = 0, c->g.ctm[1] = -scale, c->g.ctm[2] = -scale, c->g.ctm[3] = 0;
        c->g.ctm[4] = box[3] * scale, c->g.ctm[5] = box[2] * scale;
        break;
    default:
        c->g.ctm[0] = scale, c->g.ctm[1] = 0, c->g.ctm[2] = 0, c->g.ctm[3] = -scale;
        c->g.ctm[4] = -box[0] * scale, c->g.ctm[5] = box[3] * scale;
    }
    c->g.ctm[4] -= ox, c->g.ctm[5] -= oy;
    c->g.fill = c->g.stroke = 0, c->g.fill_alpha = c->g.stroke_alpha = 1, c->g.width = 1, c->g.th = 1;
    c->g.clip[0] = -ox, c->g.clip[1] = -oy;
    c->g.clip[2] = (turn & 1 ? h : w) * scale - ox, c->g.clip[3] = (turn & 1 ? w : h) * scale - oy;
    resources = pdf_page_attr(d, page, "Resources");
    contents = pdf_get(d, page, "Contents");
    count = pdf_is(contents, PDF_ARRAY) ? contents->n : 1;
    {                                           /* its streams are one run of words: put end to end */
        unsigned char *all = NULL;
        unsigned long total = 0;
        for (i = 0; i < count && i < 4096; i++) {
            unsigned long len = 0;
            unsigned char *b = pdf_stream(d, pdf_is(contents, PDF_ARRAY) ? pdf_item(d, contents, i) : contents, &len, NULL, NULL), *more;
            if (!b)
                continue;
            if ((more = realloc(all, total + len + 2)) != NULL) {
                all = more;
                memcpy(all + total, b, len);
                total += len;
                all[total++] = '\n';
            }
            free(b);
        }
        if (all)
            run(c, all, total, resources);
        free(all);
    }
    for (i = 0; i < (unsigned)c->nfonts; i++)
        pdf_font_close(c->fonts[i].f);
    free(c->tp.xy);
    free(c->tp.first);
    free(c->p.xy);
    free(c->p.first);
    free(c->acc);
    free(c);
    return PDF_OK;
}
