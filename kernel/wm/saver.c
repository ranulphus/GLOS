/* A saver that is a program (phase D, docs/v1/phase-d.md D6): [saver] saver names one, whose program is
 * NAME.SCR beside GLOS.EXE: a GLOS app that puts a full-screen window up and draws in it.
 *
 * The desktop's thread asks each turn while the saver is on (saver_turn). The first time, the program is started,
 * as a job of the kernel's own (as a background app is: services.c), and the desktop goes on composing, so that
 * the program's window shows when it comes. Once the program has a full-screen window up, that is the saver. If
 * the program isn't there, ends, has no such window ten seconds on, or the screen's power goes off, the blank
 * saver takes over (the answer 0): the screen is never left as it was. When the saver ends (the first input, as
 * ever) the program is ended (saver_end). */
#include <string.h>

#include "agent.h"
#include "app.h"
#include "display.h"
#include "glos/api.h"
#include "kapi.h"
#include "kcfg.h"
#include "kprintf.h"
#include "saver.h"
#include "timer.h"
#include "vm.h"
#include "desk.h"

#define COME_BY (10 * 1024u)            /* its window is up this long after its start, or it is no saver */

enum { S_NONE, S_STARTED, S_SHOWN, S_BLANK };

static int owner;                       /* (its address: whose the job is) */
static u32 seq, started;
static u8 state;
static char name[12];

static void notify(void *o) { (void)o; desk_kick(); }   /* its job moved on: looked at next turn */

static void end(const char *why)
{
    agent_app_kill(&owner, seq);
    agent_app_forget(&owner, seq);
    state = S_BLANK;
    kprintf("GLOS-IDLE saver program=%s %s: blank\n", name, why);
}

/* A full-screen window of the job's app is shown. */
static int shown(const struct wm *m)
{
    const struct wm_window *w;
    for (w = m->top; w; w = w->below)
        if ((w->flags & (WF_SHOWN | WF_FULL)) == (WF_SHOWN | WF_FULL) && w->owner && app_began_as(app_id(w->owner)) == seq)
            return 1;
    return 0;
}

int saver_turn(const struct wm *m)
{
    u32 st, code, nout, nerr, ticks;
    u8 drop[128];
    if (state == S_NONE) {                      /* the saver has just come on */
        static struct krun run;                 /* (the desktop's thread alone fills and posts it) */
        struct agent_opts o;
        char home[80];
        name[0] = 0;
        kcfg_get("saver", "saver", name, sizeof name);
        state = S_BLANK;
        if (!name[0] || !strcmp(name, "blank"))
            return 0;
        memset(&run, 0, sizeof run);
        memset(&o, 0, sizeof o);
        run.size = sizeof run;
        vm_glos_home(home, sizeof home);
        ksnprintf(run.command, sizeof run.command, "%s%s%s.SCR", home, home[strlen(home) - 1] == '\\' ? "" : "\\", name);
        o.keep = 1;
        o.exclusive = 2;                        /* (an app, whatever it is: it never holds DOS) */
        o.notify = notify;
        if (agent_post_job(&owner, &run, &o, &seq) != 0) {
            kprintf("GLOS-IDLE saver program=%s not started: blank\n", name);
            return 0;
        }
        state = S_STARTED;
        started = timer_ticks();
        kprintf("GLOS-IDLE saver program=%s seq=%u\n", name, seq);
    }
    if (state == S_BLANK)
        return 0;
    while (agent_app_read1(&owner, seq, 0, drop, sizeof drop) > 0) ;    /* (what it writes is nobody's) */
    while (agent_app_read1(&owner, seq, 1, drop, sizeof drop) > 0) ;
    if (agent_app_status(&owner, seq, &st, &code, &nout, &nerr, &ticks) != 0 || st == GLOS_JS_DONE) {
        agent_app_forget(&owner, seq);
        state = S_BLANK;
        kprintf("GLOS-IDLE saver program=%s ended code=%u: blank\n", name, code);
    } else if (display_power_now() != DISPLAY_POWER_ON) {
        end("screen-off");                      /* (nobody sees its pictures) */
    } else if (shown(m)) {
        if (state == S_STARTED)
            kprintf("GLOS-IDLE saver program=%s shown after=%u ms\n", name, (timer_ticks() - started) * 1000 / 1024);
        state = S_SHOWN;
    } else if (state == S_SHOWN) {
        end("window-gone");
    } else if (timer_ticks() - started >= COME_BY) {
        end("no-window");
    }
    return state != S_BLANK;
}

void saver_end(void)
{
    if (state == S_STARTED || state == S_SHOWN) {
        agent_app_kill(&owner, seq);
        agent_app_forget(&owner, seq);
        kprintf("GLOS-IDLE saver program=%s ended by the saver's end\n", name);
    }
    state = S_NONE;
}
