/* API 2.0's SHELL, the Start menu's data and the schedule (include/glos/api2.h; docs/v1/phase-b.md step 8):
 * ENTRY, RECENT, SCHEDULE.
 *
 * An entry is a program's profile in GLOS.CFG ([program NAME.EXE]); ENTRY is that profile's keys for the calling
 * program, and every profile that asks to be in the menu for the shell. The recent documents and the schedule
 * are records in GLOS.DAT (kernel/core/kdata.c): "recent PROGRAM<tab>PATH", the newest first; "sched NAME<tab>
 * WHEN<tab>SINCE<tab>RUNS<tab>COMMAND" (SINCE: when it was set or last ran, UTC). kernel/core/schedule.c runs the
 * scheduled commands. */
#include <string.h>

#include "api2int.h"
#include "clock.h"
#include "desk.h"
#include "glos/assoc.h"
#include "glos/bootinfo.h"
#include "glos/sched.h"
#include "kcfg.h"
#include "kdata.h"
#include "kprintf.h"
#include "mm.h"
#include "vm.h"

#define OUT_ROOM 4096u

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
        a++, b++;
    return !*a && !*b;
}

static int has_tab(const char *s)
{
    for (; *s; s++)
        if (*s == '\t' || (unsigned char)*s < 0x20)
            return 1;
    return 0;
}

/* A list for the app: its length in the structure, the text as far as there is room. */
static int list_out(struct trapframe *tf, u32 buffer, u32 room, u32 *length, const char *out, u32 len)
{
    *length = len;
    return buffer || room ? a2_text_out(tf, buffer, room, out, len) : GLOS_E_ROOM;
}

/* ---- ENTRY */

static u32 entries(char *out, char *secs)
{
    const char *s = secs;
    u32 len = 0;
    secs[kcfg_sections(secs, OUT_ROOM - 1)] = 0;
    while (*s) {
        const char *nl = strchr(s, '\n');
        u32 sl = nl ? (u32)(nl - s) : (u32)strlen(s);
        char section[64], v[8], title[48], group[32], icon[16], path[80], args[128], tasks[192];
        if (sl > 8 && sl < sizeof section && !memcmp(s, "program ", 8)) {
            memcpy(section, s, sl);
            section[sl] = 0;
            if (kcfg_get(section, "menu", v, sizeof v) > 0 && v[0] == '1') {
                if (kcfg_get(section, "title", title, sizeof title) <= 0 || !title[0])
                    ksnprintf(title, sizeof title, "%s", section + 8);
                if (kcfg_get(section, "group", group, sizeof group) < 0)
                    group[0] = 0;
                if (kcfg_get(section, "icon", icon, sizeof icon) < 0)
                    ksnprintf(icon, sizeof icon, "program");
                if (kcfg_get(section, "path", path, sizeof path) < 0)
                    path[0] = 0;
                if (kcfg_get(section, "args", args, sizeof args) < 0)
                    args[0] = 0;
                if (kcfg_get(section, "tasks", tasks, sizeof tasks) < 0)
                    tasks[0] = 0;
                if (len < OUT_ROOM)
                    len += (u32)ksnprintf(out + len, OUT_ROOM - len, "%s\t%s\t%s\t%s\t%s\t%s\t%s\n", section + 8, title, group, icon,
                                          path, args, tasks);
            }
        }
        s += sl + (nl ? 1 : 0);
    }
    return len < OUT_ROOM ? len : OUT_ROOM - 1;
}

