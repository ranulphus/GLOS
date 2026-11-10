/* APITEST: API 2.0's conformance suite (docs/v1/phase-b.md: grown a step at
 * a time), on libglos 2.0. Run as GLOS.EXE /RUN APITEST.EXE (tests/loopa/jobs.py
 * api, make loopa-api): before INIT, the calls any program may make; then it
 * becomes an app and runs each area's cases. Each case says on COM1 (an app's
 * lines go into GLOS's log):
 *     HX-APITEST case=NAME ok=1|0 [what]
 * then HX-APITEST done pass=N fail=M, and the exit code is the failures (at
 * most 99).
 * Step 1: CORE (VERSION, INIT, CLOSE, GET, SET, EVENT, POST, TIMER, TICKS,
 * SLEEP, YIELD, TIME, RANDOM), with their errors, structures smaller than
 * GLOS's, and handles closed or never made.
 * Step 2: WIN and DRAW: a window made, shown, placed, maximised, minimised,
 * full screen and back, each with its events; its pixels and properties;
 * the list of windows; a dialog; the errors.
 *
 *   APITEST win    becomes an app with a window "APITEST W" and writes each
 *                  event it is sent as a line on stdout (the test drives the
 *                  desktop with glos key and glos mouse), until the window
 *                  is closed: WIN-OK closed, exit code 40. */
#include <pc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <glos/glos2.h>

static int pass, fails;

static void com1(const char *s)
{
    for (; *s; s++) {
        while (!(inportb(0x3FD) & 0x20))
            ;
        outportb(0x3F8, (unsigned char)*s);
    }
}

static void check(const char *name, int ok, const char *fmt, ...)
{
    char line[160], what[100] = "";
    va_list ap;
    if (fmt) {
        va_start(ap, fmt);
        vsnprintf(what, sizeof what, fmt, ap);
        va_end(ap);
    }
    snprintf(line, sizeof line, "HX-APITEST case=%s ok=%d%s%s\r\n", name, ok != 0, what[0] ? " " : "", what);
    com1(line);
    if (ok)
        pass++;
    else
        fails++;
}

/* Before INIT: the calls any program may make, and those it may not yet. */
static void before(void)
{
    struct glos_version v;
    struct { struct glos_version v; unsigned guard; } small;
    struct glos_post p = { sizeof p, 1, 0, 0, 0 };
    unsigned t;

    check("present", glos_running(), 0);
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    check("version", glos_version(&v) == 0 && v.major == 2 && v.minor == 0 && v.build[0]
          && v.functions[GLOS_AREA_CORE][0] == 0x1FFFu && GLOS_HAS(&v, GLOS_CORE_RANDOM)
          && GLOS_HAS(&v, GLOS_WIN_CREATE) && GLOS_HAS(&v, GLOS_DRAW_SCREEN) && !GLOS_HAS(&v, GLOS_FN(GLOS_AREA_WIN, 5))
          && !GLOS_HAS(&v, GLOS_FN(GLOS_AREA_SHELL, 0)),
          "major=%u minor=%u build=%s core=%x machine=%x", v.major, v.minor, v.build, v.functions[0][0], v.machine);
    memset(&small, 0xAB, sizeof small);
    small.v.size = 12;                                  /* (size, major, minor: an older program's) */
    check("version-small", glos_version(&small.v) == 0 && small.v.major == 2 && small.v.minor == 0
          && small.v.machine == 0xABABABABu && small.guard == 0xABABABABu, "machine=%x", small.v.machine);
    t = glos_ticks();
    check("ticks-any", t > 0, "ms=%u", t);
    check("post-not-app", glos_call(GLOS_CORE_POST, &p) == GLOS_E_NOT_APP, 0);
}

static void init(void)
{
    struct glos_init in;
    int r;
    memset(&in, 0, sizeof in);
    in.size = sizeof in;
    in.major = 3;
    check("init-major", glos_call(GLOS_CORE_INIT, &in) == GLOS_E_VERSION, 0);
    r = glos_init("API test", "apitest", "Tests");
    check("init", r == 0 && glos_app() > 0, "r=%d app=%u", r, glos_app());
    memset(&in, 0, sizeof in);
    in.size = sizeof in;
    in.major = 2;
    r = glos_call(GLOS_CORE_INIT, &in);
    check("init-twice", r == GLOS_E_INIT && in.why == GLOS_WHY_ALREADY, "r=%d why=%u", r, in.why);
}

static void calls(void)
{
    struct glos_time t;
    unsigned char buf[300];
    unsigned i, zero = 0;
    int r;

    r = glos_call(2, 0);                                /* (1.x's YIELD: a 2.0 app makes 2.0's calls) */
    check("1x-refused", r == GLOS_E_VERSION, "r=%d", r);
    check("unknown-core", glos_call(GLOS_FN(GLOS_AREA_CORE, 200), 0) == GLOS_E_FUNCTION, 0);
    check("unknown-area", glos_call(GLOS_FN(GLOS_AREA_SHELL, 0), 0) == GLOS_E_FUNCTION, 0);
    check("unknown-win", glos_call(GLOS_FN(GLOS_AREA_WIN, 5), 0) == GLOS_E_FUNCTION, 0);
    check("yield", glos_yield() == 0, 0);
    memset(&t, 0, sizeof t);
    t.size = sizeof t;
    r = glos_time(&t);
    check("time", r == 0 && t.utc > 315532800u && t.how <= 2, "utc=%u how=%u offset=%d", t.utc, t.how, t.offset);
    memset(buf, 0, sizeof buf);
    r = glos_random(buf, 32);
    for (i = 0; i < 32; i++)
        zero += !buf[i];
    check("random", r == 0 && zero < 8, "zeros=%u", zero);
    check("random-long", glos_random(buf, 300) == GLOS_E_ARGUMENT, 0);
}

