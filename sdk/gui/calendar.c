/* The calendar and the date and time fields (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): Date & Time's
 * and the task bar clock's calendar.
 *
 * A date is one number, yyyymmdd (20261117), so that dates compare as numbers; a time is hhmmss (140500). Dates are
 * the Gregorian calendar's, years 1601 to 9999.
 *
 * The calendar shows a month: arrows to the months before and after, the weekdays from the region's first day
 * (GLOS.CFG's [region] first_day), six weeks of days (the days of the months either side dimmed). Today is framed;
 * the chosen day (or days: GUI_CAL_RANGE, chosen with Shift) is filled; days the app marks are bold. Keys: the
 * arrows a day or a week, Page Up and Page Down a month (with Ctrl a year), Home and End the month's first and last.
 *
 * The date field writes its date as the region says (2026-11-17, 17/11/2026, 11/17/2026) and the time field its
 * time (14:05, 2:05 PM): each part is changed by Up and Down or typed, Left and Right (or the separator) move between
 * parts; the date field's button (or Alt+Down, F4) drops a calendar down. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "guiint.h"

static const char *const month_name[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                            "September", "October", "November", "December" };
static const char *const day_name[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };

#define Y(d) ((d) / 10000)
#define M(d) ((d) / 100 % 100)
#define D(d) ((d) % 100)
#define DATE(y, m, d) ((y) * 10000 + (m) * 100 + (d))

/* ---- the region (how dates and times are written, the week's first day): asked of GLOS once */
static struct glos_region region;
static int region_known;

static const struct glos_region *reg(void)
{
    if (!region_known) {
        region_known = 1;
        if (glos_region(&region) != 0) {        /* (GLOS's defaults: 2026-11-17, 14:05, Monday) */
            memset(&region, 0, sizeof region);
            region.date = GLOS_DATE_YMD, region.date_sep = '-', region.first_day = 1;
        }
    }
    return &region;
}

/* ---- dates */
static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int gui_month_days(int year, int month)
{
    static const unsigned char n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return month < 1 || month > 12 ? 0 : month == 2 && leap(year) ? 29 : n[month - 1];
}

static long day_number(int date)                /* days since 1 January 1601 (a Monday) */
{
    int y = Y(date) - 1601, m;
    long n = 365L * y + y / 4 - y / 100 + y / 400;
    for (m = 1; m < M(date); m++)
        n += gui_month_days(Y(date), m);
    return n + D(date) - 1;
}

static int from_number(long n)
{
    int y = 1601, m = 1, len;
    long c400 = n / 146097;                     /* (whole 400-year cycles first: 146097 days each) */
    y += (int)c400 * 400, n -= c400 * 146097;
    while (n >= (len = 365 + leap(y)))
        n -= len, y++;
    while (n >= (len = gui_month_days(y, m)))
        n -= len, m++;
    return DATE(y, m, (int)n + 1);
}

int gui_weekday(int date) { return (int)((day_number(date) + 1) % 7); }        /* 0 Sunday .. 6 Saturday */

int gui_date_add(int date, int days)
{
    long n = day_number(date) + days;
    if (n < 0)
        n = 0;
    if (n > day_number(DATE(9999, 12, 31)))
        n = day_number(DATE(9999, 12, 31));
    return from_number(n);
}

int gui_date_valid(int date)
{
    return Y(date) >= 1601 && Y(date) <= 9999 && M(date) >= 1 && M(date) <= 12 && D(date) >= 1
           && D(date) <= gui_month_days(Y(date), M(date));
}

static int month_add(int date, int months)     /* (the day kept, or the month's last) */
{
    int y = Y(date), m = M(date) - 1 + months, d = D(date);
    y += m >= 0 ? m / 12 : -((11 - m) / 12);
    m = ((m % 12) + 12) % 12 + 1;
    if (y < 1601)
        return DATE(1601, 1, 1);
    if (y > 9999)
        return DATE(9999, 12, 31);
    return DATE(y, m, d > gui_month_days(y, m) ? gui_month_days(y, m) : d);
}

