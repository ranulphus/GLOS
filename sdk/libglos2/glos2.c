/* libglos 2.0 (include/glos/glos2.h): API 2.0 for DJGPP programs. A call is
 * EAX the function, EBX its structure (a near pointer is DS's offset in
 * DJGPP's flat model), ECX and EDX 0 (glos/api2.h). */
#include <dpmi.h>
#include <string.h>
#include <sys/exceptn.h>
#include <sys/nearptr.h>

#include <glos/glos2.h>

static struct {
    unsigned long off;
    unsigned short sel;
} entry;
static int state;                       /* 1: GLOS is there; -1: it isn't */
static unsigned app;

static int find(void)
{
    __dpmi_paddr p;
    if (!state) {
        if (__dpmi_get_vendor_specific_api_entry_point("GLOS", &p) == 0) {
            entry.off = p.offset32;
            entry.sel = p.selector;
            state = 1;
        } else {
            state = -1;
        }
    }
    return state;
}

/* The call: 0, or GLOS's error (the carry flag set), or -1 without GLOS; *eax what EAX came back as. */
static int raw(unsigned fn, void *s, unsigned *eax)
{
    unsigned a, b, c = 0, d = 0;
    unsigned char failed;
    if (find() < 0)
        return -1;
    do {                                /* (again while a wait was cut short for an interrupt of the program's
                                           own: with ECX and EDX as GLOS left them, for the rest of the wait) */
        a = fn;
        b = (unsigned)s;
        __asm__ __volatile__("lcall *%[e]\n\tsetc %[f]"
                             : "+a"(a), "+b"(b), "+c"(c), "+d"(d), [f] "=m"(failed)
                             : [e] "m"(entry)
                             : "esi", "edi", "cc", "memory");
    } while (failed && a == GLOS_E_AGAIN);
    if (eax)
        *eax = a;
    return failed ? (int)a : 0;
}

int glos_call(unsigned fn, void *s) { return raw(fn, s, 0); }

unsigned __glos_call_eax(unsigned fn)   /* (glos2thr.c's) */
{
    unsigned v = 0;
    return raw(fn, 0, &v) == 0 ? v : 0;
}

int glos_running(void)
{
    unsigned v = 0;
    return raw(GLOS_CORE_VERSION, 0, &v) == 0 && (v >> 16) == GLOS_MAJOR;
}

int glos_version(struct glos_version *v) { return raw(GLOS_CORE_VERSION, v, 0); }

static void copy(char *dst, const char *src, unsigned room)
{
    if (src) {
        strncpy(dst, src, room - 1);
        dst[room - 1] = 0;
    }
}

int glos_init(const char *name, const char *help, const char *category)
{
    struct glos_init in;
    int r;
    memset(&in, 0, sizeof in);
    in.size = sizeof in;
    in.major = GLOS_MAJOR;
    in.minor = GLOS_MINOR;
    copy(in.name, name, sizeof in.name);
    copy(in.help, help, sizeof in.help);
    copy(in.category, category, sizeof in.category);
    r = raw(GLOS_CORE_INIT, &in, 0);
    if (!r)
        app = in.app;
    return r;
}

unsigned glos_app(void) { return app; }

int glos_close(glos_handle h)
{
    struct glos_close q = { sizeof q, h };
    return raw(GLOS_CORE_CLOSE, &q, 0);
}

int glos_get(glos_handle h, unsigned prop, int *value)
{
    struct glos_prop p;
    int r;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.handle = h;
    p.prop = prop;
    r = raw(GLOS_CORE_GET, &p, 0);
    if (!r && value)
        *value = p.value;
    return r;
}

int glos_set(glos_handle h, unsigned prop, int value)
{
    struct glos_prop p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.handle = h;
    p.prop = prop;
    p.value = value;
    return raw(GLOS_CORE_SET, &p, 0);
}

int glos_get_text(glos_handle h, unsigned prop, char *buf, unsigned room)
{
    struct glos_prop p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.handle = h;
    p.prop = prop;
    p.data = (unsigned)buf;
    p.room = room;
    return raw(GLOS_CORE_GET, &p, 0);
}

