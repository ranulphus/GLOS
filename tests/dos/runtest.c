/* RUNTEST (GLOS M9 step C): an app that starts programs (GLOS_FN_RUN,
 * GLOS_FN_JOB), watches directories, reads GLOS_FN_STAT, and is the shell.
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

#include <glos/api.h>
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
   see GLOS_JOB_FULL, but its event waits for them. */
static void job_event(const struct glos_event *ev, unsigned id, unsigned *last)
{
    if (ev->type == GLOS_EV_JOB && ev->key == id && (unsigned)ev->x != *last) {
        printf("STATE %u\n", (unsigned)ev->x);
        fflush(stdout);
        *last = (unsigned)ev->x;
    }
}

static int run(int argc, char **argv)
{
    struct glos_run r;
    struct glos_event ev;
    const char *keys = 0, *after = 0;
    char buf[1024], seen[4096];
    unsigned id, st = 99, code = 0, last = 99;
    int i, typed = 0, n, nseen = 0, now = 0, nowtyped = -1;
    memset(&r, 0, sizeof r);
    r.flags = GLOS_RUN_CONSOLE;
    for (i = 2; i < argc && argv[i][0] == '-' && argv[i][1] == '-'; i++) {
        if (!strcmp(argv[i], "--dir") && i + 1 < argc)
            strncpy(r.dir, argv[++i], sizeof r.dir - 1);
        else if (!strcmp(argv[i], "--full"))
            r.flags |= GLOS_RUN_FULL;
        else if (!strcmp(argv[i], "--detach"))
            r.flags = GLOS_RUN_DETACH;
        else if (!strcmp(argv[i], "--type") && i + 1 < argc)
            keys = argv[++i];
        else if (!strcmp(argv[i], "--after") && i + 1 < argc)
            after = argv[++i];
        else if (!strcmp(argv[i], "--now"))
            now = 1;                            /* --type's keys at once, queued with the job */
    }
    for (; i < argc; i++) {
        strncat(r.command, argv[i], sizeof r.command - strlen(r.command) - 2);
        if (i + 1 < argc)
            strcat(r.command, " ");
    }
    if ((int)(id = (unsigned)glos_run(&r)) < 0) {
        printf("RUN refused %d\n", glos_error());
        return 1;
    }
    if (keys && now) {                          /* (before any DOS call of its own, which may wait for the job) */
        unsigned short k[128];
        int m = 0;
        const char *p;
        for (p = keys; *p && m < 128; p++)
            k[m++] = key_of(*p);
        nowtyped = glos_job_type(id, k, (unsigned)m);
        typed = 1;
    }
    printf("RUN id=%u\n", id);
    if (nowtyped >= 0)
        printf("TYPED %d\n", nowtyped);
    fflush(stdout);
    if (r.flags & GLOS_RUN_DETACH)
        return 0;
    for (;;) {
        n = glos_job_read(id, buf, sizeof buf, 0, &st, &code);
        if (n < 0) {
            printf("RUN lost\n");
            return 1;
        }
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
        if (keys && !typed && st == GLOS_JOB_RUNNING && (!after || strstr(seen, after))) {
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
            printf("TYPED %d\n", glos_job_type(id, k, (unsigned)m));
            fflush(stdout);
            typed = 1;
        }
        if (st == GLOS_JOB_DONE && n < (int)sizeof buf)
            break;                              /* (its last output: the job is gone now, as the API has it) */
        if (n == 0 && glos_event(&ev, 200))     /* (GLOS_EV_JOB when there is more, or a moment) */
            job_event(&ev, id, &last);
    }
    while (glos_event(&ev, 0))                  /* states it was told of while it was paused (a full-screen job's) */
        job_event(&ev, id, &last);
    {                                           /* keys typed that it didn't read: ours again (type-ahead) */
        unsigned short left[64];
        int i, nl = glos_job_keys(id, left, 64);
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
    int id = glos_watch(dir);
    unsigned t0 = glos_ticks();
    struct glos_event ev;
    if (id < 0) {
        printf("WATCH refused %d\n", glos_error());
        return 1;
    }
    printf("WATCH id=%d\n", id);
    fflush(stdout);
    while (glos_ticks() - t0 < (unsigned)secs * 1000)
        if (glos_event(&ev, 200) && ev.type == GLOS_EV_FILES) {
            printf("FILES %u\n", ev.key);
            fflush(stdout);
        }
    return 0;
}

static int stat(void)
{
    struct glos_stat_system s;
    static unsigned char buf[2048];
    int n, o;
    memset(&s, 0, sizeof s);
    if (glos_stat(GLOS_STAT_SYSTEM, &s, sizeof s) < 0)
        return 1;
    printf("SYS version=%x uptime=%u busy=%u idle=%u cpu=%u mem=%u free=%u heap=%u threads=%u apps=%u windows=%u "
           "screen=%ux%ux%u accel=%u vram=%u mixer=%u streams=%u underruns=%u ip=%u.%u.%u.%u rx=%u tx=%u "
           "nic=%s host=%s card=%s session=%s\n", s.version, s.uptime_ms, s.busy_ms, s.idle_ms, s.cpu_family, s.mem_kb,
           s.mem_free_kb, s.heap_kb, s.threads, s.apps, s.windows, s.screen_w, s.screen_h, s.screen_bpp, s.accel,
           s.vram_kb, s.mixer_rate, s.streams, s.underruns, s.ip[0], s.ip[1], s.ip[2], s.ip[3], s.net_rx, s.net_tx,
           s.nic, s.hostname, s.card, s.session[0] ? s.session : "-");
    n = glos_stat(GLOS_STAT_APPS, buf, sizeof buf);
    for (o = 0; o + (int)sizeof(struct glos_stat_app) <= n; o += (int)sizeof(struct glos_stat_app)) {
        const struct glos_stat_app *a = (const struct glos_stat_app *)(buf + o);
        printf("APP kind=%u id=%u name=%s busy=%u mem=%u windows=%u state=%u\n", a->kind, a->id, a->name, a->busy_ms,
               a->mem_kb, a->windows, a->state);
    }
    n = glos_stat(GLOS_STAT_LOG, buf, sizeof buf - 1);
    printf("LOG bytes=%d\n", n);
    return 0;
}

static int shell(int secs)
{
    struct glos_window w;
    struct gfx_surface s;
    struct glos_event ev;
    unsigned t0 = glos_ticks();
    memset(&w, 0, sizeof w);
    w.flags = GLOS_WIN_DESKTOP;
    w.w = w.h = 1;
    strcpy(w.title, "RUNTEST Desktop");
    if (glos_window(&w) != 0 || glos_surface(&w, &s) != 0) {
        printf("SHELL no-window %d\n", glos_error());
        return 1;
    }
    gfx_fill(&s, 0, 0, w.w, w.h, 0x336699);
    glos_present(w.id, NULL);
    glos_show(w.id, 1);
    if (glos_shell(1) != 0) {
        printf("SHELL refused\n");
        return 1;
    }
    printf("SHELL window=%u %dx%d\n", w.id, w.w, w.h);
    fflush(stdout);
    while (glos_ticks() - t0 < (unsigned)secs * 1000)
        if (glos_event(&ev, 200)) {
            if (ev.type == GLOS_EV_LAUNCH)
                printf("LAUNCH %d %d\n", ev.x, ev.y);
            else if (ev.type == GLOS_EV_SYSKEY)
                printf("SYSKEY\n");
            fflush(stdout);
        }
    return 0;
}

int main(int argc, char **argv)
{
    int r = 2;
    if (glos_init() != 0) {
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
        r = glos_kill(0, (unsigned)atoi(argv[2]));
        printf(r == 0 ? "KILLED\n" : "KILL refused\n");
    }
    printf("RUNTEST-OK\n");
    return r;
}
