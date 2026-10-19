/* The VNC server (kernel/include/vnc.h; M10 step F, PRD §9.8). RFB 3.8
 * (3.3 and 3.7 taken too) with no password: it is reached only through
 * SSH's direct-tcpip to 127.0.0.1:5900 (the agent's key is the lock), and
 * only when GLOS.CFG's [vnc] enable is on.
 *
 * Updates come from the composer's damage: after each wm_compose() the
 * desktop's thread hands each viewer what it drew (so that an update read
 * while that was being drawn is made again), and the viewer's next update
 * sends that part of the screen, read from the framebuffer after the
 * drawing engine has finished (display_sync). An
 * update is made whole into a buffer, then given to the channel as it has
 * room. Encodings: Hextile (16 by 16 tiles: one colour, two, coloured
 * rectangles, or raw, whichever is smaller), Raw, and CopyRect for a window
 * moved in front of everything, when nothing was owed of where it was and
 * the two places don't overlap (the move's outline lies on the new place's
 * border, and the viewer has been sent it). The new place's damage of the
 * move's own compose is left to the copy: an app's drawing in that window
 * in that very compose waits for its next. Any
 * true-colour format the viewer asks for; an 8-bit desktop's palette is
 * turned into colours, as the DAC holds them (6 bits a colour on most VBE
 * cards). The card's cursor is drawn over the picture where it is, as glos
 * shot draws it, so that the two give the same pixels, and its moves are
 * owed as damage. Keys (X keysyms) go in as glos key's do (set 1
 * codes, a US layout, Shift added for a character that needs it); the
 * pointer goes to the desktop as an absolute position. While a session has
 * the screen nothing is sent; the desktop's return repaints everything. */
#include <string.h>

#include "desk.h"
#include "display.h"
#include "gfx.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mgadisp.h"
#include "mm.h"
#include "ssh.h"
#include "sshd.h"
#include "timer.h"
#include "vm.h"
#include "vnc.h"

#define VIEWERS 4
#define IN_MAX 1024
#define ROOM 65536                              /* channel output kept waiting at most, as glos shot's */

enum { V_VERSION, V_SECTYPE, V_INIT, V_NORMAL };

static struct viewer {
    struct ssh_chan *ch;
    int state, minor;
    u8 in[IN_MAX];
    u32 in_n;
    u32 skip;                                   /* ClientCutText still to be passed over */
    /* the client's pixel format */
    u32 bpp, big;
    u32 rtab[256], gtab[256], btab[256];
    int hextile, copyrect;
    /* what it is owed */
    struct gfx_region owed;
    int asked, whole;                           /* an update asked for (incremental or not) */
    int copy, copy_fresh;                       /* a CopyRect to send first (fresh: the move's compose not yet seen): */
    struct gfx_rect copy_dst;
    s32 copy_sx, copy_sy;
    s32 w, h;                                   /* the screen it was told of */
    /* the update being sent */
    u8 *out;
    u32 out_n, out_at;
    u32 updates;
    u8 shift;                                   /* its Shift keys held */
    int cur_on;                                 /* the card's cursor as last sent */
    s32 cur_x, cur_y;
} vw[VIEWERS];

/* The screen as a shot sees it: the card's cursor (64 by 64, AND and XOR) and the DAC's width at 8 bits. */
static struct {
    int on, dac6;
    s32 x, y;
    const u8 *and_mask, *xor_mask;
} look;

static u32 nview;
static u16 keys[128];                           /* keys waiting for the keyboard to have room (100h E0, 8000h up): all
                                                   viewers', in order, kept when a viewer goes (vncdo sends and leaves) */
static u32 kh, kt;

static void keys_out(void);

static struct viewer *of(const struct ssh_chan *ch)
{
    int i;
    for (i = 0; i < VIEWERS; i++)
        if (vw[i].ch == ch)
            return &vw[i];
    return 0;
}

int vnc_owns(const struct ssh_chan *ch) { return of(ch) != 0; }
u32 vnc_viewers(void) { return nview; }

