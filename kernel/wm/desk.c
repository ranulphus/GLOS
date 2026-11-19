/* The desktop (kernel/include/desk.h; PRD §9.4; docs/graphics.md §3): the
 * window manager's core at work in the kernel.
 *
 * One manager, for the one screen. Its state is one thread's at a time
 * (desk_lock): the composer's own thread, an app's making or drawing in a
 * window, the agent's. The composer's thread is of the normal class, with
 * the system VM and the apps: drawing a whole screen takes a 486 tens of
 * milliseconds, and an urgent thread would keep the system VM from its
 * interrupts that long (the first desktop after a session did: vif-stuck).
 * It mostly waits, so it runs at once when woken, and the VM thread still
 * goes first. It wakes when something changed, and draws what is
 * out of date on the screen (wm_compose) when the screen is the desktop's;
 * while a session has it nothing is drawn, and the windows, which keep
 * their pixels, are all there again when it comes back (the display calls
 * the painter then, which says "everything").
 *
 * An app's window's pixels are whole pages of their own (mm_win_alloc), so
 * that they can be mapped into the app as well; the small things come from
 * the kernel's heap. The desktop's own windows (the task bar, its own
 * windows) keep their pixels in the card's memory while a drawing engine is
 * up and there is room (M7 step E), where the engine copies them to the
 * screen; before the screen changes hands (a session, another mode) they
 * are moved into the system's memory, and back when the engine is up again.
 * Apps' windows stay in the system's memory: a session takes the card's
 * memory whole, and an app may be drawing in its window meanwhile.
 *
 * The task bar shows the time from the RTC, to the minute, and a note while
 * someone is connected over ssh (PRD D35): there, and never drawn over a
 * program's own picture. */
#include <string.h>

#include "clock.h"
#include "deskint.h"
#include "display.h"
#include "glos/region.h"
#include "idle.h"
#include "saver.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "theme.h"
#include "timer.h"
#include "vnc.h"

void wm_demo(struct wm *m);

static struct wm desk;
static struct waitq lockq, wakeq;
static struct thread *owner;                    /* who has the manager */
static volatile u8 changed, repaint;
static u8 demo;                                 /* the demonstration's windows are up: its clock stands */
static char note[16];
static volatile u32 frames;                     /* composes that drew something */
static u32 cursor_gen;                          /* the pointer's shape the card's cursor has (wm's cursor_gen) */
static s32 cursor_hx, cursor_hy;                /* ... and its hot spot */

/* The pointer's shape now into the card's cursor. */
static void cursor_image(struct wm *m)
{
    u8 and_mask[512], xor_mask[512];
    wm_pointer_masks(m, and_mask, xor_mask, &cursor_hx, &cursor_hy);
    display_cursor_image(and_mask, xor_mask);
    cursor_gen = m->cursor_gen;
}

u32 desk_frames(void) { return frames; }

/* A full-screen window that is all there is to see, in pixels of an app's own in the card's memory, is shown by
   pointing the display at them, not by copying them to the screen (docs/v1/fullscreen-apps.md, F1). While it is
   so nothing is composed: what lies under it is drawn when it is seen again. VNC's viewers are shown the
   composed screen, so with one there the window is copied as any other. */
static u32 scan_id;                             /* the window shown so, by its id; 0: none */
static u8 scan_left;                            /* it has just stopped being so: the screen composed whole, then
                                                   the display on it again */
static u32 scan_no;                             /* the last window that could have been, but for its pixels (the log's) */

/* The card is changing into or out of a mode of a window's own: nothing is drawn, and what was shown stays
   until the change is done. Once it is out of one, the screen is composed whole. */
static int scan_wait(struct wm *m)
{
    if (scan_id) {
        kprintf("GLOS-DESK scanout off win=%u why=mode\n", scan_id);
        scan_id = 0;
    }
    scan_left = 1;
    wm_damage_all(m);
    return 1;
}

