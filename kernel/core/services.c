/* Background apps (phase D, docs/v1/phase-d.md D7): [services] run lists programs (their command lines, ";"
 * between them) that GLOS starts with itself, with a desktop or with none, and starts again if they end.
 *
 * Supervision only: each is a job of the agent's that is made an app without asking (so that it never holds
 * DOS), owned by nobody's app but by this file, which is told when it moves on. An app like any other, it waits
 * while a DOS program has the whole PC. One that ends is started again after two seconds, then four, and so on
 * up to five minutes while it keeps ending within a minute of its start; one that ran a minute or more starts
 * again after two seconds. `glos services` lists them; `glos services stop NAME` ends one and leaves it ended;
 * `glos services start NAME` starts it again. A service's name is its program's file name (CHATD.EXE); one
 * written with no folder is beside GLOS.EXE. A change
 * to the setting takes effect at once: what is no longer listed is ended, what is new is started.
 *
 * A service is a GLOS app, or a 32-bit DPMI program GLOS can make one. Anything else would run as a DOS program
 * for as long as it liked, with DOS its own and the apps paused: one that is no app twenty seconds after its
 * start is ended and left so ("refused"), until glos services start NAME or a change to the setting. */
#include <string.h>

#include "agent.h"
#include "glos/api.h"
#include "glos/bootinfo.h"
#include "io.h"
#include "kapi.h"
#include "kcfg.h"
#include "kprintf.h"
#include "sched.h"
#include "services.h"
#include "timer.h"
#include "vm.h"

#define SVC_MAX   8
#define TICKS     1024u                 /* a second */
#define PAUSE_MIN (2 * TICKS)
#define PAUSE_MAX (300 * TICKS)
#define RAN_WELL  (60 * TICKS)          /* one that ran this long starts again with the least pause */
#define APP_BY    (20 * TICKS)          /* one that is no app this long after its start is no service */

/* To be started (at `next`); running; ended by hand; ended by us, being no app. */
enum { S_FREE, S_WAIT, S_RUN, S_OFF, S_BAD };

struct svc {
    char name[16];                      /* its program's file name */
    char cmd[160];
    u8 state, want;                     /* S_; asked for by glos services: 1 stop, 2 start */
    u8 listed;                          /* (while the setting is read: still in it) */
    u32 seq;                            /* its job, while it runs */
    u32 next, pause, started, restarts, code;
};

static struct svc svcs[SVC_MAX];
static struct waitq wq;
static int owner;                       /* (its address: whose the jobs are) */
static volatile u8 changed = 1;         /* the setting is to be read (again) */

static void wake(void)
{
    u32 f = irq_save();
    thread_wake(&wq);
    irq_restore(f);
}

static void notify(void *o) { (void)o; wake(); }       /* a job of ours moved on */

void services_changed(void)
{
    changed = 1;
    wake();
}