static void events(void)
{
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev_post *p = (struct glos_ev_post *)ev;
    struct glos_wait w;
    int n, r, i;

    check("post", glos_post(7, 1, 2, 3) == 0, 0);
    n = glos_event(ev, sizeof ev, 0, 0);
    check("event-post", n == (int)sizeof *p && p->ev.kind == GLOS_EV_POST && p->code == 7 && p->a == 1 && p->b == 2
          && p->c == 3 && p->ev.handle == 0, "n=%d kind=%x", n, n > 0 ? p->ev.kind : 0);
    check("event-none", glos_event(ev, sizeof ev, 0, 0) == 0, 0);

    glos_post(8, 0, 0, 0);
    memset(&w, 0, sizeof w);
    w.size = sizeof w;
    w.buffer = (unsigned)ev;
    w.room = 8;
    r = glos_call(GLOS_CORE_EVENT, &w);
    check("event-room", r == GLOS_E_ROOM && w.length == sizeof *p, "r=%d length=%u", r, w.length);
    n = glos_event(ev, sizeof ev, 0, 1u << GLOS_AREA_WIN);
    check("event-filter-other", n == 0, "n=%d", n);
    n = glos_event(ev, sizeof ev, 0, 1u << GLOS_AREA_CORE);
    check("event-filter-core", n == (int)sizeof *p && p->code == 8, "n=%d", n);

    for (i = 0; i < 2000 && glos_post(9, (unsigned)i, 0, 0) == 0; i++)
        ;
    check("queue-full", i > 100 && i < 2000 && glos_post(9, 0, 0, 0) == GLOS_E_MEMORY, "posted=%d", i);
    for (n = 0; glos_event(ev, sizeof ev, 0, 0) > 0; n++)
        if (p->a != (unsigned)n)
            break;
    check("queue-order", n == i, "taken=%d", n);

    n = glos_event(ev, sizeof ev, 150, 0);
    check("event-wait", n == 0, "n=%d", n);
}

static void timers(void)
{
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev_timer *te = (struct glos_ev_timer *)ev;
    glos_handle h, once, again;
    unsigned t0, got = 0, last = 0, ms;
    int v = 0, r, n;
    char name[8];
    struct { struct glos_timer t; unsigned guard; } small;

    h = glos_timer(50, 1);
    check("timer", h != 0 && GLOS_HANDLE_KIND(h) == GLOS_HK_TIMER, "h=%x", h);
    t0 = glos_ticks();
    while (glos_ticks() - t0 < 1000) {
        n = glos_event(ev, sizeof ev, 200, 0);
        if (n > 0 && te->ev.kind == GLOS_EV_TIMER && te->ev.handle == h) {
            got++;
            last = te->count;
        }
    }
    check("timer-rate", got >= 15 && got <= 22, "events=%u count=%u", got, last);
    r = glos_get(h, GLOS_PROP_TIMER_COUNT, &v);
    check("prop-count", r == 0 && (unsigned)v >= last, "r=%d count=%d", r, v);
    r = glos_set(h, GLOS_PROP_TIMER_MS, 20) || glos_get(h, GLOS_PROP_TIMER_MS, &v);
    check("prop-ms", r == 0 && v == 20, "r=%d ms=%d", r, v);
    check("prop-ro", glos_set(h, GLOS_PROP_TIMER_COUNT, 5) == GLOS_E_PROPERTY, 0);
    check("prop-none", glos_get(h, GLOS_PROP(GLOS_HK_TIMER, 99), &v) == GLOS_E_PROPERTY, 0);
    r = glos_set_text(h, GLOS_PROP_TIMER_NAME, "ticker") || glos_get_text(h, GLOS_PROP_TIMER_NAME, name, sizeof name);
    check("prop-text", r == 0 && !strcmp(name, "ticker"), "r=%d name=%s", r, name);
    r = glos_get_text(h, GLOS_PROP_TIMER_NAME, name, 4);
    check("prop-text-room", r == GLOS_E_ROOM && !memcmp(name, "tick", 4), "r=%d", r);
    check("close", glos_close(h) == 0, 0);
    check("close-again", glos_close(h) == GLOS_E_HANDLE, 0);
    check("get-closed", glos_get(h, GLOS_PROP_TIMER_MS, &v) == GLOS_E_HANDLE, 0);
    again = glos_timer(1000, 1);
    check("handle-new-gen", again != 0 && again != h && (again & 0xFFFF) == (h & 0xFFFF)
          && glos_get(h, GLOS_PROP_TIMER_MS, &v) == GLOS_E_HANDLE, "h=%x again=%x", h, again);
    glos_close(again);
    while (glos_event(ev, sizeof ev, 0, 0) > 0)         /* (what the closed timers queued before they went) */
        ;

    once = glos_timer(30, 0);
    got = 0;
    t0 = glos_ticks();
    while (glos_ticks() - t0 < 400)
        if (glos_event(ev, sizeof ev, 100, 0) > 0 && te->ev.kind == GLOS_EV_TIMER && te->ev.handle == once)
            got++;
    check("timer-once", got == 1, "events=%u", got);
    glos_close(once);

    memset(&small, 0xCD, sizeof small);
    small.t.size = 8;                                   /* (size and ms only: repeat 0, handle not given back) */
    small.t.ms = 40;
    r = glos_call(GLOS_CORE_TIMER, &small.t);
    check("timer-small", r == 0 && small.t.repeat == 0xCDCDCDCDu && small.t.handle == 0xCDCDCDCDu
          && small.guard == 0xCDCDCDCDu, "r=%d", r);
    check("timer-zero", glos_timer(0, 1) == 0, 0);
    /* (the small timer's handle is unknown here: GLOS closes it at the app's end) */

    t0 = glos_ticks();
    r = glos_sleep(200);
    ms = glos_ticks() - t0;
    check("sleep", r == 0 && ms >= 190 && ms <= 300, "ms=%u", ms);
}

/* The next event of that kind (for that handle, if one is given) within ms; those before it are let go. */
static int await(unsigned kind, glos_handle h, void *ev, unsigned ms)
{
    unsigned t0 = glos_ticks();
    for (;;) {
        unsigned waited = glos_ticks() - t0;
        struct glos_ev *e = ev;
        int n = glos_event(ev, GLOS_EV_MAX, waited < ms ? ms - waited : 0, 0);
        if (n > 0 && e->kind == kind && (!h || e->handle == h))
            return n;
        if (n <= 0 && glos_ticks() - t0 >= ms)
            return 0;
    }
}

static int prop(glos_handle h, unsigned p)
{
    int v = -12345;
    return glos_get(h, p, &v) == 0 ? v : -12345;
}

static void fill(glos_handle w, unsigned char byte)
{
    struct glos_pixels p;
    unsigned char *pix;
    if (glos_pixels(w, &p) == 0 && (pix = glos_ptr(p.pixels)) != 0) {
        memset(pix, byte, p.pitch * (unsigned)p.h);
        glos_present(w, 0, 0, 0, 0);
    }
}

