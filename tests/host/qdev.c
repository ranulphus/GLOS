/* qdev.c - a drawing engine for the host tests (gfx.h's struct gfx_dev; M7
 * step D): like the card's, its work waits in a queue and is done only when
 * it is synced, in order, so the 2D library and the composer are tried with
 * the pixels behind what they asked for. What the card takes from the CPU at
 * once (pixels from memory, 1-bit pictures) is taken at once here too. It
 * draws with its own loops on pixel values; qdev_refuse makes it decline
 * some calls, for the software to do after it.
 *
 * One engine for every surface given qdev. */
#include <stdlib.h>
#include <string.h>
#include "qdev.h"

enum { Q_FILL, Q_MEM, Q_DEV, Q_MONO };

struct q {
    int kind;
    struct gfx_surface *d;
    struct gfx_rect r;
    unsigned px, fg, bg;
    const struct gfx_surface *s;                /* Q_DEV: the source, read when done */
    int sx, sy, x, y, w, h;
    unsigned stride;
    unsigned char *data;                        /* Q_MEM: the pixels; Q_MONO: the bits */
};

static struct q *queue;
static unsigned n, room, refuse;
unsigned long qdev_done;

void qdev_refuse(unsigned one_in) { refuse = one_in; }

static unsigned rnd_state = 99;
static int declined(void)
{
    rnd_state = rnd_state * 1664525u + 1013904223u;
    return refuse && (rnd_state >> 8) % refuse == 0;
}

static struct q *push(void)
{
    if (n == room) {
        room = room ? room * 2 : 64;
        queue = realloc(queue, room * sizeof *queue);
    }
    memset(&queue[n], 0, sizeof queue[n]);
    return &queue[n++];
}

static unsigned char *at(const struct gfx_surface *s, int x, int y)
{
    return s->pix + (unsigned)y * s->pitch + (unsigned)x * (s->fmt.bpp / 8u);
}

static void put(const struct gfx_surface *s, int x, int y, unsigned px)
{
    unsigned char *p = at(s, x, y);
    if (s->fmt.bpp == 8) *p = (unsigned char)px;
    else if (s->fmt.bpp == 16) *(unsigned short *)p = (unsigned short)px;
    else *(unsigned *)p = px;
}

static int q_fill(struct gfx_surface *s, const struct gfx_rect *r, unsigned px)
{
    struct q *e;
    if (declined())
        return 0;
    e = push();
    e->kind = Q_FILL, e->d = s, e->r = *r, e->px = px;
    return 1;
}

static int q_copy(struct gfx_surface *d, const struct gfx_rect *r, const struct gfx_surface *s, int sx, int sy)
{
    unsigned row = (unsigned)r->w * (d->fmt.bpp / 8u);
    struct q *e;
    int j;
    if (declined())
        return 0;
    e = push();
    e->d = d, e->r = *r, e->s = s, e->sx = sx, e->sy = sy;
    if (s->dev) {
        e->kind = Q_DEV;
        return 1;
    }
    e->kind = Q_MEM;
    e->data = malloc(row * (unsigned)r->h);
    for (j = 0; j < r->h; j++)
        memcpy(e->data + row * (unsigned)j, at(s, sx, sy + j), row);
    return 1;
}

static int q_mono(struct gfx_surface *s, const struct gfx_rect *r, int x, int y, const unsigned char *bits, int w, int h,
                  unsigned stride, unsigned fg, unsigned bg)
{
    struct q *e;
    if (declined())
        return 0;
    e = push();
    e->kind = Q_MONO, e->d = s, e->r = *r, e->x = x, e->y = y, e->w = w, e->h = h, e->stride = stride, e->fg = fg,
    e->bg = bg;
    e->data = malloc(stride * (unsigned)h + 1);
    memcpy(e->data, bits, stride * (unsigned)h);
    return 1;
}

static void run(struct q *e)
{
    unsigned bytes = e->d->fmt.bpp / 8u, row = (unsigned)e->r.w * bytes;
    int i, j;
    switch (e->kind) {
    case Q_FILL:
        for (j = 0; j < e->r.h; j++)
            for (i = 0; i < e->r.w; i++)
                put(e->d, e->r.x + i, e->r.y + j, e->px);
        break;
    case Q_MEM:
        for (j = 0; j < e->r.h; j++)
            memcpy(at(e->d, e->r.x, e->r.y + j), e->data + row * (unsigned)j, row);
        break;
    case Q_DEV: {                               /* the source as it is now, all of it before any is written */
        unsigned char *keep = malloc(row * (unsigned)e->r.h);
        for (j = 0; j < e->r.h; j++)
            memcpy(keep + row * (unsigned)j, at(e->s, e->sx, e->sy + j), row);
        for (j = 0; j < e->r.h; j++)
            memcpy(at(e->d, e->r.x, e->r.y + j), keep + row * (unsigned)j, row);
        free(keep);
        break;
    }
    default:
        for (j = 0; j < e->r.h; j++)
            for (i = 0; i < e->r.w; i++) {
                int k = e->r.x + i - e->x, l = e->r.y + j - e->y;
                if (e->data[(unsigned)l * e->stride + (unsigned)k / 8] & (0x80u >> (k & 7)))
                    put(e->d, e->r.x + i, e->r.y + j, e->fg);
                else if (e->bg != GFX_NONE)
                    put(e->d, e->r.x + i, e->r.y + j, e->bg);
            }
    }
}

static void q_sync(const struct gfx_surface *s)
{
    unsigned i;
    (void)s;
    for (i = 0; i < n; i++) {
        run(&queue[i]);
        free(queue[i].data);
        qdev_done++;
    }
    n = 0;
}

const struct gfx_dev qdev = { q_fill, q_copy, q_mono, q_sync };

void qdev_sync(void) { q_sync(0); }
