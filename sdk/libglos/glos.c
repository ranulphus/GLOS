/* libglos: the GLOS API for DJGPP programs (include/glos/api.h; PRD §10).
 * The entry is found once, with DPMI 0A00h "GLOS"; without GLOS (plain DOS,
 * another DPMI host) every function fails or does nothing, so a program can
 * call glos_init() and carry on as a plain DOS program when it returns -1. */
#include <dpmi.h>
#include <sys/nearptr.h>

#include <glos/api.h>
#include <glos/gfx.h>

static struct {
    unsigned long off;
    unsigned short sel;
} entry;
static int state;                       /* 1: GLOS is there; -1: it isn't */
static int error, reason;
static unsigned id;

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

/* Function fn with b in EBX, c in ECX, d in EDX and si in ESI; EAX's result
   in *a, EBX's in *b: 0, or -1 with error (and reason) set. */
static int call5(unsigned fn, unsigned *a, unsigned *b, unsigned c, unsigned d, unsigned si)
{
    unsigned char failed;
    if (find() < 0) {
        error = 0;
        return -1;
    }
    *a = fn;
    __asm__ __volatile__("lcall *%[e]\n\tsetc %[f]"
                         : "+a"(*a), "+b"(*b), "+c"(c), "+d"(d), "+S"(si), [f] "=m"(failed)
                         : [e] "m"(entry)
                         : "edi", "cc", "memory");
    if (failed) {
        error = (int)*a;
        reason = (int)*b;
        return -1;
    }
    return 0;
}

static int call4(unsigned fn, unsigned *a, unsigned *b, unsigned c, unsigned d) { return call5(fn, a, b, c, d, 0); }

static int call(unsigned fn, unsigned *a, unsigned *b) { return call4(fn, a, b, 0, 0); }

/* ... for the functions that give nothing back but whether they worked. */
static int call3(unsigned fn, unsigned b, unsigned c, unsigned d)
{
    unsigned a;
    return call4(fn, &a, &b, c, d);
}

unsigned glos_version(void)
{
    unsigned a, b = 0;
    return call(GLOS_FN_VERSION, &a, &b) == 0 ? a : 0;
}

int glos_init(void)
{
    unsigned a, b = GLOS_API_VERSION;
    if (call(GLOS_FN_INIT, &a, &b) != 0)
        return -1;
    id = b;
    return 0;
}

int glos_error(void) { return error; }
int glos_reason(void) { return reason; }
unsigned glos_id(void) { return id; }

void glos_yield(void)
{
    unsigned a, b = 0;
    call(GLOS_FN_YIELD, &a, &b);
}

void glos_sleep(unsigned ms)
{
    unsigned a, b = ms;
    call(GLOS_FN_SLEEP, &a, &b);
}

unsigned glos_ticks(void)
{
    unsigned a, b = 0;
    return call(GLOS_FN_TICKS, &a, &b) == 0 ? a : 0;
}

int glos_event(struct glos_event *ev, unsigned wait_ms)
{
    unsigned a, b = (unsigned)ev;
    ev->size = sizeof *ev;
    return call4(GLOS_FN_EVENT, &a, &b, wait_ms, 0) == 0 ? (int)a : 0;
}

int glos_window(struct glos_window *w)
{
    w->size = sizeof *w;
    return call3(GLOS_FN_WINDOW, (unsigned)w, 0, 0);
}

int glos_close(unsigned id) { return call3(GLOS_FN_CLOSE, id, 0, 0); }
int glos_show(unsigned id, int on) { return call3(GLOS_FN_SHOW, id, on != 0, 0); }
int glos_move(unsigned id, int x, int y) { return call3(GLOS_FN_MOVE, id, (unsigned)x, (unsigned)y); }
int glos_resize(unsigned id, int w, int h) { return call3(GLOS_FN_RESIZE, id, (unsigned)w, (unsigned)h); }
int glos_title(unsigned id, const char *title) { return call3(GLOS_FN_TITLE, id, (unsigned)title, 0); }

int glos_pixels(unsigned id, struct glos_window *w)
{
    w->size = sizeof *w;
    return call3(GLOS_FN_PIXELS, id, (unsigned)w, 0);
}

int glos_present(unsigned id, const struct glos_rect *r) { return call3(GLOS_FN_PRESENT, id, (unsigned)r, 0); }
int glos_focus(unsigned id) { return call3(GLOS_FN_FOCUS, id, 0, 0); }

int glos_screen(struct glos_window *w)
{
    w->size = sizeof *w;
    return call3(GLOS_FN_SCREEN, (unsigned)w, 0, 0);
}

unsigned glos_caps(void)
{
    unsigned a, b = 0;
    return call(GLOS_FN_VERSION, &a, &b) == 0 && a >= 0x10001u ? b : 0;
}

int glos_gpu(struct glos_gpu *g)
{
    g->size = sizeof *g;
    return call3(GLOS_FN_GPU, (unsigned)g, 0, 0);
}

int glos_gpu_lock(void) { return call3(GLOS_FN_GPU_LOCK, 0, 0, 0); }
int glos_gpu_unlock(void) { return call3(GLOS_FN_GPU_UNLOCK, 0, 0, 0); }

int glos_vram(unsigned min, unsigned max, unsigned align, unsigned *off, unsigned *bytes)
{
    unsigned a, b = 0;
    if (call5(GLOS_FN_VRAM, &a, &b, min, max, align) != 0)
        return -1;
    *off = a;
    if (bytes)
        *bytes = b;
    return 0;
}

int glos_vram_free(unsigned off) { return call3(GLOS_FN_VRAM, 1, off, 0); }
int glos_winvram(unsigned id, unsigned off, unsigned pitch) { return call3(GLOS_FN_WINVRAM, id, off, pitch); }

int glos_audio(unsigned rate, unsigned channels, unsigned bytes, unsigned *id, struct glos_stream **s)
{
    unsigned a, b = 0;
    if (call5(GLOS_FN_AUDIO, &a, &b, rate, channels, bytes) != 0 || !(*s = glos_ptr(b)))
        return -1;
    *id = a;
    return 0;
}

int glos_audio_close(unsigned id) { return call3(GLOS_FN_AUDIO, 1, id, 0); }
int glos_audio_volume(unsigned id, unsigned volume) { return call3(GLOS_FN_AUDIO, 2, id, volume); }

/* A linear address as the program's own pointer: its data segment reaches
   everything from here on (DJGPP's "near pointers"), and the address is
   taken from the segment's base, which moves when the program's memory
   grows: so the pointer is asked for again after anything that may have
   allocated (or the program starts with _CRT0_FLAG_UNIX_SBRK, and it
   stays). */
void *glos_ptr(unsigned linear)
{
    static int on;
    if (!linear || (!on && !(on = __djgpp_nearptr_enable())))
        return 0;
    return (void *)(linear + __djgpp_conventional_base);
}

int glos_surface(const struct glos_window *w, struct gfx_surface *s)
{
    struct gfx_fmt f = { (unsigned char)w->bpp, w->rpos, w->rsize, w->gpos, w->gsize, w->bpos, w->bsize };
    void *pix = glos_ptr(w->pixels);
    if (!pix)
        return -1;
    gfx_init(s, pix, w->w, w->h, w->pitch, &f);
    return 0;
}