int glos_set_text(glos_handle h, unsigned prop, const char *text)
{
    struct glos_prop p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.handle = h;
    p.prop = prop;
    p.data = (unsigned)text;
    p.room = (unsigned)strlen(text);
    return raw(GLOS_CORE_SET, &p, 0);
}

int glos_get_data(glos_handle h, unsigned prop, void *buf, unsigned room, unsigned *length)
{
    struct glos_prop p;
    int r;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.handle = h;
    p.prop = prop;
    p.data = (unsigned)buf;
    p.room = room;
    r = raw(GLOS_CORE_GET, &p, 0);
    if (length)
        *length = p.length;
    return r;
}

int glos_set_data(glos_handle h, unsigned prop, const void *data, unsigned length)
{
    struct glos_prop p;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    p.handle = h;
    p.prop = prop;
    p.data = (unsigned)data;
    p.room = length;
    return raw(GLOS_CORE_SET, &p, 0);
}

int glos_event(void *buf, unsigned room, unsigned ms, unsigned areas)
{
    struct glos_wait w;
    int r;
    memset(&w, 0, sizeof w);
    w.size = sizeof w;
    w.ms = ms;
    w.areas = areas;
    w.buffer = (unsigned)buf;
    w.room = room;
    r = raw(GLOS_CORE_EVENT, &w, 0);
    return r > 0 ? -r : r < 0 ? r : (int)w.length;
}

int glos_post(unsigned code, unsigned a, unsigned b, unsigned c)
{
    struct glos_post q = { sizeof q, code, a, b, c };
    return raw(GLOS_CORE_POST, &q, 0);
}

glos_handle glos_timer(unsigned ms, int repeat)
{
    struct glos_timer q;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.ms = ms;
    q.repeat = repeat != 0;
    return raw(GLOS_CORE_TIMER, &q, 0) == 0 ? q.handle : 0;
}

unsigned glos_ticks(void)
{
    unsigned v = 0;
    return raw(GLOS_CORE_TICKS, 0, &v) == 0 ? v : 0;
}

int glos_sleep(unsigned ms)
{
    struct glos_sleep q;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.ms = ms;
    return raw(GLOS_CORE_SLEEP, &q, 0);
}

int glos_yield(void) { return raw(GLOS_CORE_YIELD, 0, 0); }

int glos_time(struct glos_time *t) { return raw(GLOS_CORE_TIME, t, 0); }

int glos_random(void *buf, unsigned n)
{
    struct glos_random q = { sizeof q, (unsigned)buf, n };
    return raw(GLOS_CORE_RANDOM, &q, 0);
}

/* ---- windows and their pixels */

static int last_error;

int glos_error(void) { return last_error; }
void __glos_error_set(int e) { last_error = e; }       /* (glos2thr.c's) */

glos_handle glos_window(const char *title, int x, int y, int w, int h, unsigned kind, unsigned flags, glos_handle owner)
{
    struct glos_win_create q;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.kind = kind, q.flags = flags;
    q.x = x, q.y = y, q.w = w, q.h = h;
    q.owner = owner;
    copy(q.title, title, sizeof q.title);
    last_error = raw(GLOS_WIN_CREATE, &q, 0);
    return last_error ? 0 : q.handle;
}

int glos_show(glos_handle w, unsigned state)
{
    struct glos_win_show q = { sizeof q, w, state, 0 };
    return raw(GLOS_WIN_SHOW, &q, 0);
}

int glos_move(glos_handle w, int x, int y)
{
    struct glos_win_place q = { sizeof q, w, GLOS_PLACE_MOVE, x, y, 0, 0 };
    return raw(GLOS_WIN_PLACE, &q, 0);
}

int glos_resize(glos_handle w, int cw, int ch)
{
    struct glos_win_place q = { sizeof q, w, GLOS_PLACE_SIZE, 0, 0, cw, ch };
    return raw(GLOS_WIN_PLACE, &q, 0);
}

int glos_focus(glos_handle w)
{
    struct glos_win_focus q = { sizeof q, w, 0 };
    return raw(GLOS_WIN_FOCUS, &q, 0);
}

int glos_windows(unsigned app_id, struct glos_win_info *out, unsigned max, unsigned *count)
{
    struct glos_win_enum q = { sizeof q, app_id, (unsigned)out, max * sizeof *out, 0 };
    int r = raw(GLOS_WIN_ENUM, &q, 0);
    if (count)
        *count = q.count;
    return r;
}

