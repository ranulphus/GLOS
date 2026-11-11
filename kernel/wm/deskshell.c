/* The desktop's tray and notifications (API 2.0's SHELL; docs/v1/shell.md): what the kernel draws so that it
 * never waits for an app.
 *
 * The tray's icons are the task bar's (wm.c draws them); here is whose each is and its tooltip, shown in a small
 * window of the desktop's own above the icon once the pointer has rested on it. A notification is a window of
 * the desktop's own above the tray: notes.c says which is shown and for how long; it is drawn from the theme.
 * Presses on either are the desktop's input's (deskin.c), which asks here first. Apps hear of them through the
 * API (kernel/app/api2shell.c). */
#include <string.h>

#include "app.h"
#include "deskint.h"
#include "display.h"
#include "gfx.h"
#include "notes.h"
#include "theme.h"
#include "timer.h"

#define TIP_REST 600u                   /* ticks the pointer rests on an icon before its tooltip comes */

static struct {
    u32 id;                             /* the bar's (wm_tray_add) */
    struct app *a;
    char tip[64];
} trays[WM_TRAY];
static u32 tip_id, tip_since, tip_win;  /* the icon under the pointer, since when; its tooltip's window */
static u32 press_id, press_at;          /* the last icon pressed, and when: twice soon is a double click */

static struct notes notes;
static u32 note_win;                    /* the window of the one shown */
static u32 look_w, look_h, look_bpp;    /* the screen and the theme the two windows were drawn for */
static const void *look_theme;
static struct note_layout note_l;

static const struct gfx_font *f_ui = &gfx_font_ui, *f_bold = &gfx_font_bold;

static int width(const char *text, int len, int bold) { return gfx_text_width(bold ? f_bold : f_ui, text, len); }

/* ---- the tray */

u32 desk_tray_add(struct app *a, const u32 *icon, const char *tip)
{
    struct wm *m = desk_lock();
    u32 id = wm_tray_add(m, icon), i;
    for (i = 0; id && i < WM_TRAY; i++)
        if (!trays[i].id) {
            trays[i].id = id;
            trays[i].a = a;
            strncpy(trays[i].tip, tip ? tip : "", sizeof trays[i].tip - 1);
            trays[i].tip[sizeof trays[i].tip - 1] = 0;
            break;
        }
    desk_unlock();
    return id;
}

static int tray_of(u32 id)
{
    u32 i;
    for (i = 0; i < WM_TRAY; i++)
        if (id && trays[i].id == id)
            return (int)i;
    return -1;
}

void desk_tray_set(u32 id, const u32 *icon, const char *tip)
{
    struct wm *m = desk_lock();
    int i = tray_of(id);
    if (i >= 0 && icon)
        wm_tray_icon(m, id, icon);
    if (i >= 0 && tip) {
        strncpy(trays[i].tip, tip, sizeof trays[i].tip - 1);
        trays[i].tip[sizeof trays[i].tip - 1] = 0;
        if (tip_id == id)
            tip_id = 0;                 /* (shown again, with the new words) */
    }
    desk_unlock();
}

int desk_tray_tip(u32 id, char *out, u32 room)
{
    int i, n = -1;
    desk_lock();
    if ((i = tray_of(id)) >= 0) {
        strncpy(out, trays[i].tip, room - 1);
        out[room - 1] = 0;
        n = (int)strlen(out);
    }
    desk_unlock();
    return n;
}

void desk_tray_remove(struct app *a, u32 id)
{
    struct wm *m = desk_lock();
    u32 i;
    for (i = 0; i < WM_TRAY; i++)
        if (trays[i].id && trays[i].a == a && (!id || trays[i].id == id)) {
            wm_tray_remove(m, trays[i].id);
            trays[i].id = 0;
        }
    desk_unlock();
}

static void tip_hide(struct wm *m)
{
    struct wm_window *w = tip_win ? wm_find(m, tip_win) : 0;
    if (w)
        wm_destroy(m, w);
    tip_win = 0;
}

