/* API 2.0's SHELL (include/glos/api2.h; docs/v1/phase-b.md step 8): the shell's identity and its events, the
 * tray, notifications, where the task bar's parts are, Help, opening a file or a link, kinds of file and their
 * programs and verbs.
 *
 * The desktop draws the tray and the notifications (kernel/wm/deskshell.c) and tells of presses there; this is
 * the apps' side: handles for an icon and a notification, their events, and the settings behind OPEN and ASSOC
 * (GLOS.CFG's [open], and [program NAME] verbs; the rules are sdk/config/assoc.c's). A program started here is
 * a job of nobody's (it runs on when the app that opened it ends). */
#include <string.h>

#include "agent.h"
#include "api2int.h"
#include "desk.h"
#include "dpmi.h"
#include "glos/assoc.h"
#include "glos/bootinfo.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mm.h"
#include "notes.h"
#include "vm.h"
#include "wm.h"

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
        a++, b++;
    return !*a && !*b;
}

/* GLOS's own folder (GLOS.CFG's), without its last backslash. */
static void home(char *out, u32 room)
{
    const char *cfg = vm.bi->cfg_path;
    u32 i, n = 0;
    for (i = 0; cfg[i]; i++)
        if (cfg[i] == '\\')
            n = i;
    if (n >= room)
        n = room - 1;
    memcpy(out, cfg, n);
    out[n] = 0;
}

/* The handle of an app's for a tray icon or a notification (interrupts off). */
static u32 handle_of(struct app *a, u8 kind, u32 id)
{
    u32 i = 0, h;
    void *obj;
    while (a->v2 && handle_next(&a->v2->h, &i, &h, &obj) == 0)
        if (GLOS_HANDLE_KIND(h) == kind && (u32)obj == id)
            return h;
    return 0;
}

/* ---- DESKTOP */

static int desktop(struct app *a, struct trapframe *tf)
{
    struct glos_shell_desktop q;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!q.on) {
        desk_shell_set(a, 0);
        return a2_ok(tf, 0);
    }
    if (!app_own(a) || desk_shell_set(a, 1) != 0)       /* (GLOS's own programs only; one at a time) */
        return a2_fail(tf, GLOS_E_REFUSED);
    return a2_ok(tf, 0);
}

/* ---- TRAY */

static int icon_in(struct trapframe *tf, u32 off, u32 *icon)
{
    return off && user_rd((u16)tf->ds, off, icon, 16 * 16 * 4) == 0 ? 0 : -1;
}

static int tray(struct app *a, struct trapframe *tf)
{
    struct glos_tray q;
    u32 icon[16 * 16], size, id, f;
    char tip[64];
    tip[0] = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || icon_in(tf, q.icon, icon) != 0 || (q.tip && a2_text_in(tf, q.tip, tip, sizeof tip) < 0))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = desk_tray_add(a, icon, tip)))
        return a2_fail(tf, GLOS_E_MEMORY);      /* (the tray is full) */
    f = irq_save();
    q.handle = handle_new(&a->v2->h, GLOS_HK_TRAY, (void *)id);
    irq_restore(f);
    if (!q.handle || a2_wr(tf, &q, sizeof q, size) != 0) {
        if (q.handle) {
            f = irq_save();
            handle_close(&a->v2->h, q.handle);
            irq_restore(f);
        }
        desk_tray_remove(a, id);
        return a2_fail(tf, q.handle ? GLOS_E_ARGUMENT : GLOS_E_MEMORY);
    }
    return a2_ok(tf, 0);
}

static int t_icon_set(struct app *a, void *obj, s32 v, const char *text, u32 len)
{
    u32 icon[16 * 16];
    (void)a, (void)v;
    if (len != sizeof icon)
        return GLOS_E_ARGUMENT;
    memcpy(icon, text, sizeof icon);
    desk_tray_set((u32)obj, icon, 0);
    return 0;
}

static int t_icon_get(struct app *a, void *obj, s32 *v, char *text, u32 *len)
{
    (void)a, (void)obj, (void)v, (void)text, (void)len;
    return GLOS_E_PROPERTY;                     /* (written only) */
}

