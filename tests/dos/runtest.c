/* RUNTEST (GLOS M9 step C): an app that starts programs (JOB), watches
 * directories (FS), reads SYS's figures, and is the shell.
 * For the console suite (tests/loopa/jobs.py console). DJGPP, libglos.
 *
 *   RUNTEST run [--dir D] [--full] [--detach] [--type KEYS] [--after TEXT] COMMAND...
 *                     COMMAND as a console job: its output written out as it
 *                     comes ("OUT <text>", C escapes), its state's changes
 *                     ("STATE n"), at its end "DONE code=N"; KEYS typed into
 *                     it (\n Enter, \c Ctrl-C) once TEXT has come (or at once)
 *   RUNTEST beside S  an app that counts for S seconds: "BESIDE n"
 *   RUNTEST watch DIR S
 *                     "FILES" for each change there in S seconds
 *   RUNTEST stat      "SYS ..." and an "APP ..." line for each app and job
 *   RUNTEST shell S   the desktop's shell for S seconds, with a desktop
 *                     window: "LAUNCH x y", "SYSKEY" as they come
 *   RUNTEST kill ID   app ID ended: "KILLED"
 *
 * "RUNTEST-OK" at the end of each. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/gfx.h>

static void out(const char *what, const char *s, int n)
{
    char line[512];
    int o = sprintf(line, "%s ", what), i;
    for (i = 0; i < n && o < (int)sizeof line - 8; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\')
            o += sprintf(line + o, "\\\\");
        else if (c == '\r')
            o += sprintf(line + o, "\\r");
        else if (c == '\n')
            o += sprintf(line + o, "\\n");
        else if (c < 0x20 || c >= 0x7F)
            o += sprintf(line + o, "\\x%02x", c);
        else
            line[o++] = (char)c;
    }
    line[o] = 0;
    printf("%s\n", line);
    fflush(stdout);
}

/* A character as a key the BIOS would give: its scan code above it (the US layout). */
static unsigned short key_of(char c)
{
    static const char plain[] = "\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./";
    static const char shift[] = "\0!@#$%^&*()_+\0\0QWERTYUIOP{}\0\0ASDFGHJKL:\"~\0|ZXCVBNM<>?";
    unsigned i;
    if (c == '\n')
        return 0x1C0D;
    if (c == ' ')
        return 0x3920;
    if (c == 3)
        return 0x2E03;                          /* Ctrl-C */
    for (i = 0; i < sizeof plain - 1; i++)
        if (plain[i] == c)
            return (unsigned short)(((i + 1) << 8) | (unsigned char)c);
    for (i = 0; i < sizeof shift - 1; i++)
        if (shift[i] == c)
            return (unsigned short)(((i + 1) << 8) | (unsigned char)c);
    return (unsigned char)c;
}

/* A job's state as an event told it (GLOS_EV_JOB): apps pause while a job has the screen, so the reads may never
   see GLOS_JS_FULL, but its event waits for them. */
static void job_event(const struct glos_ev *ev, glos_handle job, unsigned *last)
{
    const struct glos_ev_job *j = (const struct glos_ev_job *)ev;
    if (ev->kind == GLOS_EV_JOB && ev->handle == job && j->state != *last) {
        printf("STATE %u\n", j->state);
        fflush(stdout);
        *last = j->state;
    }
}

static union {
    struct glos_ev ev;
    unsigned char room[GLOS_EV_MAX];
} evbuf;

