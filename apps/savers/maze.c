/* MAZE.SCR: a screen saver, a walk through a maze (phase D, docs/v1/phase-d.md D6; saver.h).
 *
 * A maze of ten cells by ten is made by carving passages from a corner (every cell is reached, and there is one
 * way between any two). The viewer walks it with a hand on the right-hand wall: a cell forward at a time,
 * turning where the wall turns, back out of the dead ends. After a good while the walls change colour and a new
 * maze is made. The walls are thin boxes lit by which way they face (saver_box), between a floor and a ceiling. */
#include "saver.h"

#define W 10
#define H 10
#define STEP_MS 620                     /* a cell's walk */
#define TURN_MS 420                     /* a quarter turn */
#define WALKS   420                     /* cells walked before another maze */

static unsigned char north[W][H + 1];   /* a wall along the north side of cell x, y (y = H: the south edge) */
static unsigned char west[W + 1][H];    /* ... along its west side (x = W: the east edge) */
static unsigned char seen[W][H];
static const int dx[4] = { 0, 1, 0, -1 }, dy[4] = { -1, 0, 1, 0 };     /* north, east, south, west */

static int open_way(int x, int y, int d)
{
    return d == 0 ? !north[x][y] : d == 2 ? !north[x][y + 1] : d == 1 ? !west[x + 1][y] : !west[x][y];
}

static void carve(int x, int y)
{
    int order[4] = { 0, 1, 2, 3 }, i;
    seen[x][y] = 1;
    for (i = 3; i > 0; i--) {                   /* the four ways, in an order of chance */
        int j = (int)(saver_rnd() * (float)(i + 1) * 0.9999f), t = order[i];
        order[i] = order[j], order[j] = t;
    }
    for (i = 0; i < 4; i++) {
        int d = order[i], nx = x + dx[d], ny = y + dy[d];
        if (nx < 0 || ny < 0 || nx >= W || ny >= H || seen[nx][ny])
            continue;
        if (d == 0)
            north[x][y] = 0;
        else if (d == 2)
            north[x][y + 1] = 0;
        else if (d == 1)
            west[x + 1][y] = 0;
        else
            west[x][y] = 0;
        carve(nx, ny);
    }
}

static void make(void)
{
    memset(north, 1, sizeof north);
    memset(west, 1, sizeof west);
    memset(seen, 0, sizeof seen);
    carve(0, 0);
}

static void frame(float px, float pz, float heading, const float *wall, float aspect)
{
    static const float floor_c[3] = { 0.30f, 0.34f, 0.30f }, roof_c[3] = { 0.16f, 0.17f, 0.22f };
    const float t = 0.045f;                     /* half a wall's thickness */
    int x, y;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.14f * aspect, 0.14f * aspect, -0.14f, 0.14f, 0.2f, 30.0f);     /* (nearer than any wall is to the walk) */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(heading, 0.0f, 1.0f, 0.0f);
    glTranslatef(-px, -0.5f, -pz);
    glBegin(GL_TRIANGLES);
    saver_box(0.0f, -0.05f, 0.0f, (float)W, 0.0f, (float)H, floor_c);
    saver_box(0.0f, 1.0f, 0.0f, (float)W, 1.05f, (float)H, roof_c);
    for (x = 0; x < W; x++)
        for (y = 0; y <= H; y++)
            if (north[x][y])
                saver_box((float)x - t, 0.0f, (float)y - t, (float)x + 1.0f + t, 1.0f, (float)y + t, wall);
    for (x = 0; x <= W; x++)
        for (y = 0; y < H; y++)
            if (west[x][y])
                saver_box((float)x - t, 0.0f, (float)y - t, (float)x + t, 1.0f, (float)y + 1.0f + t, wall);
    glEnd();
}

int main(int argc, char **argv)
{
    static const float tints[4][3] = { { 0.80f, 0.42f, 0.30f }, { 0.35f, 0.55f, 0.80f }, { 0.55f, 0.72f, 0.40f },
                                       { 0.78f, 0.70f, 0.45f } };
    static const int tryd[4] = { 1, 0, 3, 2 };  /* the right hand's order: right, ahead, left, back */
    int w, h, r = saver_begin(argc, argv, "Maze", 1, &w, &h);
    int cx = 0, cy = 0, d = 2, nd = 2, walked = 0, tint = 0, turning = 0, arrived = 1, k;
    float at = 0.0f, from = 180.0f, to = 180.0f;        /* a walk's or a turn's part done; the headings turned between */
    if (r)
        return r;
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    make();
    do {
        float ms = (float)saver_ms(), px, pz, heading;
        if (arrived) {                          /* at a cell's middle: the way on from here */
            arrived = 0;
            for (k = 0; k < 4; k++) {
                nd = (d + tryd[k]) % 4;
                if (open_way(cx, cy, nd))
                    break;
            }
            if (k < 4 && tryd[k]) {             /* (a quarter to the right or the left, or about by the right) */
                turning = tryd[k] == 2 ? 2 : 1;
                to = from + (tryd[k] == 1 ? 90.0f : tryd[k] == 3 ? -90.0f : 180.0f);
                at = 0.0f;
            }
        }
        if (turning) {
            at += ms / (float)(TURN_MS * turning);      /* (a turn about takes two quarters' time) */
            if (at >= 1.0f) {
                at = 0.0f;
                turning = 0;
                d = nd;
                from = to = (float)d * 90.0f;
            }
        } else {
            at += ms / (float)STEP_MS;
            if (at >= 1.0f) {                   /* a cell on */
                at = 0.0f;
                cx += dx[d], cy += dy[d];
                arrived = 1;
                if (++walked >= WALKS) {        /* another maze, from its corner, in another colour */
                    walked = 0;
                    tint = (tint + 1) % 4;
                    make();
                    cx = cy = 0;
                    d = 2;
                    from = to = 180.0f;
                }
            }
        }
        /* (A heading of 0 looks north, the picture's -z; a quarter to the right is 90 more.) */
        heading = turning ? from + (to - from) * at : from;
        px = (float)cx + 0.5f + (turning ? 0.0f : (float)dx[d] * at);
        pz = (float)cy + 0.5f + (turning ? 0.0f : (float)dy[d] * at);
        frame(px, pz, heading, tints[tint], (float)w / (float)h);
    } while (saver_shown());
    return saver_done();
}
