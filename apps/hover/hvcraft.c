/* HVCRAFT.EXE: the hovercraft game (GLOS v1, phase F; docs/v1/apps/hovercraft.md), its first playable part.
 * Its name, Cushion (the air a hovercraft rides on), is the owner's choice (2026-11-21). GLOS's own game: its craft, arenas, rules
 * and look were made for it; nothing of any other game is in it.
 *
 * Here: the game's six arenas (built in: apps/shared/hvarenas.c) and the user's own (text files in the folder
 * ARENAS beside the program, as hvarena.h says they are written; the lobby is to come),
 * the player's craft with its side's and the other side's driven by the computer, the four kinds of game
 * (apps/shared/hvgame.c is their truth, thirty steps a second), and the picture, drawn with DOS-GL (RetroGPU's
 * gl) in a window of the desktop: the card's engine where DOS-GL has one for a window, its software renderer
 * elsewhere. Plain coloured faces, lit by the way they face.
 *
 * Keys: Up and Down (or W and S) the fans forwards and back; Left and Right (or A and D) turn; Q and E the side
 * fans; Shift the boost; Space sets the craft down. 1 to 4 choose the game (flags, beacons, the lamp, laps) and
 * begin it again; Tab (or N) goes to the next arena; Esc ends. Over the scene: the score, the boost's tank, and a small map of the arena (the craft,
 * the flags, beacons, the lamp or the next buoy).
 *
 * Over the network (apps/hover/hvlink.c; the messages: apps/shared/hvnet.h): O opens the game to the other GLOS
 * PCs of the network (one of the game's six arenas; it begins again), and closes it; J looks for an open game
 * and joins the first that has a craft free, and leaves it. No addresses: a broadcast finds the games. The
 * opener's PC keeps the game; the craft nobody has are the computer's; a PC that leaves or goes quiet gives its
 * craft back to the computer; when the opener leaves, its joiners are alone again. The window's title says how
 * things stand. The game and the arena are the opener's to choose, before it opens.
 *
 * HVCRAFT -demo [-kind 0..3] [-until STEP] is the suite's: every craft the computer's, the game's state said at
 * a step ("HVCRAFT at ..."), which is the same on every PC whatever the pictures cost. With -open or -join
 * [-name NAME] the game is opened or one is joined at once, and both say the game at every 150th step as they
 * have it ("HVCRAFT net at ..."): the same line on both PCs. A joiner's -until counts steps from the first it is
 * told.
 *
 * Sound (apps/hover/hvsound.c), made by rule: the fans by their work, the air by the speed, the skirt's scrape,
 * bumps, the water, a flag or the lamp taken and lost, a point, a beacon.
 *
 * HVCRAFT.CFG beside the program keeps the last game and arena, a name, and whether the game opens to the
 * network whenever it starts.
 *
 * A pad drives it too (pad_held: the left stick, the right stick or the shoulder buttons, A boosts, B sets down).
 *
 * Not yet (the plan's rest): the lobby (the games found as a list, names, the craft's colour); a joiner's own
 * craft moved at once by its own controls; the mouse; full screen; the computer's strengths chosen; names over the craft; an arrow to the flag or the lamp. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dir.h>

#include <GL/dosgl.h>
#include <GL/gl.h>
#include <glos/glos.h>

#include "../shared/hvarenas.h"
#include "../shared/hvbasin.h"
#include "../shared/hvgame.h"
#include "hvlink.h"
#include "hvsound.h"

#define GAME_NAME "Cushion"             /* (the owner's choice, 2026-11-21) */

static struct hv_arena arena;
static struct hv_game alone;            /* the game played alone */
static struct hv_game *G = &alone;      /* the game shown: that one, or a network game (the opener's own; a joiner's
                                           view of it) */
static int me;                          /* the player's craft in it */
static int which;                       /* the arena: the game's six, then the user's own */

/* The arenas: the six that come with the game, then the user's own: text files (hvarena.h says how they are
   written) in the folder ARENAS beside the program. One that the reader refuses is left out, and why is said on
   the program's output (a terminal's, if it was started from one). */
#define USER_ARENAS 24
static char *user_arena[USER_ARENAS];
static int nuser;

static const char *arena_text(int k) { return k < HV_ARENAS ? hv_arenas[k] : user_arena[k - HV_ARENAS]; }

