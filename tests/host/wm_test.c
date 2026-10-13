/* Host test for the window manager's core (kernel/wm/wm.c).
 *
 * The composer against a model, with windows as rectangles of colour: after
 * each of thousands of random things done to a set of frameless windows
 * (made, shown, hidden, moved, resized, raised, given the focus, drawn in,
 * removed), the screen as wm_compose() has brought it up to date, one
 * damaged part at a time, must be the picture the model paints from
 * nothing: for every pixel, the topmost shown window that covers it, or the
 * desktop. Then the same with framed windows, the task bar and the pointer
 * against the composer's own picture of everything drawn afresh: nothing
 * that changed may have gone undamaged. Then where a point is in a window,
 * who is next for the focus, the task bar's buttons, and that the memory
 * taken was given back. Last, the demonstration desktop (kernel/wm/demo.c)
 * at each depth, written to build/host/wm-desk-N.png. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wm.h"
#include "png.h"

void wm_demo(struct wm *m);

static int fails;
#define CHECK(c) do { if (!(c)) { if (fails++ < 20) printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static u32 rnd_state = 4711;
static u32 rnd(void) { rnd_state = rnd_state * 1664525u + 1013904223u; return rnd_state >> 8; }
static s32 rin(s32 lo, s32 hi) { return lo + (s32)(rnd() % (u32)(hi - lo + 1)); }

static long held, allocs;
static void *h_alloc(u32 n, int pixels) { (void)pixels; held += n; allocs++; return calloc(1, n ? n : 1); }
static void h_free(void *p, u32 n, int pixels) { (void)pixels; held -= n; allocs--; free(p); }
static const struct wm_host host = { h_alloc, h_free };

#define SW 160
#define SH 120

static void screen_new(struct gfx_surface *s, const struct gfx_fmt *f, s32 w, s32 h)
{
    u32 pitch = (u32)w * (f->bpp / 8u);
    gfx_init(s, calloc(pitch, (size_t)h), w, h, pitch, f);
}

static int screens_same(const struct gfx_surface *a, const struct gfx_surface *b)
{
    return !memcmp(a->pix, b->pix, a->pitch * (u32)a->h);
}

/* The model's picture: each pixel from the topmost shown window over it. */
static void model(const struct wm *m, struct gfx_surface *out)
{
    struct gfx_rect c;
    s32 x, y;
    for (y = 0; y < m->h; y++)
        for (x = 0; x < m->w; x++) {
            const struct wm_window *w;
            u32 px = gfx_px(&m->fmt, m->look.desk);
            for (w = m->top; w; w = w->below) {
                if (!(w->flags & WF_SHOWN))
                    continue;
                wm_client(m, w, &c);
                if (x >= c.x && y >= c.y && x < c.x + c.w && y < c.y + c.h) {
                    px = gfx_get(&w->store, x - c.x, y - c.y);
                    break;
                }
            }
            if (out->fmt.bpp == 8) out->pix[(u32)y * out->pitch + (u32)x] = (u8)px;
            else if (out->fmt.bpp == 16) *(u16 *)(out->pix + (u32)y * out->pitch + (u32)x * 2) = (u16)px;
            else *(u32 *)(out->pix + (u32)y * out->pitch + (u32)x * 4) = px;
        }
}

static void list_check(const struct wm *m)
{
    const struct wm_window *w;
    u32 n = 0;
    int seen_top = 0;
    for (w = m->bottom; w; w = w->above, n++) {
        CHECK(!w->above || w->above->below == w);
        if (w->flags & WF_TOPMOST)
            seen_top = 1;
        else
            CHECK(!seen_top);                   /* nothing ordinary above a topmost one */
    }
    CHECK(n == m->count && (!m->focus || (m->focus->flags & WF_SHOWN)));
}

/* Random things done to up to 12 windows; framed: with frames, the bar and
   the pointer, and judged against a fresh composition instead of the model. */