static u16 be16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 be32v(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static void put16(u8 *p, u32 v) { p[0] = (u8)(v >> 8), p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24), p[1] = (u8)(v >> 16), p[2] = (u8)(v >> 8), p[3] = (u8)v; }

static void send(struct viewer *v, const void *d, u32 n) { ssh_chan_write(v->ch, 0, d, n); }

/* The client's pixel format: its tables. */
static void set_format(struct viewer *v, const u8 *pf)
{
    u32 rmax = be16(pf + 4), gmax = be16(pf + 6), bmax = be16(pf + 8), i;
    v->bpp = pf[0];
    v->big = pf[2];
    if (!pf[3] || (v->bpp != 8 && v->bpp != 16 && v->bpp != 32)) {     /* (a palette, or odd sizes: 32 bits) */
        v->bpp = 32, v->big = 0, rmax = gmax = bmax = 255;
        pf = (const u8 *)"\x20\x18\x00\x01\x00\xff\x00\xff\x00\xff\x10\x08\x00";
    }
    for (i = 0; i < 256; i++) {
        v->rtab[i] = (i * rmax + 127) / 255 << pf[10];
        v->gtab[i] = (i * gmax + 127) / 255 << pf[11];
        v->btab[i] = (i * bmax + 127) / 255 << pf[12];
    }
}

static void lead(struct viewer *v)
{
    static const char ver[] = "RFB 003.008\n";
    send(v, ver, 12);
    v->state = V_VERSION;
}

int vnc_open(struct ssh_chan *ch)
{
    char on[8];
    int i;
    if (kcfg_get("vnc", "enable", on, sizeof on) < 0 || strcmp(on, "on")) {
        kprintf("GLOS-VNC refused why=off\n");
        return -1;
    }
    for (i = 0; i < VIEWERS && vw[i].ch; i++) ;
    if (i == VIEWERS) {
        kprintf("GLOS-VNC refused why=busy\n");
        return -1;
    }
    memset(&vw[i], 0, sizeof vw[i]);
    vw[i].ch = ch;
    set_format(&vw[i], (const u8 *)"\x20\x18\x00\x01\x00\xff\x00\xff\x00\xff\x10\x08\x00");
    lead(&vw[i]);
    if (!nview++)
        desk_note("VNC");                       /* (the task bar's mark: a viewer, PRD D35) */
    kprintf("GLOS-VNC open viewers=%u\n", nview);
    return 0;
}

void vnc_gone(struct ssh_chan *ch)
{
    struct viewer *v = of(ch);
    if (!v)
        return;
    if (v->out)
        kfree(v->out);
    kprintf("GLOS-VNC close updates=%u\n", v->updates);
    v->ch = 0;
    v->out = 0;
    if (!--nview)
        desk_note("");
}

void vnc_keys_turn(void)
{
    if (kh == kt)
        return;
    keys_out();
    if (kh != kt)
        sshd_turn_at(timer_ticks() + 16);
}

/* ---- the screen's pixels */

static u32 rgb_at(const struct gfx_surface *fb, s32 x, s32 y)
{
    const u8 *p = fb->pix + (u32)y * fb->pitch;
    const struct gfx_fmt *f = &fb->fmt;
    u32 px, r, g, b;
    if (f->bpp == 8) {
        const u8 *c = gfx_palette[p[x]];
        if (look.dac6) {                        /* (as glos shot reads it back: 6 bits widened) */
            u32 r6 = c[0] >> 2, g6 = c[1] >> 2, b6 = c[2] >> 2;
            return (r6 << 2 | r6 >> 4) << 16 | (g6 << 2 | g6 >> 4) << 8 | (b6 << 2 | b6 >> 4);
        }
        return (u32)c[0] << 16 | (u32)c[1] << 8 | c[2];
    }
    px = f->bpp == 16 ? ((const u16 *)p)[x] : ((const u32 *)p)[x];
    r = (px >> f->rpos) & ((1u << f->rsize) - 1);
    g = (px >> f->gpos) & ((1u << f->gsize) - 1);
    b = (px >> f->bpos) & ((1u << f->bsize) - 1);
    r = f->rsize >= 8 ? r : r << (8 - f->rsize) | r >> (2 * f->rsize - 8 > 0 ? 2 * f->rsize - 8 : 0);
    g = f->gsize >= 8 ? g : g << (8 - f->gsize) | g >> (2 * f->gsize - 8 > 0 ? 2 * f->gsize - 8 : 0);
    b = f->bsize >= 8 ? b : b << (8 - f->bsize) | b >> (2 * f->bsize - 8 > 0 ? 2 * f->bsize - 8 : 0);
    return (r & 255) << 16 | (g & 255) << 8 | (b & 255);
}

