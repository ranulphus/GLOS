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
#include <io.h>
#include <pc.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
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

static char home[80] = "C:\\TEST", home_slash[84] = "C:/TEST/";   /* the folder the test is in (GLOS's own) */

static void find_home(const char *argv0)
{
    const char *end = strrchr(argv0, '/') > strrchr(argv0, '\\') ? strrchr(argv0, '/') : strrchr(argv0, '\\');
    unsigned i, n = end ? (unsigned)(end - argv0) : 0;
    if (n < 3 || n >= sizeof home || argv0[1] != ':')
        return;
    for (i = 0; i < n; i++) {
        home[i] = argv0[i] == '/' ? '\\' : (char)toupper((unsigned char)argv0[i]);
        home_slash[i] = home[i] == '\\' ? '/' : home[i];
    }
    home[n] = 0;
    home_slash[n] = '/', home_slash[n + 1] = 0;
}

/* A path in the test's folder: fmt with one %s for it (hp: backslashes; hps: slashes, one at its end). */
static const char *hp_of(const char *fmt, const char *dir)
{
    static char b[8][200];
    static int i;
    i = (i + 1) & 7;
    snprintf(b[i], sizeof b[i], fmt, dir);
    return b[i];
}
#define hp(fmt)  hp_of(fmt, home)
#define hps(fmt) hp_of(fmt, home_slash)

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
          && GLOS_HAS(&v, GLOS_SHELL_SCHEDULE) && !GLOS_HAS(&v, GLOS_FN(GLOS_AREA_SHELL, 10))
          && GLOS_HAS(&v, GLOS_JOB_STATUS) && !GLOS_HAS(&v, GLOS_FN(GLOS_AREA_JOB, 5))
          && GLOS_HAS(&v, GLOS_SYS_PROGRAMS) && !GLOS_HAS(&v, GLOS_FN(GLOS_AREA_SYS, 4))
          && GLOS_HAS(&v, GLOS_AUDIO_OUT) && GLOS_HAS(&v, GLOS_AUDIO_DEVICES) && !GLOS_HAS(&v, GLOS_FN(GLOS_AREA_AUDIO, 1))
          && !GLOS_HAS(&v, GLOS_FN(GLOS_AREA_NET, 0)),
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
    check("unknown-area", glos_call(GLOS_FN(GLOS_AREA_NET, 0), 0) == GLOS_E_FUNCTION, 0);
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
        /* (Loop A's PCs have no pad; a bench may: whichever, the game port's pads are those there are) */
        any = pads.pad[0].present | pads.pad[1].present;
        check("input-pads", r == 0 && pads.pad[0].present <= 1 && pads.pad[1].present <= 1 && glos_pads(&pads, 0) == 0,
              "r=%d present=%u,%u", r, pads.pad[0].present, pads.pad[1].present);
        r = glos_gameport(&gp);
        check("input-gameport", r == 0 && gp.mode <= 2 && gp.range >= 100 && (gp.pads != 0) == (any != 0), "r=%d on=%u mode=%u range=%u", r,
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

/* CLIP (step 6): the clipboard in one format and in several, read whole while it is changed, private formats,
   large data in pieces, transactions dropped, its event; and DND's refusals (a drag itself needs the mouse:
   APITEST dnd, under the agent). */
static void clipboard(void)
{
    static unsigned char big[300000], back[300000];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_clip_formats f;
    struct { struct glos_clip_image h; unsigned px[6]; } img = { { 3, 2 }, { 1, 2, 3, 4, 5, 6 } }, img2;
    char buf[64];
    glos_handle w, r, r2;
    unsigned len = 0, serial, priv, priv2, i, total = 0, sum = 0, sum2 = 0, text[1] = { GLOS_CLIP_TEXT };
    int e, n, ok;

    while (glos_event(ev, sizeof ev, 0, 0) > 0)         /* (the cases before: their events out of the way) */
        ;
    glos_clip_formats(0, &f);
    serial = f.serial;
    e = glos_clip_put(GLOS_CLIP_TEXT, "hello, clipboard", 16);
    n = await(GLOS_EV_CLIP, 0, ev, 2000);
    memset(buf, 0, sizeof buf);
    ok = e == 0 && n == (int)sizeof(struct glos_ev_clip) && glos_clip_get(GLOS_CLIP_TEXT, buf, sizeof buf, &len) == 0
         && len == 16 && !strcmp(buf, "hello, clipboard") && glos_clip_formats(0, &f) == 0 && f.count == 1
         && f.format[0] == GLOS_CLIP_TEXT && f.length[0] == 16 && f.serial > serial
         && ((struct glos_ev_clip *)ev)->serial == f.serial;
    check("clip-text", ok, "e=%d ev=%d len=%u text=%s count=%u serial=%u", e, n, len, buf, f.count, f.serial);
    memset(buf, 'x', sizeof buf);
    e = glos_clip_get(GLOS_CLIP_TEXT, buf, 5, &len);
    check("clip-room", e == GLOS_E_ROOM && len == 16 && !memcmp(buf, "hellox", 6)
          && glos_clip_get(GLOS_CLIP_IMAGE, buf, sizeof buf, &len) == GLOS_E_ARGUMENT, "e=%d len=%u", e, len);

    priv = glos_clip_format("glos.test.one");
    priv2 = glos_clip_format("glos.test.two");
    check("clip-private", priv >= GLOS_CLIP_PRIVATE && priv2 >= GLOS_CLIP_PRIVATE && priv != priv2
          && glos_clip_format("glos.test.one") == priv && glos_clip_format("") == 0 && glos_clip_format("two words") == 0,
          "one=%x two=%x", priv, priv2);

    /* Several formats at once; a reader keeps the clipboard it opened whatever is put meanwhile. */
    w = glos_clip_open(1);
    e = glos_clip_add(w, GLOS_CLIP_TEXT, "first", 5) | glos_clip_add(w, GLOS_CLIP_IMAGE, &img, sizeof img)
        | glos_clip_add(w, priv, "private!", 8) | glos_clip_add(w, GLOS_CLIP_TEXT, "three formats", 13);
    glos_clip_get(GLOS_CLIP_TEXT, buf, sizeof buf, &len);       /* (not yet the clipboard: until its CLOSE) */
    ok = e == 0 && len == 16 && glos_close(w) == 0;
    r = glos_clip_open(0);
    e = glos_clip_put(GLOS_CLIP_TEXT, "newer", 5);
    memset(buf, 0, sizeof buf);
    memset(&img2, 0, sizeof img2);
    ok &= e == 0 && glos_clip_formats(r, &f) == 0 && f.count == 3 && glos_clip_read(r, GLOS_CLIP_TEXT, buf, sizeof buf, 0, &total) == 13
          && total == 13 && !strcmp(buf, "three formats") && glos_clip_read(r, GLOS_CLIP_IMAGE, &img2, sizeof img2, 0, 0) == (int)sizeof img
          && !memcmp(&img, &img2, sizeof img) && glos_clip_read(r, priv, buf, sizeof buf, 0, 0) == 8 && !memcmp(buf, "private!", 8)
          && glos_clip_read(r, priv2, buf, sizeof buf, 0, 0) == -GLOS_E_ARGUMENT;
    r2 = glos_clip_open(0);
    memset(buf, 0, sizeof buf);
    ok &= glos_clip_read(r2, GLOS_CLIP_TEXT, buf, sizeof buf, 0, 0) == 5 && !strcmp(buf, "newer") && glos_clip_formats(r2, &f) == 0
          && f.count == 1 && glos_close(r) == 0 && glos_close(r2) == 0 && glos_close(r2) == GLOS_E_HANDLE;
    check("clip-transactions", ok, "e=%d count=%u total=%u text=%s", e, f.count, total, buf);

    /* Transactions that change nothing: no PUT; COMMIT set to 0; and what a transaction refuses. */
    w = glos_clip_open(1);
    glos_close(w);
    w = glos_clip_open(1);
    glos_clip_add(w, GLOS_CLIP_TEXT, "dropped", 7);
    e = glos_set(w, GLOS_PROP_CLIP_COMMIT, 0);
    glos_close(w);
    memset(buf, 0, sizeof buf);
    glos_clip_get(GLOS_CLIP_TEXT, buf, sizeof buf, &len);
    r = glos_clip_open(0);
    w = glos_clip_open(1);
    ok = e == 0 && !strcmp(buf, "newer") && glos_clip_add(r, GLOS_CLIP_TEXT, "x", 1) == GLOS_E_REFUSED
         && glos_set(r, GLOS_PROP_CLIP_COMMIT, 0) == GLOS_E_REFUSED && glos_clip_add(w, 0, "x", 1) == GLOS_E_ARGUMENT
         && glos_clip_add(w, 77, "x", 1) == GLOS_E_ARGUMENT && glos_clip_add(w, GLOS_CLIP_TEXT, (void *)0xFFFFFF00u, 64) == GLOS_E_ARGUMENT
         && glos_clip_add(0x0D000077u, GLOS_CLIP_TEXT, "x", 1) == GLOS_E_HANDLE && glos_clip_read(w, GLOS_CLIP_TEXT, buf, 4, 0, 0) == 0;
    glos_close(r);
    glos_close(w);                                      /* (its only PUTs failed: nothing to commit) */
    check("clip-dropped", ok, "e=%d text=%s", e, buf);

    /* Large data, read back a piece at a time. */
    for (i = 0; i < sizeof big; i++) {
        big[i] = (unsigned char)(i * 7 + (i >> 9));
        sum += big[i];
    }
    e = glos_clip_put(priv, big, sizeof big);
    r = glos_clip_open(0);
    for (i = 0; i < sizeof back; i += (unsigned)n)
        if ((n = glos_clip_read(r, priv, back + i, 70001, i, &total)) <= 0)
            break;
    for (len = 0; len < sizeof back; len++)
        sum2 += back[len];
    check("clip-large", e == 0 && i == sizeof big && total == sizeof big && sum == sum2 && !memcmp(big, back, sizeof big)
          && glos_clip_read(r, priv, back, 10, sizeof big, 0) == 0, "e=%d read=%u total=%u", e, i, total);
    glos_close(r);
    glos_clip_put(GLOS_CLIP_TEXT, "left by APITEST", 15);       /* (it stays when the app has ended) */

    /* DND without the mouse: every way a drag is refused. */
    w = glos_clip_open(1);
    glos_clip_add(w, GLOS_CLIP_TEXT, "drag", 4);
    r = glos_window("APITEST D", 300, 300, 80, 60, GLOS_WK_NORMAL, 0, 0);
    glos_show(r, GLOS_WS_NORMAL);
    e = glos_dnd(r, w, GLOS_DND_COPY, 0, 0, 0);                 /* no button is down in it */
    ok = e == GLOS_E_REFUSED && glos_close(w) == GLOS_E_HANDLE;  /* (the call closed the transaction) */
    w = glos_clip_open(1);
    e = glos_dnd(r, w, GLOS_DND_COPY, 0, 0, 0);                 /* nothing was put in it */
    ok &= e == GLOS_E_ARGUMENT && glos_dnd(r, 0, GLOS_DND_COPY, 0, 0, 0) == GLOS_E_ARGUMENT;
    ok &= glos_dnd_target(r, GLOS_DND_COPY | GLOS_DND_MOVE, text, 1) == 0 && glos_dnd_target(r, GLOS_DND_COPY, text, 0) == GLOS_E_ARGUMENT
          && glos_dnd_target(r, 8, text, 1) == GLOS_E_ARGUMENT && glos_dnd_target(0x02000077u, GLOS_DND_COPY, text, 1) == GLOS_E_HANDLE
          && glos_dnd_answer(r, GLOS_DND_COPY) == GLOS_E_REFUSED && glos_dnd_target(r, 0, 0, 0) == 0
          && glos_dnd_target(0, GLOS_DND_COPY, text, 1) == GLOS_E_REFUSED      /* (the bare desktop's are the shell's) */
          && glos_dnd_target(0, 0, 0, 0) == 0;
    glos_close(r);
    check("dnd-refused", ok, "e=%d", e);
}

/* FS (step 7): a folder's entries in batches, several files' details, the drives, watches, a disk's sectors. The
   folder of many files is made here (C:\OUT\MANY: `many` files; 2,000 in Loop A). */
static struct glos_fs_entry *entry_named(void *buf, unsigned count, const char *name)
{
    struct glos_fs_entry *e = buf;
    unsigned i;
    for (i = 0; i < count; i++, e = GLOS_FS_NEXT(e))
        if (!strcmp(e->name, name))
            return e;
    return 0;
}

static void files(unsigned many)
{
    static unsigned char buf[16384], seen[4096 / 8];
    static unsigned char sec[4 * 512], back[4 * 512];
    static struct glos_fs_drive dr[26];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_fs_entry *e;
    struct glos_fs_sectors q;
    glos_handle l = 0, w, w2;
    unsigned count = 0, total = 0, batches = 0, i, n, t0, first_ms = 0, dups = 0, c = 0, a = 0, x;
    char name[64];
    int r, ok, fd;

    /* A small folder in one batch (the test's own: GLOS's folder): what DOS says of a file. */
    r = glos_fs_list(&l, home, buf, sizeof buf, &count, 0);
    e = entry_named(buf, count, "APITEST.EXE");
    ok = r == 0 && l == 0 && count >= 3 && e && e->bytes_lo > 100000 && e->bytes_hi == 0 && !(e->attr & GLOS_FA_FOLDER)
         && (e->mtime >> 25) + 1980 >= 2026 && !e->short_name[0] && !(e->size & 3) && entry_named(buf, count, "GLOSK.BIN")
         && !entry_named(buf, count, ".") && !entry_named(buf, count, "..");
    check("fs-list", ok, "r=%d count=%u bytes=%u attr=%x year=%u", r, count, e ? e->bytes_lo : 0, e ? e->attr : 0,
          e ? (e->mtime >> 25) + 1980 : 0);
    r = glos_fs_list(&l, "C:\\NOSUCH.DIR", buf, sizeof buf, &count, 0);
    mkdir("C:\\OUT\\EMPTY", 0777);
    ok = r == GLOS_E_ARGUMENT && glos_fs_list(&l, "C:\\OUT\\EMPTY", buf, sizeof buf, &count, 0) == 0 && count == 0 && l == 0
         && glos_fs_list(&l, home_slash, buf, sizeof buf, &count, 0) == 0 && count >= 3      /* (slashes either way) */
         && glos_fs_list(&l, home, buf, 512, &count, 0) == GLOS_E_ARGUMENT                    /* (too little room) */
         && glos_fs_list(&l, 0, buf, sizeof buf, &count, 0) == GLOS_E_ARGUMENT;
    rmdir("C:\\OUT\\EMPTY");
    check("fs-list-none", ok, "r=%d count=%u", r, count);

    /* A folder of many files, a small batch at a time: every name once; then in large batches; a listing closed
       before its end. */
    mkdir("C:\\OUT\\MANY", 0777);
    for (i = 0; i < many; i++) {
        snprintf(name, sizeof name, "C:\\OUT\\MANY\\F%04u.TMP", i);
        if ((fd = _creat(name, 0)) < 0)
            break;
        _close(fd);
        if (i % 200 == 199)
            com1("HX-APITEST making\r\n");             /* (Loop A takes a long silence for a hang) */
    }
    n = i;
    memset(seen, 0, sizeof seen);
    t0 = glos_ticks();
    l = 0;
    do {
        if ((r = glos_fs_list(&l, "C:\\OUT\\MANY", buf, 2048, &count, 0)) != 0)
            break;
        if (!batches++)
            first_ms = glos_ticks() - t0;
        for (i = 0, e = (struct glos_fs_entry *)buf; i < count; i++, e = GLOS_FS_NEXT(e)) {
            if (sscanf(e->name, "F%u.TMP", &x) == 1 && x < many && strlen(e->name) == 9) {
                dups += seen[x / 8] >> (x % 8) & 1;
                seen[x / 8] |= (unsigned char)(1u << (x % 8));
            } else {
                dups += 1000;
            }
        }
        total += count;
    } while (l);
    t0 = glos_ticks() - t0;
    check("fs-list-many", r == 0 && n == many && total == many && !dups && batches > many / 60 && !l,
          "r=%d made=%u listed=%u dups=%u batches=%u first=%ums all=%ums", r, n, total, dups, batches, first_ms, t0);
    total = batches = 0;
    t0 = glos_ticks();
    do {
        if ((r = glos_fs_list(&l, "C:\\OUT\\MANY", buf, sizeof buf, &count, 0)) != 0)
            break;
        batches++;
        total += count;
    } while (l);
    t0 = glos_ticks() - t0;
    check("fs-list-large", r == 0 && total == many && batches >= 1 && batches <= many / 250 + 2, "r=%d listed=%u batches=%u all=%ums",
          r, total, batches, t0);
    l = 0;
    r = glos_fs_list(&l, "C:\\OUT\\MANY", buf, 1024, &count, 0);
    w = l;
    ok = r == 0 && l && count && glos_close(l) == 0 && glos_fs_list(&w, 0, buf, 1024, &count, 0) == GLOS_E_HANDLE;
    check("fs-list-closed", ok, "r=%d count=%u", r, count);
    for (i = 0; i < many; i++) {
        snprintf(name, sizeof name, "C:\\OUT\\MANY\\F%04u.TMP", i);
        remove(name);
        if (i % 200 == 199)
            com1("HX-APITEST removing\r\n");
    }
    rmdir("C:\\OUT\\MANY");

    /* Several files' details at once: a file, a folder, a drive's root, one that isn't there. */
    {
        static char paths[400];
        int k = 0;
        k += snprintf(paths + k, sizeof paths - k, "%s\\APITEST.EXE", home) + 1;
        k += snprintf(paths + k, sizeof paths - k, "%s", home) + 1;
        k += snprintf(paths + k, sizeof paths - k, "C:\\") + 1;
        k += snprintf(paths + k, sizeof paths - k, "%s\\NOPE.XXX", home) + 1;
        paths[k] = 0;
        r = glos_fs_info(paths, 4, buf, sizeof buf, 0);
    }
    e = (struct glos_fs_entry *)buf;
    ok = r == 0 && !strcmp(e->name, "APITEST.EXE") && e->bytes_lo > 100000 && !(e->attr & GLOS_FA_FOLDER);
    e = GLOS_FS_NEXT(e);
    ok = ok && (e->attr & GLOS_FA_FOLDER) && !strcasecmp(e->name, strrchr(home, '\\') + 1);
    e = GLOS_FS_NEXT(e);
    ok = ok && (e->attr & GLOS_FA_FOLDER) && e->attr != GLOS_FA_NONE;
    e = GLOS_FS_NEXT(e);
    ok = ok && e->attr == GLOS_FA_NONE && (e->bytes_lo == 2 || e->bytes_lo == 18);
    check("fs-info", ok && glos_fs_info(home, 1, buf, 8, 0) == GLOS_E_ROOM && glos_fs_info("X\0", 0, buf, 64, 0) == GLOS_E_ARGUMENT,
          "r=%d none=%x error=%u", r, e->attr, e->bytes_lo);

    /* The drives: C is a hard disk with its details; a drive asked for by its letter; one that isn't there. */
    r = glos_fs_drives(0, dr, 26, &count);
    for (i = 0; i < count; i++) {
        if (dr[i].letter == 'C')
            c = i + 1;
        if (dr[i].letter == 'A')
            a = i + 1;
    }
    ok = r == 0 && c && dr[c - 1].kind == GLOS_DRIVE_HARD && (dr[c - 1].flags & GLOS_DRIVE_DETAILS) && dr[c - 1].total_kb > 1000
         && dr[c - 1].free_kb <= dr[c - 1].total_kb && !strncmp(dr[c - 1].system, "FAT", 3)
         && (!a || (dr[a - 1].kind == GLOS_DRIVE_FLOPPY && !(dr[a - 1].flags & GLOS_DRIVE_DETAILS)));
    check("fs-drives", ok, "r=%d count=%u C=%u kind=%u flags=%x total=%uK free=%uK system=%s label=%s", r, count, c,
          c ? dr[c - 1].kind : 0, c ? dr[c - 1].flags : 0, c ? dr[c - 1].total_kb : 0, c ? dr[c - 1].free_kb : 0,
          c ? dr[c - 1].system : "", c ? dr[c - 1].label : "");
    r = glos_fs_drives('c', dr, 1, &n);
    ok = r == 0 && n == 1 && dr[0].letter == 'C' && (dr[0].flags & GLOS_DRIVE_DETAILS) && glos_fs_drives('Q', dr, 1, &n) == 0 && n == 0
         && glos_fs_drives('1', dr, 1, &n) == GLOS_E_ARGUMENT && glos_fs_drives(0, dr, 0, &n) == GLOS_E_ROOM && n == count;
    check("fs-drive-one", ok, "r=%d n=%u long=%u", r, n, (dr[0].flags & GLOS_DRIVE_LONG) != 0);

    /* Watches: a folder (a file made in it), a file (another file made beside it is not its change; it is). */
    while (glos_event(ev, sizeof ev, 0, 0) > 0)
        ;
    w = glos_fs_watch("C:\\OUT");
    w2 = glos_fs_watch("C:\\OUT\\WATCHED.TXT");
    fd = _creat("C:\\OUT\\OTHER.TXT", 0);
    _close(fd);
    n = (unsigned)await(GLOS_EV_FS, w, ev, 3000);
    x = (unsigned)await(GLOS_EV_FS, w2, ev, 700);        /* (the file's watch: not told of the other file) */
    fd = _creat("C:\\OUT\\WATCHED.TXT", 0);
    _close(fd);
    i = (unsigned)await(GLOS_EV_FS, w2, ev, 3000);
    ok = w && w2 && n == sizeof(struct glos_ev) && !x && i == sizeof(struct glos_ev) && glos_close(w) == 0 && glos_close(w2) == 0;
    remove("C:\\OUT\\OTHER.TXT");
    remove("C:\\OUT\\WATCHED.TXT");
    while (glos_event(ev, sizeof ev, 300, 1u << GLOS_AREA_FS) > 0)
        x += 100;                                       /* (closed: told no more) */
    check("fs-watch", ok && x == 0 && !glos_fs_watch("OUT") && !glos_fs_watch(0) && glos_close(w) == GLOS_E_HANDLE,
          "folder=%u other=%u file=%u", n, x, i);

    /* Sectors: the first hard disk read, never written; the scratch disk (the second) written and read back. */
    memset(&q, 0, sizeof q);
    q.disk = 0x80;
    q.count = 1;
    q.buffer = (unsigned)sec;
    r = glos_fs_sectors(&q);
    ok = r == 0 && q.bytes == 512 && q.total_lo > 1000 && sec[510] == 0x55 && sec[511] == 0xAA;
    q.write = 1;
    ok = ok && glos_fs_sectors(&q) == GLOS_E_REFUSED;   /* (GLOS's own disk) */
    q.write = 0;
    q.count = 33;
    ok = ok && glos_fs_sectors(&q) == GLOS_E_ARGUMENT;
    q.count = 1;
    q.disk = 0x8F;                                      /* no such disk: the BIOS says so */
    x = (unsigned)glos_fs_sectors(&q);
    check("fs-sectors", ok && x == GLOS_E_REFUSED && q.error, "r=%d total=%u sig=%02x%02x none=%u error=%x", r, q.total_lo, sec[510],
          sec[511], x, q.error);
    memset(&q, 0, sizeof q);
    q.disk = 0x81;
    r = glos_fs_sectors(&q);                            /* (count 0: only its size) */
    n = q.total_lo;
    for (i = 0; i < sizeof sec; i++)
        sec[i] = (unsigned char)(i * 13 + 7);
    q.write = 1;
    q.lba_lo = 100;
    q.count = 4;
    q.buffer = (unsigned)sec;
    if (r != 0) {                                       /* no second disk (a bench PC): only that GLOS's own is */
        q.disk = 0x80;                                  /* never written */
        x = (unsigned)glos_fs_sectors(&q);
        check("fs-sectors-write", x == GLOS_E_REFUSED, "r=%d own=%u (no second disk)", r, x);
        return;
    }
    ok = r == 0 && n > 1000 && glos_fs_sectors(&q) == 0;
    q.write = 0;
    q.buffer = (unsigned)back;
    ok = ok && glos_fs_sectors(&q) == 0 && !memcmp(sec, back, sizeof sec);
    q.lba_lo = n;                                       /* past its end */
    q.count = 1;
    x = (unsigned)glos_fs_sectors(&q);
    check("fs-sectors-write", ok && x == GLOS_E_REFUSED, "r=%d total=%u past=%u", r, n, x);
}

/* APITEST lfn: long file names where DOS has them (a driver loaded before GLOS: DOSLFN; tests/loopa/jobs.py
   api_lfn_case). The C library makes the files by their long names; LIST and INFO give both names. */
static int lfn_mode(void)
{
    static unsigned char buf[8192];
    static struct glos_fs_drive dr[1];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_fs_entry *e;
    glos_handle l = 0, w;
    unsigned count = 0, n = 0;
    FILE *f;
    int r, ok;
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 1;
    mkdir("C:\\OUT\\Long folder name", 0777);
    f = fopen("C:\\OUT\\Long folder name\\A long file name.text", "w");
    if (f) {
        fputs("long", f);
        fclose(f);
    }
    f = fopen("C:\\OUT\\Long folder name\\SHORT.TXT", "w");
    if (f)
        fclose(f);
    r = glos_fs_drives('C', dr, 1, &n);
    check("lfn-drive", r == 0 && n == 1 && (dr[0].flags & GLOS_DRIVE_LONG), "r=%d flags=%x", r, dr[0].flags);
    r = glos_fs_list(&l, "C:\\OUT\\Long folder name", buf, sizeof buf, &count, 0);
    e = entry_named(buf, count, "A long file name.text");
    ok = r == 0 && l == 0 && count == 2 && e && e->bytes_lo == 4 && !strncmp(e->short_name, "ALONGF~1.TEX", 12);
    e = entry_named(buf, count, "SHORT.TXT");
    check("lfn-list", ok && e, "r=%d count=%u short=%s", r, count, entry_named(buf, count, "A long file name.text")
          ? entry_named(buf, count, "A long file name.text")->short_name : "?");
    r = glos_fs_list(&l, "C:\\OUT\\Long folder name", buf, sizeof buf, &count, GLOS_FS_SHORT);
    check("lfn-short", r == 0 && count == 2 && entry_named(buf, count, "ALONGF~1.TEX") && !entry_named(buf, count, "A long file name.text"),
          "r=%d count=%u", r, count);
    r = glos_fs_info("C:\\OUT\\Long folder name\\A long file name.text\0C:\\OUT\\Long folder name\0", 2, buf, sizeof buf, 0);
    e = (struct glos_fs_entry *)buf;
    ok = r == 0 && !strcmp(e->name, "A long file name.text") && e->bytes_lo == 4;
    e = GLOS_FS_NEXT(e);
    check("lfn-info", ok && (e->attr & GLOS_FA_FOLDER) && !strcmp(e->name, "Long folder name"), "r=%d name=%s", r, e->name);
    while (glos_event(ev, sizeof ev, 0, 0) > 0)
        ;
    w = glos_fs_watch("C:\\OUT\\Long folder name");
    f = fopen("C:\\OUT\\Long folder name\\Another long name.text", "w");
    if (f)
        fclose(f);
    n = (unsigned)await(GLOS_EV_FS, w, ev, 3000);
    check("lfn-watch", w && n == sizeof(struct glos_ev), "w=%x ev=%u", w, n);
    glos_close(w);
    remove("C:\\OUT\\Long folder name\\Another long name.text");
    remove("C:\\OUT\\Long folder name\\A long file name.text");
    remove("C:\\OUT\\Long folder name\\SHORT.TXT");
    rmdir("C:\\OUT\\Long folder name");
    {
        char line[64];
        snprintf(line, sizeof line, "HX-APITEST done pass=%d fail=%d\r\n", pass, fails);
        com1(line);
    }
    return fails > 99 ? 99 : fails;
}

/* APITEST dnd NAME X: drag and drop between two of these, under the agent (tests/loopa/jobs.py api_dnd_case moves
   the mouse). A window at X,60 that takes text (copy or move) and whose left half answers copy, its right half
   move; a press in it starts a drag of the text "from NAME" with a picture. What it is told goes to stdout:
   DND enter/over/leave/drop/done ...; p puts "clip NAME" on the clipboard, g says what the clipboard's text is,
   q ends it (42). */
static int dnd_mode(const char *name, int x)
{
    static unsigned picture[16 * 12];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev *e = (struct glos_ev *)ev;
    unsigned text[1] = { GLOS_CLIP_TEXT }, i, overs = 0;
    char title[24], buf[64];
    glos_handle w, t;
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 1;
    snprintf(title, sizeof title, "APITEST %s", name);
    w = glos_window(title, x, 60, 160, 100, GLOS_WK_NORMAL, 0, 0);
    if (!w || glos_show(w, GLOS_WS_NORMAL) != 0)
        return 2;
    fill(w, 0x2A);
    for (i = 0; i < sizeof picture / sizeof picture[0]; i++)
        picture[i] = (i % 16 + i / 16) % 3 ? 0xFF3366CCu : 0;
    printf("DND %s target r=%d\n", name, glos_dnd_target(w, GLOS_DND_COPY | GLOS_DND_MOVE, text, 1));
    fflush(stdout);
    for (;;) {
        int n = glos_event(ev, sizeof ev, GLOS_FOREVER, 0);
        if (n <= 0)
            continue;
        if (e->kind == GLOS_EV_MOUSE) {
            struct glos_ev_mouse *m = (struct glos_ev_mouse *)ev;
            if (m->button == 1 && m->down) {    /* a press: what it has is dragged from here */
                snprintf(buf, sizeof buf, "from %s", name);
                t = glos_clip_open(1);
                glos_clip_add(t, GLOS_CLIP_TEXT, buf, strlen(buf));
                printf("DND %s drag r=%d\n", name, glos_dnd(w, t, GLOS_DND_COPY | GLOS_DND_MOVE, picture, 16, 12));
            } else if (m->button) {
                printf("DND %s button-up\n", name);        /* (never during its own drag) */
            }
        } else if (e->kind == GLOS_EV_DND_ENTER || e->kind == GLOS_EV_DND_OVER) {
            struct glos_ev_dnd *d = (struct glos_ev_dnd *)ev;
            int r = glos_dnd_answer(w, d->x < 80 ? GLOS_DND_COPY : GLOS_DND_MOVE);
            if (e->kind == GLOS_EV_DND_ENTER)
                printf("DND %s enter at=%d,%d allowed=%u formats=%u:%u answer=%d\n", name, d->x, d->y, d->effects, d->count,
                       d->format[0], r);
            else
                overs++;
        } else if (e->kind == GLOS_EV_DND_LEAVE) {
            printf("DND %s leave overs=%u\n", name, overs > 0);
            overs = 0;
        } else if (e->kind == GLOS_EV_DND_DROP) {
            struct glos_ev_dnd *d = (struct glos_ev_dnd *)ev;
            int got;
            memset(buf, 0, sizeof buf);
            got = glos_clip_read(d->data, GLOS_CLIP_TEXT, buf, sizeof buf - 1, 0, 0);
            printf("DND %s drop at=%d,%d effect=%u overs=%u text=%s got=%d close=%d\n", name, d->x, d->y, d->effects, overs > 0,
                   buf, got, glos_close(d->data));
            overs = 0;
        } else if (e->kind == GLOS_EV_DND_DONE) {
            printf("DND %s done effect=%u\n", name, ((struct glos_ev_dnd *)ev)->effects);
        } else if (e->kind == GLOS_EV_CLIP) {
            printf("DND %s clip-changed\n", name);
        } else if (e->kind == GLOS_EV_KEY && !((struct glos_ev_key *)ev)->down) {
            unsigned key = ((struct glos_ev_key *)ev)->key, len = 0;
            if (key == 0x19) {                  /* p */
                snprintf(buf, sizeof buf, "clip %s", name);
                printf("DND %s put r=%d\n", name, glos_clip_put(GLOS_CLIP_TEXT, buf, strlen(buf)));
            } else if (key == 0x22) {           /* g */
                int r;
                memset(buf, 0, sizeof buf);
                r = glos_clip_get(GLOS_CLIP_TEXT, buf, sizeof buf - 1, &len);
                printf("DND %s got r=%d text=%s\n", name, r, buf);
            } else if (key == 0x10) {           /* q */
                printf("DND %s end\n", name);
                fflush(stdout);
                return 42;
            }
        }
        fflush(stdout);
    }
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

/* SYS (step 9): the system's figures, the programs, the log, an app asked to end; what KILL refuses. (A job
   ended by KILL is the job mode's.) */
static void sys(void)
{
    static struct glos_sys_pci pci[40];
    static struct glos_program progs[16];
    static char text[4096];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev *e = (struct glos_ev *)ev;
    struct glos_sys_system st;
    struct glos_sys_memory mem;
    struct glos_init again;
    unsigned len = 0, count = 0, next = 0, i, me = 99;
    int r, n, ok;

    r = glos_sys_stat(GLOS_STAT_SYSTEM, &st, sizeof st, &len, &count);
    ok = r == 0 && len == sizeof st && count == 1 && st.size == sizeof st && st.uptime_ms > 0 && st.busy_ms <= st.uptime_ms
         && st.cpu_family >= 4 && st.threads > 3 && st.apps >= 1 && st.hostname[0] && st.agent <= 1;
    check("sys-system", ok, "r=%d len=%u up=%u busy=%u cpu=%u threads=%u apps=%u windows=%u host=%s agent=%u", r, len,
          st.uptime_ms, st.busy_ms, st.cpu_family, st.threads, st.apps, st.windows, st.hostname, st.agent);
    r = glos_sys_stat(GLOS_STAT_MEMORY, &mem, sizeof mem, &len, &count);
    ok = r == 0 && count == 1 && mem.size == sizeof mem && mem.total_kb >= 4096 && mem.free_kb > 0 && mem.free_kb < mem.total_kb
         && mem.kernel_kb > 100 && mem.heap_kb <= mem.heap_max_kb && glos_sys_stat(GLOS_STAT_MEMORY, &mem, 8, &len, &count) == GLOS_E_ROOM
         && len == sizeof mem && count == 0;
    check("sys-memory", ok, "r=%d total=%uK free=%uK kernel=%uK heap=%uK cache=%uK vram=%uK", r, mem.total_kb, mem.free_kb,
          mem.kernel_kb, mem.heap_kb, mem.cache_kb, mem.vram_kb);
    r = glos_sys_stat(GLOS_STAT_PCI, pci, sizeof pci, &len, &count);
    ok = r == 0 && count >= 3 && len == count * sizeof pci[0];
    for (i = 0; i < count; i++)
        ok = ok && pci[i].size == sizeof pci[0] && pci[i].vendor != 0xFFFF && pci[i].vendor && pci[i].keeper <= GLOS_KEEPER_QUIET;
    n = glos_sys_stat(GLOS_STAT_PCI, pci, sizeof pci[0] * 2 + 5, &len, &i);
    check("sys-pci", ok && n == GLOS_E_ROOM && i == 2 && len == count * sizeof pci[0], "r=%d count=%u first=%04x:%04x n=%d given=%u",
          r, count, pci[0].vendor, pci[0].device, n, i);
    ok = 1;
    for (i = GLOS_STAT_NET; i <= GLOS_STAT_THREADS; i++) {
        text[0] = 0;
        r = glos_sys_stat(i, text, sizeof text - 1, &len, &count);
        ok = ok && (r == 0 || r == GLOS_E_ROOM) && count == 0 && len < 8192;
    }
    ok = ok && len > 10 && glos_sys_stat(9, text, sizeof text, &len, &count) == GLOS_E_ARGUMENT
         && glos_sys_stat(GLOS_STAT_SYSTEM, (void *)0xFFFFFF00u, 4096, &len, &count) == GLOS_E_ARGUMENT;
    check("sys-text", ok, "r=%d threads=%u bytes", r, len);

    /* The programs: this one among them. */
    r = glos_programs(progs, sizeof progs, &count);
    for (i = 0; i < count; i++)
        if (progs[i].kind == GLOS_PROG_APP && !strcmp(progs[i].name, "APITEST.EXE"))
            me = i;
    ok = r == 0 && count >= 1 && me < count && progs[me].size == sizeof progs[0] && progs[me].id > 0 && progs[me].mem_kb > 100
         && progs[me].state == 0;
    check("sys-programs", ok, "r=%d count=%u id=%u by=%u job=%u mem=%uK windows=%u", r, count, me < count ? progs[me].id : 0,
          me < count ? progs[me].by : 0, me < count ? progs[me].job : 0, me < count ? progs[me].mem_kb : 0,
          me < count ? progs[me].windows : 0);

    /* The log: from its oldest line; then followed, a line of the test's own making. */
    n = glos_sys_log(0, text, sizeof text - 1, &next, 0);
    text[n > 0 ? n : 0] = 0;
    ok = n > 20 && strstr(text, "GLOS-") && next >= (unsigned)n;
    while (n > 0)                                       /* (to its end) */
        n = glos_sys_log(next, text, sizeof text - 1, &next, 1);
    while (glos_event(ev, sizeof ev, 0, 1u << GLOS_AREA_SYS) > 0) ;
    memset(&again, 0, sizeof again);
    again.size = sizeof again;
    again.major = 2;
    glos_call(GLOS_CORE_INIT, &again);                  /* (refused: the kernel says so in its log) */
    r = await(GLOS_EV_LOG, 0, ev, 3000);
    n = glos_sys_log(next, text, sizeof text - 1, &next, 0);
    text[n > 0 ? n : 0] = 0;
    glos_call(GLOS_CORE_INIT, &again);                  /* (no longer followed: no event) */
    check("sys-log", ok && r > 0 && n > 0 && strstr(text, "GLOS-APP refuse") && glos_event(ev, sizeof ev, 300, 1u << GLOS_AREA_SYS) == 0
          && glos_sys_log(0, (char *)0xFFFFFF00u, 100, &next, 0) == -1, "ok=%d r=%d n=%d", ok, r, n);

    /* Asked to end: the event; what KILL refuses. */
    while (glos_event(ev, sizeof ev, 0, 0) > 0) ;
    r = glos_sys_kill(GLOS_KILL_ASK, progs[me < count ? me : 0].id);
    n = glos_event(ev, sizeof ev, 1000, 1u << GLOS_AREA_CORE);
    ok = r == 0 && n > 0 && e->kind == GLOS_EV_END && glos_sys_kill(GLOS_KILL_ASK, 9999) == GLOS_E_NOT_FOUND
         && glos_sys_kill(GLOS_KILL_APP, 9999) == GLOS_E_NOT_FOUND && glos_sys_kill(GLOS_KILL_JOB, 9999) == GLOS_E_NOT_FOUND
         && glos_sys_kill(7, 1) == GLOS_E_ARGUMENT;
    check("sys-kill", ok, "r=%d n=%d kind=%x", r, n, n > 0 ? e->kind : 0);
}

/* SHELL (step 8): the shell's identity, where the task bar's parts are, the tray, notifications, kinds of file
   and their programs and verbs, OPEN, HELP. What the user's hands do to an icon and a notification is the shell
   mode's (shell_mode, under the agent). */
#define OPENED hp("%s\\OPENED.TXT")

static int opened_is(const char *want, unsigned ms)    /* the program OPEN started has said what it was given */
{
    unsigned t0 = glos_ticks();
    char got[200];
    for (;;) {
        FILE *f = fopen(OPENED, "r");
        got[0] = 0;
        if (f) {
            if (!fgets(got, sizeof got, f))
                got[0] = 0;
            fclose(f);
            if (strchr(got, '\n')) {                    /* (written whole) */
                *strchr(got, '\n') = 0;
                remove(OPENED);
                if (strcmp(got, want))
                    printf("opened: got \"%s\" want \"%s\"\n", got, want);
                return !strcmp(got, want);
            }
        }
        if (glos_ticks() - t0 >= ms)
            return 0;
        glos_sleep(200);
    }
}

static void shell(void)
{
    static unsigned icon[256];
    static char longtip[80], longtitle[60];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_point bar;
    unsigned text[1] = { GLOS_CLIP_TEXT }, i;
    glos_handle t, n, n2, many[16];
    char buf[128];
    int r, e, ok, job;

    for (i = 0; i < 256; i++)
        icon[i] = 0xFF2060C0u;
    memset(longtip, 'x', sizeof longtip - 1);
    memset(longtitle, 'y', sizeof longtitle - 1);

    /* The shell: one of GLOS's own programs (this one is in GLOS's folder); the bare desktop's drops are its. */
    r = glos_shell(1);
    e = glos_shell(1);
    ok = r == 0 && e == 0 && glos_dnd_target(0, GLOS_DND_COPY, text, 1) == 0 && glos_dnd_target(0, 0, 0, 0) == 0;
    check("shell-on", ok, "r=%d e=%d", r, e);
    r = glos_shell(0);
    check("shell-off", r == 0 && glos_dnd_target(0, GLOS_DND_COPY, text, 1) == GLOS_E_REFUSED, "r=%d", r);

    check("point-bad", glos_point(0, &bar) == GLOS_E_ARGUMENT && glos_point(6, &bar) == GLOS_E_ARGUMENT, 0);

    /* The tray (GLOS /RUN has no screen: where an icon is, and a notification's time, are the store mode's). */
    t = glos_tray(icon, "APITEST's icon");
    buf[0] = 0;
    e = glos_get_text(t, GLOS_PROP_TRAY_TIP, buf, sizeof buf);
    ok = t && GLOS_HANDLE_KIND(t) == GLOS_HK_TRAY && e >= 0 && !strcmp(buf, "APITEST's icon");
    check("tray", ok, "t=%x e=%d tip=%s", t, e, buf);
    r = glos_set_text(t, GLOS_PROP_TRAY_TIP, "Another");
    glos_get_text(t, GLOS_PROP_TRAY_TIP, buf, sizeof buf);
    e = glos_set_data(t, GLOS_PROP_TRAY_ICON, icon, sizeof icon);
    ok = r == 0 && !strcmp(buf, "Another") && e == 0 && glos_set_data(t, GLOS_PROP_TRAY_ICON, icon, 100) == GLOS_E_ARGUMENT
         && glos_set_text(t, GLOS_PROP_TRAY_TIP, longtip) == GLOS_E_ARGUMENT && glos_set_text(t, GLOS_PROP_TRAY_TIP, "") == 0;
    check("tray-props", ok, "r=%d e=%d tip=%s", r, e, buf);
    ok = glos_tray(0, 0) == 0 && glos_error() == GLOS_E_ARGUMENT && glos_tray(icon, longtip) == 0
         && glos_error() == GLOS_E_ARGUMENT;
    for (i = 0; i < GLOS_TRAY_MAX - 1; i++)
        ok &= (many[i] = glos_tray(icon, 0)) != 0;
    ok &= glos_tray(icon, 0) == 0 && glos_error() == GLOS_E_MEMORY;
    for (i = 0; i < GLOS_TRAY_MAX - 1; i++)
        ok &= glos_close(many[i]) == 0;
    ok &= glos_close(t) == 0 && glos_close(t) == GLOS_E_HANDLE;
    check("tray-full", ok, "err=%d", glos_error());

    /* Notifications: closed while shown or waiting; what one may not be; too many. */
    while (glos_event(ev, sizeof ev, 0, 0) > 0) ;
    n = glos_notify("Shown", 0, 0, 0, 0, 0);
    n2 = glos_notify("Waiting", "It never has its turn.", 0, 0, 0, 3);
    glos_sleep(300);
    ok = n && n2 && glos_close(n) == 0 && glos_close(n2) == 0 && glos_event(ev, sizeof ev, 500, 1u << GLOS_AREA_SHELL) == 0;
    check("notify-closed", ok, "n=%x n2=%x", n, n2);
    ok = glos_notify(0, "x", 0, 0, 0, 0) == 0 && glos_error() == GLOS_E_ARGUMENT && glos_notify("", 0, 0, 0, 0, 0) == 0
         && glos_notify("t", 0, 0, 0, 0, 1) == 0 && glos_error() == GLOS_E_ARGUMENT && glos_notify("t", 0, 0, 0, 0, 61) == 0
         && glos_notify("t", 0, 0, "No", 0, 0) == 0 && glos_notify(longtitle, 0, 0, 0, 0, 0) == 0
         && glos_notify("t", 0, "A label too long by far", 0, 0, 0) == 0 && glos_error() == GLOS_E_ARGUMENT;
    check("notify-bad", ok, "err=%d", glos_error());
    ok = 1;
    for (i = 0; i < 8; i++)
        ok &= (many[i] = glos_notify("Many", 0, 0, 0, 0, 60)) != 0;
    ok &= glos_notify("One too many", 0, 0, 0, 0, 0) == 0 && glos_error() == GLOS_E_MEMORY;
    for (i = 0; i < 8; i++)
        ok &= glos_close(many[i]) == 0;
    check("notify-many", ok && (n = glos_notify("Room again", 0, 0, 0, 0, 0)) != 0 && glos_close(n) == 0, "err=%d", glos_error());

    /* Under GLOS /RUN no setting is kept and nothing can be started (the store mode has these, under the agent).
       Under the agent (a bench PC's run) the first set succeeds: it is undone, and the rest is not tried. */
    r = glos_assoc_set(".ZZT", "APITEST.EXE");
    if (r == 0) {
        check("run-refused", glos_assoc_set(".ZZT", 0) == 0, "(the agent's: settings are kept)");
    } else {
        job = glos_open(hp("%s\\APITEST.EXE"), 0, 0);
        e = glos_schedule_set("never", "start", "C:\\NEVER.BAT");
        if (glos_job_run("C:\\NEVER.BAT", 0, 0, GLOS_RUN_HIDDEN, 0, 0) != 0 || glos_error() != GLOS_E_REFUSED)
            e = -1;
        check("run-refused", r == GLOS_E_SETTING && job == -1 && glos_error() == GLOS_E_REFUSED && e == GLOS_E_REFUSED
              && glos_entry_set("APITEST", 0, 0, 0) == GLOS_E_SETTING, "r=%d job=%d err=%d e=%d", r, job, glos_error(), e);
    }

    /* Help is an app of phase E's: not there yet. */
    r = glos_help("windows");
    check("help", r == GLOS_E_NOT_FOUND && glos_help(0) == GLOS_E_NOT_FOUND, "r=%d", r);
}

/* The test's own profile, as the entry's cases leave it: out of GLOS.CFG. */
static void forget_profile(void)
{
    static const char *const key[] = { "title", "group", "icon", "args", "path", "tasks", "verbs", "menu" };
    char why[80];
    unsigned i;
    for (i = 0; i < sizeof key / sizeof key[0]; i++)
        glos_setting_set("program APITEST.EXE", key[i], 0, 0, why, sizeof why);
}

/* SHELL on a screen (under the agent, the display on): where the task bar's parts and a tray icon are, a
   notification shown for its time. */
static void on_screen(void)
{
    static unsigned icon[256];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev_notify *ne = (struct glos_ev_notify *)ev;
    struct glos_point bar, work, start, clock, tray;
    struct glos_screen scr;
    unsigned t0, took, i;
    glos_handle t, n;
    int r, e, ok;
    for (i = 0; i < 256; i++)
        icon[i] = 0xFF2060C0u;
    glos_screen(&scr);
    /* Where the task bar's parts are. */
    r = glos_point(GLOS_POINT_BAR, &bar);
    r |= glos_point(GLOS_POINT_WORK, &work) | glos_point(GLOS_POINT_START, &start) | glos_point(GLOS_POINT_CLOCK, &clock);
    e = glos_point(GLOS_POINT_TRAY, &tray);
    ok = r == 0 && bar.x == 0 && bar.w == scr.w && bar.y + bar.h == scr.h && work.x == 0 && work.y == 0 && work.w == scr.w
         && work.h == bar.y && start.x == 0 && start.y == bar.y && start.w > 8 && start.w < 100 && clock.y == bar.y
         && clock.x > scr.w / 2 && clock.x + clock.w <= scr.w && clock.w > 8 && e == 0 && tray.w == 0;
    check("point", ok, "r=%d bar=%d,%d %dx%d work=%dx%d start=%d clock=%d,%d tray=%d", r, bar.x, bar.y, bar.w, bar.h, work.w,
          work.h, start.w, clock.x, clock.w, tray.w);
    t = glos_tray(icon, "APITEST's icon");
    r = glos_point(GLOS_POINT_TRAY, &tray);
    ok = t && r == 0 && tray.w >= 16 && tray.w <= 24 && tray.y == bar.y && tray.x + tray.w <= clock.x + 2;
    glos_close(t);
    ok = ok && glos_point(GLOS_POINT_TRAY, &bar) == 0 && bar.w == 0;
    check("tray-place", ok, "t=%x r=%d at=%d,%d w=%d clock=%d after=%d", t, r, tray.x, tray.y, tray.w, clock.x, bar.w);
    /* A notification shown for its time. */
    while (glos_event(ev, sizeof ev, 0, 0) > 0) ;
    t0 = glos_ticks();
    n = glos_notify("APITEST", "A notification of the test's.\nIts second line.", "Yes", "No", icon, 2);
    r = await(GLOS_EV_NOTIFY, n, ev, 6000);
    took = glos_ticks() - t0;
    check("notify-timeout", n && GLOS_HANDLE_KIND(n) == GLOS_HK_NOTIFY && r > 0 && ne->what == GLOS_NOTIFY_TIMEOUT
          && took >= 1900 && took < 4000 && glos_close(n) == 0, "n=%x err=%d r=%d what=%u took=%u", n, glos_error(), r,
          r > 0 ? ne->what : 0, took);
}

/* SHELL's calls that keep settings and start programs (under the agent): kinds of file, verbs, OPEN. */
static int store_mode(void)
{
    static char list[512];
    unsigned len = 0;
    char buf[128], line[64];
    int r, e, ok, job;
    FILE *f;
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 98;
    on_screen();
    /* Kinds of file and their programs. */
    r = glos_assoc_set(".ZZT", "APITEST.EXE");
    buf[0] = 'x';
    e = glos_assoc_get("C:\\DOCS\\Letter.zzt", buf, sizeof buf);
    ok = r == 0 && e == 0 && !strcmp(buf, "APITEST.EXE") && glos_assoc_get("zzt", buf, 4) == GLOS_E_ROOM
         && glos_assoc_get("NOKIND", buf, sizeof buf) == GLOS_E_ARGUMENT && glos_assoc_get(".LONGER", buf, sizeof buf) == GLOS_E_ARGUMENT;
    check("assoc", ok, "r=%d e=%d program=%s", r, e, buf);

    /* Verbs: this program's own, in its profile. */
    r = glos_assoc_verb(".ZZT", "Say it", "said");
    e = glos_assoc_verb("zzt", "Show", "");
    glos_assoc_verb(".ZZU", "Other kind", "x");
    list[0] = 0;
    ok = r == 0 && e == 0 && glos_assoc_verbs("A.ZZT", list, sizeof list, &len) == 0 && len == strlen(list)
         && strstr(list, "Say it\tAPITEST.EXE said\n") && strstr(list, "Show\tAPITEST.EXE\n") && !strstr(list, "Other")
         && glos_assoc_verbs(".ZZT", list, 8, &len) == GLOS_E_ROOM && len > 8
         && glos_assoc_verb(".ZZT", "Bad;label", "x") == GLOS_E_ARGUMENT && glos_assoc_verb(".ZZT", "Bad", "a;b") == GLOS_E_ARGUMENT;
    check("verbs", ok, "r=%d e=%d len=%u list=%s", r, e, len, list);

    /* OPEN: by the kind's program, by a verb, with a program named; a link; what can't be opened. */
    remove(OPENED);
    if ((f = fopen(hp("%s\\OPENME.ZZT"), "w")) != 0) {
        fputs("open me\n", f);
        fclose(f);
    }
    job = glos_open(hp("%s\\OPENME.ZZT"), 0, 0);
    check("open-kind", job > 0 && opened_is(hp("%s\\OPENME.ZZT"), 60000), "job=%d err=%d", job, glos_error());
    job = glos_open(hps("%sopenme.zzt"), "APITEST.EXE", "say it");
    check("open-verb", job > 0 && opened_is(hp("said %s\\openme.zzt"), 60000), "job=%d err=%d", job, glos_error());
    job = glos_open(hp("%s\\OPENME.ZZT"), hp("%s\\APITEST.EXE with"), 0);
    check("open-with", job > 0 && opened_is(hp("with %s\\OPENME.ZZT"), 60000), "job=%d err=%d", job, glos_error());
    r = glos_open("http://example.com/a.zzt", 0, 0);
    e = glos_error();
    glos_assoc_set(".URL", "APITEST.EXE");
    job = glos_open("http://example.com/a.zzt", 0, 0);
    check("open-link", r == -1 && e == GLOS_E_NOT_FOUND && job > 0 && opened_is("http://example.com/a.zzt", 60000),
          "r=%d e=%d job=%d err=%d", r, e, job, glos_error());
    glos_assoc_set(".URL", 0);
    ok = glos_open(hp("%s\\NOSUCH.ZZT"), 0, 0) == -1 && glos_error() == GLOS_E_NOT_FOUND
         && glos_open(hp("%s\\*.ZZT"), 0, 0) == -1 && glos_error() == GLOS_E_NOT_FOUND
         && glos_open(hp("%s\\OPENME.ZZT"), "APITEST.EXE", "No such verb") == -1 && glos_error() == GLOS_E_NOT_FOUND
         && glos_open(hp("%s\\OPENME.ZZT"), 0, "Say it") == -1 && glos_error() == GLOS_E_ARGUMENT
         && glos_open(0, 0, 0) == -1 && glos_error() == GLOS_E_ARGUMENT;
    r = glos_assoc_set(".ZZT", 0);
    buf[0] = 'x';
    e = glos_assoc_get(".ZZT", buf, sizeof buf);
    ok &= r == 0 && e == 0 && !buf[0] && glos_open(hp("%s\\OPENME.ZZT"), 0, 0) == -1 && glos_error() == GLOS_E_NOT_FOUND;
    check("open-refused", ok, "r=%d e=%d err=%d program=%s", r, e, glos_error(), buf);
    remove(hp("%s\\OPENME.ZZT"));

    /* The verbs taken away again: the profile is as it was. */
    r = glos_assoc_verb(".ZZT", "Say it", 0);
    ok = r == 0 && glos_assoc_verb(".ZZT", "Say it", 0) == GLOS_E_NOT_FOUND && glos_assoc_verb(".ZZT", "show", 0) == 0
         && glos_assoc_verb(".ZZU", "Other kind", 0) == 0 && glos_assoc_verbs(".ZZT", list, sizeof list, &len) == 0 && len == 0;
    check("verbs-gone", ok, "r=%d len=%u list=%s", r, len, list);

    /* The recent documents: OPEN's (the three files' opens above, the newest first, each path once), the app's
       own, by program, forgotten. */
    list[0] = 0;
    r = glos_recent_list(0, list, sizeof list, &len);
    check("recent-open", r == 0 && len == strlen(list) && strstr(list, hp("%s\\OPENME.ZZT\tAPITEST.EXE\n")) == list
          && strstr(list, hps("%sopenme.zzt\tAPITEST.EXE\n")) && !strstr(list, "http:"), "r=%d len=%u list=%s", r, len, list);
    glos_recent_clear(0);
    r = glos_recent_add("C:\\DOCS\\One.txt");
    glos_recent_add("C:\\DOCS\\Two.txt");
    e = glos_recent_add("c:\\docs\\ONE.TXT");            /* (the same file: its place changes, and its spelling) */
    list[0] = 0;
    ok = r == 0 && e == 0 && glos_recent_list(0, list, sizeof list, &len) == 0
         && !strcmp(list, "c:\\docs\\ONE.TXT\tAPITEST.EXE\nC:\\DOCS\\Two.txt\tAPITEST.EXE\n")
         && glos_recent_list("apitest.exe", list, sizeof list, &len) == 0 && len == 56
         && glos_recent_list("OTHER.EXE", list, sizeof list, &len) == 0 && len == 0
         && glos_recent_list(0, list, 10, &len) == GLOS_E_ROOM && len == 56
         && glos_recent_add("") == GLOS_E_ARGUMENT && glos_recent_add("a\tb") == GLOS_E_ARGUMENT;
    check("recent", ok, "r=%d e=%d len=%u list=%s", r, e, len, list);
    for (r = 0; r < GLOS_RECENT_MAX + 5; r++) {         /* more than it keeps: the oldest go */
        snprintf(buf, sizeof buf, "C:\\DOCS\\F%d.TXT", r);
        glos_recent_add(buf);
    }
    {
        static char all[4096];
        int lines = 0;
        char *c;
        e = glos_recent_list(0, all, sizeof all, &len);
        for (c = all; *c; c++)
            lines += *c == '\n';
        ok = e == 0 && lines == GLOS_RECENT_MAX && !strncmp(all, "C:\\DOCS\\F36.TXT\t", 16) && !strstr(all, "F4.TXT")
             && strstr(all, "F5.TXT") && !strstr(all, "Two.txt");
        check("recent-most", ok, "e=%d lines=%d len=%u", e, lines, len);
    }
    r = glos_recent_clear("C:\\DOCS\\F36.TXT");
    e = glos_recent_clear("C:\\DOCS\\F36.TXT");
    glos_recent_list(0, list, sizeof list, &len);
    ok = r == 0 && e == GLOS_E_NOT_FOUND && !strncmp(list, "C:\\DOCS\\F35.TXT\t", 16) && glos_recent_clear(0) == 0
         && glos_recent_list(0, list, sizeof list, &len) == 0 && len == 0;
    check("recent-clear", ok, "r=%d e=%d len=%u", r, e, len);

    /* The Start menu's entry: this program's, its tasks; every entry listed. */
    r = glos_entry_set("API test", 0, "tool", "shell");
    list[0] = 0;
    e = glos_entry_list(list, sizeof list, &len);
    ok = r == 0 && e == 0 && len == strlen(list) && strstr(list, hp("APITEST.EXE\tAPI test\tTests\ttool\t%s\tshell\t\n"));
    check("entry", ok, "r=%d e=%d len=%u list=%s", r, e, len, list);
    r = glos_entry_task("New window", "win");
    e = glos_entry_task("Plain", "");
    glos_entry_set(0, "Tools", 0, 0);
    glos_entry_list(list, sizeof list, &len);
    ok = r == 0 && e == 0 && strstr(list, hp("APITEST.EXE\tAPI test\tTools\ttool\t%s\tshell\tNew window=win;Plain=\n"))
         && glos_entry_task("new WINDOW", 0) == 0 && glos_entry_task("New window", 0) == GLOS_E_NOT_FOUND
         && glos_entry_task("Plain", 0) == 0 && glos_entry_task("a=b", "x") == GLOS_E_ARGUMENT
         && glos_entry_set(0, 0, "no such picture", 0) == GLOS_E_SETTING && glos_entry_list(list, 4, &len) == GLOS_E_ROOM && len > 4;
    check("entry-tasks", ok, "r=%d e=%d list=%s", r, e, list);
    r = glos_entry_remove();
    e = glos_entry_list(list, sizeof list, &len);
    check("entry-removed", r == 0 && e == 0 && !strstr(list, "APITEST.EXE"), "r=%d e=%d list=%s", r, e, list);
    forget_profile();                                   /* (nothing of the test's is left in GLOS.CFG) */

    /* The schedule: set, changed, listed, removed; what it refuses. (Its running is the schedule case's.) */
    r = glos_schedule_set("nightly", "Daily 3:00", "C:\\TEST\\APITEST.EXE said nightly");
    e = glos_schedule_set("boot", "start", "C:\\TEST\\APITEST.EXE said started");
    list[0] = 0;
    ok = r == 0 && e == 0 && glos_schedule_list(list, sizeof list, &len) == 0 && len == strlen(list)
         && !strcmp(list, "nightly\tdaily 03:00\tC:\\TEST\\APITEST.EXE said nightly\t0\t0\n"
                          "boot\tstart\tC:\\TEST\\APITEST.EXE said started\t0\t0\n");
    check("schedule", ok, "r=%d e=%d len=%u list=%s", r, e, len, list);
    r = glos_schedule_set("NIGHTLY", "weekly sat 23:30", "C:\\X.BAT");
    glos_schedule_list(list, sizeof list, &len);
    ok = r == 0 && !strncmp(list, "NIGHTLY\tweekly sat 23:30\tC:\\X.BAT\t0\t0\nboot\t", 43)
         && glos_schedule_set("two words", "start", "x") == GLOS_E_ARGUMENT && glos_schedule_set("x", "hourly", "x") == GLOS_E_ARGUMENT
         && glos_schedule_set("x", "daily 25:00", "x") == GLOS_E_ARGUMENT && glos_schedule_set("x", "start", "") == GLOS_E_ARGUMENT
         && glos_schedule_set("x", "start", "a\tb") == GLOS_E_ARGUMENT && glos_schedule_remove("nosuch") == GLOS_E_NOT_FOUND
         && glos_schedule_list(list, 8, &len) == GLOS_E_ROOM && len > 8;
    check("schedule-change", ok, "r=%d list=%s", r, list);
    r = glos_schedule_remove("nightly");
    e = glos_schedule_remove("boot");
    check("schedule-remove", r == 0 && e == 0 && glos_schedule_list(list, sizeof list, &len) == 0 && len == 0, "r=%d e=%d len=%u",
          r, e, len);

    snprintf(line, sizeof line, "HX-APITEST done pass=%d fail=%d\r\n", pass, fails);
    com1(line);
    return fails > 90 ? 90 : fails;
}

/* The schedule's running (under the agent, over two starts of GLOS): "set" schedules a command at each start and
   one daily at 03:00; "clear" takes them away. */
static int sched_mode(const char *what)
{
    int r, e;
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 98;
    if (!strcmp(what, "set")) {
        r = glos_schedule_set("boot", "start", hp("%s\\APITEST.EXE said started"));
        e = glos_schedule_set("nightly", "daily 03:00", hp("%s\\APITEST.EXE said nightly"));
    } else {
        r = glos_schedule_remove("boot");
        e = glos_schedule_remove("nightly");
    }
    printf("SCHED %s r=%d e=%d\n", what, r, e);
    return r || e;
}

/* JOB (step 9), under the agent: programs run for the app (tests/api/jobtool.c is the program). */
static void no_cr(char *t)
{
    char *o = t;
    for (; *t; t++)
        if (*t != '\r')
            *o++ = *t;
    *o = 0;
}

/* What the job has written is added to out and err (text, their rooms); 1 once it has ended and all is read, or
   once `until` is in out; 0 when the time is up. */
static int file_is(const char *path, const char *want)  /* a text file's whole contents are these */
{
    static char got[1024];
    FILE *f = fopen(path, "r");
    size_t n;
    if (!f)
        return 0;
    n = fread(got, 1, sizeof got - 1, f);
    got[n] = 0;
    fclose(f);
    if (strcmp(got, want))
        printf("file_is %s: got \"%s\"\n", path, got);
    return !strcmp(got, want);
}

static unsigned job_events, job_last;           /* the job's events seen while waiting, and the last one's state */

static int job_take(glos_handle j, char *out, unsigned room, char *err, unsigned eroom, const char *until, unsigned ms,
                    struct glos_job_status *st)
{
    unsigned char ev[GLOS_EV_MAX];
    unsigned t0 = glos_ticks(), no = strlen(out), ne = err ? strlen(err) : 0;
    int n;
    for (;;) {
        while (no + 1 < room && (n = glos_job_read(j, GLOS_JOB_OUT, out + no, room - 1 - no)) > 0)
            no += (unsigned)n;
        while (err && ne + 1 < eroom && (n = glos_job_read(j, GLOS_JOB_ERR, err + ne, eroom - 1 - ne)) > 0)
            ne += (unsigned)n;
        out[no] = 0;
        if (err)
            err[ne] = 0;
        if (glos_job_status(j, st) != 0)
            return 0;
        if (until ? strstr(out, until) || (err && strstr(err, until)) : st->state == GLOS_JS_DONE && !st->out && (!err || !st->err))
            return 1;
        if (until && st->state == GLOS_JS_DONE && !st->out && !st->err)
            return 0;
        if (glos_ticks() - t0 > ms)
            return 0;
        if (glos_event(ev, sizeof ev, 100, 1u << GLOS_AREA_JOB) > 0 && ((struct glos_ev *)ev)->kind == GLOS_EV_JOB
            && ((struct glos_ev *)ev)->handle == j) {
            job_events++;
            job_last = ((struct glos_ev_job *)ev)->state;
        }
    }
}

static int job_mode(void)
{
    static char out[8192], err[2048];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev_job *je = (struct glos_ev_job *)ev;
    struct glos_job_status st, st2;
    struct glos_job_run bad;
    unsigned short keys[3] = { 0x1E61, 0x3062, 0x342E };        /* a, b, a full stop */
    glos_handle j, j2;
    unsigned t0;
    char line[64];
    int r, e, ok, n;
    FILE *f;
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 98;
    job_events = 0;
#define TOOL(args) hp("%s\\JOBTOOL.EXE " args)

    /* A program with arguments: its output read, its end told, its code. */
    out[0] = 0;
    j = glos_job_run(TOOL("args one two"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    r = job_take(j, out, sizeof out, 0, 0, 0, 60000, &st);
    no_cr(out);
    check("job-run", j && GLOS_HANDLE_KIND(j) == GLOS_HK_JOB && r && !strcmp(out, "3 [args] [one] [two]\n") && st.code == 0
          && st.state == GLOS_JS_DONE && st.id > 0 && st.ms > 0 && st.ms < 60000, "j=%x err=%d r=%d out=%s code=%u ms=%u", j,
          glos_error(), r, out, st.code, st.ms);
    n = (int)job_events;                                /* (its events came while it was waited for) */
    while (glos_event(ev, sizeof ev, 300, 1u << GLOS_AREA_JOB) > 0)
        n++, job_last = je->state;
    check("job-events", n >= 2 && job_last == GLOS_JS_DONE && glos_close(j) == 0 && glos_job_status(j, &st) == GLOS_E_HANDLE,
          "events=%d last=%u", n, job_last);

    /* Its folder and its environment. */
    out[0] = 0;
    j = glos_job_run(TOOL("where JT"), "C:\\", "JT=hello\0", GLOS_RUN_HIDDEN, 0, 0);
    r = job_take(j, out, sizeof out, 0, 0, 0, 60000, &st);
    check("job-where", r && (strstr(out, "cwd=c:/ ") || strstr(out, "cwd=C:/ ") || strstr(out, "cwd=C:\\ ")) && strstr(out, "JT=hello"),
          "r=%d out=%s", r, out);
    glos_close(j);

    /* Its two outputs apart; its exit code. */
    out[0] = err[0] = 0;
    j = glos_job_run(TOOL("both"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    r = job_take(j, out, sizeof out, err, sizeof err, 0, 60000, &st);
    no_cr(out), no_cr(err);
    check("job-streams", r && !strcmp(out, "to out\n") && !strcmp(err, "to err\n") && st.code == 3, "r=%d out=%s err=%s code=%u",
          r, out, err, st.code);
    glos_close(j);

    /* Its standard input from the app: a line at a time, then its end. */
    out[0] = err[0] = 0;
    j = glos_job_run(TOOL("echo"), 0, 0, GLOS_RUN_HIDDEN, GLOS_RUNF_STDIN, 0);
    r = glos_job_write(j, "one\n", 4);
    ok = r == 4 && job_take(j, out, sizeof out, err, sizeof err, "ONE", 60000, &st) && st.state != GLOS_JS_DONE;
    r = glos_job_write(j, "!two\r\nthr", 9);
    ok = ok && r == 9 && job_take(j, out, sizeof out, err, sizeof err, "!TWO", 30000, &st);
    r = glos_job_write(j, "ee\n", 3);
    e = glos_job_eof(j);
    ok = ok && r == 3 && e == 0 && job_take(j, out, sizeof out, err, sizeof err, 0, 30000, &st);
    no_cr(out), no_cr(err);
    check("job-stdin", ok && !strcmp(out, "ONE\n!TWO\nTHREE\nend 3\n") && !strcmp(err, "!TWO\n") && st.code == 3,
          "ok=%d out=%s err=%s code=%u", ok, out, err, st.code);
    glos_close(j);

    /* ... and a character at a time, through DOS's console calls. */
    out[0] = 0;
    j = glos_job_run(TOOL("chars"), 0, 0, GLOS_RUN_HIDDEN, GLOS_RUNF_STDIN, 0);
    r = glos_job_write(j, "xy.", 3);
    ok = job_take(j, out, sizeof out, 0, 0, 0, 60000, &st);
    no_cr(out);
    check("job-chars", r == 3 && ok && !strcmp(out, "<x><y>\n") && st.code == 2, "r=%d ok=%d out=%s code=%u", r, ok, out, st.code);
    glos_close(j);

    /* Ctrl+C and Ctrl+Break, half way through a long run. */
    out[0] = 0;
    j = glos_job_run(TOOL("stream 2000"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    ok = job_take(j, out, sizeof out, 0, 0, "line 5\r", 60000, &st);
    r = glos_job_signal(j, GLOS_SIGNAL_INT);
    t0 = glos_ticks();
    e = job_take(j, out, sizeof out, 0, 0, 0, 30000, &st);
    check("job-ctrl-c", ok && r == 0 && e && strstr(out, "interrupted") && st.code == 7 && !strstr(out, "line 400"),
          "ok=%d r=%d e=%d code=%u took=%u tail=%s", ok, r, e, st.code, glos_ticks() - t0, out + (strlen(out) > 30 ? strlen(out) - 30 : 0));
    glos_close(j);
    out[0] = 0;
    j = glos_job_run(TOOL("stream 2000"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    ok = job_take(j, out, sizeof out, 0, 0, "line 5\r", 60000, &st);
    r = glos_job_signal(j, GLOS_SIGNAL_BREAK);
    e = job_take(j, out, sizeof out, 0, 0, 0, 30000, &st);
    check("job-ctrl-break", ok && r == 0 && e && strstr(out, "interrupted") && st.code == 7, "ok=%d r=%d e=%d code=%u", ok, r, e,
          st.code);
    glos_close(j);

    /* Ended, whatever it is doing; and one that waits its turn behind another. */
    out[0] = 0;
    j = glos_job_run(TOOL("stream 100000"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    j2 = glos_job_run(TOOL("args behind"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    ok = job_take(j, out, sizeof out, 0, 0, "line 3\r", 60000, &st);
    e = glos_job_status(j2, &st2);
    ok = ok && e == 0 && st2.state == GLOS_JS_QUEUED && glos_job_signal(j2, GLOS_SIGNAL_INT) == GLOS_E_REFUSED;
    r = glos_job_signal(j, GLOS_SIGNAL_END);
    t0 = glos_ticks();
    e = job_take(j, out, sizeof out, 0, 0, 0, 30000, &st);
    check("job-end", ok && r == 0 && e && st.state == GLOS_JS_DONE && st.code != 0 && glos_ticks() - t0 < 15000,
          "ok=%d r=%d e=%d code=%u took=%u", ok, r, e, st.code, glos_ticks() - t0);
    out[0] = 0;
    e = job_take(j2, out, sizeof out, 0, 0, 0, 60000, &st2);
    no_cr(out);
    check("job-queue", e && !strcmp(out, "2 [args] [behind]\n") && glos_job_signal(j2, GLOS_SIGNAL_INT) == GLOS_E_REFUSED,
          "e=%d out=%s", e, out);
    glos_close(j);
    glos_close(j2);

    /* A command line longer than DOS passes: all of its arguments arrive. */
    {
        static char longcmd[1024], want[1024];
        int k, at = snprintf(longcmd, sizeof longcmd, "%s\\JOBTOOL.EXE args", home), w = snprintf(want, sizeof want, "61 [args]");
        for (k = 0; k < 60; k++) {
            at += snprintf(longcmd + at, sizeof longcmd - at, " arg%02d", k);
            w += snprintf(want + w, sizeof want - w, " [arg%02d]", k);
        }
        snprintf(want + w, sizeof want - w, "\n");
        out[0] = 0;
        j = glos_job_run(longcmd, 0, "JT=kept\0", GLOS_RUN_HIDDEN, 0, 0);
        r = job_take(j, out, sizeof out, 0, 0, 0, 60000, &st);
        no_cr(out);
        check("job-long", j && r && !strcmp(out, want) && st.code == 0, "j=%x err=%d r=%d len=%d code=%u out=%.60s", j, glos_error(),
              r, at, st.code, out);
        glos_close(j);
    }

    /* Files for its streams: its input from one, its output made and then added to, its error to a file of its
       own; output and error into one file; a file that isn't there. */
    {
        struct glos_job_run q;
        char in[100], o1[100], o2[100];
        snprintf(in, sizeof in, "%s\\JOBIN.TXT", home);
        snprintf(o1, sizeof o1, "%s\\JOBOUT.TXT", home);
        snprintf(o2, sizeof o2, "%s\\JOBERR.TXT", home);
        if ((f = fopen(in, "w")) != 0) {
            fputs("alpha\n!beta\n", f);
            fclose(f);
        }
        remove(o1), remove(o2);
        memset(&q, 0, sizeof q);
        q.command = (unsigned)TOOL("echo");
        q.in = (unsigned)in, q.out = (unsigned)o1, q.err = (unsigned)o2;
        out[0] = err[0] = 0;
        j = glos_job_start(&q);
        r = job_take(j, out, sizeof out, err, sizeof err, 0, 60000, &st);
        glos_close(j);
        ok = j && r && st.code == 2 && !out[0] && !err[0] && file_is(o1, "ALPHA\n!BETA\nend 2\n") && file_is(o2, "!BETA\n");
        memset(&q, 0, sizeof q);                        /* added to; and one file for both, in the job's folder */
        q.command = (unsigned)TOOL("both");
        q.dir = (unsigned)home;
        q.out = (unsigned)"JOBOUT.TXT", q.err = (unsigned)"jobout.txt";
        q.flags = GLOS_RUNF_APPEND_OUT;
        j = glos_job_start(&q);
        r = job_take(j, out, sizeof out, err, sizeof err, 0, 60000, &st);
        glos_close(j);
        check("job-files", ok && j && r && st.code == 3 && !out[0] && !err[0]
              && file_is(o1, "ALPHA\n!BETA\nend 2\nto out\nto err\n"), "ok=%d j=%x r=%d code=%u out=%s err=%s", ok, j, r,
              st.code, out, err);
        memset(&q, 0, sizeof q);
        q.command = (unsigned)TOOL("echo");
        q.in = (unsigned)"C:\\NOSUCH\\INPUT.TXT";
        j = glos_job_start(&q);
        r = job_take(j, out, sizeof out, 0, 0, 0, 60000, &st);
        glos_close(j);
        ok = j && r && st.code == 126;
        q.in = (unsigned)in;                            /* (a file and the app's writes both: no) */
        q.flags = GLOS_RUNF_STDIN;
        ok = ok && glos_job_start(&q) == 0 && glos_error() == GLOS_E_ARGUMENT;
        out[0] = 0;                                     /* (the console's handles are the stub's own again) */
        j = glos_job_run(TOOL("args back"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
        r = job_take(j, out, sizeof out, 0, 0, 0, 60000, &st);
        glos_close(j);
        check("job-files-none", ok && r && strstr(out, "[back]"), "ok=%d r=%d code=%u out=%s", ok, r, st.code, out);
        remove(in), remove(o1), remove(o2);
    }

    /* SYS: a running job among the programs, with who started it; ended by KILL. */
    {
        static struct glos_program progs[16];
        unsigned count = 0, i, mine = 0, by = 0, self = 0;
        out[0] = 0;
        j = glos_job_run(TOOL("stream 100000"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
        ok = job_take(j, out, sizeof out, 0, 0, "line 3\r", 60000, &st);
        r = glos_programs(progs, sizeof progs, &count);
        for (i = 0; i < count; i++) {
            if (progs[i].kind == GLOS_PROG_APP && !strcmp(progs[i].name, "APITEST.EXE"))
                self = progs[i].id;
            if (progs[i].kind == GLOS_PROG_JOB && progs[i].id == st.id)
                mine = 1, by = progs[i].by;
        }
        e = glos_sys_kill(GLOS_KILL_JOB, st.id);
        n = job_take(j, out, sizeof out, 0, 0, 0, 30000, &st);
        check("sys-kill-job", ok && r == 0 && mine && self && by == self && e == 0 && n && st.state == GLOS_JS_DONE,
              "ok=%d r=%d mine=%u by=%u self=%u e=%d n=%d code=%u", ok, r, mine, by, self, e, n, st.code);
        glos_close(j);
    }

    /* What is refused. */
    j = glos_job_run(TOOL("both"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    ok = j && glos_job_write(j, "x", 1) == -1 && glos_error() == GLOS_E_REFUSED && glos_job_eof(j) == -1
         && glos_job_read(j, 5, out, 10) == -1 && glos_error() == GLOS_E_ARGUMENT
         && glos_job_keys(j, keys, 1) == -1 && glos_error() == GLOS_E_REFUSED
         && glos_job_read(0x08000077u, GLOS_JOB_OUT, out, 10) == -1 && glos_error() == GLOS_E_HANDLE
         && glos_job_signal(j, 9) == GLOS_E_ARGUMENT;
    out[0] = 0;
    job_take(j, out, sizeof out, 0, 0, 0, 60000, &st);
    glos_close(j);
    ok = ok && glos_job_run(0, 0, 0, 0, 0, 0) == 0 && glos_error() == GLOS_E_ARGUMENT
         && glos_job_run("x", 0, 0, 9, 0, 0) == 0 && glos_error() == GLOS_E_ARGUMENT
         && glos_job_run("x", 0, 0, GLOS_RUN_WINDOW, GLOS_RUNF_STDIN, 0) == 0 && glos_error() == GLOS_E_ARGUMENT
         && glos_job_run("x", 0, 0, 0, 64, 0) == 0 && glos_error() == GLOS_E_ARGUMENT;
    memset(&bad, 0, sizeof bad);
    bad.size = 8;                                       /* (too small to say what to run) */
    ok = ok && glos_call(GLOS_JOB_RUN, &bad) == GLOS_E_ARGUMENT;
    check("job-refused", ok, "err=%d", glos_error());

    /* A program that isn't there: the job ends, with a code that says so. */
    out[0] = err[0] = 0;
    j = glos_job_run("C:\\NOSUCH\\PROGRAM.EXE now", 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    r = job_take(j, out, sizeof out, err, sizeof err, 0, 60000, &st);
    check("job-none", j && r && st.state == GLOS_JS_DONE && st.code == 126, "j=%x r=%d code=%u", j, r, st.code);
    glos_close(j);
    out[0] = 0;
    j = glos_job_run("NOSUCHPG one", 0, 0, GLOS_RUN_HIDDEN, 0, 0);     /* (nor on the PATH) */
    r = job_take(j, out, sizeof out, 0, 0, 0, 60000, &st);
    check("job-none-path", j && r && st.code == 126 && !out[0], "j=%x r=%d code=%u out=%s", j, r, st.code, out);
    glos_close(j);

    /* Nobody's: no handle; it runs. A handle closed while its job runs: the job runs on, and the next after it. */
    remove(hp("%s\\DET.TXT"));
    j = glos_job_run(hp("echo detached > %s\\DET.TXT"), 0, 0, GLOS_RUN_HIDDEN, GLOS_RUNF_DETACH, 0);
    e = glos_error();
    j2 = glos_job_run(TOOL("stream 20"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    r = glos_close(j2);
    out[0] = 0;
    j2 = glos_job_run(TOOL("args after"), 0, 0, GLOS_RUN_HIDDEN, 0, 0);
    ok = job_take(j2, out, sizeof out, 0, 0, 0, 60000, &st);
    line[0] = 0;
    if ((f = fopen(hp("%s\\DET.TXT"), "r")) != 0) {
        if (!fgets(line, sizeof line, f))
            line[0] = 0;
        fclose(f);
    }
    remove(hp("%s\\DET.TXT"));
    check("job-detached", j == 0 && e == 0 && r == 0 && ok && strstr(out, "[after]") && !strncmp(line, "detached", 8),
          "j=%x e=%d r=%d ok=%d line=%s", j, e, r, ok, line);
    glos_close(j2);

    /* In the app's window: what it writes to the screen is read, its keys are typed. */
    out[0] = 0;
    j = glos_job_run(TOOL("chars"), 0, 0, GLOS_RUN_WINDOW, 0, 0);
    ok = j != 0;
    glos_sleep(1500);                                   /* (its program is up, waiting for a key) */
    r = glos_job_keys(j, keys, 3);
    e = job_take(j, out, sizeof out, 0, 0, 0, 60000, &st);
    no_cr(out);
    check("job-window", ok && r == 3 && e && strstr(out, "<a><b>") && st.code == 2, "j=%x err=%d r=%d e=%d out=%s code=%u", j,
          glos_error(), r, e, out, st.code);
    glos_close(j);

    snprintf(line, sizeof line, "HX-APITEST done pass=%d fail=%d\r\n", pass, fails);
    com1(line);
    return fails > 90 ? 90 : fails;
}

/* AUDIO (step 10), under the agent with the desktop on and a sound card: a stream out and its properties, levels,
   the mixer (the master, the streams, the chip's channels and input), the devices. The tone is a triangle of
   a known peak, so levels can be checked against it. */
#define TONE_PEAK 16000

static void tone(short *out, unsigned frames, unsigned channels, unsigned *phase)
{
    unsigned i, c;
    for (i = 0; i < frames; i++) {
        unsigned p = (*phase)++ % 50;                   /* 441 Hz at 22050 */
        int v = p < 25 ? -TONE_PEAK + (int)p * (2 * TONE_PEAK / 25) : TONE_PEAK - (int)(p - 25) * (2 * TONE_PEAK / 25);
        for (c = 0; c < channels; c++)
            out[i * channels + c] = (short)v;
    }
}

/* The stream fed for ms milliseconds (its ring kept about half full), then left with what it has. */
static void feed(struct glos_audio_ring *r, unsigned ms, unsigned *phase)
{
    static short buf[2048];
    unsigned t0 = glos_ticks();
    while (glos_ticks() - t0 < ms) {
        while (r->write - r->read < r->bytes / 2) {
            unsigned frames = sizeof buf / 2 / r->channels;
            tone(buf, frames, r->channels, phase);
            glos_audio_write(r, buf, frames * r->channels * 2);
        }
        glos_sleep(10);
    }
}

static int near(unsigned v, unsigned want, unsigned slack) { return v + slack >= want && v <= want + slack; }

static int audio_mode(void)
{
    static struct glos_audio_stream streams[8];
    static struct glos_audio_channel chans[8];
    static struct glos_audio_device devs[4];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev *e = (struct glos_ev *)ev;
    struct glos_audio_ring *ring = 0, *ring2 = 0;
    struct glos_audio_mixer m;
    glos_handle s, s2, more[8];
    unsigned id = 0, id2 = 0, phase = 0, phase2 = 0, l = 0, r = 0, ol = 0, orr = 0, count = 0, i, wave = 99;
    int v = 0, before, after, ok, n;
    char line[64];
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 98;

    s = glos_audio_out(22050, 2, 16384, &ring, &id);
    ok = s && GLOS_HANDLE_KIND(s) == GLOS_HK_STREAM && ring && ring->size == sizeof *ring && ring->rate == 22050
         && ring->channels == 2 && ring->bytes == 16384 && ring->ring >= sizeof *ring && ring->mixer_rate >= 8000 && id
         && prop(s, GLOS_PROP_STREAM_ID) == (int)id && prop(s, GLOS_PROP_STREAM_VOLUME) == 256
         && prop(s, GLOS_PROP_STREAM_BALANCE) == 0 && prop(s, GLOS_PROP_STREAM_POSITION) == 0;
    check("audio-open", ok, "s=%x err=%d id=%u mixer=%u", s, glos_error(), id, ring ? ring->mixer_rate : 0);
    if (!s) {                                           /* (no card: nothing more to try) */
        snprintf(line, sizeof line, "HX-APITEST done pass=%d fail=%d\r\n", pass, fails);
        com1(line);
        return fails;
    }

    /* Played: its position moves at its rate, nothing runs short, its level is the tone's, and the output's. */
    feed(ring, 300, &phase);
    before = prop(s, GLOS_PROP_STREAM_POSITION);
    feed(ring, 1000, &phase);
    after = prop(s, GLOS_PROP_STREAM_POSITION);
    glos_audio_levels(id, &l, &r);
    n = glos_audio_levels(0, &ol, &orr);
    ok = after - before > 22050 * 8 / 10 && after - before < 22050 * 12 / 10 && ring->underruns == 0
         && near(l, TONE_PEAK, 1500) && near(r, TONE_PEAK, 1500) && n == 0 && near(ol, TONE_PEAK, 1500) && near(orr, TONE_PEAK, 1500);
    check("audio-plays", ok, "frames=%d underruns=%u level=%u,%u out=%u,%u periods=%u", after - before, ring->underruns, l, r, ol,
          orr, ring->periods);

    /* Its volume, its balance, its mute. */
    glos_set(s, GLOS_PROP_STREAM_VOLUME, 128);
    feed(ring, 250, &phase);
    glos_audio_levels(id, &l, &r);
    ok = near(l, TONE_PEAK / 2, 1000) && near(r, TONE_PEAK / 2, 1000);
    glos_set(s, GLOS_PROP_STREAM_BALANCE, 256);
    feed(ring, 250, &phase);
    glos_audio_levels(id, &ol, &orr);
    ok = ok && ol < 200 && near(orr, TONE_PEAK / 2, 1000);
    glos_set(s, GLOS_PROP_STREAM_BALANCE, -128);
    feed(ring, 250, &phase);
    glos_audio_levels(id, &ol, &orr);
    ok = ok && near(ol, TONE_PEAK / 2, 1000) && near(orr, TONE_PEAK / 4, 800);
    check("audio-volume", ok, "half=%u,%u left-half=%u,%u", l, r, ol, orr);
    glos_set(s, GLOS_PROP_STREAM_BALANCE, 0);
    glos_set(s, GLOS_PROP_STREAM_VOLUME, 256);
    glos_set(s, GLOS_PROP_STREAM_MUTE, 1);
    before = prop(s, GLOS_PROP_STREAM_POSITION);
    feed(ring, 250, &phase);
    glos_audio_levels(id, &l, &r);
    after = prop(s, GLOS_PROP_STREAM_POSITION);
    ok = l == 0 && r == 0 && after - before > 3000 && prop(s, GLOS_PROP_STREAM_MUTE) == 1;        /* (silent, and still used up) */
    glos_set(s, GLOS_PROP_STREAM_MUTE, 0);
    check("audio-mute", ok, "level=%u,%u frames=%d", l, r, after - before);

    /* Paused: it stands still, and what it has waits. */
    feed(ring, 100, &phase);
    glos_set(s, GLOS_PROP_STREAM_PAUSED, 1);
    glos_sleep(50);
    before = prop(s, GLOS_PROP_STREAM_POSITION);
    v = prop(s, GLOS_PROP_STREAM_QUEUED);
    glos_sleep(300);
    after = prop(s, GLOS_PROP_STREAM_POSITION);
    glos_audio_levels(id, &l, &r);
    ok = after == before && prop(s, GLOS_PROP_STREAM_QUEUED) == v && v > 0 && l == 0 && ring->underruns == 0;
    glos_set(s, GLOS_PROP_STREAM_PAUSED, 0);
    feed(ring, 200, &phase);
    check("audio-pause", ok && prop(s, GLOS_PROP_STREAM_POSITION) > after + 2000, "held=%d queued=%d then=%d", after - before, v,
          prop(s, GLOS_PROP_STREAM_POSITION) - after);

    /* NOTIFY: told when it has little left, once. */
    while (glos_event(ev, sizeof ev, 0, 0) > 0) ;
    feed(ring, 100, &phase);
    glos_set(s, GLOS_PROP_STREAM_NOTIFY, 4096);
    n = await(GLOS_EV_AUDIO, s, ev, 3000);
    v = prop(s, GLOS_PROP_STREAM_QUEUED);
    ok = n > 0 && e->handle == s && v < 4096 && glos_event(ev, sizeof ev, 300, 1u << GLOS_AREA_AUDIO) == 0;
    glos_set(s, GLOS_PROP_STREAM_NOTIFY, 0);
    check("audio-notify", ok, "n=%d queued=%d", n, v);

    /* The master (this program is one of GLOS's own): half, mute, back. */
    memset(&m, 0, sizeof m);
    m.what = GLOS_MIXER_MASTER;
    n = glos_mixer(&m);
    ok = n == 0 && m.volume == 100 && m.mute == 0;
    m.what = GLOS_MIXER_MASTER_SET, m.volume = 50, m.mute = -1;
    n = glos_mixer(&m);
    feed(ring, 300, &phase);
    glos_audio_levels(id, &l, &r);
    glos_audio_levels(0, &ol, &orr);
    ok = ok && n == 0 && near(l, TONE_PEAK, 1500) && near(ol, TONE_PEAK / 2, 1000);
    m.what = GLOS_MIXER_MASTER_SET, m.volume = -1, m.mute = 1;
    glos_mixer(&m);
    feed(ring, 300, &phase);
    glos_audio_levels(0, &i, &count);
    ok = ok && i == 0 && count == 0;
    m.what = GLOS_MIXER_MASTER;
    glos_mixer(&m);
    ok = ok && m.volume == 50 && m.mute == 1;
    m.what = GLOS_MIXER_MASTER_SET, m.volume = 100, m.mute = 0;
    glos_mixer(&m);
    m.what = GLOS_MIXER_MASTER_SET, m.volume = 101, m.mute = 0;
    check("audio-master", ok && glos_mixer(&m) == GLOS_E_ARGUMENT, "n=%d stream=%u out=%u muted=%u vol=%d mute=%d", n, l, ol, i,
          m.volume, m.mute);

    /* A second stream of another rate and shape beside it; the mixer lists both and sets one. */
    s2 = glos_audio_out(11025, 1, 8192, &ring2, &id2);
    feed(ring, 100, &phase);                            /* (it ran dry for NOTIFY: full again before the count is taken) */
    before = (int)ring->underruns;
    for (i = 0; i < 40; i++) {
        feed(ring, 15, &phase);
        feed(ring2, 15, &phase2);
    }
    memset(&m, 0, sizeof m);
    m.what = GLOS_MIXER_STREAMS, m.buffer = (unsigned)streams, m.room = sizeof streams;
    n = glos_mixer(&m);
    ok = s2 && n == 0 && m.count == 2 && (int)ring->underruns == before && ring2->underruns == 0;
    for (i = 0; i < m.count; i++)
        ok = ok && streams[i].size == sizeof streams[0] && (streams[i].id == id || streams[i].id == id2)
             && streams[i].rate == (streams[i].id == id ? 22050u : 11025u) && streams[i].channels == (streams[i].id == id ? 2u : 1u)
             && streams[i].volume == 256 && streams[i].left > 1000;
    m.what = GLOS_MIXER_STREAM_SET, m.id = id2, m.volume = 64, m.balance = -1000, m.mute = -1;
    n = glos_mixer(&m);
    ok = ok && n == 0 && prop(s2, GLOS_PROP_STREAM_VOLUME) == 64 && prop(s2, GLOS_PROP_STREAM_BALANCE) == 0;
    m.id = 9999;
    check("audio-streams", ok && glos_mixer(&m) == GLOS_E_NOT_FOUND, "s2=%x n=%d count=%u under=%u,%u", s2, n, m.count,
          ring->underruns - (unsigned)before, ring2 ? ring2->underruns : 99);
    glos_close(s2);

    /* The chip's own channels and the input recorded. */
    memset(&m, 0, sizeof m);
    m.what = GLOS_MIXER_CHANNELS, m.buffer = (unsigned)chans, m.room = sizeof chans;
    n = glos_mixer(&m);
    for (i = 0; i < m.count; i++)
        if (chans[i].id == GLOS_CHANNEL_WAVE && !strcmp(chans[i].name, "Wave"))
            wave = i;
    ok = n == 0 && m.count >= 3 && wave < m.count && chans[wave].left <= 255;
    count = m.count;
    m.what = GLOS_MIXER_CHANNEL_SET, m.id = GLOS_CHANNEL_WAVE, m.volume = 128, m.balance = 64, m.mute = -1;
    n = glos_mixer(&m);
    ok = ok && n == 0 && near((unsigned)m.volume, 128, 9) && near((unsigned)m.balance, 64, 9);
    l = (unsigned)m.volume, r = (unsigned)m.balance;
    m.what = GLOS_MIXER_CHANNEL_SET, m.id = GLOS_CHANNEL_WAVE, m.volume = (int)chans[wave < 8 ? wave : 0].left,
        m.balance = (int)chans[wave < 8 ? wave : 0].right, m.mute = -1;
    glos_mixer(&m);                                     /* (as it was) */
    m.what = GLOS_MIXER_INPUT_SET, m.id = GLOS_INPUT_LINE;
    n = glos_mixer(&m);
    m.what = GLOS_MIXER_INPUT, m.id = 99;
    v = glos_mixer(&m);
    ok = ok && n == 0 && v == 0 && m.id == GLOS_INPUT_LINE;
    m.what = GLOS_MIXER_CHANNEL_SET, m.id = 17, m.volume = 1, m.balance = 1, m.mute = 0;
    check("audio-chip", ok && glos_mixer(&m) == GLOS_E_ARGUMENT, "n=%d channels=%u wave=%u set=%u,%u input=%u", n, count, wave, l, r,
          m.id);

    /* The devices. */
    n = glos_audio_devices(devs, sizeof devs, &count);
    ok = n == 0 && count == 1 && devs[0].size == sizeof devs[0] && devs[0].kind == GLOS_AUDIO_DEV_OUT && devs[0].name[0]
         && devs[0].rate == ring->mixer_rate && devs[0].streams == 1;
    check("audio-devices", ok, "n=%d count=%u name=%s rate=%u streams=%u", n, count, devs[0].name, devs[0].rate, devs[0].streams);

    /* What is refused; as many streams as there are; closed. */
    ok = glos_audio_out(100, 2, 16384, 0, 0) == 0 && glos_error() == GLOS_E_ARGUMENT
         && glos_audio_out(22050, 3, 16384, 0, 0) == 0 && glos_audio_out(22050, 2, 5000, 0, 0) == 0 && glos_error() == GLOS_E_ARGUMENT
         && glos_set(s, GLOS_PROP_STREAM_VOLUME, 257) == GLOS_E_ARGUMENT && glos_set(s, GLOS_PROP_STREAM_BALANCE, -300) == GLOS_E_ARGUMENT
         && glos_set(s, GLOS_PROP_STREAM_POSITION, 1) == GLOS_E_PROPERTY && glos_audio_levels(9999, &l, &r) == GLOS_E_NOT_FOUND;
    for (i = 0; i < 7; i++)
        ok = ok && (more[i] = glos_audio_out(8000, 1, 4096, 0, 0)) != 0;
    ok = ok && glos_audio_out(8000, 1, 4096, 0, 0) == 0 && glos_error() == GLOS_E_MEMORY;
    for (i = 0; i < 7; i++)
        ok = ok && glos_close(more[i]) == 0;
    ok = ok && glos_close(s) == 0 && glos_get(s, GLOS_PROP_STREAM_VOLUME, &v) == GLOS_E_HANDLE
         && glos_audio_devices(devs, sizeof devs, &count) == 0 && devs[0].streams == 0;
    check("audio-refused", ok, "err=%d streams=%u", glos_error(), devs[0].streams);

    snprintf(line, sizeof line, "HX-APITEST done pass=%d fail=%d\r\n", pass, fails);
    com1(line);
    return fails > 90 ? 90 : fails;
}

/* What a run of the test may have left on a PC that is kept (a bench): taken away. */
static int clean_mode(void)
{
    int a, b, c, d, e;
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 98;
    a = glos_assoc_set(".ZZT", 0);
    b = glos_assoc_set(".URL", 0);
    c = glos_schedule_remove("never");
    d = glos_schedule_remove("boot");
    e = glos_schedule_remove("nightly");
    glos_entry_remove();
    forget_profile();
    printf("CLEAN zzt=%d url=%d never=%d boot=%d nightly=%d\n", a, b, c, d, e);
    return 0;
}

/* A program OPEN started (not an app): what it was given, for the test that opened it. */
static int opened_mode(int argc, char **argv)
{
    FILE *f = fopen(OPENED, "w");
    int i;
    if (!f)
        return 1;
    for (i = 1; i < argc; i++)
        fprintf(f, i > 1 ? " %s" : "%s", argv[i]);
    fputc('\n', f);
    fclose(f);
    return 0;
}

/* The shell under the user's hands (the agent's): the shell's events, a tray icon's presses, what becomes of
   notifications. Each is said on the standard output. */
static int shell_mode(void)
{
    static unsigned icon[256];
    unsigned char ev[GLOS_EV_MAX];
    struct glos_ev *e = (struct glos_ev *)ev;
    struct glos_point tray;
    glos_handle t, n = 0;
    unsigned i, notes = 0;
    if (glos_init("API test", "apitest", "Tests") != 0)
        return 1;
    for (i = 0; i < 256; i++)
        icon[i] = 0xFFC02040u;
    struct glos_win_focus fq = { sizeof fq, 0, 99999 };  /* (any window by its number: GLOS's own programs') */
    t = glos_tray(icon, "The test's icon");
    glos_point(GLOS_POINT_TRAY, &tray);
    printf("SHELL ready shell=%d focus=%d tray=%d,%d,%d,%d\n", glos_shell(1), glos_call(GLOS_WIN_FOCUS, &fq), tray.x, tray.y,
           tray.w, tray.h);
    fflush(stdout);
    for (;;) {
        if (glos_event(ev, sizeof ev, GLOS_FOREVER, 0) <= 0)
            continue;
        if (e->kind == GLOS_EV_TRAY) {
            struct glos_ev_tray *te = (struct glos_ev_tray *)ev;
            printf("SHELL tray what=%u mine=%d at=%d,%d %dx%d\n", te->what, te->ev.handle == t, te->x, te->y, te->w, te->h);
            if (te->what == GLOS_TRAY_MENU) {           /* its menu asked for: the next notification */
                if (n)
                    glos_close(n);
                notes++;
                n = notes == 1 ? glos_notify("First", "Two actions.", "Yes", "No", icon, 60)
                  : notes == 2 ? glos_notify("Second", "One action, and a text long enough to be broken into lines.", "Fine", 0, 0, 60)
                  : notes == 3 ? glos_notify("Third", 0, 0, 0, 0, 2) : glos_notify("Fourth", "Pressed away.", 0, 0, icon, 60);
                printf("SHELL note %u r=%d\n", notes, n ? 0 : glos_error());
            }
        } else if (e->kind == GLOS_EV_NOTIFY) {
            struct glos_ev_notify *ne = (struct glos_ev_notify *)ev;
            printf("SHELL notify what=%u mine=%d\n", ne->what, ne->ev.handle == n);
        } else if (e->kind == GLOS_EV_SHELL) {
            struct glos_ev_shell *se = (struct glos_ev_shell *)ev;
            printf("SHELL shell what=%u at=%d,%d window=%u app=%u\n", se->what, se->x, se->y, se->window, se->app);
            if (se->what == GLOS_SHELL_TASKS)
                break;
        } else if (e->kind == GLOS_EV_END) {
            break;
        }
        fflush(stdout);
    }
    printf("SHELL end\n");
    fflush(stdout);
    return 42;
}

int main(int argc, char **argv)
{
    char line[64];
    find_home(argv[0]);
    if (argc > 1 && !strcmp(argv[1], "shell"))
        return shell_mode();
    if (argc > 1 && !strcmp(argv[1], "store"))
        return store_mode();
    if (argc > 1 && !strcmp(argv[1], "clean"))
        return clean_mode();
    if (argc > 1 && !strcmp(argv[1], "job"))
        return job_mode();
    if (argc > 1 && !strcmp(argv[1], "audio"))
        return audio_mode();
    if (argc > 2 && !strcmp(argv[1], "sched"))
        return sched_mode(argv[2]);
    if (argc > 1 && (strstr(argv[1], ".ZZT") || strstr(argv[1], ".zzt") || !strcmp(argv[1], "said") || !strcmp(argv[1], "with")))
        return opened_mode(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "win"))
        return interactive();
    if (argc > 1 && !strcmp(argv[1], "set"))
        return setting_mode();
    if (argc > 3 && !strcmp(argv[1], "dnd"))
        return dnd_mode(argv[2], atoi(argv[3]));
    if (argc > 1 && !strcmp(argv[1], "lfn"))
        return lfn_mode();
    before();
    init();
    if (glos_app()) {
        calls();
        events();
        timers();
        windows();
        settings();
        clipboard();
        files(argc > 2 && !strcmp(argv[1], "many") ? (unsigned)atoi(argv[2]) : 300);
        shell();
        sys();
    }
    snprintf(line, sizeof line, "HX-APITEST done pass=%d fail=%d\r\n", pass, fails);
    com1(line);
    return fails > 99 ? 99 : fails;
}