int gui_today(void)
{
    time_t t = time(NULL);
    struct tm *l = localtime(&t);
    return l ? DATE(l->tm_year + 1900, l->tm_mon + 1, l->tm_mday) : DATE(2026, 1, 1);
}

int gui_date_text(int date, char *out, int room)        /* as the region writes it */
{
    const struct glos_region *r = reg();
    char sep = (char)(r->date_sep ? r->date_sep : '-');
    if (r->date == GLOS_DATE_DMY)
        return snprintf(out, (size_t)room, "%02d%c%02d%c%04d", D(date), sep, M(date), sep, Y(date));
    if (r->date == GLOS_DATE_MDY)
        return snprintf(out, (size_t)room, "%02d%c%02d%c%04d", M(date), sep, D(date), sep, Y(date));
    return snprintf(out, (size_t)room, "%04d%c%02d%c%02d", Y(date), sep, M(date), sep, D(date));
}

/* ---- the calendar */
struct cal {
    struct gui_widget w;
    int from, to;                       /* the days chosen (from == to: one) */
    int shown;                          /* the month shown: its first day */
    int today;
    unsigned flags;
    int (*mark)(void *ctx, int date);
    void *mctx;
    struct gui_widget *field;           /* a date field's drop-down: told of a day chosen */
};

#define CAL(w) ((struct cal *)(w))

static int cell_w(void) { return gfx_text_width(gui_font, "00", -1) + 10; }
static int head_h(void) { return GUI_ROW_H + 6; }

static void cal_measure(struct gui_widget *w, int *pw, int *ph)
{
    (void)w;
    *pw = cell_w() * 7 + 2;
    *ph = head_h() + GUI_ROW_H * 7 + 4;
}

static int first_shown(struct cal *c)           /* the date in the grid's first cell */
{
    int back = (gui_weekday(c->shown) - (int)reg()->first_day + 7) % 7;
    return gui_date_add(c->shown, -(back ? back : 7));         /* (a week of the month before at least) */
}

static void cal_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct cal *c = CAL(w);
    int on = !(w->flags & GUI_DISABLED), cw = cell_w(), x0 = w->r.x + 1, y0 = w->r.y + head_h(), i, d, lo, hi;
    char t[32];
    struct gfx_rect prev = { w->r.x + 2, w->r.y + 2, head_h() - 4, head_h() - 4 };
    struct gfx_rect next = { w->r.x + w->r.w - head_h() + 2, w->r.y + 2, head_h() - 4, head_h() - 4 };
    gui_draw_field(s, &w->r, on);
    gui_draw_box(s, &prev, GUI_FACE, 0);
    gui_draw_box(s, &next, GUI_FACE, 0);
    gui_draw_arrow(s, prev.x + prev.w / 2, prev.y + prev.h / 2, 2, on ? GUI_TEXT : GUI_DIM);
    gui_draw_arrow(s, next.x + next.w / 2, next.y + next.h / 2, 3, on ? GUI_TEXT : GUI_DIM);
    snprintf(t, sizeof t, "%s %d", month_name[M(c->shown) - 1], Y(c->shown));
    gfx_text(s, w->r.x + (w->r.w - gfx_text_width(gui_bold, t, -1)) / 2, w->r.y + (head_h() - gui_bold->height) / 2,
             gui_bold, t, -1, on ? GUI_FIELD_TEXT : GUI_DIM);
    for (i = 0; i < 7; i++) {                   /* the weekdays */
        const char *n = day_name[(i + (int)reg()->first_day) % 7];
        gfx_text(s, x0 + i * cw + (cw - gfx_text_width(gui_font, n, -1)) / 2, y0 + 1, gui_font, n, -1, GUI_DIM);
    }
    gfx_fill(s, x0 + 2, y0 + GUI_ROW_H, w->r.w - 6, 1, GUI_SHADE);
    lo = c->from < c->to ? c->from : c->to, hi = c->from < c->to ? c->to : c->from;
    for (i = 0, d = first_shown(c); i < 42; i++, d = gui_date_add(d, 1)) {
        int x = x0 + i % 7 * cw, y = y0 + GUI_ROW_H + 2 + i / 7 * GUI_ROW_H, chosen = d >= lo && d <= hi;
        int here = M(d) == M(c->shown);
        const struct gfx_font *f = c->mark && c->mark(c->mctx, d) ? gui_bold : gui_font;
        unsigned fg = chosen ? GUI_CHOSEN_TEXT : !on || !here ? GUI_DIM : GUI_FIELD_TEXT;
        if (chosen)
            gfx_fill(s, x + 1, y, cw - 2, GUI_ROW_H, GUI_CHOSEN);
        if (d == c->today)
            gfx_frame(s, x + 1, y, cw - 2, GUI_ROW_H, GUI_MARK);
        snprintf(t, sizeof t, "%d", D(d));
        gfx_text(s, x + cw - 5 - gfx_text_width(f, t, -1), y + 1, f, t, -1, fg);
        if (d == c->to && gui_is_focus(w)) {
            struct gfx_rect r = { x + 1, y, cw - 2, GUI_ROW_H };
            gui_draw_focus(s, &r);
        }
    }
}