static void user_arenas(const char *argv0)
{
    static struct hv_arena trial;
    char dir[260], path[540], why[160];
    struct ffblk ff;
    const char *cut = argv0, *p;
    int done;
    for (p = argv0; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':')
            cut = p + 1;
    snprintf(dir, sizeof dir, "%.*sARENAS", (int)(cut - argv0), argv0);
    snprintf(path, sizeof path, "%s/*.TXT", dir);
    for (done = findfirst(path, &ff, 0); !done && nuser < USER_ARENAS; done = findnext(&ff)) {
        FILE *f;
        char *text;
        long n;
        snprintf(path, sizeof path, "%s/%s", dir, ff.ff_name);
        if (!(f = fopen(path, "rb")))
            continue;
        text = malloc(8192);
        n = text ? (long)fread(text, 1, 8191, f) : 0;
        fclose(f);
        if (!text)
            continue;
        text[n] = 0;
        if (hv_arena_read(&trial, text, (size_t)n, why, sizeof why) == 0) {
            user_arena[nuser++] = text;
            printf("HVCRAFT arena %s: %s\n", ff.ff_name, trial.name);
        } else {
            printf("HVCRAFT arena %s refused: %s\n", ff.ff_name, why);
            free(text);
        }
    }
    fflush(stdout);
}
static const float SIDE[2][3] = { { 0.92f, 0.48f, 0.16f }, { 0.16f, 0.60f, 0.82f } };

/* A box between two corners, each side the colour times how it is lit by the way it faces. */
static void box(float x0, float y0, float z0, float x1, float y1, float z1, const float *c)
{
    static const float lit[6] = { 0.78f, 0.62f, 1.00f, 0.38f, 0.88f, 0.50f };  /* +x -x +y -y +z -z */
#define FACE(k, ax, ay, az, bx, by, bz, cx, cy, cz, dx, dy, dz) \
    glColor3f(c[0] * lit[k], c[1] * lit[k], c[2] * lit[k]); \
    glVertex3f(ax, ay, az), glVertex3f(bx, by, bz), glVertex3f(cx, cy, cz); \
    glVertex3f(ax, ay, az), glVertex3f(cx, cy, cz), glVertex3f(dx, dy, dz);
    FACE(0, x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1)
    FACE(1, x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0)
    FACE(2, x0, y1, z0, x0, y1, z1, x1, y1, z1, x1, y1, z0)
    FACE(3, x0, y0, z0, x1, y0, z0, x1, y0, z1, x0, y0, z1)
    FACE(4, x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1)
    FACE(5, x0, y0, z0, x0, y1, z0, x1, y1, z0, x1, y0, z0)
#undef FACE
}

#define LEVEL 0.45f                     /* a level's height, in squares */

static void draw_arena(void)
{
    static const float wall[3] = { 0.56f, 0.36f, 0.26f }, floor_a[3] = { 0.44f, 0.46f, 0.48f }, floor_b[3] = { 0.40f, 0.42f, 0.44f },
                       quay[3] = { 0.62f, 0.60f, 0.54f }, water[3] = { 0.10f, 0.30f, 0.55f }, ramp[3] = { 0.70f, 0.66f, 0.40f },
                       white[3] = { 0.95f, 0.95f, 0.95f }, lampc[3] = { 0.98f, 0.86f, 0.20f }, buoy[3] = { 0.85f, 0.15f, 0.15f };
    int x, y, k;
    glBegin(GL_TRIANGLES);
    for (y = 0; y < arena.h; y++)
        for (x = 0; x < arena.w; x++) {
            const struct hv_square *s = &arena.sq[y][x];
            float fx = (float)x, fz = (float)y, top = (float)s->level * LEVEL;
            if (s->kind == HV_WALL) {
                box(fx, 0.0f, fz, fx + 1.0f, 1.1f, fz + 1.0f, wall);
            } else if (s->kind == HV_WATER) {
                box(fx, -0.40f, fz, fx + 1.0f, -0.30f, fz + 1.0f, water);
            } else if (s->kind == HV_RAMP) {    /* a wedge: its low edge at its level, its high edge a level up */
                float lo = top, hi = top + LEVEL, a = lo, b = lo, c = lo, d = lo;      /* corners: (x,z) (x+1,z) (x+1,z+1) (x,z+1) */
                if (s->way == HV_N)
                    a = b = hi;
                else if (s->way == HV_E)
                    b = c = hi;
                else if (s->way == HV_S)
                    c = d = hi;
                else
                    a = d = hi;
                glColor3f(ramp[0], ramp[1], ramp[2]);
                glVertex3f(fx, a, fz), glVertex3f(fx, d, fz + 1.0f), glVertex3f(fx + 1.0f, c, fz + 1.0f);
                glVertex3f(fx, a, fz), glVertex3f(fx + 1.0f, c, fz + 1.0f), glVertex3f(fx + 1.0f, b, fz);
            } else {
                const float *c = s->level ? quay : (x + y) & 1 ? floor_a : floor_b;
                if (s->mark == 'A' || s->mark == 'B') {
                    float tint[3];
                    for (k = 0; k < 3; k++)
                        tint[k] = 0.5f * c[k] + 0.5f * SIDE[s->mark - 'A'][k];
                    box(fx, top - 0.05f, fz, fx + 1.0f, top, fz + 1.0f, tint);
                } else {
                    box(fx, s->level ? 0.0f : -0.05f, fz, fx + 1.0f, top, fz + 1.0f, c);
                }
            }
        }
    /* what stands in it: the beacons in their side's colour, the lamp, a lap's buoys, the flags */
    for (k = 0; k < arena.nbeacon; k++) {
        float fx = (float)arena.beacon[k].x + 0.5f, fz = (float)arena.beacon[k].y + 0.5f;
        box(fx - 0.06f, 0.0f, fz - 0.06f, fx + 0.06f, 0.9f, fz + 0.06f, G->beacon[k] < 0 ? white : SIDE[G->beacon[k]]);
        box(fx - 0.16f, 0.9f, fz - 0.16f, fx + 0.16f, 1.1f, fz + 0.16f, G->beacon[k] < 0 ? white : SIDE[G->beacon[k]]);
    }
    if (G->kind == HV_LAMP) {
        float fx = (float)G->lamp_x / 256.0f, fz = (float)G->lamp_y / 256.0f, up = G->lamp_holder >= 0 ? 0.55f : 0.0f;
        box(fx - 0.10f, up, fz - 0.10f, fx + 0.10f, up + 0.35f, fz + 0.10f, lampc);
    }
    if (G->kind == HV_LAPS)
        for (k = 0; k < arena.nbuoy; k++) {
            float fx = (float)arena.buoy[k].x + 0.5f, fz = (float)arena.buoy[k].y + 0.5f;
            box(fx - 0.10f, 0.0f, fz - 0.10f, fx + 0.10f, k == G->c[me].buoy ? 0.9f : 0.4f, fz + 0.10f, k == G->c[me].buoy ? lampc : buoy);
        }
    if (G->kind == HV_FLAGS)
        for (k = 0; k < 2; k++) {
            float fx = (float)G->flag[k].x / 256.0f, fz = (float)G->flag[k].y / 256.0f, up = G->flag[k].holder >= 0 ? 0.35f : 0.0f;
            box(fx - 0.03f, up, fz - 0.03f, fx + 0.03f, up + 1.0f, fz + 0.03f, white);
            box(fx + 0.03f, up + 0.70f, fz - 0.02f, fx + 0.40f, up + 1.0f, fz + 0.02f, SIDE[k]);
        }
    glEnd();
}

