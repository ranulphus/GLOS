/* THRTEST: an app's own threads (API 2.0's THREAD; docs/v1/phase-b.md step
 * 4; docs/supervisor.md 12.7), on libglos with the C library's lock.
 * Run by tests/loopa/jobs.py's thr cases (make loopa-thr) through the
 * agent; it writes on stdout.
 *
 *   THRTEST          every case: THR case=NAME ok=1|0 [what], then
 *                    THR done pass=N fail=M; the exit code is the failures
 *   THRTEST crash    a second thread writes through a null pointer: the app
 *                    is ended, with a crash report naming the thread
 *   THRTEST exit N   a second thread calls exit(N): the app ends with N
 *   THRTEST spin     three threads compute, allocate and write files for
 *                    ever, the first waits for events: to be killed
 *   THRTEST dos      a second thread waits for a key in DOS, the first for
 *                    events: to be killed
 *   THRTEST quick    four threads that do a little and end; the first
 *                    joins them. Exit code 0
 *   THRTEST leave    the first thread returns from main() while three
 *                    others spin: the app ends, code 3 */
#include <conio.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/exceptn.h>
#include <sys/segments.h>
#include <sys/time.h>
#include <unistd.h>

#include <glos/glos.h>

static int pass, fails;

static void check(const char *name, int ok, const char *fmt, ...)
{
    char what[120] = "";
    va_list ap;
    if (fmt) {
        va_start(ap, fmt);
        vsnprintf(what, sizeof what, fmt, ap);
        va_end(ap);
    }
    printf("THR case=%s ok=%d%s%s\n", name, ok != 0, what[0] ? " " : "", what);
    fflush(stdout);
    if (ok)
        pass++;
    else
        fails++;
}

/* ---- work for a thread, with an answer that says whether it was disturbed */

struct job {
    unsigned n;                         /* which of several */
    unsigned number;                    /* -> glos_self() there */
    unsigned isum;                      /* -> */
    double fsum;                        /* -> */
    unsigned errors;                    /* -> */
    volatile unsigned turns;            /* -> */
    glos_handle h;
};

static void compute_of(unsigned n, unsigned *isum, double *fsum)
{
    unsigned i, s = n * 2654435761u;
    double f = 0.0;
    for (i = 1; i <= 120000; i++) {
        s = s * 1664525u + 1013904223u + i;
        f += (double)(i + n) * 1.000001 / (double)(i + 3);      /* (values held in the FPU between switches) */
    }
    *isum = s;
    *fsum = f;
}

static void compute_thread(void *arg)
{
    struct job *j = arg;
    j->number = glos_self();
    if (_my_ds() != __djgpp_ds_alias || _my_ss() != __djgpp_ds_alias)
        j->errors++;                                    /* (not the program's DS, which its signals shrink) */
    compute_of(j->n, &j->isum, &j->fsum);
}

static void alloc_thread(void *arg)
{
    struct job *j = arg;
    unsigned char *ring[32];
    unsigned size[32], i, k, seed = j->n * 7919u + 1;
    memset(ring, 0, sizeof ring);
    for (i = 0; i < 1500; i++) {
        unsigned slot = i % 32;
        seed = seed * 1103515245u + 12345u;
        if (ring[slot]) {
            for (k = 0; k < size[slot]; k++)
                if (ring[slot][k] != (unsigned char)(j->n * 16 + slot))
                    j->errors++;
            free(ring[slot]);
        }
        size[slot] = 16 + (seed >> 16) % 2000;
        if (!(ring[slot] = malloc(size[slot]))) {
            j->errors++;
            size[slot] = 0;
            continue;
        }
        memset(ring[slot], (int)(j->n * 16 + slot), size[slot]);
        if (i % 4 == 0) {                               /* (and the size changed in place, as realloc does) */
            unsigned char *p = realloc(ring[slot], size[slot] + 64);
            if (p) {
                ring[slot] = p;
                memset(p + size[slot], (int)(j->n * 16 + slot), 64);
                size[slot] += 64;
            }
        }
    }
    for (i = 0; i < 32; i++)
        free(ring[i]);
}