/* rgb_at() with the card's cursor over it, as glos shot draws it (kernel/dos/shot.c, cursor_over). */
static u32 seen_at(const struct gfx_surface *fb, s32 x, s32 y)
{
    u32 rgb = rgb_at(fb, x, y);
    s32 ix = x - look.x, iy = y - look.y;
    u8 bit;
    int a, o;
    if (!look.on || ix < 0 || ix >= 64 || iy < 0 || iy >= 64)
        return rgb;
    bit = (u8)(0x80 >> (ix & 7));
    a = (look.and_mask[iy * 8 + ix / 8] & bit) != 0, o = (look.xor_mask[iy * 8 + ix / 8] & bit) != 0;
    if (a && !o)
        return rgb;
    if (fb->fmt.bpp == 8)
        return a ? rgb : o ? 0xFFFFFF : 0;      /* (inverting a palette's colours: not drawn, as a shot has it) */
    return a ? ~rgb & 0xFFFFFF : o ? 0xFFFFFF : 0;
}

static u32 client_px(const struct viewer *v, u32 rgb)
{
    return v->rtab[rgb >> 16] | v->gtab[(rgb >> 8) & 255] | v->btab[rgb & 255];
}

static u8 *put_px(const struct viewer *v, u8 *o, u32 px)
{
    if (v->bpp == 8) {
        *o++ = (u8)px;
    } else if (v->bpp == 16) {
        if (v->big)
            *o++ = (u8)(px >> 8), *o++ = (u8)px;
        else
            *o++ = (u8)px, *o++ = (u8)(px >> 8);
    } else if (v->big) {
        *o++ = (u8)(px >> 24), *o++ = (u8)(px >> 16), *o++ = (u8)(px >> 8), *o++ = (u8)px;
    } else {
        *o++ = (u8)px, *o++ = (u8)(px >> 8), *o++ = (u8)(px >> 16), *o++ = (u8)(px >> 24);
    }
    return o;
}

static u8 *rect_head(u8 *o, const struct gfx_rect *r, s32 enc)
{
    put16(o, (u32)r->x), put16(o + 2, (u32)r->y), put16(o + 4, (u32)r->w), put16(o + 6, (u32)r->h);
    put32(o + 8, (u32)enc);
    return o + 12;
}

static u8 *raw(const struct viewer *v, const struct gfx_surface *fb, const struct gfx_rect *r, u8 *o)
{
    s32 x, y;
    for (y = r->y; y < r->y + r->h; y++)
        for (x = r->x; x < r->x + r->w; x++)
            o = put_px(v, o, client_px(v, seen_at(fb, x, y)));
    return o;
}

/* One tile of Hextile (RFB 7.7.4): the smallest of one colour, two (subrectangles of the second), coloured
   subrectangles, or the pixels raw. *bg_ok: the last background still holds for the next tile. */