static void draw_craft(int i)
{
    static const float dark[3] = { 0.20f, 0.20f, 0.22f }, glass[3] = { 0.80f, 0.90f, 0.95f }, white[3] = { 0.95f, 0.95f, 0.95f };
    const struct hv_craft *c = &G->c[i];
    float up = (float)hv_craft_level(G, i) / 256.0f * LEVEL + (c->down ? 0.0f : 0.10f);
    if (c->wet)
        return;
    glPushMatrix();
    glTranslatef((float)c->x / 256.0f, up, (float)c->y / 256.0f);
    glRotatef(-(float)c->heading * 360.0f / 1024.0f, 0.0f, 1.0f, 0.0f);         /* (its nose towards -z at heading 0) */
    glBegin(GL_TRIANGLES);
    box(-0.26f, 0.00f, -0.30f, 0.26f, 0.08f, 0.30f, dark);                      /* the skirt */
    box(-0.22f, 0.08f, -0.28f, 0.22f, 0.20f, 0.26f, SIDE[c->side]);            /* the hull */
    box(-0.10f, 0.20f, -0.16f, 0.10f, 0.30f, 0.04f, i == me ? white : glass);    /* the cabin (the player's: white) */
    box(-0.16f, 0.20f, 0.12f, 0.16f, 0.36f, 0.24f, dark);                       /* the fan's housing, at the back */
    glEnd();
    glPopMatrix();
}

