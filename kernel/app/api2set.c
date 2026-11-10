/* API 2.0's SETTINGS and IDLE (include/glos/api2.h; docs/v1/phase-b.md step 5).
 *
 * SETTINGS is the kernel's settings store (kernel/core/config.c) and its schema (sdk/config/cfg.c) for apps:
 * GET, SET, the file's sections and a section's keys, the schema an entry at a time, a watch on sections (an
 * event when a setting of one changes, whoever changed it), the theme in force (kernel/wm/theme.c) and the
 * region's formats (include/glos/region.h). IDLE is kernel/wm/idle.c's: the idle time, AWAKE handles, the
 * saver, the screen's power. */
#include <string.h>

#include "api2int.h"
#include "clock.h"
#include "desk.h"
#include "display.h"
#include "dpmi.h"
#include "glos/config.h"
#include "glos/region.h"
#include "idle.h"
#include "io.h"
#include "kcfg.h"
#include "theme.h"

/* ---- texts in and out */

/* A text of the app's at DS:off into buf (room bytes, its NUL kept): its length, or -1 (none, unreadable, or
   too long). */
int a2_text_in(struct trapframe *tf, u32 off, char *buf, u32 room)
{
    u32 i;
    if (!off)
        return -1;
    for (i = 0; i < room; i++) {
        if (user_rd((u16)tf->ds, off + i, &buf[i], 1) != 0)
            return -1;
        if (!buf[i])
            return (int)i;
    }
    return -1;
}

/* len bytes of text to DS:off with a NUL, as many as there is room for: 0, GLOS_E_ROOM if cut, GLOS_E_ARGUMENT
   if it can't be written. */
int a2_text_out(struct trapframe *tf, u32 off, u32 room, const char *text, u32 len)
{
    u32 n = len < room ? len : room ? room - 1 : 0;
    char nul = 0;
    if (room && (user_wr((u16)tf->ds, off, text, n) != 0 || user_wr((u16)tf->ds, off + n, &nul, 1) != 0))
        return GLOS_E_ARGUMENT;
    return len < room ? 0 : GLOS_E_ROOM;
}

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same(const char *a, const char *b)
{
    while (*a && lower((unsigned char)*a) == lower((unsigned char)*b))
        a++, b++;
    return !*a && !*b;
}

/* ---- settings */

