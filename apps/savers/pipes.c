/* PIPES.SCR: a screen saver, pipes that grow through a box (phase D, docs/v1/phase-d.md D6; saver.h).
 *
 * A grid of cells; a pipe starts in a free one and goes on a cell at a time, straight more often than not, into
 * cells no pipe has, with a joint where it turns. One that can't go on, or is long enough, is left, and another
 * starts in another colour; when there are enough the whole is looked at for a moment and begun again. The box
 * turns slowly. Each stretch is a square pipe whose sides are lit by which way they face. */
#include "saver.h"

#define N       9                       /* cells a side */
#define MAXSEG  420
#define PIPES   7                       /* pipes to a picture */
#define LONGEST 90                      /* cells to a pipe */
#define GROW_MS 55                      /* a cell this often */
#define REST_MS 2500                    /* the finished picture is looked at this long */

struct seg {
    signed char a[3], b[3];             /* from cell a to cell b (a = b: a joint) */
    unsigned char colour;
};
static struct seg seg[MAXSEG];
static int nseg, npipes, len, at[3], dir = -1, colour, rest;
static unsigned char used[N][N][N];

static const float tint[6][3] = { { 0.90f, 0.25f, 0.20f }, { 0.20f, 0.60f, 0.90f }, { 0.25f, 0.80f, 0.35f },
                                  { 0.95f, 0.80f, 0.20f }, { 0.75f, 0.40f, 0.85f }, { 0.85f, 0.85f, 0.85f } };
static const signed char step[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };

static int pick(int n) { return (int)(saver_rnd() * (float)n * 0.9999f); }

static int free_at(const int *c, int d)
{
    int x = c[0] + step[d][0], y = c[1] + step[d][1], z = c[2] + step[d][2];
    return x >= 0 && x < N && y >= 0 && y < N && z >= 0 && z < N && !used[x][y][z];
}

static void add(const int *a, const int *b)
{
    int i;
    if (nseg == MAXSEG)
        return;
    for (i = 0; i < 3; i++)
        seg[nseg].a[i] = (signed char)a[i], seg[nseg].b[i] = (signed char)b[i];
    seg[nseg++].colour = (unsigned char)colour;
}

static void begin_again(void)
{
    memset(used, 0, sizeof used);
    nseg = npipes = 0;
    dir = -1;
    rest = 0;
}

/* A cell more: the pipe goes on, or a new one starts, or the picture is done. */
static void grow(void)
{
    int next[3], d, tries, i;
    if (dir < 0) {                              /* a new pipe, in a free cell */
        if (npipes == PIPES || nseg >= MAXSEG - 2) {
            rest = REST_MS;
            return;
        }
        for (tries = 0; tries < 60; tries++) {
            at[0] = pick(N), at[1] = pick(N), at[2] = pick(N);
            if (!used[at[0]][at[1]][at[2]])
                break;
        }
        if (tries == 60) {
            rest = REST_MS;
            return;
        }
        used[at[0]][at[1]][at[2]] = 1;
        colour = npipes++ % 6;
        dir = pick(6);
        len = 0;
        add(at, at);                            /* (its end: a joint) */
    }
    d = dir;
    if (!free_at(at, d) || saver_rnd() < 0.3f) {        /* a turn: any free way, the same one if it is the only one */
        int ways[6], n = 0;
        for (i = 0; i < 6; i++)
            if (free_at(at, i) && (i ^ 1) != dir)
                ways[n++] = i;
        if (!n) {
            add(at, at);
            dir = -1;                           /* nowhere to go: this pipe is done */
            return;
        }
        d = ways[pick(n)];
    }
    if (d != dir)
        add(at, at);                            /* a joint at the turn */
    for (i = 0; i < 3; i++)
        next[i] = at[i] + step[d][i];
    add(at, next);
    used[next[0]][next[1]][next[2]] = 1;
    for (i = 0; i < 3; i++)
        at[i] = next[i];
    dir = d;
    if (++len >= LONGEST) {
        add(at, at);
        dir = -1;
    }
}

static void frame(float turn, float aspect)
{
    const float r = 0.17f, j = 0.25f, mid = (float)(N - 1) * 0.5f;
    int i, k;
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5f * aspect, 0.5f * aspect, -0.5f, 0.5f, 1.0f, 60.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -15.5f);
    glRotatef(22.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(turn, 0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    for (i = 0; i < nseg; i++) {
        float lo[3], hi[3];
        int joint = !memcmp(seg[i].a, seg[i].b, 3);
        for (k = 0; k < 3; k++) {
            float a = (float)seg[i].a[k] - mid, b = (float)seg[i].b[k] - mid, w = joint ? j : r;
            lo[k] = (a < b ? a : b) - w;
            hi[k] = (a > b ? a : b) + w;
        }
        saver_box(lo[0], lo[1], lo[2], hi[0], hi[1], hi[2], tint[seg[i].colour]);
    }
    glEnd();
}

int main(int argc, char **argv)
{
    int w, h, r = saver_begin(argc, argv, "Pipes", 1, &w, &h);
    unsigned owed = 0;
    float turn = 0.0f;
    if (r)
        return r;
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    begin_again();
    do {
        unsigned ms = saver_ms();
        turn += (float)ms * 0.012f;
        if (turn >= 360.0f)
            turn -= 360.0f;
        if (rest > 0) {                         /* the finished picture, for a moment; then another */
            if ((rest -= (int)ms) <= 0)
                begin_again();
        } else {
            for (owed += ms; owed >= GROW_MS && rest <= 0; owed -= GROW_MS)
                grow();
        }
        frame(turn, (float)w / (float)h);
    } while (saver_shown());
    return saver_done();
}
