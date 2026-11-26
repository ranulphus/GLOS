/* LIM EMS 4.0 of GLOS's own: the manager's bookkeeping (ems.h). */
#include <string.h>

#include "ems.h"

#define NONE 0xFFFFu

static void ah(struct ems_regs *r, unsigned code) { r->ax = (r->ax & 0xFF) | (code & 0xFF) << 8; }
static void al(struct ems_regs *r, unsigned v) { r->ax = (r->ax & 0xFF00) | (v & 0xFF); }
static unsigned long lin(unsigned seg, unsigned off) { return (unsigned long)(seg & 0xFFFF) * 16 + (off & 0xFFFF); }

static int in_mem(const struct ems *e, unsigned long at, unsigned long n)
{
    return at <= e->mem_size && n <= e->mem_size - at;
}

static unsigned rd16(const struct ems *e, unsigned long at)
{
    return in_mem(e, at, 2) ? (unsigned)(e->mem[at] | e->mem[at + 1] << 8) : 0;
}

static void wr16(struct ems *e, unsigned long at, unsigned v)
{
    if (in_mem(e, at, 2))
        e->mem[at] = (unsigned char)v, e->mem[at + 1] = (unsigned char)(v >> 8);
}

static unsigned long rd32(const struct ems *e, unsigned long at) { return rd16(e, at) | (unsigned long)rd16(e, at + 2) << 16; }

static int valid(const struct ems *e, unsigned h) { return h < EMS_HANDLES && e->h[h].used; }

void ems_init(struct ems *e, const struct ems_ops *ops, unsigned total, unsigned frame_seg, unsigned char *mem,
              unsigned long mem_size, void *(*grow)(void *old, unsigned long bytes))
{
    int i;
    memset(e, 0, sizeof *e);
    e->ops = *ops;
    e->total = e->free = total;
    e->frame_seg = frame_seg;
    e->mem = mem, e->mem_size = mem_size;
    e->grow = grow;
    e->h[0].used = 1;                           /* (the operating system's handle: no pages) */
    for (i = 0; i < EMS_PHYS; i++)
        e->map_h[i] = NONE, e->map_l[i] = 0;
}

static void map(struct ems *e, int phys, unsigned h, unsigned l)
{
    e->map_h[phys] = (unsigned short)h, e->map_l[phys] = (unsigned short)l;
    e->ops.map(e->ops.ctx, phys, h == NONE ? EMS_UNMAPPED : e->h[h].page[l]);
}

/* A handle's pages to n: more allocated (00h, or 88h with nothing changed), or the last given back (its mapped
   physical pages unmapped). */
static unsigned resize(struct ems *e, unsigned h, unsigned n)
{
    struct ems_handle *x = &e->h[h];
    unsigned k;
    int p;

    if (n > x->n) {
        if (n - x->n > e->free)
            return n - x->n > e->total ? 0x87 : 0x88;
        if (n > x->cap) {
            unsigned long *np = e->grow(x->page, (unsigned long)n * sizeof *np);
            if (!np)
                return 0x80;
            x->page = np, x->cap = n;
        }
        for (k = x->n; k < n; k++) {
            unsigned long id = e->ops.alloc(e->ops.ctx);
            if (id == EMS_UNMAPPED) {           /* (fewer frames than counted: what was got goes back) */
                while (k > x->n)
                    e->ops.free(e->ops.ctx, x->page[--k]);
                return 0x88;
            }
            x->page[k] = id;
        }
        e->free -= n - x->n;
        x->n = n;
        return 0;
    }
    for (p = 0; p < EMS_PHYS; p++)
        if (e->map_h[p] == h && e->map_l[p] >= n)
            map(e, p, NONE, 0);
    for (k = n; k < x->n; k++)
        e->ops.free(e->ops.ctx, x->page[k]);
    e->free += x->n - n;
    x->n = n;
    return 0;
}

static unsigned new_handle(struct ems *e, unsigned n, unsigned *out)
{
    unsigned h, r;
    for (h = 1; h < EMS_HANDLES && e->h[h].used; h++)
        ;
    if (h == EMS_HANDLES)
        return 0x85;
    if (n > e->total)
        return 0x87;
    if (n > e->free)
        return 0x88;
    memset(&e->h[h], 0, sizeof e->h[h]);
    e->h[h].used = 1;
    if ((r = resize(e, h, n)) != 0) {
        e->h[h].used = 0;
        return r;
    }
    *out = h;
    return 0;
}

