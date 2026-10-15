/* The Matrox card's drawing engine (PRD §9.1; docs/milestones-m7.md step
 * C): MGA-Glide's HAL (third_party/mgahal, its FPU-free part, with the
 * port hal/port/glos.c), brought up on the desktop's mode.
 *
 * The mode is still set through the video BIOS, as on any card
 * (display.c). When the framebuffer it gives is a G100, G200, G400 or
 * G450's, the card's registers and all of its memory are mapped into the
 * kernel (mm_dev_map), its memory is sized once (the G200's BIOS tells VBE
 * 2 MB whatever it has), and the engine is set up on the screen as its
 * target, the screen's bytes kept in the VRAM manager. Before a session
 * takes the screen, and when the desktop ends, the engine is drained and
 * let go of: from then on the card is the session's. GLOS.CFG's [display]
 * accel = off keeps it out of use.
 *
 * The screen's surface then has a device (gfx.h's struct gfx_dev, step D):
 * fills, copies between VRAM and from memory, and 1-bit pictures go to the
 * engine, each exactly what the software would draw; the software waits
 * for the engine before touching the pixels itself. The HAL is one
 * thread's at a time (its FIFO count is a variable of its own), and from M8
 * the engine is shared with apps that have a HAL of their own: the GPU lock
 * (gpu.c) covers every use, each draw on its own. The cursor's registers and
 * the VRAM manager's table are not the engine's: they are kept with
 * interrupts off for a moment instead, so the pointer moves while an app
 * has the engine. */
#include <string.h>

#include "glos/bootinfo.h"
#include "app.h"
#include "io.h"
#include "gpu.h"
#include "kprintf.h"
#include "mgadisp.h"
#include "mm.h"
#include "sched.h"

#include "mga/hal.h"
#include "mga/mmio.h"

#define OWN_SCREEN 1u                           /* (VRAM owners) */
#define OWN_CURSOR 2u
#define OWN_STORE  3u                           /* the desktop's windows' pixels (step E) */
#define OWN_APP    0x100u                       /* + an app's id: its blocks (M8) */
#define APP_BLOCKS 64

static struct {
    u8 off;                                     /* [display] accel = off */
    u8 found, mapped, up;
    u8 desk;                                    /* the last desktop mode was on the card (up() worked) */
    u32 vram;                                   /* bytes, sized at the first desktop */
    mga_vram heap;
    mga_target screen;
    mga_target cur;                             /* the engine's target now */
    u8 *screen_pix;                             /* the screen's pixels in display.c's mapping */
    u32 screen_bytes;
    u8 busy;                                    /* drawn since the last sync */
    u32 ops;                                    /* draws the engine has had */
    u32 store_max;                              /* bytes of VRAM the desktop's windows may have (glos display vram) */
    mga_card_info info;                         /* (for apps: gpu.c) */
    /* The hardware cursor (step F): the desktop's pointer, its image loaded
       into VRAM on each desktop mode. */
    u8 cur_and[512], cur_xor[512];
    u8 cur_have, cur_loaded, cur_shown;
    s32 cur_x, cur_y;
} m;
static u32 row_buf[4096];                       /* an image's row, padded to dwords */

/* Apps' blocks (M8 step C): at the same offsets for the app's life, so they
   are kept here as well as in the VRAM manager's table, which each desktop
   mode begins afresh; while the card is another's, their contents are in
   frames of the system's memory. */
static struct ablk {
    u32 id;                                     /* the app's (0: a free entry) */
    u32 off, bytes;
    u32 *saved;                                 /* its pages' frames while the card is another's, or 0 */
} ab[APP_BLOCKS];

static void ab_drop(struct ablk *b)
{
    u32 i;
    if (b->saved) {
        for (i = 0; i < b->bytes >> 12; i++)
            if (b->saved[i])
                pmm_free(b->saved[i]);
        kfree(b->saved);
    }
    memset(b, 0, sizeof *b);
}

/* Their contents into the system's memory (the engine drained): an app whose
   blocks don't fit there is ended. */
static void apps_out(void)
{
    u32 i, p;
    for (i = 0; i < APP_BLOCKS; i++) {
        struct ablk *b = &ab[i];
        u32 n = b->bytes >> 12;
        if (!b->id || b->saved)
            continue;
        if (!(b->saved = kmalloc(n * sizeof *b->saved))) {
            app_kill_id(b->id, "vram-lost");
            continue;
        }
        memset(b->saved, 0, n * sizeof *b->saved);
        for (p = 0; p < n; p++) {
            u8 *k;
            if (!(b->saved[p] = pmm_alloc()))
                break;
            k = kmap(b->saved[p]);
            memcpy(k, (const u8 *)mga_fb + b->off + (p << 12), 4096);
            kunmap(k);
        }
        if (p < n)
            app_kill_id(b->id, "vram-lost");
        kprintf("GLOS-MGA vram-out app=%u at=%x bytes=%u%s\n", b->id, b->off, b->bytes, p < n ? " lost=1" : "");
    }
}

