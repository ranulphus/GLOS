/* The hovercraft game's truth (hvgame.h). A step:
 *
 *   - each craft turns, and its fans push it: along its heading (forwards harder than back), across it (the side
 *     fans), harder still while the boost's tank lasts; the air takes a twenty-fourth of its speed a step, so
 *     that it goes on the way it was going for a good while: the fans change its speed, never its place. Set
 *     down on its skirt it loses a third of its speed a step and cannot be pushed;
 *   - it meets the arena: an edge it may not cross (a wall, a step up, a ramp's side) sends it back with half
 *     its speed against the edge; water takes it for three seconds and it is back at its dock;
 *   - craft that touch pass speed along the line between them (most of it comes back: nothing is lost but a
 *     fifth), and are moved apart; one set down gives nothing and takes nothing;
 *   - then the game's own rules: flags taken, carried and brought home; a hard bump knocks a flag loose, and
 *     as whoever bumped is touching it, it is theirs: home at once if it is their own side's flag, in their
 *     hands if they are the carrier's side (a pass); a carrier set down keeps it. Beacons turned and counted
 *     each second; the lamp held for points and taken by any bump; a lap's buoys in their order.
 *
 * The computer's driving (hv_auto) goes by squares: how far every square is from the place wanted (the arena's
 * own ways: hv_arena_distances), the next square nearer, and the fans that take the craft there. */
#include <string.h>

#include "hvgame.h"

#define TURN    16                      /* a step's turn: half a turn in a second */
#define REACH   200                     /* how near a thing's middle a craft takes or turns it (more than two
                                           craft's radii: a craft set down on its own flag does not keep it) */
#define HARD    5000                    /* a bump this hard (speed along the line) knocks a flag loose */

static const short QSIN[65] = {
    0, 402, 804, 1205, 1606, 2006, 2404, 2801, 3196, 3590, 3981, 4370, 4756, 5139, 5520, 5897, 6270, 6639, 7005, 7366, 7723,
    8076, 8423, 8765, 9102, 9434, 9760, 10080, 10394, 10702, 11003, 11297, 11585, 11866, 12140, 12406, 12665, 12916, 13160,
    13395, 13623, 13842, 14053, 14256, 14449, 14635, 14811, 14978, 15137, 15286, 15426, 15557, 15679, 15791, 15893, 15986,
    16069, 16143, 16207, 16261, 16305, 16340, 16364, 16379, 16384 };

static int quarter(int f)               /* sin of f/256 of a quarter turn, f 0 to 256 */
{
    int k = f >> 2, part = f & 3;
    return k >= 64 ? 16384 : QSIN[k] + (QSIN[k + 1] - QSIN[k]) * part / 4;
}

int hv_sin(int h)
{
    int f;
    h &= 1023;
    f = h & 255;
    switch (h >> 8) {
    case 0: return quarter(f);
    case 1: return quarter(256 - f);
    case 2: return -quarter(f);
    }
    return -quarter(256 - f);
}

int hv_cos(int h) { return hv_sin(h + 256); }

static int iabs(int v) { return v < 0 ? -v : v; }

static int isqrt(int v)
{
    int r = 0, bit = 1 << 14;
    for (; bit; bit >>= 1)
        if ((r + bit) * (r + bit) <= v)
            r += bit;
    return r;
}

static int in_play(const struct hv_craft *c) { return !c->wet; }

static int near(int ax, int ay, int bx, int by, int reach)
{
    int dx = ax - bx, dy = ay - by;
    return iabs(dx) < reach && iabs(dy) < reach && dx * dx + dy * dy < reach * reach;
}

static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };

/* May a craft in square (x, y) not cross its edge towards `way`? (Water is open: it falls in.) */
static int closed(const struct hv_arena *a, int x, int y, int way)
{
    int nx = x + DX[way], ny = y + DY[way];
    if (nx < 0 || ny < 0 || nx >= a->w || ny >= a->h)
        return 1;
    if (a->sq[ny][nx].kind == HV_WATER && a->sq[y][x].kind != HV_WALL)
        return 0;
    return !hv_arena_step(a, x, y, way);
}