static int scan_turn(struct wm *m, const struct gfx_surface *fb, int viewers)
{
    struct wm_window *w = viewers ? 0 : wm_scanout(m);
    int own = w && (w->ext.w != fb->w || w->ext.h != fb->h), r;
    /* A size of its own (WF_OWNMODE; F3): the card in a mode of that size first, by the system VM's thread, which
       tells this one when it is done; until then the window is composed in the middle of the screen. Out of
       such a mode the same way, before anything is drawn on the screen again: 1 (nothing composed) while the
       card is in one or changing. */
    if (own) {
        struct gfx_rect c;
        wm_client(m, w, &c);
        r = display_app_mode((u32)w->ext.w, (u32)w->ext.h, c.x, c.y, desk_kick);
        if (r == 0)
            return scan_wait(m);                /* (asked for) */
        if (r < 0)
            w = 0, own = 0;                     /* (no such mode: composed in the middle, for good) */
    }
    if (!own && (display_app_mode(0, 0, 0, 0, desk_kick) == 0 || display_app_busy()))
        return scan_wait(m);                    /* (the desktop's mode is on its way back) */
    if (w && (own || w->ext.pitch == fb->pitch) && display_scanout(&w->ext)) {
        if (scan_id != w->id) {
            scan_id = w->id;
            scan_left = 0;
            kprintf("GLOS-DESK scanout on win=%u\n", w->id);
        }
        gfx_region_set(&m->dirty, 0);
        return 1;
    }
    /* (The mode is there, but its rows are not the block's length: never again for this size; the desktop's mode
       back first.) */
    if (own) {
        display_app_refuse();
        return scan_wait(m);
    }
    if (scan_id) {
        kprintf("GLOS-DESK scanout off win=%u why=%s\n", scan_id, viewers ? "viewer" : !w ? "window" : "pixels");
        scan_id = 0;
        scan_left = 1;
        wm_damage_all(m);
    } else if (w && w->id != scan_no) {         /* (said once a window: why such a window is copied all the same) */
        scan_no = w->id;
        kprintf("GLOS-DESK scanout no win=%u pitch=%u screen=%u\n", w->id, w->ext.pitch, fb->pitch);
    }
    return 0;
}

/* The window shown so has other pixels to show (an app's swap: desk_win_vram, in the app's own call, under the
   desktop's lock): the display goes to them now, before the app draws into those it showed. If it cannot (they
   are not of the screen's row length any more), the display is the screen's again at once, composed as soon as
   the desktop's thread has its turn. 1 if the display is on the window's new pixels. */
int desk_scan_swap(struct wm *m, struct wm_window *w)
{
    (void)m;
    if (!scan_id || scan_id != w->id)
        return 0;
    if (w->has_ext && display_scanout(&w->ext)) {
        frames++;
        return 1;
    }
    display_scanout(0);                         /* (the next turn sees no such window, and composes) */
    desk_kick();
    return 0;
}

static void *d_alloc(u32 bytes, int kind)
{
    void *p;
    if (kind == WM_MEM_SMALL)
        return kmalloc(bytes);
    if (kind == WM_MEM_DESK && (p = display_vram_alloc(bytes)) != 0)
        return p;
    return mm_win_alloc(bytes);
}

static void d_free(void *p, u32 bytes, int kind)
{
    if (kind == WM_MEM_SMALL)
        kfree(p);
    else if (display_vram_free(p))
        ;
    else if (kind != WM_MEM_APP || !deskapp_keep(p))    /* (an app that has them mapped lets go first) */
        mm_win_free(p, bytes);
}

static const struct wm_host host = { d_alloc, d_free, display_vram_dev };

/* The display is about to change hands or modes: the desktop's windows'
   pixels out of the card's memory (on the display's thread). */
static void evict(void)
{
    struct wm *m = desk_lock();
    struct wm_window *w;
    u32 n = 0, failed = 0;
    for (w = m->bottom; w; w = w->above)
        if (w->store.dev)
            wm_store_again(m, w, WM_MEM_RAM) == 0 ? n++ : failed++;
    if (n || failed)
        kprintf("GLOS-DESK vram-out windows=%u failed=%u\n", n, failed);
    desk_unlock();
}

/* The engine is up: the desktop's windows' pixels into the card's memory,
   while there is room. */
