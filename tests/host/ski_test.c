/* ski_test: the skiing game's mountains and runs (apps/shared/skimount.c, skirun.c).
 *
 *   - a mountain is its number: made twice it is the same, piece by piece in any order;
 *   - every mountain keeps to the slope, its clear line to the piste, and its things off the clear line;
 *   - there is a way down every mountain, found here without the clear line's help: over a grid of the piste,
 *     through what is left when everything on it (ramps and moguls too) is taken out with a skier's room round
 *     it; in a slalom, a way through every gate;
 *   - the careful skier (skirun_auto) gets down every mountain without a fall or a missed gate;
 *   - the motion: the fall line is fastest, a tuck faster, a plough slower, powder slow; skis across the slope
 *     stop; a ramp taken fast is a jump, a half turn in the air lands, a quarter turn falls; a tree is a fall
 *     and some seconds; a gate missed is five seconds;
 *   (ski_test alone: some hundreds of mountains, a few seconds; "ski_test more": thousands, some minutes.)
 *   - runs skied by the careful skier end at the same step with the same score on every build (the numbers
 *     here were made by a 64-bit build and hold for the 32-bit one and for DJGPP's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/skirun.h"
#include "../../apps/shared/skitable.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define CELL 8
#define GW (SKI_WORLD_W / CELL)

/* Is there a way from the start to the finish over the piste's cells that nothing is near? */
static int way_down(const struct skimount *m)
{
    int gh = (m->length + 64) / CELL + 1, x, y, c, i, n, k, found = 0;
    unsigned char *blocked = calloc((size_t)gh, GW), *seen = calloc((size_t)gh, GW);
    int *queue = malloc(sizeof(int) * (size_t)gh * GW), head = 0, tail = 0;
    struct ski_thing t[64];
    for (y = 0; y < gh; y++)
        for (x = 0; x < GW; x++) {
            int snow = skimount_snow(m, x * CELL + CELL / 2, y * CELL + CELL / 2);
            if (snow != SKI_SNOW_PISTE && snow != SKI_SNOW_ICE)
                blocked[y * GW + x] = 1;
        }
    for (c = 0; c * SKI_CHUNK < m->length + 64; c++) {
        n = skimount_chunk(m, c, t, 64);
        for (i = 0; i < n; i++) {
            int reach = t[i].r + 8;
            if (!t[i].r)
                continue;
            for (y = (t[i].y - reach) / CELL; y <= (t[i].y + reach) / CELL; y++)
                for (x = (t[i].x - reach) / CELL; x <= (t[i].x + reach) / CELL; x++)
                    if (x >= 0 && x < GW && y >= 0 && y < gh) {
                        int dx = x * CELL + CELL / 2 - t[i].x, dy = y * CELL + CELL / 2 - t[i].y;
                        if (dx * dx + dy * dy < (reach + CELL) * (reach + CELL))
                            blocked[y * GW + x] = 1;
                    }
        }
    }
    for (k = 0; k < m->ngates; k++) {   /* a gate's row: only between its poles */
        int gx, gy;
        skimount_gate(m, k, &gx, &gy);
        for (x = 0; x < GW; x++)
            if (abs(x * CELL + CELL / 2 - gx) > m->gate_half - 6)
                blocked[(gy / CELL) * GW + x] = 1;
    }
    x = skimount_centre(m, 96) / CELL, y = 96 / CELL;
    if (!blocked[y * GW + x]) {
        seen[y * GW + x] = 1;
        queue[tail++] = y * GW + x;
    }
    while (head < tail && !found) {
        int at = queue[head++], ay = at / GW, ax = at % GW, dx, dy;
        if (ay * CELL >= m->length) {
            found = 1;
            break;
        }
        for (dy = 0; dy <= 1; dy++)
            for (dx = -1; dx <= 1; dx++) {
                int nx = ax + dx, ny = ay + dy;
                if ((dx || dy) && nx >= 0 && nx < GW && ny < gh && !blocked[ny * GW + nx] && !seen[ny * GW + nx]) {
                    seen[ny * GW + nx] = 1;
                    queue[tail++] = ny * GW + nx;
                }
            }
    }
    free(blocked), free(seen), free(queue);
    return found;
}