static void put_at_dock(struct hv_game *g, int i)
{
    struct hv_craft *c = &g->c[i];
    const struct hv_arena *a = g->a;
    const struct hv_spot *s = &a->dock[c->side][(i / 2) % a->ndock[c->side]];
    int mx = a->w * 128, my = a->h * 128;
    c->x = s->x * 256 + 128 + ((i / 2) / a->ndock[c->side]) * 40;       /* (more craft than squares: side by side) */
    c->y = s->y * 256 + 128;
    c->vx = c->vy = 0;
    c->wet = c->down = 0;
    /* facing the arena's middle: the nearer of the four ways */
    if (iabs(mx - c->x) > iabs(my - c->y))
        c->heading = mx > c->x ? 256 : 768;
    else
        c->heading = my > c->y ? 512 : 0;
}

static void flag_home(struct hv_game *g, int side)
{
    g->flag[side].x = g->a->dock[side][0].x * 256 + 128;
    g->flag[side].y = g->a->dock[side][0].y * 256 + 128;
    g->flag[side].holder = -1;
    g->flag[side].away = 0;
}

int hv_game_start(struct hv_game *g, const struct hv_arena *a, int kind, int ncraft)
{
    int i;
    if (ncraft < 1 || ncraft > HV_CRAFT || kind < HV_FLAGS || kind > HV_LAPS || (kind == HV_LAPS && a->nbuoy < 2)
        || (kind == HV_BEACONS && a->nbeacon < 1))
        return -1;
    memset(g, 0, sizeof *g);
    g->a = a;
    g->kind = kind;
    g->ncraft = ncraft;
    for (i = 0; i < ncraft; i++) {
        g->c[i].side = i & 1;
        g->c[i].tank = HV_TANK;
        put_at_dock(g, i);
    }
    flag_home(g, 0), flag_home(g, 1);
    memset(g->beacon, -1, sizeof g->beacon);
    g->lamp_x = a->lamp.x * 256 + 128, g->lamp_y = a->lamp.y * 256 + 128;
    g->lamp_holder = -1;
    for (i = 0; i < 12; i++)
        g->way[i].x = -1;
    return 0;
}

int hv_craft_level(const struct hv_game *g, int i)
{
    const struct hv_craft *c = &g->c[i];
    const struct hv_square *s = &g->a->sq[c->y >> 8][c->x >> 8];
    int fx = c->x & 255, fy = c->y & 255;
    if (s->kind == HV_RAMP)
        return s->level * 256 + (s->way == HV_N ? 256 - fy : s->way == HV_E ? fx : s->way == HV_S ? fy : 256 - fx);
    return s->kind == HV_FLOOR ? s->level * 256 : 0;
}

static void drop_what_it_has(struct hv_game *g, int i)
{
    struct hv_craft *c = &g->c[i];
    if (!c->carrying)
        return;
    c->carrying = 0;
    if (g->kind == HV_FLAGS) {
        struct hv_flag *f = &g->flag[!c->side];
        f->holder = -1;
        f->x = c->x, f->y = c->y;
        f->away = 10 * HV_STEPS;        /* (it lies there ten seconds, then is home again) */
        c->numb = HV_STEPS;             /* (and is not picked up again by the craft that lost it, for a second) */
        g->ev |= HV_EV_DROP;
    } else if (g->kind == HV_LAMP && g->lamp_holder == i) {
        g->lamp_holder = -1;
    }
}

