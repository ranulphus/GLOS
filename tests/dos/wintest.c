/* WINTEST: a GLOS app with a window (M6 steps E and F; the "win" suite of
 * tests/loopa/jobs.py). DJGPP, with libglos.
 *
 *   WINTEST show NAME SECONDS [X Y W H [FLAGS]]   (FLAGS: 1 fixed, 2 bare, 4 anywhere, 8 a pop-up, 16 the desktop)
 *                              a window with a picture of its own in it (a
 *                              colour by NAME's first letter, a white line
 *                              round it, the name and the size written in
 *                              it); every event is written out as it comes,
 *                              the picture drawn again for a new size; ends
 *                              when the window is asked to close (40) or
 *                              SECONDS have passed (41)
 *   WINTEST draw NAME SECONDS  a window with a square that moves along it, 50
 *                              steps a second at most (a sleep of 20 ms
 *                              after each); says how many it made (41)
 *   WINTEST spin NAME          a window, drawn in for ever as fast as the
 *                              program can and shown now and then: it never
 *                              sleeps, waits or asks for an event
 *   WINTEST keys NAME HOW SECONDS
 *                              a window, and the keyboard read one way, each
 *                              key written out, until Esc (42) or SECONDS
 *                              have passed (41). HOW:
 *                                ev    the API's events
 *                                bios  INT 16h, waiting in it (00h)
 *                                poll  INT 16h, asking first (11h, 10h)
 *                                port  port 60h, from a handler of its own
 *                                      for IRQ 1
 *   WINTEST mouse NAME SECONDS a window, and the mouse as INT 33h has it,
 *                              asked 50 times a second, written out when it
 *                              changes, until the right button (42)
 *
 * Lines: "WIN name=..." once the window is up; "EV NAME ...", "KEY NAME
 * ...", "MOUSE NAME ..."; "WIN-OK NAME ..." at the end. */
#include <bios.h>
#include <crt0.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/gfx.h>

int _crt0_startup_flags = _CRT0_FLAG_UNIX_SBRK; /* (memory that grows stays where it is: so do glos_ptr()'s pointers) */

static struct {
    glos_handle id;
    int w, h;
    unsigned bpp, pitch;
} win;
static struct gfx_surface surf;
static union {
    struct glos_ev ev;
    struct glos_ev_key key;
    struct glos_ev_mouse mouse;
    struct glos_ev_focus focus;
    struct glos_ev_size size;
    struct glos_ev_state state;
    unsigned char room[GLOS_EV_MAX];
} ev;

/* Where the window's pixels are now, and a surface on them. */
static int pixels(void)
{
    struct glos_pixels px;
    struct gfx_fmt f;
    void *p;
    memset(&px, 0, sizeof px);
    px.size = sizeof px;
    if (glos_pixels(win.id, &px) != 0 || !(p = glos_ptr(px.pixels)))
        return -1;
    f.bpp = (unsigned char)px.format.bpp;
    f.rpos = px.format.rpos, f.rsize = px.format.rsize, f.gpos = px.format.gpos, f.gsize = px.format.gsize;
    f.bpos = px.format.bpos, f.bsize = px.format.bsize;
    gfx_init(&surf, p, px.w, px.h, px.pitch, &f);
    win.w = px.w, win.h = px.h, win.bpp = px.format.bpp, win.pitch = px.pitch;
    return 0;
}
static const char *name = "?";

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static unsigned colour(void)
{
    switch (name[0]) {                          /* (colours of the palette's cube: the same at every depth) */
    case 'A': return 0xCC3333;
    case 'B': return 0x3333CC;
    case 'C': return 0x33CC33;
    default: return 0x999999;
    }
}

static void paint(void)
{
    char text[64];
    gfx_fill(&surf, 0, 0, win.w, win.h, colour());
    gfx_frame(&surf, 2, 2, win.w - 4, win.h - 4, 0xFFFFFF);
    sprintf(text, "%s %dx%d", name, win.w, win.h);
    gfx_text(&surf, 8, 8, &gfx_font_bold, text, -1, 0xFFFFFF);
    glos_present(win.id, 0, 0, 0, 0);
}