int glos_drag(glos_handle w, unsigned part)
{
    struct glos_win_drag q = { sizeof q, w, part };
    return raw(GLOS_WIN_DRAG, &q, 0);
}

int glos_pixels(glos_handle w, struct glos_pixels *p)
{
    memset(p, 0, sizeof *p);
    p->size = sizeof *p;
    p->handle = w;
    return raw(GLOS_DRAW_PIXELS, p, 0);
}

int glos_present(glos_handle w, int x, int y, int cw, int ch)
{
    struct glos_present q = { sizeof q, w, x, y, cw, ch };
    return raw(GLOS_DRAW_PRESENT, &q, 0);
}

int glos_screen(struct glos_screen *s)
{
    memset(s, 0, sizeof *s);
    s->size = sizeof *s;
    return raw(GLOS_DRAW_SCREEN, s, 0);
}

int glos_blit(glos_handle w, int x, int y, const void *pixels, int pw, int ph, unsigned pitch, const struct glos_format *format,
              const unsigned *palette)
{
    struct glos_blit q;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.handle = w;
    q.pixels = (unsigned)pixels, q.pitch = pitch;
    q.format = *format;
    q.w = pw, q.h = ph, q.x = x, q.y = y;
    q.palette = (unsigned)palette;
    return raw(GLOS_DRAW_BLIT, &q, 0);
}

int glos_keys(struct glos_keys *k)
{
    memset(k, 0, sizeof *k);
    k->size = sizeof *k;
    return raw(GLOS_INPUT_KEYS, k, 0);
}

int glos_capture(glos_handle w, int on)
{
    struct glos_capture q = { sizeof q, w, on != 0 };
    return raw(GLOS_INPUT_CAPTURE, &q, 0);
}

int glos_pads(struct glos_pads *p, int events)
{
    memset(p, 0, sizeof *p);
    p->size = sizeof *p;
    p->flags = events ? GLOS_PADS_EVENTS : 0;
    return raw(GLOS_INPUT_PADS, p, 0);
}

int glos_gameport(struct glos_gameport *g)
{
    memset(g, 0, sizeof *g);
    g->size = sizeof *g;
    return raw(GLOS_INPUT_GAMEPORT, g, 0);
}

int glos_input_mode(unsigned flags)
{
    struct glos_input_mode q = { sizeof q, 1, flags };
    return raw(GLOS_INPUT_MODE, &q, 0);
}

void *glos_ptr(unsigned linear)
{
    static int on;
    if (!linear)
        return 0;
    if (!on) {
        if (!(on = __djgpp_nearptr_enable()))
            return 0;
        /* (near pointers reach everywhere through the data segment's limit; the threads a program makes run on
           DJGPP's second data selector, which gets the same limit: glos2wrap.c) */
        __dpmi_set_segment_limit(__djgpp_ds_alias, 0xFFFFFFFFul);
    }
    return (void *)(linear + __djgpp_conventional_base);
}

/* ---- the clipboard, drag and drop */

glos_handle glos_clip_open(int write)
{
    struct glos_clip_open q = { sizeof q, write != 0, 0, 0 };
    int r = raw(GLOS_CLIP_OPEN, &q, 0);
    if (r)
        last_error = r;
    return r ? 0 : q.handle;
}

int glos_clip_add(glos_handle t, unsigned format, const void *data, unsigned length)
{
    struct glos_clip_data q = { sizeof q, t, format, (unsigned)data, length, 0, 0 };
    return raw(GLOS_CLIP_PUT, &q, 0);
}

int glos_clip_read(glos_handle t, unsigned format, void *buf, unsigned room, unsigned offset, unsigned *total)
{
    struct glos_clip_data q = { sizeof q, t, format, (unsigned)buf, room, offset, 0 };
    unsigned n = 0;
    int r = raw(GLOS_CLIP_GET, &q, &n);
    if (!r && total)
        *total = q.total;
    return r ? -r : (int)n;
}

int glos_clip_formats(glos_handle t, struct glos_clip_formats *f)
{
    memset(f, 0, sizeof *f);
    f->size = sizeof *f;
    f->handle = t;
    return raw(GLOS_CLIP_FORMATS, f, 0);
}