/* ... and back, at the same offsets, beside the new screen. */
static void apps_in(void)
{
    u32 i, p;
    for (i = 0; i < APP_BLOCKS; i++) {
        struct ablk *b = &ab[i];
        if (!b->id)
            continue;
        if (vram_reserve(&m.heap, b->off, b->bytes, OWN_APP + b->id) != 0) {
            kprintf("GLOS-MGA vram-in app=%u at=%x bytes=%u lost=1\n", b->id, b->off, b->bytes);
            app_kill_id(b->id, "vram-lost");
            ab_drop(b);
            continue;
        }
        if (!b->saved)
            continue;
        for (p = 0; p < b->bytes >> 12 && b->saved[p]; p++) {
            u8 *k = kmap(b->saved[p]);
            memcpy((u8 *)mga_fb + b->off + (p << 12), k, 4096);
            kunmap(k);
            pmm_free(b->saved[p]);
        }
        kfree(b->saved);
        b->saved = 0;
        kprintf("GLOS-MGA vram-in app=%u at=%x bytes=%u\n", b->id, b->off, b->bytes);
    }
}

static int up(u8 *pix, u32 lfb, u32 pitch, u32 bpp, u32 h, u32 bytes);

/* The engine finished (within a second: on a card, microseconds), or reset
   by the HAL, which is worth saying. */
static void drain(const char *when)
{
    if (engine_sync(1000000) != 0)
        kprintf("GLOS-WARN mga-reset when=%s resets=%u\n", when, engine_resets);
}

static void take(void) { gpu_take(); }
static void give(void) { gpu_give(); }

void mga_display_init(const struct bootinfo *bi)
{
    m.off = (bi->display_flags & BI_DISPLAY_NOACCEL) != 0;
    if (m.off || mga_find(&mga) != 0 || mga.family == MGA_FAMILY_G200E)
        return;
    m.found = 1;
    mga.fb_size = mga.family == MGA_FAMILY_G400 ? 32u << 20 : 16u << 20;   /* (the apertures: the G400's and
                                                   G450's are 32 MB, the others' 16; the HAL would assume 16) */
    kprintf("GLOS-MGA found %s id=%04x rev=%02x at %u:%u.%u fb=%08x mmio=%08x\n", mga.name, mga.device_id,
            mga.revision, mga.bus, mga.dev, mga.fn, mga.fb_phys, mga.mmio_phys);
}

int mga_display_up(u8 *pix, u32 lfb, u32 pitch, u32 bpp, u32 h)
{
    u32 bytes = pitch * h;
    int r;
    gpu_take_force();                           /* (an app's turn ends wherever it is: the mode is new) */
    r = up(pix, lfb, pitch, bpp, h, bytes);
    if (!r) {
        u32 i;
        for (i = 0; i < APP_BLOCKS; i++)        /* (apps' blocks kept for a desktop on the card: none now) */
            if (ab[i].id) {
                app_kill_id(ab[i].id, "vram-lost");
                ab_drop(&ab[i]);
            }
    }
    gpu_give();
    return r;
}

