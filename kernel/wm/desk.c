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
 * Windows' pixels are whole pages of their own (mm_win_alloc), so that an
 * app's window can be mapped into the app as well (step F); the small
 * things come from the kernel's heap.
 *
 * The task bar shows the time from the RTC, to the minute, and a note while
 * someone is connected over ssh (PRD D35): there, and never drawn over a
 * program's own picture. */
#include <string.h>

#include "deskint.h"
#include "display.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "timer.h"

void wm_demo(struct wm *m);

static struct wm desk;
static struct waitq lockq, wakeq;
static struct thread *owner;                    /* who has the manager */
static volatile u8 changed, repaint;
static u8 demo;                                 /* the demonstration's windows are up: its clock stands */
static char note[16];

static void *d_alloc(u32 bytes, int pixels) { return pixels ? mm_win_alloc(bytes) : kmalloc(bytes); }

static void d_free(void *p, u32 bytes, int pixels)
{
    if (!pixels)
        kfree(p);
    else if (!deskapp_keep(p))                  /* (an app that has them mapped lets go first) */
        mm_win_free(p, bytes);
}

static const struct wm_host host = { d_alloc, d_free };

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
        m = desk_lock();
        deskin_turn(m);
        fb = display_fb();
        if (fb) {
            int fmt = fb->fmt.bpp != m->fmt.bpp || fb->fmt.gsize != m->fmt.gsize || fb->fmt.rpos != m->fmt.rpos;
            if (repaint || fmt || fb->w != m->w || fb->h != m->h) {
                repaint = 0;
                if (fmt || fb->w != m->w || fb->h != m->h)
                    wm_screen(m, fb->w, fb->h, &fb->fmt);
                if (fmt)
                    deskapp_screen(m);          /* (every window has new pixels: the apps are told) */
                wm_damage_all(m);
            }
            bar_text(m);
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
    u32 n = (u32)ksnprintf(buf, max, "desk %dx%dx%u windows=%u focus=%u pointer=%d,%d%s\n", m->w, m->h, m->fmt.bpp, m->count,
                           m->focus ? m->focus->id : 0, m->px, m->py, m->pointer ? "" : " hidden");
    for (w = m->top; w && n < max; w = w->below)
        n += (u32)ksnprintf(buf + n, max - n, "window %u %s%s at %d,%d %dx%d \"%s\"\n", w->id,
                            w->flags & WF_SHOWN ? "shown" : "hidden", w == m->bar ? " bar" : w->owner ? " app" : "", w->frame.x,
                            w->frame.y, w->store.w, w->store.h, w->title);
    desk_unlock();
    return n < max ? n : max;
}

void desk_init(void)
{
    wm_init(&desk, &host, 640, 480, &gfx_fmt8); /* (the screen's own size and format when there is one) */
    wm_bar(&desk, 1);
    deskin_init();
    display_painter(painter);
    thread_create("desk", PRIO_NORMAL, desk_thread, NULL);
}