static int t_tip_set(struct app *a, void *obj, s32 v, const char *text, u32 len)
{
    char tip[64];
    (void)a, (void)v;
    if (len >= sizeof tip)
        return GLOS_E_ARGUMENT;
    memcpy(tip, text, len);
    tip[len] = 0;
    desk_tray_set((u32)obj, 0, tip);
    return 0;
}

static int t_tip_get(struct app *a, void *obj, s32 *v, char *text, u32 *len)
{
    int n;
    (void)a, (void)v;
    if ((n = desk_tray_tip((u32)obj, text, 64)) < 0)
        return GLOS_E_HANDLE;
    *len = (u32)n;
    return 0;
}

const struct propdef api2_shell_props[] = {
    { GLOS_PROP_TRAY_ICON, P_DATA, t_icon_get, t_icon_set },
    { GLOS_PROP_TRAY_TIP, P_TEXT, t_tip_get, t_tip_set },
};
const u32 api2_shell_nprops = ARRAY_SIZE(api2_shell_props);

void api2_tray_event(struct app *a, u32 id, int what, const struct gfx_rect *r)
{
    struct glos_ev_tray e;
    u32 f = irq_save();
    memset(&e, 0, sizeof e);
    e.ev.handle = handle_of(a, GLOS_HK_TRAY, id);
    irq_restore(f);
    if (!e.ev.handle)
        return;
    e.ev.size = sizeof e;
    e.ev.kind = GLOS_EV_TRAY;
    e.what = (unsigned)what;
    e.x = r->x, e.y = r->y, e.w = r->w, e.h = r->h;
    a2_post(a, &e.ev, 0);
}

/* ---- NOTIFY */

static int notify_fill(struct trapframe *tf, const struct glos_notify *q, struct note *n)
{
    u32 i;
    memset(n, 0, sizeof *n);
    if (a2_text_in(tf, q->title, n->title, sizeof n->title) <= 0 || (q->text && a2_text_in(tf, q->text, n->text, sizeof n->text) < 0))
        return -1;
    for (i = 0; i < 2 && q->action[i]; i++) {
        if (a2_text_in(tf, q->action[i], n->action[i], sizeof n->action[i]) <= 0)
            return -1;
        n->nactions++;
    }
    if (!q->action[0] && q->action[1])
        return -1;
    if (q->icon) {
        if (icon_in(tf, q->icon, n->icon) != 0)
            return -1;
        n->has_icon = 1;
    }
    n->life = q->seconds;
    return 0;
}

static int notify(struct app *a, struct trapframe *tf)
{
    struct note *n;                             /* (its own: the desktop's lock may be waited for) */
    struct glos_notify q;
    u32 size, id, f;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || (q.seconds && (q.seconds < 2 || q.seconds > 60)))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(n = kmalloc(sizeof *n)))
        return a2_fail(tf, GLOS_E_MEMORY);
    if (notify_fill(tf, &q, n) != 0) {
        kfree(n);
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    id = desk_notify_add(a, n);
    kfree(n);
    if (!id)
        return a2_fail(tf, GLOS_E_MEMORY);      /* (too many wait their turn) */
    f = irq_save();
    q.handle = handle_new(&a->v2->h, GLOS_HK_NOTIFY, (void *)id);
    irq_restore(f);
    if (!q.handle || a2_wr(tf, &q, sizeof q, size) != 0) {
        if (q.handle) {
            f = irq_save();
            handle_close(&a->v2->h, q.handle);
            irq_restore(f);
        }
        desk_notify_remove(a, id);
        return a2_fail(tf, q.handle ? GLOS_E_ARGUMENT : GLOS_E_MEMORY);
    }
    desk_kick();                                /* (the desktop's turn shows it) */
    return a2_ok(tf, 0);
}

void api2_note_event(const struct note_news *n)
{
    struct glos_ev_notify e;
    struct app *a;
    u32 f = irq_save();
    memset(&e, 0, sizeof e);
    if ((a = app_by_id(n->owner)) != 0)
        e.ev.handle = handle_of(a, GLOS_HK_NOTIFY, n->id);
    irq_restore(f);
    if (!e.ev.handle)
        return;
    e.ev.size = sizeof e;
    e.ev.kind = GLOS_EV_NOTIFY;
    e.what = (unsigned)n->what;                 /* (NOTE_ are GLOS_NOTIFY_) */
    a2_post(a, &e.ev, 0);
}