static void cal_choose(struct cal *c, int date, int extend)
{
    if (!gui_date_valid(date))
        return;
    if (!(extend && c->flags & GUI_CAL_RANGE))
        c->from = date;
    c->to = date;
    c->shown = DATE(Y(date), M(date), 1);
    gui_damage(&c->w);
    gui_send(&c->w, GUI_ON_CHANGE);
}

static void field_took(struct gui_widget *field, int date);

static int cal_event(struct gui_widget *w, struct gui_ev *e)
{
    struct cal *c = CAL(w);
    int shift = (e->mods & GLOS_MOD_SHIFT) != 0, ctrl = (e->mods & GLOS_MOD_CTRL) != 0;
    switch (e->type) {
    case GUI_EV_KEY:
        if (e->mods & GLOS_MOD_ALT)
            return 0;
        switch (e->key) {
        case GUI_K_LEFT: cal_choose(c, gui_date_add(c->to, -1), shift); return 1;
        case GUI_K_RIGHT: cal_choose(c, gui_date_add(c->to, 1), shift); return 1;
        case GUI_K_UP: cal_choose(c, gui_date_add(c->to, -7), shift); return 1;
        case GUI_K_DOWN: cal_choose(c, gui_date_add(c->to, 7), shift); return 1;
        case GUI_K_PGUP: cal_choose(c, month_add(c->to, ctrl ? -12 : -1), shift); return 1;
        case GUI_K_PGDN: cal_choose(c, month_add(c->to, ctrl ? 12 : 1), shift); return 1;
        case GUI_K_HOME: cal_choose(c, DATE(Y(c->to), M(c->to), 1), shift); return 1;
        case GUI_K_END: cal_choose(c, DATE(Y(c->to), M(c->to), gui_month_days(Y(c->to), M(c->to))), shift); return 1;
        }
        return 0;
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 0;
        if (e->y < w->r.y + head_h()) {         /* the arrows: the month before or after (the day kept) */
            if (e->x < w->r.x + head_h())
                c->shown = month_add(c->shown, -1);
            else if (e->x >= w->r.x + w->r.w - head_h())
                c->shown = month_add(c->shown, 1);
            gui_damage(w);
            return 1;
        }
        {
            int row = (e->y - w->r.y - head_h() - GUI_ROW_H - 2) / GUI_ROW_H, col = (e->x - w->r.x - 1) / cell_w(), d;
            if (e->y < w->r.y + head_h() + GUI_ROW_H + 2 || row > 5 || col < 0 || col > 6)
                return 1;
            d = gui_date_add(first_shown(c), row * 7 + col);
            cal_choose(c, d, shift);
            if (c->field)
                field_took(c->field, d);
            else if (e->clicks == 2)
                gui_send(w, GUI_ON_ACTIVATE);
        }
        return 1;
    case GUI_EV_WHEEL:
        c->shown = month_add(c->shown, e->wheel > 0 ? -1 : 1);
        gui_damage(w);
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    }
    return 0;
}

static const struct gui_class cal_class = { "calendar", GUI_C_FOCUS, cal_draw, cal_event, cal_measure, NULL, NULL };

