/* libglos 2.0 (include/glos/glos2.h): API 2.0 for DJGPP programs. A call is
 * EAX the function, EBX its structure (a near pointer is DS's offset in
 * DJGPP's flat model), ECX and EDX 0 (glos/api2.h). */
#include <dpmi.h>
#include <string.h>
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
    unsigned a = fn, b = (unsigned)s, c = 0, d = 0;
    unsigned char failed;
    if (find() < 0)
        return -1;
    __asm__ __volatile__("lcall *%[e]\n\tsetc %[f]"
                         : "+a"(a), "+b"(b), "+c"(c), "+d"(d), [f] "=m"(failed)
                         : [e] "m"(entry)
                         : "esi", "edi", "cc", "memory");
    if (eax)
        *eax = a;
    return failed ? (int)a : 0;
}

int glos_call(unsigned fn, void *s) { return raw(fn, s, 0); }

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

void *glos_ptr(unsigned linear)
{
    static int on;
    if (!linear || (!on && !(on = __djgpp_nearptr_enable())))
        return 0;
    return (void *)(linear + __djgpp_conventional_base);
}