static void churn(const struct gfx_fmt *f, const char *name, int framed, u32 rounds)
{
    struct gfx_surface scr, ref;
    struct wm m, *mp = &m;
    struct wm_window *win[12] = { 0 };
    u32 n, bad = 0, composed = 0;
    long before = held;

    screen_new(&scr, f, SW, SH);
    screen_new(&ref, f, SW, SH);
    wm_init(mp, &host, SW, SH, f);
    if (framed)
        wm_bar(mp, 1);
    for (n = 0; n < rounds && bad < 5; n++) {
        u32 k = rnd() % 12, op = rnd() % 12;
        struct wm_window *w = win[k];
        if (!w) {
            u32 flags = framed ? (rnd() & 1 ? WF_FRAME : 0) | (rnd() % 4 ? 0 : WF_FIXED) | (rnd() % 9 ? 0 : WF_TOPMOST) : 0;
            w = win[k] = wm_create(mp, rin(-30, SW - 10), rin(-30, SH - 10), rin(1, 90), rin(1, 70), "A window", flags, 0);
            CHECK(w != 0);
            gfx_fill(&w->store, 0, 0, w->store.w, w->store.h, rnd() & 0xFFFFFF);
            if (rnd() % 4)
                wm_show(mp, w, 1);
        } else switch (op) {
        case 0:
            wm_destroy(mp, w);
            win[k] = 0;
            break;
        case 1:
            wm_show(mp, w, rnd() & 1);
            break;
        case 2: case 3:
            wm_move(mp, w, rin(-40, SW), rin(-40, SH));
            break;
        case 4:
            CHECK(wm_resize(mp, w, rin(1, 100), rin(1, 80)) == 0);
            break;
        case 5:
            wm_raise(mp, w);
            break;
        case 6:
            wm_set_focus(mp, rnd() % 5 ? w : 0);
            break;
        case 7: case 8: {                       /* its owner draws, and says where */
            struct gfx_rect r = { rin(-5, w->store.w), rin(-5, w->store.h), rin(1, 40), rin(1, 30) };
            gfx_fill(&w->store, r.x, r.y, r.w, r.h, rnd() & 0xFFFFFF);
            wm_damage(mp, w, rnd() % 6 ? &r : 0);
            if (!(rnd() % 6))                   /* (all of it: so whatever it drew before is covered too) */
                wm_damage(mp, w, 0);
            break;
        }
        case 9:
            if (framed)
                wm_title(mp, w, rnd() & 1 ? "Another name for it" : "x");
            break;
        case 10:
            if (framed && (rnd() & 1)) {
                wm_pointer(mp, rin(-5, SW + 5), rin(-5, SH + 5), rnd() % 4 != 0);
            } else if (framed) {                /* the outline of a move or a new size */
                struct gfx_rect g = { rin(-40, SW), rin(-40, SH), rin(1, 150), rin(1, 120) };
                wm_ghost(mp, rnd() % 3 ? &g : 0);
            }
            break;
        default:
            if (framed)
                wm_bar_text(mp, rnd() & 1 ? "09:15" : "23:59", rnd() & 1 ? "ssh" : "");
        }
        list_check(mp);
        if (rnd() % 3)                          /* (not after every one: damage adds up) */
            continue;
        composed += (u32)wm_compose(mp, &scr);
        CHECK(mp->dirty.n == 0);
        if (framed) {
            struct gfx_region none = mp->dirty;
            wm_damage_all(mp);
            wm_compose(mp, &ref);
            mp->dirty = none;
        } else {
            model(mp, &ref);
        }
        if (!screens_same(&scr, &ref)) {
            s32 x, y;
            for (y = 0; y < SH; y++)
                for (x = 0; x < SW; x++)
                    if (gfx_get(&scr, x, y) != gfx_get(&ref, x, y)) {
                        printf("FAIL wm %s%s: after %u things (the last: %u on window %u) pixel %d,%d is %x, should be %x\n",
                               name, framed ? " framed" : "", n + 1, op, k, x, y, gfx_get(&scr, x, y), gfx_get(&ref, x, y));
                        y = SH;
                        break;
                    }
            memcpy(scr.pix, ref.pix, scr.pitch * (u32)scr.h);
            bad++;
            fails++;
        }
    }
    for (n = 0; n < 12; n++)
        if (win[n])
            wm_destroy(mp, win[n]);
    wm_bar(mp, 0);
    CHECK(mp->count == 0 && held == before);
    printf("wm %s%s: %u things, %u compositions\n", name, framed ? " framed" : "", rounds, composed);
    free(scr.pix), free(ref.pix);
}