static void windows(void)
{
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev_focus *fo = (struct glos_ev_focus *)ev;
    struct glos_ev_size *sz = (struct glos_ev_size *)ev;
    struct glos_ev_state *st = (struct glos_ev_state *)ev;
    struct glos_screen scr;
    struct glos_pixels p;
    struct glos_win_info list[8];
    struct glos_win_create bad;
    glos_handle w, d, t;
    unsigned count = 99;
    unsigned char *pix;
    char title[64];
    int r, n, maxw, maxh;

    r = glos_screen(&scr);
    check("screen", r == 0 && scr.w >= 320 && scr.h >= 200 && (scr.format.bpp == 8 || scr.format.bpp == 16 || scr.format.bpp == 32)
          && scr.work_w == scr.w && scr.work_h > 0 && scr.work_h <= scr.h, "r=%d %dx%dx%u work=%dx%d", r, scr.w, scr.h,
          scr.format.bpp, scr.work_w, scr.work_h);
    maxw = scr.work_w - 2, maxh = scr.work_h - 21;

    w = glos_window("APITEST W", 40, 50, 200, 120, GLOS_WK_NORMAL, 0, 0);
    check("win-create", w && GLOS_HANDLE_KIND(w) == GLOS_HK_WINDOW && prop(w, GLOS_PROP_WIN_STATE) == GLOS_WS_HIDDEN
          && prop(w, GLOS_PROP_WIN_ID) > 0, "h=%x err=%d", w, glos_error());
    r = glos_show(w, GLOS_WS_NORMAL);
    n = await(GLOS_EV_FOCUS, w, ev, 2000);
    check("win-show", r == 0 && n == (int)sizeof *fo && fo->got == 1 && prop(w, GLOS_PROP_WIN_STATE) == GLOS_WS_NORMAL,
          "r=%d n=%d", r, n);
    check("win-where", prop(w, GLOS_PROP_WIN_X) == 41 && prop(w, GLOS_PROP_WIN_Y) == 70 && prop(w, GLOS_PROP_WIN_W) == 200
          && prop(w, GLOS_PROP_WIN_H) == 120, "%d,%d %dx%d", prop(w, GLOS_PROP_WIN_X), prop(w, GLOS_PROP_WIN_Y),
          prop(w, GLOS_PROP_WIN_W), prop(w, GLOS_PROP_WIN_H));
    r = glos_pixels(w, &p);
    pix = r ? 0 : glos_ptr(p.pixels);
    check("win-pixels", r == 0 && pix && p.w == 200 && p.h == 120 && p.pitch >= 200 * scr.format.bpp / 8
          && p.format.bpp == scr.format.bpp, "r=%d %dx%d pitch=%u bpp=%u", r, p.w, p.h, p.pitch, p.format.bpp);
    if (pix) {
        memset(pix, 0x55, p.pitch * (unsigned)p.h);
        pix[0] = 1, pix[p.pitch * (unsigned)p.h - 1] = 2;
    }
    check("win-present", glos_present(w, 0, 0, 0, 0) == 0 && glos_present(w, 10, 10, 50, 20) == 0
          && glos_present(w, 0, 0, -1, 5) == GLOS_E_ARGUMENT, 0);
    r = glos_get_text(w, GLOS_PROP_WIN_TITLE, title, sizeof title);
    check("win-title", r == 0 && !strcmp(title, "APITEST W") && glos_set_text(w, GLOS_PROP_WIN_TITLE, "Renamed") == 0
          && glos_get_text(w, GLOS_PROP_WIN_TITLE, title, sizeof title) == 0 && !strcmp(title, "Renamed"), "r=%d %s", r, title);
    check("win-limits", glos_set(w, GLOS_PROP_WIN_MIN_W, 120) == 0 && prop(w, GLOS_PROP_WIN_MIN_W) == 120
          && glos_set(w, GLOS_PROP_WIN_MAX_H, -1) == GLOS_E_ARGUMENT && glos_set(w, GLOS_PROP_WIN_W, 5) == GLOS_E_PROPERTY, 0);
    check("win-cursor", glos_set(w, GLOS_PROP_WIN_CURSOR, GLOS_CURSOR_HAND) == 0
          && prop(w, GLOS_PROP_WIN_CURSOR) == GLOS_CURSOR_HAND && glos_set(w, GLOS_PROP_WIN_CURSOR, 99) == GLOS_E_ARGUMENT, 0);

    r = glos_move(w, 60, 70);
    check("win-move", r == 0 && prop(w, GLOS_PROP_WIN_X) == 61 && prop(w, GLOS_PROP_WIN_Y) == 90, "r=%d", r);
    r = glos_resize(w, 240, 150);
    n = await(GLOS_EV_SIZE, w, ev, 2000);
    check("win-resize", r == 0 && n == (int)sizeof *sz && sz->w == 240 && sz->h == 150 && prop(w, GLOS_PROP_WIN_W) == 240
          && glos_pixels(w, &p) == 0 && p.w == 240 && p.h == 150, "r=%d n=%d", r, n);
    fill(w, 0x33);

    r = glos_show(w, GLOS_WS_MAX);
    n = await(GLOS_EV_SIZE, w, ev, 2000);
    check("win-max-size", r == 0 && n && sz->w == maxw && sz->h == maxh, "r=%d %dx%d want %dx%d", r, n ? sz->w : 0,
          n ? sz->h : 0, maxw, maxh);
    n = await(GLOS_EV_STATE, w, ev, 2000);
    check("win-max-state", n == (int)sizeof *st && st->state == GLOS_WS_MAX && prop(w, GLOS_PROP_WIN_STATE) == GLOS_WS_MAX
          && prop(w, GLOS_PROP_WIN_X) == 1 && prop(w, GLOS_PROP_WIN_Y) == 20, "n=%d", n);
    fill(w, 0x44);
    r = glos_resize(w, 180, 100);               /* while maximised: the size it goes back to */
    check("win-max-place", r == 0 && prop(w, GLOS_PROP_WIN_W) == maxw && glos_event(ev, sizeof ev, 100, 1u << GLOS_AREA_WIN) == 0,
          "r=%d", r);
    r = glos_show(w, GLOS_WS_NORMAL);
    n = await(GLOS_EV_SIZE, w, ev, 2000);
    check("win-restore", r == 0 && n && sz->w == 180 && sz->h == 100 && prop(w, GLOS_PROP_WIN_X) == 61
          && await(GLOS_EV_STATE, w, ev, 2000) && st->state == GLOS_WS_NORMAL, "r=%d", r);

    r = glos_show(w, GLOS_WS_MIN);
    n = await(GLOS_EV_STATE, w, ev, 2000);
    check("win-min", r == 0 && n && st->state == GLOS_WS_MIN && prop(w, GLOS_PROP_WIN_STATE) == GLOS_WS_MIN
          && await(GLOS_EV_FOCUS, w, ev, 2000) && fo->got == 0, "r=%d n=%d", r, n);
    r = glos_show(w, GLOS_WS_SHOWN);
    n = await(GLOS_EV_STATE, w, ev, 2000);
    check("win-unmin", r == 0 && n && st->state == GLOS_WS_NORMAL && await(GLOS_EV_FOCUS, w, ev, 2000) && fo->got == 1,
          "r=%d n=%d", r, n);

    r = glos_show(w, GLOS_WS_FULL);
    n = await(GLOS_EV_SIZE, w, ev, 2000);
    check("win-full", r == 0 && n && sz->w == scr.w && sz->h == scr.h && await(GLOS_EV_STATE, w, ev, 2000)
          && st->state == GLOS_WS_FULL && prop(w, GLOS_PROP_WIN_X) == 0 && prop(w, GLOS_PROP_WIN_Y) == 0, "r=%d", r);
    fill(w, 0x66);
    r = glos_show(w, GLOS_WS_NORMAL);
    n = await(GLOS_EV_SIZE, w, ev, 2000);
    check("win-unfull", r == 0 && n && sz->w == 180 && sz->h == 100 && prop(w, GLOS_PROP_WIN_Y) == 90, "r=%d", r);

    memset(list, 0, sizeof list);
    r = glos_windows(0, list, 8, &count);
    check("win-enum", r == 0 && count == 1 && list[0].size == sizeof list[0] && !strcmp(list[0].title, "Renamed")
          && list[0].focus == 1 && list[0].kind == GLOS_WK_NORMAL && list[0].state == GLOS_WS_NORMAL && list[0].w == 180
          && list[0].app == glos_app() && list[0].id == (unsigned)prop(w, GLOS_PROP_WIN_ID), "r=%d count=%u", r, count);
    r = glos_windows(GLOS_ENUM_ALL, list, 8, &count);
    check("win-enum-all", (r == 0 || r == GLOS_E_ROOM) && count >= 1, "r=%d count=%u", r, count);
    r = glos_windows(0, list, 0, &count);
    check("win-enum-room", r == GLOS_E_ROOM && count == 1, "r=%d count=%u", r, count);

    {                                           /* its other properties */
        static unsigned icon[256], back[256];
        struct glos_cursor cur;
        unsigned len = 99, i;
        check("win-topmost", prop(w, GLOS_PROP_WIN_TOPMOST) == 0 && glos_set(w, GLOS_PROP_WIN_TOPMOST, 1) == 0
              && prop(w, GLOS_PROP_WIN_TOPMOST) == 1 && glos_set(w, GLOS_PROP_WIN_TOPMOST, 0) == 0
              && prop(w, GLOS_PROP_WIN_TOPMOST) == 0, 0);
        check("win-progress", prop(w, GLOS_PROP_WIN_PROGRESS) == -1 && glos_set(w, GLOS_PROP_WIN_PROGRESS, 50) == 0
              && prop(w, GLOS_PROP_WIN_PROGRESS) == 50 && glos_set(w, GLOS_PROP_WIN_PROGRESS, 101) == GLOS_E_ARGUMENT
              && glos_set(w, GLOS_PROP_WIN_PROGRESS, -1) == 0 && prop(w, GLOS_PROP_WIN_PROGRESS) == -1, 0);
        check("win-taskbar", prop(w, GLOS_PROP_WIN_TASKBAR) == 1 && glos_set(w, GLOS_PROP_WIN_TASKBAR, 0) == 0
              && prop(w, GLOS_PROP_WIN_TASKBAR) == 0 && glos_set(w, GLOS_PROP_WIN_TASKBAR, 1) == 0, 0);
        check("win-flash-focus", glos_set(w, GLOS_PROP_WIN_FLASH, 1) == 0 && prop(w, GLOS_PROP_WIN_FLASH) == 0, 0);
        for (i = 0; i < 256; i++)
            icon[i] = 0xFF000000u | (i * 0x010203u);
        r = glos_get_data(w, GLOS_PROP_WIN_ICON, back, sizeof back, &len);
        check("win-icon-none", r == 0 && len == 0, "r=%d len=%u", r, len);
        r = glos_set_data(w, GLOS_PROP_WIN_ICON, icon, sizeof icon);
        n = glos_get_data(w, GLOS_PROP_WIN_ICON, back, sizeof back, &len);
        check("win-icon", r == 0 && n == 0 && len == 1024 && !memcmp(icon, back, sizeof icon)
              && glos_set_data(w, GLOS_PROP_WIN_ICON, icon, 100) == GLOS_E_ARGUMENT
              && glos_get_data(w, GLOS_PROP_WIN_ICON, back, 16, &len) == GLOS_E_ROOM && len == 1024, "r=%d n=%d len=%u", r, n, len);
        r = glos_set_data(w, GLOS_PROP_WIN_ICON, icon, 0);
        check("win-icon-off", r == 0 && glos_get_data(w, GLOS_PROP_WIN_ICON, back, sizeof back, &len) == 0 && len == 0, "r=%d", r);
        check("win-pointer-none", glos_set(w, GLOS_PROP_WIN_CURSOR, GLOS_CURSOR_CUSTOM) == GLOS_E_ARGUMENT, 0);
        memset(&cur, 0, sizeof cur);
        cur.w = 8, cur.h = 8, cur.hot_x = 4, cur.hot_y = 4;
        for (i = 0; i < 8; i++)
            cur.black[i][0] = (unsigned char)(0x80 >> i), cur.white[i][0] = (unsigned char)(0x01 << i);
        r = glos_set_data(w, GLOS_PROP_WIN_POINTER, &cur, sizeof cur);
        check("win-pointer", r == 0 && prop(w, GLOS_PROP_WIN_CURSOR) == GLOS_CURSOR_CUSTOM, "r=%d", r);
        cur.hot_x = 8;
        check("win-pointer-bad", glos_set_data(w, GLOS_PROP_WIN_POINTER, &cur, sizeof cur) == GLOS_E_ARGUMENT
              && glos_set_data(w, GLOS_PROP_WIN_POINTER, &cur, 10) == GLOS_E_ARGUMENT
              && glos_set(w, GLOS_PROP_WIN_CURSOR, GLOS_CURSOR_ARROW) == 0, 0);
        r = glos_set_text(w, GLOS_PROP_WIN_HELP, "apitest/windows");
        check("win-help", r == 0 && glos_get_text(w, GLOS_PROP_WIN_HELP, title, sizeof title) == 0
              && !strcmp(title, "apitest/windows"), "r=%d %s", r, title);
    }

    {                                           /* BLIT: the app's own formats into the window's */
        static const struct glos_format f32 = { 32, 16, 8, 8, 8, 0, 8, { 0, 0 } }, f8 = { 8, 0, 0, 0, 0, 0, 0, { 0, 0 } };
        static const struct glos_format f24 = { 24, 16, 8, 8, 8, 0, 8, { 0, 0 } };
        static const unsigned cols[6] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000, 0x333333 };
        static unsigned src32[6 * 3], pal[256];
        static unsigned char src8[6 * 3], own[6 * 4 * 3];
        unsigned bytes, i;
        struct glos_version v;
        for (i = 0; i < 18; i++)
            src32[i] = cols[i % 6], src8[i] = (unsigned char)(10 + i % 6);
        for (i = 0; i < 6; i++)
            pal[10 + i] = cols[i];
        fill(w, 0x11);
        glos_pixels(w, &p);
        pix = glos_ptr(p.pixels);
        bytes = p.format.bpp / 8;
        r = glos_blit(w, 4, 2, src32, 6, 3, 6 * 4, &f32, 0);
        n = glos_blit(w, 4, 20, src8, 6, 3, 6, &f8, pal);
        check("blit", r == 0 && n == 0 && pix && !memcmp(pix + 2 * p.pitch + 4 * bytes, pix + 20 * p.pitch + 4 * bytes, 6 * bytes)
              && memcmp(pix + 2 * p.pitch + 4 * bytes, pix + 2 * p.pitch + 5 * bytes, bytes)
              && pix[2 * p.pitch + 3 * bytes] == 0x11 && pix[2 * p.pitch + 10 * bytes] == 0x11 && pix[5 * p.pitch + 4 * bytes] == 0x11,
              "r=%d n=%d", r, n);
        if (pix)                                /* the screen's own format: as they are */
            for (i = 0; i < 3; i++)
                memcpy(own + i * 6 * bytes, pix + (2 + i) * p.pitch + 4 * bytes, 6 * bytes);
        r = glos_blit(w, 30, 40, own, 6, 3, 6 * bytes, &p.format, 0);
        check("blit-same", r == 0 && pix && !memcmp(pix + 40 * p.pitch + 30 * bytes, pix + 2 * p.pitch + 4 * bytes, 6 * bytes)
              && !memcmp(pix + 42 * p.pitch + 30 * bytes, pix + 4 * p.pitch + 4 * bytes, 6 * bytes), "r=%d", r);
        r = glos_blit(w, -2, -1, src32, 6, 3, 6 * 4, &f32, 0);          /* partly outside: what is inside drawn */
        n = glos_blit(w, p.w - 3, p.h - 2, src32, 6, 3, 6 * 4, &f32, 0);
        check("blit-clip", r == 0 && n == 0 && pix && !memcmp(pix, pix + 3 * p.pitch + 6 * bytes, 4 * bytes)
              && glos_blit(w, 5000, 5000, src32, 6, 3, 6 * 4, &f32, 0) == 0, "r=%d n=%d", r, n);
        check("blit-bad", glos_blit(w, 0, 0, src32, 6, 3, 6 * 4, &f24, 0) == GLOS_E_ARGUMENT
              && glos_blit(w, 0, 0, src32, 6, 3, 8, &f32, 0) == GLOS_E_ARGUMENT
              && glos_blit(w, 0, 0, (void *)0xFFFFFF00u, 6, 3, 24, &f32, 0) == GLOS_E_ARGUMENT
              && glos_blit(w, 0, 0, src32, 0, 3, 24, &f32, 0) == GLOS_E_ARGUMENT, 0);
        memset(&v, 0, sizeof v);
        v.size = sizeof v;
        glos_version(&v);
        if (!(v.machine & GLOS_MACHINE_GPU)) {  /* no Matrox engine on this desktop: its calls say so */
            struct glos_gpu g;
            struct glos_vram vr;
            memset(&g, 0, sizeof g);
            g.size = sizeof g;
            memset(&vr, 0, sizeof vr);
            vr.size = sizeof vr, vr.least = 65536, vr.align = 4096;
            check("gpu-none", glos_call(GLOS_DRAW_GPU, &g) == GLOS_E_NO_GPU && glos_call(GLOS_DRAW_VRAM, &vr) != 0, 0);
        }
    }

    {                                           /* INPUT: the keyboard, the mode, pads, the game port, capture */
        struct glos_keys k;
        struct glos_pads pads;
        struct glos_gameport gp;
        struct glos_input_mode im = { sizeof im, 0, 99 };
        unsigned i, any = 0;
        r = glos_keys(&k);
        for (i = 0; i < sizeof k.down; i++)
            any |= k.down[i];
        check("input-keys", r == 0 && !any && k.mods == 0 && !GLOS_KEY_DOWN(&k, 0x1E), "r=%d any=%u mods=%u", r, any, k.mods);
        r = glos_call(GLOS_INPUT_MODE, &im);
        check("input-mode", r == 0 && im.flags == 0 && glos_input_mode(GLOS_INPUT_RAW) == 0
              && glos_call(GLOS_INPUT_MODE, &im) == 0 && im.flags == GLOS_INPUT_RAW && glos_input_mode(8) == GLOS_E_ARGUMENT
              && glos_input_mode(0) == 0, "r=%d flags=%u", r, im.flags);
        r = glos_pads(&pads, 1);
        check("input-pads", r == 0 && !pads.pad[0].present && !pads.pad[1].present && glos_pads(&pads, 0) == 0, "r=%d", r);
        r = glos_gameport(&gp);
        check("input-gameport", r == 0 && gp.mode <= 2 && gp.range >= 100 && gp.pads == 0, "r=%d on=%u mode=%u range=%u", r,
              gp.virtual_on, gp.mode, gp.range);
        r = glos_capture(w, 1);
        check("input-capture", r == 0 && glos_capture(w, 0) == 0 && glos_capture(w, 0) == 0, "r=%d", r);
    }

    d = glos_window("A dialog", 0, 0, 120, 60, GLOS_WK_DIALOG, GLOS_WF_ANYWHERE, w);
    r = glos_windows(0, list, 8, &count);
    check("win-dialog", d && r == 0 && count == 2 && (list[0].kind == GLOS_WK_DIALOG || list[1].kind == GLOS_WK_DIALOG)
          && prop(d, GLOS_PROP_WIN_STATE) == GLOS_WS_HIDDEN, "d=%x r=%d count=%u err=%d", d, r, count, glos_error());
    check("input-capture-hidden", glos_capture(d, 1) == GLOS_E_REFUSED, 0);
    check("win-dialog-props", prop(d, GLOS_PROP_WIN_TASKBAR) == 0 && glos_set(d, GLOS_PROP_WIN_FLASH, 1) == 0
          && prop(d, GLOS_PROP_WIN_FLASH) == 1, 0);
    {                                           /* a modal dialog: the focus asked for its owner goes to it */
        glos_handle md = glos_window("Modal", 0, 0, 100, 50, GLOS_WK_DIALOG, GLOS_WF_ANYWHERE | GLOS_WF_MODAL, w);
        int got_d = 0, lost_w = 0, k;
        r = md ? glos_show(md, GLOS_WS_NORMAL) : -1;
        for (k = 0; k < 20 && !(got_d && lost_w); k++)
            if (glos_event(ev, sizeof ev, 200, 1u << GLOS_AREA_WIN) > 0 && fo->ev.kind == GLOS_EV_FOCUS) {
                got_d |= fo->ev.handle == md && fo->got == 1;
                lost_w |= fo->ev.handle == w && fo->got == 0;
            }
        check("win-modal", md && r == 0 && got_d && lost_w, "md=%x r=%d got=%d lost=%d", md, r, got_d, lost_w);
        r = glos_focus(w);
        memset(list, 0, sizeof list);
        glos_windows(0, list, 8, &count);
        for (k = 0, n = -1; k < (int)count && k < 8; k++)
            if (list[k].focus)
                n = k;
        check("win-modal-keeps", r == 0 && n >= 0 && !strcmp(list[n].title, "Modal") && (list[n].flags & GLOS_WF_MODAL)
              && glos_event(ev, sizeof ev, 100, 1u << GLOS_AREA_WIN) == 0, "r=%d focus=%s", r, n >= 0 ? list[n].title : "?");
        check("win-modal-owner", !glos_window("x", 0, 0, 50, 50, GLOS_WK_DIALOG, GLOS_WF_MODAL, 0)
              && glos_error() == GLOS_E_ARGUMENT, "err=%d", glos_error());
        r = glos_close(md);
        n = await(GLOS_EV_FOCUS, w, ev, 2000);
        check("win-modal-gone", r == 0 && n && fo->got == 1, "r=%d n=%d", r, n);
    }
    r = glos_windows(0, list, 8, &count);
    t = glos_timer(5000, 0);
    check("win-owner-kind", !glos_window("x", 0, 0, 50, 50, GLOS_WK_DIALOG, 0, t) && glos_error() == GLOS_E_KIND, "err=%d",
          glos_error());
    check("win-owner-none", !glos_window("x", 0, 0, 50, 50, GLOS_WK_DIALOG, 0, 0x02000077u) && glos_error() == GLOS_E_HANDLE,
          "err=%d", glos_error());
    memset(&bad, 0, sizeof bad);
    bad.size = sizeof bad, bad.kind = 9, bad.w = bad.h = 50;
    check("win-kind", glos_call(GLOS_WIN_CREATE, &bad) == GLOS_E_ARGUMENT, 0);
    bad.kind = 0, bad.w = 0;
    check("win-empty", glos_call(GLOS_WIN_CREATE, &bad) == GLOS_E_ARGUMENT, 0);
    check("win-not-window", glos_show(t, GLOS_WS_NORMAL) == GLOS_E_KIND && glos_pixels(t, &p) == GLOS_E_KIND
          && glos_show(w, 9) == GLOS_E_ARGUMENT && glos_capture(t, 1) == GLOS_E_KIND, 0);
    check("win-drag-refused", glos_drag(w, GLOS_DRAG_MOVE) == GLOS_E_REFUSED, 0);
    glos_close(t);
    r = glos_close(d) || glos_close(w);
    check("win-close", r == 0 && glos_get(w, GLOS_PROP_WIN_STATE, &n) == GLOS_E_HANDLE && glos_pixels(w, &p) == GLOS_E_HANDLE
          && glos_windows(0, list, 8, &count) == 0 && count == 0, "r=%d count=%u", r, count);
    while (glos_event(ev, sizeof ev, 0, 0) > 0)
        ;
}