static void move(struct hv_game *g, int i)
{
    struct hv_craft *c = &g->c[i];
    const struct hv_arena *a = g->a;
    int sx = c->x >> 8, sy = c->y >> 8, nx = c->x + c->vx / 256, ny = c->y + c->vy / 256, tx, ty, k;
    /* the four edges of the square it is in */
    if (closed(a, sx, sy, HV_E) && nx + HV_RADIUS > (sx + 1) * 256) {
        nx = (sx + 1) * 256 - HV_RADIUS;
        if (c->vx > 0)
            c->vx = -c->vx / 2, g->ev |= HV_EV_WALL;
    }
    if (closed(a, sx, sy, HV_W) && nx - HV_RADIUS < sx * 256) {
        nx = sx * 256 + HV_RADIUS;
        if (c->vx < 0)
            c->vx = -c->vx / 2, g->ev |= HV_EV_WALL;
    }
    if (closed(a, sx, sy, HV_S) && ny + HV_RADIUS > (sy + 1) * 256) {
        ny = (sy + 1) * 256 - HV_RADIUS;
        if (c->vy > 0)
            c->vy = -c->vy / 2, g->ev |= HV_EV_WALL;
    }
    if (closed(a, sx, sy, HV_N) && ny - HV_RADIUS < sy * 256) {
        ny = sy * 256 + HV_RADIUS;
        if (c->vy < 0)
            c->vy = -c->vy / 2, g->ev |= HV_EV_WALL;
    }
    /* across a corner into a square neither way round leads to: the lesser of the two moves is not made */
    tx = nx >> 8, ty = ny >> 8;
    if (tx != sx && ty != sy) {
        int wx = tx > sx ? HV_E : HV_W, wy = ty > sy ? HV_S : HV_N;
        if (closed(a, tx, sy, wy) && closed(a, sx, ty, wx)) {
            if (iabs(nx - c->x) < iabs(ny - c->y))
                nx = c->x, c->vx = 0;
            else
                ny = c->y, c->vy = 0;
            tx = nx >> 8, ty = ny >> 8;
        }
    }
    /* the corners of the walls round the square it comes to */
    for (k = 0; k < 4; k++) {
        int qx = tx + (k & 1 ? 1 : -1), qy = ty + (k & 2 ? 1 : -1), px, py, dx, dy, d;
        if (qx >= 0 && qy >= 0 && qx < a->w && qy < a->h && a->sq[qy][qx].kind != HV_WALL)
            continue;
        px = (k & 1 ? tx + 1 : tx) * 256, py = (k & 2 ? ty + 1 : ty) * 256;
        dx = nx - px, dy = ny - py;
        if (iabs(dx) >= HV_RADIUS || iabs(dy) >= HV_RADIUS || dx * dx + dy * dy >= HV_RADIUS * HV_RADIUS)
            continue;
        d = isqrt(dx * dx + dy * dy);
        if (d == 0)
            continue;
        nx = px + dx * HV_RADIUS / d;
        ny = py + dy * HV_RADIUS / d;
        c->vx -= c->vx / 4, c->vy -= c->vy / 4;
    }
    if (nx < HV_RADIUS)                 /* (the grid's own edge, whatever a file's last row was) */
        nx = HV_RADIUS;
    if (ny < HV_RADIUS)
        ny = HV_RADIUS;
    if (nx > a->w * 256 - HV_RADIUS)
        nx = a->w * 256 - HV_RADIUS;
    if (ny > a->h * 256 - HV_RADIUS)
        ny = a->h * 256 - HV_RADIUS;
    if (a->sq[ny >> 8][nx >> 8].kind == HV_WALL)        /* (never into a wall: where it was) */
        nx = c->x, ny = c->y, c->vx = c->vy = 0;
    c->x = nx, c->y = ny;
    if (a->sq[ny >> 8][nx >> 8].kind == HV_WATER) {     /* in the water: three seconds, then its dock */
        drop_what_it_has(g, i);
        if (g->kind == HV_FLAGS && g->flag[!c->side].away)
            flag_home(g, !c->side);     /* (a flag that went in with it is home at once) */
        c->wet = 3 * HV_STEPS;
        c->vx = c->vy = 0;
        g->ev |= HV_EV_SPLASH;
    }
}