static u8 *tile(const struct viewer *v, const struct gfx_surface *fb, s32 tx, s32 ty, s32 tw, s32 th, u8 *o,
                int *bg_ok, u32 *bg_last)
{
    u32 t[256], bg, fg = 0, n = 0, i, ncol = 1, bpp = v->bpp / 8, budget;
    u8 done[256], *start = o, *sub, *cnt;
    s32 x, y;
    for (y = 0; y < th; y++)
        for (x = 0; x < tw; x++)
            t[y * 16 + x] = client_px(v, seen_at(fb, tx + x, ty + y));
    bg = t[0];
    {                                           /* (the background: the commoner of the first two colours seen) */
        u32 other = bg, cb = 0, co = 0;
        for (y = 0; y < th; y++)
            for (x = 0; x < tw; x++) {
                u32 p = t[y * 16 + x];
                if (p == bg) {
                    cb++;
                } else {
                    if (other == bg)
                        other = p, ncol = 2;
                    else if (p != other)
                        ncol = 3;
                    if (p == other)
                        co++;
                }
            }
        if (co > cb)
            bg = other, fg = t[0];
        else
            fg = other;
    }
    if (ncol == 1) {
        if (*bg_ok && bg == *bg_last) {
            *o++ = 0;
        } else {
            *o++ = 2;
            o = put_px(v, o, bg);
        }
        *bg_ok = 1, *bg_last = bg;
        return o;
    }
    budget = (u32)(tw * th) * bpp;              /* (more than raw: raw) */
    *o++ = (u8)(8 | 2 | (ncol == 2 ? 4 : 16));
    o = put_px(v, o, bg);
    if (ncol == 2)
        o = put_px(v, o, fg);
    cnt = o++;
    sub = o;
    memset(done, 0, sizeof done);
    for (y = 0; y < th; y++)
        for (x = 0; x < tw; x++) {
            u32 c = t[y * 16 + x];
            s32 w, h, xx, yy;
            int ok;
            if (c == bg || done[y * 16 + x])
                continue;
            for (w = 1; x + w < tw && t[y * 16 + x + w] == c && !done[y * 16 + x + w]; w++) ;
            for (h = 1, ok = 1; y + h < th && ok; ) {
                for (xx = x; xx < x + w; xx++)
                    if (t[(y + h) * 16 + xx] != c || done[(y + h) * 16 + xx]) {
                        ok = 0;
                        break;
                    }
                if (ok)
                    h++;
            }
            for (yy = y; yy < y + h; yy++)
                for (xx = x; xx < x + w; xx++)
                    done[yy * 16 + xx] = 1;
            if (ncol != 2)
                sub = put_px(v, sub, c);
            *sub++ = (u8)(x << 4 | y);
            *sub++ = (u8)((w - 1) << 4 | (h - 1));
            n++;
            if (n > 255 || (u32)(sub - start) > budget + 1)
                goto raw_tile;
        }
    *cnt = (u8)n;
    *bg_ok = 1, *bg_last = bg;
    return sub;
raw_tile:
    o = start;
    *o++ = 1;
    for (i = 0; i < (u32)(tw * th); i++)
        o = put_px(v, o, t[(i / (u32)tw) * 16 + i % (u32)tw]);
    *bg_ok = 0;                                 /* (after a raw tile the next must give its background) */
    return o;
}

static u8 *hextile(const struct viewer *v, const struct gfx_surface *fb, const struct gfx_rect *r, u8 *o)
{
    s32 tx, ty;
    int bg_ok = 0;
    u32 bg_last = 0;
    for (ty = r->y; ty < r->y + r->h; ty += 16)
        for (tx = r->x; tx < r->x + r->w; tx += 16)
            o = tile(v, fb, tx, ty, r->x + r->w - tx < 16 ? r->x + r->w - tx : 16, r->y + r->h - ty < 16
                     ? r->y + r->h - ty : 16, o, &bg_ok, &bg_last);
    return o;
}

