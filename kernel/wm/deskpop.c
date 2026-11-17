/* The desktop's own pop-up menus (phase D, docs/v1/phase-d.md D1): a task bar button's window menu (its right
 * click, or Alt+Space for the window in front) and a group's windows (a grouped button's press). Drawn by the
 * kernel in the task bar's colours, so that they answer whatever the apps are doing; popmenu.c keeps the items,
 * the current one and what a key or a press chooses.
 *
 * One at a time. It has the keyboard while it is up (the window that had it gets it back when it goes without a
 * choice); a press outside it takes it away, and the press goes on to what it was on. */
#include <string.h>

#include "app.h"
#include "deskint.h"
#include "gfx.h"
#include "kprintf.h"
#include "popmenu.h"
#include "theme.h"

enum { CMD_RESTORE = 1, CMD_MIN, CMD_MAX, CMD_CLOSE };

static struct {
    u32 win;                            /* the pop-up's window, 0: none */
    int kind;                           /* DP_ */
    u32 target;                         /* DP_WINDOW: the window it is for */
    void (*chosen)(struct wm *m, u32 data);     /* DP_CUSTOM: told the item chosen's data */
    u32 back;                           /* the window that had the focus before it */
    struct popmenu pm;
} pop;

static int cal_key(struct wm *m, u32 key, int down);
static int cal_press(struct wm *m, u32 bit);
static void cal_close(struct wm *m, int give_back);
static u32 cal_win_id(void);
static int vol_key(struct wm *m, u32 key, int down, u32 ch);
static int vol_press(struct wm *m, u32 bit);
static void vol_move(struct wm *m, u32 buttons);
static void vol_close(struct wm *m, int give_back);
static u32 vol_win_id(void);

static int width(const char *t, int len, int bold)
{
    return gfx_text_width(bold ? &gfx_font_bold : &gfx_font_ui, t, len);
}

/* A text with "&" before its letter: drawn without it, the letter underlined ("&&" is one "&"). */
static void text_amp(struct gfx_surface *s, s32 x, s32 y, const char *t, u32 ink)
{
    char out[PM_TEXT];
    s32 ux = -1, uw = 0;
    u32 n = 0;
    for (; *t && n < sizeof out - 1; t++) {
        if (*t == '&' && t[1] && t[1] != '&') {
            ux = width(out, (int)n, 0);
            uw = width(t + 1, 1, 0);
            continue;
        }
        if (*t == '&')
            t++;
        out[n++] = *t;
    }
    out[n] = 0;
    gfx_text(s, x, y, &gfx_font_ui, out, (int)n, ink);
    if (ux >= 0)
        gfx_fill(s, x + ux, y + gfx_font_ui.height, uw, 1, ink);
}

static void draw(struct wm *m)
{
    struct wm_window *w = pop.win ? wm_find(m, pop.win) : 0;
    struct theme_look c;
    struct gfx_surface *s;
    u32 i;
    if (!w)
        return;
    theme_look(desk_theme(), &c);
    s = &w->store;
    gfx_fill(s, 0, 0, pop.pm.w, pop.pm.h, c.edge);
    gfx_fill(s, 1, 1, pop.pm.w - 2, pop.pm.h - 2, c.bar);
    for (i = 0; i < pop.pm.n; i++) {
        const struct pm_item *it = &pop.pm.it[i];
        s32 y = pm_item_y(&pop.pm, i);
        u32 ink = it->flags & PMI_OFF ? c.face_off : c.bar_text;
        if (it->flags & PMI_TEXT) {             /* (a line of words: from the menu's left edge) */
            text_amp(s, pop.pm.pad + 6, y + 3, it->text, ink);
            continue;
        }
        if (it->flags & PMI_SEP) {
            gfx_fill(s, pop.pm.pad + 4, y + pop.pm.sep_h / 2, pop.pm.w - 2 * pop.pm.pad - 8, 1, c.face_off);
            continue;
        }
        if ((s32)i == pop.pm.cur) {
            gfx_fill(s, pop.pm.pad, y, pop.pm.w - 2 * pop.pm.pad, pop.pm.item_h, c.bar_on);
            ink = c.bar_on_text;
        }
        if (it->flags & PMI_CHECK)
            gfx_text(s, pop.pm.pad + 6, y + 3, &gfx_font_bold, "\xE2\x80\xA2", -1, ink);       /* (a bullet) */
        text_amp(s, pop.pm.pad + 20, y + 3, it->text, ink);
    }
    wm_damage(m, w, 0);
}