static void frame(float aspect)
{
    const struct hv_craft *my = &G->c[me];
    float heading = (float)my->heading * 360.0f / 1024.0f;
    float s = (float)hv_sin(my->heading) / 16384.0f, c = (float)hv_cos(my->heading) / 16384.0f;
    float px = (float)my->x / 256.0f, pz = (float)my->y / 256.0f, py = (float)hv_craft_level(G, me) / 256.0f * LEVEL;
    float back = 0.5f, d;
    int i;
    /* the eye: behind the craft and above it, but never the far side of a wall from it: as far back as the
       squares behind are open, to 2.3 squares */
    for (d = 0.75f; d <= 2.31f; d += 0.25f) {
        int qx = (int)(px - s * d), qz = (int)(pz + c * d);
        if (qx < 0 || qz < 0 || qx >= arena.w || qz >= arena.h || arena.sq[qz][qx].kind == HV_WALL)
            break;
        back = d;
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.11f * aspect, 0.11f * aspect, -0.11f, 0.11f, 0.2f, 60.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(24.0f + (2.3f - back) * 12.0f, 1.0f, 0.0f, 0.0f);         /* looking down a little: more, the nearer the eye */
    glRotatef(heading, 0.0f, 1.0f, 0.0f);
    glTranslatef(-(px - s * back), -(py + 0.75f + back * 0.26f), -(pz + c * back));
    draw_arena();
    for (i = 0; i < G->ncraft; i++)
        draw_craft(i);
}

/* ---- over the scene: the score, the boost's tank, a small map. Flat, in the window's own pixels. */

static void flat(float x, float y, float w, float h, const float *c)
{
    glColor3f(c[0], c[1], c[2]);
    glVertex3f(x, y, 0.0f), glVertex3f(x + w, y, 0.0f), glVertex3f(x + w, y + h, 0.0f);
    glVertex3f(x, y, 0.0f), glVertex3f(x + w, y + h, 0.0f), glVertex3f(x, y + h, 0.0f);
}

/* A number in the seven strokes of a clock's figures, each figure 14 by 24 pixels; the x after it. */
static float number(float x, float y, long v, const float *c)
{
    /* the strokes lit for each figure: top, upper left, upper right, middle, lower left, lower right, bottom */
    static const unsigned char lit[10] = { 0x77, 0x24, 0x5D, 0x6D, 0x2E, 0x6B, 0x7B, 0x25, 0x7F, 0x6F };
    char text[12];
    int i;
    snprintf(text, sizeof text, "%ld", v < 0 ? 0 : v);
    for (i = 0; text[i]; i++, x += 18.0f) {
        unsigned m = lit[text[i] - '0'];
        if (m & 0x01) flat(x, y, 14.0f, 3.0f, c);
        if (m & 0x02) flat(x, y, 3.0f, 12.0f, c);
        if (m & 0x04) flat(x + 11.0f, y, 3.0f, 12.0f, c);
        if (m & 0x08) flat(x, y + 10.0f, 14.0f, 3.0f, c);
        if (m & 0x10) flat(x, y + 12.0f, 3.0f, 12.0f, c);
        if (m & 0x20) flat(x + 11.0f, y + 12.0f, 3.0f, 12.0f, c);
        if (m & 0x40) flat(x, y + 21.0f, 14.0f, 3.0f, c);
    }
    return x;
}

static void overlay(int w, int h)
{
    static const float dark[3] = { 0.10f, 0.10f, 0.12f }, light[3] = { 0.80f, 0.80f, 0.82f }, mid[3] = { 0.55f, 0.55f, 0.58f },
                       water[3] = { 0.15f, 0.35f, 0.65f }, white[3] = { 1.0f, 1.0f, 1.0f }, tank[3] = { 0.98f, 0.86f, 0.20f };
    const struct hv_craft *own = &G->c[me];
    float x, mx, my, cell = 4.0f;
    int i, sx, sy;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)w, (double)h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    glBegin(GL_TRIANGLES);
    /* the score: the two sides', or the player's own */
    flat(8.0f, 8.0f, G->kind <= HV_BEACONS ? 104.0f : 52.0f, 34.0f, dark);
    if (G->kind <= HV_BEACONS) {
        x = number(14.0f, 13.0f, G->score[0], SIDE[0]);
        flat(x + 2.0f, 20.0f, 4.0f, 4.0f, light), flat(x + 2.0f, 28.0f, 4.0f, 4.0f, light);
        number(x + 12.0f, 13.0f, G->score[1], SIDE[1]);
    } else {
        number(14.0f, 13.0f, own->score, white);
    }
    /* the boost's tank */
    flat(8.0f, (float)h - 20.0f, 104.0f, 12.0f, dark);
    flat(10.0f, (float)h - 18.0f, 100.0f * (float)own->tank / (float)HV_TANK, 8.0f, tank);
    /* the map: the arena from above, its top the grid's */
    mx = (float)w - 8.0f - cell * (float)arena.w, my = 8.0f;
    flat(mx - 2.0f, my - 2.0f, cell * (float)arena.w + 4.0f, cell * (float)arena.h + 4.0f, dark);
    for (sy = 0; sy < arena.h; sy++)
        for (sx = 0; sx < arena.w; sx++) {
            const struct hv_square *q = &arena.sq[sy][sx];
            if (q->kind != HV_WALL)
                flat(mx + cell * (float)sx, my + cell * (float)sy, cell, cell,
                     q->kind == HV_WATER ? water : q->mark == 'A' || q->mark == 'B' ? SIDE[q->mark - 'A'] : q->level || q->kind == HV_RAMP ? light : mid);
        }
    for (i = 0; i < arena.nbeacon && G->kind == HV_BEACONS; i++)
        flat(mx + cell * (float)arena.beacon[i].x, my + cell * (float)arena.beacon[i].y, cell, cell, G->beacon[i] < 0 ? white : SIDE[G->beacon[i]]);
    if (G->kind == HV_LAMP)
        flat(mx + (float)G->lamp_x / 256.0f * cell - 2.0f, my + (float)G->lamp_y / 256.0f * cell - 2.0f, 5.0f, 5.0f, tank);
    if (G->kind == HV_LAPS)
        flat(mx + cell * (float)arena.buoy[own->buoy].x, my + cell * (float)arena.buoy[own->buoy].y, cell, cell, tank);
    for (i = 0; i < 2 && G->kind == HV_FLAGS; i++)
        flat(mx + (float)G->flag[i].x / 256.0f * cell - 1.0f, my + (float)G->flag[i].y / 256.0f * cell - 4.0f, 2.0f, 6.0f, white);
    for (i = G->ncraft - 1; i >= 0; i--)
        if (!G->c[i].wet)
            flat(mx + (float)G->c[i].x / 256.0f * cell - 1.5f, my + (float)G->c[i].y / 256.0f * cell - 1.5f, 3.0f, 3.0f, i != me ? SIDE[G->c[i].side] : white);
    glEnd();
    glEnable(GL_DEPTH_TEST);
}