unsigned glos_clip_format(const char *name)
{
    struct glos_clip_format q = { sizeof q, (unsigned)name, 0 };
    int r = raw(GLOS_CLIP_FORMAT, &q, 0);
    if (r)
        last_error = r;
    return r ? 0 : q.format;
}

int glos_clip_put(unsigned format, const void *data, unsigned length)
{
    glos_handle t = glos_clip_open(1);
    int r;
    if (!t)
        return last_error;
    if ((r = glos_clip_add(t, format, data, length)) != 0)
        glos_set(t, GLOS_PROP_CLIP_COMMIT, 0);
    glos_close(t);
    return r;
}

int glos_clip_get(unsigned format, void *buf, unsigned room, unsigned *length)
{
    glos_handle t = glos_clip_open(0);
    unsigned total = 0;
    int n;
    if (!t)
        return last_error;
    n = glos_clip_read(t, format, buf, room, 0, &total);
    glos_close(t);
    if (length)
        *length = n < 0 ? 0 : total;
    return n < 0 ? -n : total > room ? GLOS_E_ROOM : 0;
}

int glos_dnd(glos_handle w, glos_handle data, unsigned effects, const unsigned *picture, unsigned pw, unsigned ph)
{
    struct glos_dnd q = { sizeof q, w, data, effects, (unsigned)picture, pw, ph };
    return raw(GLOS_DND_DRAG, &q, 0);
}

int glos_dnd_target(glos_handle w, unsigned effects, const unsigned *formats, unsigned count)
{
    struct glos_dnd_target q;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.window = w;
    q.effects = effects;
    q.count = count <= GLOS_CLIP_ITEMS ? count : GLOS_CLIP_ITEMS + 1;
    if (formats && count <= GLOS_CLIP_ITEMS)
        memcpy(q.format, formats, count * sizeof *formats);
    return raw(GLOS_DND_TARGET, &q, 0);
}

int glos_dnd_answer(glos_handle w, unsigned effect)
{
    struct glos_dnd_answer q = { sizeof q, w, effect };
    return raw(GLOS_DND_ANSWER, &q, 0);
}

/* ---- files and disks */

int glos_fs_list(glos_handle *listing, const char *path, void *buf, unsigned room, unsigned *count, unsigned flags)
{
    struct glos_fs_list q;
    int r;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.handle = *listing;
    q.path = (unsigned)path;
    q.flags = flags;
    q.buffer = (unsigned)buf;
    q.room = room;
    r = raw(GLOS_FS_LIST, &q, 0);
    if (count)
        *count = r ? 0 : q.count;
    if (!r)
        *listing = q.handle;
    return r;
}

int glos_fs_info(const char *paths, unsigned count, void *buf, unsigned room, unsigned flags)
{
    struct glos_fs_info q;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.paths = (unsigned)paths;
    q.count = count;
    q.flags = flags;
    q.buffer = (unsigned)buf;
    q.room = room;
    return raw(GLOS_FS_INFO, &q, 0);
}

int glos_fs_drives(unsigned drive, struct glos_fs_drive *out, unsigned room, unsigned *count)
{
    struct glos_fs_drives q = { sizeof q, drive, (unsigned)out, room, 0 };
    int r = raw(GLOS_FS_DRIVES, &q, 0);
    if (count)
        *count = q.count;
    return r;
}

glos_handle glos_fs_watch(const char *path)
{
    struct glos_fs_watch q = { sizeof q, (unsigned)path, 0 };
    int r = raw(GLOS_FS_WATCH, &q, 0);
    if (r)
        last_error = r;
    return r ? 0 : q.handle;
}

int glos_fs_sectors(struct glos_fs_sectors *s)
{
    s->size = sizeof *s;
    return raw(GLOS_FS_SECTORS, s, 0);
}

/* ---- settings, the screen's modes, idle time */

int glos_setting_get(const char *section, const char *key, char *value, unsigned room, int *set)
{
    struct glos_setting q;
    int r;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.section = (unsigned)section;
    q.key = (unsigned)key;
    q.value = (unsigned)value;
    q.room = room;
    r = raw(GLOS_SETTINGS_GET, &q, 0);
    if ((!r || r == GLOS_E_ROOM) && set)
        *set = (int)q.set;
    return r;
}