static void file_once(struct job *j)
{
    char name[16], buf[512];
    unsigned b, k;
    FILE *f;
    snprintf(name, sizeof name, "THR%u.TMP", j->n);
    if (!(f = fopen(name, "wb"))) {
        j->errors++;
        return;
    }
    for (b = 0; b < 24; b++) {
        for (k = 0; k < sizeof buf; k++)
            buf[k] = (char)(j->n * 31 + b * 7 + k);
        if (fwrite(buf, 1, sizeof buf, f) != sizeof buf)
            j->errors++;
    }
    fclose(f);
    if (!(f = fopen(name, "rb"))) {
        j->errors++;
        return;
    }
    for (b = 0; b < 24; b++) {
        if (fread(buf, 1, sizeof buf, f) != sizeof buf)
            j->errors++;
        for (k = 0; k < sizeof buf; k++)
            if (buf[k] != (char)(j->n * 31 + b * 7 + k)) {
                j->errors++;
                break;
            }
    }
    if (fgetc(f) != EOF)
        j->errors++;
    fclose(f);
    if (remove(name) != 0)
        j->errors++;
}

static void file_thread(void *arg)
{
    struct job *j = arg;
    unsigned i;
    for (i = 0; i < 3; i++)
        file_once(j);
}

static int join_all(struct job *j, unsigned n, unsigned ms)
{
    unsigned i, ok = 0;
    for (i = 0; i < n; i++)
        ok += j[i].h && glos_join(j[i].h, ms, 0) == 1;
    return ok == n;
}

/* Threads that compute, allocate and do file I/O at once: each one's answer as it is alone. */
static void together(void)
{
    static struct job c[4], al[3], fl[3];
    unsigned i, isum, ok = 1, started = 1, errors = 0;
    double fsum;
    check("self-first", glos_self() == 0, "self=%u", glos_self());
    memset(c, 0, sizeof c);
    memset(al, 0, sizeof al);
    memset(fl, 0, sizeof fl);
    for (i = 0; i < 4; i++) {
        c[i].n = i + 1;
        started &= (c[i].h = glos_thread(compute_thread, &c[i], 0, 0)) != 0;
    }
    for (i = 0; i < 3; i++) {
        al[i].n = fl[i].n = i + 1;
        started &= (al[i].h = glos_thread(alloc_thread, &al[i], 0, 0)) != 0;
        started &= (fl[i].h = glos_thread(file_thread, &fl[i], 32768, 0)) != 0;
    }
    check("create", started, "error=%d", glos_error());
    check("join", join_all(c, 4, 120000) & join_all(al, 3, 120000) & join_all(fl, 3, 120000), 0);
    for (i = 0; i < 4; i++) {
        compute_of(c[i].n, &isum, &fsum);
        ok &= c[i].isum == isum && c[i].fsum == fsum && c[i].number >= 1 && c[i].number <= GLOS_THREADS && !c[i].errors;
    }
    check("compute", ok, "isum=%08x fsum=%.6f number=%u", c[0].isum, c[0].fsum, c[0].number);
    for (i = 0; i < 3; i++)
        errors += al[i].errors;
    check("alloc", errors == 0, "errors=%u", errors);
    for (errors = 0, i = 0; i < 3; i++)
        errors += fl[i].errors;
    check("files", errors == 0, "errors=%u", errors);
}

/* ---- a lock contended */

static glos_handle lock;
static volatile unsigned counter;
static volatile int hold_ms;

static void lock_thread(void *arg)
{
    struct job *j = arg;
    unsigned i, v;
    for (i = 0; i < 400; i++) {
        if (glos_take(lock, GLOS_FOREVER) != 1) {
            j->errors++;
            continue;
        }
        v = counter;
        if (i % 16 == 0)
            glos_yield();                               /* (the others run, and find it taken) */
        counter = v + 1;
        if (glos_give(lock) != 0)
            j->errors++;
    }
}