static int up(u8 *pix, u32 lfb, u32 pitch, u32 bpp, u32 h, u32 bytes)
{
    m.up = m.desk = 0;
    if (!m.found)
        return 0;
    if (lfb < mga.fb_phys || lfb - mga.fb_phys >= mga.fb_size) {
        kprintf("GLOS-MGA other lfb=%08x\n", lfb);  /* (not the card's framebuffer: VBE alone) */
        return 0;
    }
    if (!m.mapped) {
        if (mga_map(&mga) != 0) {
            kprintf("GLOS-MGA error=map\n");
            m.found = 0;
            return 0;
        }
        m.mapped = 1;
    }
    if (!m.vram)                                /* (before anything is drawn: it writes a word a megabyte) */
        mga.vram_bytes = m.vram = mga_probe_vram();
    memset(&m.screen, 0, sizeof m.screen);
    m.screen.color_off = lfb - mga.fb_phys;
    m.screen.pitch_px = (int)(pitch / (bpp / 8));
    m.screen.bpp = (int)bpp;
    m.screen.zbits = 16;
    engine_init(m.screen.pitch_px, m.screen.bpp);
    crtc_set_start(m.screen.color_off);         /* (the screen where the VRAM manager has it, whatever ran before) */
    engine_set_target(&m.screen);
    engine_set_clip(0, 0, m.screen.pitch_px, (int)h);
    if (engine_sync(1000000) != 0) {
        kprintf("GLOS-MGA error=engine resets=%u\n", engine_resets);
        return 0;
    }
    vram_init(&m.heap, 0, m.vram);
    vram_reserve(&m.heap, m.screen.color_off, bytes, OWN_SCREEN);
    apps_in();
    m.cur = m.screen;
    m.cur_loaded = m.cur_shown = 0;             /* (the image again, in this mode's VRAM) */
    m.screen_pix = pix;
    m.screen_bytes = bytes;
    m.busy = 0;
    m.info.device = mga.device_id;
    m.info.revision = mga.revision;
    m.info.vram = m.vram;
    m.info.bpp = bpp;
    m.info.fb_phys = mga.fb_phys;
    m.info.regs_phys = mga.mmio_phys;
    m.info.option = mga.option;
    m.info.subsys = mga.subsys;
    m.up = m.desk = 1;
    kprintf("GLOS-MGA up %s vram=%u screen=%x+%u pitch=%u bpp=%u\n", mga.name, m.vram, m.screen.color_off, bytes,
            m.screen.pitch_px, bpp);
    return 1;
}

void mga_display_down(void)
{
    gpu_take();                                 /* (an app with the engine is paused only at its next safe point:
                                                   app.c app_return) */
    if (m.up) {
        drain("down");
        gpu_views_out();                        /* (apps touching their blocks now wait for the next desktop) */
        apps_out();
        if (m.cur_shown)
            cursor_show(0);
        m.up = m.busy = m.cur_shown = m.cur_loaded = 0;
        kprintf("GLOS-MGA down\n");
    }
    gpu_give();
}

/* ---- the hardware cursor */

void mga_cursor_image(const u8 *and_mask, const u8 *xor_mask)
{
    u32 f = irq_save();
    memcpy(m.cur_and, and_mask, sizeof m.cur_and);
    memcpy(m.cur_xor, xor_mask, sizeof m.cur_xor);
    m.cur_have = 1;
    m.cur_loaded = 0;
    irq_restore(f);
}

/* Its image in VRAM: as high as there is room, within the first 16 MB (8 on
   a G100), which is all the cursor's address reaches. */
static int cursor_place(void)
{
    u32 reach = mga.family == MGA_FAMILY_G100 ? 8u << 20 : 16u << 20, off;
    if (vram_alloc(&m.heap, MGA_CURSOR_BYTES, 1024, MGA_VRAM_TOP, OWN_CURSOR, &off) != 0)
        return -1;
    if (off + MGA_CURSOR_BYTES > reach) {
        vram_free(&m.heap, off);
        off = reach - MGA_CURSOR_BYTES;
        if (vram_reserve(&m.heap, off, MGA_CURSOR_BYTES, OWN_CURSOR) != 0)
            return -1;
    }
    if (cursor_load(off, m.cur_and, m.cur_xor) != 0)
        return -1;
    cursor_colours(0x000000, 0xFFFFFF);
    kprintf("GLOS-MGA cursor at=%x\n", off);
    return 0;
}

int mga_cursor(int on, s32 x, s32 y)
{
    int r = 0;
    u32 f = irq_save();                         /* (the RAMDAC's registers: not the engine's) */
    if (m.up && m.cur_have && (m.cur_loaded || cursor_place() == 0)) {
        m.cur_loaded = 1;
        if (on && (!m.cur_shown || x != m.cur_x || y != m.cur_y))
            cursor_move(x, y);
        if ((on != 0) != m.cur_shown)
            cursor_show(on);
        m.cur_shown = on != 0, m.cur_x = x, m.cur_y = y;
        r = 1;
    }
    irq_restore(f);
    return r;
}

int mga_cursor_state(s32 *x, s32 *y, const u8 **and_mask, const u8 **xor_mask)
{
    if (!m.up || !m.cur_shown)
        return 0;
    *x = m.cur_x, *y = m.cur_y, *and_mask = m.cur_and, *xor_mask = m.cur_xor;
    return 1;
}

void mga_display_sync(void)
{
    take();
    if (m.up && m.busy)
        drain("sync");
    m.busy = 0;
    give();
}

int mga_display_on(void) { return m.up; }

const mga_card_info *mga_card(void) { return m.up ? &m.info : 0; }

int mga_desktop_card(void) { return m.desk; }

u32 mga_regs_phys(void) { return mga.mmio_phys; }