static void place(struct wm *m)
{
    struct wm_window *w;
    u32 n = 0;
    for (w = m->bottom; w; w = w->above)
        if (!w->owner && !w->store.dev && wm_store_again(m, w, WM_MEM_DESK) == 0 && w->store.dev)
            n++;
    if (n)
        kprintf("GLOS-DESK vram-in windows=%u\n", n);
}

/* The desktop has the screen and is not done with it: a thread is in the manager (composing, or drawing in a
   window), or something is damaged and not yet composed. For glos shot (another thread, no lock taken: a look). */
int desk_drawing(void)
{
    return display_fb() && ((owner && owner != current) || desk.dirty.n != 0);
}

struct wm *desk_lock(void)
{
    u32 f = irq_save();
    while (owner && owner != current)
        thread_block(&lockq);
    owner = current;
    irq_restore(f);
    return &desk;
}

void desk_unlock(void)
{
    u32 f;
    deskin_focus(&desk);
    f = irq_save();
    owner = 0;
    thread_wake(&lockq);
    changed = 1;
    thread_wake(&wakeq);
    irq_restore(f);
}

void desk_kick(void)
{
    u32 f = irq_save();
    changed = 1;
    thread_wake(&wakeq);
    irq_restore(f);
}

/* The display has (again) set the desktop's mode: everything is to be drawn. */
static void painter(struct gfx_surface *fb)
{
    (void)fb;
    repaint = 1;
    desk_kick();
}

static u32 bcd(u8 v) { return (v >> 4) * 10u + (v & 15); }

/* The task bar's clock (as the region writes a time: [region] clock) and note, when either has changed. */
static void bar_text(struct wm *m)
{
    char t[sizeof m->clock];
    u32 f = irq_save(), h = bcd(rtc_read(4) & 0x7F), min = bcd(rtc_read(2));
    irq_restore(f);
    region_time(kcfg_region(), t, sizeof t, (int)h, (int)min, -1);
    /* (a remote session's note is an icon in the tray since phase D: desktray.c) */
    if (!demo && strcmp(t, m->clock))
        wm_bar_text(m, t, "");
}

/* ---- the theme ([desktop] theme; kernel/wm/theme.c) */

static const struct theme *theme_now = &themes[0], *theme_shown = &themes[0];

const struct theme *desk_theme(void) { return theme_now; }

/* From anywhere: the theme of that name from now on (the desktop's thread draws it). 0, or -1: no such theme. */
void desk_bar_place(int top, int hide)
{
    struct wm *m = desk_lock();
    struct wm_window *w;
    struct gfx_rect work;
    wm_bar_place(m, top, hide);
    wm_work(m, &work);
    for (w = m->bottom; w; w = w->above)        /* the shell's desktop is the work area: there, and its size */
        if (w->flags & WF_DESKTOP) {
            wm_move(m, w, work.x, work.y);
            deskapp_resize(m, w, work.w, work.h);
        }
    deskapp_screen(m);                          /* (windows placed in the work area have new sizes: told) */
    desk_unlock();
    desk_kick();
}

void desk_bar_group(int how)
{
    wm_bar_group(desk_lock(), how);
    desk_unlock();
    desk_kick();
}

int desk_theme_set(const char *name)
{
    const struct theme *t = theme_find(name);
    if (!t)
        return -1;
    theme_now = t;
    desk_kick();
    return 0;
}

static void theme_show(struct wm *m, const struct theme *t)
{
    struct wm_look *l = &m->look;
    struct theme_look c;
    theme_look(t, &c);
    l->desk = c.desk, l->edge = c.edge, l->face = c.face, l->text = c.text, l->face_off = c.face_off;
    l->text_off = c.text_off, l->mark = c.mark, l->bar = c.bar, l->bar_text = c.bar_text, l->bar_on = c.bar_on;
    l->bar_on_text = c.bar_on_text;
}