static void hold_thread(void *arg)
{
    (void)arg;
    glos_take(lock, GLOS_FOREVER);
    glos_sleep((unsigned)hold_ms);
    if (hold_ms < 1000)
        glos_give(lock);                                /* (or it ends holding it: free at its end) */
}

static void locks(void)
{
    static struct job j[4];
    unsigned i, errors = 0, t;
    int held = -1, r, ok;
    glos_handle h;
    lock = glos_lock();
    ok = lock != 0 && glos_get(lock, GLOS_PROP_LOCK_HELD, &held) == 0 && held == 0;
    check("lock-new", ok, "held=%d", held);
    ok = glos_take(lock, 0) == 1 && glos_take(lock, 0) == 1 && glos_get(lock, GLOS_PROP_LOCK_HELD, &held) == 0
         && held == 1 && glos_give(lock) == 0 && glos_give(lock) == 0 && glos_give(lock) == GLOS_E_REFUSED;
    check("lock-again", ok, "held=%d", held);
    memset(j, 0, sizeof j);
    counter = 0;
    for (i = 0; i < 4; i++)
        j[i].h = glos_thread(lock_thread, &j[i], 0, 0);
    r = join_all(j, 4, 120000);
    for (i = 0; i < 4; i++)
        errors += j[i].errors;
    check("lock-contended", r && counter == 1600 && errors == 0, "counter=%u errors=%u", counter, errors);
    hold_ms = 300;                                      /* another holds it: a try fails, a short wait runs out */
    h = glos_thread(hold_thread, 0, 0, 0);
    glos_sleep(60);
    t = glos_ticks();
    r = glos_take(lock, 0);
    i = (unsigned)glos_take(lock, 80);
    t = glos_ticks() - t;
    check("lock-timeout", r == 0 && i == 0 && t >= 70 && t < 250, "try=%d wait=%u ms=%u", r, i, t);
    check("lock-given", glos_take(lock, 2000) == 1 && glos_give(lock) == 0, 0);
    glos_join(h, 2000, 0);
    hold_ms = 1200;                                     /* its holder ends with it: free */
    h = glos_thread(hold_thread, 0, 0, 0);
    glos_sleep(60);
    r = glos_take(lock, 0);
    ok = r == 0 && glos_get(lock, GLOS_PROP_LOCK_HELD, &held) == 0 && held >= 2;
    check("lock-held", ok, "held=%d", held);
    r = glos_join(h, 10000, 0);
    ok = r == 1 && glos_get(lock, GLOS_PROP_LOCK_HELD, &held) == 0 && held == 0 && glos_take(lock, 0) == 1 && glos_give(lock) == 0;
    check("lock-freed", ok, "join=%d held=%d", r, held);
    check("lock-bad", glos_give(0x04000099u) == GLOS_E_HANDLE && glos_take(0x04000099u, 0) == -GLOS_E_HANDLE, 0);
}

/* ---- a signal passed round */

static glos_handle ring[3], finished;
static volatile unsigned laps[3];

static void ring_thread(void *arg)
{
    unsigned n = (unsigned)arg;
    int which;
    for (;;) {
        if (glos_wait(&ring[n], 1, 5000, &which) != 0 || which != 0)
            break;
        if (++laps[n] > 20)
            break;
        if (n == 2 && laps[2] == 20) {
            glos_signal_set(finished, 1);
            break;
        }
        glos_signal_set(ring[(n + 1) % 3], 1);
    }
}