/* ---- apps' blocks (M8 step C) */

int mga_app_vram(u32 id, u32 min, u32 max, u32 align, u32 *off, u32 *bytes)
{
    u32 f, largest, n, i;
    int r = -1;
    min = (min + 4095) & ~4095u;
    max = max < min ? min : (max + 4095) & ~4095u;
    if (align < 4096)
        align = 4096;
    if (!min || (align & (align - 1)))
        return -1;
    f = irq_save();
    for (i = 0; i < APP_BLOCKS && ab[i].id; i++) ;
    if (m.up && i < APP_BLOCKS) {
        vram_free_bytes(&m.heap, &largest);
        for (n = max < (largest & ~4095u) ? max : largest & ~4095u; n >= min; n -= 4096)
            if (vram_alloc(&m.heap, n, align, MGA_VRAM_TOP, OWN_APP + id, off) == 0) {
                ab[i].id = id;
                ab[i].off = *off;
                ab[i].bytes = *bytes = n;
                r = 0;
                break;
            }
    }
    irq_restore(f);
    return r;
}

u32 mga_app_vram_free(u32 id, u32 off)
{
    u32 f = irq_save(), i, bytes = 0;
    for (i = 0; i < APP_BLOCKS; i++)
        if (ab[i].id == id && ab[i].off == off) {
            bytes = ab[i].bytes;
            if (m.up && !ab[i].saved)
                vram_free(&m.heap, off);
            ab_drop(&ab[i]);
            break;
        }
    irq_restore(f);
    return bytes;
}

int mga_app_block(u32 id, u32 off, u32 len, u32 *start, u32 *bytes)
{
    u32 i;
    for (i = 0; i < APP_BLOCKS; i++)
        if (ab[i].id == id && off >= ab[i].off && len <= ab[i].bytes && off - ab[i].off <= ab[i].bytes - len) {
            if (start)
                *start = ab[i].off;
            if (bytes)
                *bytes = ab[i].bytes;
            return 1;
        }
    return 0;
}

void mga_app_gone(u32 id)
{
    u32 f = irq_save(), i;
    for (i = 0; i < APP_BLOCKS; i++)
        if (ab[i].id == id) {
            if (m.up && !ab[i].saved)
                vram_free(&m.heap, ab[i].off);
            ab_drop(&ab[i]);
        }
    irq_restore(f);
}

void *mga_vram_ptr(u32 off) { return (u8 *)mga_fb + off; }

u32 mga_vram_size(void) { return m.vram; }

int mga_app_next(u32 id, u32 from, u32 *start, u32 *bytes)
{
    u32 i, best = 0xFFFFFFFFu;
    for (i = 0; i < APP_BLOCKS; i++)
        if (ab[i].id == id && ab[i].off >= from && ab[i].off < best) {
            best = ab[i].off;
            *bytes = ab[i].bytes;
        }
    *start = best;
    return best != 0xFFFFFFFFu;
}

int mga_handover(int kernel, int reset)
{
    int r = 0;
    if (!m.mapped)
        return 0;
    if (reset) {
        engine_reset();                         /* (the watchdog's: an app had it, stuck) */
    } else if (engine_sync(1000000) != 0) {
        kprintf("GLOS-WARN mga-reset when=handover resets=%u\n", engine_resets);
        r = -1;
    }
    if (kernel)
        engine_resync();                        /* nothing the kernel's HAL knew of the registers holds */
    m.busy = 0;
    return r;
}

/* ---- the desktop's windows' pixels (step E) */

void *mga_store_alloc(u32 bytes)
{
    void *p = 0;
    u32 off, f = irq_save();
    if (m.up && (!m.store_max || vram_owned_bytes(&m.heap, OWN_STORE) + bytes <= m.store_max)
        && vram_alloc(&m.heap, bytes, 64, 0, OWN_STORE, &off) == 0)
        p = (u8 *)mga_fb + off;
    irq_restore(f);
    return p;
}

static int owns(const void *p) { return mga_fb && (const u8 *)p >= (const u8 *)mga_fb && (const u8 *)p < (const u8 *)mga_fb + m.vram; }

int mga_store_free(void *p)
{
    u32 f;
    if (!owns(p))
        return 0;
    f = irq_save();
    if (m.up)
        vram_free(&m.heap, (u32)((u8 *)p - (u8 *)mga_fb));
    irq_restore(f);
    return 1;
}

const struct gfx_dev *mga_store_dev(const void *p) { return owns(p) ? &mga_dev : 0; }

void mga_store_limit(u32 bytes) { m.store_max = bytes; }