void api2_shell_close(struct app *a, int kind, void *obj)
{
    if (kind == GLOS_HK_TRAY)
        desk_tray_remove(a, (u32)obj);
    else
        desk_notify_remove(a, (u32)obj);
    desk_kick();
}

void api2_shell_gone(struct app *a)
{
    desk_tray_remove(a, 0);
    desk_notify_remove(a, 0);
    desk_kick();
}

/* ---- the shell's events */

static void shell_event(struct app *a, unsigned what, int x, int y, u32 window, u32 app)
{
    struct glos_ev_shell e;
    if (!a->v2)
        return;
    memset(&e, 0, sizeof e);
    e.ev.size = sizeof e;
    e.ev.kind = GLOS_EV_SHELL;
    e.what = what;
    e.x = x, e.y = y;
    e.window = window, e.app = app;
    a2_post(a, &e.ev, 0);
}

void api2_shell_1x(struct app *a, u32 type, int x, int y)       /* (1.x's LAUNCH and SYSKEY, from the desktop) */
{
    shell_event(a, type == GLOS_EV_LAUNCH ? GLOS_SHELL_LAUNCH : GLOS_SHELL_TASKS, x, y, 0, 0);
}

void api2_shell_news(struct app *shell, unsigned what)  /* (ENTRIES, RECENTS: one waiting stands for the next) */
{
    struct glos_ev_shell e;
    if (!shell->v2)
        return;
    memset(&e, 0, sizeof e);
    e.ev.size = sizeof e;
    e.ev.kind = GLOS_EV_SHELL;
    e.ev.handle = what;                         /* (so that only one of its kind is merged with) */
    e.what = what;
    a2_post(shell, &e.ev, 1);
}

void api2_shell_changed(const char *section)    /* (api2set.c's: a setting changed) */
{
    struct app *shell = desk_shell_app();
    if (shell && !strncmp(section, "program ", 8))
        api2_shell_news(shell, GLOS_SHELL_ENTRIES);
}

void api2_shell_hung(struct app *shell, u32 window, struct app *owner, int hung)
{
    shell_event(shell, hung ? GLOS_SHELL_HUNG : GLOS_SHELL_ANSWERS, 0, 0, window, owner ? app_id(owner) : 0);
}

/* ---- POINT */

static int point(struct trapframe *tf)
{
    struct glos_point q;
    struct gfx_rect r;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.what < GLOS_POINT_START || q.what > GLOS_POINT_WORK)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    memset(&r, 0, sizeof r);
    if (!desk_point((int)q.what, &r))           /* (GLOS_POINT_ are wm.h's WB_) */
        memset(&r, 0, sizeof r);
    q.x = r.x, q.y = r.y, q.w = r.w, q.h = r.h;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

/* ---- OPEN and HELP */

static int start(struct trapframe *tf, const char *command, u32 *job)
{
    static struct glos_run run;                 /* (no wait between filling it and its post) */
    int r;
    if (!(vm.bi->flags & BI_F_AGENT))           /* (GLOS /RUN: there is nobody to start it) */
        return a2_fail(tf, GLOS_E_REFUSED);
    memset(&run, 0, sizeof run);
    run.size = sizeof run;
    run.flags = GLOS_RUN_DETACH;
    strncpy(run.command, command, sizeof run.command - 1);
    if ((r = agent_post_app(0, &run, job)) != 0)
        return a2_fail(tf, (u32)r);
    return 0;
}

static int help(struct app *a, struct trapframe *tf)
{
    struct glos_help q;
    char page[32], prog[96], cmd[160], dir[80];
    u32 size, job;
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.page) {
        if (a2_text_in(tf, q.page, page, sizeof page) < 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
    } else {
        memcpy(page, a->v2->help, sizeof page);
    }
    home(dir, sizeof dir);
    ksnprintf(prog, sizeof prog, "%s\\HELP.EXE", dir);
    if ((e = api2_fs_exists(a, prog)) != 1)     /* (Help is an app: phase E's) */
        return a2_fail(tf, e < 0 ? GLOS_E_REFUSED : GLOS_E_NOT_FOUND);
    ksnprintf(cmd, sizeof cmd, page[0] ? "%s %s" : "%s", prog, page);
    return start(tf, cmd, &job) ? 0 : a2_ok(tf, 0);
}