static void signals(void)
{
    glos_handle t[3];
    unsigned i;
    int which = -9, set = -1, r;
    for (i = 0; i < 3; i++) {
        ring[i] = glos_signal(GLOS_SIGNAL_ONE);
        laps[i] = 0;
    }
    finished = glos_signal(0);
    for (i = 0; i < 3; i++)
        t[i] = glos_thread(ring_thread, (void *)i, 0, 0);
    glos_signal_set(ring[0], 1);
    r = glos_wait(&finished, 1, 20000, &which);
    check("signal-ring", r == 0 && which == 0 && laps[0] == 20 && laps[1] == 20 && laps[2] == 20, "r=%d which=%d laps=%u,%u,%u",
          r, which, laps[0], laps[1], laps[2]);
    /* The one that stays is set for every wait until it is cleared; the one that lets one go is clear after. */
    r = glos_wait(&finished, 1, 0, &which) == 0 && which == 0 && glos_get(finished, GLOS_PROP_SIGNAL_SET, &set) == 0 && set == 1;
    check("signal-stays", r, "set=%d", set);
    glos_signal_set(finished, 0);
    r = glos_wait(&finished, 1, 30, &which);
    check("signal-cleared", r == 0 && which == -1, "which=%d", which);
    glos_signal_set(ring[1], 1);                        /* (the others wait for theirs: let them end) */
    glos_signal_set(ring[0], 1);
    for (i = 0; i < 3; i++)
        glos_signal_set(ring[i], 1);
    for (i = 0, r = 1; i < 3; i++)
        r &= glos_join(t[i], 10000, 0) == 1;
    check("signal-ended", r, 0);
    glos_signal_set(ring[0], 1);
    r = glos_wait(&ring[0], 1, 0, &which);
    i = (unsigned)glos_wait(&ring[0], 1, 0, &set);
    check("signal-one", r == 0 && which == 0 && i == 0 && set == -1, "which=%d then=%d", which, set);
    for (i = 0; i < 3; i++)
        glos_close(ring[i]);
    check("signal-closed", glos_signal_set(ring[0], 1) == GLOS_E_HANDLE && glos_wait(&ring[0], 1, 0, &which) == GLOS_E_HANDLE, 0);
}

/* ---- JOIN and WAIT */

static void sleeper(void *arg)
{
    glos_sleep((unsigned)arg);
    glos_thread_exit(77);
}

static void returner(void *arg) { (void)arg; }

static void setter(void *arg)
{
    glos_sleep(150);
    glos_signal_set((glos_handle)arg, 1);
}

static void waits(void)
{
    glos_handle t, s, tm, h[3], helper;
    unsigned code = 0, ms;
    int v = -1, w = -1, which = -9, r;
    t = glos_thread(sleeper, (void *)300, 0, 0);
    ms = glos_ticks();
    r = glos_join(t, 50, &code);
    ms = glos_ticks() - ms;
    r = r == 0 && ms >= 40 && ms < 250 && glos_get(t, GLOS_PROP_THREAD_ENDED, &v) == 0 && v == 0;
    check("join-timeout", r, "ended=%d ms=%u", v, ms);
    glos_sleep(400);
    r = glos_get(t, GLOS_PROP_THREAD_ENDED, &v) == 0 && v == 1 && glos_get(t, GLOS_PROP_THREAD_CODE, &w) == 0 && w == 77;
    check("join-props", r, "ended=%d code=%d", v, w);
    r = glos_join(t, GLOS_FOREVER, &code);
    check("join-code", r == 1 && code == 77, "r=%d code=%u", r, code);
    check("join-closed", glos_join(t, 0, 0) == -GLOS_E_HANDLE, 0);
    t = glos_thread(returner, 0, 0, 0);
    code = 99;
    r = glos_join(t, 5000, &code);
    check("join-return", r == 1 && code == 0, "r=%d code=%u", r, code);

    s = glos_signal(0);
    tm = glos_timer(100, 0);
    t = glos_thread(sleeper, (void *)600, 0, 0);
    helper = glos_thread(setter, (void *)s, 0, 0);
    h[0] = s;
    h[1] = tm;
    h[2] = t;
    ms = glos_ticks();
    r = glos_wait(h, 3, 5000, &which);
    check("wait-timer", r == 0 && which == 1, "r=%d which=%d ms=%u", r, which, glos_ticks() - ms);
    r = glos_wait(h, 3, 5000, &which);
    check("wait-signal", r == 0 && which == 0, "r=%d which=%d ms=%u", r, which, glos_ticks() - ms);
    glos_signal_set(s, 0);
    r = glos_wait(h, 3, 5000, &which);
    check("wait-thread", r == 0 && which == 2, "r=%d which=%d ms=%u", r, which, glos_ticks() - ms);
    h[2] = s;                                           /* (nothing ready: the time runs out) */
    ms = glos_ticks();
    r = glos_wait(h, 3, 60, &which);
    ms = glos_ticks() - ms;
    check("wait-none", r == 0 && which == -1 && ms >= 50 && ms < 250, "r=%d which=%d ms=%u", r, which, ms);
    h[2] = lock;                                        /* (a lock is not waited for so) */
    check("wait-kind", glos_wait(h, 3, 0, &which) == GLOS_E_KIND && glos_wait(h, 0, 0, &which) == GLOS_E_ARGUMENT, 0);
    glos_join(t, 5000, 0);
    glos_join(helper, 5000, 0);
    glos_close(tm);
    glos_close(s);
}