static void run_auto_wind(struct skirun *r, unsigned seed, int grade, int mode, unsigned limit, int wind)
{
    skirun_start(r, seed, grade, mode);
    while (r->state != SKI_S_DONE && r->steps < limit)
        skirun_step(r, skirun_auto(r) | (wind ? SKI_K_WIND : 0));
}

static void run_auto(struct skirun *r, unsigned seed, int grade, int mode, unsigned limit)
{
    skirun_start(r, seed, grade, mode);
    while (r->state != SKI_S_DONE && r->steps < limit)
        skirun_step(r, skirun_auto(r));
}

static int top_speed(int grade, unsigned keys, int heading, int steps)    /* on a slope with nothing on it: by hand */
{
    struct skirun r;
    int i, best = 0;
    skirun_start(&r, 1, grade, SKI_FREE);
    r.heading = heading;
    for (i = 0; i < steps; i++) {
        r.x = skimount_centre(&r.m, 150) * 256, r.y = 150 * 256;       /* (held at one place of the start's clear snow) */
        r.state = SKI_S_SKIING;
        skirun_step(&r, keys);
        if (skirun_speed(&r) > best)
            best = skirun_speed(&r);
    }
    (void)best;
    return skirun_speed(&r);            /* (settled: its last speed) */
}

int main(int argc, char **argv)
{
    int more = argc > 1 && !strcmp(argv[1], "more");    /* (ski_test more: thousands of mountains, some minutes) */
    struct skimount m, m2;
    struct ski_thing a[64], b[64];
    struct skirun r;
    unsigned seed;
    int grade, mode, c, i, n, n2, y, bad, ways, most = 0;
    long falls = 0, missed = 0, unfinished = 0, steps_sum = 0;

    /* the same number, the same mountain: pieces made forwards and backwards */
    for (seed = 1, bad = 0; seed <= 40; seed++)
        for (grade = 0; grade < 4; grade++) {
            skimount_init(&m, seed * 7919u, grade, seed & 1);
            skimount_init(&m2, seed * 7919u, grade, seed & 1);
            for (c = 0; c < 90; c++) {
                n = skimount_chunk(&m, c, a, 64);
                n2 = skimount_chunk(&m2, 89 - c, b, 64);
                n2 = skimount_chunk(&m2, c, b, 64);
                bad += n != n2 || memcmp(a, b, sizeof *a * (size_t)n) != 0;
                if (n > most)
                    most = n;
            }
        }
    CHECK(bad == 0, "pieces differ between two makings: %d", bad);
    CHECK(most <= 48 && most >= 16, "the most things in a piece: %d", most);
    skimount_init(&m, 1, SKI_RED, SKI_FREE);
    skimount_init(&m2, 2, SKI_RED, SKI_FREE);
    n = skimount_chunk(&m, 20, a, 64), n2 = skimount_chunk(&m2, 20, b, 64);
    CHECK(n != n2 || memcmp(a, b, sizeof *a * (size_t)n), "two numbers, one mountain");
    CHECK(skimount_day(2026, 11, 21) == skimount_day(2026, 11, 21) && skimount_day(2026, 11, 21) != skimount_day(2026, 11, 22),
          "the day's number");

    /* every mountain keeps to the rules */
    for (seed = 1, bad = 0; seed <= (unsigned)(more ? 1000 : 150); seed++)
        for (grade = 0; grade < 4; grade++)
            for (mode = 0; mode < 2; mode++) {
                skimount_init(&m, seed * 2654435761u + (unsigned)grade, grade, mode);
                for (y = 0; y < m.length + 200; y += 16) {
                    int cx = skimount_centre(&m, y), half = skimount_half(&m, y), l = skimount_line(&m, y);
                    bad += cx - half < 60 || cx + half > SKI_WORLD_W - 60 || abs(l - cx) > half - 40 || half < 60;
                    bad += abs(skimount_line(&m, y + 16) - l) > 24;     /* (the line never jumps aside) */
                }
                for (c = 0; c * SKI_CHUNK < m.length + 200; c++) {
                    n = skimount_chunk(&m, c, a, 64);
                    bad += n > 48;
                    for (i = 0; i < n; i++) {
                        bad += i > 0 && a[i].y < a[i - 1].y;
                        bad += a[i].x < 0 || a[i].x >= SKI_WORLD_W || a[i].y < c * SKI_CHUNK || a[i].y >= (c + 1) * SKI_CHUNK;
                        if (a[i].r && a[i].kind != SKI_T_HUT && a[i].kind < SKI_T_GATE_L)
                            bad += abs(a[i].x - skimount_line(&m, a[i].y)) < 24 + a[i].r;
                    }
                }
                if (mode == SKI_SLALOM) {
                    int gx, gy, lx = 0;
                    bad += m.ngates < 8;
                    for (i = 0; skimount_gate(&m, i, &gx, &gy); i++) {
                        bad += gy > m.length - 250 || gx != skimount_line(&m, gy);
                        bad += i > 0 && ((gx > skimount_centre(&m, gy)) == (lx > 0));   /* side to side */
                        lx = gx - skimount_centre(&m, gy);
                        bad += abs(lx) < 10;
                    }
                    bad += i != m.ngates;
                }
            }
    CHECK(bad == 0, "mountains out of their rules: %d faults in %d mountains", bad, more ? 8000 : 1200);

    /* a way down, found without the clear line */
    for (seed = 1, ways = 0; seed <= (unsigned)(more ? 250 : 40); seed++)
        for (grade = 0; grade < 4; grade++)
            for (mode = 0; mode < 2; mode++) {
                skimount_init(&m, seed * 40503u + 17, grade, mode);
                if (way_down(&m))
                    ways++;
                else if (ways > -100)
                    printf("  no way down: seed %u grade %d mode %d\n", seed * 40503u + 17, grade, mode);
            }
    CHECK(ways == (more ? 2000 : 320), "mountains with a way down: %d of %d", ways, more ? 2000 : 320);

    /* the careful skier gets down them all */
    for (seed = 1; seed <= (unsigned)(more ? 3000 : 150); seed++)
        for (grade = 0; grade < 4; grade++)
            for (mode = 0; mode < 2; mode++) {
                run_auto(&r, seed * 97u + 5, grade, mode, 5 * 60 * SKI_STEPS);
                unfinished += r.state != SKI_S_DONE;
                falls += r.falls;
                missed += r.gates_missed;
                steps_sum += (long)r.steps;
                if (r.falls || r.gates_missed || r.state != SKI_S_DONE)
                    printf("  seed %u grade %d mode %d: falls %d missed %d state %d at y=%d\n", seed * 97u + 5, grade, mode, r.falls,
                           r.gates_missed, r.state, r.y >> 8);
            }
    CHECK(unfinished == 0 && falls == 0 && missed == 0, "the careful skier in %d runs: %ld unfinished, %ld falls, %ld gates missed",
          more ? 24000 : 1200, unfinished, falls, missed);
    printf("  the careful skier's runs: %ld seconds each on average\n", steps_sum / (more ? 24000 : 1200) / SKI_STEPS);

    /* (phase F) the other skiers: there are some (about one piece in five, where a lane is clear); each waits until
       the skier is near, goes down its lane slowly and stops at its end (those near the valley may not have got
       there when the run ends); its lane is on the piste, off the clear
       line and clear of things all the way; skied into, it is a fall */
    {
        long count = 0, mountains = 0, waited = 0, set_off = 0, stopped = 0, too_near = 0, off_piste = 0, hits = 0;
        for (seed = 1; seed <= 120; seed++) {
            int c, x, y, moving, heading, x2, y2;
            run_auto(&r, seed * 131u + 7, (int)(seed % 4), (int)(seed & 1), 5 * 60 * SKI_STEPS);
            mountains++;
            for (c = 0; c < SKI_OTHERS; c++) {
                const struct ski_thing *t;
                int k, n, cc;
                if (!skirun_other(&r, c, &x, &y, &moving, &heading))
                    continue;
                count++;
                set_off += r.other[c].start != 0;
                stopped += r.other[c].start && !moving && y == r.other[c].y0 + 320;
                {
                    int d = x - skimount_line(&r.m, y), e = x - skimount_centre(&r.m, y);
                    too_near += (d < 0 ? -d : d) < 34;
                    off_piste += (e < 0 ? -e : e) > skimount_half(&r.m, y);
                }
                for (cc = (y - 40) / SKI_CHUNK; cc <= (y + 40) / SKI_CHUNK; cc++)  /* (where it stopped: clear) */
                    for (t = skirun_piece(&r, cc < 0 ? 0 : cc, &n), k = 0; k < n; k++) {
                        int dx = t[k].x - x, dy = t[k].y - y, reach = (t[k].r ? t[k].r : 5) + 6;
                        hits += dx * dx + dy * dy < reach * reach;
                    }
            }
            {                                   /* a fresh run: none has set off; the first one skied into */
                struct skirun f;
                skirun_start(&f, seed * 131u + 7, (int)(seed % 4), (int)(seed & 1));
                for (c = 0; c < SKI_OTHERS && !skirun_other(&f, c, &x2, &y2, &moving, &heading); c++)
                    ;
                if (c < SKI_OTHERS) {
                    waited += !moving && !f.other[c].start;
                    f.x = x2 * 256, f.y = (y2 - 2) * 256, f.vx = 0, f.vy = 64, f.state = SKI_S_SKIING, f.grace = 0;
                    skirun_step(&f, 0);
                    CHECK(f.state == SKI_S_FALLEN && f.falls == 1, "seed %u: skied into another skier, no fall", seed);
                }
            }
        }
        printf("  other skiers: %ld on %ld mountains, %ld set off, %ld stopped at their lanes' ends\n", count, mountains,
               set_off, stopped);
        CHECK(count >= mountains * 2 && set_off == count && stopped >= count * 8 / 10 && too_near == 0 && off_piste == 0 && hits == 0,
              "other skiers: %ld, %ld set off, %ld stopped, %ld near the line, %ld off the piste, %ld in things", count,
              set_off, stopped, too_near, off_piste, hits);
        CHECK(waited >= mountains / 2, "other skiers waiting at the start: %ld", waited);
    }
    /* (phase F) the groomers: some; each sets off as the skier comes near and parks beyond the far edge, having
       crossed the clear line (those near the valley may still be crossing when the run ends); skied into, a fall;
       flown over in a jump, nothing */
    {
        long count = 0, parked = 0, crossed = 0, hit = 0, flown = 0, tried = 0;
        for (seed = 1; seed <= 120; seed++) {
            int c, x, y, dir, moving, done;
            run_auto(&r, seed * 131u + 7, (int)(seed % 4), (int)(seed & 1), 5 * 60 * SKI_STEPS);
            for (c = 0; c < SKI_OTHERS; c++) {
                if (!skirun_groomer(&r, c, &x, &y, &dir, &moving, &done))
                    continue;
                count++;
                parked += r.groomer[c].start && !moving && done == r.groomer[c].travel;
                crossed += (x - skimount_line(&r.m, y)) * dir > 0;
            }
            {
                struct skirun f;
                skirun_start(&f, seed * 131u + 7, (int)(seed % 4), (int)(seed & 1));
                for (c = 0; c < SKI_OTHERS && !skirun_groomer(&f, c, &x, &y, &dir, &moving, &done); c++)
                    ;
                if (c < SKI_OTHERS) {
                    struct skirun g;
                    tried++;                    /* (on the piste: set off, half way across) */
                    f.groomer[c].start = 1, f.steps = 1 + (unsigned)f.groomer[c].travel / 2 * 256 / 77;
                    skirun_groomer(&f, c, &x, &y, &dir, &moving, &done);
                    f.x = x * 256, f.y = (y - 4) * 256, f.vx = 0, f.vy = 64, f.state = SKI_S_SKIING, f.grace = 0;
                    g = f;
                    skirun_step(&f, 0);
                    hit += f.state == SKI_S_FALLEN;
                    g.state = SKI_S_AIR, g.air = g.air_total = 20;
                    skirun_step(&g, 0);
                    flown += g.falls == 0;
                }
            }
        }
        printf("  groomers: %ld on 120 mountains, %ld parked, %ld past the line\n", count, parked, crossed);
        CHECK(count >= 120 && parked >= count / 2 && crossed >= parked && hit == tried && flown == tried,
              "groomers: %ld, %ld parked, %ld past the line; %ld of %ld skied into a fall, %ld flown over", count, parked,
              crossed, hit, tried, flown);
    }

    /* (phase F) in the wind: the gusts blow by rule (a run skied twice is the same run), they push (the run is
       not the calm one's), they blow both ways and calm between, and the careful skier still gets down */
    {
        struct skirun w2;
        long wfalls = 0, wmissed = 0, wunfinished = 0, differ = 0, same = 0, left = 0, right = 0, calm = 0;
        for (seed = 1; seed <= (unsigned)(more ? 3000 : 150); seed++)
            for (grade = 0; grade < 4; grade++)
                for (mode = 0; mode < 2; mode++) {
                    run_auto_wind(&r, seed * 97u + 5, grade, mode, 5 * 60 * SKI_STEPS, 1);
                    wunfinished += r.state != SKI_S_DONE;
                    wfalls += r.falls;
                    wmissed += r.gates_missed;
                    if (r.falls || r.gates_missed || r.state != SKI_S_DONE)
                        printf("  wind: seed %u grade %d mode %d: falls %d missed %d state %d at y=%d\n", seed * 97u + 5, grade,
                               mode, r.falls, r.gates_missed, r.state, r.y >> 8);
                    if (seed <= 20) {
                        run_auto_wind(&w2, seed * 97u + 5, grade, mode, 5 * 60 * SKI_STEPS, 1);
                        same += w2.steps == r.steps && w2.x == r.x && w2.score == r.score;
                        run_auto_wind(&w2, seed * 97u + 5, grade, mode, 5 * 60 * SKI_STEPS, 0);
                        differ += w2.steps != r.steps || w2.x != r.x;
                    }
                }
        for (i = 0; i < 6000; i++) {            /* (the gusts of one windy run's first hundred seconds) */
            int g;
            skirun_start(&r, 12345, SKI_RED, SKI_FREE);
            r.wind = 1, r.steps = (unsigned)i;
            g = skirun_gust(&r);
            left += g < 0, right += g > 0, calm += g == 0;
            if (g > 128 || g < -128)
                CHECK(0, "a red run's gust of %d at step %d", g, i);
        }
        CHECK(wunfinished == 0 && wfalls == 0 && wmissed == 0, "the careful skier in the wind: %ld unfinished, %ld falls, %ld gates missed",
              wunfinished, wfalls, wmissed);
        CHECK(same == 160 && differ >= 150, "windy runs: %ld of 160 the same twice, %ld of 160 not the calm run", same, differ);
        CHECK(left > 600 && right > 600 && calm > 2400, "the gusts: %ld left, %ld right, %ld calm steps of 6000", left, right, calm);
    }

    /* the motion */
    {
        int plain = top_speed(SKI_RED, 0, 0, 900), tuck = top_speed(SKI_RED, SKI_K_TUCK, 0, 900);
        int plough = top_speed(SKI_RED, SKI_K_PLOUGH, 0, 900), aside = top_speed(SKI_RED, 0, 4, 900);
        int across = top_speed(SKI_RED, 0, 8, 900), green = top_speed(SKI_GREEN, 0, 0, 900), black = top_speed(SKI_BLACK, 0, 0, 900);
        CHECK(plain > 330 && plain < 400, "the fall line on a red run: %d pixels a second", plain);
        CHECK(tuck > plain + 60 && plough < plain / 2 && plough > 20, "a tuck %d, a plough %d", tuck, plough);
        CHECK(aside < plain && aside > plain * 2 / 5 && across == 0, "half aside %d, across %d", aside, across);
        CHECK(green < plain && plain < black, "by grade: %d %d %d", green, plain, black);
    }
    {
        /* powder at the piste's side is slow; the forest's snow slower */
        struct skirun p;
        int v_powder, v_deep, k;
        skirun_start(&p, 1, SKI_RED, SKI_FREE);
        for (k = 0; k < 600; k++) {
            p.x = (skimount_centre(&p.m, 150) + skimount_half(&p.m, 150) + 18) * 256, p.y = 150 * 256, p.state = SKI_S_SKIING;
            skirun_step(&p, 0);
        }
        v_powder = skirun_speed(&p);
        for (k = 0; k < 600; k++) {
            p.x = 20 * 256, p.y = 1200 * 256, p.state = SKI_S_SKIING, p.falls = 0;
            skirun_step(&p, 0);
        }
        v_deep = skirun_speed(&p);
        CHECK(v_powder > 100 && v_powder < 220 && v_deep < v_powder && v_deep > 30, "powder %d, the forest's snow %d", v_powder, v_deep);
    }
    {
        /* a ramp, a tree, and what a turn in the air does: on a mountain's own things */
        struct skirun j;
        int found_ramp = 0, found_tree = 0, k, turns;
        skimount_init(&m, 4242, SKI_RED, SKI_FREE);
        for (c = 2; c < 70 && !(found_ramp && found_tree); c++) {
            n = skimount_chunk(&m, c, a, 64);
            for (i = 0; i < n; i++) {
                if (a[i].kind == SKI_T_RAMP && !found_ramp) {
                    for (turns = 0; turns <= 3; turns++) {      /* no turn, a half turn, a quarter (a fall), a whole one */
                        static const int spins[4] = { 0, 8, 4, 16 };
                        skirun_start(&j, 4242, SKI_RED, SKI_FREE);
                        j.x = a[i].x * 256, j.y = (a[i].y - 30) * 256, j.vy = 1500, j.vx = 0;
                        for (k = 0; k < 40 && j.state == SKI_S_SKIING; k++)
                            skirun_step(&j, 0);
                        CHECK(j.state == SKI_S_AIR && j.jumps == 1 && j.air_total > 40, "the ramp: state %d air %d", j.state, j.air_total);
                        CHECK(skirun_height(&j) >= 0, "height");
                        j.vx = j.vy = 0;                        /* (held over the ramp: nothing else to land on) */
                        j.spin = spins[turns];
                        for (k = 0; k < 200 && j.state == SKI_S_AIR; k++)
                            skirun_step(&j, 0);
                        if (turns == 2)
                            CHECK(j.state == SKI_S_FALLEN && j.falls == 1, "a quarter turn landed: state %d", j.state);
                        else
                            CHECK(j.state == SKI_S_SKIING && j.falls == 0 && j.turns == spins[turns] / 8
                                  && j.score >= 150L * (spins[turns] / 8) * (spins[turns] / 8), "a turn of %d sixteenths: state %d turns %d score %ld",
                                  spins[turns], j.state, j.turns, j.score);
                    }
                    /* too slow for it: skied over */
                    skirun_start(&j, 4242, SKI_RED, SKI_FREE);
                    j.x = a[i].x * 256, j.y = (a[i].y - 12) * 256, j.vy = 300;
                    for (k = 0; k < 10; k++)
                        skirun_step(&j, SKI_K_PLOUGH);
                    CHECK(j.jumps == 0 && j.falls == 0, "a ramp taken slowly");
                    found_ramp = 1;
                }
                if (a[i].kind == SKI_T_ROCK && !found_tree) {
                    unsigned before;
                    skirun_start(&j, 4242, SKI_RED, SKI_FREE);
                    j.x = a[i].x * 256, j.y = (a[i].y - 40) * 256, j.vy = 1200;
                    for (k = 0; k < 60 && j.state == SKI_S_SKIING; k++)
                        skirun_step(&j, 0);
                    CHECK(j.state == SKI_S_FALLEN && j.falls == 1 && (j.ev & SKI_EV_FALL), "a rock: state %d", j.state);
                    before = j.steps;
                    for (k = 0; k < 200 && j.state == SKI_S_FALLEN; k++)
                        skirun_step(&j, SKI_K_LEFT);
                    CHECK(j.state == SKI_S_SKIING && j.steps - before == 90 && j.heading == 0, "up again after %u steps", j.steps - before);
                    found_tree = 1;
                }
            }
        }
        CHECK(found_ramp && found_tree, "the mountain's ramp and rock: %d %d", found_ramp, found_tree);
    }
    {
        /* gates: straight down the piste's middle misses those at the sides; each is five seconds */
        struct skirun g;
        unsigned long plain_ms;
        skirun_start(&g, 99, SKI_GREEN, SKI_SLALOM);
        while (g.state != SKI_S_DONE && g.steps < 20000) {
            unsigned keys = skirun_auto(&g);
            skirun_step(&g, keys);
        }
        CHECK(g.state == SKI_S_DONE && g.gates_missed == 0 && g.gates_passed == g.m.ngates, "every gate: %d of %d", g.gates_passed, g.m.ngates);
        plain_ms = (unsigned long)g.finish_steps * 1000 / SKI_STEPS;
        CHECK(skirun_ms(&g) == plain_ms, "no gate missed, no seconds added");
        g.gates_missed = 3;
        CHECK(skirun_ms(&g) == plain_ms + 15000, "three gates missed: fifteen seconds");
        skirun_start(&g, 99, SKI_GREEN, SKI_SLALOM);
        for (i = 0; i < 3000 && g.next_gate < 1; i++) {         /* down the far side from the first gate */
            int gx, gy;
            skimount_gate(&g.m, 0, &gx, &gy);
            g.x = (2 * skimount_centre(&g.m, g.y >> 8) - gx) * 256;
            g.state = SKI_S_SKIING;
            skirun_step(&g, SKI_K_PLOUGH);
        }
        CHECK(g.gates_missed == 1 && g.gates_passed == 0 && (g.ev & SKI_EV_MISS), "a gate missed: %d", g.gates_missed);
    }

    /* runs that end the same on every build */
    {
        static const struct { unsigned seed; int grade, mode; unsigned steps; long score; int jumps; } gold[] = {
            { 7, SKI_GREEN, SKI_SLALOM, 2173, 0, 0 },
            { 7, SKI_BLACK, SKI_SLALOM, 3319, 0, 0 },
            { 20261121, SKI_RED, SKI_FREE, 2342, 2520, 0 },
            { 123456789, SKI_BLUE, SKI_FREE, 2083, 2220, 0 },
        };
        for (i = 0; i < 4; i++) {
            run_auto(&r, gold[i].seed, gold[i].grade, gold[i].mode, 5 * 60 * SKI_STEPS);
            CHECK(r.finish_steps == gold[i].steps && r.score == gold[i].score && r.jumps == gold[i].jumps, "run %u: steps %u score %ld",
                      gold[i].seed, r.finish_steps, r.score);
        }
    }
    /* the tables: ten of each, the best first; written and read again */
    {
        struct ski_tables t, back;
        struct ski_entry e;
        FILE *f;
        skitables_init(&t);
        memset(&e, 0, sizeof e);
        for (i = 0; i < 14; i++) {      /* times in no order: 50 s, 47, 44 ... and 51, 54 ... */
            int place;
            e.ms = i < 7 ? 50000 - 3000ul * (unsigned)i : 30000 + 3000ul * (unsigned)i;
            e.score = 100 * i;
            e.seed = 1000u + (unsigned)i;
            e.day = 20261121;
            snprintf(e.name, sizeof e.name, "Skier %d", i);
            place = skitable_place(&t.grade[SKI_SLALOM][SKI_RED], SKI_SLALOM, e.ms, e.score);
            CHECK(skitable_add(&t.grade[SKI_SLALOM][SKI_RED], SKI_SLALOM, &e) == place, "placed where it was said: run %d", i);
            skitable_add(&t.grade[SKI_FREE][SKI_BLUE], SKI_FREE, &e);
        }
        for (i = 1, bad = 0; i < 10; i++) {
            bad += t.grade[SKI_SLALOM][SKI_RED].e[i].ms < t.grade[SKI_SLALOM][SKI_RED].e[i - 1].ms;
            bad += t.grade[SKI_FREE][SKI_BLUE].e[i].score > t.grade[SKI_FREE][SKI_BLUE].e[i - 1].score;
        }
        CHECK(bad == 0 && t.grade[SKI_SLALOM][SKI_RED].n == 10 && t.grade[SKI_SLALOM][SKI_RED].e[0].ms == 32000
              && !strcmp(t.grade[SKI_SLALOM][SKI_RED].e[0].name, "Skier 6") && t.grade[SKI_FREE][SKI_BLUE].e[0].score == 1300
              && t.grade[SKI_FREE][SKI_BLUE].e[9].score == 400, "the tables' order: %d faults, the best %lu", bad, t.grade[SKI_SLALOM][SKI_RED].e[0].ms);
        CHECK(skitable_place(&t.grade[SKI_SLALOM][SKI_RED], SKI_SLALOM, 99000, 0) == -1
              && skitable_place(&t.grade[SKI_SLALOM][SKI_RED], SKI_SLALOM, 32000, 0) == 1
              && skitable_place(&t.grade[SKI_SLALOM][SKI_GREEN], SKI_SLALOM, 99000, 0) == 0, "places: too slow, a tie, an empty table");
        skitables_today(&t, 20261121);
        snprintf(t.player, sizeof t.player, "Ada of the Alps");
        t.grade_last = SKI_BLACK, t.mode_last = SKI_FREE, t.jacket = 3, t.figure = 2, t.weather = 3;
        snprintf(e.name, sizeof e.name, "A name with blanks");
        e.ms = 41234, e.score = 0;
        skitable_add(&t.day[SKI_SLALOM], SKI_SLALOM, &e);
        f = tmpfile();
        CHECK(f && skitables_write(&t, f) == 0, "written");
        rewind(f);
        CHECK(skitables_read(&back, f) == 21 && !memcmp(&back, &t, sizeof t), "read again the same");
        fclose(f);
        skitables_today(&back, 20261121);
        CHECK(back.day[SKI_SLALOM].n == 1 && !strcmp(back.day[SKI_SLALOM].e[0].name, "A name with blanks"), "the day's table kept that day");
        skitables_today(&back, 20261122);
        CHECK(back.day[SKI_SLALOM].n == 0 && back.grade[SKI_SLALOM][SKI_RED].n == 10, "and empty the next");
        f = tmpfile();
        fputs("not a table\nT 0 0 1 1 1 1 x\n", f);
        rewind(f);
        CHECK(skitables_read(&back, f) == 0, "a file that is no table");
        fclose(f);
        f = tmpfile();
        fputs("SKI 1\nT 9 9 1 1 1 1 x\nrubbish\nT 0 1 5000 0 7 20261121 Good One\nT 0 1\n", f);
        rewind(f);
        CHECK(skitables_read(&back, f) == 1 && back.grade[0][1].n == 1 && !strcmp(back.grade[0][1].e[0].name, "Good One"), "lines out of order passed over");
        fclose(f);
    }
    printf("ski_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
