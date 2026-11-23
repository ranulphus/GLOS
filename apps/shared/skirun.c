/* The skiing game's runs (skirun.h). A step:
 *
 *   - the skis turn a heading at a time towards the key held (more slowly in a tuck); in the air the skier
 *     turns instead, for style;
 *   - the slope pulls along the skis by how nearly they point down it; the snow drags (powder much, ice
 *     little, a tuck less, a plough more), and takes away what speed there is across the skis: a quarter of it
 *     a step on the piste, hardly any on ice, where the skier slides on the way they were going;
 *   - a ramp taken fast, or a mogul taken very fast, throws the skier into the air for a time that grows with
 *     the speed; landed with the skis across the slope, or part way round a turn, is a fall;
 *   - a tree, a rock, a fence or the hut is a fall: some seconds on the ground (fewer in powder), no more;
 *   - a gate is passed between its poles or missed (five seconds); the finish ends the run.
 *
 * Speeds are in 1/256 pixel a step: 1792 is 420 pixels a second, about what the fall line gives on a red run. */
#include "skirun.h"

/* sin of a heading's angle from the fall line (0 to 8: 0 to 90 degrees), in 1024ths; cos is the same table read
   from the other end. */
static const short SIN[9] = { 0, 200, 392, 569, 724, 851, 946, 1004, 1024 };
static const short PULL[4] = { 11, 13, 15, 17 };        /* the slope's pull by grade, 1/256 pixel a step each step */

static int iabs(int v) { return v < 0 ? -v : v; }

void skirun_start(struct skirun *r, unsigned seed, int grade, int mode)
{
    int i;
    struct skimount m;
    skimount_init(&m, seed, grade, mode);
    for (i = 0; i < (int)sizeof *r; i++)
        ((unsigned char *)r)[i] = 0;
    r->m = m;
    r->x = skimount_centre(&m, 96) * 256;
    r->y = 96 * 256;
    r->vy = 128;                        /* (a push off) */
    for (i = 0; i < 4; i++)
        r->near[i] = -1;
    for (i = 0; i < SKI_PIECES; i++)
        r->piece[i].index = -1;
}

const struct ski_thing *skirun_piece(struct skirun *r, int index, int *n)
{
    struct ski_piece *p = &r->piece[(unsigned)index % SKI_PIECES];
    if (p->index != index) {
        p->n = skimount_chunk(&r->m, index, p->t, 48);
        p->index = index;
    }
    *n = p->n;
    return p->t;
}

int skirun_gust(const struct skirun *r)
{
    /* the strongest push of a gust by grade: on a black run some 37 pixels a second at the gust's height */
    static const short PEAK[4] = { 64, 96, 128, 160 };
    unsigned g, t, h;
    int peak;
    if (!r->wind)
        return 0;
    g = r->steps / SKI_GUST_STEPS, t = r->steps % SKI_GUST_STEPS;
    h = (r->m.seed ^ (g + 1) * 0x9E3779B1u) * 0x85EBCA6Bu;     /* (the spell's dice) */
    h ^= h >> 13, h *= 0xC2B2AE35u, h ^= h >> 16;
    if ((h & 3) == 0 || t >= SKI_GUST_STEPS / 2)                /* a calm spell, or the calm half of one */
        return 0;
    peak = PEAK[r->m.grade & 3] * (2 + (int)((h >> 2) & 3)) / 5;        /* (two fifths to all of it) */
    t = t < SKI_GUST_STEPS / 4 ? t : SKI_GUST_STEPS / 2 - t;            /* (up and down again) */
    peak = peak * (int)t / (SKI_GUST_STEPS / 4);
    return h & 0x10 ? peak : -peak;
}

int skirun_speed(const struct skirun *r)
{
    /* (the longer side and three eighths of the shorter: within a few per cent, and whole numbers) */
    int a = iabs(r->vx), b = iabs(r->vy), v = a > b ? a + b * 3 / 8 : b + a * 3 / 8;
    return v * SKI_STEPS / 256;
}

int skirun_height(const struct skirun *r)
{
    int t = r->air_total - r->air;
    return r->state == SKI_S_AIR && r->air_total > 0 ? t * r->air * 4 * (8 + r->air_total / 4) / (r->air_total * r->air_total) : 0;
}

unsigned long skirun_ms(const struct skirun *r)
{
    unsigned long steps = (r->state == SKI_S_DONE ? r->finish_steps : r->steps) + (unsigned long)r->gates_missed * SKI_MISS_STEPS;
    return steps * 1000 / SKI_STEPS;
}

static void fall(struct skirun *r, int soft)
{
    r->state = SKI_S_FALLEN;
    r->down = soft ? 60 : 90;
    r->vx = r->vy = 0;
    r->falls++;
    r->streak = 0;
    r->run_acc = 0;
    r->ev |= SKI_EV_FALL;
}