static void desk_thread(void *arg)
{
    struct gfx_surface *fb;
    struct wm *m;
    u32 f;
    int dirty, vnc;
    static struct gfx_region drawn;             /* (the desktop's thread's alone) */
    static u8 saved;                            /* the saver's black screen is drawn */
    static u32 wake_at;                         /* the tick the next turn must come by (0: the usual five seconds) */
    (void)arg;
    for (;;) {
        f = irq_save();
        if (!changed) {                         /* (the clock, at the latest; sooner for a notification's time) */
            u32 at = timer_ticks() + 5 * 1024;
            if (wake_at && (s32)(wake_at - at) < 0)
                at = (s32)(wake_at - timer_ticks()) > 0 ? wake_at : timer_ticks() + 1;
            thread_wait(&wakeq, at);
        }
        changed = 0;
        irq_restore(f);
        clock_second();                         /* (summer time's changes, without a network: M10) */
        m = desk_lock();
        deskapp_hung(m);
        deskin_turn(m);
        deskapp_embeds(m);
        fb = display_fb();
        if (!fb && scan_id)                     /* (the screen is a session's: the display is its program's, and the
                                                   desktop's own again when it comes back) */
            scan_id = scan_left = 0;
        if (theme_shown != theme_now) {         /* a new theme: the frames, the bar and the desktop drawn again */
            theme_shown = theme_now;
            theme_show(m, theme_now);
            kprintf("GLOS-DESK theme=%s\n", theme_now->name);
            wm_bar_again(m);
            repaint = 1;
        }
        /* The saver (kernel/wm/idle.c): a black screen in place of the windows, which are as they were under it;
           or a program's full-screen window (saver.c, phase D), over windows that go on being composed. */
        if (idle_turn(fb != 0, m->focus && (m->focus->flags & WF_FULL))) {
            int program = saver_turn(m);
            if (!saved) {                       /* (either way the pointer is away) */
                display_cursor(0, 0, 0);
                wm_hw_pointer(m, 1);
            }
            if (program) {
                saved = 2;
            } else if (saved != 1) {
                saved = 1;
                if (scan_id) {                  /* (the black screen is the screen's own pixels) */
                    scan_id = scan_left = 0;
                    display_scanout(0);
                }
                gfx_fill(fb, 0, 0, fb->w, fb->h, 0x000000);
                display_sync();
                if (vnc_viewers()) {
                    drawn.n = 1;
                    drawn.r[0].x = drawn.r[0].y = 0, drawn.r[0].w = fb->w, drawn.r[0].h = fb->h;
                    vnc_damage(&drawn, fb);
                }
            }
            if (!program)
                fb = 0;                         /* (nothing is composed under the blank one) */
        } else if (saved) {
            saved = 0;
            saver_end();                        /* (its program, if it had one, is ended) */
            repaint = 1;
        }
        if (!fb)
            wake_at = deskshell_turn(m, 0);     /* (notifications' time stands still while nothing is composed) */
        if (cursor_gen != m->cursor_gen)
            cursor_image(m);
        if (!saved)
            wm_hw_pointer(m, fb && display_cursor(m->pointer, m->px - cursor_hx, m->py - cursor_hy));  /* (M7 step F) */
        if (fb) {
            int fmt = fb->fmt.bpp != m->fmt.bpp || fb->fmt.gsize != m->fmt.gsize || fb->fmt.rpos != m->fmt.rpos;
            if (repaint || fmt || fb->w != m->w || fb->h != m->h) {
                repaint = 0;
                if (fmt || fb->w != m->w || fb->h != m->h)
                    wm_screen(m, fb->w, fb->h, &fb->fmt);
                if (fmt)
                    deskapp_screen(m);          /* (every window has new pixels: the apps are told) */
                place(m);
                wm_damage_all(m);
            }
            bar_text(m);
            wake_at = deskshell_turn(m, 1);     /* the tray's tooltip, the notification shown (deskshell.c) */
            dirty = m->dirty.n != 0;
            if ((vnc = vnc_viewers() != 0))
                drawn = m->dirty;
            if (scan_turn(m, fb, vnc)) {        /* (a full-screen window's own pixels are on the display) */
                dirty = 0;
            } else {
                if (scan_left && vnc) {         /* (the viewers are owed the whole screen) */
                    drawn.n = 1;
                    drawn.r[0].x = drawn.r[0].y = 0, drawn.r[0].w = fb->w, drawn.r[0].h = fb->h;
                }
                wm_compose(m, fb);
                if (scan_left) {                /* the screen is whole again: the display back on it */
                    scan_left = 0;
                    display_sync();
                    display_scanout(0);
                }
            }
            /* What changed, once it is drawn (M10): VNC's viewers are owed it, and glos rec counts a frame. After the
               compose, so that a viewer's update or a capture read while it was being drawn is made again. */
            if (vnc)
                vnc_damage(&drawn, fb);
            if (dirty)
                frames++;
        }
        deskin_focus(m);
        f = irq_save();                         /* (not desk_unlock(): that would wake this thread again) */
        owner = 0;
        thread_wake(&lockq);
        irq_restore(f);
        desktray_keep();                        /* (the volume, kept in GLOS.CFG: without the manager's lock) */
    }
}