static int run(int argc, char **argv)
{
    struct glos_job_run r;
    struct glos_job_status js;
    const char *keys = 0, *after = 0, *dir = 0;
    char buf[1024], seen[4096], command[1024] = "";
    glos_handle job;
    unsigned st = 99, code = 0, last = 99;
    int i, typed = 0, n, nseen = 0, now = 0, nowtyped = -1, detach = 0;
    memset(&r, 0, sizeof r);
    r.how = GLOS_RUN_WINDOW;
    for (i = 2; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc)
            dir = argv[++i];
        else if (!strcmp(argv[i], "--full"))
            r.how = GLOS_RUN_FULL;
        else if (!strcmp(argv[i], "--detach"))
            detach = 1;
        else if (!strcmp(argv[i], "--type") && i + 1 < argc)
            keys = argv[++i];
        else if (!strcmp(argv[i], "--after") && i + 1 < argc)
            after = argv[++i];
        else if (!strcmp(argv[i], "--now"))
            now = 1;                            /* --type's keys at once, queued with the job */
    }
    for (; i < argc; i++) {
        strncat(command, argv[i], sizeof command - strlen(command) - 2);
        if (i + 1 < argc)
            strcat(command, " ");
    }
    r.command = (unsigned)command;
    r.dir = (unsigned)dir;
    if (detach)
        r.how = GLOS_RUN_HIDDEN, r.flags = GLOS_RUNF_DETACH;
    job = glos_job_start(&r);
    if (detach ? glos_error() != 0 : !job) {
        printf("RUN refused %d\n", glos_error());
        return 1;
    }
    if (keys && now) {                          /* (before any DOS call of its own, which may wait for the job) */
        unsigned short k[128];
        int m = 0;
        const char *p;
        for (p = keys; *p && m < 128; p++)
            k[m++] = key_of(*p);
        nowtyped = glos_job_keys(job, k, (unsigned)m);
        typed = 1;
    }
    printf("RUN id=%u\n", r.id);
    if (nowtyped >= 0)
        printf("TYPED %d\n", nowtyped);
    fflush(stdout);
    if (detach)
        return 0;
    for (;;) {
        if (glos_job_status(job, &js) != 0      /* (its state first: once it is done, what is read is all of it) */
            || (n = glos_job_read(job, GLOS_JOB_OUT, buf, sizeof buf)) < 0) {
            printf("RUN lost\n");
            return 1;
        }
        st = js.state, code = js.code;
        if (n > 0) {
            out("OUT", buf, n);
            if (nseen + n < (int)sizeof seen - 1) {
                memcpy(seen + nseen, buf, (size_t)n);
                nseen += n;
                seen[nseen] = 0;
            }
        }
        if (st != last) {
            printf("STATE %u\n", st);
            fflush(stdout);
            last = st;
        }
        if (keys && !typed && st == GLOS_JS_RUNNING && (!after || strstr(seen, after))) {
            unsigned short k[128];
            int m = 0;
            const char *p;
            for (p = keys; *p && m < 128; p++) {
                char c = *p;
                if (c == '\\' && p[1]) {
                    p++;
                    c = *p == 'n' ? '\n' : *p == 'c' ? 3 : *p;
                }
                k[m++] = key_of(c);
            }
            printf("TYPED %d\n", glos_job_keys(job, k, (unsigned)m));
            fflush(stdout);
            typed = 1;
        }
        if (st == GLOS_JS_DONE && n < (int)sizeof buf)
            break;                              /* (its last output) */
        if (n == 0 && glos_event(&evbuf, sizeof evbuf, 200, 0) > 0)    /* (GLOS_EV_JOB when there is more, or a moment) */
            job_event(&evbuf.ev, job, &last);
    }
    while (glos_event(&evbuf, sizeof evbuf, 0, 0) > 0)  /* states it was told of while it was paused (a full-screen job's) */
        job_event(&evbuf.ev, job, &last);
    {                                           /* keys typed that it didn't read: ours again (type-ahead) */
        unsigned short left[64];
        int i, nl = glos_job_read(job, GLOS_JOB_KEYS, left, sizeof left) / 2;
        if (nl > 0) {
            printf("LEFT ");
            for (i = 0; i < nl; i++)
                putchar(left[i] & 0xFF);
            printf("\nLEFT-WORDS");
            for (i = 0; i < nl; i++)
                printf(" %04x", left[i]);
            printf("\n");
        }
    }
    glos_close(job);
    printf("DONE code=%u\n", code);
    return 0;
}

static int beside(int secs)
{
    unsigned t0 = glos_ticks(), n = 0;
    while (glos_ticks() - t0 < (unsigned)secs * 1000) {
        n++;
        if (!(n & 1023))
            glos_yield();
    }
    printf("BESIDE %u\n", n);
    return 0;
}

static int watch(const char *dir, int secs)
{
    glos_handle id = glos_fs_watch(dir);
    unsigned t0 = glos_ticks();
    if (!id) {
        printf("WATCH refused %d\n", glos_error());
        return 1;
    }
    printf("WATCH id=%u\n", id);
    fflush(stdout);
    while (glos_ticks() - t0 < (unsigned)secs * 1000)
        if (glos_event(&evbuf, sizeof evbuf, 200, 0) > 0 && evbuf.ev.kind == GLOS_EV_FS) {
            printf("FILES %u\n", evbuf.ev.handle);
            fflush(stdout);
        }
    return 0;
}