/* The pointer over the tray: an icon's tooltip once it has rested there. The tick to look again at, 0 none. */
static u32 tip_turn(struct wm *m, u32 now)
{
    u32 id = wm_tray_hit(m, m->px, m->py);
    struct gfx_rect r;
    struct wm_window *w;
    struct theme_look c;
    int i;
    if (id != tip_id) {
        tip_hide(m);
        tip_id = id;
        tip_since = now;
    }
    if (!id || tip_win || (i = tray_of(id)) < 0 || !trays[i].tip[0])
        return 0;
    if (now - tip_since < TIP_REST)
        return tip_since + TIP_REST;
    if (!wm_bar_part(m, WB_TRAY, id, &r))
        return 0;
    {
        s32 tw = width(trays[i].tip, -1, 0) + 8, x = r.x + 8 - tw / 2;
        if (x + tw > m->w - 2)
            x = m->w - 2 - tw;
        if (x < 2)
            x = 2;
        if (!(w = wm_create(m, x, m->bar->frame.y - f_ui->height - 8, tw, f_ui->height + 4, "",
                            WF_TOPMOST | WF_NOFOCUS | WF_NOBAR, 0)))
            return 0;
        theme_look(desk_theme(), &c);
        gfx_fill(&w->store, 0, 0, tw, f_ui->height + 4, c.edge);
        gfx_fill(&w->store, 1, 1, tw - 2, f_ui->height + 2, 0xFFFFFF);
        gfx_text(&w->store, 4, 2, f_ui, trays[i].tip, -1, 0x000000);
        wm_show(m, w, 1);
        tip_win = w->id;
    }
    return 0;
}

/* ---- notifications */

u32 desk_notify_add(struct app *a, const struct note *n)
{
    u32 id;
    desk_lock();
    id = notes_add(&notes, n, app_id(a));
    desk_unlock();
    return id;
}

void desk_notify_remove(struct app *a, u32 id)
{
    desk_lock();
    notes_remove(&notes, app_id(a), id);
    desk_unlock();                      /* (the desktop's turn takes its window away) */
}

static void note_hide(struct wm *m)
{
    struct wm_window *w = note_win ? wm_find(m, note_win) : 0;
    if (w)
        wm_destroy(m, w);
    note_win = 0;
}

static void note_draw(struct wm *m, const struct note *n)
{
    struct theme_look c;
    struct wm_window *w;
    struct gfx_surface *s;
    s32 i, j, run;
    u32 k;
    note_layout(n, width, &note_l);
    if (!(w = wm_create(m, m->w - note_l.w - 6, (m->bar ? m->bar->frame.y : m->h) - note_l.h - 6, note_l.w, note_l.h, "",
                        WF_TOPMOST | WF_NOFOCUS | WF_NOBAR, 0)))
        return;
    theme_look(desk_theme(), &c);
    s = &w->store;
    gfx_fill(s, 0, 0, note_l.w, note_l.h, c.edge);                      /* a line round it; the task bar's colours */
    gfx_fill(s, 1, 1, note_l.w - 2, note_l.h - 2, c.bar);
    gfx_fill(s, 1, 1, note_l.w - 2, 2, c.mark);
    if (n->has_icon)
        for (j = 0; j < 16; j++)
            for (i = 0; i < 16; i += run) {
                u32 px = n->icon[j * 16 + i];
                for (run = 1; i + run < 16 && n->icon[j * 16 + i + run] == px; run++) ;
                if (px >> 24 >= 0x80)
                    gfx_fill(s, 8 + i, 8 + j, run, 1, px & 0xFFFFFF);
            }
    gfx_text(s, note_l.text_x, note_l.title_y, f_bold, n->title, -1, c.bar_text);
    for (k = 0; k < note_l.nlines; k++) {
        gfx_text(s, note_l.text_x, note_l.line_y[k], f_ui, n->text + note_l.line_at[k], (int)note_l.line_len[k], c.bar_text);
        if (k + 1 == note_l.nlines && note_l.cut)
            gfx_text(s, note_l.text_x + width(n->text + note_l.line_at[k], (int)note_l.line_len[k], 0), note_l.line_y[k], f_ui,
                     "...", -1, c.bar_text);
    }
    gfx_text(s, note_l.close.x + 2, note_l.close.y - 2, f_bold, "x", -1, c.bar_text);
    for (k = 0; k < n->nactions; k++) {
        gfx_frame(s, note_l.act[k].x, note_l.act[k].y, note_l.act[k].w, note_l.act[k].h, c.bar_text);
        gfx_text(s, note_l.act[k].x + 8, note_l.act[k].y + 3, f_ui, n->action[k], -1, c.bar_text);
    }
    wm_show(m, w, 1);
    note_win = w->id;
}