static void jump(struct skirun *r, int steps)
{
    r->state = SKI_S_AIR;
    r->air = r->air_total = steps;
    r->spin = 0;
    r->turn_wait = 0;
    r->jumps++;
    r->ev |= SKI_EV_JUMP;
}

/* What the skier meets where they are now: a fall, a jump, or a near thing. */
static void meet(struct skirun *r, int speed)
{
    const struct ski_thing *t;
    int px = r->x >> 8, py = r->y >> 8, c0 = (py - 40) / SKI_CHUNK, c1 = (py + 40) / SKI_CHUNK, c, i, n;
    if (py < 40)
        c0 = 0;
    for (c = c0; c <= c1; c++) {
        t = skirun_piece(r, c, &n);
        for (i = 0; i < n; i++) {
            int dx = t[i].x - px, dy = t[i].y - py, reach = t[i].r + 6, id = c * 64 + i, k;
            if (!t[i].r || iabs(dx) > reach + 12 || iabs(dy) > reach + 12)
                continue;
            if (dx * dx + dy * dy < reach * reach) {
                if (t[i].kind == SKI_T_RAMP) {
                    if (r->state == SKI_S_SKIING && speed >= 768)
                        jump(r, 24 + speed / 64);
                } else if (t[i].kind == SKI_T_MOGUL) {
                    if (r->state == SKI_S_SKIING && speed >= 1400) {
                        jump(r, 14);
                    } else if (r->state == SKI_S_SKIING) {       /* (a bump: a little speed lost, once) */
                        for (k = 0; k < 4 && r->near[k] != id; k++)
                            ;
                        if (k == 4) {
                            r->vx -= r->vx / 6, r->vy -= r->vy / 6;
                            r->near[r->near_at++ & 3] = id;
                        }
                    }
                } else if (r->grace) {
                    /* (just up from a fall: let by) */
                } else if (r->state == SKI_S_SKIING
                           || (r->state == SKI_S_AIR && (t[i].kind == SKI_T_FIR || t[i].kind == SKI_T_PINE || t[i].kind == SKI_T_HUT))) {
                    fall(r, skimount_snow(&r->m, px, py) != SKI_SNOW_PISTE);    /* (in the air only what stands tall) */
                    return;
                }
            } else if (r->state == SKI_S_SKIING && speed >= 1024 && t[i].kind <= SKI_T_FENCE
                       && dx * dx + dy * dy < (reach + 10) * (reach + 10)) {
                for (k = 0; k < 4 && r->near[k] != id; k++)
                    ;
                if (k == 4) {           /* a near thing, once for each */
                    r->near[r->near_at++ & 3] = id;
                    r->ev |= SKI_EV_NEAR;
                    if (r->m.mode == SKI_FREE)
                        r->score += 25;
                }
            }
        }
    }
}