/* APITEST win: the events of a window, a line each, until it is closed. */
/* SETTINGS, DRAW's MODES and IDLE (step 5): what can be asked without changing anything. (The changes, their
   events, the theme's change and the saver are APITEST set's, under the agent: /RUN has no GLOS.CFG to write.) */
static void settings(void)
{
    static struct glos_mode modes[64];
    static char buf[1024];
    struct glos_schema sc;
    struct glos_theme th;
    struct glos_region rg;
    struct glos_time tm;
    struct glos_screen scr;
    struct glos_saver sv;
    struct glos_power pw;
    glos_handle h, h2;
    unsigned i, count = 0, found = 0, sorted = 1, texts = 1, n;
    int set = -1, r, e;

    r = glos_setting_get("input", "double_click", buf, sizeof buf, &set);
    check("setting-get", r == 0 && !strcmp(buf, "500") && set == 0, "r=%d value=%s set=%d", r, buf, set);
    r = glos_setting_get("Display", "ACCEL", buf, sizeof buf, &set);    /* (names in any case) */
    check("setting-case", r == 0 && !strcmp(buf, "on"), "r=%d value=%s", r, buf);
    memset(buf, 'x', 8);
    r = glos_setting_get("time", "server", buf, 5, &set);
    check("setting-room", r == GLOS_E_ROOM && !strcmp(buf, "pool"), "r=%d value=%.8s", r, buf);
    check("setting-none", glos_setting_get("input", "nothing", buf, sizeof buf, 0) == GLOS_E_SETTING
          && glos_setting_get("nowhere", "mode", buf, sizeof buf, 0) == GLOS_E_SETTING
          && glos_setting_get(0, "mode", buf, sizeof buf, 0) == GLOS_E_ARGUMENT, 0);

    r = glos_schema(0, &sc, buf, sizeof buf);
    count = sc.count;
    for (i = 0; r == 0 && i < count; i++) {
        const char *sec, *key, *choices, *def, *label, *desc;
        if (glos_schema(i, &sc, buf, sizeof buf) != 0 || sc.count != count) {
            texts = 0;
            break;
        }
        sec = buf, key = sec + strlen(sec) + 1, choices = key + strlen(key) + 1, def = choices + strlen(choices) + 1;
        label = def + strlen(def) + 1, desc = label + strlen(label) + 1;
        texts &= sec[0] && key[0] && label[0] && desc[0] && (unsigned)(desc + strlen(desc) + 1 - buf) == sc.length
                 && sc.kind >= GLOS_SK_CHOICE && sc.kind <= GLOS_SK_ENV && sc.when <= GLOS_SET_APP;
        if (!strcmp(sec, "desktop") && !strcmp(key, "theme"))
            found += sc.kind == GLOS_SK_CHOICE && sc.when == GLOS_SET_NOW && !strcmp(choices, "slate|dark|contrast")
                     && !strcmp(def, "slate") && strchr(label, '&') != 0;
        if (!strcmp(sec, "saver") && !strcmp(key, "wait"))
            found += sc.kind == GLOS_SK_NUMBER && sc.min == 1 && sc.max == 240 && !strcmp(choices, "off");
    }
    check("schema", r == 0 && count >= 50 && texts && found == 2, "r=%d count=%u texts=%u found=%u", r, count, texts, found);
    e = glos_schema(count, &sc, buf, sizeof buf);
    r = glos_schema(0, &sc, buf, 4);
    check("schema-ends", e == GLOS_E_ARGUMENT && sc.count == count && r == GLOS_E_ROOM && sc.length > 4, "e=%d r=%d", e, r);

    for (i = 0, r = 0; i < GLOS_WATCH_MAX; i++) {       /* as many sections as may be watched, and no more */
        char name[8] = "s0";
        name[1] = (char)('0' + i);
        r |= glos_setting_watch(name, 1);
    }
    e = glos_setting_watch("ninth", 1);
    check("watch", r == 0 && e == GLOS_E_MEMORY && glos_setting_watch("s3", 1) == 0 && glos_setting_watch("s3", 0) == 0
          && glos_setting_watch("ninth", 1) == 0 && glos_setting_watch(0, 0) == 0, "r=%d e=%d", r, e);

    r = glos_theme(&th);
    check("theme", r == 0 && !strcmp(th.name, "slate") && th.flags == 0 && th.roles == GLOS_ROLES
          && th.colour[GLOS_ROLE_FACE] == 0xCCCCCC && th.colour[GLOS_ROLE_MARK] == 0xCC6633
          && th.colour[GLOS_ROLE_TITLE] == 0x333333 && th.border == 1 && th.title == 18 && th.bar == 20 && th.text == 1,
          "r=%d name=%s face=%06x title=%d", r, th.name, th.colour[GLOS_ROLE_FACE], th.title);
    memset(&tm, 0, sizeof tm);
    tm.size = sizeof tm;
    glos_time(&tm);
    r = glos_region(&rg);
    check("region", r == 0 && rg.date == GLOS_DATE_YMD && rg.date_sep == '-' && rg.clock12 == 0 && rg.decimal == '.'
          && rg.thousands == ',' && rg.first_day == 1 && rg.codepage == 0 && !strcmp(rg.layout, "us")
          && !strcmp(rg.language, "en") && rg.offset == tm.offset,
          "r=%d date=%u%c clock12=%u offset=%d", r, rg.date, rg.date_sep, rg.clock12, rg.offset);

    memset(&scr, 0, sizeof scr);
    scr.size = sizeof scr;
    glos_screen(&scr);
    r = glos_modes(modes, 64, &count);
    for (i = 0, found = 0; r == 0 && i < count && i < 64; i++) {
        found |= modes[i].w == scr.w && modes[i].h == scr.h && modes[i].bits == scr.format.bpp;
        if (i && (modes[i].w < modes[i - 1].w || (modes[i].w == modes[i - 1].w && modes[i].h < modes[i - 1].h)))
            sorted = 0;
    }
    check("modes", r == 0 && count >= 3 && found && sorted, "r=%d count=%u screen=%dx%dx%u found=%u sorted=%u", r, count,
          scr.w, scr.h, scr.format.bpp, found, sorted);
    n = 0;
    r = glos_modes(modes, 1, &n);
    check("modes-room", r == GLOS_E_ROOM && n == count, "r=%d count=%u", r, n);

    n = glos_idle();
    glos_sleep(300);
    i = glos_idle();
    check("idle", i >= n + 250 && i < n + 2000, "before=%u after=%u", n, i);
    h = glos_awake();
    h2 = glos_awake();
    r = glos_saver(GLOS_SAVER_STATE, &sv);
    e = (int)sv.awake;
    glos_close(h);
    glos_saver(GLOS_SAVER_STATE, &sv);
    check("awake", h && h2 && h != h2 && r == 0 && e == 2 && sv.awake == 1 && sv.on == 0 && sv.wait == 0
          && glos_close(h2) == 0 && glos_close(h2) == GLOS_E_HANDLE && glos_saver(7, 0) == GLOS_E_ARGUMENT,
          "r=%d awake=%d then=%u on=%u wait=%u", r, e, sv.awake, sv.on, sv.wait);
    r = glos_power(0, 0, &pw);
    e = pw.can & (1u << GLOS_POWER_OFF) ? 0 : glos_power(1, GLOS_POWER_OFF, 0);     /* (never turned off here) */
    check("power", r == 0 && pw.state == GLOS_POWER_ON && (pw.can == 0 || (pw.can & 1)) && (e == 0 || e == GLOS_E_REFUSED)
          && glos_power(1, 9, 0) == GLOS_E_ARGUMENT, "r=%d state=%u can=%x off=%d", r, pw.state, pw.can, e);
}

