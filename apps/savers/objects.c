/* OBJECTS.SCR: a screen saver, solid things that tumble past (phase D, docs/v1/phase-d.md D6; saver.h).
 *
 * Fourteen blocks (eight or twenty-six by [saver] detail) of several shapes and colours come from far off towards the viewer, each turning about an axis
 * of its own and drifting a little sideways; one that has gone by starts again far off, somewhere else. Their
 * sides are lit by which way they face (saver_box), so the turning shows. */
#include "saver.h"

#define THINGS 26                       /* the most ([saver] detail = more); 14 as a rule, 8 for less */
static int nthings = 14;
#define FAR   (-46.0f)
#define NEAR  (-5.0f)

static struct {
    float x, y, z, vx, vy, vz;          /* where it is, and how it moves (a second) */
    float turn, spin, ax, ay;           /* its angle, degrees a second, and its axis (with 1 for z) */
    float sx, sy, sz;                   /* half its size, each way */
    int colour;
} th[THINGS];

static const float tint[7][3] = { { 0.90f, 0.25f, 0.20f }, { 0.20f, 0.60f, 0.90f }, { 0.25f, 0.80f, 0.35f },
                                  { 0.95f, 0.80f, 0.20f }, { 0.75f, 0.40f, 0.85f }, { 0.85f, 0.85f, 0.85f },
                                  { 0.95f, 0.55f, 0.15f } };

static void start(int i, int anywhere)
{
    float kind = saver_rnd();
    th[i].x = (saver_rnd() - 0.5f) * 26.0f;
    th[i].y = (saver_rnd() - 0.5f) * 18.0f;
    th[i].z = anywhere ? FAR + saver_rnd() * (NEAR - FAR) : FAR;
    th[i].vx = (saver_rnd() - 0.5f) * 1.6f;
    th[i].vy = (saver_rnd() - 0.5f) * 1.2f;
    th[i].vz = 4.0f + saver_rnd() * 5.0f;
    th[i].turn = saver_rnd() * 360.0f;
    th[i].spin = 40.0f + saver_rnd() * 110.0f;
    th[i].ax = saver_rnd() - 0.5f;
    th[i].ay = saver_rnd() - 0.5f;
    th[i].colour = (int)(saver_rnd() * 6.999f);
    if (kind < 0.4f)                            /* a cube, */
        th[i].sx = th[i].sy = th[i].sz = 0.7f + saver_rnd() * 0.5f;
    else if (kind < 0.7f)                       /* a slab, */
        th[i].sx = 1.3f, th[i].sy = 0.9f, th[i].sz = 0.18f;
    else                                        /* or a bar */
        th[i].sx = 0.28f, th[i].sy = 1.5f, th[i].sz = 0.28f;
}

static void frame(float s, float aspect)
{
    int i;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5f * aspect, 0.5f * aspect, -0.5f, 0.5f, 1.0f, 80.0f);
    glMatrixMode(GL_MODELVIEW);
    for (i = 0; i < nthings; i++) {
        th[i].x += th[i].vx * s, th[i].y += th[i].vy * s, th[i].z += th[i].vz * s;
        th[i].turn += th[i].spin * s;
        if (th[i].turn >= 360.0f)
            th[i].turn -= 360.0f;
        if (th[i].z > NEAR)
            start(i, 0);
        glLoadIdentity();
        glTranslatef(th[i].x, th[i].y, th[i].z);
        glRotatef(th[i].turn, th[i].ax, th[i].ay, 1.0f);
        glBegin(GL_TRIANGLES);
        saver_box(-th[i].sx, -th[i].sy, -th[i].sz, th[i].sx, th[i].sy, th[i].sz, tint[th[i].colour]);
        glEnd();
    }
}

int main(int argc, char **argv)
{
    int w, h, i, r = saver_begin(argc, argv, "Objects", 1, &w, &h);
    if (r)
        return r;
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    nthings = saver_amount(8, 14, THINGS);
    for (i = 0; i < nthings; i++)
        start(i, 1);
    do
        frame((float)saver_ms() * 0.001f, (float)w / (float)h);
    while (saver_shown());
    return saver_done();
}