void skirun_step(struct skirun *r, unsigned keys)
{
    int h, ux, uy, along, sx, sy, snow, drag, grip, speed, px, py, turn;
    r->ev = 0;
    r->wind = (keys & SKI_K_WIND) != 0;
    if (r->state == SKI_S_DONE) {       /* past the finish: gliding to a stop */
        r->vx -= r->vx / 16, r->vy -= r->vy / 16;
        r->x += r->vx, r->y += r->vy;
        return;
    }
    r->steps++;
    if (r->state == SKI_S_FALLEN) {
        if (--r->down <= 0) {           /* up again, the skis down the slope */
            r->state = SKI_S_SKIING;
            r->heading = 0;
            r->vy = 128;
            r->grace = 45;
        }
        return;
    }
    turn = (keys & SKI_K_RIGHT ? 1 : 0) - (keys & SKI_K_LEFT ? 1 : 0);
    if (!turn) {
        r->turn_wait = 0;
    } else if (r->turn_wait > 0) {
        r->turn_wait--;
    } else if (r->state == SKI_S_AIR) {
        r->spin += turn;
        r->turn_wait = 2;
    } else {
        r->heading += turn;
        if (r->heading > 8)
            r->heading = 8;
        if (r->heading < -8)
            r->heading = -8;
        r->turn_wait = keys & SKI_K_TUCK ? 5 : 3;
    }
    px = r->x >> 8, py = r->y >> 8;
    if (r->state == SKI_S_AIR) {
        r->x += r->vx, r->y += r->vy;
        if (--r->air <= 0) {            /* down again */
            int part = ((r->spin % 8) + 8) % 8, halves = (r->spin + (r->spin >= 0 ? 4 : -4)) / 8;
            r->state = SKI_S_SKIING;
            r->turn_wait = 0;
            if ((part != 0 && part != 1 && part != 7) || iabs(r->heading) >= 7) {
                fall(r, skimount_snow(&r->m, r->x >> 8, r->y >> 8) != SKI_SNOW_PISTE);
            } else {
                r->ev |= SKI_EV_LAND;
                r->turns += iabs(halves);
                if (r->m.mode == SKI_FREE)
                    r->score += r->air_total * 2 + 150L * halves * halves;
            }
        }
    } else {
        h = r->heading;
        ux = h < 0 ? -SIN[-h] : SIN[h];
        uy = SIN[8 - iabs(h)];
        snow = skimount_snow(&r->m, px, py);
        along = (r->vx * ux + r->vy * uy) / 1024;       /* the speed along the skis, and what is across them */
        sx = r->vx - along * ux / 1024;
        sy = r->vy - along * uy / 1024;
        grip = snow == SKI_SNOW_ICE ? 24 : snow == SKI_SNOW_PISTE ? 4 : snow == SKI_SNOW_POWDER ? 3 : 2;
        sx = iabs(sx) < grip ? 0 : sx - sx / grip;
        sy = iabs(sy) < grip ? 0 : sy - sy / grip;
        along += PULL[r->m.grade] * uy / 1024;
        drag = snow == SKI_SNOW_DEEP ? 22 : snow == SKI_SNOW_POWDER ? 44 : (keys & SKI_K_TUCK ? 150 : 112) + (snow == SKI_SNOW_ICE ? 16 : 0);
        along -= along / drag;
        if (keys & SKI_K_PLOUGH)
            along -= along / 16 + 2;
        along -= 1;                     /* (the snow's hold, which stops skis that lie across the slope) */
        if (along < 0)
            along = 0;
        r->vx = sx + along * ux / 1024;
        r->vy = sy + along * uy / 1024;
        r->x += r->vx, r->y += r->vy;
    }
    r->x += skirun_gust(r);             /* (the wind: a push across, on the ground and in the air) */
    if (r->x < 8 * 256)                 /* (the slope's sides) */
        r->x = 8 * 256, r->vx = 0;
    if (r->x > (SKI_WORLD_W - 8) * 256)
        r->x = (SKI_WORLD_W - 8) * 256, r->vx = 0;
    if (r->y < 0)
        r->y = 0;
    {
        int a = iabs(r->vx), b = iabs(r->vy);
        speed = a > b ? a + b * 3 / 8 : b + a * 3 / 8;
    }
    if (r->grace)
        r->grace--;
    if (r->state != SKI_S_FALLEN)
        meet(r, speed);
    px = r->x >> 8, py = r->y >> 8;
    while (r->next_gate < r->m.ngates) {        /* the gates gone by in this step */
        int gx, gy;
        skimount_gate(&r->m, r->next_gate, &gx, &gy);
        if (py < gy)
            break;
        if (iabs(px - gx) <= r->m.gate_half) {
            r->gates_passed++;
            r->ev |= SKI_EV_GATE;
        } else {
            r->gates_missed++;
            r->ev |= SKI_EV_MISS;
        }
        r->next_gate++;
    }
    if (r->m.mode == SKI_FREE && r->state == SKI_S_SKIING && r->vy > 0) {      /* points for the way made without a fall */
        r->run_acc += r->vy;
        while (r->run_acc >= 100 * 256) {
            r->run_acc -= 100 * 256;
            r->score += 10 + (r->streak < 20 ? r->streak : 20);
            r->streak++;
        }
    }
    if (py >= r->m.length) {
        r->state = SKI_S_DONE;
        r->finish_steps = r->steps;
        r->ev |= SKI_EV_FINISH;
    }
}

unsigned skirun_auto(const struct skirun *r)
{
    /* tan of the angle half way between a heading and the next, in 256ths: the heading wanted is the one
       nearest the line from the skier to the clear line a little way ahead */
    static const short MID[6] = { 25, 78, 137, 210, 312, 479 };
    int px = r->x >> 8, py = r->y >> 8, a = iabs(r->vx), b = iabs(r->vy), speed = a > b ? a + b * 3 / 8 : b + a * 3 / 8;
    int ahead = 36 + speed / 40, dx = skimount_line(&r->m, py + ahead) - px, want = 0, most;
    unsigned keys = 0;
    if (r->state != SKI_S_SKIING)
        return 0;
    while (want < 6 && MID[want] * ahead / 256 < iabs(dx))
        want++;
    if (dx < 0)
        want = -want;
    if (r->heading < want)
        keys |= SKI_K_RIGHT;
    else if (r->heading > want)
        keys |= SKI_K_LEFT;
    most = (r->m.mode == SKI_SLALOM ? 1150 : 1350) - 70 * r->m.grade;  /* (slower where the line swings) */
    if (skimount_snow(&r->m, px, py + 30) == SKI_SNOW_ICE)
        most -= 250;
    if (iabs(r->heading - want) >= 2)   /* (and through a sharp turn) */
        most -= 200;
    if (speed > most)
        keys |= SKI_K_PLOUGH;
    return keys;
}