static int stat(void)
{
    struct glos_sys_system s;
    struct glos_sys_memory m;
    struct glos_version v;
    static struct glos_program progs[48];
    static char buf[2048];
    unsigned count = 0, i;
    int n;
    memset(&s, 0, sizeof s);
    memset(&m, 0, sizeof m);
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    if (glos_version(&v) != 0 || glos_sys_stat(GLOS_STAT_SYSTEM, &s, sizeof s, 0, 0) != 0
        || glos_sys_stat(GLOS_STAT_MEMORY, &m, sizeof m, 0, 0) != 0)
        return 1;
    printf("SYS version=%x uptime=%u busy=%u idle=%u cpu=%u mem=%u free=%u heap=%u threads=%u apps=%u windows=%u "
           "screen=%ux%ux%u accel=%u vram=%u mixer=%u streams=%u underruns=%u ip=%u.%u.%u.%u rx=%u tx=%u "
           "nic=%s host=%s card=%s session=%s\n", v.major << 16 | v.minor, s.uptime_ms, s.busy_ms, s.idle_ms,
           s.cpu_family, m.total_kb, m.free_kb, m.heap_kb, s.threads, s.apps, s.windows, s.screen_w, s.screen_h,
           s.screen_bpp, s.accel, m.vram_kb, s.mixer_rate, s.streams, s.underruns, s.ip[0], s.ip[1], s.ip[2], s.ip[3],
           s.net_rx, s.net_tx, s.nic, s.hostname, s.card, s.session[0] ? s.session : "-");
    n = glos_programs(progs, sizeof progs, &count);
    for (i = 0; (n == 0 || n == GLOS_E_ROOM) && i < count; i++) {
        const struct glos_program *a = &progs[i];
        printf("APP kind=%u id=%u name=%s busy=%u mem=%u windows=%u state=%u\n", a->kind, a->id, a->name, a->busy_ms,
               a->mem_kb, a->windows, a->state);
    }
    n = glos_sys_log(0, buf, sizeof buf, 0, 0);
    printf("LOG bytes=%d\n", n);
    return 0;
}

static int shell(int secs)
{
    struct glos_pixels px;
    struct gfx_surface s;
    glos_handle w;
    unsigned t0 = glos_ticks();
    memset(&px, 0, sizeof px);
    px.size = sizeof px;
    if (!(w = glos_window("RUNTEST Desktop", 0, 0, 1, 1, GLOS_WK_DESKTOP, 0, 0)) || glos_pixels(w, &px) != 0
        || !glos_ptr(px.pixels)) {
        printf("SHELL no-window %d\n", glos_error());
        return 1;
    }
    {
        struct gfx_fmt f = { (unsigned char)px.format.bpp, px.format.rpos, px.format.rsize, px.format.gpos,
                             px.format.gsize, px.format.bpos, px.format.bsize };
        gfx_init(&s, glos_ptr(px.pixels), px.w, px.h, px.pitch, &f);
    }
    gfx_fill(&s, 0, 0, px.w, px.h, 0x336699);
    glos_present(w, 0, 0, 0, 0);
    glos_show(w, GLOS_WS_NORMAL);
    if (glos_shell(1) != 0) {
        printf("SHELL refused\n");
        return 1;
    }
    printf("SHELL window=%u %dx%d\n", w, px.w, px.h);
    fflush(stdout);
    while (glos_ticks() - t0 < (unsigned)secs * 1000)
        if (glos_event(&evbuf, sizeof evbuf, 200, 0) > 0 && evbuf.ev.kind == GLOS_EV_SHELL) {
            const struct glos_ev_shell *e = (const struct glos_ev_shell *)&evbuf.ev;
            if (e->what == GLOS_SHELL_LAUNCH)
                printf("LAUNCH %d %d\n", e->x, e->y);
            else if (e->what == GLOS_SHELL_TASKS)
                printf("SYSKEY\n");
            fflush(stdout);
        }
    return 0;
}

int main(int argc, char **argv)
{
    int r = 2;
    if (glos_init("RUNTEST", "", "") != 0) {
        printf("RUNTEST no GLOS\n");
        return 1;
    }
    if (argc > 2 && !strcmp(argv[1], "run"))
        r = run(argc, argv);
    else if (argc > 2 && !strcmp(argv[1], "beside"))
        r = beside(atoi(argv[2]));
    else if (argc > 3 && !strcmp(argv[1], "watch"))
        r = watch(argv[2], atoi(argv[3]));
    else if (argc > 1 && !strcmp(argv[1], "stat"))
        r = stat();
    else if (argc > 2 && !strcmp(argv[1], "shell"))
        r = shell(atoi(argv[2]));
    else if (argc > 2 && !strcmp(argv[1], "kill")) {
        r = glos_sys_kill(GLOS_KILL_APP, (unsigned)atoi(argv[2]));
        printf(r == 0 ? "KILLED\n" : "KILL refused\n");
    }
    printf("RUNTEST-OK\n");
    return r;
}