static int open_call(struct app *a, struct trapframe *tf)
{
    struct glos_open q;
    char path[200], with[80], label[ASSOC_LABEL], name[ASSOC_LABEL], kind[ASSOC_EXT], prog[224], cmd[256], dir[80];
    char section[96], verbs[ASSOC_VERBS], vname[ASSOC_LABEL], args[128], given[200], opener[16];
    u32 size;
    int have_kind, e, i, n, file;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || a2_text_in(tf, q.path, path, sizeof path) <= 0
        || (q.with && a2_text_in(tf, q.with, with, sizeof with) <= 0)
        || (q.verb && (!q.with || a2_text_in(tf, q.verb, label, sizeof label) <= 0)))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    memcpy(given, path, sizeof given);          /* (as the app wrote it: the recent documents') */
    if ((file = !assoc_link(path)) != 0) {      /* a file: in DOS's bytes, and there */
        if (api2_fs_path(tf, q.path, path, sizeof path) < 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if ((e = api2_fs_exists(a, path)) <= 0)
            return a2_fail(tf, e < 0 ? GLOS_E_REFUSED : GLOS_E_NOT_FOUND);
    }
    have_kind = assoc_kind(path, kind) == 0;
    home(dir, sizeof dir);
    prog[0] = 0;
    if (q.verb) {                               /* a verb of the program's, for this kind */
        if (!have_kind || assoc_verb_name(kind, label, name) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        ksnprintf(section, sizeof section, "program %s", with);
        if (kcfg_get(section, "verbs", verbs, sizeof verbs) <= 0)
            return a2_fail(tf, GLOS_E_NOT_FOUND);
        n = assoc_verb_count(verbs);
        for (i = 0; i < n; i++)
            if (assoc_verb_get(verbs, i, vname, sizeof vname, args, sizeof args) == 0 && same(vname, name))
                break;
        if (i == n)
            return a2_fail(tf, GLOS_E_NOT_FOUND);
        ksnprintf(prog, sizeof prog, args[0] ? "%s %s" : "%s", with, args);
    } else if (q.with) {
        ksnprintf(prog, sizeof prog, "%s", with);
    } else if (have_kind && assoc_program(kind)) {
        ksnprintf(cmd, sizeof cmd, "%s", path);         /* (DOS runs it itself) */
        goto run;
    } else if (!have_kind || kcfg_get("open", kind, prog, sizeof prog) <= 0 || !prog[0]) {
        return a2_fail(tf, GLOS_E_NOT_FOUND);   /* (nothing is set to open its kind) */
    }
    if (assoc_command(prog, path, dir, cmd, sizeof cmd) < 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (start(tf, cmd, &q.job))
        return 0;
    if (file) {                                 /* a document: among the recent ones, with the program that has it */
        const char *b = prog;
        for (i = 0; prog[i] && prog[i] != ' '; i++)
            if (prog[i] == '\\' || prog[i] == '/' || prog[i] == ':')
                b = prog + i + 1;
        for (i = 0; b[i] && b[i] != ' ' && i < (int)sizeof opener - 1; i++)
            opener[i] = b[i] >= 'a' && b[i] <= 'z' ? (char)(b[i] - 32) : b[i];
        opener[i] = 0;
        api2_recent_add(opener, given);
    }
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
run:
    if (start(tf, cmd, &q.job))
        return 0;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

/* ---- ASSOC */

#define ASSOC_OUT 2048u

static int assoc_do(struct app *a, struct trapframe *tf, char *out, char *secs);

static int assoc(struct app *a, struct trapframe *tf)
{
    char *buf = kmalloc(2 * ASSOC_OUT);         /* (its own: the settings' lock may be waited for) */
    int r;
    if (!buf)
        return a2_fail(tf, GLOS_E_MEMORY);
    r = assoc_do(a, tf, buf, buf + ASSOC_OUT);
    kfree(buf);
    return r;
}

static int assoc_do(struct app *a, struct trapframe *tf, char *out, char *secs)
{
    struct glos_assoc q;
    char text[80], kind[ASSOC_EXT], label[ASSOC_LABEL], name[ASSOC_LABEL], args[128], why[64];
    char section[96], verbs[ASSOC_VERBS], set[ASSOC_VERBS], vname[ASSOC_LABEL];
    u32 size, len = 0;
    int e, i, n;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || a2_text_in(tf, q.kind, text, sizeof text) <= 0 || assoc_kind(text, kind) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    switch (q.what) {
    case GLOS_ASSOC_GET:
        out[0] = 0;
        if (kcfg_get("open", kind, out, ASSOC_OUT) < 0)
            out[0] = 0;
        len = (u32)strlen(out);
        break;
    case GLOS_ASSOC_SET:
        if (q.text && a2_text_in(tf, q.text, text, sizeof text) < 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (kcfg_set("open", kind, q.text && text[0] ? text : 0, why, sizeof why) < 0)
            return a2_fail(tf, GLOS_E_SETTING);
        return a2_ok(tf, 0);
    case GLOS_ASSOC_VERBS: {                    /* every program's profile: its verbs for this kind */
        const char *s = secs;
        u32 kl = (u32)strlen(kind);
        secs[kcfg_sections(secs, ASSOC_OUT - 1)] = 0;
        while (*s) {
            const char *nl = strchr(s, '\n');
            u32 sl = nl ? (u32)(nl - s) : (u32)strlen(s);
            if (sl > 8 && sl < sizeof section && !memcmp(s, "program ", 8)) {
                memcpy(section, s, sl);
                section[sl] = 0;
                if (kcfg_get(section, "verbs", verbs, sizeof verbs) > 0) {
                    n = assoc_verb_count(verbs);
                    for (i = 0; i < n; i++) {
                        if (assoc_verb_get(verbs, i, vname, sizeof vname, args, sizeof args) != 0 || strlen(vname) <= kl
                            || vname[kl] != ':')
                            continue;
                        vname[kl] = 0;
                        if (!same(vname, kind))
                            continue;
                        if (len < ASSOC_OUT)
                            len += (u32)ksnprintf(out + len, ASSOC_OUT - len, args[0] ? "%s\t%s %s\n" : "%s\t%s\n",
                                                  vname + kl + 1, section + 8, args);
                    }
                }
            }
            s += sl + (nl ? 1 : 0);
        }
        if (len >= ASSOC_OUT)
            len = ASSOC_OUT - 1;
        break;
    }
    case GLOS_ASSOC_VERB:                       /* the caller's own, in its profile */
        if (a2_text_in(tf, q.text, label, sizeof label) <= 0 || assoc_verb_name(kind, label, name) != 0
            || (q.args && a2_text_in(tf, q.args, args, sizeof args) < 0))
            return a2_fail(tf, GLOS_E_ARGUMENT);
        ksnprintf(section, sizeof section, "program %s", a->prog);
        if (kcfg_get(section, "verbs", verbs, sizeof verbs) <= 0)
            verbs[0] = 0;
        if (assoc_verb_set(verbs, name, q.args ? args : 0, set) != 0)
            return a2_fail(tf, q.args ? GLOS_E_ARGUMENT : GLOS_E_NOT_FOUND);
        if (kcfg_set(section, "verbs", set[0] ? set : 0, why, sizeof why) < 0)
            return a2_fail(tf, GLOS_E_SETTING);
        return a2_ok(tf, 0);
    default:
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    q.length = len;
    e = q.buffer || q.room ? a2_text_out(tf, q.buffer, q.room, out, len) : GLOS_E_ROOM;
    if (e == GLOS_E_ARGUMENT || a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return e ? a2_fail(tf, (u32)e) : a2_ok(tf, 0);
}

int api2_shell(struct app *a, struct trapframe *tf, u32 n)
{
    switch (GLOS_FN(GLOS_AREA_SHELL, n)) {
    case GLOS_SHELL_DESKTOP:
        return desktop(a, tf);
    case GLOS_SHELL_TRAY:
        return tray(a, tf);
    case GLOS_SHELL_NOTIFY:
        return notify(a, tf);
    case GLOS_SHELL_POINT:
        return point(tf);
    case GLOS_SHELL_HELP:
        return help(a, tf);
    case GLOS_SHELL_OPEN:
        return open_call(a, tf);
    case GLOS_SHELL_ASSOC:
        return assoc(a, tf);
    case GLOS_SHELL_ENTRY:
    case GLOS_SHELL_RECENT:
    case GLOS_SHELL_SCHEDULE:
        return api2_start(a, tf, GLOS_FN(GLOS_AREA_SHELL, n));
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
