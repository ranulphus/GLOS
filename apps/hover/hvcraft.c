/* HVCRAFT.EXE: the hovercraft game (GLOS v1, phase F; docs/v1/apps/hovercraft.md), its first playable part.
 * "Hovercraft" is its working name: its name is the owner's to choose. GLOS's own game: its craft, arenas, rules
 * and look were made for it; nothing of any other game is in it.
 *
 * Here: one arena (built in: the six that come as files, and the lobby that chooses among them, are to come),
 * the player's craft with its side's and the other side's driven by the computer, the four kinds of game
 * (apps/shared/hvgame.c is their truth, thirty steps a second), and the picture, drawn with DOS-GL (RetroGPU's
 * gl) in a window of the desktop: the card's engine where DOS-GL has one for a window, its software renderer
 * elsewhere. Plain coloured faces, lit by the way they face.
 *
 * Keys: Up and Down (or W and S) the fans forwards and back; Left and Right (or A and D) turn; Q and E the side
 * fans; Shift the boost; Space sets the craft down. 1 to 4 choose the game (flags, beacons, the lamp, laps) and
 * begin it again; Esc ends. Over the scene: the score, the boost's tank, and a small map of the arena (the craft,
 * the flags, beacons, the lamp or the next buoy).
 *
 * HVCRAFT -demo [-kind 0..3] [-until STEP] is the suite's: every craft the computer's, the game's state said at
 * a step ("HVCRAFT at ..."), which is the same on every PC whatever the pictures cost.
 *
 * Not yet (the plan's rest): the lobby and network games; arenas from files; sound; pads and the mouse; full
 * screen; the computer's strengths chosen; names over the craft; an arrow to the flag or the lamp. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <GL/dosgl.h>
#include <GL/gl.h>
#include <glos/glos.h>

#include "../shared/hvbasin.h"
#include "../shared/hvgame.h"

#define GAME_NAME "Hovercraft"          /* (the working name) */

static struct hv_arena arena;
static struct hv_game game;
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
        box(fx - 0.06f, 0.0f, fz - 0.06f, fx + 0.06f, 0.9f, fz + 0.06f, game.beacon[k] < 0 ? white : SIDE[game.beacon[k]]);
        box(fx - 0.16f, 0.9f, fz - 0.16f, fx + 0.16f, 1.1f, fz + 0.16f, game.beacon[k] < 0 ? white : SIDE[game.beacon[k]]);
    }
    if (game.kind == HV_LAMP) {
        float fx = (float)game.lamp_x / 256.0f, fz = (float)game.lamp_y / 256.0f, up = game.lamp_holder >= 0 ? 0.55f : 0.0f;
        box(fx - 0.10f, up, fz - 0.10f, fx + 0.10f, up + 0.35f, fz + 0.10f, lampc);
    }
    if (game.kind == HV_LAPS)
        for (k = 0; k < arena.nbuoy; k++) {
            float fx = (float)arena.buoy[k].x + 0.5f, fz = (float)arena.buoy[k].y + 0.5f;
            box(fx - 0.10f, 0.0f, fz - 0.10f, fx + 0.10f, k == game.c[0].buoy ? 0.9f : 0.4f, fz + 0.10f, k == game.c[0].buoy ? lampc : buoy);
        }
    if (game.kind == HV_FLAGS)
        for (k = 0; k < 2; k++) {
            float fx = (float)game.flag[k].x / 256.0f, fz = (float)game.flag[k].y / 256.0f, up = game.flag[k].holder >= 0 ? 0.35f : 0.0f;
            box(fx - 0.03f, up, fz - 0.03f, fx + 0.03f, up + 1.0f, fz + 0.03f, white);
            box(fx + 0.03f, up + 0.70f, fz - 0.02f, fx + 0.40f, up + 1.0f, fz + 0.02f, SIDE[k]);
        }
    glEnd();
}

static void draw_craft(int i)
{
    static const float dark[3] = { 0.20f, 0.20f, 0.22f }, glass[3] = { 0.80f, 0.90f, 0.95f }, white[3] = { 0.95f, 0.95f, 0.95f };
    const struct hv_craft *c = &game.c[i];
    float up = (float)hv_craft_level(&game, i) / 256.0f * LEVEL + (c->down ? 0.0f : 0.10f);
    if (c->wet)
        return;
    glPushMatrix();
    glTranslatef((float)c->x / 256.0f, up, (float)c->y / 256.0f);
    glRotatef(-(float)c->heading * 360.0f / 1024.0f, 0.0f, 1.0f, 0.0f);         /* (its nose towards -z at heading 0) */
    glBegin(GL_TRIANGLES);
    box(-0.26f, 0.00f, -0.30f, 0.26f, 0.08f, 0.30f, dark);                      /* the skirt */
    box(-0.22f, 0.08f, -0.28f, 0.22f, 0.20f, 0.26f, SIDE[c->side]);            /* the hull */
    box(-0.10f, 0.20f, -0.16f, 0.10f, 0.30f, 0.04f, i == 0 ? white : glass);    /* the cabin (the player's: white) */
    box(-0.16f, 0.20f, 0.12f, 0.16f, 0.36f, 0.24f, dark);                       /* the fan's housing, at the back */
    glEnd();
    glPopMatrix();
}