/* ---- events: one thread posts, another is in DOS, the first takes them */

static volatile int stop;

static void poster(void *arg)
{
    unsigned i;
    (void)arg;
    for (i = 0; i < 200; i++) {
        while (glos_post(1000 + i, i, 0, 0) != 0)       /* (the queue full: until the first thread has taken some) */
            glos_sleep(1);
        if (i % 10 == 9)
            glos_yield();
    }
}

static void dos_worker(void *arg)
{
    struct job *j = arg;
    while (!stop) {
        file_once(j);
        j->turns++;
    }
}

static void events(void)
{
    static struct job fj;
    struct glos_ev_post ev;
    glos_handle p, f;
    unsigned got = 0, order = 1, t0 = glos_ticks();
    memset(&fj, 0, sizeof fj);
    fj.n = 9;
    stop = 0;
    f = glos_thread(dos_worker, &fj, 0, 0);
    p = glos_thread(poster, 0, 0, 0);
    while (got < 200 && glos_ticks() - t0 < 60000) {
        if (glos_event(&ev, sizeof ev, 1000, 0) != (int)sizeof ev || ev.ev.kind != GLOS_EV_POST)
            continue;                                   /* (a timer's, left from the cases before) */
        order &= ev.code == 1000 + got && ev.a == got;
        got++;
    }
    stop = 1;
    check("events", got == 200 && order, "got=%u order=%u", got, order);
    check("events-dos", glos_join(f, 60000, 0) == 1 && fj.turns >= 1 && fj.errors == 0, "turns=%u errors=%u", fj.turns, fj.errors);
    glos_join(p, 5000, 0);
}

/* ---- the first thread's own interrupts while the others are in DOS */

static volatile unsigned alarms;

static void on_alarm(int sig)
{
    (void)sig;
    alarms++;
}

static void interrupts(void)
{
    static struct job fj[2], cj;
    struct itimerval it, off;
    unsigned i, t0, isum;
    double fsum;
    memset(fj, 0, sizeof fj);
    memset(&cj, 0, sizeof cj);
    memset(&it, 0, sizeof it);
    memset(&off, 0, sizeof off);
    alarms = 0;
    stop = 0;
    signal(SIGALRM, on_alarm);
    it.it_interval.tv_usec = it.it_value.tv_usec = 20000;
    setitimer(ITIMER_REAL, &it, 0);
    for (i = 0; i < 2; i++) {
        fj[i].n = 20 + i;
        fj[i].h = glos_thread(dos_worker, &fj[i], 0, 0);
    }
    cj.n = 5;
    cj.h = glos_thread(compute_thread, &cj, 0, 0);
    t0 = glos_ticks();
    while (glos_ticks() - t0 < 1500)                    /* (busy, and asleep: its signals come either way) */
        if ((glos_ticks() - t0) / 100 % 2)
            glos_sleep(10);
    setitimer(ITIMER_REAL, &off, 0);
    signal(SIGALRM, SIG_DFL);
    stop = 1;
    i = join_all(fj, 2, 60000) && glos_join(cj.h, 60000, 0) == 1;
    compute_of(5, &isum, &fsum);
    /* (DJGPP's timer runs on the PC's tick, 18.2 a second: 27 or so in the time) */
    /* (and the C library's lock is handed from one to the other: neither is left with a turn or two) */
    check("interrupts", i && alarms >= 15 && alarms <= 40 && fj[0].errors + fj[1].errors == 0
          && fj[0].turns * 4 >= fj[1].turns && fj[1].turns * 4 >= fj[0].turns && fj[0].turns + fj[1].turns >= 4
          && cj.isum == isum && cj.fsum == fsum,
          "alarms=%u turns=%u,%u errors=%u", alarms, fj[0].turns, fj[1].turns, fj[0].errors + fj[1].errors);
}