struct gui_widget *gui_calendar(struct gui_widget *parent, int date)
{
    struct cal *c = (struct cal *)gui_new(parent, &cal_class, sizeof *c);
    c->today = gui_today();
    if (!gui_date_valid(date))
        date = c->today;
    c->from = c->to = date;
    c->shown = DATE(Y(date), M(date), 1);
    return &c->w;
}

void gui_calendar_style(struct gui_widget *w, unsigned flags, int today, int (*mark)(void *ctx, int date), void *ctx)
{
    struct cal *c = CAL(w);
    c->flags = flags;
    c->today = gui_date_valid(today) ? today : gui_today();
    c->mark = mark, c->mctx = ctx;
    gui_damage(w);
}

int gui_calendar_get(struct gui_widget *w, int *to)
{
    struct cal *c = CAL(w);
    int lo = c->from < c->to ? c->from : c->to, hi = c->from < c->to ? c->to : c->from;
    if (to)
        *to = hi;
    return lo;
}

void gui_calendar_set(struct gui_widget *w, int from, int to)
{
    struct cal *c = CAL(w);
    if (!gui_date_valid(from))
        return;
    c->from = from;
    c->to = gui_date_valid(to) && c->flags & GUI_CAL_RANGE ? to : from;
    c->shown = DATE(Y(c->to), M(c->to), 1);
    gui_damage(w);
}

/* ---- the date and time fields: parts, each changed alone */
struct dtf {
    struct gui_widget w;
    int time;                           /* 0: a date; 1: a time */
    int v[4];                           /* the parts' values in the order shown (a time: hours, minutes, seconds,
                                           and 0 AM 1 PM with a twelve-hour clock) */
    int kind[4];                        /* what each part is: 'y' 'm' 'd', 'h' 'n' 's' 'p' */
    int parts, part;                    /* how many; the current one */
    int typed, pend;                    /* digits typed into the part so far, and their number (shown as typed;
                                           the part's value once it is one) */
    int seconds;
    struct gui_window *pop;             /* the date's drop-down calendar */
    struct gui_widget *cal;
};

#define DTF(w) ((struct dtf *)(w))
#define DROP_W (GUI_SB + 1)

static void dtf_layout(struct dtf *f)           /* the parts as the region orders them */
{
    const struct glos_region *r = reg();
    static const int ymd[3] = { 'y', 'm', 'd' }, dmy[3] = { 'd', 'm', 'y' }, mdy[3] = { 'm', 'd', 'y' };
    const int *k = r->date == GLOS_DATE_DMY ? dmy : r->date == GLOS_DATE_MDY ? mdy : ymd;
    int i;
    if (f->time) {
        f->parts = 0;
        f->kind[f->parts++] = 'h', f->kind[f->parts++] = 'n';
        if (f->seconds)
            f->kind[f->parts++] = 's';
        if (r->clock12)
            f->kind[f->parts++] = 'p';
    } else {
        for (i = 0; i < 3; i++)
            f->kind[i] = k[i];
        f->parts = 3;
    }
}

static int part_of(struct dtf *f, int kind)
{
    int i;
    for (i = 0; i < f->parts; i++)
        if (f->kind[i] == kind)
            return i;
    return -1;
}

static int dtf_value(struct dtf *f)             /* yyyymmdd or hhmmss */
{
    if (!f->time)
        return DATE(f->v[part_of(f, 'y')], f->v[part_of(f, 'm')], f->v[part_of(f, 'd')]);
    {
        int h = f->v[part_of(f, 'h')], p = part_of(f, 'p'), s = part_of(f, 's');
        if (p >= 0)
            h = h % 12 + (f->v[p] ? 12 : 0);
        return h * 10000 + f->v[part_of(f, 'n')] * 100 + (s >= 0 ? f->v[s] : 0);
    }
}