static void frame(float aspect)
{
    const struct hv_craft *me = &game.c[0];
    float heading = (float)me->heading * 360.0f / 1024.0f;
    float s = (float)hv_sin(me->heading) / 16384.0f, c = (float)hv_cos(me->heading) / 16384.0f;
    float px = (float)me->x / 256.0f, pz = (float)me->y / 256.0f, py = (float)hv_craft_level(&game, 0) / 256.0f * LEVEL;
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
    for (i = 0; i < game.ncraft; i++)
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
    const struct hv_craft *me = &game.c[0];
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
    flat(8.0f, 8.0f, game.kind <= HV_BEACONS ? 104.0f : 52.0f, 34.0f, dark);
    if (game.kind <= HV_BEACONS) {
        x = number(14.0f, 13.0f, game.score[0], SIDE[0]);
        flat(x + 2.0f, 20.0f, 4.0f, 4.0f, light), flat(x + 2.0f, 28.0f, 4.0f, 4.0f, light);
        number(x + 12.0f, 13.0f, game.score[1], SIDE[1]);
    } else {
        number(14.0f, 13.0f, me->score, white);
    }
    /* the boost's tank */
    flat(8.0f, (float)h - 20.0f, 104.0f, 12.0f, dark);
    flat(10.0f, (float)h - 18.0f, 100.0f * (float)me->tank / (float)HV_TANK, 8.0f, tank);
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
    for (i = 0; i < arena.nbeacon && game.kind == HV_BEACONS; i++)
        flat(mx + cell * (float)arena.beacon[i].x, my + cell * (float)arena.beacon[i].y, cell, cell, game.beacon[i] < 0 ? white : SIDE[game.beacon[i]]);
    if (game.kind == HV_LAMP)
        flat(mx + (float)game.lamp_x / 256.0f * cell - 2.0f, my + (float)game.lamp_y / 256.0f * cell - 2.0f, 5.0f, 5.0f, tank);
    if (game.kind == HV_LAPS)
        flat(mx + cell * (float)arena.buoy[me->buoy].x, my + cell * (float)arena.buoy[me->buoy].y, cell, cell, tank);
    for (i = 0; i < 2 && game.kind == HV_FLAGS; i++)
        flat(mx + (float)game.flag[i].x / 256.0f * cell - 1.0f, my + (float)game.flag[i].y / 256.0f * cell - 4.0f, 2.0f, 6.0f, white);
    for (i = game.ncraft - 1; i >= 0; i--)
        if (!game.c[i].wet)
            flat(mx + (float)game.c[i].x / 256.0f * cell - 1.5f, my + (float)game.c[i].y / 256.0f * cell - 1.5f, 3.0f, 3.0f, i ? SIDE[game.c[i].side] : white);
    glEnd();
    glEnable(GL_DEPTH_TEST);
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
    return c;
}

static void title(glos_handle win)
{
    static const char *const kinds[4] = { "Flags", "Beacons", "The lamp", "Laps" };
    char t[64];
    if (game.kind == HV_FLAGS || game.kind == HV_BEACONS)
        snprintf(t, sizeof t, "%s: %s %d : %d", GAME_NAME, kinds[game.kind], game.score[0], game.score[1]);
    else
        snprintf(t, sizeof t, "%s: %s %ld", GAME_NAME, kinds[game.kind], game.c[0].score);
    glos_set_text(win, GLOS_PROP_WIN_TITLE, t);
}

int main(int argc, char **argv)
{
    static unsigned char evbuf[GLOS_EV_MAX];
    const struct glos_ev *ev = (const struct glos_ev *)evbuf;
    DGLConfig cfg;
    glos_handle win;
    char why[160];
    unsigned last, acc = 0, said = 0;
    int w = 480, h = 360, i, on = 1, demo = 0, kind = HV_FLAGS;
    long until = -1;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-demo"))
            demo = 1;
        else if (!strcmp(argv[i], "-kind") && i + 1 < argc)
            kind = atoi(argv[++i]) & 3;
        else if (!strcmp(argv[i], "-until") && i + 1 < argc)
            until = atol(argv[++i]);
    }
    if (glos_init(GAME_NAME, 0, 0) != 0) {
        printf("HVCRAFT no GLOS\n");
        return 2;
    }
    if (hv_arena_read(&arena, HV_BASIN, sizeof HV_BASIN - 1, why, sizeof why) != 0 || hv_game_start(&game, &arena, kind, HV_DEMO_CRAFT) != 0) {
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
    title(win);
    printf("HVCRAFT started kind=%d craft=%d arena=%s\n", game.kind, game.ncraft, arena.name);
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
            unsigned char keys[HV_CRAFT];
            for (i = 0; i < game.ncraft; i++)
                keys[i] = (unsigned char)(i == 0 && !demo ? keys_held() : hv_auto(&game, i, HV_DEMO_STRENGTH));
            hv_game_step(&game, keys);
            if (until >= 0 && (long)game.steps >= until) {     /* the suite's: the game's state at this step, and the end */
                printf(HV_AT_LINE, game.steps, game.score[0], game.score[1], game.c[0].x, game.c[0].y, game.c[0].heading, game.c[1].x,
                       game.c[1].y, game.lamp_holder, game.c[0].score);
                fflush(stdout);
                on = 0;
            }
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
                if (!k->down)
                    continue;
                if (k->key == 0x01)             /* Esc */
                    on = 0;
                else if (k->key >= 0x02 && k->key <= 0x05) {    /* 1 to 4: that kind of game, begun again */
                    hv_game_start(&game, &arena, (int)k->key - 2, 4);
                    title(win);
                }
            }
        }
    }
    dglShutdown();
    printf("HVCRAFT end\n");
    return 0;
}