static void close_pop(struct wm *m, int give_back)
{
    struct wm_window *w = pop.win ? wm_find(m, pop.win) : 0, *b;
    if (w)
        wm_destroy(m, w);
    pop.win = 0;
    pop.kind = DP_NONE;
    if (give_back && (b = pop.back ? wm_find(m, pop.back) : 0) != 0 && (b->flags & WF_SHOWN))
        wm_set_focus(m, b);
    else
        desk_refocus(m);
}

int deskpop_up(void) { return pop.win != 0; }

/* The pop-up of that kind, its items made here, with its bottom left corner (above) or its top left at x, y
   (moved to stay on the screen). */
void deskpop_open(struct wm *m, int kind, u32 target, s32 x, s32 y, int above)
{
    struct wm_window *t = wm_find(m, target), *w, *v;
    if (pop.win)
        close_pop(m, 1);
    if (!t)
        return;
    pm_clear(&pop.pm);
    if (kind == DP_WINDOW) {
        int st = wm_state_of(t);
        pm_add(&pop.pm, "&Restore", st == WS_NORMAL ? PMI_OFF : 0, CMD_RESTORE);
        pm_add(&pop.pm, "Mi&nimise", st == WS_MIN || !wm_can(t, WH_MIN) ? PMI_OFF : 0, CMD_MIN);
        pm_add(&pop.pm, "Ma&ximise", st == WS_MAX || !wm_can(t, WH_MAX) ? PMI_OFF : 0, CMD_MAX);
        pm_add(&pop.pm, "", PMI_SEP, 0);
        pm_add(&pop.pm, "&Close", 0, CMD_CLOSE);
    } else {                                    /* DP_GROUP: target's owner's windows on the bar, in their order */
        for (v = m->bottom; v; v = v->above)
            if (v->owner == t->owner && wm_on_bar(v))
                pm_add(&pop.pm, v->title[0] ? v->title : "(no title)", v == m->focus ? PMI_CHECK : 0, v->id);
    }
    pm_layout(&pop.pm, width, gfx_font_ui.height, 120);
    pm_first(&pop.pm);
    if (x + pop.pm.w > m->w)
        x = m->w - pop.pm.w;
    if (x < 0)
        x = 0;
    if (above)
        y -= pop.pm.h;
    if (y + pop.pm.h > m->h)
        y = m->h - pop.pm.h;
    if (y < 0)
        y = 0;
    if (!(w = wm_create(m, x, y, pop.pm.w, pop.pm.h, "", WF_TOPMOST | WF_NOBAR | WF_FIXED, 0)))
        return;
    pop.win = w->id;
    pop.kind = kind;
    pop.target = target;
    pop.back = m->focus ? m->focus->id : 0;
    draw(m);
    wm_show(m, w, 1);
    wm_set_focus(m, w);
}

/* A menu of the desktop's own making (the tray's icons': desktray.c): deskpop_begin gives the menu to fill
   (pm_add), deskpop_show puts it up at x, y (as deskpop_open) and `chosen` is told the item chosen's data. */
struct popmenu *deskpop_begin(struct wm *m)
{
    if (pop.win)
        close_pop(m, 1);
    pm_clear(&pop.pm);
    return &pop.pm;
}