static void dtf_put(struct dtf *f, int value)  /* into the parts */
{
    int p;
    if (!f->time) {
        f->v[part_of(f, 'y')] = Y(value), f->v[part_of(f, 'm')] = M(value), f->v[part_of(f, 'd')] = D(value);
        return;
    }
    p = part_of(f, 'p');
    f->v[part_of(f, 'h')] = p >= 0 ? (value / 10000 % 12 ? value / 10000 % 12 : 12) : value / 10000;
    if (p >= 0)
        f->v[p] = value / 10000 >= 12;
    f->v[part_of(f, 'n')] = value / 100 % 100;
    if (part_of(f, 's') >= 0)
        f->v[part_of(f, 's')] = value % 100;
}

static void range_of(struct dtf *f, int i, int *lo, int *hi)
{
    switch (f->kind[i]) {
    case 'y': *lo = 1601, *hi = 9999; break;
    case 'm': *lo = 1, *hi = 12; break;
    case 'd': *lo = 1, *hi = gui_month_days(f->v[part_of(f, 'y')], f->v[part_of(f, 'm')]); break;
    case 'h': *lo = part_of(f, 'p') >= 0 ? 1 : 0, *hi = part_of(f, 'p') >= 0 ? 12 : 23; break;
    case 'p': *lo = 0, *hi = 1; break;
    default: *lo = 0, *hi = 59; break;
    }
}

/* The text and where each part is in it (start, length). */
static void dtf_text(struct dtf *f, char *t, int room, int *at, int *len)
{
    const struct glos_region *r = reg();
    int i, n = 0;
    for (i = 0; i < f->parts && n < room - 8; i++) {
        if (i)
            t[n++] = f->time ? (f->kind[i] == 'p' ? ' ' : ':') : (char)(r->date_sep ? r->date_sep : '-');
        at[i] = n;
        if (i == f->part && f->typed)
            n += snprintf(t + n, (size_t)(room - n), "%d", f->pend);
        else if (f->kind[i] == 'p')
            n += snprintf(t + n, (size_t)(room - n), "%s", f->v[i] ? "PM" : "AM");
        else if (f->kind[i] == 'y')
            n += snprintf(t + n, (size_t)(room - n), "%04d", f->v[i]);
        else if (f->kind[i] == 'h' && part_of(f, 'p') >= 0)
            n += snprintf(t + n, (size_t)(room - n), "%d", f->v[i]);
        else
            n += snprintf(t + n, (size_t)(room - n), "%02d", f->v[i]);
        len[i] = n - at[i];
    }
    t[n] = 0;
}

static void dtf_measure(struct gui_widget *w, int *pw, int *ph)
{
    struct dtf *f = DTF(w);
    *pw = gfx_text_width(gui_font, f->time ? "00:00:00 PM" : "0000-00-00", -1) + 10 + (f->time ? 0 : DROP_W);
    *ph = GUI_FIELD_H;
}

static void dtf_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct dtf *f = DTF(w);
    int on = !(w->flags & GUI_DISABLED), at[4], len[4], x, y = w->r.y + (w->r.h - gui_font->height) / 2;
    char t[32];
    struct gfx_rect box = { w->r.x, w->r.y, w->r.w - (f->time ? 0 : DROP_W - 1), w->r.h };
    gui_draw_field(s, &box, on);
    dtf_text(f, t, sizeof t, at, len);
    x = w->r.x + 4;
    if (gui_is_focus(w)) {                      /* the current part chosen */
        int px = x + gfx_text_width(gui_font, t, at[f->part]), pw = gfx_text_width(gui_font, t + at[f->part], len[f->part]);
        gfx_fill(s, px - 1, y - 1, pw + 2, gui_font->height + 2, GUI_CHOSEN);
        gfx_text(s, x, y, gui_font, t, at[f->part], on ? GUI_FIELD_TEXT : GUI_DIM);
        gfx_text(s, px, y, gui_font, t + at[f->part], len[f->part], GUI_CHOSEN_TEXT);
        gfx_text(s, px + pw, y, gui_font, t + at[f->part] + len[f->part], -1, on ? GUI_FIELD_TEXT : GUI_DIM);
    } else {
        gfx_text(s, x, y, gui_font, t, -1, on ? GUI_FIELD_TEXT : GUI_DIM);
    }
    if (!f->time) {
        struct gfx_rect b = { w->r.x + w->r.w - DROP_W, w->r.y + 1, DROP_W - 1, w->r.h - 2 };
        gui_draw_box(s, &b, GUI_FACE, f->pop != NULL);
        gui_draw_arrow(s, b.x + b.w / 2, b.y + b.h / 2, 1, on ? GUI_TEXT : GUI_DIM);
    }
}