u32 mga_display_stat(char *buf, u32 max)
{
    u32 largest, free_;
    if (m.off)
        return (u32)ksnprintf(buf, max, "accel off (GLOS.CFG)\n");
    if (!m.found)
        return (u32)ksnprintf(buf, max, "accel none (no Matrox card)\n");
    if (!m.up)
        return (u32)ksnprintf(buf, max, "accel idle %s\n", mga.name);
    free_ = vram_free_bytes(&m.heap, &largest);
    return (u32)ksnprintf(buf, max, "accel on %s vram=%uK free=%uK largest=%uK draws=%u\n", mga.name, m.vram >> 10,
                          free_ >> 10, largest >> 10, m.ops);
}

/* ---- the device behind the screen's surface */

/* Where a surface's pixels are in VRAM, or -1 if they aren't the card's. */
static s32 vram_off(const struct gfx_surface *s)
{
    if (s->pix >= m.screen_pix && s->pix < m.screen_pix + m.screen_bytes)
        return (s32)(m.screen.color_off + (u32)(s->pix - m.screen_pix));
    if (mga_fb && s->pix >= (const u8 *)mga_fb && s->pix < (const u8 *)mga_fb + m.vram)
        return (s32)(s->pix - (const u8 *)mga_fb);
    return -1;
}

/* The engine (taken) set on surface s: 1, or 0 (not taken) if it can't draw
   there: not up, not the card's memory, not a layout the engine has. */
static int on(const struct gfx_surface *s)
{
    u32 bpp_b = s->fmt.bpp / 8u, pitch_px;
    s32 off;
    take();
    off = m.up ? vram_off(s) : -1;
    pitch_px = s->pitch / bpp_b;
    if (off < 0 || (off & 63) || s->pitch % bpp_b || (pitch_px & 31) || pitch_px > 4096) {
        give();
        return 0;
    }
    if ((u32)off != m.cur.color_off || (int)pitch_px != m.cur.pitch_px || s->fmt.bpp != m.cur.bpp) {
        memset(&m.cur, 0, sizeof m.cur);
        m.cur.color_off = (u32)off;
        m.cur.pitch_px = (int)pitch_px;
        m.cur.bpp = s->fmt.bpp;
        m.cur.zbits = 16;
        engine_set_target(&m.cur);
        engine_set_clip(0, 0, m.cur.pitch_px, s->h);
    }
    m.busy = 1;
    m.ops++;
    return 1;
}

static int dev_fill(struct gfx_surface *s, const struct gfx_rect *r, unsigned px)
{
    if (!on(s))
        return 0;
    engine_fill(r->x, r->y, r->w, r->h, px);
    give();
    return 1;
}

static int dev_copy(struct gfx_surface *d, const struct gfx_rect *r, const struct gfx_surface *s, int sx, int sy)
{
    u32 bytes = (u32)r->w * (d->fmt.bpp / 8u);
    s32 soff;
    int j;
    if (!on(d))
        return 0;
    if (s->dev) {                               /* VRAM to VRAM */
        if ((soff = vram_off(s)) < 0) {
            give();
            return 0;
        }
        engine_copy_from((u32)soff, (int)(s->pitch / (s->fmt.bpp / 8u)), sx, sy, r->x, r->y, r->w, r->h);
    } else {                                    /* memory to VRAM: rows from the CPU */
        if (bytes > sizeof row_buf || !engine_image_begin(r->x, r->y, r->w, r->h)) {
            give();
            return 0;
        }
        for (j = 0; j < r->h; j++) {
            memcpy(row_buf, s->pix + (u32)(sy + j) * s->pitch + (u32)sx * (s->fmt.bpp / 8u), bytes);
            engine_iload_row(row_buf);
        }
        engine_iload_end();
    }
    give();
    return 1;
}

static int dev_mono(struct gfx_surface *s, const struct gfx_rect *r, int x, int y, const unsigned char *bits, int w, int h,
                    unsigned stride, unsigned fg, unsigned bg)
{
    if (w > 4096 || !on(s))                     /* (wider: the HAL's limit, the software's to draw) */
        return 0;
    engine_set_clip(r->x, r->y, r->x + r->w, r->y + r->h);
    engine_mono(x, y, w, h, bits, (int)stride, fg, bg == GFX_NONE ? 0 : bg, bg == GFX_NONE ? MGA_MONO_TRANSPARENT : 0);
    engine_set_clip(0, 0, m.cur.pitch_px, s->h);
    give();
    return 1;
}

static void dev_sync(const struct gfx_surface *s)
{
    (void)s;
    mga_display_sync();
}

const struct gfx_dev mga_dev = { dev_fill, dev_copy, dev_mono, dev_sync };
