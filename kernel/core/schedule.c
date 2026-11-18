/* The schedule's runner (API 2.0's SHELL SCHEDULE; docs/v1/phase-b.md step 8): a thread that looks at the
 * schedule in GLOS.DAT (kernel/app/api2start.c writes it: "sched NAME<tab>WHEN<tab>SINCE<tab>RUNS<tab>COMMAND")
 * a few times a minute and starts what is due as a program of nobody's (the agent's queue: it waits there for
 * a full-screen program's end). The rules of what is due are sdk/config/sched.c's. Agent mode only: there is no
 * GLOS.DAT and nobody to start a program otherwise.
 *
 * A run that ends with a code other than 0 (126: its program couldn't be run) is said: a line in the log, and one
 * of GLOS's own notifications on the desktop (phase D). The agent tells of every job's end; the runner knows its
 * own by their numbers, and says so from its own thread. */
#include <string.h>

#include "agent.h"
#include "clock.h"
#include "kapi.h"
#include "glos/bootinfo.h"
#include "glos/sched.h"
#include "kcfg.h"
#include "kdata.h"
#include "kprintf.h"
#include "sched.h"
#include "timer.h"
#include "desk.h"
#include "display.h"
#include "io.h"

#define EVERY (15 * 1024u)              /* ticks between looks */
#define RUNS  8                         /* runs remembered until they end */

/* (state: 1 running, 2 ended badly, 3 that is in the log and the desktop is still to be told) */
static struct { u32 seq; u8 state; u8 code; char name[24]; } runs[RUNS];
static struct waitq wq;

/* (The VM thread, or an app's: only noted here, and the runner woken.) */
static void job_done(u32 seq, u32 code)
{
    u32 f = irq_save(), i;
    for (i = 0; i < RUNS; i++)
        if (runs[i].state == 1 && runs[i].seq == seq) {
            runs[i].state = code ? 2 : 0;
            runs[i].code = (u8)code;
            if (code)
                thread_wake(&wq);
        }
    irq_restore(f);
}

static void remember(u32 seq, const char *name)
{
    u32 f = irq_save(), i;
    for (i = 0; i < RUNS && runs[i].state; i++) ;
    if (i < RUNS) {
        runs[i].seq = seq;
        runs[i].state = 1;
        strncpy(runs[i].name, name, sizeof runs[i].name - 1);
        runs[i].name[sizeof runs[i].name - 1] = 0;
    }
    irq_restore(f);
}

/* The runs that ended badly since the last look: in the log at once; on the desktop when there is one on the
   screen (a run at GLOS's start ends before the desktop is up: said when it is). */
static void say_failed(void)
{
    char text[96];
    u32 i;
    for (i = 0; i < RUNS; i++) {
        if (runs[i].state == 2) {
            kprintf("GLOS-SCHED failed name=%s code=%u\n", runs[i].name, runs[i].code);
            runs[i].state = 3;                  /* (in the log: the desktop's notification is still owed) */
        }
        if (runs[i].state == 3 && display_fb()) {
            if (runs[i].code == 126)
                ksnprintf(text, sizeof text, "\"%s\" couldn't be run: its program wasn't found.", runs[i].name);
            else
                ksnprintf(text, sizeof text, "\"%s\" ended with an error (code %u).", runs[i].name, runs[i].code);
            runs[i].state = 0;
            desk_notify_own("Schedule", text);
        }
    }
}

static u32 num(const char *s)
{
    u32 v = 0;
    for (; *s >= '0' && *s <= '9'; s++)
        v = v * 10 + (u32)(*s - '0');
    return v;
}

static void turn(int first)
{
    static struct krun run;         /* (this thread's alone) */
    char rec[KDATA_TEXT + 1], again[KDATA_TEXT + 1];
    u32 n = kdata_count("sched"), i, k, seq;
    for (i = 0; i < n; i++) {
        struct sched_when w;
        char *f[5];
        u32 since, now = clock_utc();
        long off = clock_offset();
        if (kdata_get("sched", i, rec, sizeof rec) < 0)
            continue;
        f[0] = rec;
        for (k = 1; k < 5; k++) {       /* name, when, since, runs, command */
            char *t = strchr(f[k - 1], '\t');
            if (!t)
                break;
            *t = 0;
            f[k] = t + 1;
        }
        if (k < 5 || sched_parse(f[1], &w) != 0)
            continue;
        since = num(f[2]);
        if (w.kind == SCHED_START ? !first : !sched_due(&w, (u32)((long)since + off), (u32)((long)now + off)))
            continue;
        memset(&run, 0, sizeof run);
        run.size = sizeof run;
        run.flags = KRUN_DETACH;
        strncpy(run.command, f[4], sizeof run.command - 1);
        if (agent_post_app(0, &run, &seq) != 0) {
            kprintf("GLOS-SCHED error=queue name=%s\n", f[0]);
            continue;                   /* (the agent's queue is full: at the next look) */
        }
        remember(seq, f[0]);
        kprintf("GLOS-SCHED run name=%s when=\"%s\" seq=%u\n", f[0], f[1], seq);
        ksnprintf(again, sizeof again, "%s\t%s\t%u\t%u\t%s", f[0], f[1], now, num(f[3]) + 1, f[4]);
        kdata_set("sched", i, again);
    }
}

static void runner(void *arg)
{
    int first = 1;
    (void)arg;
    while (!kcfg_settled())             /* (GLOS.DAT is read with GLOS.CFG; the agent starts nothing before) */
        thread_sleep_until(timer_ticks() + 256);
    agent_on_done(job_done);
    for (;;) {
        u32 until = timer_ticks() + EVERY, f;
        turn(first);
        first = 0;
        do {                                    /* (woken early by a run that ended badly: said at once) */
            say_failed();
            f = irq_save();
            thread_wait(&wq, until);
            irq_restore(f);
        } while ((s32)(timer_ticks() - until) < 0);
        say_failed();
    }
}

void schedule_init(const struct bootinfo *bi)
{
    if (bi->flags & BI_F_AGENT)
        thread_create("schedule", PRIO_BULK, runner, 0);
}