/* A pad (the first plugged in): the left stick turns the craft and drives the fans forwards and back; the right
   stick, or the shoulder buttons, the side fans; the first button boosts, the second sets the craft down. (An Xbox
   pad's A, B, LB and RB.) */
static unsigned pad_held(void)
{
    struct glos_pads p;
    unsigned c = 0;
    int i;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    if (glos_pads(&p, 0) != 0)
        return 0;
    for (i = 0; i < GLOS_PADS && !p.pad[i].present; i++)
        ;
    if (i == GLOS_PADS)
        return 0;
    if (p.pad[i].lx < -40)
        c |= HV_C_LEFT;
    if (p.pad[i].lx > 40)
        c |= HV_C_RIGHT;
    if (p.pad[i].ly < -40)
        c |= HV_C_FWD;
    if (p.pad[i].ly > 40)
        c |= HV_C_BACK;
    if (p.pad[i].rx < -40 || (p.pad[i].buttons & 0x10))
        c |= HV_C_SLIDE_L;
    if (p.pad[i].rx > 40 || (p.pad[i].buttons & 0x20))
        c |= HV_C_SLIDE_R;
    if (p.pad[i].buttons & 1)
        c |= HV_C_BOOST;
    if (p.pad[i].buttons & 2)
        c |= HV_C_DOWN;
    return c;
}

static unsigned keys_held(void)
{
    struct glos_keys k;
    unsigned c = 0;
    memset(&k, 0, sizeof k);
    k.size = sizeof k;
    if (glos_keys(&k) != 0)
        return 0;
    if (GLOS_KEY_DOWN(&k, 0x148) || GLOS_KEY_DOWN(&k, 0x11))    /* Up, W */
        c |= HV_C_FWD;
    if (GLOS_KEY_DOWN(&k, 0x150) || GLOS_KEY_DOWN(&k, 0x1F))    /* Down, S */
        c |= HV_C_BACK;
    if (GLOS_KEY_DOWN(&k, 0x14B) || GLOS_KEY_DOWN(&k, 0x1E))    /* Left, A */
        c |= HV_C_LEFT;
    if (GLOS_KEY_DOWN(&k, 0x14D) || GLOS_KEY_DOWN(&k, 0x20))    /* Right, D */
        c |= HV_C_RIGHT;
    if (GLOS_KEY_DOWN(&k, 0x10))                                /* Q */
        c |= HV_C_SLIDE_L;
    if (GLOS_KEY_DOWN(&k, 0x12))                                /* E */
        c |= HV_C_SLIDE_R;
    if (k.mods & GLOS_MOD_SHIFT)
        c |= HV_C_BOOST;
    if (GLOS_KEY_DOWN(&k, 0x39))                                /* Space */
        c |= HV_C_DOWN;
    return c | pad_held();
}

static void title(glos_handle win)
{
    static const char *const kinds[4] = { "Flags", "Beacons", "The lamp", "Laps" };
    char t[128], lead[56];
    int m = hvlink_mode();
    if (m == HVL_OPEN)
        snprintf(lead, sizeof lead, "%s (open, %d joined)", GAME_NAME, hvlink_seats());
    else if (m == HVL_LOOKING)
        snprintf(lead, sizeof lead, "%s (looking for a game)", GAME_NAME);
    else if (m == HVL_JOINED)
        snprintf(lead, sizeof lead, "%s (in %.16s)", GAME_NAME, hvlink_host());
    else
        snprintf(lead, sizeof lead, "%s", GAME_NAME);
    if (G->kind == HV_FLAGS || G->kind == HV_BEACONS)
        snprintf(t, sizeof t, "%s: %s, %s %d : %d", lead, G->a->name, kinds[G->kind & 3], G->score[0], G->score[1]);
    else
        snprintf(t, sizeof t, "%s: %s, %s %ld", lead, G->a->name, kinds[G->kind & 3], G->c[me].score);
    glos_set_text(win, GLOS_PROP_WIN_TITLE, t);
}