static void bumps(struct hv_game *g)
{
    int i, j;
    for (i = 0; i < g->ncraft; i++)
        for (j = i + 1; j < g->ncraft; j++) {
            struct hv_craft *p = &g->c[i], *q = &g->c[j];
            int dx = q->x - p->x, dy = q->y - p->y, d, ux, uy, along, over;
            if (!in_play(p) || !in_play(q) || iabs(dx) >= 2 * HV_RADIUS || iabs(dy) >= 2 * HV_RADIUS)
                continue;
            if (dx * dx + dy * dy >= 4 * HV_RADIUS * HV_RADIUS)
                continue;
            if (hv_craft_level(g, i) / 256 != hv_craft_level(g, j) / 256)
                continue;               /* (one on the quay, one under it) */
            d = isqrt(dx * dx + dy * dy);
            if (d == 0)
                dx = 1, dy = 0, d = 1;
            ux = dx * 256 / d, uy = dy * 256 / d;       /* from the first to the second, 256ths */
            along = ((q->vx - p->vx) / 16 * ux + (q->vy - p->vy) / 16 * uy) / 16;      /* closing when below 0 */
            if (along < 0 && !(p->down && q->down)) {
                int give = -along;      /* the speed they close at */
                if (p->down) {
                    q->vx += give * 9 / 5 * ux / 256, q->vy += give * 9 / 5 * uy / 256;
                } else if (q->down) {
                    p->vx -= give * 9 / 5 * ux / 256, p->vy -= give * 9 / 5 * uy / 256;
                } else {
                    p->vx -= give * 9 / 10 * ux / 256, p->vy -= give * 9 / 10 * uy / 256;
                    q->vx += give * 9 / 10 * ux / 256, q->vy += give * 9 / 10 * uy / 256;
                }
                g->ev |= HV_EV_BUMP;
                if (g->kind == HV_FLAGS && give > HARD) {       /* a carrier hit hard lets the flag go */
                    if (p->carrying && !p->down)
                        drop_what_it_has(g, i);
                    if (q->carrying && !q->down)
                        drop_what_it_has(g, j);
                }
                if (g->kind == HV_LAMP && (g->lamp_holder == i || g->lamp_holder == j)) {       /* the lamp changes hands */
                    int from = g->lamp_holder, to = from == i ? j : i;
                    g->c[from].carrying = 0;
                    g->c[to].carrying = 1;
                    g->lamp_holder = to;
                    g->ev |= HV_EV_TAKEN;
                }
            }
            over = 2 * HV_RADIUS - d;   /* moved apart: each half the way, or all of it the one that can move */
            if (p->down && !q->down) {
                q->x += over * ux / 256, q->y += over * uy / 256;
            } else if (q->down && !p->down) {
                p->x -= over * ux / 256, p->y -= over * uy / 256;
            } else if (!p->down) {
                p->x -= over * ux / 512, p->y -= over * uy / 512;
                q->x += (over - over / 2) * ux / 256, q->y += (over - over / 2) * uy / 256;
            }
        }
}