static void dtf_set_part(struct dtf *f, int i, int v)
{
    int lo, hi, before = dtf_value(f);
    range_of(f, i, &lo, &hi);
    v = v < lo ? hi : v > hi ? lo : v;          /* (Up past the top: the bottom) */
    f->v[i] = v;
    if (!f->time) {                             /* (31 January to February: the 28th or 29th) */
        int d = part_of(f, 'd'), dl, dh;
        range_of(f, d, &dl, &dh);
        if (f->v[d] > dh)
            f->v[d] = dh;
    }
    gui_damage(&f->w);
    if (dtf_value(f) != before)
        gui_send(&f->w, GUI_ON_CHANGE);
}

static void drop_close(struct dtf *f)
{
    if (f->pop) {
        struct gui_window *p = f->pop;
        f->pop = NULL;
        CAL(f->cal)->field = NULL;
        f->cal = NULL;
        gui_close_later(p);
        gui_damage(&f->w);
    }
}

static void field_took(struct gui_widget *field, int date)     /* a day chosen in the drop-down */
{
    struct dtf *f = DTF(field);
    int before = dtf_value(f);
    dtf_put(f, date);
    drop_close(f);
    if (dtf_value(f) != before)
        gui_send(field, GUI_ON_CHANGE);
}

static void drop_open(struct dtf *f)
{
    struct gui_widget *w = &f->w;
    int ox, oy, cw, ch;
    if (f->pop || !w->win || gui_origin(w->win, &ox, &oy) != 0)
        return;
    f->pop = gui_window_at("", ox + w->r.x, oy + w->r.y + w->r.h, 10, 10, GUI_W_POPUP | GUI_W_BARE);
    f->pop->owner = w->win;
    f->cal = gui_calendar(f->pop->root, dtf_value(f));
    CAL(f->cal)->field = w;
    cal_measure(f->cal, &cw, &ch);
    gui_resize(f->pop, cw, ch);
    gui_show_nofocus(f->pop, ox + w->r.x, oy + w->r.y + w->r.h);
    gui_damage(w);
}