void deskpop_show(struct wm *m, s32 x, s32 y, int above, void (*chosen)(struct wm *m, u32 data))
{
    struct wm_window *w;
    if (!pop.pm.n)
        return;
    pm_layout(&pop.pm, width, gfx_font_ui.height, 120);
    pm_first(&pop.pm);
    if (x + pop.pm.w > m->w)
        x = m->w - pop.pm.w;
    if (x < 0)
        x = 0;
    if (above)
        y -= pop.pm.h;
    if (y + pop.pm.h > m->h)
        y = m->h - pop.pm.h;
    if (y < 0)
        y = 0;
    if (!(w = wm_create(m, x, y, pop.pm.w, pop.pm.h, "", WF_TOPMOST | WF_NOBAR | WF_FIXED, 0)))
        return;
    pop.win = w->id;
    pop.kind = DP_CUSTOM;
    pop.target = 0;
    pop.chosen = chosen;
    pop.back = m->focus ? m->focus->id : 0;
    draw(m);
    wm_show(m, w, 1);
    wm_set_focus(m, w);
}

/* The item chosen: done. */
static void choose(struct wm *m, int i)
{
    struct wm_window *t = wm_find(m, pop.target), *v;
    int kind = pop.kind;
    u32 data = pop.pm.it[i].data;
    void (*chosen)(struct wm *m, u32 data) = pop.chosen;
    close_pop(m, kind == DP_CUSTOM);
    if (kind == DP_WINDOW && t) {
        switch (data) {
        case CMD_RESTORE:
            deskapp_state(m, t, wm_state_of(t) == WS_MIN ? WS_UNMIN : WS_NORMAL);
            wm_set_focus(m, t);
            break;
        case CMD_MIN:
            deskapp_state(m, t, WS_MIN);
            desk_refocus(m);
            break;
        case CMD_MAX:
            if (wm_state_of(t) == WS_MIN)
                deskapp_state(m, t, WS_UNMIN);
            deskapp_state(m, t, WS_MAX);
            wm_set_focus(m, t);
            break;
        case CMD_CLOSE:
            deskapp_close(m, t);
            break;
        }
    } else if (kind == DP_GROUP && (v = wm_find(m, data)) != 0) {
        if (wm_state_of(v) == WS_MIN)
            deskapp_state(m, v, WS_UNMIN);
        wm_set_focus(m, v);
    } else if (kind == DP_CUSTOM && chosen) {
        chosen(m, data);
    }
    deskin_focus(m);
}

int deskpop_key(struct wm *m, u32 key, int down, u32 ch)
{
    int r;
    if (cal_key(m, key, down) || vol_key(m, key, down, ch))
        return 1;
    if (!pop.win || !m->focus || m->focus->id != pop.win)
        return 0;
    if (!down)
        return 1;
    r = pm_key(&pop.pm, key, ch);
    if (r == PM_CANCEL)
        close_pop(m, 1);
    else if (r >= 0)
        choose(m, r);
    else
        draw(m);
    return 1;
}

int deskpop_press(struct wm *m, u32 bit)
{
    struct wm_window *w = pop.win ? wm_find(m, pop.win) : 0;
    int i;
    if (cal_press(m, bit) || vol_press(m, bit))
        return 1;
    if (!w)
        return 0;
    if (m->px < w->frame.x || m->py < w->frame.y || m->px >= w->frame.x + w->frame.w || m->py >= w->frame.y + w->frame.h) {
        close_pop(m, 1);                        /* outside: it goes, and the press goes on */
        return 0;
    }
    if (bit == 1 && (i = pm_at(&pop.pm, m->px - w->frame.x, m->py - w->frame.y)) >= 0)
        choose(m, i);
    return 1;
}

void deskpop_move(struct wm *m, u32 buttons)
{
    struct wm_window *w = pop.win ? wm_find(m, pop.win) : 0;
    int i;
    vol_move(m, buttons);
    if (!w || m->px < w->frame.x || m->py < w->frame.y || m->px >= w->frame.x + w->frame.w || m->py >= w->frame.y + w->frame.h)
        return;
    if ((i = pm_at(&pop.pm, m->px - w->frame.x, m->py - w->frame.y)) >= 0 && i != pop.pm.cur) {
        pop.pm.cur = i;
        draw(m);
    }
}

void deskpop_turn(struct wm *m, int again)
{
    if (cal_win_id() && (again || !wm_find(m, cal_win_id())))
        cal_close(m, 1);
    if (vol_win_id() && (again || !wm_find(m, vol_win_id())))
        vol_close(m, 1);
    if (pop.win && (again || !wm_find(m, pop.win) || (pop.kind != DP_CUSTOM && !wm_find(m, pop.target))))
        close_pop(m, 1);                        /* (another screen or theme, or its window went: it goes) */
}

