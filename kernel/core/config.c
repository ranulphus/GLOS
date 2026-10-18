/* GLOS.CFG in the kernel (PRD §10.7, D53; docs/milestones-m9.md step B;
 * include/glos/config.h is the schema).
 *
 * The kernel keeps the file's lines, as they are, and is their keeper from
 * when it has read them: glos config and GLOS_FN_CONFIG read and change
 * them, a change is checked against the schema, made to the lines (the
 * changed line in place, a new key at its section's end, a new section at
 * the file's end: comments, blank lines, order and unknown keys all kept),
 * applied at once where the setting can be, and written back to the file.
 *
 * The file is read and written through the DOS server (dos.c), which makes
 * its calls only while the resident stub is idle: so a thread of its own
 * does it, and waits. Until the file has been read, a change is refused (it
 * would be lost); the settings in effect meanwhile are those the loader
 * read. The copy in memory is the one that counts: the file catches up. */
#include <string.h>

#include "glos/bootinfo.h"
#include "glos/config.h"
#include "kcfg.h"
#include "agent.h"
#include "clock.h"
#include "display.h"
#include "gpu.h"
#include "mixer.h"
#include "nic.h"
#include "vm.h"
#include "desk.h"
#include "dos.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "timer.h"

#define MAX_LINES 512
#define MAX_TEXT  16384u                        /* the file's bytes at most */

static struct {
    char *line[MAX_LINES];                      /* each kmalloc'd, without its line end */
    u32 n;
    char path[80];
    u8 loaded, failed, dirty, writing;
    u32 writes, write_errors, changes;
    struct waitq q, lockq;
    struct thread *owner;
    void (*apply)(const char *section, const char *key, const char *value);
} c;

static void lock(void)
{
    u32 f = irq_save();
    while (c.owner && c.owner != current)
        thread_block(&c.lockq);
    c.owner = current;
    irq_restore(f);
}

static void unlock(void)
{
    u32 f = irq_save();
    c.owner = 0;
    thread_wake(&c.lockq);
    irq_restore(f);
}

static int lower(int ch) { return ch >= 'A' && ch <= 'Z' ? ch + 32 : ch; }

static const char *chr(const char *s, int ch)          /* (the kernel's C library has no strchr) */
{
    for (; *s; s++)
        if (*s == ch)
            return s;
    return 0;
}

static int same(const char *a, const char *b)
{
    for (; *a && lower((u8)*a) == lower((u8)*b); a++, b++)
        ;
    return !*a && !*b;
}

static char *dup(const char *s, u32 n)
{
    char *d = kmalloc(n + 1);
    if (d) {
        memcpy(d, s, n);
        d[n] = 0;
    }
    return d;
}

/* ---- parsing the lines */

/* Line i's section header, into sec ("program FWHEEL.EXE": the word "program" and the name, one space): 1 if
   it is one. */
static int header(const char *l, char *sec, u32 room)
{
    const char *e;
    u32 n = 0;
    while (*l == ' ' || *l == '\t')
        l++;
    if (*l != '[' || !(e = chr(l, ']')))
        return 0;
    for (l++; l < e && (*l == ' ' || *l == '\t'); l++)
        ;
    while (l < e && n < room - 1) {
        if (*l == ' ' || *l == '\t') {          /* (inner spaces: one) */
            while (l < e && (*l == ' ' || *l == '\t'))
                l++;
            if (l < e && n < room - 1)
                sec[n++] = ' ';
            continue;
        }
        sec[n++] = *l++;
    }
    sec[n] = 0;
    return 1;
}