const char *desk_note_text(void) { return note; }

void desk_note(const char *text)
{
    u32 i;
    for (i = 0; text[i] && i < sizeof note - 1; i++)
        note[i] = text[i];
    note[i] = 0;
    desk_kick();
}

int desk_demo(int on)
{
    struct wm *m = desk_lock();
    struct wm_window *w, *next;
    int r = 0;
    for (w = m->bottom; w; w = next) {          /* the demonstration's windows are the kernel's own, with a frame */
        next = w->above;
        if (!w->owner && (w->flags & WF_FRAME))
            wm_destroy(m, w);
    }
    demo = 0;
    wm_pointer(m, m->w / 2, m->h / 2, 0);
    if (on) {
        u32 before = m->count;
        wm_demo(m);
        demo = 1;
        r = m->count >= before + 3 ? 0 : -1;
    } else {
        wm_bar(m, 1);
        wm_bar_text(m, "", "");                 /* (the composer's thread puts the time back) */
    }
    desk_unlock();
    return r;
}

u32 desk_stat(char *buf, u32 max)
{
    struct wm *m = desk_lock();
    const struct wm_window *w;
    u32 n, vram = 0, bytes = 0;
    for (w = m->top; w; w = w->below)
        if (w->store.dev)                       /* (pixels in the card's memory) */
            vram++, bytes += w->store.pitch * (u32)w->store.h;
    n = (u32)ksnprintf(buf, max, "desk %dx%dx%u windows=%u focus=%u pointer=%d,%d%s vram=%u/%uK\n", m->w, m->h, m->fmt.bpp,
                       m->count, m->focus ? m->focus->id : 0, m->px, m->py, m->pointer ? "" : " hidden", vram, bytes >> 10);
    for (w = m->top; w && n < max; w = w->below)
        n += (u32)ksnprintf(buf + n, max - n, "window %u %s%s at %d,%d %dx%d \"%s\"\n", w->id,
                            w->flags & WF_SHOWN ? "shown" : "hidden", w == m->bar ? " bar" : w->flags & WF_POPUP ? " popup"
                            : w->flags & WF_DESKTOP ? " desktop"
                            : !(w->flags & WF_FRAME) ? " bare" : w->flags & WF_HUNG ? " hung" : w->owner ? " app" : "", w->frame.x,
                            w->frame.y, w->store.w, w->store.h, w->title);
    if (n < max)
        n += deskshell_list(m, buf + n, max - n);       /* (and the tray's icons: phase D) */
    desk_unlock();
    return n < max ? n : max;
}

void desk_init(void)
{
    wm_init(&desk, &host, 640, 480, &gfx_fmt8); /* (the screen's own size and format when there is one) */
    wm_bar(&desk, 1);
    deskin_init();
    cursor_image(&desk);
    display_painter(painter);
    display_evicter(evict);
    thread_create("desk", PRIO_NORMAL, desk_thread, NULL);
}

/* For GLOS_FN_STAT (M9): the windows an app has (owner NULL: every window). */
u32 desk_windows(const void *owner)
{
    struct wm *m = desk_lock();
    const struct wm_window *w;
    u32 n = 0;
    for (w = m->top; w; w = w->below)
        if (!owner || w->owner == owner)
            n++;
    desk_unlock();
    return n;
}