static int get(struct trapframe *tf)
{
    struct glos_setting q;
    char sec[64], key[64], out[1024];
    u32 size;
    int r, e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || a2_text_in(tf, q.section, sec, sizeof sec) < 0
        || a2_text_in(tf, q.key, key, sizeof key) < 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    out[0] = 0;
    if ((r = kcfg_get(sec, key, out, sizeof out)) < 0)
        return a2_fail(tf, GLOS_E_SETTING);
    q.length = (u32)strlen(out);
    q.set = (u32)r;
    e = a2_text_out(tf, q.value, q.room, out, q.length);
    if (e == GLOS_E_ARGUMENT || a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return e ? a2_fail(tf, (u32)e) : a2_ok(tf, 0);
}

static int set(struct trapframe *tf)
{
    struct glos_setting q;
    char sec[64], key[64], value[800], why[160];
    u32 size;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || a2_text_in(tf, q.section, sec, sizeof sec) < 0
        || a2_text_in(tf, q.key, key, sizeof key) < 0 || (q.value && a2_text_in(tf, q.value, value, sizeof value) < 0))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    why[0] = 0;
    if ((r = kcfg_set(sec, key, q.value ? value : 0, why, sizeof why)) < 0) {
        if (q.why)
            a2_text_out(tf, q.why, q.why_room, why, (u32)strlen(why));
        return a2_fail(tf, GLOS_E_SETTING);
    }
    q.when = (u32)r;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int list(struct trapframe *tf, int keys)
{
    struct glos_setting_list q;
    char sec[64], out[1024];
    u32 size;
    int e;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || (keys && a2_text_in(tf, q.section, sec, sizeof sec) < 0))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    out[0] = 0;
    q.length = keys ? kcfg_keys(sec, out, sizeof out) : kcfg_sections(out, sizeof out);
    if (q.length >= sizeof out)
        q.length = sizeof out - 1;
    e = a2_text_out(tf, q.buffer, q.room, out, q.length);
    if (e == GLOS_E_ARGUMENT || a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return e ? a2_fail(tf, (u32)e) : a2_ok(tf, 0);
}

static u32 add(char *out, u32 n, u32 max, const char *s)       /* s and its NUL */
{
    u32 len = (u32)strlen(s ? s : "") + 1;
    if (n + len <= max)
        memcpy(out + n, s ? s : "", len);
    return n + len;
}

static int schema(struct trapframe *tf)
{
    struct glos_schema q;
    const struct cfg_key *k;
    char out[1024];
    u32 size, n = 0, room;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    q.count = (u32)cfg_schema_n;
    if (q.index >= q.count) {
        a2_wr(tf, &q, sizeof q, size);
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    k = &cfg_schema[q.index];
    q.kind = k->type;
    q.when = k->when;
    q.min = (int)k->min;
    q.max = (int)k->max;
    n = add(out, n, sizeof out, k->section);
    n = add(out, n, sizeof out, k->key);
    n = add(out, n, sizeof out, k->choices);
    n = add(out, n, sizeof out, k->def);
    n = add(out, n, sizeof out, k->label);
    n = add(out, n, sizeof out, k->desc);
    if (n > sizeof out)
        n = sizeof out;
    q.length = n;
    room = q.room < n ? q.room : n;
    if ((room && user_wr((u16)tf->ds, q.buffer, out, room) != 0) || a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return q.room < n ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

static int watch(struct app *a, struct trapframe *tf)
{
    struct api2 *s = a->v2;
    struct glos_setting_watch q;
    char sec[32];
    u32 size, i, f;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    sec[0] = 0;
    if (q.section && a2_text_in(tf, q.section, sec, sizeof sec) < 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    f = irq_save();                             /* (a change's news reads these from another thread) */
    if (!sec[0]) {
        s->watch_all = q.on != 0;
        if (!q.on)
            s->nwatch = 0;
    } else {
        for (i = 0; i < s->nwatch && !same(s->watch[i], sec); i++) ;
        if (q.on && i == s->nwatch) {
            if (s->nwatch == GLOS_WATCH_MAX) {
                irq_restore(f);
                return a2_fail(tf, GLOS_E_MEMORY);
            }
            memcpy(s->watch[s->nwatch++], sec, sizeof sec);
        } else if (!q.on && i < s->nwatch) {
            memcpy(s->watch[i], s->watch[--s->nwatch], sizeof sec);
        }
    }
    irq_restore(f);
    return a2_ok(tf, 0);
}

static int theme(struct trapframe *tf)
{
    const struct theme *t = desk_theme();
    struct glos_theme q;
    u32 size, i;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    memset(q.name, 0, sizeof q.name);
    memcpy(q.name, t->name, strlen(t->name));
    q.flags = t->flags;
    q.roles = GLOS_ROLES;
    for (i = 0; i < GLOS_ROLES; i++)
        q.colour[i] = t->colour[i];
    q.border = t->border;
    q.title = t->title;
    q.bar = t->bar;
    q.scroll = t->scroll;
    q.icon = t->icon;
    q.text = t->text;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int region_call(struct trapframe *tf)
{
    struct glos_region q;
    struct region r;
    u32 size, f;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    f = irq_save();
    r = *kcfg_region();
    irq_restore(f);
    q.date = r.date;
    q.date_sep = (unsigned char)r.date_sep;
    q.clock12 = r.clock12;
    q.decimal = (unsigned char)r.decimal;
    q.thousands = (unsigned char)r.thousands;
    q.first_day = r.first_day;
    q.codepage = r.codepage;
    memcpy(q.layout, r.layout, sizeof q.layout);
    memcpy(q.language, r.language, sizeof q.language);
    q.offset = (int)clock_offset();
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

int api2_settings(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_SETTINGS_GET & 0xFF: return get(tf);
    case GLOS_SETTINGS_SET & 0xFF: return set(tf);
    case GLOS_SETTINGS_SECTIONS & 0xFF: return list(tf, 0);
    case GLOS_SETTINGS_KEYS & 0xFF: return list(tf, 1);
    case GLOS_SETTINGS_SCHEMA & 0xFF: return schema(tf);
    case GLOS_SETTINGS_WATCH & 0xFF: return watch(a, tf);
    case GLOS_SETTINGS_THEME & 0xFF: return theme(tf);
    case GLOS_SETTINGS_REGION & 0xFF: return region_call(tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}

/* ---- a setting changed (the store's hook: whoever changed it, on its thread) */

struct news {
    const char *section, *key;
    int theme;
};

static void tell(struct app *a, void *arg)
{
    const struct news *n = arg;
    struct api2 *s = a->v2;
    u32 i, len;
    int hit;
    if (!s)
        return;
    if (n->theme) {
        struct glos_ev ev;
        memset(&ev, 0, sizeof ev);
        ev.size = sizeof ev;
        ev.kind = GLOS_EV_THEME;
        a2_post(a, &ev, 0);
    }
    hit = s->watch_all;
    for (i = 0; i < s->nwatch && !hit; i++) {   /* ("program" watches every program's profile) */
        len = (u32)strlen(s->watch[i]);
        hit = same(s->watch[i], n->section)
              || (same(s->watch[i], "program") && !strncmp(n->section, "program ", 8) && len == 7);
    }
    if (hit) {
        struct glos_ev_setting ev;
        memset(&ev, 0, sizeof ev);
        ev.ev.size = sizeof ev;
        ev.ev.kind = GLOS_EV_SETTING;
        for (i = 0; n->section[i] && i < sizeof ev.section - 1; i++)
            ev.section[i] = (char)lower((unsigned char)n->section[i]);
        for (i = 0; n->key[i] && i < sizeof ev.key - 1; i++)
            ev.key[i] = (char)lower((unsigned char)n->key[i]);
        a2_post(a, &ev.ev, 0);
    }
}

static void changed(const char *section, const char *key, const char *value)
{
    struct news n = { section, key, same(section, "desktop") && same(key, "theme") };
    (void)value;
    app_each(tell, &n);
}

/* ---- IDLE */

static void saver_tell(struct app *a, void *arg)
{
    struct glos_ev_saver ev;
    if (!a->v2)
        return;
    memset(&ev, 0, sizeof ev);
    ev.ev.size = sizeof ev;
    ev.ev.kind = GLOS_EV_SAVER;
    ev.on = *(int *)arg != 0;
    a2_post(a, &ev.ev, 0);
}

static void saver_told(int on) { app_each(saver_tell, &on); }

void api2_settings_init(void)
{
    kcfg_on_change(changed);
    idle_hook(saver_told);
}

static int awake(struct app *a, struct trapframe *tf)
{
    struct glos_awake q;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(q.handle = handle_new(&a->v2->h, GLOS_HK_AWAKE, (void *)1)))
        return a2_fail(tf, GLOS_E_MEMORY);
    if (a2_wr(tf, &q, sizeof q, size) != 0) {
        handle_close(&a->v2->h, q.handle);
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    idle_awake(1);
    return a2_ok(tf, 0);
}

static int saver(struct trapframe *tf)
{
    struct glos_saver q;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.what > GLOS_SAVER_END)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.what != GLOS_SAVER_STATE && idle_saver_ask(q.what == GLOS_SAVER_START) != 0)
        return a2_fail(tf, GLOS_E_REFUSED);
    q.on = (u32)idle_saver();
    q.wait = idle_saver_wait();
    q.awake = idle_awake_count();
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int power(struct trapframe *tf)
{
    struct glos_power q;
    u32 size, can = 0;
    int now;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.what > 1 || (q.what && q.state > GLOS_POWER_OFF))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    now = display_power(-1, &can);
    if (q.what) {
        if (!(can >> q.state & 1))
            return a2_fail(tf, GLOS_E_REFUSED);
        now = display_power((int)q.state, &can);
    }
    q.state = (u32)now;
    q.can = can;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

int api2_idle(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_IDLE_TIME & 0xFF: return a2_ok(tf, idle_ms());
    case GLOS_IDLE_AWAKE & 0xFF: return awake(a, tf);
    case GLOS_IDLE_SAVER & 0xFF: return saver(tf);
    case GLOS_IDLE_POWER & 0xFF: return power(tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