/* ---- the background class; what is the first thread's alone */

static volatile unsigned bg_count;

static void background(void *arg)
{
    unsigned i;
    (void)arg;
    for (i = 0; i < 400000; i++)
        bg_count = i + 1;
}

static char raw_stack[16384];
static volatile unsigned raw_ds;

static void raw_thread(void *arg)                       /* (made without libglos: no table entry, the caller's DS) */
{
    struct glos_thread_exit q = { sizeof q, (unsigned)arg + 1 };
    raw_ds = _my_ds();
    glos_call(GLOS_THREAD_EXIT, &q);
}

static volatile int gpu_answer = -9;

static void gpu_thread(void *arg)
{
    struct glos_gpu g;
    (void)arg;
    memset(&g, 0, sizeof g);
    g.size = sizeof g;
    gpu_answer = glos_call(GLOS_DRAW_GPU, &g);
}

static void classes(void)
{
    struct glos_thread_exit q = { sizeof q, 5 };
    struct glos_thread bad;
    glos_handle t;
    unsigned t0, during;
    int flags = -1, v = -1, r;
    bg_count = 0;
    t = glos_thread(background, 0, 0, GLOS_THREAD_BACKGROUND);
    r = glos_get(t, GLOS_PROP_THREAD_FLAGS, &flags);
    t0 = glos_ticks();
    while (glos_ticks() - t0 < 400)                     /* (the first thread busy: the background one runs beside it) */
        ;
    during = bg_count;
    r = r == 0 && flags == GLOS_THREAD_BACKGROUND && during > 0 && glos_join(t, 60000, 0) == 1 && bg_count == 400000;
    check("background", r, "flags=%d during=%u count=%u", flags, during, bg_count);
    t = glos_thread(gpu_thread, 0, 0, 0);
    glos_join(t, 5000, 0);
    check("gpu-refused", gpu_answer == GLOS_E_REFUSED, "answer=%d", gpu_answer);
    check("exit-first", glos_call(GLOS_THREAD_EXIT, &q) == GLOS_E_REFUSED, 0);
    memset(&bad, 0, sizeof bad);
    bad.size = sizeof bad;
    r = glos_call(GLOS_THREAD_CREATE, &bad);            /* no entry, no stack */
    bad.entry = (unsigned)returner;
    bad.stack = 0xFFFFFF00u;                            /* a stack that is nobody's memory */
    check("create-bad", r == GLOS_E_ARGUMENT && glos_call(GLOS_THREAD_CREATE, &bad) == GLOS_E_ARGUMENT, "r=%d", r);
    bad.stack = (unsigned)raw_stack + sizeof raw_stack - 12;
    bad.data = _my_cs();                                /* a code segment for its data */
    r = glos_call(GLOS_THREAD_CREATE, &bad);
    bad.data = 0;                                       /* the caller's own segments: as a program without libglos has it */
    bad.entry = (unsigned)raw_thread;
    bad.arg = 41;
    during = (unsigned)glos_call(GLOS_THREAD_CREATE, &bad);
    {
        struct glos_thread_join jq = { sizeof jq, bad.handle, 5000, 0, 0 };
        glos_call(GLOS_THREAD_JOIN, &jq);
        glos_close(bad.handle);
        check("create-raw", r == GLOS_E_ARGUMENT && during == 0 && jq.ended && jq.code == 42 && raw_ds == (unsigned)_my_ds(),
              "r=%d create=%u ended=%u code=%u ds=%x", r, during, jq.ended, jq.code, raw_ds);
    }
    t = glos_thread(returner, 0, 0, 0);
    r = glos_get(t, GLOS_PROP_THREAD_NUMBER, &v);
    flags = v;
    r = r == 0 && v >= 1 && v <= GLOS_THREADS && glos_set(t, GLOS_PROP_THREAD_NUMBER, 3) == GLOS_E_PROPERTY
        && glos_get(t, GLOS_PROP_LOCK_HELD, &v) == GLOS_E_KIND;
    check("thread-props", r, "number=%d", flags);
    glos_join(t, 5000, 0);
}