static int dtf_event(struct gui_widget *w, struct gui_ev *e)
{
    struct dtf *f = DTF(w);
    int plain = !(e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT)), alt = (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT)) == GLOS_MOD_ALT;
    switch (e->type) {
    case GUI_EV_KEY:
        if (!f->time && ((alt && e->key == GUI_K_DOWN) || (plain && e->key == GUI_K_F4))) {
            if (f->pop)
                drop_close(f);
            else
                drop_open(f);
            return 1;
        }
        if (f->pop) {                           /* (dropped: the keys move the calendar's day) */
            if (e->key == GUI_K_ESC) {
                drop_close(f);
                return 1;
            }
            if (e->key == GUI_K_ENTER || e->key == GUI_K_KPENTER) {
                field_took(w, gui_calendar_get(f->cal, NULL));
                return 1;
            }
            return cal_event(f->cal, e);
        }
        if (!plain)
            return 0;
        if (e->key == GUI_K_LEFT || (e->key == GUI_K_RIGHT) || (e->ch && !(e->ch >= '0' && e->ch <= '9')
                                                                 && strchr("/-.: ", (int)e->ch))) {
            int i = f->part + (e->key == GUI_K_LEFT ? -1 : 1);
            if (i < 0 || i >= f->parts)
                return e->key == GUI_K_LEFT || e->key == GUI_K_RIGHT;
            f->part = i, f->typed = 0;
            gui_damage(w);
            return 1;
        }
        if (e->key == GUI_K_UP || e->key == GUI_K_DOWN) {
            f->typed = 0;
            dtf_set_part(f, f->part, f->v[f->part] + (e->key == GUI_K_UP ? 1 : -1));
            return 1;
        }
        if (e->key == GUI_K_HOME || e->key == GUI_K_END) {
            f->part = e->key == GUI_K_HOME ? 0 : f->parts - 1, f->typed = 0;
            gui_damage(w);
            return 1;
        }
        if (f->kind[f->part] == 'p' && (e->ch == 'a' || e->ch == 'A' || e->ch == 'p' || e->ch == 'P')) {
            dtf_set_part(f, f->part, e->ch == 'p' || e->ch == 'P');
            return 1;
        }
        if (e->ch >= '0' && e->ch <= '9' && f->kind[f->part] != 'p') {
            int lo, hi, digits = f->kind[f->part] == 'y' ? 4 : 2, v, dg = (int)(e->ch - '0'), full;
            range_of(f, f->part, &lo, &hi);
            v = f->typed ? f->pend * 10 + dg : dg;
            if (v > hi)                         /* (too big with this digit: the digit begins it anew) */
                v = dg, f->typed = 0;
            f->typed++, f->pend = v;
            full = f->typed >= digits || v * 10 > hi;           /* (it can take no more digits) */
            gui_damage(w);
            if (f->kind[f->part] != 'y' ? v >= lo : full) {    /* (a year once its four digits are in) */
                int before = dtf_value(f), dl, dh;
                f->v[f->part] = v < lo ? lo : v;
                if (!f->time && f->v[part_of(f, 'd')] > (range_of(f, part_of(f, 'd'), &dl, &dh), dh))
                    f->v[part_of(f, 'd')] = dh;
                if (dtf_value(f) != before)
                    gui_send(w, GUI_ON_CHANGE);
            }
            if (full) {                         /* the next part */
                if (f->part + 1 < f->parts)
                    f->part++;
                f->typed = 0;
            }
            return 1;
        }
        return 0;
    case GUI_EV_PRESS:
        if (e->button != 1)
            return 0;
        if (!f->time && e->x >= w->r.x + w->r.w - DROP_W) {
            if (f->pop)
                drop_close(f);
            else
                drop_open(f);
            return 1;
        }
        {                                       /* the part pressed */
            int at[4], len[4], i;
            char t[32];
            dtf_text(f, t, sizeof t, at, len);
            for (i = f->parts - 1; i > 0 && e->x < w->r.x + 4 + gfx_text_width(gui_font, t, at[i]); i--)
                ;
            f->part = i, f->typed = 0;
            gui_damage(w);
        }
        return 1;
    case GUI_EV_WHEEL:
        dtf_set_part(f, f->part, f->v[f->part] + (e->wheel > 0 ? 1 : -1));
        return 1;
    case GUI_EV_BLUR:
        drop_close(f);
        f->typed = 0;                           /* (digits that made no value yet: forgotten) */
        gui_damage(w);
        return 1;
    case GUI_EV_FOCUS:
        gui_damage(w);
        return 1;
    }
    return 0;
}

static void dtf_destroy(struct gui_widget *w) { drop_close(DTF(w)); }

static const struct gui_class dtf_class = { "datetime", GUI_C_FOCUS, dtf_draw, dtf_event, dtf_measure, NULL,
                                            dtf_destroy };

struct gui_widget *gui_date_field(struct gui_widget *parent, int date)
{
    struct dtf *f = (struct dtf *)gui_new(parent, &dtf_class, sizeof *f);
    dtf_layout(f);
    dtf_put(f, gui_date_valid(date) ? date : gui_today());
    return &f->w;
}

struct gui_widget *gui_time_field(struct gui_widget *parent, int hms, int seconds)
{
    struct dtf *f = (struct dtf *)gui_new(parent, &dtf_class, sizeof *f);
    f->time = 1, f->seconds = seconds != 0;
    dtf_layout(f);
    dtf_put(f, hms >= 0 && hms < 240000 ? hms : 0);
    return &f->w;
}

int gui_datetime_get(struct gui_widget *w) { return dtf_value(DTF(w)); }

void gui_datetime_set(struct gui_widget *w, int value)
{
    struct dtf *f = DTF(w);
    if (f->time ? value < 0 || value >= 240000 : !gui_date_valid(value))
        return;
    dtf_put(f, value);
    f->typed = 0;
    gui_damage(w);
}