/* A physical page by its segment in the page frame: its number, or -1. */
static int phys_of(const struct ems *e, unsigned seg)
{
    unsigned d = (seg & 0xFFFF) - e->frame_seg;
    if ((seg & 0xFFFF) < e->frame_seg || d % 0x400 || d / 0x400 >= EMS_PHYS)
        return -1;
    return (int)(d / 0x400);
}

/* A map as 4Eh keeps it: each physical page's handle and logical page, a word each. */
#define MAP_SIZE (EMS_PHYS * 4)

static void map_out(struct ems *e, unsigned long at)
{
    int p;
    for (p = 0; p < EMS_PHYS; p++)
        wr16(e, at + 4u * p, e->map_h[p]), wr16(e, at + 4u * p + 2, e->map_l[p]);
}

static unsigned map_in(struct ems *e, unsigned long at)
{
    unsigned hh[EMS_PHYS], ll[EMS_PHYS];
    int p;
    for (p = 0; p < EMS_PHYS; p++) {
        hh[p] = rd16(e, at + 4u * p), ll[p] = rd16(e, at + 4u * p + 2);
        if (hh[p] != NONE && (!valid(e, hh[p]) || ll[p] >= e->h[hh[p]].n))
            return 0xA3;                        /* (a map that isn't one: nothing changed) */
    }
    for (p = 0; p < EMS_PHYS; p++)
        map(e, p, hh[p], ll[p]);
    return 0;
}

/* ---- 57h: a region moved or exchanged */

struct region {
    int ems;                            /* 0: conventional memory */
    unsigned h;
    unsigned long at;                   /* conventional: its linear address; expanded: page * 16 KB + offset */
};

static unsigned region_in(struct ems *e, unsigned long d, unsigned long len, struct region *g)
{
    unsigned type = e->mem[d], h = rd16(e, d + 1), off = rd16(e, d + 3), sp = rd16(e, d + 5);
    if (type > 1)
        return 0x98;
    g->ems = (int)type, g->h = h;
    if (!type) {
        g->at = lin(sp, off);
        return g->at + len > 0x100000UL ? 0xA2 : 0;
    }
    if (!valid(e, h))
        return 0x83;
    if (off >= EMS_PAGE)
        return 0x95;
    g->at = (unsigned long)sp * EMS_PAGE + off;
    return g->at + len > (unsigned long)e->h[h].n * EMS_PAGE ? 0x93 : 0;
}

/* The byte at a region's place, and how many follow it there contiguously (to the next 4 KB boundary of an
   expanded page, or the end of conventional memory). */
static unsigned char *byte_at(struct ems *e, const struct region *g, unsigned long pos, unsigned long *run)
{
    unsigned long a = g->at + pos;
    if (!g->ems) {
        *run = a < e->mem_size ? e->mem_size - a : 0;
        return a < e->mem_size ? e->mem + a : 0;
    }
    *run = 4096 - (a % 4096);
    return e->ops.page_at(e->ops.ctx, e->h[g->h].page[a / EMS_PAGE], (unsigned)(a % EMS_PAGE));
}

static int overlap(const struct region *a, const struct region *b, unsigned long len)
{
    if (a->ems != b->ems || (a->ems && a->h != b->h))
        return 0;
    return a->at < b->at + len && b->at < a->at + len;
}