/* The next update, made whole into v->out, if one is asked for and owed. */
static void make_update(struct viewer *v)
{
    struct gfx_surface *fb = display_fb();
    struct gfx_region todo;
    struct gfx_rect all;
    u32 f, i, size, n;
    u8 *o;
    int copy;
    if (!fb || !v->asked || v->out)
        return;
    all.x = all.y = 0, all.w = fb->w, all.h = fb->h;
    f = irq_save();
    if (v->w != fb->w || v->h != fb->h)
        v->whole = 1;                           /* (another mode: all of it; the client's size stays its own) */
    if (v->whole)
        gfx_region_set(&v->owed, &all);
    todo = v->owed;
    copy = v->copy && !v->whole;
    gfx_region_set(&v->owed, 0);
    v->whole = v->copy = 0;
    irq_restore(f);
    look.dac6 = fb->fmt.bpp == 8 && display_dac_bits() == 6;
    look.on = mga_cursor_state(&look.x, &look.y, &look.and_mask, &look.xor_mask);
    if (look.on != v->cur_on || (look.on && (look.x != v->cur_x || look.y != v->cur_y))) {
        struct gfx_rect r;                      /* (the cursor moved: where it was and where it is) */
        if (v->cur_on) {
            r.x = v->cur_x, r.y = v->cur_y, r.w = r.h = 64;
            gfx_region_add(&todo, &r);
        }
        if (look.on) {
            r.x = look.x, r.y = look.y, r.w = r.h = 64;
            gfx_region_add(&todo, &r);
        }
        v->cur_on = look.on, v->cur_x = look.x, v->cur_y = look.y;
    }
    gfx_region_cut(&todo, &all);
    if (!todo.n && !copy)
        return;                                 /* (asked, nothing owed: when there is) */
    if (copy && look.on && look.x < v->copy_sx + v->copy_dst.w && v->copy_sx < look.x + 64
        && look.y < v->copy_sy + v->copy_dst.h && v->copy_sy < look.y + 64)
        copy = 0, gfx_region_add(&todo, &v->copy_dst);  /* (the cursor over the copy's source: drawn instead) */
    display_sync();                             /* (what a drawing engine had queued is on the screen) */
    size = 4 + (copy ? 16 : 0);
    for (i = 0; i < todo.n; i++)
        size += 12 + (u32)(todo.r[i].w * todo.r[i].h) * (v->bpp / 8) + (u32)((todo.r[i].w + 15) / 16
                * ((todo.r[i].h + 15) / 16)) * 24;     /* (a tile may go a little past raw before it falls back) */
    if (!(v->out = kmalloc(size))) {
        f = irq_save();
        gfx_region_add(&v->owed, &all);         /* (no memory now: again later) */
        irq_restore(f);
        return;
    }
    o = v->out;
    n = todo.n + (copy ? 1 : 0);
    o[0] = 0, o[1] = 0;
    put16(o + 2, n);
    o += 4;
    if (copy) {                                 /* (first: its source is the viewer's screen as it was) */
        o = rect_head(o, &v->copy_dst, 1);
        put16(o, (u32)v->copy_sx), put16(o + 2, (u32)v->copy_sy);
        o += 4;
    }
    for (i = 0; i < todo.n; i++) {
        o = rect_head(o, &todo.r[i], v->hextile ? 5 : 0);
        o = v->hextile ? hextile(v, fb, &todo.r[i], o) : raw(v, fb, &todo.r[i], o);
    }
    v->out_n = (u32)(o - v->out);
    v->out_at = 0;
    v->asked = 0;
    v->updates++;
}

int vnc_turn(struct ssh_chan *ch, void *arg)
{
    struct viewer *v = of(ch);
    (void)arg;
    if (!v)
        return 1;
    if (v->state == V_NORMAL)
        make_update(v);
    while (v->out && ssh_chan_pending(ch) < ROOM) {
        u32 k = v->out_n - v->out_at;
        if (k > 16384)
            k = 16384;
        send(v, v->out + v->out_at, k);
        v->out_at += k;
        if (v->out_at == v->out_n) {
            kfree(v->out);
            v->out = 0;
        }
    }
    return 0;
}