/* ---- Alt+Tab's strip: every window that can have the focus, the one in front first, each its icon (16 pixels
   drawn twice the size: an app can't give one of 32 yet) in a row over the screen's middle, the current one's
   title under them. Tab and Shift+Tab move along it while Alt is held, Esc gives up, letting go of Alt switches. */

#define SW_CELL 64                      /* a window's place in the strip */
#define SW_MAX  24

static struct {
    u32 win;                            /* the strip's window, 0: none */
    u32 ids[SW_MAX], n;
    s32 cur, cols;
} sw;

static int switchable(const struct wm_window *v)
{
    return ((v->flags & WF_SHOWN) || (v->flags & WF_MIN)) && !(v->flags & (WF_NOFOCUS | WF_EMBED | WF_POPUP))
           && (v->flags & WF_FRAME);
}

static void sw_icon(struct gfx_surface *s, s32 x, s32 y, const struct wm_window *v, u32 ink)
{
    s32 i, j;
    if (!v->has_icon) {                         /* (no picture: a window's outline) */
        gfx_frame(s, x + 2, y + 4, 28, 24, ink);
        gfx_fill(s, x + 2, y + 4, 28, 5, ink);
        return;
    }
    for (j = 0; j < 16; j++)
        for (i = 0; i < 16; i++)
            if (v->icon[j * 16 + i] >> 24 >= 0x80)
                gfx_fill(s, x + 2 * i, y + 2 * j, 2, 2, v->icon[j * 16 + i] & 0xFFFFFF);
}

static void sw_draw(struct wm *m)
{
    struct wm_window *w = sw.win ? wm_find(m, sw.win) : 0, *v;
    struct theme_look c;
    struct gfx_surface *s;
    u32 i;
    if (!w)
        return;
    theme_look(desk_theme(), &c);
    s = &w->store;
    gfx_fill(s, 0, 0, w->frame.w, w->frame.h, c.edge);
    gfx_fill(s, 1, 1, w->frame.w - 2, w->frame.h - 2, c.bar);
    for (i = 0; i < sw.n; i++) {
        s32 x = 8 + (s32)(i % (u32)sw.cols) * SW_CELL, y = 8 + (s32)(i / (u32)sw.cols) * SW_CELL;
        if (!(v = wm_find(m, sw.ids[i])))
            continue;
        if ((s32)i == sw.cur)
            gfx_fill(s, x + 2, y + 2, SW_CELL - 4, SW_CELL - 4, c.bar_on);
        sw_icon(s, x + (SW_CELL - 32) / 2, y + (SW_CELL - 32) / 2, v, (s32)i == sw.cur ? c.bar_on_text : c.bar_text);
    }
    if (sw.cur >= 0 && (v = wm_find(m, sw.ids[sw.cur])) != 0) {
        s32 rows = ((s32)sw.n + sw.cols - 1) / sw.cols, ty = 8 + rows * SW_CELL + 4;
        struct gfx_rect clip = { 8, ty, w->frame.w - 16, gfx_font_ui.height + 2 };
        s32 tw = gfx_text_width(&gfx_font_bold, v->title, -1);
        gfx_clip(s, &clip);
        gfx_text(s, tw < clip.w ? (w->frame.w - tw) / 2 : 8, ty, &gfx_font_bold, v->title[0] ? v->title : "(no title)", -1,
                 c.bar_text);
        gfx_clip(s, 0);
    }
    wm_damage(m, w, 0);
}

static void sw_close(struct wm *m)
{
    struct wm_window *w = sw.win ? wm_find(m, sw.win) : 0;
    if (w)
        wm_destroy(m, w);
    sw.win = 0;
    sw.n = 0;
}

int deskswitch_up(void) { return sw.win != 0; }