/* What the game remembers, in HVCRAFT.CFG beside the program, lines of "key = value": the kind of game and the
   arena last played (written when the game ends), the player's name on the network (the PC's own if none is
   written), and "network = open" or "network = join": the game opens to the network, or looks for a game,
   whenever it starts (for a PC that is the one others join; written by hand until the lobby sets it). */
static const char *const kind_words[4] = { "flags", "beacons", "lamp", "laps" };
static char cfg_path[540], cfg_name[17];
static int cfg_network;                 /* 0 alone, 1 open, 2 join */

static void cfg_read(const char *argv0, int *kind)
{
    char line[120], key[24], val[64];
    const char *cut = argv0, *p;
    FILE *f;
    int i;
    for (p = argv0; *p; p++)
        if (*p == '/' || *p == '\\' || *p == ':')
            cut = p + 1;
    snprintf(cfg_path, sizeof cfg_path, "%.*sHVCRAFT.CFG", (int)(cut - argv0), argv0);
    if (!(f = fopen(cfg_path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, " %23[a-z] = %63[^\r\n]", key, val) != 2)
            continue;
        if (!strcmp(key, "game")) {
            for (i = 0; i < 4; i++)
                if (!strcmp(val, kind_words[i]))
                    *kind = i;
        } else if (!strcmp(key, "arena")) {
            which = atoi(val);
        } else if (!strcmp(key, "name")) {
            snprintf(cfg_name, sizeof cfg_name, "%.16s", val);
        } else if (!strcmp(key, "network")) {
            cfg_network = !strcmp(val, "open") ? 1 : !strcmp(val, "join") ? 2 : 0;
        }
    }
    fclose(f);
}

static void cfg_write(int kind)
{
    FILE *f = cfg_path[0] ? fopen(cfg_path, "w") : 0;
    if (!f)
        return;
    fprintf(f, "game = %s\narena = %d\n", kind_words[kind & 3], which);
    if (cfg_name[0])
        fprintf(f, "name = %s\n", cfg_name);
    if (cfg_network)
        fprintf(f, "network = %s\n", cfg_network == 1 ? "open" : "join");
    fclose(f);
}

/* The game alone, begun: in the arena chosen, or the first if that one has not what the kind wants. */
static void begin_alone(int kind)
{
    char why[160];
    G = &alone, me = 0;
    if (hv_arena_read(&arena, arena_text(which), strlen(arena_text(which)), why, sizeof why) != 0
        || hv_game_start(&alone, &arena, kind, HV_DEMO_CRAFT) != 0) {
        which = 0;
        hv_arena_read(&arena, hv_arenas[0], strlen(hv_arenas[0]), why, sizeof why);
        hv_game_start(&alone, &arena, kind, HV_DEMO_CRAFT);
    }
}

/* The game opened to the network: 0; -1 (an arena of this PC's own, which the others have not; no network). */
static int open_game(int kind, const char *name)
{
    if (which >= HV_ARENAS || hvlink_open(&arena, which, kind, HV_DEMO_CRAFT, name) != 0)
        return -1;
    G = hvlink_game(), me = hvlink_craft();
    printf("HVCRAFT net open kind=%d arena=%d name=%s\n", kind, which, name);
    fflush(stdout);
    return 0;
}