/* APITEST set: the settings' changes and their events, the theme's change, and the saver, under the agent
   (tests/loopa/jobs.py api_set_case drives it with glos config, glos idle and keys). It writes what it is told
   on stdout: EV setting SECTION KEY (it watches desktop and region), EV theme NAME, EV region ..., EV saver N,
   EV key ...; s runs the set and unset cases (SET ok=N); a takes or closes an AWAKE handle; q ends it (41). */
static int setting_cases(void)
{
    char buf[256], why[120];
    unsigned when = 99;
    int set = -1, ok = 1, r;
    r = glos_setting_set("input", "double_click", "400", &when, why, sizeof why);
    ok &= r == 0 && when == GLOS_SET_APP;
    ok &= glos_setting_get("input", "double_click", buf, sizeof buf, &set) == 0 && !strcmp(buf, "400") && set == 1;
    ok &= glos_setting_keys("input", buf, sizeof buf) == 0 && strstr(buf, "double_click = 400") != 0;
    ok &= glos_setting_sections(buf, sizeof buf) == 0 && strstr(buf, "input") != 0;
    ok &= glos_setting_set("input", "double_click", 0, &when, 0, 0) == 0;
    ok &= glos_setting_get("input", "double_click", buf, sizeof buf, &set) == 0 && !strcmp(buf, "500") && set == 0;
    r = glos_setting_set("input", "double_click", "5", 0, why, sizeof why);
    ok &= r == GLOS_E_SETTING && why[0];
    printf("SET refused r=%d why=%s\n", r, why);
    ok &= glos_setting_set("input", "nothing", "1", 0, why, sizeof why) == GLOS_E_SETTING;
    ok &= glos_setting_set("program APITEST.EXE", "title", "Api Test", &when, 0, 0) == 0 && when == GLOS_SET_NOW;
    ok &= glos_setting_get("program apitest.exe", "title", buf, sizeof buf, &set) == 0 && !strcmp(buf, "Api Test") && set == 1;
    ok &= glos_setting_set("program APITEST.EXE", "title", 0, 0, 0, 0) == 0;
    return ok;
}

