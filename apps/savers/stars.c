/* STARS.SCR: a screen saver, a flight through stars (phase D, docs/v1/phase-d.md D6).
 *
 * A saver is a GLOS app that draws with DOS-GL in a window (saver.h: what every saver does the same; the kernel
 * starts it when the saver comes on and ends it at the first input, kernel/wm/saver.c). Each star is a small
 * square, larger and brighter the nearer it is, moved by the clock: the same speed at any frame rate. */
#include "saver.h"

#define STARS 220

static struct { float x, y, z; } star[STARS];

static void place(int i, int anywhere)
{
    star[i].x = (saver_rnd() - 0.5f) * 2.4f;
    star[i].y = (saver_rnd() - 0.5f) * 2.4f;
    star[i].z = anywhere ? 0.05f + saver_rnd() * 0.95f : 1.0f;
}

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
    int w, h, i, r = saver_begin(argc, argv, "Stars", 0, &w, &h);
    if (r)
        return r;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    for (i = 0; i < STARS; i++)
        place(i, 1);
    do
        frame((float)saver_ms() * 0.00022f, (float)w / (float)h);
    while (saver_shown());
    return saver_done();
}
