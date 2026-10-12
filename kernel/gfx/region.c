/* Areas as lists of rectangles that don't overlap (kernel/include/gfx.h):
 * what of a window can be seen, what of the screen must be drawn again. A
 * region holds GFX_REGION_MAX rectangles at most; one that would need more
 * becomes its own bounding rectangle, which covers too much rather than too
 * little (`rough` says so: the window manager draws again what it must,
 * and never trusts a rough region to say what is hidden). */
#include "gfx.h"

int gfx_rect_cut(struct gfx_rect *r, const struct gfx_rect *by)
{
    s32 x0, y0, x1, y1;
    if (r->w <= 0 || r->h <= 0 || by->w <= 0 || by->h <= 0) {
        r->w = r->h = 0;
        return 0;
    }
    x0 = r->x > by->x ? r->x : by->x;
    y0 = r->y > by->y ? r->y : by->y;
    x1 = r->x + r->w < by->x + by->w ? r->x + r->w : by->x + by->w;
    y1 = r->y + r->h < by->y + by->h ? r->y + r->h : by->y + by->h;
    if (x1 <= x0 || y1 <= y0) {
        r->w = r->h = 0;
        return 0;
    }
    r->x = x0;
    r->y = y0;
    r->w = x1 - x0;
    r->h = y1 - y0;
    return 1;
}

void gfx_region_set(struct gfx_region *g, const struct gfx_rect *r)
{
    g->n = 0;
    g->rough = 0;
    if (r && r->w > 0 && r->h > 0)
        g->r[g->n++] = *r;
}

void gfx_region_bounds(const struct gfx_region *g, struct gfx_rect *out)
{
    s32 x0, y0, x1, y1;
    u32 i;
    out->x = out->y = out->w = out->h = 0;
    if (!g->n)
        return;
    x0 = g->r[0].x, y0 = g->r[0].y, x1 = x0 + g->r[0].w, y1 = y0 + g->r[0].h;
    for (i = 1; i < g->n; i++) {
        const struct gfx_rect *r = &g->r[i];
        if (r->x < x0) x0 = r->x;
        if (r->y < y0) y0 = r->y;
        if (r->x + r->w > x1) x1 = r->x + r->w;
        if (r->y + r->h > y1) y1 = r->y + r->h;
    }
    out->x = x0, out->y = y0, out->w = x1 - x0, out->h = y1 - y0;
}

/* One more rectangle (it overlaps none there): or the bounding rectangle of
   it all, when there is no room. */
static void push(struct gfx_region *g, s32 x, s32 y, s32 w, s32 h)
{
    if (w <= 0 || h <= 0)
        return;
    if (g->n == GFX_REGION_MAX) {
        struct gfx_rect b, r = { x, y, w, h };
        struct gfx_region one;
        gfx_region_bounds(g, &b);
        gfx_region_set(&one, &b);
        one.r[one.n++] = r;
        gfx_region_bounds(&one, &b);
        gfx_region_set(g, &b);
        g->rough = 1;
        return;
    }
    g->r[g->n].x = x, g->r[g->n].y = y, g->r[g->n].w = w, g->r[g->n].h = h;
    g->n++;
}

void gfx_region_sub(struct gfx_region *g, const struct gfx_rect *cut)
{
    struct gfx_region out;
    u32 i;
    if (cut->w <= 0 || cut->h <= 0)
        return;
    out.n = 0;
    out.rough = g->rough;
    for (i = 0; i < g->n; i++) {
        struct gfx_rect r = g->r[i], in = r;
        if (!gfx_rect_cut(&in, cut)) {
            push(&out, r.x, r.y, r.w, r.h);
            continue;
        }
        push(&out, r.x, r.y, r.w, in.y - r.y);                          /* above */
        push(&out, r.x, in.y + in.h, r.w, r.y + r.h - in.y - in.h);     /* below */
        push(&out, r.x, in.y, in.x - r.x, in.h);                        /* left */
        push(&out, in.x + in.w, in.y, r.x + r.w - in.x - in.w, in.h);   /* right */
    }
    if (out.rough && !g->rough) {               /* no room for the pieces: left whole, which covers too much */
        g->rough = 1;
        return;
    }
    *g = out;
}

void gfx_region_add(struct gfx_region *g, const struct gfx_rect *r)
{
    struct gfx_region rest;
    u32 i;
    if (r->w <= 0 || r->h <= 0)
        return;
    gfx_region_set(&rest, r);                   /* what of r the region hasn't got yet */
    for (i = 0; i < g->n && rest.n; i++)
        gfx_region_sub(&rest, &g->r[i]);
    for (i = 0; i < rest.n; i++)
        push(g, rest.r[i].x, rest.r[i].y, rest.r[i].w, rest.r[i].h);
    if (rest.rough && !g->rough) {              /* (r's own pieces had no room: r whole, over what was there) */
        struct gfx_rect b;
        gfx_region_bounds(g, &b);
        gfx_region_set(&rest, &b);
        rest.r[rest.n++] = *r;
        gfx_region_bounds(&rest, &b);
        gfx_region_set(g, &b);
        g->rough = 1;
    }
}

void gfx_region_cut(struct gfx_region *g, const struct gfx_rect *by)
{
    u32 i, n = 0;
    for (i = 0; i < g->n; i++) {
        struct gfx_rect r = g->r[i];
        if (gfx_rect_cut(&r, by))
            g->r[n++] = r;
    }
    g->n = n;
}

int gfx_region_has(const struct gfx_region *g, s32 x, s32 y)
{
    u32 i;
    for (i = 0; i < g->n; i++)
        if (x >= g->r[i].x && y >= g->r[i].y && x - g->r[i].x < g->r[i].w && y - g->r[i].y < g->r[i].h)
            return 1;
    return 0;
}
