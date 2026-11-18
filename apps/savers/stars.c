/* STARS.SCR: a screen saver, a flight through stars (phase D, docs/v1/phase-d.md D6).
 *
 * A saver is a GLOS app that draws with DOS-GL in a window (RetroGPU's library, as built there). With no
 * arguments it is the saver itself: a context of the screen's size, its window made full-screen and topmost,
 * frames until GLOS ends it (the kernel starts it when the saver comes on and ends it at the first input:
 * kernel/wm/saver.c). With /P W H it is its own preview: a context W by H in an ordinary window, which the
 * Control Panel takes into its page.
 *
 * It ends at once, with a code that says why, where DOS-GL can't draw in a window (GLOS then shows its blank
 * saver): 2 no GLOS, 3 no windowed DOS-GL on this card or at this depth, 4 DOS-GL wouldn't start. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/dosgl.h>
#include <GL/gl.h>
#include <glos/glos.h>

#define STARS 220

static struct { float x, y, z; } star[STARS];
static unsigned seed = 0x1234567u;

static float rnd(void)                  /* 0 to 1 */
{
    seed = seed * 1103515245u + 12345u;
    return (float)(seed >> 8 & 0xFFFF) / 65535.0f;
}

static void place(int i, int anywhere)
{
    star[i].x = (rnd() - 0.5f) * 2.4f;
    star[i].y = (rnd() - 0.5f) * 2.4f;
    star[i].z = anywhere ? 0.05f + rnd() * 0.95f : 1.0f;
}

/* A frame: each star a small square, larger and brighter the nearer it is. */
static void frame(float step, float aspect)
{
    int i;
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    for (i = 0; i < STARS; i++) {
        float x, y, s, c;
        star[i].z -= step;
        if (star[i].z < 0.02f)
            place(i, 0);
        x = star[i].x / star[i].z * 0.5f;
        y = star[i].y / star[i].z * 0.5f;
        if (x < -1.1f || x > 1.1f || y < -1.1f || y > 1.1f) {
            place(i, 0);
            continue;
        }
        s = 0.0016f / star[i].z + 0.0016f;
        if (s > 0.012f)
            s = 0.012f;
        c = 1.15f - star[i].z;
        if (c > 1.0f)
            c = 1.0f;
        glColor3f(c, c, c * 0.92f + 0.08f);
        glVertex2f(x - s, y - s * aspect);
        glVertex2f(x + s, y - s * aspect);
        glVertex2f(x + s, y + s * aspect);
        glVertex2f(x - s, y - s * aspect);
        glVertex2f(x + s, y + s * aspect);
        glVertex2f(x - s, y + s * aspect);
    }
    glEnd();
}

int main(int argc, char **argv)
{
    static unsigned char evbuf[GLOS_EV_MAX];
    struct glos_ev *ev = (struct glos_ev *)evbuf;
    struct glos_version ver;
    struct glos_screen scr;
    DGLConfig cfg;
    glos_handle win;
    unsigned last;
    int preview = argc >= 4 && (!strcmp(argv[1], "/P") || !strcmp(argv[1], "/p")), w, h, i, run = 1;

    if (glos_init("Stars", 0, 0) != 0)
        return 2;
    memset(&ver, 0, sizeof ver);
    ver.size = sizeof ver;
    if (glos_version(&ver) != 0 || !(ver.machine & GLOS_MACHINE_GLWIN))
        return 3;                               /* (DOS-GL would take the whole screen, as a DOS program does) */
    memset(&scr, 0, sizeof scr);
    scr.size = sizeof scr;
    glos_screen(&scr);
    w = preview ? atoi(argv[2]) : scr.w;
    h = preview ? atoi(argv[3]) : scr.h;
    if (w < 32 || h < 24)
        return 3;
    putenv("DGL_TEXHEAP_KB=64");                /* (no textures: the card's memory is for the screen's buffers) */
    memset(&cfg, 0, sizeof cfg);
    cfg.width = w, cfg.height = h;
    cfg.color_bits = 16;
    cfg.double_buffer = 1;
    cfg.vsync = 1;
#ifdef DGL_GLOS_HIDDEN                          /* (DOS-GL 1.6: its window shown by us, full-screen at once) */
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
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    for (i = 0; i < STARS; i++)
        place(i, 1);
    last = glos_ticks();
    while (run) {
        unsigned now = glos_ticks(), ms = now - last;
        last = now;
        if (ms > 100)
            ms = 100;                           /* (held up: no leap) */
        frame((float)ms * 0.00022f, (float)w / (float)h);
        dglSwapBuffers();
        while (glos_event(evbuf, sizeof evbuf, 0, 0) > 0)      /* GLOS's end, or the window's close: done */
            if (ev->kind == GLOS_EV_END || ev->kind == GLOS_EV_CLOSE)
                run = 0;
    }
    dglShutdown();
    return 0;
}
