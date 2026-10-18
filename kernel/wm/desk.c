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
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
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

u32 desk_frames(void) { return frames; }

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

/* The task bar's clock and note, when either has changed. */
static void bar_text(struct wm *m)
{
    char t[8];
    u32 f = irq_save(), h = bcd(rtc_read(4) & 0x7F), min = bcd(rtc_read(2));
    irq_restore(f);
    t[0] = (char)('0' + h / 10 % 10), t[1] = (char)('0' + h % 10), t[2] = ':';
    t[3] = (char)('0' + min / 10 % 10), t[4] = (char)('0' + min % 10), t[5] = 0;
    if (!demo && (memcmp(t, m->clock, 6) || strcmp(note, m->note)))
        wm_bar_text(m, t, note);
}

static void desk_thread(void *arg)
{
    struct gfx_surface *fb;
    struct wm *m;
    u32 f;
    (void)arg;
    for (;;) {
        f = irq_save();
        if (!changed)
            thread_wait(&wakeq, timer_ticks() + 5 * 1024);      /* (the clock, at the latest) */
        changed = 0;
        irq_restore(f);
        clock_second();                         /* (summer time's changes, without a network: M10) */
        m = desk_lock();
        deskin_turn(m);
        fb = display_fb();
        wm_hw_pointer(m, fb && display_cursor(m->pointer, m->px, m->py));     /* (M7 step F) */
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
            if (m->dirty.n)
                frames++;                       /* (glos rec's: a frame has changed) */
            vnc_damage(&m->dirty, fb);          /* (what VNC's viewers are owed: M10) */
            wm_compose(m, fb);
        }
        deskin_focus(m);
        f = irq_save();                         /* (not desk_unlock(): that would wake this thread again) */
        owner = 0;
        thread_wake(&lockq);
        irq_restore(f);
    }
}

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
        wm_bar_text(m, "", note);               /* (the composer's thread puts the time back) */
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
                            : !(w->flags & WF_FRAME) ? " bare" : w->owner ? " app" : "", w->frame.x,
                            w->frame.y, w->store.w, w->store.h, w->title);
    desk_unlock();
    return n < max ? n : max;
}

void desk_init(void)
{
    wm_init(&desk, &host, 640, 480, &gfx_fmt8); /* (the screen's own size and format when there is one) */
    wm_bar(&desk, 1);
    deskin_init();
    {
        u8 and_mask[512], xor_mask[512];
        wm_pointer_masks(and_mask, xor_mask);
        display_cursor_image(and_mask, xor_mask);
    }
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