void vnc_damage(const struct gfx_region *dirty, const struct gfx_surface *fb)
{
    u32 i, j, f;
    if (!nview)
        return;
    f = irq_save();
    for (i = 0; i < VIEWERS; i++)
        if (vw[i].ch && vw[i].state == V_NORMAL) {
            for (j = 0; j < dirty->n; j++) {
                struct gfx_rect r = dirty->r[j];
                if (vw[i].copy_fresh && r.x >= vw[i].copy_dst.x && r.y >= vw[i].copy_dst.y
                    && r.x + r.w <= vw[i].copy_dst.x + vw[i].copy_dst.w && r.y + r.h <= vw[i].copy_dst.y + vw[i].copy_dst.h)
                    continue;                   /* (inside the moved window's new place: the CopyRect has it) */
                gfx_region_add(&vw[i].owed, &r);
            }
            if (dirty->rough && fb) {
                struct gfx_rect all = { 0, 0, fb->w, fb->h };
                gfx_region_add(&vw[i].owed, &all);
                vw[i].copy = 0;                 /* (everything again: no copy) */
            }
            vw[i].copy_fresh = 0;               /* (only the move's own compose: what changes there later is sent) */
        }
    irq_restore(f);
    sshd_wake();
}

void vnc_moved(s32 x, s32 y, s32 w, s32 h, s32 dx, s32 dy)
{
    u32 i, f;
    struct gfx_surface *fb = display_fb();
    if (!nview || !fb || x < 0 || y < 0 || x + w > fb->w || y + h > fb->h || x + dx < 0 || y + dy < 0
        || x + dx + w > fb->w || y + dy + h > fb->h)
        return;                                 /* (off the screen at either end: drawn instead) */
    f = irq_save();
    for (i = 0; i < VIEWERS; i++) {
        struct viewer *v = &vw[i];
        struct gfx_region src;
        struct gfx_rect r = { x, y, w, h };
        if (!v->ch || !v->copyrect || v->copy || v->state != V_NORMAL)
            continue;
        src = v->owed;                          /* (owed anything where it was: the viewer's copy is stale there) */
        gfx_region_cut(&src, &r);
        if (src.n)
            continue;
        v->copy = v->copy_fresh = 1;
        v->copy_dst.x = x + dx, v->copy_dst.y = y + dy, v->copy_dst.w = w, v->copy_dst.h = h;
        v->copy_sx = x, v->copy_sy = y;
    }
    irq_restore(f);
}

/* ---- keys */

/* A keysym as a set 1 code (US layout; 100h: E0), and whether Shift makes it: the code, or 0. */
static u32 key_code(u32 sym, int *shifted)
{
    static const char row1[] = "1234567890-=", row1s[] = "!@#$%^&*()_+", qrow[] = "qwertyuiop[]", qrows[] = "QWERTYUIOP{}";
    static const char arow[] = "asdfghjkl;'`", arows[] = "ASDFGHJKL:\"~", zrow[] = "zxcvbnm,./", zrows[] = "ZXCVBNM<>?";
    static const struct { u32 sym, code; } special[] = {
        { 0xFF08, 0x0E }, { 0xFF09, 0x0F }, { 0xFF0D, 0x1C }, { 0xFF1B, 0x01 }, { 0xFFFF, 0x153 }, { 0xFF50, 0x147 },
        { 0xFF51, 0x14B }, { 0xFF52, 0x148 }, { 0xFF53, 0x14D }, { 0xFF54, 0x150 }, { 0xFF55, 0x149 },
        { 0xFF56, 0x151 }, { 0xFF57, 0x14F }, { 0xFF63, 0x152 }, { 0xFFE1, 0x2A }, { 0xFFE2, 0x36 },
        { 0xFFE3, 0x1D }, { 0xFFE4, 0x11D }, { 0xFFE9, 0x38 }, { 0xFFEA, 0x138 }, { 0xFFE7, 0x38 }, { 0xFFE8, 0x138 },
        { 0xFFEB, 0x15B }, { 0xFFEC, 0x15C }, { 0xFFE5, 0x3A }, { 0xFF8D, 0x11C }, { 0xFF67, 0x15D },
        { 0xFFBE, 0x3B }, { 0xFFBF, 0x3C }, { 0xFFC0, 0x3D }, { 0xFFC1, 0x3E }, { 0xFFC2, 0x3F }, { 0xFFC3, 0x40 },
        { 0xFFC4, 0x41 }, { 0xFFC5, 0x42 }, { 0xFFC6, 0x43 }, { 0xFFC7, 0x44 }, { 0xFFC8, 0x57 }, { 0xFFC9, 0x58 },
        { 0x20, 0x39 }, { 0x5C, 0x2B }, { 0x7C, 0x2B } };
    const char *p;
    u32 i;
    *shifted = 0;
    for (i = 0; i < ARRAY_SIZE(special); i++)
        if (special[i].sym == sym) {
            *shifted = sym == 0x7C;
            return special[i].code;
        }
    if (sym >= 0x80 || !sym)
        return 0;
    if ((p = strchr(row1, (int)sym)) != 0) return 0x02 + (u32)(p - row1);
    if ((p = strchr(row1s, (int)sym)) != 0) return *shifted = 1, 0x02 + (u32)(p - row1s);
    if ((p = strchr(qrow, (int)sym)) != 0) return 0x10 + (u32)(p - qrow);
    if ((p = strchr(qrows, (int)sym)) != 0) return *shifted = 1, 0x10 + (u32)(p - qrows);
    if ((p = strchr(arow, (int)sym)) != 0) return (p - arow == 11 ? 0x29 : 0x1E + (u32)(p - arow));
    if ((p = strchr(arows, (int)sym)) != 0) return *shifted = 1, (p - arows == 11 ? 0x29 : 0x1E + (u32)(p - arows));
    if ((p = strchr(zrow, (int)sym)) != 0) return 0x2C + (u32)(p - zrow);
    if ((p = strchr(zrows, (int)sym)) != 0) return *shifted = 1, 0x2C + (u32)(p - zrows);
    return 0;
}