static int entry(struct app *a, struct trapframe *tf)
{
    struct glos_entry q;
    char section[32], text[128], label[ASSOC_LABEL], tasks[ASSOC_VERBS], set[ASSOC_VERBS], why[64];
    u32 size;
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    ksnprintf(section, sizeof section, "program %s", a->prog);
    switch (q.what) {
    case GLOS_ENTRY_SET: {
        static const char *const key[4] = { "title", "group", "icon", "args" };
        static const u8 room[4] = { 41, 32, 16, 121 };
        u32 off[4], i;
        off[0] = q.title, off[1] = q.group, off[2] = q.icon, off[3] = q.args;
        for (i = 0; i < 4; i++) {               /* (each checked by the schema before any is set) */
            if (off[i] && (a2_text_in(tf, off[i], text, room[i]) < 0 || has_tab(text)))
                return a2_fail(tf, GLOS_E_ARGUMENT);
        }
        for (i = 0; i < 4; i++) {
            if (off[i]) {
                a2_text_in(tf, off[i], text, room[i]);
            } else if (i == 1 && kcfg_get(section, "group", text, sizeof text) <= 0) {
                ksnprintf(text, sizeof text, "%s", a->v2->category);     /* (INIT's, unless it has one already) */
            } else {
                continue;
            }
            if (kcfg_set(section, key[i], text[0] ? text : 0, why, sizeof why) < 0)
                return a2_fail(tf, GLOS_E_SETTING);
        }
        if (kcfg_set(section, "path", a->folder[0] ? a->folder : 0, why, sizeof why) < 0
            || kcfg_set(section, "menu", "1", why, sizeof why) < 0)
            return a2_fail(tf, GLOS_E_SETTING);
        return a2_ok(tf, 0);
    }
    case GLOS_ENTRY_REMOVE:
        if (kcfg_set(section, "menu", 0, why, sizeof why) < 0)
            return a2_fail(tf, GLOS_E_SETTING);
        return a2_ok(tf, 0);
    case GLOS_ENTRY_TASK:
        if (a2_text_in(tf, q.label, label, sizeof label) <= 0 || has_tab(label)
            || (q.args && (a2_text_in(tf, q.args, text, sizeof text) < 0 || has_tab(text))))
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (kcfg_get(section, "tasks", tasks, sizeof tasks) <= 0)
            tasks[0] = 0;
        if (assoc_verb_set(tasks, label, q.args ? text : 0, set) != 0)
            return a2_fail(tf, q.args ? GLOS_E_ARGUMENT : GLOS_E_NOT_FOUND);
        if (kcfg_set(section, "tasks", set[0] ? set : 0, why, sizeof why) < 0)
            return a2_fail(tf, GLOS_E_SETTING);
        return a2_ok(tf, 0);
    case GLOS_ENTRY_LIST: {
        char *buf = kmalloc(2 * OUT_ROOM);      /* (its own: the settings' lock may be waited for) */
        if (!buf)
            return a2_fail(tf, GLOS_E_MEMORY);
        e = list_out(tf, q.buffer, q.room, &q.length, buf, entries(buf, buf + OUT_ROOM));
        kfree(buf);
        if (e == GLOS_E_ARGUMENT || a2_wr(tf, &q, sizeof q, size) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        return e ? a2_fail(tf, (u32)e) : a2_ok(tf, 0);
    }
    }
    return a2_fail(tf, GLOS_E_ARGUMENT);
}

/* ---- RECENT */

static void recents_changed(void)
{
    struct app *shell = desk_shell_app();
    if (shell)
        api2_shell_news(shell, GLOS_SHELL_RECENTS);
}

/* A record's two parts: the program in prog, the path's start returned. */
static const char *recent_parts(const char *rec, char *prog, u32 room)
{
    u32 i;
    for (i = 0; rec[i] && rec[i] != '\t' && i < room - 1; i++)
        prog[i] = rec[i];
    prog[i] = 0;
    while (rec[i] && rec[i] != '\t')
        i++;
    return rec[i] ? rec + i + 1 : rec + i;
}

void api2_recent_add(const char *prog, const char *path)
{
    char rec[KDATA_TEXT + 1], old[KDATA_TEXT + 1], p[16];
    u32 n = kdata_count("recent"), i;
    if (has_tab(path) || has_tab(prog) || strlen(prog) + strlen(path) + 1 > KDATA_TEXT)
        return;
    for (i = 0; i < n; i++)                     /* (it is there already: only its place changes) */
        if (kdata_get("recent", i, old, sizeof old) >= 0 && same(recent_parts(old, p, sizeof p), path)) {
            kdata_remove("recent", i);
            n--;
            break;
        }
    while (n >= GLOS_RECENT_MAX)
        kdata_remove("recent", --n);            /* (the oldest goes) */
    ksnprintf(rec, sizeof rec, "%s\t%s", prog, path);
    if (kdata_add("recent", rec, 1) == 0)
        recents_changed();
}

static int recent(struct app *a, struct trapframe *tf)
{
    struct glos_recent q;
    char path[200], prog[16], p[16], rec[KDATA_TEXT + 1];
    u32 size, n, i, len = 0;
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    switch (q.what) {
    case GLOS_RECENT_ADD:
        if (a2_text_in(tf, q.path, path, sizeof path) <= 0 || has_tab(path))
            return a2_fail(tf, GLOS_E_ARGUMENT);
        api2_recent_add(a->prog, path);
        return a2_ok(tf, 0);
    case GLOS_RECENT_CLEAR:
        if (q.path && a2_text_in(tf, q.path, path, sizeof path) <= 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        for (i = kdata_count("recent"), n = 0; i-- > 0; )
            if (!q.path || (kdata_get("recent", i, rec, sizeof rec) >= 0 && same(recent_parts(rec, p, sizeof p), path))) {
                kdata_remove("recent", i);
                n++;
            }
        if (n)
            recents_changed();
        return q.path && !n ? a2_fail(tf, GLOS_E_NOT_FOUND) : a2_ok(tf, 0);
    case GLOS_RECENT_LIST: {
        char *out;
        prog[0] = 0;
        if (q.program && a2_text_in(tf, q.program, prog, sizeof prog) <= 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (!(out = kmalloc(GLOS_RECENT_MAX * (KDATA_TEXT + 2))))
            return a2_fail(tf, GLOS_E_MEMORY);
        n = kdata_count("recent");
        for (i = 0; i < n; i++) {
            const char *where;
            if (kdata_get("recent", i, rec, sizeof rec) < 0)
                continue;
            where = recent_parts(rec, p, sizeof p);
            if (!prog[0] || same(p, prog))
                len += (u32)ksnprintf(out + len, KDATA_TEXT + 2, "%s\t%s\n", where, p);
        }
        e = list_out(tf, q.buffer, q.room, &q.length, out, len);
        kfree(out);
        if (e == GLOS_E_ARGUMENT || a2_wr(tf, &q, sizeof q, size) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        return e ? a2_fail(tf, (u32)e) : a2_ok(tf, 0);
    }
    }
    return a2_fail(tf, GLOS_E_ARGUMENT);
}

/* ---- SCHEDULE */

/* The index of the record with this name, or -1; its text into rec. */
static int sched_find(const char *name, char *rec, u32 room)
{
    u32 n = kdata_count("sched"), i, k = (u32)strlen(name);
    for (i = 0; i < n; i++)
        if (kdata_get("sched", i, rec, room) >= 0 && strlen(rec) > k && rec[k] == '\t') {
            rec[k] = 0;
            if (same(rec, name)) {
                rec[k] = '\t';
                return (int)i;
            }
        }
    return -1;
}

static int sched_call(struct trapframe *tf)
{
    struct glos_schedule q;
    struct sched_when w;
    char name[SCHED_NAME], when[SCHED_WHEN + 8], cmd[161], rec[KDATA_TEXT + 1], canon[SCHED_WHEN];
    u32 size, n, i, len = 0;
    int at, e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.what != GLOS_SCHEDULE_LIST && (a2_text_in(tf, q.name, name, sizeof name) <= 0 || !sched_name_ok(name)))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    switch (q.what) {
    case GLOS_SCHEDULE_SET:
        if (a2_text_in(tf, q.when, when, sizeof when) <= 0 || sched_parse(when, &w) != 0 || sched_format(&w, canon, sizeof canon) < 0
            || a2_text_in(tf, q.command, cmd, sizeof cmd) <= 0 || has_tab(cmd))
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (!(vm.bi->flags & BI_F_AGENT))       /* (GLOS /RUN: no file to keep it in, nobody to run it) */
            return a2_fail(tf, GLOS_E_REFUSED);
        at = sched_find(name, rec, sizeof rec);
        if (at < 0 && kdata_count("sched") >= GLOS_SCHEDULE_MAX)
            return a2_fail(tf, GLOS_E_MEMORY);
        ksnprintf(rec, sizeof rec, "%s\t%s\t%u\t0\t%s", name, canon, clock_utc(), cmd);  /* (its times count from now) */
        if ((at < 0 ? kdata_add("sched", rec, 0) : kdata_set("sched", (u32)at, rec)) != 0)
            return a2_fail(tf, GLOS_E_MEMORY);
        return a2_ok(tf, 0);
    case GLOS_SCHEDULE_REMOVE:
        if ((at = sched_find(name, rec, sizeof rec)) < 0)
            return a2_fail(tf, GLOS_E_NOT_FOUND);
        kdata_remove("sched", (u32)at);
        return a2_ok(tf, 0);
    case GLOS_SCHEDULE_LIST: {
        char *out = kmalloc(GLOS_SCHEDULE_MAX * (KDATA_TEXT + 16));
        if (!out)
            return a2_fail(tf, GLOS_E_MEMORY);
        n = kdata_count("sched");
        for (i = 0; i < n; i++) {
            char *f[5];
            u32 k, runs = 0;
            if (kdata_get("sched", i, rec, sizeof rec) < 0)
                continue;
            f[0] = rec;
            for (k = 1; k < 5; k++) {           /* name, when, since, runs, command */
                char *t = strchr(f[k - 1], '\t');
                if (!t)
                    break;
                *t = 0;
                f[k] = t + 1;
            }
            if (k < 5)
                continue;
            for (k = 0; f[3][k] >= '0' && f[3][k] <= '9'; k++)
                runs = runs * 10 + (u32)(f[3][k] - '0');
            len += (u32)ksnprintf(out + len, KDATA_TEXT + 16, "%s\t%s\t%s\t%u\t%s\n", f[0], f[1], f[4], runs, runs ? f[2] : "0");
        }
        e = list_out(tf, q.buffer, q.room, &q.length, out, len);
        kfree(out);
        if (e == GLOS_E_ARGUMENT || a2_wr(tf, &q, sizeof q, size) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        return e ? a2_fail(tf, (u32)e) : a2_ok(tf, 0);
    }
    }
    return a2_fail(tf, GLOS_E_ARGUMENT);
}

int api2_start(struct app *a, struct trapframe *tf, u32 fn)
{
    return fn == GLOS_SHELL_ENTRY ? entry(a, tf) : fn == GLOS_SHELL_RECENT ? recent(a, tf) : sched_call(tf);
}