int glos_setting_set(const char *section, const char *key, const char *value, unsigned *when, char *why, unsigned room)
{
    struct glos_setting q;
    int r;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.section = (unsigned)section;
    q.key = (unsigned)key;
    q.value = (unsigned)value;
    q.why = (unsigned)why;
    q.why_room = why ? room : 0;
    if (why && room)
        why[0] = 0;
    r = raw(GLOS_SETTINGS_SET, &q, 0);
    if (!r && when)
        *when = q.when;
    return r;
}

static int list(unsigned fn, const char *section, char *buf, unsigned room)
{
    struct glos_setting_list q;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.section = (unsigned)section;
    q.buffer = (unsigned)buf;
    q.room = room;
    return raw(fn, &q, 0);
}

int glos_setting_sections(char *buf, unsigned room) { return list(GLOS_SETTINGS_SECTIONS, 0, buf, room); }
int glos_setting_keys(const char *section, char *buf, unsigned room) { return list(GLOS_SETTINGS_KEYS, section, buf, room); }

int glos_schema(unsigned index, struct glos_schema *s, char *buf, unsigned room)
{
    memset(s, 0, sizeof *s);
    s->size = sizeof *s;
    s->index = index;
    s->buffer = (unsigned)buf;
    s->room = room;
    return raw(GLOS_SETTINGS_SCHEMA, s, 0);
}

int glos_setting_watch(const char *section, int on)
{
    struct glos_setting_watch q = { sizeof q, (unsigned)section, on != 0 };
    return raw(GLOS_SETTINGS_WATCH, &q, 0);
}

int glos_theme(struct glos_theme *t)
{
    memset(t, 0, sizeof *t);
    t->size = sizeof *t;
    return raw(GLOS_SETTINGS_THEME, t, 0);
}

int glos_region(struct glos_region *r)
{
    memset(r, 0, sizeof *r);
    r->size = sizeof *r;
    return raw(GLOS_SETTINGS_REGION, r, 0);
}

int glos_modes(struct glos_mode *out, unsigned room, unsigned *count)
{
    struct glos_modes q = { sizeof q, (unsigned)out, room, 0 };
    int r = raw(GLOS_DRAW_MODES, &q, 0);
    if (count)
        *count = q.count;
    return r;
}

unsigned glos_idle(void)
{
    unsigned v = 0;
    return raw(GLOS_IDLE_TIME, 0, &v) == 0 ? v : 0;
}

glos_handle glos_awake(void)
{
    struct glos_awake q = { sizeof q, 0 };
    int r = raw(GLOS_IDLE_AWAKE, &q, 0);
    if (r)
        last_error = r;
    return r ? 0 : q.handle;
}

int glos_saver(unsigned what, struct glos_saver *state)
{
    struct glos_saver q;
    int r;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.what = what;
    r = raw(GLOS_IDLE_SAVER, &q, 0);
    if (!r && state)
        *state = q;
    return r;
}

int glos_power(int set, unsigned state, struct glos_power *now)
{
    struct glos_power q;
    int r;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.what = set != 0;
    q.state = state;
    r = raw(GLOS_IDLE_POWER, &q, 0);
    if (now)
        *now = q;
    return r;
}

/* ---- SHELL */

int glos_shell(int on)
{
    struct glos_shell_desktop q = { sizeof q, (unsigned)on };
    return raw(GLOS_SHELL_DESKTOP, &q, 0);
}

glos_handle glos_tray(const unsigned *icon, const char *tip)
{
    struct glos_tray q = { sizeof q, (unsigned)icon, (unsigned)tip, 0 };
    int r = raw(GLOS_SHELL_TRAY, &q, 0);
    if (r)
        last_error = r;
    return r ? 0 : q.handle;
}

glos_handle glos_notify(const char *title, const char *text, const char *action1, const char *action2,
                        const unsigned *icon, unsigned seconds)
{
    struct glos_notify q;
    int r;
    memset(&q, 0, sizeof q);
    q.size = sizeof q;
    q.title = (unsigned)title;
    q.text = (unsigned)text;
    q.action[0] = (unsigned)action1;
    q.action[1] = (unsigned)action2;
    q.icon = (unsigned)icon;
    q.seconds = seconds;
    if ((r = raw(GLOS_SHELL_NOTIFY, &q, 0)) != 0)
        last_error = r;
    return r ? 0 : q.handle;
}

