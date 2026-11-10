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