/* Keys as the keyboard has room for them (glos key waits the same way: a slow app lost keys). */
static void keys_out(void)
{
    while (kh != kt && !vkbc_key_busy()) {
        u16 k = keys[kh % 128];
        if (vkbc_key(k & 0x1FF, !(k & 0x8000)) != 0)
            break;
        kh++;
    }
    if (kh != kt)
        sshd_turn_at(timer_ticks() + 16);       /* (the keyboard full: again in a moment) */
}

static void key_in(u32 code, int down)
{
    if (kt - kh < 128)
        keys[kt++ % 128] = (u16)(code | (down ? 0 : 0x8000));
}

static void key(struct viewer *v, u32 sym, int down)
{
    int shifted;
    u32 code = key_code(sym, &shifted);
    if (!code)
        return;
    if (code == 0x2A || code == 0x36) {
        u8 bit = code == 0x2A ? 1 : 2;
        v->shift = (u8)(down ? v->shift | bit : v->shift & ~bit);
    }
    if (down && shifted && !v->shift)
        key_in(0x2A, 1);                        /* (a character Shift makes, and the viewer holds none: one held) */
    key_in(code, down);
    if (down && shifted && !v->shift)
        key_in(0x2A, 0);
    keys_out();
}

/* ---- input from the viewer */

static void eat(struct viewer *v, u32 n)
{
    memmove(v->in, v->in + n, v->in_n - n);
    v->in_n -= n;
}