static void rules(struct hv_game *g)
{
    const struct hv_arena *a = g->a;
    int i, k, second = g->steps % HV_STEPS == 0;
    if (g->kind == HV_FLAGS) {
        for (k = 0; k < 2; k++) {       /* side k's own flag */
            struct hv_flag *f = &g->flag[k];
            if (f->holder >= 0) {
                struct hv_craft *c = &g->c[f->holder];
                const struct hv_square *s = &a->sq[c->y >> 8][c->x >> 8];
                f->x = c->x, f->y = c->y;
                if (s->mark == 'A' + c->side && g->flag[c->side].holder < 0 && !g->flag[c->side].away) {
                    g->score[c->side]++;        /* brought home, its own flag there */
                    c->carrying = 0;
                    flag_home(g, k);
                    g->ev |= HV_EV_SCORE;
                }
                continue;
            }
            for (i = 0; i < g->ncraft; i++) {
                struct hv_craft *c = &g->c[i];
                if (!in_play(c) || !near(c->x, c->y, f->x, f->y, REACH))
                    continue;
                if (c->side != k && !c->carrying && !c->numb) {
                    f->holder = i;
                    f->away = 0;
                    c->carrying = 1;
                    g->ev |= HV_EV_TAKEN;
                    break;
                }
                if (c->side == k && f->away) {  /* its own side's craft sends it home */
                    flag_home(g, k);
                    break;
                }
            }
            if (f->holder < 0 && f->away && --f->away == 0)
                flag_home(g, k);
        }
    } else if (g->kind == HV_BEACONS) {
        int count[2] = { 0, 0 };
        for (k = 0; k < a->nbeacon; k++) {
            for (i = 0; i < g->ncraft; i++)
                if (in_play(&g->c[i]) && near(g->c[i].x, g->c[i].y, a->beacon[k].x * 256 + 128, a->beacon[k].y * 256 + 128, REACH)
                    && g->beacon[k] != g->c[i].side) {
                    g->beacon[k] = (signed char)g->c[i].side;
                    g->ev |= HV_EV_BEACON;
                }
            if (g->beacon[k] >= 0)
                count[g->beacon[k]]++;
        }
        if (second && count[0] != count[1])
            g->score[count[1] > count[0]]++;
    } else if (g->kind == HV_LAMP) {
        if (g->lamp_holder >= 0 && !in_play(&g->c[g->lamp_holder])) {
            g->c[g->lamp_holder].carrying = 0;
            g->lamp_holder = -1;
        }
        if (g->lamp_holder < 0) {
            g->lamp_x = a->lamp.x * 256 + 128, g->lamp_y = a->lamp.y * 256 + 128;      /* (nobody's: at its place) */
            for (i = 0; i < g->ncraft; i++)
                if (in_play(&g->c[i]) && near(g->c[i].x, g->c[i].y, g->lamp_x, g->lamp_y, REACH)) {
                    g->lamp_holder = i;
                    g->c[i].carrying = 1;
                    g->ev |= HV_EV_TAKEN;
                    break;
                }
        } else {
            g->lamp_x = g->c[g->lamp_holder].x, g->lamp_y = g->c[g->lamp_holder].y;
            if (second)
                g->c[g->lamp_holder].score++;
        }
    } else {
        for (i = 0; i < g->ncraft; i++) {
            struct hv_craft *c = &g->c[i];
            if (!in_play(c) || !near(c->x, c->y, a->buoy[c->buoy].x * 256 + 128, a->buoy[c->buoy].y * 256 + 128, REACH))
                continue;
            if (++c->buoy == a->nbuoy) {        /* round them all: a lap */
                unsigned took = g->steps - c->lap_start;
                c->buoy = 0;
                c->score++;
                if (!c->best_lap || took < c->best_lap)
                    c->best_lap = took;
                c->lap_start = g->steps;
                g->ev |= HV_EV_SCORE;
            }
        }
    }
}

void hv_game_step(struct hv_game *g, const unsigned char *controls)
{
    int i;
    g->ev = 0;
    g->steps++;
    for (i = 0; i < g->ncraft; i++) {
        struct hv_craft *c = &g->c[i];
        unsigned k = controls[i];
        if (c->wet) {
            if (--c->wet == 0)
                put_at_dock(g, i);
            continue;
        }
        if (c->numb)
            c->numb--;
        if (k & HV_C_LEFT)
            c->heading = (c->heading - TURN) & 1023;
        if (k & HV_C_RIGHT)
            c->heading = (c->heading + TURN) & 1023;
        c->down = (k & HV_C_DOWN) != 0;
        if (c->down) {                  /* on its skirt: a third of its speed gone a step */
            c->vx -= c->vx / 3, c->vy -= c->vy / 3;
            if (iabs(c->vx) < 96)
                c->vx = 0;
            if (iabs(c->vy) < 96)
                c->vy = 0;
        } else {
            int s = hv_sin(c->heading), co = hv_cos(c->heading);
            int thrust = (k & HV_C_FWD ? 560 : 0) - (k & HV_C_BACK ? 300 : 0);
            int slide = (k & HV_C_SLIDE_R ? 400 : 0) - (k & HV_C_SLIDE_L ? 400 : 0);
            int flag = g->kind == HV_FLAGS && c->carrying;
            if (k & HV_C_BOOST && c->tank > 0 && !flag) {
                thrust += 900;
                c->tank--;
            } else if (!(k & HV_C_BOOST) && c->tank < HV_TANK && g->steps % 4 == 0) {
                c->tank++;              /* (full again in twelve seconds) */
            }
            if (flag)
                thrust = thrust * 3 / 4;        /* a flag's carrier is slower */
            c->vx += (thrust * s + slide * co) / 16384;
            c->vy += (-thrust * co + slide * s) / 16384;
            c->vx -= c->vx / 24, c->vy -= c->vy / 24;
        }
        move(g, i);
    }
    bumps(g);
    for (i = 0; i < g->ncraft; i++)     /* (a bump may have put one where it must not be: the arena again) */
        if (in_play(&g->c[i])) {
            int vx = g->c[i].vx, vy = g->c[i].vy;
            g->c[i].vx = g->c[i].vy = 0;
            move(g, i);
            if (in_play(&g->c[i]))
                g->c[i].vx = vx, g->c[i].vy = vy;
        }
    rules(g);
}