/* ---- the desktop's turn, and its input */

/* Each turn of the desktop's thread (screen: the desktop is on the screen): the tooltip, the notification shown.
   The tick the thread must wake at, at the latest (0: whenever). */
u32 deskshell_turn(struct wm *m, int screen)
{
    struct note_news news;
    struct wm_window *w = note_win ? wm_find(m, note_win) : 0;
    u32 now = timer_ticks(), due, tip;
    int full = m->focus && (m->focus->flags & WF_FULL);
    int over = w && m->px >= w->frame.x && m->py >= w->frame.y && m->px < w->frame.x + w->frame.w
               && m->py < w->frame.y + w->frame.h;
    if (look_w != (u32)m->w || look_h != (u32)m->h || look_bpp != m->fmt.bpp || look_theme != (const void *)desk_theme()) {
        look_w = (u32)m->w, look_h = (u32)m->h, look_bpp = m->fmt.bpp, look_theme = desk_theme();
        note_hide(m);                           /* another screen or theme: both drawn again, where they go now */
        tip_hide(m);
        deskpass_turn(m, 1);                    /* (and the secrets' password dialog, if it is up) */
        tip_id = 0;
        w = 0;
    }
    if (note_win && !w)
        note_win = 0;                           /* (its window went with the screen it was on) */
    if (tip_win && !wm_find(m, tip_win))
        tip_win = 0;
    if (notes_turn(&notes, now, screen && !full, over, &news) || (notes_shown(&notes) != 0) != (note_win != 0)
        || (notes_shown(&notes) && w && !(w->flags & WF_SHOWN))) {
        note_hide(m);
        if (notes_shown(&notes))
            note_draw(m, notes_shown(&notes));
    }
    if (news.id)
        api2_note_event(&news);
    tip = screen ? tip_turn(m, now) : 0;
    due = notes_due(&notes);
    if (over && due)
        due = now + 256;                /* (the pointer is on it: looked at again soon) */
    return tip && (!due || (s32)(tip - due) < 0) ? tip : due;
}

/* A button went down on the bar or on a window of the desktop's own: 1 if it was the tray's or a notification's
   (and is used up). */
int deskshell_press(struct wm *m, u32 bit)
{
    struct wm_window *w = note_win ? wm_find(m, note_win) : 0;
    struct note_news news;
    struct gfx_rect r;
    u32 id, now = timer_ticks();
    int i;
    if (w && m->px >= w->frame.x && m->py >= w->frame.y && m->px < w->frame.x + w->frame.w
        && m->py < w->frame.y + w->frame.h) {
        if (bit == 1 && notes_click(&notes, note_part(&note_l, m->px - w->frame.x, m->py - w->frame.y), &news)) {
            note_hide(m);
            api2_note_event(&news);
            desk_kick();                /* (the next one, if one waits) */
        }
        return 1;
    }
    if (!(id = wm_tray_hit(m, m->px, m->py)) || (i = tray_of(id)) < 0)
        return 0;
    tip_hide(m);
    tip_since = now;                    /* (the tooltip not at once after a press) */
    wm_bar_part(m, WB_TRAY, id, &r);
    if (bit == 1) {
        int twice = press_id == id && now - press_at < deskin_double_ticks();
        press_id = twice ? 0 : id;
        press_at = now;
        api2_tray_event(trays[i].a, id, twice ? 2 : 1, &r);
    } else if (bit == 2) {
        api2_tray_event(trays[i].a, id, 3, &r);
    }
    return 1;
}

int desk_point(int what, struct gfx_rect *r)
{
    struct wm *m = desk_lock();
    int ok = display_fb() != 0 && wm_bar_part(m, what, 0, r);
    desk_unlock();
    return ok;
}