int glos_point(unsigned what, struct glos_point *out)
{
    memset(out, 0, sizeof *out);
    out->size = sizeof *out;
    out->what = what;
    return raw(GLOS_SHELL_POINT, out, 0);
}

int glos_help(const char *page)
{
    struct glos_help q = { sizeof q, (unsigned)page };
    return raw(GLOS_SHELL_HELP, &q, 0);
}

int glos_open(const char *path, const char *with, const char *verb)
{
    struct glos_open q = { sizeof q, (unsigned)path, (unsigned)with, (unsigned)verb, 0 };
    int r = raw(GLOS_SHELL_OPEN, &q, 0);
    if (r)
        last_error = r;
    return r ? -1 : (int)q.job;
}

static int assoc(unsigned what, const char *kind, const char *text, const char *args, char *buf, unsigned room,
                 unsigned *length)
{
    struct glos_assoc q = { sizeof q, what, (unsigned)kind, (unsigned)text, (unsigned)args, (unsigned)buf, room, 0 };
    int r = raw(GLOS_SHELL_ASSOC, &q, 0);
    if (length)
        *length = q.length;
    return r;
}

int glos_assoc_get(const char *kind, char *program, unsigned room)
{
    return assoc(GLOS_ASSOC_GET, kind, 0, 0, program, room, 0);
}

int glos_assoc_set(const char *kind, const char *program) { return assoc(GLOS_ASSOC_SET, kind, program, 0, 0, 0, 0); }

int glos_assoc_verbs(const char *kind, char *list, unsigned room, unsigned *length)
{
    return assoc(GLOS_ASSOC_VERBS, kind, 0, 0, list, room, length);
}

int glos_assoc_verb(const char *kind, const char *label, const char *args)
{
    return assoc(GLOS_ASSOC_VERB, kind, label, args, 0, 0, 0);
}

static int entry_call(unsigned what, const char *title, const char *group, const char *icon, const char *args, const char *label,
                 char *buf, unsigned room, unsigned *length)
{
    struct glos_entry q = { sizeof q, what, (unsigned)title, (unsigned)group, (unsigned)icon, (unsigned)args, (unsigned)label,
                            (unsigned)buf, room, 0 };
    int r = raw(GLOS_SHELL_ENTRY, &q, 0);
    if (length)
        *length = q.length;
    return r;
}

int glos_entry_set(const char *title, const char *group, const char *icon, const char *args)
{
    return entry_call(GLOS_ENTRY_SET, title, group, icon, args, 0, 0, 0, 0);
}

int glos_entry_remove(void) { return entry_call(GLOS_ENTRY_REMOVE, 0, 0, 0, 0, 0, 0, 0, 0); }

int glos_entry_task(const char *label, const char *args) { return entry_call(GLOS_ENTRY_TASK, 0, 0, 0, args, label, 0, 0, 0); }

int glos_entry_list(char *list, unsigned room, unsigned *length)
{
    return entry_call(GLOS_ENTRY_LIST, 0, 0, 0, 0, 0, list, room, length);
}

static int recent(unsigned what, const char *path, const char *program, char *buf, unsigned room, unsigned *length)
{
    struct glos_recent q = { sizeof q, what, (unsigned)path, (unsigned)program, (unsigned)buf, room, 0 };
    int r = raw(GLOS_SHELL_RECENT, &q, 0);
    if (length)
        *length = q.length;
    return r;
}

int glos_recent_add(const char *path) { return recent(GLOS_RECENT_ADD, path, 0, 0, 0, 0); }

int glos_recent_list(const char *program, char *list, unsigned room, unsigned *length)
{
    return recent(GLOS_RECENT_LIST, 0, program, list, room, length);
}

int glos_recent_clear(const char *path) { return recent(GLOS_RECENT_CLEAR, path, 0, 0, 0, 0); }

static int sched(unsigned what, const char *name, const char *when, const char *command, char *buf, unsigned room,
                 unsigned *length)
{
    struct glos_schedule q = { sizeof q, what, (unsigned)name, (unsigned)when, (unsigned)command, (unsigned)buf, room, 0 };
    int r = raw(GLOS_SHELL_SCHEDULE, &q, 0);
    if (length)
        *length = q.length;
    return r;
}