static int setting_mode(void)
{
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev *e = (struct glos_ev *)ev;
    struct glos_theme th;
    struct glos_region rg;
    glos_handle w, awake = 0;
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 1;
    w = glos_window("APITEST S", 40, 40, 200, 120, GLOS_WK_NORMAL, 0, 0);
    if (!w || glos_show(w, GLOS_WS_NORMAL) != 0)
        return 2;
    fill(w, 0x2A);
    printf("SET watch r=%d\n", glos_setting_watch("desktop", 1) | glos_setting_watch("region", 1));
    fflush(stdout);
    for (;;) {
        int n = glos_event(ev, sizeof ev, GLOS_FOREVER, 0);
        if (n <= 0)
            continue;
        if (e->kind == GLOS_EV_KEY) {
            struct glos_ev_key *k = (struct glos_ev_key *)ev;
            printf("EV key %03X down=%u\n", k->key, k->down);
            if (k->key == 0x1F && !k->down)     /* s let go */
                printf("SET ok=%d\n", setting_cases());
            if (k->key == 0x1E && !k->down) {   /* a let go: an AWAKE handle taken, or closed */
                if (awake) {
                    printf("EV awake 0 r=%d\n", glos_close(awake));
                    awake = 0;
                } else {
                    awake = glos_awake();
                    printf("EV awake 1 r=%d\n", awake != 0);
                }
            }
            if (k->key == 0x10 && !k->down) {   /* q let go */
                printf("SET-OK end\n");
                fflush(stdout);
                return 41;
            }
        } else if (e->kind == GLOS_EV_SETTING) {
            struct glos_ev_setting *s = (struct glos_ev_setting *)ev;
            printf("EV setting %s %s\n", s->section, s->key);
            if (!strcmp(s->section, "region") && glos_region(&rg) == 0)
                printf("EV region date=%u%c clock12=%u decimal=%c thousands=%u first=%u cp=%u\n", rg.date, rg.date_sep,
                       rg.clock12, rg.decimal, rg.thousands, rg.first_day, rg.codepage);
        } else if (e->kind == GLOS_EV_THEME) {
            if (glos_theme(&th) == 0)
                printf("EV theme %s flags=%u face=%06x\n", th.name, th.flags, th.colour[GLOS_ROLE_FACE]);
        } else if (e->kind == GLOS_EV_SAVER) {
            printf("EV saver %u\n", ((struct glos_ev_saver *)ev)->on);
        }
        fflush(stdout);
    }
}

