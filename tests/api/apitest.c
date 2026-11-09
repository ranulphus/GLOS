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
 * GLOS's, and handles closed or never made. */
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

    check("present", glos_present(), 0);
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    check("version", glos_version(&v) == 0 && v.major == 2 && v.minor == 0 && v.build[0]
          && v.functions[GLOS_AREA_CORE][0] == 0x1FFFu && GLOS_HAS(&v, GLOS_CORE_RANDOM)
          && !GLOS_HAS(&v, GLOS_FN(GLOS_AREA_WIN, 0)),
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
    check("unknown-area", glos_call(GLOS_FN(GLOS_AREA_WIN, 0), 0) == GLOS_E_FUNCTION, 0);
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
    check("prop-ms", glos_set(h, GLOS_PROP_TIMER_MS, 20) == 0 && glos_get(h, GLOS_PROP_TIMER_MS, &v) == 0 && v == 20,
          "ms=%d", v);
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

int main(void)
{
    char line[64];
    before();
    init();
    if (glos_app()) {
        calls();
        events();
        timers();
    }
    snprintf(line, sizeof line, "HX-APITEST done pass=%d fail=%d\r\n", pass, fails);
    com1(line);
    return fails > 99 ? 99 : fails;
}