int glos_schedule_set(const char *name, const char *when, const char *command)
{
    return sched(GLOS_SCHEDULE_SET, name, when, command, 0, 0, 0);
}

int glos_schedule_remove(const char *name) { return sched(GLOS_SCHEDULE_REMOVE, name, 0, 0, 0, 0, 0); }

int glos_schedule_list(char *list, unsigned room, unsigned *length)
{
    return sched(GLOS_SCHEDULE_LIST, 0, 0, 0, list, room, length);
}

/* ---- JOB */

glos_handle glos_job_run(const char *command, const char *dir, const char *env, unsigned how, unsigned flags,
                         const char *profile)
{
    struct glos_job_run q;
    memset(&q, 0, sizeof q);
    q.command = (unsigned)command;
    q.dir = (unsigned)dir;
    q.env = (unsigned)env;
    q.how = how;
    q.flags = flags;
    q.profile = (unsigned)profile;
    return glos_job_start(&q);
}

glos_handle glos_job_start(struct glos_job_run *run)
{
    int r;
    run->size = sizeof *run;
    r = raw(GLOS_JOB_RUN, run, 0);
    last_error = r;
    return r ? 0 : run->handle;
}

static int job_io(unsigned fn, glos_handle job, unsigned stream, const void *buf, unsigned length)
{
    struct glos_job_io q = { sizeof q, job, stream, (unsigned)buf, length, 0 };
    int r = raw(fn, &q, 0);
    if (r)
        last_error = r;
    return r ? -1 : (int)q.done;
}

int glos_job_read(glos_handle job, unsigned stream, void *buf, unsigned room)
{
    return job_io(GLOS_JOB_READ, job, stream, buf, room);
}

int glos_job_write(glos_handle job, const void *data, unsigned length)
{
    return length ? job_io(GLOS_JOB_WRITE, job, GLOS_JOB_IN, data, length) : 0;
}

int glos_job_eof(glos_handle job) { return job_io(GLOS_JOB_WRITE, job, GLOS_JOB_IN, 0, 0); }

int glos_job_keys(glos_handle job, const unsigned short *keys, unsigned count)
{
    int n = job_io(GLOS_JOB_WRITE, job, GLOS_JOB_KEYS, keys, count * 2);
    return n < 0 ? n : n / 2;
}

int glos_job_signal(glos_handle job, unsigned what)
{
    struct glos_job_signal q = { sizeof q, job, what };
    return raw(GLOS_JOB_SIGNAL, &q, 0);
}

int glos_job_status(glos_handle job, struct glos_job_status *out)
{
    memset(out, 0, sizeof *out);
    out->size = sizeof *out;
    out->handle = job;
    return raw(GLOS_JOB_STATUS, out, 0);
}

/* ---- SYS */

int glos_sys_stat(unsigned kind, void *buf, unsigned room, unsigned *length, unsigned *count)
{
    struct glos_sys_stat q = { sizeof q, kind, (unsigned)buf, room, 0, 0 };
    int r = raw(GLOS_SYS_STAT, &q, 0);
    if (length)
        *length = q.length;
    if (count)
        *count = q.count;
    return r;
}

int glos_programs(struct glos_program *out, unsigned room, unsigned *count)
{
    struct glos_sys_stat q = { sizeof q, 0, (unsigned)out, room, 0, 0 };
    int r = raw(GLOS_SYS_PROGRAMS, &q, 0);
    if (count)
        *count = q.count;
    return r;
}

int glos_sys_kill(unsigned what, unsigned id)
{
    struct glos_sys_kill q = { sizeof q, what, id };
    return raw(GLOS_SYS_KILL, &q, 0);
}

int glos_sys_log(unsigned pos, char *buf, unsigned room, unsigned *next, int follow)
{
    struct glos_sys_log q = { sizeof q, pos, (unsigned)buf, room, 0, 0, (unsigned)follow };
    int r = raw(GLOS_SYS_LOG, &q, 0);
    if (next)
        *next = q.next;
    if (r)
        last_error = r;
    return r ? -1 : (int)q.length;
}