/* flags as the suite gives them: 1 fixed, 2 bare, 4 anywhere, 8 a pop-up, 16 the desktop. */
static void window(int x, int y, int w, int h, unsigned flags)
{
    char title[64];
    int id = 0, wx = 0, wy = 0;
    memset(&win, 0, sizeof win);
    sprintf(title, "WINTEST %s", name);
    win.id = glos_window(title, x, y, w, h, flags & 8 ? GLOS_WK_POPUP : flags & 16 ? GLOS_WK_DESKTOP : GLOS_WK_NORMAL,
                         (flags & 1 ? GLOS_WF_FIXED : 0u) | (flags & 2 ? GLOS_WF_BARE : 0u)
                         | (flags & 4 ? GLOS_WF_ANYWHERE : 0u), 0);
    if (!win.id || pixels() != 0) {
        say("WIN-FAIL %s window error=%d\n", name, glos_error());
        exit(1);
    }
    paint();
    glos_show(win.id, GLOS_WS_NORMAL);
    glos_get(win.id, GLOS_PROP_WIN_ID, &id);
    glos_get(win.id, GLOS_PROP_WIN_X, &wx);
    glos_get(win.id, GLOS_PROP_WIN_Y, &wy);
    say("WIN name=%s id=%d at=%d,%d size=%dx%d bpp=%u pitch=%u\n", name, id, wx, wy, win.w, win.h, win.bpp, win.pitch);
}

/* The window has new pixels: found, and drawn in. */
static int again(void)
{
    if (pixels() != 0)
        return -1;
    paint();
    return 0;
}

static int show(unsigned seconds)
{
    unsigned t0 = glos_ticks(), now;
    while ((now = glos_ticks() - t0) < seconds * 1000) {
        if (glos_event(&ev, sizeof ev, seconds * 1000 - now, 0) <= 0)
            continue;
        switch (ev.ev.kind) {
        case GLOS_EV_KEY:
            say("EV %s key %03X ch=%u down=%u mods=%u\n", name, ev.key.key, ev.key.down ? ev.key.ch : 0, ev.key.down,
                ev.key.mods);
            break;
        case GLOS_EV_TEXT:                      /* (the key's event had its character) */
            break;
        case GLOS_EV_MOUSE:
            say("EV %s mouse %d,%d buttons=%u button=%u down=%u\n", name, ev.mouse.x, ev.mouse.y, ev.mouse.buttons,
                ev.mouse.button, ev.mouse.down);
            break;
        case GLOS_EV_FOCUS:
            say("EV %s focus %u\n", name, ev.focus.got);
            break;
        case GLOS_EV_SIZE:
            if (again() != 0) {
                say("WIN-FAIL %s pixels\n", name);
                return 1;
            }
            say("EV %s resize %dx%d now=%dx%d bpp=%u\n", name, ev.size.w, ev.size.h, win.w, win.h, win.bpp);
            break;
        case GLOS_EV_STATE:
            say("EV %s state %u\n", name, ev.state.state);
            break;
        case GLOS_EV_CLOSE:
            say("EV %s close\n", name);
            glos_close(win.id);
            say("WIN-OK %s closed\n", name);
            return 40;
        default:
            say("EV %s type=%u\n", name, (unsigned)ev.ev.kind);
        }
    }
    say("WIN-OK %s time\n", name);
    return 41;
}

static void square(unsigned n)
{
    int x = (int)(n * 4 % (unsigned)(win.w - 16));
    gfx_fill(&surf, 0, 24, win.w, 16, colour());
    gfx_fill(&surf, x, 24, 16, 16, 0xFFFFFF);
}

static int draw(unsigned seconds)
{
    unsigned t0 = glos_ticks(), n = 0;
    while (glos_ticks() - t0 < seconds * 1000) {
        square(n++);
        glos_present(win.id, 0, 24, win.w, 16);
        glos_sleep(20);
    }
    say("WIN-OK %s frames=%u ms=%u\n", name, n, glos_ticks() - t0);
    return 41;
}

static int spin(void)
{
    unsigned n = 0;
    for (;;) {
        square(n >> 6);
        if (!(++n & 255))
            glos_present(win.id, 0, 24, win.w, 16);
    }
    return 0;
}

/* ---- the keyboard, four ways */

static volatile unsigned char ring[64];
static volatile unsigned rh, rt;

static void kb_isr(void)
{
    ring[rt++ % sizeof ring] = inportb(0x60);
    outportb(0x20, 0x20);
}