int main(int argc, char **argv)
{
    static unsigned char evbuf[GLOS_EV_MAX];
    const struct glos_ev *ev = (const struct glos_ev *)evbuf;
    DGLConfig cfg;
    glos_handle win;
    char why[160], name[17] = "";
    unsigned last, acc = 0, said = 0, my_keys = 0, marked = 0, heard_from = 0;
    int heard = 0;
    int sounding = 0, code = 0;
    int w = 480, h = 360, i, on = 1, demo = 0, kind = HV_FLAGS, opening = 0, joining = 0;
    long until = -1;
    cfg_read(argc > 0 ? argv[0] : "", &kind);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-demo"))
            demo = 1;
        else if (!strcmp(argv[i], "-kind") && i + 1 < argc)
            kind = atoi(argv[++i]) & 3;
        else if (!strcmp(argv[i], "-until") && i + 1 < argc)
            until = atol(argv[++i]);
        else if (!strcmp(argv[i], "-arena") && i + 1 < argc)
            which = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-open"))
            opening = 1;
        else if (!strcmp(argv[i], "-join"))
            joining = 1;
        else if (!strcmp(argv[i], "-name") && i + 1 < argc)
            snprintf(name, sizeof name, "%s", argv[++i]);
    }
    if (glos_init(GAME_NAME, 0, 0) != 0) {
        printf("HVCRAFT no GLOS\n");
        return 2;
    }
    if (!name[0])
        snprintf(name, sizeof name, "%s", cfg_name);
    if (!name[0]) {                             /* the player's name on the network: the PC's own, unless said */
        int set = 0;
        if (glos_setting_get("net", "hostname", name, sizeof name, &set) != 0 || !name[0])
            snprintf(name, sizeof name, "GLOS PC");
    }
    user_arenas(argc > 0 ? argv[0] : "");
    if (which < 0 || which >= HV_ARENAS + nuser)
        which = 0;
    if (hv_arena_read(&arena, arena_text(which), strlen(arena_text(which)), why, sizeof why) != 0
        || hv_game_start(&alone, &arena, kind, HV_DEMO_CRAFT) != 0) {
        printf("HVCRAFT arena %s\n", why);
        return 3;
    }
    memset(&cfg, 0, sizeof cfg);
    cfg.width = w, cfg.height = h;
    cfg.color_bits = 16;
    cfg.depth_bits = 16;
    cfg.double_buffer = 1;
    cfg.vsync = 1;
    if (dglInit(&cfg) != 0) {
        printf("HVCRAFT no DOS-GL\n");
        return 4;
    }
    if (!(win = dglGetGlosWindow())) {          /* (it took the whole screen: this part of the game wants a window) */
        dglShutdown();
        printf("HVCRAFT no window\n");
        return 4;
    }
    glViewport(0, 0, w, h);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.55f, 0.70f, 0.85f, 1.0f);
    if ((opening && open_game(kind, name) != 0) || (joining && hvlink_look(name) != 0)) {
        printf("HVCRAFT net no network\n");
        code = 6, on = 0;
    } else if (!opening && !joining && !demo && cfg_network == 1) {     /* (as HVCRAFT.CFG says: failing, the game alone) */
        open_game(kind, name);
    } else if (!opening && !joining && !demo && cfg_network == 2) {
        hvlink_look(name);
    }
    title(win);
    if (!demo)                          /* (the suite's games are silent) */
        sounding = hvsound_start();
    printf("HVCRAFT started kind=%d craft=%d arena=%s\n", G->kind, G->ncraft, arena.name);
    fflush(stdout);
    last = glos_ticks();
    while (on) {
        unsigned now = glos_ticks(), due;
        acc += (now - last) * HV_STEPS;
        last = now;
        due = acc / 1000;
        acc %= 1000;
        if (!demo && due > 6)                   /* (a PC too slow for thirty a second: the game slows, it does not skip) */
            due = 6;
        while (due-- && on) {
            unsigned ctl = demo ? hv_auto(G, me, HV_DEMO_STRENGTH) : keys_held(), evs = 0, le = 0;
            int mode = hvlink_mode();
            my_keys = ctl;
            if (mode == HVL_ALONE || mode == HVL_LOOKING) {     /* the game alone (it goes on while a game is looked for) */
                unsigned char keys[HV_CRAFT];
                keys[0] = (unsigned char)ctl;
                for (i = 1; i < alone.ncraft; i++)
                    keys[i] = (unsigned char)hv_auto(&alone, i, HV_DEMO_STRENGTH);
                hv_game_step(&alone, keys);
                evs = alone.ev;
            }
            if (mode != HVL_ALONE) {
                le = hvlink_step(ctl);
                if (le & HVL_CRAFT) {           /* a craft in somebody's game: its arena, which is one of the six */
                    const struct hv_joiner *j = hvlink_joiner();
                    if (j->arena >= 0 && j->arena < HV_ARENAS
                        && hv_arena_read(&arena, hv_arenas[j->arena], strlen(hv_arenas[j->arena]), why, sizeof why) == 0) {
                        hvlink_arena(&arena);
                        G = hvlink_game(), me = hvlink_craft();
                        printf("HVCRAFT net joined craft=%d of=%d kind=%d arena=%d game=%s\n", me, j->ncraft, j->kind, j->arena, hvlink_host());
                    } else {
                        hvlink_leave();
                        le |= HVL_ENDED;
                    }
                }
                if (mode == HVL_OPEN || (le & HVL_STATE))
                    evs = G->ev;
                if ((le & HVL_STATE) && !heard)
                    heard = 1, heard_from = G->steps;   /* (a joiner's -until counts from the first step it is told) */
                if (le & HVL_SEATS)
                    printf("HVCRAFT net seats=%d\n", hvlink_seats());
                if ((mode == HVL_OPEN || (le & HVL_STATE)) && G->steps % 150 == 0 && G->steps != marked) {
                    marked = G->steps;          /* (the suite's: the game as both PCs have it at this step) */
                    printf("HVCRAFT net at %u score=%d:%d c0=%d,%d,%d c1=%d,%d,%d lamp=%d\n", G->steps, G->score[0], G->score[1],
                           G->c[0].x, G->c[0].y, G->c[0].heading, G->c[1].x, G->c[1].y, G->c[1].heading, G->lamp_holder);
                }
                if (le & (HVL_ENDED | HVL_NOBODY)) {
                    printf(le & HVL_ENDED ? "HVCRAFT net ended\n" : "HVCRAFT net nobody\n");
                    begin_alone(kind);
                    if (joining)                /* (the suite's: that was what it came for) */
                        code = le & HVL_NOBODY ? 5 : 0, on = 0;
                }
                if (le)
                    fflush(stdout);
            }
            if (sounding)
                hvsound_events(evs);
            if (until >= 0 && hvlink_mode() != HVL_LOOKING && on
                && (hvlink_mode() == HVL_JOINED ? heard && (long)(G->steps - heard_from) >= until : (long)G->steps >= until)) {
                /* the suite's: the game's state at this step, and the end */
                if (hvlink_mode() == HVL_ALONE)
                    printf(HV_AT_LINE, G->steps, G->score[0], G->score[1], G->c[0].x, G->c[0].y, G->c[0].heading, G->c[1].x,
                           G->c[1].y, G->lamp_holder, G->c[0].score);
                else
                    printf("HVCRAFT net end step=%u seats=%d\n", G->steps, hvlink_seats());
                fflush(stdout);
                on = 0;
            }
        }
        if (sounding) {                 /* the player's own fans, and the air by the craft's speed */
            const struct hv_craft *my = &G->c[me];
            int vx = my->vx < 0 ? -my->vx : my->vx, vy = my->vy < 0 ? -my->vy : my->vy;
            int work = (my_keys & HV_C_BOOST && my->tank > 0 ? 3 : 0) + (my_keys & (HV_C_FWD | HV_C_BACK) ? 1 : 0)
                       + (my_keys & (HV_C_SLIDE_L | HV_C_SLIDE_R) ? 1 : 0);
            hvsound_frame(work > 3 ? 3 : work, vx > vy ? vx + vy * 3 / 8 : vy + vx * 3 / 8, my->down, my->wet != 0);
        }
        frame((float)w / (float)h);
        overlay(w, h);
        dglSwapBuffers();
        if (now - said >= 1000) {
            said = now;
            title(win);
        }
        while (glos_event(evbuf, sizeof evbuf, 0, 0) > 0) {
            if (ev->kind == GLOS_EV_END || ev->kind == GLOS_EV_CLOSE) {
                on = 0;
            } else if (ev->kind == GLOS_EV_KEY) {
                const struct glos_ev_key *k = (const struct glos_ev_key *)evbuf;
                int mode = hvlink_mode();
                if (!k->down)
                    continue;
                if (k->key == 0x01)             /* Esc */
                    on = 0;
                else if (k->key == 0x18) {      /* O: the game opened to the network (begun again), or closed */
                    if (mode == HVL_OPEN) {
                        hvlink_leave();
                        begin_alone(kind);
                        printf("HVCRAFT net closed\n");
                    } else if (mode == HVL_ALONE) {
                        open_game(kind, name);
                    }
                    title(win);
                } else if (k->key == 0x24) {    /* J: a game looked for and joined, or left */
                    if (mode == HVL_LOOKING || mode == HVL_JOINED) {
                        hvlink_leave();
                        begin_alone(kind);
                        printf("HVCRAFT net left\n");
                    } else if (mode == HVL_ALONE) {
                        hvlink_look(name);
                    }
                    title(win);
                } else if (mode != HVL_ALONE) {
                    ;                           /* (the game and the arena are chosen before a game is opened, and by its opener) */
                } else if (k->key >= 0x02 && k->key <= 0x05) {  /* 1 to 4: that kind of game, begun again */
                    kind = (int)k->key - 2;
                    begin_alone(kind);
                    title(win);
                } else if (k->key == 0x0F || k->key == 0x31) { /* Tab or N: the next arena, the same kind of game */
                    which = (which + 1) % (HV_ARENAS + nuser);
                    begin_alone(kind);
                    title(win);
                }
            }
        }
    }
    hvlink_leave();
    hvsound_stop();
    dglShutdown();
    if (!demo && until < 0)                     /* (the suite's games leave nothing behind) */
        cfg_write(kind);
    printf("HVCRAFT end\n");
    return code;
}