/* As many threads as there may be; one more is refused; their records are used again. */
static void many(void)
{
    static struct job j[GLOS_THREADS];
    glos_handle extra;
    unsigned i, made = 0, round;
    int e;
    for (round = 0; round < 2; round++) {
        memset(j, 0, sizeof j);
        for (i = 0, made = 0; i < GLOS_THREADS; i++) {
            j[i].n = i;
            made += (j[i].h = glos_thread(sleeper, (void *)200, 16384, 0)) != 0;
        }
        extra = glos_thread(sleeper, (void *)10, 16384, 0);
        e = glos_error();
        check(round ? "many-again" : "many", made == GLOS_THREADS && !extra && e == GLOS_E_MEMORY && join_all(j, GLOS_THREADS, 20000),
              "made=%u extra=%x error=%d", made, extra, e);
    }
}

/* ---- the other modes */

static void spinner(void *arg)
{
    struct job *j = arg;
    unsigned isum;
    double fsum;
    for (;;) {
        if (j->n % 3 == 0)
            compute_of(j->n, &isum, &fsum);
        else if (j->n % 3 == 1)
            alloc_thread(j);
        else
            file_once(j);
        j->turns++;
    }
}

static void crasher(void *arg)
{
    glos_sleep(200);
    *(volatile int *)arg = 1;                           /* (a null pointer's) */
}

static void exiter(void *arg)
{
    glos_sleep(200);
    exit((int)arg);
}

static void key_waiter(void *arg)
{
    (void)arg;
    getch();                                            /* (the BIOS's keyboard, through DOS's idle: for ever) */
}

static void idle_first(void)
{
    char ev[GLOS_EV_MAX];
    for (;;)
        glos_event(ev, sizeof ev, GLOS_FOREVER, 0);
}

int main(int argc, char **argv)
{
    static struct job j[4];
    const char *mode = argc > 1 ? argv[1] : "";
    unsigned i;
    int r;

    if ((r = glos_init("THRTEST", 0, 0)) != 0) {
        printf("THR no-app error=%d\n", r);
        return 98;
    }
    for (i = 0; i < 4; i++)
        j[i].n = i;
    if (!strcmp(mode, "crash")) {
        glos_thread(crasher, 0, 0, 0);
        printf("THR crashing\n");
        fflush(stdout);
        idle_first();
    }
    if (!strcmp(mode, "exit")) {
        glos_thread(exiter, (void *)(argc > 2 ? atoi(argv[2]) : 0), 0, 0);
        idle_first();
    }
    if (!strcmp(mode, "spin") || !strcmp(mode, "leave")) {
        for (i = 0; i < 3; i++)
            glos_thread(spinner, &j[i], 0, 0);
        printf("THR spinning\n");
        fflush(stdout);
        if (!strcmp(mode, "leave")) {
            glos_sleep(500);
            return 3;
        }
        idle_first();
    }
    if (!strcmp(mode, "dos")) {
        glos_thread(key_waiter, 0, 0, 0);
        glos_thread(spinner, &j[0], 0, 0);
        printf("THR waiting\n");
        fflush(stdout);
        idle_first();
    }
    if (!strcmp(mode, "quick")) {
        for (i = 0; i < 4; i++)
            j[i].h = glos_thread(i % 2 ? file_thread : compute_thread, &j[i], 0, 0);
        r = join_all(j, 4, 60000);
        printf("THR quick ok=%d\n", r);
        return r ? 0 : 1;
    }
    together();
    locks();
    signals();
    waits();
    events();
    interrupts();
    classes();
    many();
    printf("THR done pass=%d fail=%d\n", pass, fails);
    return fails > 99 ? 99 : fails;
}