static int interactive(void)
{
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev *e = (struct glos_ev *)ev;
    glos_handle w;
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 1;
    struct glos_pads pads;
    int raw_mode = 0, captured = 0;
    w = glos_window("APITEST W", 40, 40, 200, 120, GLOS_WK_NORMAL, 0, 0);
    if (!w || glos_show(w, GLOS_WS_NORMAL) != 0)
        return 2;
    fill(w, 0x2A);
    glos_pads(&pads, 1);
    for (;;) {
        int n = glos_event(ev, sizeof ev, GLOS_FOREVER, 0);
        if (n <= 0)
            continue;
        if (e->kind == GLOS_EV_KEY) {
            struct glos_ev_key *k = (struct glos_ev_key *)ev;
            printf("EV key %03X ch=%u down=%u mods=%u\n", k->key, k->ch, k->down, k->mods);
            if (k->key == 0x13 && !k->down) {   /* r let go: raw keys, or no longer */
                raw_mode = !raw_mode;
                printf("EV raw %d r=%d\n", raw_mode, glos_input_mode(raw_mode ? GLOS_INPUT_RAW : 0));
            }
            if (k->key == 0x2E && !k->down) {   /* c let go: the mouse captured, or let go */
                captured = !captured;
                printf("EV capture %d r=%d\n", captured, glos_capture(w, captured));
            }
            if (k->key == 0x23 && !k->down) {   /* h let go: 8 seconds without taking an event ("not answering") */
                unsigned t0 = glos_ticks();
                while (glos_ticks() - t0 < 8000)
                    ;
                printf("EV woke\n");
            }
        } else if (e->kind == GLOS_EV_MOUSE) {
            struct glos_ev_mouse *m = (struct glos_ev_mouse *)ev;
            if (m->button && m->down)
                printf("EV mouse %d,%d buttons=%u button=%u down=1 clicks=%u\n", m->x, m->y, m->buttons, m->button, m->clicks);
            else if (m->button)
                printf("EV mouse %d,%d buttons=%u button=%u down=0\n", m->x, m->y, m->buttons, m->button);
        } else if (e->kind == GLOS_EV_TEXT) {
            printf("EV text %u\n", ((struct glos_ev_text *)ev)->ch);
        } else if (e->kind == GLOS_EV_PAD) {
            struct glos_ev_pad *pe = (struct glos_ev_pad *)ev;
            printf("EV pad %u present=%u lx=%d buttons=%x changed=%x\n", pe->index, pe->pad.present, pe->pad.lx,
                   pe->pad.buttons, pe->changed);
        } else if (e->kind == GLOS_EV_WHEEL) {
            printf("EV wheel %d\n", ((struct glos_ev_wheel *)ev)->notches);
        } else if (e->kind == GLOS_EV_FOCUS) {
            printf("EV focus %u\n", ((struct glos_ev_focus *)ev)->got);
        } else if (e->kind == GLOS_EV_SIZE) {
            printf("EV size %dx%d\n", ((struct glos_ev_size *)ev)->w, ((struct glos_ev_size *)ev)->h);
            fill(w, 0x2A);
        } else if (e->kind == GLOS_EV_STATE) {
            printf("EV state %u\n", ((struct glos_ev_state *)ev)->state);
        } else if (e->kind == GLOS_EV_CLOSE) {
            printf("EV close\n");
            glos_close(w);
            printf("WIN-OK closed\n");
            fflush(stdout);
            return 40;
        }
        fflush(stdout);
    }
}

int main(int argc, char **argv)
{
    char line[64];
    if (argc > 1 && !strcmp(argv[1], "win"))
        return interactive();
    if (argc > 1 && !strcmp(argv[1], "set"))
        return setting_mode();
    before();
    init();
    if (glos_app()) {
        calls();
        events();
        timers();
        windows();
        settings();
    }
    snprintf(line, sizeof line, "HX-APITEST done pass=%d fail=%d\r\n", pass, fails);
    com1(line);
    return fails > 99 ? 99 : fails;
}
