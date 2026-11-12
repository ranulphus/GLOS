/* The schedule's runner (API 2.0's SHELL SCHEDULE; docs/v1/phase-b.md step 8): a thread that looks at the
 * schedule in GLOS.DAT (kernel/app/api2start.c writes it: "sched NAME<tab>WHEN<tab>SINCE<tab>RUNS<tab>COMMAND")
 * a few times a minute and starts what is due as a program of nobody's (the agent's queue: it waits there for
 * a full-screen program's end). The rules of what is due are sdk/config/sched.c's. Agent mode only: there is no
 * GLOS.DAT and nobody to start a program otherwise. */
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

#define EVERY (15 * 1024u)              /* ticks between looks */

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
    for (;;) {
        turn(first);
        first = 0;
        thread_sleep_until(timer_ticks() + EVERY);
    }
}

void schedule_init(const struct bootinfo *bi)
{
    if (bi->flags & BI_F_AGENT)
        thread_create("schedule", PRIO_BULK, runner, 0);
}