/* Line l as KEY = VALUE: the key's and the value's start and length; 0 if it isn't one. */
static int pair(const char *l, const char **k, u32 *kn, const char **v, u32 *vn)
{
    const char *eq, *e;
    while (*l == ' ' || *l == '\t')
        l++;
    if (*l == ';' || *l == '#' || *l == '[' || !*l || !(eq = chr(l, '=')))
        return 0;
    for (e = eq; e > l && (e[-1] == ' ' || e[-1] == '\t'); e--)
        ;
    *k = l, *kn = (u32)(e - l);
    for (l = eq + 1; *l == ' ' || *l == '\t'; l++)
        ;
    for (e = l + strlen(l); e > l && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r'); e--)
        ;
    *v = l, *vn = (u32)(e - l);
    return *kn > 0;
}

static int same_n(const char *a, u32 an, const char *b)
{
    u32 i;
    for (i = 0; i < an; i++)
        if (!b[i] || lower((u8)a[i]) != lower((u8)b[i]))
            return 0;
    return !b[an];
}

/* The section a line is in (the last header above it), into sec. "" before the first. */
static void section_of(u32 i, char *sec, u32 room)
{
    sec[0] = 0;
    while (i-- > 0)
        if (header(c.line[i], sec, room))
            return;
}

/* The lines of section: the header's index (or -1) and the index after its last line. */
static int find_section(const char *section, u32 *end)
{
    char sec[48];
    u32 i;
    int at = -1;
    for (i = 0; i < c.n; i++) {
        if (header(c.line[i], sec, sizeof sec)) {
            if (at >= 0)
                break;
            if (same(sec, section))
                at = (int)i;
        }
    }
    *end = i;
    return at;
}

/* ---- reading */

int kcfg_loaded(void) { return c.loaded; }

int kcfg_get(const char *section, const char *key, char *out, u32 room)
{
    const struct cfg_key *k = cfg_find(section, key);
    u32 end, i, n = 0;
    int at, found = 0;
    if (!k || room == 0)
        return -1;
    lock();
    at = find_section(section, &end);
    for (i = at < 0 ? end : (u32)at + 1; at >= 0 && i < end; i++) {
        const char *kk, *v;
        u32 kn, vn;
        if (pair(c.line[i], &kk, &kn, &v, &vn) && same_n(kk, kn, key)) {
            if (k->type == CFG_ENV && found) {  /* (env: every line, one to a line) */
                if (n < room - 1)
                    out[n++] = '\n';
            } else {
                n = 0;
            }
            if (vn > room - 1 - n)
                vn = room - 1 - n;
            memcpy(out + n, v, vn);
            n += vn;
            found = 1;
        }
    }
    unlock();
    if (!found) {
        n = strlen(k->def);
        if (n > room - 1)
            n = room - 1;
        memcpy(out, k->def, n);
    }
    out[n] = 0;
    return found;
}

/* The [program NAME.EXT] section for program file name prog (upper case): the same NAME.EXT, or the same
   NAME when either has no extension (session.c's rule), into sec. 0, or -1. */
static int profile_section(const char *prog, char *sec, u32 room)
{
    char s[48];
    u32 i, n, m;
    for (n = 0; prog[n] && prog[n] != '.'; n++) ;
    lock();
    for (i = 0; i < c.n; i++) {
        const char *name;
        if (!header(c.line[i], s, sizeof s) || strncmp(s, "program ", 8))
            continue;
        name = s + 8;
        for (m = 0; name[m] && name[m] != '.'; m++) ;
        if (same(name, prog) || (m == n && !strncmp(name, prog, n) && (!name[m] || !prog[n]))) {
            ksnprintf(sec, room, "%s", s);
            unlock();
            return 0;
        }
    }
    unlock();
    return -1;
}

/* The program's profile from its section (supervisor.md §11.2): 0, or -1 if it has none. */
int kcfg_profile(const char *name, struct bi_profile *p)
{
    char sec[48], v[200];
    u32 n = strlen(name);
    if (!c.loaded || n > 12)
        return -2;                              /* (not read yet: the loader's profiles) */
    if (profile_section(name, sec, sizeof sec) != 0)
        return -1;
    memset(p, 0, sizeof *p);
    memcpy(p->name, sec + 8, strlen(sec + 8) < sizeof p->name ? strlen(sec + 8) + 1 : sizeof p->name - 1);
    kcfg_get(sec, "direct", v, sizeof v);
    if (v[0] == '1')
        p->flags |= BI_PROF_DIRECT;
    kcfg_get(sec, "memory", v, sizeof v);
    {
        const char *q = v;
        while (*q >= '0' && *q <= '9')
            p->memory_kb = p->memory_kb * 10 + (u32)(*q++ - '0');
    }
    if (kcfg_get(sec, "env", v, sizeof v) > 0) {
        u32 i, o = 0;
        for (i = 0; v[i] && o < sizeof p->env - 2; i++)
            p->env[o++] = v[i] == '\n' ? 0 : v[i];
        p->env[o++] = 0;
        p->env[o] = 0;
    }
    return 0;
}

/* ---- changing */

static int insert(u32 at, char *l)
{
    if (c.n >= MAX_LINES || !l)
        return -1;
    memmove(&c.line[at + 1], &c.line[at], (c.n - at) * sizeof c.line[0]);
    c.line[at] = l;
    c.n++;
    return 0;
}

static void drop(u32 at)
{
    kfree(c.line[at]);
    memmove(&c.line[at], &c.line[at + 1], (c.n - at - 1) * sizeof c.line[0]);
    c.n--;
}

static u32 text_size(void)
{
    u32 i, t = 0;
    for (i = 0; i < c.n; i++)
        t += strlen(c.line[i]) + 2;
    return t;
}

/* "KEY = VALUE" for a new line */
static char *make_line(const char *key, const char *value)
{
    u32 kn = strlen(key), vn = strlen(value);
    char *l = kmalloc(kn + vn + 4);
    if (l) {
        memcpy(l, key, kn);
        memcpy(l + kn, " = ", 3);
        memcpy(l + kn + 3, value, vn + 1);
    }
    return l;
}

static void wake_writer(void)
{
    u32 f = irq_save();
    c.dirty = 1;
    thread_wake(&c.q);
    irq_restore(f);
}

/* Set (value) or unset (value NULL) a key. env takes lines separated by ';' or '\n', each one line. The
   setting's `when`, or -1 with why. */
int kcfg_set(const char *section, const char *key, const char *value, char *why, u32 room)
{
    const struct cfg_key *k = cfg_find(section, key);
    const char *given = value;                  /* (value is used up below; this is what was asked for) */
    char one[200], semi = ';';                  /* (env: lines, or with none, parts by ";" as ssh gives them) */
    u32 end, i;
    int at, r;
    if (!k || !cfg_section_ok(section)) {
        ksnprintf(why, room, "no such setting: [%s] %s", section, key);
        return -1;
    }
    if (!c.loaded) {
        ksnprintf(why, room, c.failed ? "GLOS.CFG couldn't be read" : "GLOS.CFG isn't read yet");
        return -1;
    }
    if (value) {                                /* every part checked first */
        const char *p = value;
        for (i = 0; value[i]; i++)
            if (value[i] == '\n')
                semi = '\n';
        do {
            u32 n = 0;
            while (*p && (k->type != CFG_ENV || (*p != semi && *p != '\n')) && n < sizeof one - 1)
                one[n++] = *p++;
            one[n] = 0;
            if (*p)
                p++;
            if (cfg_check(k, one, why, (int)room) != 0)
                return -1;
        } while (k->type == CFG_ENV && *p);
    }
    lock();
    at = find_section(section, &end);
    for (i = at < 0 ? end : (u32)at + 1; at >= 0 && i < end;) {        /* the key's lines go */
        const char *kk, *v;
        u32 kn, vn;
        if (pair(c.line[i], &kk, &kn, &v, &vn) && same_n(kk, kn, key)) {
            if (k->type != CFG_ENV && value) {  /* (one: changed in place) */
                char *l = make_line(key, value);
                if (!l) {
                    unlock();
                    ksnprintf(why, room, "no memory");
                    return -1;
                }
                kfree(c.line[i]);
                c.line[i] = l;
                value = 0;
                i++;
                continue;
            }
            drop(i);
            end--;
            continue;
        }
        i++;
    }
    r = 0;
    if (value) {
        const char *p = value;
        if (at < 0) {                           /* a new section at the end, after a blank line */
            char hdr[48];
            if (c.n && c.line[c.n - 1][0])
                r |= insert(c.n, dup("", 0));
            ksnprintf(hdr, sizeof hdr, "[%s]", section);
            r |= insert(c.n, dup(hdr, strlen(hdr)));
            end = c.n;
        }
        while (end > (u32)(at < 0 ? 0 : at + 1) && !c.line[end - 1][0])
            end--;                              /* (after the section's last line, before blank ones) */
        do {
            u32 n = 0;
            while (*p && (k->type != CFG_ENV || (*p != semi && *p != '\n')) && n < sizeof one - 1)
                one[n++] = *p++;
            one[n] = 0;
            if (*p)
                p++;
            r |= insert(end++, make_line(key, one));
        } while (k->type == CFG_ENV && *p);
    }
    if (text_size() > MAX_TEXT)
        r = -1;
    c.changes++;
    unlock();
    if (r) {
        ksnprintf(why, room, "GLOS.CFG is too long");
        return -1;
    }
    wake_writer();
    if (k->when == CFG_NOW)
        kcfg_apply(section, key, given ? given : k->def);
    if (c.apply)
        c.apply(section, key, given ? given : k->def);
    return k->when;
}

/* ---- what can be applied at once */

static u32 num(const char *s)
{
    u32 v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (u32)(*s++ - '0');
    return v;
}

void kcfg_apply(const char *section, const char *key, const char *v)
{
    if (same(section, "display") && same(key, "mode")) {
        u32 w = num(v), h = 0, b = 0;
        const char *p = chr(v, 'x');
        if (p) {
            h = num(p + 1);
            if ((p = chr(p + 1, 'x')) != 0)
                b = num(p + 1);
        }
        if (same(v, "none"))
            display_ask(DISPLAY_OFF, 0, 0, 0);
        else
            display_ask(DISPLAY_ON, w, h, b);
    } else if (same(section, "display") && same(key, "gpu_watchdog")) {
        gpu_watchdog(same(v, "off") ? 0 : num(v));
    } else if (same(section, "sound") && same(key, "mixer")) {
        mixer_enable(same(v, "on"));
    } else if (same(section, "sound") && same(key, "rate")) {
        mixer_set_rate(num(v));
    } else if (same(section, "input") && (same(key, "repeat_delay") || same(key, "repeat_rate"))) {
        char d[16], r[16];
        kcfg_get("input", "repeat_delay", d, sizeof d);
        kcfg_get("input", "repeat_rate", r, sizeof r);
        vkbc_typematic(num(d), num(r));
    } else if (same(section, "input") && same(key, "pointer_speed")) {
        deskin_pointer_speed((int)num(v));
    } else if (same(section, "time") && same(key, "zone")) {
        clock_zone(v);
    } else if (same(section, "time")) {         /* (sntp, server, interval: asked again, now) */
        net_sntp_restart();
    }
}

/* Once the file has been read: what it says that the loader didn't apply (the M9 settings). */
static void apply_read(void)
{
    char v[32];
    char z[48];
    kcfg_get("input", "pointer_speed", v, sizeof v);
    kcfg_apply("input", "pointer_speed", v);
    kcfg_apply("input", "repeat_rate", "");
    kcfg_get("time", "zone", z, sizeof z);      /* (M10: SNTP starts by itself once the file is read) */
    clock_zone(z);
}

/* The desktop has come on (display.c, the VM thread): [desktop] start's programs, beside GLOS.EXE when they have
   no folder of their own, each a job of nobody's. */
void kcfg_desktop_started(void)
{
    char list[128], cmd[200], dir[80];
    u32 i, n;
    const char *p, *slash = 0;
    if (kcfg_get("desktop", "start", list, sizeof list) < 0 || !list[0])
        return;
    for (p = c.path; *p; p++)
        if (*p == '\\')
            slash = p;
    n = slash ? (u32)(slash - c.path) : 0;
    memcpy(dir, c.path, n);
    dir[n] = 0;
    for (p = list; *p; ) {
        u32 k = 0;
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        for (i = 0; p[i] && p[i] != ' ' && i < 60; i++)
            ;
        if (chr(p, ':') && chr(p, ':') < p + i)
            k = (u32)ksnprintf(cmd, sizeof cmd, "%s", "");
        else
            k = (u32)ksnprintf(cmd, sizeof cmd, "%s\\", dir);
        memcpy(cmd + k, p, i);
        cmd[k + i] = 0;
        agent_post_detached(cmd, dir);
        kprintf("GLOS-CFG desktop-start cmd=%s\n", cmd);
        p += i;
    }
}

/* ---- listing */

/* Every setting of the schema that applies, with its value, then the file's own sections' (programs, [open]):
   "[section] key = value" lines; "(default)" after one the file doesn't set. */
u32 kcfg_list(char *buf, u32 max)
{
    u32 n = 0, i;
    int s;
    char v[200], sec[48];
    for (s = 0; s < cfg_schema_n && n < max; s++) {
        const struct cfg_key *k = &cfg_schema[s];
        int set;
        if (!strcmp(k->section, "program") || !strcmp(k->section, "open"))
            continue;
        set = kcfg_get(k->section, k->key, v, sizeof v);
        n += (u32)ksnprintf(buf + n, max - n, "[%s] %s = %s%s\n", k->section, k->key, v, set > 0 ? "" : " (default)");
    }
    lock();
    for (i = 0; i < c.n && n < max; i++) {      /* programs' and [open]'s keys as the file has them */
        const char *kk, *vv;
        u32 kn, vn;
        if (!pair(c.line[i], &kk, &kn, &vv, &vn))
            continue;
        section_of(i, sec, sizeof sec);
        if (strncmp(sec, "program ", 8) && !same(sec, "open"))
            continue;
        {
            char kb[32], vb[200];               /* (no %.*s in ksnprintf) */
            if (kn > sizeof kb - 1)
                kn = sizeof kb - 1;
            if (vn > sizeof vb - 1)
                vn = sizeof vb - 1;
            memcpy(kb, kk, kn);
            kb[kn] = 0;
            memcpy(vb, vv, vn);
            vb[vn] = 0;
            n += (u32)ksnprintf(buf + n, max - n, "[%s] %s = %s\n", sec, kb, vb);
        }
    }
    unlock();
    return n < max ? n : max;
}

/* The sections the file has: one a line. */
u32 kcfg_sections(char *buf, u32 max)
{
    u32 n = 0, i;
    char sec[48];
    lock();
    for (i = 0; i < c.n && n < max; i++)
        if (header(c.line[i], sec, sizeof sec))
            n += (u32)ksnprintf(buf + n, max - n, "%s\n", sec);
    unlock();
    return n < max ? n : max;
}

/* One section's KEY = VALUE lines as the file has them ([open]'s kinds of file): "key = value" a line. */
u32 kcfg_keys(const char *section, char *buf, u32 max)
{
    u32 n = 0, i, end;
    int at;
    lock();
    at = find_section(section, &end);
    for (i = at < 0 ? end : (u32)at + 1; at >= 0 && i < end && n < max; i++) {
        const char *kk, *vv;
        u32 kn, vn;
        char kb[32], vb[200];                   /* (no %.*s in ksnprintf) */
        if (!pair(c.line[i], &kk, &kn, &vv, &vn))
            continue;
        if (kn > sizeof kb - 1)
            kn = sizeof kb - 1;
        if (vn > sizeof vb - 1)
            vn = sizeof vb - 1;
        memcpy(kb, kk, kn);
        kb[kn] = 0;
        memcpy(vb, vv, vn);
        vb[vn] = 0;
        n += (u32)ksnprintf(buf + n, max - n, "%s = %s\n", kb, vb);
    }
    unlock();
    return n < max ? n : max;
}

u32 kcfg_stat(char *buf, u32 max)
{
    return (u32)ksnprintf(buf, max, "config %s path=%s lines=%u changes=%u writes=%u errors=%u%s\n",
                          c.loaded ? "loaded" : c.failed ? "unread" : "loading", c.path, c.n, c.changes, c.writes,
                          c.write_errors, c.dirty || c.writing ? " pending" : "");
}

void kcfg_on_change(void (*apply)(const char *section, const char *key, const char *value)) { c.apply = apply; }

/* ---- the file, through the DOS server, on the writer's own thread */

static int dos_wait(struct dos_req *q)
{
    if (dos_post(q) != 0)
        return -1;
    while (q->state != DR_DONE)
        thread_wait(&c.q, timer_ticks() + 64);
    return q->error ? -1 : 0;
}

static void load(void)
{
    struct dos_req q;
    u32 room = dos_xfer_size(), plen = strlen(c.path) + 1, got, i, start;
    char *buf = kmalloc(MAX_TEXT + 1);
    u16 h;
    if (!buf)
        return;
    memset(&q, 0, sizeof q);
    q.n = 1;
    q.r[0].ax = 0x3D00;                         /* open, to read */
    q.r[0].ds = DOS_XFER;
    q.in = (const u8 *)c.path, q.in_len = plen, q.in_at = 0;
    if (dos_wait(&q) != 0) {
        if (q.error == 2) {                     /* no file: an empty one, written when something is set */
            c.loaded = 1;
            kprintf("GLOS-CFG none path=%s\n", c.path);
        } else {
            c.failed = 1;
            kprintf("GLOS-CFG error=open dos=%u path=%s\n", q.error, c.path);
        }
        kfree(buf);
        return;
    }
    h = q.r[0].ax;
    memset(&q, 0, sizeof q);
    q.n = 2;
    q.r[0].ax = 0x3F00, q.r[0].bx = h, q.r[0].cx = (u16)(room < MAX_TEXT ? room : MAX_TEXT), q.r[0].ds = DOS_XFER;
    q.r[1].ax = 0x3E00, q.r[1].bx = h;
    q.out = (u8 *)buf, q.out_len = room < MAX_TEXT ? room : MAX_TEXT, q.out_at = 0;
    if (dos_wait(&q) != 0 && q.ran < 1) {
        c.failed = 1;
        kprintf("GLOS-CFG error=read dos=%u\n", q.error);
        kfree(buf);
        return;
    }
    got = q.r[0].ax;
    lock();
    for (i = start = 0; i <= got; i++)
        if (i == got || buf[i] == '\n' || buf[i] == 0x1A) {
            u32 e = i;
            if (e > start && buf[e - 1] == '\r')
                e--;
            if (i < got || e > start)
                insert(c.n, dup(buf + start, e - start));
            if (i < got && buf[i] == 0x1A)
                break;
            start = i + 1;
        }
    c.loaded = 1;
    unlock();
    kprintf("GLOS-CFG loaded lines=%u bytes=%u\n", c.n, got);
    kfree(buf);
    apply_read();
}

static int save(void)
{
    struct dos_req q;
    u32 i, n = 0, plen = strlen(c.path) + 1;
    char *buf = kmalloc(MAX_TEXT + 2);
    u16 h;
    int r = -1;
    if (!buf)
        return -1;
    lock();
    for (i = 0; i < c.n; i++) {
        u32 l = strlen(c.line[i]);
        if (n + l + 2 > MAX_TEXT)
            break;
        memcpy(buf + n, c.line[i], l);
        n += l;
        buf[n++] = '\r', buf[n++] = '\n';
    }
    c.dirty = 0;
    unlock();
    memset(&q, 0, sizeof q);
    q.n = 1;
    q.r[0].ax = 0x3C00, q.r[0].cx = 0, q.r[0].ds = DOS_XFER;    /* create (or empty) */
    q.in = (const u8 *)c.path, q.in_len = plen, q.in_at = 0;
    if (dos_wait(&q) == 0) {
        h = q.r[0].ax;
        memset(&q, 0, sizeof q);
        q.n = 2;
        q.r[0].ax = 0x4000, q.r[0].bx = h, q.r[0].cx = (u16)n, q.r[0].ds = DOS_XFER;
        q.r[1].ax = 0x3E00, q.r[1].bx = h;
        q.in = (const u8 *)buf, q.in_len = n, q.in_at = 0;
        r = dos_wait(&q) == 0 && q.r[0].ax == n ? 0 : -1;
    }
    kfree(buf);
    return r;
}

static void writer(void *arg)
{
    (void)arg;
    dos_hold();                                 /* (the transfer buffer stays while GLOS runs: a small price) */
    load();
    for (;;) {
        u32 f = irq_save();
        while (!c.dirty)
            thread_block(&c.q);
        c.writing = 1;
        irq_restore(f);
        if (save() == 0) {
            c.writes++;
            kprintf("GLOS-CFG written lines=%u\n", c.n);
        } else {
            c.write_errors++;
            kprintf("GLOS-CFG error=write\n");
        }
        c.writing = 0;
    }
}

void kcfg_init(const struct bootinfo *bi)
{
    memcpy(c.path, bi->cfg_path, sizeof c.path);
    c.path[sizeof c.path - 1] = 0;
    if (!c.path[0] || !(bi->flags & BI_F_AGENT)) {     /* (the DOS server is the agent's) */
        c.failed = 1;
        return;
    }
    thread_create("config", PRIO_BULK, writer, 0);
}
