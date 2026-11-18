/* What every saver's program does the same (phase D, docs/v1/phase-d.md D6): become a GLOS app, see that DOS-GL
 * has a window here, make the context (the screen's size, its window topmost and full-screen; or /P W H, its own
 * preview in an ordinary window), and between frames show the picture and look for GLOS's word to end.
 *
 *     int main(int argc, char **argv)
 *     {
 *         int w, h, r = saver_begin(argc, argv, "Stars", 0, &w, &h);
 *         if (r)
 *             return r;
 *         ... the scene's set-up ...
 *         do {
 *             ... a frame, by saver_ms() ...
 *         } while (saver_shown());
 *         return saver_done();
 *     }
 *
 * A saver ends at once, with a code that says why, where it can't draw (GLOS then shows its blank saver): 2 no
 * GLOS, 3 no windowed DOS-GL on this card or at this depth (a DOS-GL before 1.7, whose window is a Matrox card's
 * at 16 bits alone), 4 DOS-GL wouldn't start. In a header: each saver is one source file. */
#ifndef SAVERS_SAVER_H
#define SAVERS_SAVER_H
#include <stdlib.h>
#include <string.h>

#include <GL/dosgl.h>
#include <GL/gl.h>
#include <glos/glos.h>

static unsigned saver_last, saver_step;
static unsigned saver_speed = 100;      /* [saver] speed, percent: slow 50, normal 100, fast 200 */
static unsigned saver_seed = 0x1234567u;

/* 0 to 1, the same numbers each run. */
static inline float saver_rnd(void)
{
    saver_seed = saver_seed * 1103515245u + 12345u;
    return (float)(saver_seed >> 8 & 0xFFFF) / 65535.0f;
}

/* The context made, w by h; 0, or the code to end with. depth: a depth buffer too. */
static inline int saver_begin(int argc, char **argv, const char *name, int depth, int *w, int *h)
{
    struct glos_version ver;
    struct glos_screen scr;
    DGLConfig cfg;
    glos_handle win;
    int preview = argc >= 4 && (!strcmp(argv[1], "/P") || !strcmp(argv[1], "/p"));
    if (glos_init(name, 0, 0) != 0)
        return 2;
    memset(&ver, 0, sizeof ver);
    ver.size = sizeof ver;
#if DGL_API_VERSION < 0x0107                    /* (since 1.7 DOS-GL has a window on any desktop: its software
                                                   renderer's where the card's engine can't draw one) */
    if (glos_version(&ver) != 0 || !(ver.machine & GLOS_MACHINE_GLWIN))
        return 3;                               /* (DOS-GL would take the whole screen, as a DOS program does) */
#else
    if (glos_version(&ver) != 0)
        return 2;
#endif
    memset(&scr, 0, sizeof scr);
    scr.size = sizeof scr;
    glos_screen(&scr);
    *w = preview ? atoi(argv[2]) : scr.w;
    *h = preview ? atoi(argv[3]) : scr.h;
    if (*w < 32 || *h < 24)
        return 3;
    putenv("DGL_TEXHEAP_KB=64");                /* (no textures: the card's memory is for the screen's buffers) */
    memset(&cfg, 0, sizeof cfg);
    cfg.width = *w, cfg.height = *h;
    cfg.color_bits = 16;
    cfg.depth_bits = depth ? 16 : 0;
    cfg.double_buffer = 1;
    cfg.vsync = 1;
#ifdef DGL_GLOS_HIDDEN                          /* (a DOS-GL with hints: its window shown by us, full-screen at once) */
    dglGlosHints(DGL_GLOS_HIDDEN | DGL_GLOS_BARE);
#endif
    if (dglInit(&cfg) != 0)
        return 4;
    if (!(win = dglGetGlosWindow())) {          /* (it took the screen for itself after all: not as a saver) */
        dglShutdown();
        return 3;
    }
    if (!preview) {
        glos_set(win, GLOS_PROP_WIN_TOPMOST, 1);
        glos_show(win, GLOS_WS_FULL);
    }
#ifdef DGL_GLOS_HIDDEN
    else
        glos_show_quiet(win, GLOS_WS_NORMAL);   /* (the Control Panel takes it in) */
#endif
    glViewport(0, 0, *w, *h);
    {                                           /* [saver] speed: how fast the saver's clock goes */
        char v[16];
        int set = 0;
        v[0] = 0;
        if (glos_setting_get("saver", "speed", v, sizeof v, &set) == 0)
            saver_speed = !strcmp(v, "slow") ? 50 : !strcmp(v, "fast") ? 200 : 100;
    }
    saver_last = glos_ticks();
    return 0;
}

/* The milliseconds this frame stands for (100 at most: held up is no leap), by [saver] speed's clock. */
static inline unsigned saver_ms(void) { return saver_step * saver_speed / 100; }

/* The frame shown; 1 to go on, 0 when GLOS has said to end or the window was closed. */
static inline int saver_shown(void)
{
    static unsigned char evbuf[GLOS_EV_MAX];
    const struct glos_ev *ev = (const struct glos_ev *)evbuf;
    unsigned now;
    int on = 1;
    dglSwapBuffers();
    while (glos_event(evbuf, sizeof evbuf, 0, 0) > 0)
        if (ev->kind == GLOS_EV_END || ev->kind == GLOS_EV_CLOSE)
            on = 0;
    now = glos_ticks();
    saver_step = now - saver_last > 100 ? 100 : now - saver_last;
    saver_last = now;
    return on;
}

/* A box from (x0, y0, z0) to (x1, y1, z1), between glBegin(GL_TRIANGLES) and glEnd(): each side the colour c
   times how it is lit by the way it faces (from above and a little to the right and the front). */
static inline void saver_box(float x0, float y0, float z0, float x1, float y1, float z1, const float *c)
{
    static const float lit[6] = { 0.78f, 0.62f, 1.00f, 0.38f, 0.88f, 0.50f };  /* +x -x +y -y +z -z */
#define SAVER_SIDE(k, ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz) \
    glColor3f(c[0] * lit[k], c[1] * lit[k], c[2] * lit[k]); \
    glVertex3f(ax, ay, az), glVertex3f(bx, by, bz), glVertex3f(cx, cy, cz); \
    glVertex3f(ax, ay, az), glVertex3f(cx, cy, cz), glVertex3f(dx, dy, dz);
    SAVER_SIDE(0, x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1)
    SAVER_SIDE(1, x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0)
    SAVER_SIDE(2, x0, y1, z0, x0, y1, z1, x1, y1, z1, x1, y1, z0)
    SAVER_SIDE(3, x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1)
    SAVER_SIDE(4, x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1)
    SAVER_SIDE(5, x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0)
#undef SAVER_SIDE
}

static inline int saver_done(void)
{
    dglShutdown();
    return 0;
}

#endif