/* Alt+Tab (back: with Shift): the strip shown, or its current window moved along. */
void deskswitch_step(struct wm *m, int back)
{
    struct wm_window *v, *top, *w;
    s32 rows, ww, wh;
    if (sw.win) {
        sw.cur = (sw.cur + (back ? -1 : 1) + (s32)sw.n) % (s32)sw.n;
        sw_draw(m);
        return;
    }
    if (pop.win)
        close_pop(m, 1);
    for (top = m->bottom; top && top->above; top = top->above) ;
    sw.n = 0;
    if (m->focus && switchable(m->focus))      /* the one in front first, then the rest from the top down */
        sw.ids[sw.n++] = m->focus->id;
    for (v = top; v && sw.n < SW_MAX; v = v->below)
        if (switchable(v) && v != m->focus && !(v->flags & WF_MIN))
            sw.ids[sw.n++] = v->id;
    for (v = top; v && sw.n < SW_MAX; v = v->below)    /* (minimised ones last) */
        if (switchable(v) && v != m->focus && (v->flags & WF_MIN))
            sw.ids[sw.n++] = v->id;
    if (sw.n < 2) {                             /* (nothing to switch to) */
        sw.n = 0;
        return;
    }
    sw.cols = (s32)sw.n;
    if (sw.cols * SW_CELL + 16 > m->w - 32)
        sw.cols = (m->w - 48) / SW_CELL;
    rows = ((s32)sw.n + sw.cols - 1) / sw.cols;
    ww = sw.cols * SW_CELL + 16;
    wh = rows * SW_CELL + 16 + gfx_font_ui.height + 6;
    if (!(w = wm_create(m, (m->w - ww) / 2, (m->h - wh) / 2, ww, wh, "", WF_TOPMOST | WF_NOFOCUS | WF_NOBAR, 0)))
        return;
    sw.win = w->id;
    sw.cur = back ? (s32)sw.n - 1 : 1;
    sw_draw(m);
    wm_show(m, w, 1);
}

/* Alt let go (go 1) or Esc (go 0): the strip goes, and (go) its current window has the focus. */
void deskswitch_end(struct wm *m, int go)
{
    struct wm_window *v = sw.win && sw.cur >= 0 ? wm_find(m, sw.ids[sw.cur]) : 0;
    if (!sw.win)
        return;
    sw_close(m);
    if (go && v) {
        if (wm_state_of(v) == WS_MIN)
            deskapp_state(m, v, WS_UNMIN);
        wm_set_focus(m, v);
    }
}

/* ---- the clock's calendar: a month, its weeks from the region's first day, today framed. PgUp and PgDn (or Left
   and Right, or a press on the header's arrows) go a month back or on; Esc, or a press outside it, closes it. */

#define CAL_W  22                       /* a day's place */
#define CAL_H  16
#define CAL_HEAD 20

static const char *const months[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                        "September", "October", "November", "December" };
static const char *const wdays[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };

static struct {
    u32 win, back;
    int y, m;                           /* the month shown */
    int ty, tm, td;                     /* today */
    int first;                          /* the week's first day: 0 Sunday, 1 Monday */
} cal;

static int mdays(int y, int m)
{
    static const u8 d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) ? 29 : d[m - 1];
}