/* ---- the computer's driving */

/* How far every square is from square (x, y): kept for the places asked for of late. */
static const unsigned short (*way_to(struct hv_game *g, int x, int y))[HV_GRID]
{
    int k;
    for (k = 0; k < 12; k++)
        if (g->way[k].x == x && g->way[k].y == y)
            return (const unsigned short (*)[HV_GRID])g->way[k].d;
    k = g->way_next++ % 12;
    g->way[k].x = x, g->way[k].y = y;
    hv_arena_distances(g->a, x, y, g->way[k].d);
    return (const unsigned short (*)[HV_GRID])g->way[k].d;
}

unsigned hv_auto(struct hv_game *g, int i, int strength)
{
    const struct hv_arena *a = g->a;
    struct hv_craft *c = &g->c[i];
    const unsigned short (*d)[HV_GRID];
    int sx = c->x >> 8, sy = c->y >> 8, tx = c->x, ty = c->y, k, best = 0, ax, ay, fx, fy, dx, dy, cross, dot, side_speed, run = -1;
    unsigned keys = 0;
    if (!in_play(c))
        return 0;
    /* where it wants to be */
    if (g->kind == HV_FLAGS) {
        struct hv_flag *theirs = &g->flag[!c->side], *ours = &g->flag[c->side];
        if (c->carrying || (theirs->holder >= 0 && (i / 2) % 2 == 0)) {         /* home with it; or beside its carrier */
            tx = a->dock[c->side][0].x * 256 + 128, ty = a->dock[c->side][0].y * 256 + 128;
            if (c->carrying && (ours->holder >= 0 || ours->away))
                tx = ours->x, ty = ours->y;     /* (no scoring while its own flag is out: after that one) */
        } else if (ours->holder >= 0 && (i / 2) % 2 == 1) {
            tx = ours->x, ty = ours->y;         /* after the craft that has its flag */
        } else {
            tx = theirs->x, ty = theirs->y;
        }
    } else if (g->kind == HV_BEACONS) {
        int far = 0x7FFFFFFF;
        for (k = 0; k < a->nbeacon; k++) {      /* the nearest beacon that is not its side's */
            int bx = a->beacon[k].x * 256 + 128, by = a->beacon[k].y * 256 + 128, dist = iabs(bx - c->x) + iabs(by - c->y);
            if (g->beacon[k] != c->side && dist < far)
                far = dist, tx = bx, ty = by;
        }
        if (far == 0x7FFFFFFF)                  /* all its side's: by the one farthest from home, to keep it */
            tx = a->beacon[(g->steps / 600 + (unsigned)i) % (unsigned)a->nbeacon].x * 256 + 128,
            ty = a->beacon[(g->steps / 600 + (unsigned)i) % (unsigned)a->nbeacon].y * 256 + 128;
    } else if (g->kind == HV_LAMP) {
        if (g->lamp_holder == i) {              /* away with it: from dock to dock */
            int to = (g->steps / 240 + (unsigned)i) & 1;
            tx = a->dock[to][0].x * 256 + 128, ty = a->dock[to][0].y * 256 + 128;
        } else {
            tx = g->lamp_x, ty = g->lamp_y;
        }
    } else {
        tx = a->buoy[c->buoy].x * 256 + 128, ty = a->buoy[c->buoy].y * 256 + 128;
    }
    /* the next square on the way there, and the place in it to make for */
    d = way_to(g, tx >> 8, ty >> 8);
    ax = tx, ay = ty;
    if ((tx >> 8) != sx || (ty >> 8) != sy) {
        best = d[sy][sx];
        for (k = 0; k < 4; k++) {
            int nx = sx + DX[k], ny = sy + DY[k];
            if (nx >= 0 && ny >= 0 && nx < a->w && ny < a->h && hv_arena_step(a, sx, sy, k) && d[ny][nx] < best) {
                best = d[ny][nx];
                ax = nx * 256 + 128, ay = ny * 256 + 128;
                run = k;
            }
        }
    }
    /* the fans: turn towards it, push when it is ahead, and (the stronger drivers) stop the slide across */
    fx = hv_sin(c->heading), fy = -hv_cos(c->heading);
    dx = ax - c->x, dy = ay - c->y;
    cross = (fx / 64) * dy - (fy / 64) * dx;    /* above 0: it is to the right */
    dot = (fx / 64) * dx + (fy / 64) * dy;
    if (cross > 256 * 12)
        keys |= HV_C_RIGHT;
    else if (cross < -256 * 12)
        keys |= HV_C_LEFT;
    else if (dot < 0)                   /* (dead astern: about, by the right) */
        keys |= HV_C_RIGHT;
    if (dot > 0 && iabs(cross) < iabs(dot) * 2)
        keys |= HV_C_FWD;
    /* a craft in its way, close ahead: the side fan takes it round, to the right of one dead ahead (two that meet
       head on each go to their own right, and pass) */
    for (k = 0; k < g->ncraft; k++) {
        const struct hv_craft *o = &g->c[k];
        int ox = o->x - c->x, oy = o->y - c->y, ahead, aside;
        if (k == i || !in_play(o) || iabs(ox) > 400 || iabs(oy) > 400)
            continue;
        if (g->kind == HV_LAMP && (g->lamp_holder == k) != (g->lamp_holder == i) && g->lamp_holder != i)
            continue;                   /* (the lamp's carrier is what it is after: no going round that one) */
        ahead = ((fx / 64) * ox + (fy / 64) * oy) / 256;        /* in 1/256 square, along its heading */
        aside = ((fx / 64) * oy - (fy / 64) * ox) / 256;        /* ... and to its right */
        if (ahead > 0 && ahead < 380 && iabs(aside) < 200) {
            keys |= aside > 24 ? HV_C_SLIDE_L : HV_C_SLIDE_R;
            return keys;
        }
    }
    if (strength >= 1) {
        side_speed = (c->vx / 64) * (-fy / 64) + (c->vy / 64) * (fx / 64);     /* its speed to its own right */
        if (side_speed > 256 * 6 && cross <= 256 * 40)
            keys |= HV_C_SLIDE_L;
        else if (side_speed < -256 * 6 && cross >= -256 * 40)
            keys |= HV_C_SLIDE_R;
    }
    if (strength >= 2 && (keys & HV_C_FWD) && iabs(cross) < 256 * 16 && c->tank > HV_TANK / 3 && run >= 0) {
        int nx = sx + 2 * DX[run], ny = sy + 2 * DY[run];       /* the way goes straight on a square more: the boost */
        if (nx >= 0 && ny >= 0 && nx < a->w && ny < a->h && hv_arena_step(a, sx + DX[run], sy + DY[run], run) && d[ny][nx] < best)
            keys |= HV_C_BOOST;
    }
    return keys;
}