static void places(void)
{
    struct wm m;
    struct wm_window *a, *b, *c, *t;
    struct gfx_rect r;
    int part;
    wm_init(&m, &host, 320, 240, &gfx_fmt8);
    wm_bar(&m, 1);
    a = wm_create(&m, 10, 10, 100, 60, "A", WF_FRAME, 0);
    b = wm_create(&m, 60, 40, 100, 60, "B", WF_FRAME | WF_FIXED, 0);
    c = wm_create(&m, 200, 10, 50, 50, "C", 0, 0);
    wm_show(&m, a, 1), wm_show(&m, b, 1), wm_show(&m, c, 1);
    wm_client(&m, a, &r);
    CHECK(r.x == 11 && r.y == 10 + 2 + 18 && r.w == 100 && r.h == 60 && a->frame.w == 102 && a->frame.h == 60 + 20 + 1);
    CHECK(wm_hit(&m, 5, 5, &part) == 0 && part == WH_NONE);
    CHECK(wm_hit(&m, 12, 32, &part) == a && part == WH_CLIENT);
    CHECK(wm_hit(&m, 50, 15, &part) == a && part == WH_TITLE);
    CHECK(wm_hit(&m, 17, 17, &part) == a && part == WH_CLOSE);
    CHECK(wm_hit(&m, 10, 50, &part) == a && part == WH_BORDER);
    CHECK(wm_hit(&m, 100, 50, &part) == b && part == WH_TITLE);         /* b is above a there */
    CHECK(wm_hit(&m, 70, 59, &part) == b && part == WH_BORDER);         /* (the line under its title) */
    CHECK(wm_hit(&m, 62, 70, &part) == b && part == WH_CLIENT);
    wm_raise(&m, a);
    CHECK(wm_hit(&m, 70, 60, &part) == a && part == WH_CLIENT);
    CHECK(wm_hit(&m, 110, 89, &part) == a && part == WH_GROW);
    CHECK(wm_hit(&m, 158, 118, &part) == b && part != WH_GROW);         /* b's size is fixed */
    CHECK(wm_hit(&m, 210, 20, &part) == c && part == WH_CLIENT);
    CHECK(wm_hit(&m, 100, 230, &part) == m.bar && part == WH_CLIENT);
    wm_set_focus(&m, a);                        /* the focus goes round in the order they were made */
    CHECK(m.focus == a && wm_next(&m, a) == b && wm_next(&m, b) == c && wm_next(&m, c) == a && wm_next(&m, 0) == a);
    wm_set_focus(&m, m.bar);
    CHECK(m.focus == a);                        /* (the bar never has it) */
    wm_show(&m, b, 0);
    CHECK(wm_next(&m, a) == c);
    wm_show(&m, b, 1);
    /* (two buttons from 62, 74 wide here; c has no frame: no button) */
    CHECK(wm_bar_hit(&m, 70, 230) == a && wm_bar_hit(&m, 70 + 74, 230) == b && wm_bar_hit(&m, 20, 230) == 0
          && wm_bar_hit(&m, 70 + 148, 230) == 0 && wm_bar_hit(&m, 70, 100) == 0);
    t = wm_create(&m, 0, 0, 20, 20, "T", WF_TOPMOST, 0);
    wm_show(&m, t, 1);
    wm_raise(&m, a);
    CHECK(wm_hit(&m, 5, 5, &part) == t && m.top == t && a->above == m.bar);  /* (a: as high as an ordinary one goes) */
    CHECK(wm_find(&m, a->id) == a && wm_find(&m, 9999) == 0);
    wm_destroy(&m, a), wm_destroy(&m, b), wm_destroy(&m, c), wm_destroy(&m, t);
    wm_bar(&m, 0);
    CHECK(m.count == 0 && m.focus == 0);
}

static u32 crc32(const u8 *p, u32 n)
{
    u32 c = ~0u, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = c >> 1 ^ (c & 1 ? 0xEDB88320u : 0);
    }
    return ~c;
}

static void desk(const struct gfx_fmt *f, const char *name, s32 w, s32 h)
{
    struct gfx_surface s;
    struct wm m;
    struct png png;
    char path[64];
    u32 row = 1 + (u32)w * 3, n;
    u8 *rgb = malloc(row * (size_t)h), *out = malloc(PNG_ROWS_MAX(PNG_BLOCK) + PNG_HEAD_MAX);
    s32 x, y, at = 0;
    FILE *fp;
    screen_new(&s, f, w, h);
    wm_init(&m, &host, w, h, f);
    wm_demo(&m);
    wm_compose(&m, &s);
    for (y = 0; y < h; y++) {
        rgb[(u32)y * row] = 0;
        for (x = 0; x < w; x++) {
            u32 c = gfx_rgb(f, gfx_get(&s, x, y));
            u8 *p = rgb + (u32)y * row + 1 + (u32)x * 3;
            p[0] = (u8)(c >> 16), p[1] = (u8)(c >> 8), p[2] = (u8)c;
        }
    }
    snprintf(path, sizeof path, "build/host/wm-desk-%s.png", name);
    if ((fp = fopen(path, "wb"))) {
        n = png_begin(&png, (u32)w, (u32)h, 24, 0, 0, out);
        fwrite(out, 1, n, fp);
        while (at < h) {
            s32 rows = (s32)(PNG_BLOCK / row) < h - at ? (s32)(PNG_BLOCK / row) : h - at;
            n = png_rows(&png, rgb + (u32)at * row, (u32)rows, out);
            fwrite(out, 1, n, fp);
            at += rows;
        }
        fclose(fp);
    }
    printf("wm desk %s %dx%d crc=%08x\n", name, w, h, crc32(rgb, row * (u32)h));
    free(rgb), free(out), free(s.pix);
}

int main(void)
{
    churn(&gfx_fmt8, "8", 0, 30000);
    churn(&gfx_fmt565, "16", 0, 30000);
    churn(&gfx_fmt888, "32", 0, 30000);
    churn(&gfx_fmt8, "8", 1, 30000);
    churn(&gfx_fmt565, "16", 1, 20000);
    churn(&gfx_fmt888, "32", 1, 20000);
    places();
    CHECK(held == 0 && allocs == 0);
    desk(&gfx_fmt8, "8", 640, 480);
    desk(&gfx_fmt565, "16", 640, 480);
    desk(&gfx_fmt888, "32", 640, 480);
    printf("wm_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