static int wday(int y, int m, int d)            /* 0 Sunday (Sakamoto's) */
{
    static const u8 t[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if (m < 3)
        y--;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

static s32 cal_w(void) { return 7 * CAL_W + 8; }
static s32 cal_h(void) { return CAL_HEAD + CAL_H + 6 * CAL_H + 8; }

static void cal_draw(struct wm *m)
{
    struct wm_window *w = cal.win ? wm_find(m, cal.win) : 0;
    struct theme_look c;
    struct gfx_surface *s;
    char t[24];
    int i, d, col, row, lead;
    s32 tw;
    if (!w)
        return;
    theme_look(desk_theme(), &c);
    s = &w->store;
    gfx_fill(s, 0, 0, cal_w(), cal_h(), c.edge);
    gfx_fill(s, 1, 1, cal_w() - 2, cal_h() - 2, c.bar);
    ksnprintf(t, sizeof t, "%s %d", months[cal.m - 1], cal.y);
    tw = gfx_text_width(&gfx_font_bold, t, -1);
    gfx_text(s, (cal_w() - tw) / 2, 4, &gfx_font_bold, t, -1, c.bar_text);
    gfx_text(s, 8, 4, &gfx_font_bold, "<", -1, c.bar_text);
    gfx_text(s, cal_w() - 14, 4, &gfx_font_bold, ">", -1, c.bar_text);
    for (i = 0; i < 7; i++)
        gfx_text(s, 4 + i * CAL_W + 3, CAL_HEAD + 1, &gfx_font_ui, wdays[(i + cal.first) % 7], -1, c.face_off);
    lead = (wday(cal.y, cal.m, 1) - cal.first + 7) % 7;
    for (d = 1; d <= mdays(cal.y, cal.m); d++) {
        s32 x, y;
        col = (lead + d - 1) % 7, row = (lead + d - 1) / 7;
        x = 4 + col * CAL_W, y = CAL_HEAD + CAL_H + row * CAL_H;
        ksnprintf(t, sizeof t, "%d", d);
        tw = gfx_text_width(&gfx_font_ui, t, -1);
        if (cal.y == cal.ty && cal.m == cal.tm && d == cal.td) {
            gfx_fill(s, x + 1, y, CAL_W - 2, CAL_H, c.bar_on);
            gfx_text(s, x + CAL_W - 4 - tw, y + 1, &gfx_font_bold, t, -1, c.bar_on_text);
        } else {
            gfx_text(s, x + CAL_W - 4 - tw, y + 1, &gfx_font_ui, t, -1, c.bar_text);
        }
    }
    wm_damage(m, w, 0);
}

static void cal_step(struct wm *m, int by)
{
    cal.m += by;
    if (cal.m < 1)
        cal.m = 12, cal.y--;
    if (cal.m > 12)
        cal.m = 1, cal.y++;
    cal_draw(m);
}

static void cal_close(struct wm *m, int give_back)
{
    struct wm_window *w = cal.win ? wm_find(m, cal.win) : 0, *b;
    if (w)
        wm_destroy(m, w);
    cal.win = 0;
    if (give_back && (b = cal.back ? wm_find(m, cal.back) : 0) != 0 && (b->flags & WF_SHOWN))
        wm_set_focus(m, b);
    else
        desk_refocus(m);
}

int deskcal_up(void) { return cal.win != 0; }

void deskcal_open(struct wm *m, s32 right, s32 edge, int below, int year, int month, int day, int first_day)
{
    struct wm_window *w;
    s32 x = right - cal_w(), y = below ? edge : edge - cal_h();
    if (pop.win)
        close_pop(m, 1);
    if (cal.win)
        cal_close(m, 1);
    cal.y = cal.ty = year, cal.m = cal.tm = month, cal.td = day, cal.first = first_day ? 1 : 0;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (!(w = wm_create(m, x, y, cal_w(), cal_h(), "", WF_TOPMOST | WF_NOBAR | WF_FIXED, 0)))
        return;
    cal.win = w->id;
    cal.back = m->focus ? m->focus->id : 0;
    cal_draw(m);
    wm_show(m, w, 1);
    wm_set_focus(m, w);
}

/* (deskpop_key, _press and _turn ask these first) */
static int cal_key(struct wm *m, u32 key, int down)
{
    if (!cal.win || !m->focus || m->focus->id != cal.win)
        return 0;
    if (!down)
        return 1;
    if (key == 0x01)
        cal_close(m, 1);
    else if (key == 0x49 || key == 0x149 || key == 0x4B || key == 0x14B)
        cal_step(m, -1);
    else if (key == 0x51 || key == 0x151 || key == 0x4D || key == 0x14D)
        cal_step(m, 1);
    return 1;
}

static int cal_press(struct wm *m, u32 bit)
{
    struct wm_window *w = cal.win ? wm_find(m, cal.win) : 0;
    s32 x, y;
    if (!w)
        return 0;
    x = m->px - w->frame.x, y = m->py - w->frame.y;
    if (x < 0 || y < 0 || x >= w->frame.w || y >= w->frame.h) {
        struct gfx_rect r;
        if (wm_bar_part(m, WB_CLOCK, 0, &r) && m->px >= r.x && m->py >= r.y && m->px < r.x + r.w && m->py < r.y + r.h)
            return 0;                           /* (the clock's own press closes it, or opens the Control Panel) */
        cal_close(m, 1);                        /* outside: it goes, and the press goes on */
        return 0;
    }
    if (bit == 1 && y < CAL_HEAD && (x < 24 || x >= cal_w() - 24))
        cal_step(m, x < 24 ? -1 : 1);
    return 1;
}

static u32 cal_win_id(void) { return cal.win; }

void deskcal_close(struct wm *m) { if (cal.win) cal_close(m, 1); }

/* ---- the volume icon's slider (phase D, D2): upright, the percentage over it, a mute box under it. A press or a
   drag on its track sets the volume; Up and Down a step, PgUp and PgDn a big one, M mutes; Esc, or a press
   outside it, closes it (a press on the icon it belongs to is left to the icon). Whose volume it is are the
   functions given when it opens (desktray.c: the mixer's master). */

#define VOL_W   64
#define VOL_TOP 24                      /* the track's top */
#define VOL_LEN 100
#define VOL_H   (VOL_TOP + VOL_LEN + 34)

static struct {
    u32 win, back;
    void (*get)(u32 *v, int *mute);     /* 0 to 256 */
    void (*set)(u32 v, int mute);
    void (*done)(void);                 /* closed: kept */
    struct gfx_rect icon;               /* the icon's place: a press there is the icon's */
    u8 hold;                            /* the left button went down on the track: it follows the pointer */
} vol;

static void vol_draw(struct wm *m)
{
    struct wm_window *w = vol.win ? wm_find(m, vol.win) : 0;
    struct theme_look c;
    struct gfx_surface *s;
    char t[8];
    u32 v;
    int mute;
    s32 tw, ky;
    if (!w)
        return;
    vol.get(&v, &mute);
    theme_look(desk_theme(), &c);
    s = &w->store;
    gfx_fill(s, 0, 0, VOL_W, VOL_H, c.edge);
    gfx_fill(s, 1, 1, VOL_W - 2, VOL_H - 2, c.bar);
    ksnprintf(t, sizeof t, "%u%%", (v * 100 + 128) / 256);
    tw = gfx_text_width(&gfx_font_bold, t, -1);
    gfx_text(s, (VOL_W - tw) / 2, 5, &gfx_font_bold, t, -1, mute ? c.face_off : c.bar_text);
    gfx_fill(s, VOL_W / 2 - 1, VOL_TOP, 2, VOL_LEN, c.face_off);
    ky = VOL_TOP + VOL_LEN - (s32)(v * VOL_LEN / 256);
    gfx_fill(s, VOL_W / 2 - 1, ky, 2, VOL_TOP + VOL_LEN - ky, mute ? c.face_off : c.bar_on);
    gfx_fill(s, VOL_W / 2 - 9, ky - 3, 18, 6, c.bar_text);
    gfx_frame(s, 10, VOL_TOP + VOL_LEN + 12, 11, 11, c.bar_text);
    if (mute)
        gfx_fill(s, 13, VOL_TOP + VOL_LEN + 15, 5, 5, c.bar_text);
    text_amp(s, 26, VOL_TOP + VOL_LEN + 11, "&Mute", c.bar_text);
    wm_damage(m, w, 0);
}

static void vol_close(struct wm *m, int give_back)
{
    struct wm_window *w = vol.win ? wm_find(m, vol.win) : 0, *b;
    if (w)
        wm_destroy(m, w);
    if (vol.win && vol.done)
        vol.done();
    vol.win = 0;
    vol.hold = 0;
    if (give_back && (b = vol.back ? wm_find(m, vol.back) : 0) != 0 && (b->flags & WF_SHOWN))
        wm_set_focus(m, b);
    else
        desk_refocus(m);
}

int deskvol_up(void) { return vol.win != 0; }
void deskvol_close(struct wm *m) { if (vol.win) vol_close(m, 1); }
void deskvol_again(struct wm *m) { vol_draw(m); }

void deskvol_open(struct wm *m, const struct gfx_rect *icon, s32 edge, int below, void (*get)(u32 *v, int *mute),
                  void (*set)(u32 v, int mute), void (*done)(void))
{
    struct wm_window *w;
    s32 x = icon->x + icon->w / 2 - VOL_W / 2, y = below ? edge : edge - VOL_H;
    if (pop.win)
        close_pop(m, 1);
    if (cal.win)
        cal_close(m, 1);
    if (vol.win)
        vol_close(m, 1);
    if (x + VOL_W > m->w)
        x = m->w - VOL_W;
    if (!(w = wm_create(m, x < 0 ? 0 : x, y < 0 ? 0 : y, VOL_W, VOL_H, "", WF_TOPMOST | WF_NOBAR | WF_FIXED, 0)))
        return;
    vol.win = w->id;
    vol.back = m->focus ? m->focus->id : 0;
    vol.get = get, vol.set = set, vol.done = done;
    vol.icon = *icon;
    vol.hold = 0;
    vol_draw(m);
    wm_show(m, w, 1);
    wm_set_focus(m, w);
}

static void vol_to(struct wm *m, s32 y)         /* the volume the track has at y (in the pop-up) */
{
    u32 v;
    int mute;
    s32 n = (VOL_TOP + VOL_LEN - y) * 256 / VOL_LEN;
    vol.get(&v, &mute);
    vol.set((u32)(n < 0 ? 0 : n > 256 ? 256 : n), mute);
    vol_draw(m);
}

static void vol_step(struct wm *m, int by, int toggle_mute)
{
    u32 v;
    int mute;
    s32 n;
    vol.get(&v, &mute);
    n = (s32)v + by;
    vol.set((u32)(n < 0 ? 0 : n > 256 ? 256 : n), toggle_mute ? !mute : mute);
    vol_draw(m);
}

static int vol_key(struct wm *m, u32 key, int down, u32 ch)
{
    if (!vol.win || !m->focus || m->focus->id != vol.win)
        return 0;
    if (!down)
        return 1;
    if (key == 0x01)
        vol_close(m, 1);
    else if (key == 0x48 || key == 0x148)
        vol_step(m, 8, 0);
    else if (key == 0x50 || key == 0x150)
        vol_step(m, -8, 0);
    else if (key == 0x49 || key == 0x149)
        vol_step(m, 32, 0);
    else if (key == 0x51 || key == 0x151)
        vol_step(m, -32, 0);
    else if (ch == 'm' || ch == 'M')
        vol_step(m, 0, 1);
    return 1;
}

static int vol_press(struct wm *m, u32 bit)
{
    struct wm_window *w = vol.win ? wm_find(m, vol.win) : 0;
    s32 x, y;
    if (!w)
        return 0;
    x = m->px - w->frame.x, y = m->py - w->frame.y;
    if (x < 0 || y < 0 || x >= VOL_W || y >= VOL_H) {
        if (m->px >= vol.icon.x - 2 && m->px < vol.icon.x + vol.icon.w + 2 && m->py >= vol.icon.y - 2
            && m->py < vol.icon.y + vol.icon.h + 2)
            return 0;                           /* (the icon's own press closes it, or opens the Sound page) */
        vol_close(m, 1);                        /* outside: it goes, and the press goes on */
        return 0;
    }
    if (bit != 1)
        return 1;
    if (y >= VOL_TOP + VOL_LEN + 8) {
        vol_step(m, 0, 1);                      /* the mute box, or its word */
    } else if (y >= VOL_TOP - 6) {
        vol.hold = 1;
        vol_to(m, y);
    }
    return 1;
}

static void vol_move(struct wm *m, u32 buttons)
{
    struct wm_window *w = vol.win ? wm_find(m, vol.win) : 0;
    if (!w || !vol.hold)
        return;
    if (!(buttons & 1)) {
        vol.hold = 0;
        return;
    }
    vol_to(m, m->py - w->frame.y);
}

static u32 vol_win_id(void) { return vol.win; }