static unsigned move(struct ems *e, unsigned long d, int exchange)
{
    struct region s, t;
    unsigned long len = rd32(e, d), pos, n;
    unsigned r;
    int back, warn;

    if (len > 0x100000UL)
        return 0x96;
    if ((r = region_in(e, d + 4, len, &s)) != 0 || (r = region_in(e, d + 11, len, &t)) != 0)
        return r;
    warn = overlap(&s, &t, len);
    if (warn && exchange)
        return 0x97;
    back = warn && t.at > s.at;                 /* (an overlapping move done from its end, as memmove does) */
    for (pos = 0; pos < len; pos += n) {
        unsigned long rs, rt, at = back ? len - pos : pos, k;
        unsigned char *ps, *pt;
        if (back) {                             /* (a chunk ending at `at`: from its start within both runs) */
            unsigned long cs = (s.at + at - 1) % 4096 + 1, ct = (t.at + at - 1) % 4096 + 1;
            n = cs < ct ? cs : ct;
            if (!s.ems && !t.ems)
                n = at;                         /* (conventional memory: one run) */
            if (n > at)
                n = at;
            ps = byte_at(e, &s, at - n, &rs), pt = byte_at(e, &t, at - n, &rt);
            if (!ps || !pt)
                return 0x80;
            memmove(pt, ps, n);
            continue;
        }
        ps = byte_at(e, &s, at, &rs), pt = byte_at(e, &t, at, &rt);
        if (!ps || !pt)
            return 0x80;
        n = len - pos;
        if (n > rs)
            n = rs;
        if (n > rt)
            n = rt;
        if (exchange)
            for (k = 0; k < n; k++) {
                unsigned char c = ps[k];
                ps[k] = pt[k], pt[k] = c;
            }
        else
            memmove(pt, ps, n);
    }
    return warn ? 0x92 : 0;                     /* (92h: done, the regions overlapping) */
}

/* ---- INT 67h */

