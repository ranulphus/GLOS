/* The secrets store's master password, asked of the user (API 2.0's SECRETS; docs/v1/phase-b.md step 13): a
 * dialog of the desktop's own, so that no app sees the password.
 *
 * One at a time. The app's thread that needs the store open makes the window (framed, in front of everything,
 * with the keyboard) and waits; the desktop's thread gives the window's keys to deskpass_key (deskin.c: Enter,
 * Esc, Backspace, characters), which draws a star for each character. Enter hands what was typed to the waiting
 * thread, which tries it (secrets.c: half a second of arithmetic, on the app's own thread): right, the window
 * goes; wrong, it says so and waits again. Esc, the window closed, or the app ending is "no". A second app that
 * needs the store meanwhile waits for the same answer. */
#include <string.h>

#include "app.h"
#include "deskint.h"
#include "display.h"
#include "gfx.h"
#include "io.h"
#include "sched.h"
#include "secrets.h"
#include "theme.h"
#include "timer.h"

#define W 300
#define H 96
#define MAXLEN 63

static struct {
    u32 win;                                    /* the dialog's window, 0: none */
    char text[MAXLEN + 1];
    u32 len;
    u8 entered, cancelled, wrong;
    struct waitq q;
} pw;

static void draw(struct wm *m)
{
    struct wm_window *w = pw.win ? wm_find(m, pw.win) : 0;
    struct theme_look c;
    struct gfx_surface *s;
    char stars[MAXLEN + 1];
    u32 i;
    if (!w)
        return;
    theme_look(desk_theme(), &c);
    s = &w->store;
    gfx_fill(s, 0, 0, W, H, c.bar);
    gfx_text(s, 12, 10, &gfx_font_ui, pw.wrong ? "That was not it. The master password:" : "The master password for your secrets:", -1,
             c.bar_text);
    gfx_fill(s, 12, 34, W - 24, 22, c.face);
    gfx_frame(s, 12, 34, W - 24, 22, c.edge);
    for (i = 0; i < pw.len && i < 40; i++)
        stars[i] = '*';
    stars[i] = 0;
    gfx_text(s, 18, 38, &gfx_font_ui, stars, -1, c.text);
    gfx_fill(s, 18 + gfx_text_width(&gfx_font_ui, stars, -1) + 1, 38, 1, 14, c.text);        /* the caret */
    gfx_text(s, 12, 70, &gfx_font_ui, "Enter: open them     Esc: not now", -1, c.bar_text);
    wm_damage(m, w, 0);
}

/* A key for the window with the focus (the desktop's thread, the manager's lock held): 1 if it is the dialog's. */
int deskpass_key(struct wm *m, u32 key, int down, u32 ch)
{
    if (!pw.win || !m->focus || m->focus->id != pw.win)
        return 0;
    if (!down || pw.entered || pw.cancelled)
        return 1;
    if (key == 0x1C || key == 0x11C || key == 0x01) {       /* Enter, or Esc: the waiting thread's to act on */
        u32 f = irq_save();
        if (key == 0x01)
            pw.cancelled = 1;
        else
            pw.entered = 1;
        thread_wake(&pw.q);
        irq_restore(f);
    } else if (key == 0x0E) {
        if (pw.len)
            pw.text[--pw.len] = 0;
    } else if (ch >= 32 && ch < 127 && pw.len < MAXLEN) {
        pw.text[pw.len++] = (char)ch;
        pw.text[pw.len] = 0;
    }
    draw(m);
    return 1;
}

/* Each turn of the desktop: the dialog drawn again when the screen changed under it (its pixels were cleared). */
void deskpass_turn(struct wm *m, int again)
{
    if (pw.win && again)
        draw(m);
}

int desk_password(struct app *a)
{
    struct wm *m;
    struct wm_window *w;
    int r = -1, mine = 0;
    for (;;) {
        u32 f;
        if (secrets_state() == SEC_OPEN)
            return 0;
        if (secrets_state() != SEC_LOCKED || app_ending(a) || !display_fb())
            return -1;                          /* (nothing to open; or nobody to ask: no desktop on the screen) */
        m = desk_lock();
        if (!pw.win) {                          /* its dialog: this thread's */
            w = wm_create(m, (m->w - W) / 2, (m->h - H) / 3, W, H, "Secrets", WF_FRAME | WF_FIXED | WF_TOPMOST | WF_NOBAR | WF_NOMIN, 0);
            if (!w) {
                desk_unlock();
                return -1;
            }
            pw.win = w->id;
            pw.len = 0, pw.text[0] = 0;
            pw.entered = pw.cancelled = pw.wrong = 0;
            mine = 1;
            draw(m);
            wm_show(m, w, 1);
            wm_set_focus(m, w);
        }
        desk_unlock();
        desk_kick();
        f = irq_save();                         /* an answer, or (another's dialog) its end */
        if (!(mine && (pw.entered || pw.cancelled)))
            thread_wait(&pw.q, timer_ticks() + 512);
        irq_restore(f);
        if (!mine)
            continue;                           /* (looked at again: open by now, or the dialog gone and asked anew) */
        m = desk_lock();
        w = wm_find(m, pw.win);
        if (!w || pw.cancelled || app_ending(a)) {      /* closed (Alt-F4, its close box), Esc, or the app going */
            r = -1;
        } else if (pw.entered) {
            char typed[MAXLEN + 1];
            memcpy(typed, pw.text, sizeof typed);
            desk_unlock();
            r = secrets_unlock(typed) == 0 ? 0 : 1;     /* (half a second: without the manager's lock) */
            memset(typed, 0, sizeof typed);
            m = desk_lock();
            w = wm_find(m, pw.win);
            if (r == 1 && w) {                  /* wrong: asked again */
                pw.len = 0, pw.text[0] = 0;
                pw.entered = 0, pw.wrong = 1;
                draw(m);
                desk_unlock();
                continue;
            }
        } else {
            desk_unlock();
            continue;                           /* (a turn with nothing to it) */
        }
        if (w)
            wm_destroy(m, w);
        pw.win = 0;
        memset(pw.text, 0, sizeof pw.text);
        pw.len = 0;
        desk_refocus(m);
        desk_unlock();
        desk_kick();
        f = irq_save();
        thread_wake(&pw.q);                     /* (others who waited for this answer) */
        irq_restore(f);
        return r == 0 ? 0 : -1;
    }
}