/* A command line's program's file name, in capitals. */
static void name_of(const char *cmd, char *out, u32 room)
{
    const char *p = cmd, *base = cmd;
    u32 n = 0;
    for (; *p && *p != ' '; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            base = p + 1;
    for (p = base; *p && *p != ' ' && n < room - 1; p++)
        out[n++] = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    out[n] = 0;
}

static struct svc *by_name(const char *name)
{
    char up[16];
    u32 i;
    name_of(name, up, sizeof up);
    for (i = 0; i < SVC_MAX; i++)
        if (svcs[i].state != S_FREE && !strcmp(svcs[i].name, up))
            return &svcs[i];
    return 0;
}

static void end(struct svc *s)          /* its job ended by us, and forgotten */
{
    if (s->state == S_RUN) {
        agent_app_kill(&owner, s->seq);
        agent_app_forget(&owner, s->seq);
    }
}

/* [services] run as it is now: the ones no longer in it end, the new ones are to be started. */
static void read_setting(void)
{
    char list[256], cmd[160], name[16];
    const char *p;
    struct svc *s;
    u32 i, n;
    list[0] = 0;
    kcfg_get("services", "run", list, sizeof list);
    for (i = 0; i < SVC_MAX; i++)
        svcs[i].listed = 0;
    for (p = list; *p;) {
        while (*p == ' ' || *p == ';')
            p++;
        for (n = 0; *p && *p != ';' && n < sizeof cmd - 1; p++)
            cmd[n++] = *p;
        while (n && cmd[n - 1] == ' ')
            n--;
        cmd[n] = 0;
        if (!n)
            continue;
        name_of(cmd, name, sizeof name);
        if (!name[0])
            continue;
        if ((s = by_name(name)) != 0) {         /* there already: another command line is for its next start */
            s->listed = 1;
            if (strcmp(s->cmd, cmd) && s->state == S_BAD) {
                s->state = S_WAIT;              /* (and a refused one's is tried) */
                s->pause = PAUSE_MIN;
                s->next = timer_ticks();
            }
            strcpy(s->cmd, cmd);
            continue;
        }
        for (i = 0; i < SVC_MAX && svcs[i].state != S_FREE; i++) ;
        if (i == SVC_MAX) {
            kprintf("GLOS-SERVICE error=too-many name=%s\n", name);
            continue;
        }
        s = &svcs[i];
        memset(s, 0, sizeof *s);
        strcpy(s->name, name);
        strcpy(s->cmd, cmd);
        s->state = S_WAIT;
        s->listed = 1;
        s->pause = PAUSE_MIN;
        s->next = timer_ticks();
    }
    for (i = 0; i < SVC_MAX; i++)
        if (svcs[i].state != S_FREE && !svcs[i].listed) {
            kprintf("GLOS-SERVICE gone name=%s\n", svcs[i].name);
            end(&svcs[i]);
            svcs[i].state = S_FREE;
        }
}

static void start(struct svc *s)
{
    static struct krun run;             /* (one thread fills and posts it) */
    struct agent_opts o;
    const char *p;
    memset(&run, 0, sizeof run);
    memset(&o, 0, sizeof o);
    run.size = sizeof run;
    for (p = s->cmd; *p && *p != ' ' && *p != ':' && *p != '\\' && *p != '/'; p++) ;
    if (!*p || *p == ' ') {                     /* named with no folder: beside GLOS.EXE, as [desktop] start's */
        char home[80];
        vm_glos_home(home, sizeof home);
        ksnprintf(run.command, sizeof run.command, "%s%s%s", home, home[strlen(home) - 1] == '\\' ? "" : "\\", s->cmd);
    } else
        strncpy(run.command, s->cmd, sizeof run.command - 1);
    o.keep = 1;
    o.exclusive = 2;                    /* (made an app without asking: it never holds DOS) */
    o.notify = notify;
    if (agent_post_job(&owner, &run, &o, &s->seq) != 0) {
        s->next = timer_ticks() + PAUSE_MIN;    /* (the agent's queue is full: soon again) */
        return;
    }
    s->state = S_RUN;
    s->started = timer_ticks();
    kprintf("GLOS-SERVICE start name=%s seq=%u\n", s->name, s->seq);
}

static void turn(void)
{
    u32 now = timer_ticks(), i, st, code, nout, nerr, ticks, wait;
    u8 drop[256];
    if (changed) {
        changed = 0;
        read_setting();
    }
    for (i = 0; i < SVC_MAX; i++) {
        struct svc *s = &svcs[i];
        u8 want = s->want;
        if (s->state == S_FREE)
            continue;
        s->want = 0;
        if (want == 1 && s->state != S_OFF) {   /* glos services stop */
            end(s);
            s->state = S_OFF;
            kprintf("GLOS-SERVICE stop name=%s\n", s->name);
        } else if (want == 2 && (s->state == S_OFF || s->state == S_BAD)) {     /* glos services start */
            s->state = S_WAIT;
            s->pause = PAUSE_MIN;
            s->next = now;
        }
        if (s->state == S_RUN) {
            while (agent_app_read1(&owner, s->seq, 0, drop, sizeof drop) > 0) ;         /* (what it writes is nobody's) */
            while (agent_app_read1(&owner, s->seq, 1, drop, sizeof drop) > 0) ;
            if (agent_app_status(&owner, s->seq, &st, &code, &nout, &nerr, &ticks) != 0 || st == GLOS_JS_DONE) {
                agent_app_forget(&owner, s->seq);
                s->code = code;
                wait = now - s->started >= RAN_WELL ? PAUSE_MIN : s->pause;
                s->pause = wait * 2 > PAUSE_MAX ? PAUSE_MAX : wait * 2;
                s->state = S_WAIT;
                s->next = now + wait;
                s->restarts++;
                kprintf("GLOS-SERVICE ended name=%s code=%u again=%us\n", s->name, code, wait / TICKS);
            } else if (st != GLOS_JS_APP && st != GLOS_JS_QUEUED && ticks >= APP_BY) {
                end(s);
                s->state = S_BAD;
                kprintf("GLOS-SERVICE refused name=%s why=not-an-app\n", s->name);
            }
        }
        /* (Not while GLOS leaves: the exit ends the apps, and one started again would hold it up.) */
        if (s->state == S_WAIT && (s32)(now - s->next) >= 0 && !agent_exiting())
            start(s);
    }
}

static void runner(void *arg)
{
    (void)arg;
    while (!kcfg_settled())             /* (the agent starts nothing before GLOS.CFG is read) */
        thread_sleep_until(timer_ticks() + 256);
    for (;;) {
        u32 f;
        turn();
        f = irq_save();
        thread_wait(&wq, timer_ticks() + TICKS);
        irq_restore(f);
    }
}

void services_init(const struct bootinfo *bi)
{
    if (bi->flags & BI_F_AGENT)
        thread_create("services", PRIO_BULK, runner, 0);
}

/* glos services: a line each. */
u32 services_stat(char *buf, u32 max)
{
    static const char *const states[] = { "", "waiting", "running", "stopped", "refused" };
    u32 n = 0, i, any = 0;
    for (i = 0; i < SVC_MAX && n < max; i++) {
        const struct svc *s = &svcs[i];
        if (s->state == S_FREE)
            continue;
        any = 1;
        n += (u32)ksnprintf(buf + n, max - n, "service %s %s seq=%u restarts=%u code=%u \"%s\"\n", s->name, states[s->state],
                            s->state == S_RUN ? s->seq : 0, s->restarts, s->code, s->cmd);
    }
    if (!any)
        n = (u32)ksnprintf(buf, max, "no services ([services] run)\n");
    return n < max ? n : max;
}

/* glos services stop NAME (what 1), start NAME (2): 0, or -1 no such service. */
int services_ask(const char *name, int what)
{
    struct svc *s = by_name(name);
    if (!s)
        return -1;
    s->want = (u8)what;
    wake();
    return 0;
}