void ems_call(struct ems *e, struct ems_regs *r)
{
    unsigned fn = r->ax >> 8 & 0xFF, sub = r->ax & 0xFF, h = r->dx & 0xFFFF, code = 0, k, n;
    int p;

    switch (fn) {
    case 0x40:                                  /* status */
        break;
    case 0x41:                                  /* the page frame's segment */
        r->bx = e->frame_seg;
        break;
    case 0x42:                                  /* pages free, pages in all */
        r->bx = e->free, r->dx = e->total;
        break;
    case 0x43:                                  /* pages allocated to a new handle */
        if (!r->bx)
            code = 0x89;
        else if ((code = new_handle(e, r->bx, &k)) == 0)
            r->dx = k;
        break;
    case 0x44:                                  /* a handle's page shown at a physical page, or none there */
        if (!valid(e, h))
            code = 0x83;
        else if (sub >= EMS_PHYS)
            code = 0x8B;
        else if (r->bx != NONE && r->bx >= e->h[h].n)
            code = 0x8A;
        else
            map(e, (int)sub, r->bx == NONE ? NONE : h, r->bx == NONE ? 0 : r->bx);
        break;
    case 0x45:                                  /* a handle's pages given back */
        if (!valid(e, h))
            code = 0x83;
        else if (e->h[h].saved)
            code = 0x86;
        else {
            for (p = 0; p < EMS_PHYS; p++)
                if (e->map_h[p] == h)
                    map(e, p, NONE, 0);
            resize(e, h, 0);
            if (h) {
                if (e->h[h].page)
                    e->grow(e->h[h].page, 0);
                memset(&e->h[h], 0, sizeof e->h[h]);
            }
        }
        break;
    case 0x46:
        al(r, 0x40);                            /* LIM 4.0 */
        break;
    case 0x47:                                  /* the map saved for a handle */
        if (!valid(e, h))
            code = 0x83;
        else if (e->h[h].saved)
            code = 0x8D;
        else {
            for (p = 0; p < EMS_PHYS; p++)
                e->h[h].save_h[p] = e->map_h[p], e->h[h].save_l[p] = e->map_l[p];
            e->h[h].saved = 1;
        }
        break;
    case 0x48:                                  /* ... and put back */
        if (!valid(e, h))
            code = 0x83;
        else if (!e->h[h].saved)
            code = 0x8E;
        else {
            for (p = 0; p < EMS_PHYS; p++) {
                unsigned sh = e->h[h].save_h[p], sl = e->h[h].save_l[p];
                if (sh != NONE && (!valid(e, sh) || sl >= e->h[sh].n))
                    sh = NONE, sl = 0;          /* (a page given back since: nothing there) */
                map(e, p, sh, sl);
            }
            e->h[h].saved = 0;
        }
        break;
    case 0x4B:                                  /* the handles in use */
        for (k = 0, n = 0; k < EMS_HANDLES; k++)
            n += e->h[k].used != 0;
        r->bx = n;
        break;
    case 0x4C:
        if (!valid(e, h))
            code = 0x83;
        else
            r->bx = e->h[h].n;
        break;
    case 0x4D:                                  /* every handle and its pages, at ES:DI */
        for (k = 0, n = 0; k < EMS_HANDLES; k++)
            if (e->h[k].used) {
                wr16(e, lin(r->es, r->di) + 4u * n, k);
                wr16(e, lin(r->es, r->di) + 4u * n + 2, e->h[k].n);
                n++;
            }
        r->bx = n;
        break;
    case 0x4E:                                  /* the whole map: to ES:DI, from DS:SI, both, its size */
        if (sub == 0 || sub == 2)
            map_out(e, lin(r->es, r->di));
        if (sub == 1 || sub == 2)
            code = map_in(e, lin(r->ds, r->si));
        if (sub == 3)
            al(r, MAP_SIZE);
        if (sub > 3)
            code = 0x8F;
        break;
    case 0x4F: {                                /* part of the map: the segments' at DS:SI to ES:DI; set; size */
        unsigned long s = lin(r->ds, r->si), t = lin(r->es, r->di);
        if (sub == 0) {
            n = rd16(e, s);
            for (k = 0; k < n; k++)
                if (phys_of(e, rd16(e, s + 2 + 2u * k)) < 0) {
                    code = 0x8B;
                    break;
                }
            if (code)
                break;
            wr16(e, t, n);
            for (k = 0; k < n; k++) {
                unsigned seg = rd16(e, s + 2 + 2u * k);
                p = phys_of(e, seg);
                wr16(e, t + 2 + 6u * k, seg);
                wr16(e, t + 4 + 6u * k, e->map_h[p]);
                wr16(e, t + 6 + 6u * k, e->map_l[p]);
            }
        } else if (sub == 1) {
            n = rd16(e, s);
            for (k = 0; k < n; k++) {
                unsigned seg = rd16(e, s + 2 + 6u * k), sh = rd16(e, s + 4 + 6u * k), sl = rd16(e, s + 6 + 6u * k);
                if (phys_of(e, seg) < 0 || (sh != NONE && (!valid(e, sh) || sl >= e->h[sh].n))) {
                    code = 0xA3;
                    break;
                }
            }
            for (k = 0; !code && k < n; k++)
                map(e, phys_of(e, rd16(e, s + 2 + 6u * k)), rd16(e, s + 4 + 6u * k), rd16(e, s + 6 + 6u * k));
        } else if (sub == 2) {
            if (r->bx > EMS_PHYS)
                code = 0x8B;
            else
                al(r, 2 + 6 * r->bx);
        } else
            code = 0x8F;
        break;
    }
    case 0x50: {                                /* several pages shown at once: by physical page, or by segment */
        unsigned long s = lin(r->ds, r->si);
        if (sub > 1) {
            code = 0x8F;
            break;
        }
        if (!valid(e, h)) {
            code = 0x83;
            break;
        }
        for (k = 0; k < r->cx; k++) {
            unsigned lp = rd16(e, s + 4u * k), pp = rd16(e, s + 4u * k + 2);
            if ((sub ? phys_of(e, pp) : (int)pp) < 0 || (sub ? 0 : pp >= EMS_PHYS)) {
                code = 0x8B;
                break;
            }
            if (lp != NONE && lp >= e->h[h].n) {
                code = 0x8A;
                break;
            }
        }
        for (k = 0; !code && k < r->cx; k++) {
            unsigned lp = rd16(e, s + 4u * k), pp = rd16(e, s + 4u * k + 2);
            map(e, sub ? phys_of(e, pp) : (int)pp, lp == NONE ? NONE : h, lp == NONE ? 0 : lp);
        }
        break;
    }
    case 0x51:                                  /* a handle's pages to BX */
        if (!valid(e, h))
            code = 0x83;
        else if ((code = resize(e, h, r->bx)) == 0)
            r->bx = e->h[h].n;
        break;
    case 0x52:                                  /* attributes: volatile only */
        if (!valid(e, h) && sub != 2)
            code = 0x83;
        else if (sub == 0 || sub == 2)
            al(r, 0);
        else if (sub == 1)
            code = (r->bx & 0xFF) ? 0x91 : 0;
        else
            code = 0x8F;
        break;
    case 0x53:                                  /* a handle's name: to ES:DI, from DS:SI */
        if (!valid(e, h))
            code = 0x83;
        else if (sub == 0) {
            for (k = 0; k < 8; k++)
                if (in_mem(e, lin(r->es, r->di) + k, 1))
                    e->mem[lin(r->es, r->di) + k] = (unsigned char)e->h[h].name[k];
        } else if (sub == 1) {
            char nm[8];
            int any = 0;
            for (k = 0; k < 8; k++)
                nm[k] = (char)(in_mem(e, lin(r->ds, r->si) + k, 1) ? e->mem[lin(r->ds, r->si) + k] : 0), any |= nm[k];
            for (k = 0; any && k < EMS_HANDLES; k++)
                if (k != h && e->h[k].used && !memcmp(e->h[k].name, nm, 8))
                    code = 0xA1;
            if (!code)
                memcpy(e->h[h].name, nm, 8);
        } else
            code = 0x8F;
        break;
    case 0x54:                                  /* the handles' directory; a name searched for; how many there are */
        if (sub == 0) {
            for (k = 0, n = 0; k < EMS_HANDLES; k++)
                if (e->h[k].used) {
                    unsigned long at = lin(r->es, r->di) + 10u * n, j;
                    wr16(e, at, k);
                    for (j = 0; j < 8; j++)
                        if (in_mem(e, at + 2 + j, 1))
                            e->mem[at + 2 + j] = (unsigned char)e->h[k].name[j];
                    n++;
                }
            al(r, n);
        } else if (sub == 1) {
            char nm[8];
            for (k = 0; k < 8; k++)
                nm[k] = (char)(in_mem(e, lin(r->ds, r->si) + k, 1) ? e->mem[lin(r->ds, r->si) + k] : 0);
            code = 0xA0;
            for (k = 0; k < EMS_HANDLES; k++)
                if (e->h[k].used && !memcmp(e->h[k].name, nm, 8) && (nm[0] || k == 0)) {
                    r->dx = k, code = 0;
                    break;
                }
        } else if (sub == 2) {
            r->bx = EMS_HANDLES;
        } else
            code = 0x8F;
        break;
    case 0x57:                                  /* a region moved (00h) or exchanged (01h) */
        if (sub > 1)
            code = 0x8F;
        else if (!in_mem(e, lin(r->ds, r->si), 18))
            code = 0x80;
        else
            code = move(e, lin(r->ds, r->si), (int)sub);
        break;
    case 0x58:                                  /* the mappable pages: their segments and numbers */
        if (sub == 0)
            for (p = 0; p < EMS_PHYS; p++)
                wr16(e, lin(r->es, r->di) + 4u * p, e->frame_seg + 0x400u * p), wr16(e, lin(r->es, r->di) + 4u * p + 2, p);
        if (sub > 1)
            code = 0x8F;
        else
            r->cx = EMS_PHYS;
        break;
    case 0x59:                                  /* the hardware: a raw page is a standard one here */
        if (sub == 0) {
            unsigned long at = lin(r->es, r->di);
            wr16(e, at, 1024), wr16(e, at + 2, 0), wr16(e, at + 4, MAP_SIZE), wr16(e, at + 6, 0), wr16(e, at + 8, 0);
        } else if (sub == 1)
            r->bx = e->free, r->dx = e->total;
        else
            code = 0x8F;
        break;
    case 0x5A:                                  /* standard or raw pages to a new handle; none is allowed */
        if (sub > 1)
            code = 0x8F;
        else if ((code = new_handle(e, r->bx, &k)) == 0)
            r->dx = k;
        break;
    case 0x5B:                                  /* alternate map register sets: none */
        if (sub == 0)
            r->bx &= 0xFF00, r->es = 0, r->di = 0;
        else if (sub == 2)
            r->dx = MAP_SIZE;
        else if (sub == 3)
            r->bx &= 0xFF00;                    /* (BL 0: none to give) */
        else if (sub == 4)
            code = (r->bx & 0xFF) ? 0x9C : 0;
        else
            code = sub <= 8 ? 0x9C : 0x8F;
        break;
    case 0x5C:                                  /* ready for a warm boot */
        break;
    default:
        code = 0x84;
    }
    ah(r, code);
}