static int keys(const char *how, unsigned seconds)
{
    _go32_dpmi_seginfo isr, old;
    unsigned t0 = glos_ticks(), k = 0, e0 = 0;
    int port = !strcmp(how, "port"), done = 0;

    if (port) {
        _go32_dpmi_get_protected_mode_interrupt_vector(9, &old);
        isr.pm_offset = (unsigned long)kb_isr;
        isr.pm_selector = _go32_my_cs();
        _go32_dpmi_allocate_iret_wrapper(&isr);
        _go32_dpmi_set_protected_mode_interrupt_vector(9, &isr);
    }
    say("KEY %s %s ready\n", name, how);
    while (!done && glos_ticks() - t0 < seconds * 1000) {
        if (!strcmp(how, "ev")) {
            if (glos_event(&ev, sizeof ev, 100, 0) > 0 && ev.ev.kind == GLOS_EV_KEY) {
                say("KEY %s ev %03X %u %u\n", name, ev.key.key, ev.key.down ? ev.key.ch : 0, ev.key.down);
                done = ev.key.key == 1 && !ev.key.down;
            }
        } else if (!strcmp(how, "bios")) {
            k = _bios_keybrd(_NKEYBRD_READ);
            say("KEY %s bios %04X shift=%02X\n", name, k & 0xFFFF, _bios_keybrd(_NKEYBRD_SHIFTSTATUS) & 0x0F);
            done = (k & 0xFFFF) == 0x011B;
        } else if (!strcmp(how, "poll")) {
            if (_bios_keybrd(_NKEYBRD_READY)) {
                k = _bios_keybrd(_NKEYBRD_READ);
                say("KEY %s poll %04X\n", name, k & 0xFFFF);
                done = (k & 0xFFFF) == 0x011B;
            } else {
                glos_sleep(20);
            }
        } else {
            while (rh != rt) {
                k = ring[rh++ % sizeof ring];
                if (k == 0xE0) {
                    e0 = 0x100;
                    continue;
                }
                say("KEY %s port %03X %u\n", name, (k & 0x7F) | e0, !(k & 0x80));
                done |= k == 0x81;
                e0 = 0;
            }
            if (!done)
                glos_sleep(20);
        }
    }
    if (port) {
        _go32_dpmi_set_protected_mode_interrupt_vector(9, &old);
        _go32_dpmi_free_iret_wrapper(&isr);
    }
    say("WIN-OK %s %s\n", name, done ? "esc" : "time");
    return done ? 42 : 41;
}

static int mouse(unsigned seconds)
{
    __dpmi_regs r;
    unsigned t0 = glos_ticks();
    int x = -1, y = -1, b = -1, mx = 0, my = 0;

    memset(&r, 0, sizeof r);
    __dpmi_int(0x33, &r);
    say("MOUSE %s driver=%04X buttons=%u\n", name, r.x.ax, r.x.bx);
    while (glos_ticks() - t0 < seconds * 1000) {
        memset(&r, 0, sizeof r);
        r.x.ax = 0x0B;
        __dpmi_int(0x33, &r);
        mx += (short)r.x.cx, my += (short)r.x.dx;
        memset(&r, 0, sizeof r);
        r.x.ax = 3;
        __dpmi_int(0x33, &r);
        if (r.x.cx != x || r.x.dx != y || r.x.bx != b) {
            x = r.x.cx, y = r.x.dx, b = r.x.bx;
            say("MOUSE %s at=%d,%d b=%d moved=%d,%d\n", name, (short)x, (short)y, b, mx, my);
            if (b & 2) {
                say("WIN-OK %s right\n", name);
                return 42;
            }
        }
        glos_sleep(20);
    }
    say("WIN-OK %s time\n", name);
    return 41;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    int n[5] = { 0, 0, 200, 120, 4 }, i, r;

    if (argc < 3) {
        say("usage: WINTEST show NAME SECONDS [X Y W H [FLAGS]] | draw NAME SECONDS | spin NAME |"
            " keys NAME ev|bios|poll|port SECONDS | mouse NAME SECONDS\n");
        return 2;
    }
    name = argv[2];
    if ((r = glos_init("WINTEST", "", "")) != 0) {
        say("WIN-FAIL %s init error=%d reason=%u\n", name, r, glos_why());
        return 3;
    }
    if (!strcmp(mode, "show") && argc > 3) {
        for (i = 0; i < 5 && argc > 4 + i; i++)
            n[i] = atoi(argv[4 + i]);
        if (argc == 8)                          /* (a place given: there) */
            n[4] = 0;
        window(n[0], n[1], n[2], n[3], (unsigned)n[4]);
        return show((unsigned)atoi(argv[3]));
    }
    window(0, 0, 200, 120, 4);
    if (!strcmp(mode, "spin"))
        return spin();
    if (!strcmp(mode, "draw") && argc > 3)
        return draw((unsigned)atoi(argv[3]));
    if (!strcmp(mode, "keys") && argc > 4)
        return keys(argv[3], (unsigned)atoi(argv[4]));
    if (!strcmp(mode, "mouse") && argc > 3)
        return mouse((unsigned)atoi(argv[3]));
    say("WIN-FAIL %s mode\n", name);
    return 2;
}