/* One message, if it has all come: 1 (taken), 0 (more to come), -1 (wrong). */
static int message(struct viewer *v)
{
    const u8 *m = v->in;
    u32 n = v->in_n, i;
    switch (v->state) {
    case V_VERSION:
        if (n < 12)
            return 0;
        if (memcmp(m, "RFB 003.", 8))
            return -1;
        v->minor = (m[8] - '0') * 100 + (m[9] - '0') * 10 + (m[10] - '0');
        eat(v, 12);
        if (v->minor < 7) {                     /* 3.3: the server says the type */
            u8 t[4] = { 0, 0, 0, 1 };
            send(v, t, 4);
            v->state = V_INIT;
        } else {
            u8 t[2] = { 1, 1 };                 /* one type: None */
            send(v, t, 2);
            v->state = V_SECTYPE;
        }
        return 1;
    case V_SECTYPE:
        if (n < 1)
            return 0;
        if (m[0] != 1)
            return -1;
        eat(v, 1);
        if (v->minor >= 8) {
            u8 ok[4] = { 0, 0, 0, 0 };
            send(v, ok, 4);
        }
        v->state = V_INIT;
        return 1;
    case V_INIT: {                              /* ClientInit: ServerInit */
        struct gfx_surface *fb = display_fb();
        u8 si[24 + 16];
        static const char name[] = "GLOS desktop";
        if (n < 1)
            return 0;
        eat(v, 1);
        v->w = fb ? fb->w : 640;
        v->h = fb ? fb->h : 480;
        put16(si, (u32)v->w), put16(si + 2, (u32)v->h);
        memcpy(si + 4, "\x20\x18\x00\x01\x00\xff\x00\xff\x00\xff\x10\x08\x00\x00\x00\x00", 16);
        put32(si + 20, sizeof name - 1);
        memcpy(si + 24, name, sizeof name - 1);
        send(v, si, 24 + sizeof name - 1);
        v->state = V_NORMAL;
        v->whole = 1;
        kprintf("GLOS-VNC init %dx%d rfb=3.%u\n", (int)v->w, (int)v->h, (u32)v->minor);
        return 1;
    }
    }
    if (!n)
        return 0;
    switch (m[0]) {
    case 0:                                     /* SetPixelFormat */
        if (n < 20)
            return 0;
        set_format(v, m + 4);
        v->whole = 1;
        eat(v, 20);
        return 1;
    case 2: {                                   /* SetEncodings */
        u32 k;
        if (n < 4)
            return 0;
        k = be16(m + 2);
        if (n < 4 + 4 * k)
            return 0;
        v->hextile = v->copyrect = 0;
        for (i = 0; i < k; i++) {
            s32 e = (s32)be32v(m + 4 + 4 * i);
            v->hextile |= e == 5;
            v->copyrect |= e == 1;
        }
        eat(v, 4 + 4 * k);
        return 1;
    }
    case 3:                                     /* FramebufferUpdateRequest */
        if (n < 10)
            return 0;
        v->asked = 1;
        if (!m[1]) {
            struct gfx_rect r = { be16(m + 2), be16(m + 4), be16(m + 6), be16(m + 8) };
            u32 f = irq_save();
            gfx_region_add(&v->owed, &r);
            irq_restore(f);
        }
        eat(v, 10);
        return 1;
    case 4:                                     /* KeyEvent */
        if (n < 8)
            return 0;
        key(v, be32v(m + 4), m[1]);
        eat(v, 8);
        return 1;
    case 5: {                                   /* PointerEvent: RFB's buttons 1, 2, 3 are left, middle, right */
        u32 b;
        if (n < 6)
            return 0;
        b = (m[1] & 1 ? 1u : 0u) | (m[1] & 4 ? 2u : 0u) | (m[1] & 2 ? 4u : 0u);
        desk_in(DESK_POINT, be16(m + 2), be16(m + 4), (int)b);
        eat(v, 6);
        return 1;
    }
    case 6:                                     /* ClientCutText: passed over */
        if (n < 8)
            return 0;
        v->skip = be32v(m + 4);
        eat(v, 8);
        return 1;
    }
    return -1;
}

void vnc_input(struct ssh_chan *ch, const u8 *d, u32 n)
{
    struct viewer *v = of(ch);
    int r;
    if (!v)
        return;
    while (n) {
        u32 k;
        if (v->skip) {
            k = n < v->skip ? n : v->skip;
            v->skip -= k, d += k, n -= k;
            continue;
        }
        k = IN_MAX - v->in_n < n ? IN_MAX - v->in_n : n;
        memcpy(v->in + v->in_n, d, k);
        v->in_n += k, d += k, n -= k;
        while ((r = message(v)) > 0 && !v->skip) ;
        if (r < 0) {
            kprintf("GLOS-VNC bad message=%u\n", v->in_n ? v->in[0] : 0);
            ssh_chan_close(ch);
            return;
        }
        if (v->skip && v->in_n) {               /* (cut text that came with the message) */
            k = v->in_n < v->skip ? v->in_n : v->skip;
            eat(v, k);
            v->skip -= k;
        }
    }
    sshd_wake();
}
