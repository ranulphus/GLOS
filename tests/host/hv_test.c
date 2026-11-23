/* hv_test: the hovercraft game's arenas (apps/shared/hvarena.c): a file's letters read; where a craft can go
 * from square to square (level floor, a drop, up a ramp, never up a step, never into a wall or water); how far
 * each square is from a place; and each fault a file can have, refused with its line. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/hvarenas.h"
#include "../../apps/shared/hvbasin.h"
#include "../../apps/shared/hvgame.h"
#include "../../apps/shared/hvnet.h"

static int fails, checks;
static int iabs(int v) { return v < 0 ? -v : v; }
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const char GOOD[] =
    "name: The Test Basin\n"
    "light: dusk\n"
    "\n"
    "##############\n"          /* line 4, row 0 */
    "#A....11....B#\n"
    "#A.a.>11<.b.B#\n"
    "#..*..~~..*..#\n"
    "#.....!..c...#\n"
    "##############\n";

static int refused(const char *text, const char *wanted)       /* 1 if it is refused with those words */
{
    struct hv_arena a;
    char why[160] = "";
    int r = hv_arena_read(&a, text, strlen(text), why, sizeof why);
    if (r != -1 || !strstr(why, wanted))
        printf("  read %d, said \"%s\" (wanted \"%s\")\n", r, why, wanted);
    return r == -1 && strstr(why, wanted) != NULL;
}

/* ---- the game's truth (apps/shared/hvgame.c): the motion, the arena's edges, bumps, the four kinds of game, the
   computer's driving. */

static const char YARD[] =
    "name: The Test Yard\n"
    "\n"
    "####################\n"
    "#A.......11.......B#\n"
    "#A....a.>11<.b....B#\n"
    "#...*....~~....*...#\n"
    "#........!.........#\n"
    "#...*..........*...#\n"
    "#.....d......c.....#\n"
    "####################\n";

static struct hv_arena yard;
static struct hv_game g, g2;
static unsigned long seed = 99;

static unsigned rnd(void)
{
    seed = seed * 1103515245u + 12345u;
    return (unsigned)(seed >> 16) & 0x7FFF;
}

static void steps(struct hv_game *game, int n, unsigned k0)    /* craft 0 holds k0, the others nothing */
{
    unsigned char k[HV_CRAFT] = { 0 };
    k[0] = (unsigned char)k0;
    while (n-- > 0)
        hv_game_step(game, k);
}

static int speed(const struct hv_craft *c)      /* 1/65536 square a step */
{
    long long v = (long long)c->vx * c->vx + (long long)c->vy * c->vy;
    int r = 0;
    while ((long long)(r + 64) * (r + 64) <= v)
        r += 64;
    return r;
}

static void place(struct hv_game *game, int i, int sqx, int sqy, int heading)  /* at a square's middle, still */
{
    game->c[i].x = sqx * 256 + 128, game->c[i].y = sqy * 256 + 128;
    game->c[i].vx = game->c[i].vy = 0;
    game->c[i].heading = heading;
    game->c[i].wet = 0;
}

static void game_tests(void)
{
    char why[160];
    int i, n, bad, top, boosted, k;
    CHECK(hv_arena_read(&yard, YARD, strlen(YARD), why, sizeof why) == 0, "the yard: %s", why);
    CHECK(hv_sin(0) == 0 && hv_sin(256) == 16384 && hv_sin(512) == 0 && hv_sin(768) == -16384 && hv_cos(0) == 16384 && hv_cos(512) == -16384
          && hv_sin(128) > 11500 && hv_sin(128) < 11650 && hv_sin(1024 + 64) == hv_sin(64) && hv_sin(-64) == -hv_sin(64), "the sines");
    CHECK(hv_game_start(&g, &yard, HV_FLAGS, 0) == -1 && hv_game_start(&g, &yard, HV_FLAGS, 9) == -1 && hv_game_start(&g, &yard, 7, 2) == -1,
          "games that cannot be");

    /* the motion: thrust, the long slide after it, set down, the boost and its tank */
    hv_game_start(&g, &yard, HV_LAPS, 1);
    place(&g, 0, 2, 4, 256);                    /* in the open, facing right */
    steps(&g, 60, HV_C_FWD);
    top = speed(&g.c[0]);
    CHECK(top > 11000 && top < 14500 && g.c[0].vy == 0 && g.c[0].x > 2 * 256 + 128 + 256 * 5, "thrust: speed %d, x %d", top, g.c[0].x);
    steps(&g, 15, 0);
    CHECK(speed(&g.c[0]) > top / 3 && speed(&g.c[0]) < top * 3 / 4, "half a second with no fan: still %d of %d", speed(&g.c[0]), top);
    steps(&g, 8, HV_C_DOWN);
    CHECK(speed(&g.c[0]) < top / 20, "set down for a quarter of a second: %d", speed(&g.c[0]));
    place(&g, 0, 2, 4, 256);
    steps(&g, 40, HV_C_FWD | HV_C_BOOST);
    boosted = speed(&g.c[0]);
    CHECK(boosted > top * 2 && g.c[0].tank == HV_TANK - 40, "the boost: %d against %d, tank %d", boosted, top, g.c[0].tank);
    place(&g, 0, 2, 4, 256);
    steps(&g, 400, 0);
    CHECK(g.c[0].tank == HV_TANK, "the tank full again: %d", g.c[0].tank);
    place(&g, 0, 6, 4, 0);
    steps(&g, 16, HV_C_RIGHT);
    CHECK(g.c[0].heading == 256, "a quarter turn in sixteen steps: %d", g.c[0].heading);
    steps(&g, 30, HV_C_SLIDE_R);                /* facing right, the side fan pushes it down the grid */
    CHECK(g.c[0].vy > 4000 && iabs(g.c[0].vx) < 200, "the side fan: %d %d", g.c[0].vx, g.c[0].vy);
    place(&g, 0, 10, 5, 256);
    steps(&g, 30, HV_C_BACK);
    CHECK(g.c[0].vx < -3000 && g.c[0].vx > -top, "backwards, and more slowly: %d", g.c[0].vx);

    /* the arena's edges */
    place(&g, 0, 2, 1, 0);                      /* against the top wall */
    steps(&g, 60, HV_C_FWD);
    CHECK(g.c[0].y >= 256 + HV_RADIUS && g.c[0].y < 256 + HV_RADIUS + 40 && (g.c[0].y >> 8) == 1, "a wall holds it: y %d", g.c[0].y);
    place(&g, 0, 6, 1, 256);                    /* at the quay's foot: a step up */
    steps(&g, 60, HV_C_FWD | HV_C_BOOST);
    CHECK((g.c[0].x >> 8) == 8 && hv_craft_level(&g, 0) == 0, "not up a step: x %d", g.c[0].x);
    place(&g, 0, 6, 2, 256);                    /* ... but up the ramp beside it */
    for (n = 0; n < 60 && (g.c[0].x >> 8) < 8; n++)
        steps(&g, 1, HV_C_FWD);
    steps(&g, 2, HV_C_FWD);
    CHECK(g.c[0].x >> 8 == 8 && hv_craft_level(&g, 0) > 0 && hv_craft_level(&g, 0) < 256, "on the ramp: x %d level %d", g.c[0].x,
          hv_craft_level(&g, 0));
    for (n = 0; n < 60 && (g.c[0].x >> 8) < 9; n++)
        steps(&g, 1, HV_C_FWD);
    CHECK(hv_craft_level(&g, 0) == 256 && (g.c[0].x >> 8) == 9, "on the quay: x %d level %d", g.c[0].x, hv_craft_level(&g, 0));
    place(&g, 0, 9, 1, 768);                    /* off the quay's edge: a drop */
    steps(&g, 30, HV_C_FWD);
    CHECK((g.c[0].x >> 8) < 9 && hv_craft_level(&g, 0) == 0, "down off the quay: x %d", g.c[0].x);
    place(&g, 0, 8, 4, 256);                    /* into the water: three seconds, then the dock */
    g.c[0].heading = 256;
    g.c[0].x = 9 * 256 + 128, g.c[0].y = 4 * 256 + 128, g.c[0].heading = 0;
    steps(&g, 20, HV_C_FWD);
    CHECK(g.c[0].wet > 0 && (g.ev & HV_EV_SPLASH) == 0, "in the water: wet %d", g.c[0].wet);
    n = g.c[0].wet;
    steps(&g, n, 0);
    CHECK(g.c[0].wet == 0 && (g.c[0].x >> 8) == 1 && (g.c[0].y >> 8) == 1 && g.c[0].heading == 256, "back at its dock, facing in: %d,%d",
          g.c[0].x >> 8, g.c[0].y >> 8);

    /* never in a wall, whatever is held: eight craft, random fans, ten thousand steps */
    hv_game_start(&g, &yard, HV_LAMP, 8);
    for (n = 0, bad = 0; n < 10000; n++) {
        unsigned char keys[HV_CRAFT];
        for (i = 0; i < 8; i++)
            keys[i] = (unsigned char)((n / 16 + i * 7) % 5 == 0 ? rnd() & 0x7F : keys[i] * (n > 0));
        hv_game_step(&g, keys);
        for (i = 0; i < 8; i++) {
            const struct hv_craft *c = &g.c[i];
            int kind = yard.sq[c->y >> 8][c->x >> 8].kind;
            bad += c->x < 0 || c->y < 0 || c->x >= yard.w * 256 || c->y >= yard.h * 256 || kind == HV_WALL || (kind == HV_WATER && !c->wet);
        }
    }
    CHECK(bad == 0, "craft in walls or off the grid: %d", bad);

    /* bumps: two craft head on change places in speed; one set down is a rock */
    hv_game_start(&g, &yard, HV_LAPS, 2);
    place(&g, 0, 4, 5, 256), place(&g, 1, 6, 5, 768);
    g.c[0].vx = 9000, g.c[1].vx = -3000;
    for (n = 0; n < 40 && !(g.ev & HV_EV_BUMP); n++)
        steps(&g, 1, 0);
    CHECK((g.ev & HV_EV_BUMP) && g.c[0].vx < 0 && g.c[1].vx > 3000 && g.c[1].vx > -g.c[0].vx * 3, "head on: %d and %d after", g.c[0].vx,
          g.c[1].vx);
    place(&g, 0, 4, 5, 256), place(&g, 1, 6, 5, 768);
    g.c[0].vx = 9000;
    for (n = 0, k = 0; n < 40; n++) {
        unsigned char keys[HV_CRAFT] = { 0, HV_C_DOWN };
        hv_game_step(&g, keys);
        k |= (g.ev & HV_EV_BUMP) != 0;
    }
    CHECK(k && g.c[1].x == 6 * 256 + 128 && g.c[1].vx == 0 && g.c[0].x < 6 * 256 + 128 - 2 * HV_RADIUS + 4, "a craft set down is not moved: %d",
          g.c[1].x);

    /* flags: taken, carried home, the point; a hard bump knocks it loose; its own side sends it home */
    hv_game_start(&g, &yard, HV_FLAGS, 3);      /* (craft 0 and 2 are A's, craft 1 is B's) */
    place(&g, 1, 15, 6, 0);                     /* (B's craft out of the way; set down on the flag it would not keep it) */
    place(&g, 2, 3, 6, 0);
    place(&g, 0, 17, 1, 768);                   /* A's craft by B's flag (B's dock is at 18,1) */
    steps(&g, 30, HV_C_BACK);
    CHECK(g.c[0].carrying && g.flag[1].holder == 0, "the flag taken: %d", g.flag[1].holder);
    place(&g, 0, 2, 1, 768);
    steps(&g, 30, HV_C_FWD);
    CHECK(g.score[0] == 1 && !g.c[0].carrying && g.flag[1].holder == -1 && (g.flag[1].x >> 8) == 18, "brought home: %d", g.score[0]);
    place(&g, 0, 17, 1, 768);
    steps(&g, 30, HV_C_BACK);
    CHECK(g.c[0].carrying, "taken again");
    place(&g, 0, 10, 5, 256), place(&g, 2, 12, 5, 768);         /* its own side's craft runs into it, hard */
    g.c[2].vx = -12000;
    for (n = 0; n < 30 && g.c[0].carrying; n++)
        steps(&g, 1, 0);
    CHECK(!g.c[0].carrying && g.c[2].carrying && g.flag[1].holder == 2 && (g.ev & HV_EV_DROP) && (g.ev & HV_EV_TAKEN),
          "knocked loose by its own side: the one that bumped has it (%d)", g.flag[1].holder);
    /* ... and the craft whose flag it is, bumping the carrier hard, has it home at once */
    place(&g, 0, 2, 5, 0), place(&g, 2, 10, 5, 256), place(&g, 1, 12, 5, 768);
    g.c[1].vx = -12000;
    for (n = 0; n < 30 && g.c[2].carrying; n++)
        steps(&g, 1, 0);
    CHECK(!g.c[2].carrying && g.flag[1].holder == -1 && g.flag[1].away == 0 && (g.flag[1].x >> 8) == 18, "bumped by the flag's own side: home");
    /* a soft bump does nothing to it; and a carrier set down is a rock: it keeps the flag */
    place(&g, 1, 15, 6, 0), place(&g, 2, 3, 6, 0), place(&g, 0, 17, 1, 768);
    steps(&g, 30, HV_C_BACK);
    place(&g, 0, 10, 5, 256), place(&g, 1, 12, 5, 768);
    g.c[1].vx = -3000;
    steps(&g, 40, 0);
    CHECK(g.c[0].carrying && g.flag[1].holder == 0, "a soft bump: kept");
    place(&g, 0, 10, 5, 256), place(&g, 1, 12, 5, 768);
    g.c[1].vx = -12000;
    steps(&g, 30, HV_C_DOWN);
    CHECK(g.c[0].carrying && g.flag[1].holder == 0, "set down, the carrier keeps it");
    /* a carrier in the water: the flag is home, the craft at its dock three seconds later */
    place(&g, 0, 9, 4, 0);
    steps(&g, 20, HV_C_FWD);
    CHECK(g.c[0].wet && !g.c[0].carrying && g.flag[1].holder == -1 && g.flag[1].away == 0 && (g.flag[1].x >> 8) == 18, "the carrier in the water");

    /* beacons: turned by a touch; the side with more of them scores by the second */
    hv_game_start(&g, &yard, HV_BEACONS, 2);
    place(&g, 0, 4, 3, 0), place(&g, 1, 10, 6, 0);
    steps(&g, 95, 0);
    CHECK(g.beacon[0] == 0 && g.beacon[1] == -1 && g.score[0] == 3 && g.score[1] == 0, "a beacon held three seconds: %d %d", g.score[0], g.score[1]);

    /* the lamp: taken, a point a second, a bump takes it */
    hv_game_start(&g, &yard, HV_LAMP, 2);
    place(&g, 0, 9, 4, 0), place(&g, 1, 2, 6, 0);
    steps(&g, 65, 0);
    CHECK(g.lamp_holder == 0 && g.c[0].score == 2, "the lamp held: %d, %ld", g.lamp_holder, g.c[0].score);
    place(&g, 1, 11, 4, 768);
    g.c[1].vx = -9000;
    for (n = 0; n < 30 && g.lamp_holder == 0; n++)
        steps(&g, 1, 0);
    CHECK(g.lamp_holder == 1 && g.c[1].carrying && !g.c[0].carrying, "a bump takes the lamp: %d", g.lamp_holder);

    /* laps: the buoys in their order, and no other */
    hv_game_start(&g, &yard, HV_LAPS, 1);
    place(&g, 0, 13, 2, 0);                     /* b before a: nothing */
    steps(&g, 3, 0);
    CHECK(g.c[0].buoy == 0, "a buoy out of its turn");
    {
        static const int bx[4] = { 6, 13, 13, 6 }, by[4] = { 2, 2, 6, 6 };
        for (k = 0; k < 4; k++) {
            place(&g, 0, bx[k], by[k], 0);
            steps(&g, 10, 0);
        }
    }
    CHECK(g.c[0].score == 1 && g.c[0].buoy == 0 && g.c[0].best_lap > 0 && g.c[0].best_lap < 60, "a lap: %ld in %u steps", g.c[0].score,
          g.c[0].best_lap);

    /* the computer's craft, against one that stands still, score in every kind; and a game is its controls: twice
       the same */
    for (k = HV_FLAGS; k <= HV_LAPS; k++) {
        int strength;
        for (strength = 0; strength < 3; strength++) {
            long points;
            hv_game_start(&g, &yard, k, 4);
            hv_game_start(&g2, &yard, k, 4);
            for (n = 0; n < 3 * 60 * HV_STEPS; n++) {
                unsigned char keys[HV_CRAFT] = { 0 };
                for (i = 0; i < 4; i++)
                    if (i != 1)
                        keys[i] = (unsigned char)hv_auto(&g, i, strength);
                hv_game_step(&g, keys);
                for (i = 0; i < 4; i++)
                    if (i != 1)
                        keys[i] = (unsigned char)hv_auto(&g2, i, strength);
                hv_game_step(&g2, keys);
            }
            points = k == HV_FLAGS || k == HV_BEACONS ? g.score[0] : g.c[0].score + g.c[2].score + g.c[3].score;
            CHECK(points >= (k == HV_FLAGS ? 1 : 4), "kind %d strength %d: %ld points in three minutes", k, strength, points);
            CHECK(!memcmp(g.c, g2.c, sizeof g.c) && !memcmp(g.score, g2.score, sizeof g.score) && g.steps == g2.steps,
                  "kind %d strength %d: the same game twice", k, strength);
            printf("  kind %d strength %d: sides %d %d, craft %ld %ld %ld\n", k, strength, g.score[0], g.score[1], g.c[0].score, g.c[2].score,
                   g.c[3].score);
        }
    }
}

/* ---- the game over a network (apps/shared/hvnet.c): an opener and two joiners in this one program, and between
   them a network that loses datagrams, sends some twice, and delivers them late and out of order. */

struct wire {                           /* a datagram on its way */
    unsigned char m[HV_NET_MOST];
    int n, due, to;                     /* to: 0 the opener, 1 and 2 the joiners; from a joiner: who it is from */
    unsigned long from;
};
static struct wire wires[512];
static int nwires, lose = 30, twice = 10, late = 3;     /* per cent lost, per cent doubled, steps late at most */

static void post(int to, unsigned long from, const unsigned char *m, int n, int now)
{
    int copies = (int)(rnd() % 100) < lose ? 0 : (int)(rnd() % 100) < twice ? 2 : 1;
    while (copies-- > 0 && nwires < 512 && n > 0) {
        memcpy(wires[nwires].m, m, (size_t)n);
        wires[nwires].n = n, wires[nwires].to = to, wires[nwires].from = from;
        wires[nwires++].due = now + (late ? (int)(rnd() % (unsigned)(late + 1)) : 0);
    }
}

static void net_tests(void)
{
    static struct hv_arena basin;
    static struct hv_opener o;
    static struct hv_joiner j[3];
    static struct hv_craft past[64][HV_CRAFT];          /* the opener's craft at its last 64 steps */
    static int past_score[64][2];
    unsigned char m[HV_NET_MOST], back[HV_NET_MOST];
    char why[160], name[17];
    int now, k, i, n, offer[4], bad = 0, heard_offer = 0, behind_most = 0;
    hv_arena_read(&basin, hv_arenas[0], strlen(hv_arenas[0]), why, sizeof why);
    hv_opener_start(&o, &basin, 0, HV_BEACONS, 4, "Opener's game");
    hv_joiner_start(&j[1]), hv_joiner_start(&j[2]);
    nwires = 0;
    for (now = 0; now < 1800; now++) {
        for (k = 1; k <= 2; k++) {              /* the joiners: look, ask until taken, then their controls each step */
            if (!j[k].joined && now % 10 == 0) {
                post(0, (unsigned long)k, m, hv_joiner_find(m), now);
                post(0, (unsigned long)k, m, hv_joiner_join(k == 1 ? "First" : "Second", m), now);
            } else if (j[k].joined) {
                unsigned keys = j[k].step ? hv_auto(&j[k].view, j[k].craft, 1) : 0;
                post(0, (unsigned long)k, m, hv_joiner_keys(&j[k], keys, m), now);
            }
        }
        for (i = 0; i < nwires;) {              /* what is due is delivered, in no order */
            struct wire w;
            if (wires[i].due > now) {
                i++;
                continue;
            }
            w = wires[i];
            wires[i] = wires[--nwires];
            if (w.to == 0) {
                n = hv_opener_hear(&o, w.from, w.m, w.n, back);
                post((int)w.from, 0, back, n, now);
            } else {
                int r = hv_joiner_hear(&j[w.to], &basin, w.m, w.n, offer, name);
                if (r == 1)
                    heard_offer += offer[0] == HV_BEACONS && offer[2] == 4 && !strcmp(name, "Opener's game");
                if (r == 2 && j[w.to].step) {   /* its view is the opener's game as it was at that step, to the unit */
                    const struct hv_craft *was = past[j[w.to].step % 64];
                    int behind = (int)(o.game.steps - j[w.to].step);
                    if (behind > behind_most)
                        behind_most = behind;
                    for (k = 0; k < 4 && behind < 64; k++)
                        bad += j[w.to].view.c[k].x != was[k].x || j[w.to].view.c[k].y != was[k].y || j[w.to].view.c[k].heading != was[k].heading
                               || j[w.to].view.c[k].vx != was[k].vx || j[w.to].view.c[k].side != was[k].side;
                    bad += behind < 64 && (j[w.to].view.score[0] != past_score[j[w.to].step % 64][0]
                                           || j[w.to].view.score[1] != past_score[j[w.to].step % 64][1]);
                }
            }
        }
        hv_opener_step(&o, hv_auto(&o.game, 0, 1));
        memcpy(past[o.game.steps % 64], o.game.c, sizeof o.game.c);
        past_score[o.game.steps % 64][0] = o.game.score[0], past_score[o.game.steps % 64][1] = o.game.score[1];
        if (now % 2 == 0) {
            n = hv_opener_state(&o, m);
            post(1, 0, m, n, now), post(2, 0, m, n, now);
        }
    }
    CHECK(j[1].joined && j[2].joined && j[1].craft != j[2].craft && j[1].craft != 0 && j[2].craft != 0 && heard_offer > 0,
          "two joined over a bad network: craft %d and %d, offers %d", j[1].craft, j[2].craft, heard_offer);
    CHECK(o.seat[j[1].craft].taken && o.seat[j[2].craft].taken && !strcmp(o.seat[j[1].craft].name, "First"), "the opener knows them: %s",
          o.seat[j[1].craft].name);
    CHECK(bad == 0 && behind_most < 40 && o.game.steps - j[1].step < 30 && o.game.steps - j[2].step < 30,
          "what a joiner sees is the game as it was: %d faults, %d steps behind at most, now %u and %u of %u", bad, behind_most, j[1].step,
          j[2].step, o.game.steps);
    CHECK(o.game.score[0] + o.game.score[1] > 10 && j[1].view.score[0] + j[1].view.score[1] > 10, "a game was played: %d %d", o.game.score[0],
          o.game.score[1]);
    /* a joiner gone quiet is the computer's after three seconds; one that says so, at once */
    for (k = 0; k < HV_NET_QUIET + 5; k++) {
        n = hv_joiner_keys(&j[2], 0, m);
        hv_opener_hear(&o, 2, m, n, back);
        hv_opener_step(&o, 0);
    }
    CHECK(!o.seat[j[1].craft].taken && o.seat[j[2].craft].taken, "the quiet one's craft is the computer's");
    /* (2026-11-23) the names: the opener's own craft the game's name, a joiner's its own, the computer's none */
    n = hv_opener_names(&o, m);
    hv_joiner_hear(&j[2], &basin, m, n, offer, name);
    CHECK(n == 5 + 12 * o.game.ncraft && !strncmp(j[2].names[o.own], o.name, 12) && !strcmp(j[2].names[j[2].craft], "Second")
          && !j[2].names[j[1].craft][0], "names: %s %s [%s]", j[2].names[o.own], j[2].names[j[2].craft], j[2].names[j[1].craft]);
    m[4] = (unsigned char)(o.game.ncraft + 1);
    strcpy(j[2].names[0], "kept");
    hv_joiner_hear(&j[2], &basin, m, n, offer, name);
    CHECK(!strcmp(j[2].names[0], "kept"), "names for another number of craft: passed over");
    strcpy(j[2].names[0], j[2].names[o.own]);
    n = hv_joiner_bye(&j[2], m);
    hv_opener_hear(&o, 2, m, n, back);
    CHECK(!o.seat[j[2].craft].taken, "the one that left");
    /* no craft free; another release; what is no message at all */
    hv_opener_start(&o, &basin, 0, HV_LAPS, 2, "Small");
    hv_joiner_start(&j[1]), hv_joiner_start(&j[2]);
    n = hv_opener_hear(&o, 1, m, hv_joiner_join("A", m), back);
    hv_joiner_hear(&j[1], &basin, back, n, offer, name);
    n = hv_opener_hear(&o, 2, m, hv_joiner_join("B", m), back);
    hv_joiner_hear(&j[2], &basin, back, n, offer, name);
    CHECK(j[1].joined && j[1].craft == 1 && !j[2].joined && j[2].refused == 1, "a game of two: one joins, one is told it is full");
    n = hv_opener_hear(&o, 1, m, hv_joiner_join("A", m), back);
    CHECK(n > 4 && back[3] == HV_M_TAKEN && back[4] == 1, "asked again: the same craft");
    hv_opener_start(&o, &basin, 0, HV_LAPS, 4, "Other");
    hv_joiner_start(&j[1]);
    n = hv_joiner_join("A", m);
    m[2] = HV_NET_VERSION + 1;
    n = hv_opener_hear(&o, 1, m, n, back);
    CHECK(n == 4 && back[3] == HV_M_FULL && !o.seat[1].taken, "another release is not let in");
    back[2] = HV_NET_VERSION + 1;
    hv_joiner_hear(&j[1], &basin, back, n, offer, name);
    CHECK(!j[1].joined && j[1].refused == 2, "... and knows why");
    n = hv_joiner_find(m);
    m[2] = HV_NET_VERSION + 1;
    CHECK(hv_opener_hear(&o, 1, m, n, back) == 0, "... nor shown the game");
    hv_joiner_start(&j[1]);
    n = hv_opener_hear(&o, 1, m, hv_joiner_join("A", m), back);
    hv_joiner_hear(&j[1], &basin, back, n, offer, name);
    for (k = 0, bad = 0; k < 20000; k++) {      /* bytes of no sense, and true messages with bytes changed */
        int len = (int)(rnd() % HV_NET_MOST) + 1;
        if (k & 1) {
            len = hv_opener_state(&o, m);
            for (i = 0; i < 3; i++)
                m[rnd() % (unsigned)len] = (unsigned char)rnd();
            if (k & 2)
                len = (int)(rnd() % (unsigned)len);
        } else {
            for (i = 0; i < len; i++)
                m[i] = (unsigned char)rnd();
            if (k & 4)
                m[0] = 'H', m[1] = 'V', m[2] = HV_NET_VERSION, m[3] = (unsigned char)(rnd() % 10);
        }
        hv_opener_hear(&o, (unsigned long)(k % 3), m, len, back);
        hv_joiner_hear(&j[1], &basin, m, len, offer, name);
        for (i = 0; i < 4 && j[1].step; i++)
            bad += j[1].view.c[i].x < 0 || j[1].view.c[i].x >= basin.w * 256 || j[1].view.c[i].y < 0 || j[1].view.c[i].y >= basin.h * 256
                   || j[1].view.c[i].heading > 1023;
        hv_opener_step(&o, 0);
    }
    CHECK(bad == 0, "20000 bad datagrams: a view's craft off the arena %d times", bad);
}

/* What a joiner makes of two states that differ (the sounds' events), and a step without a state. */
static void view_tests(void)
{
    static struct hv_arena basin;
    static struct hv_opener o;
    static struct hv_joiner j;
    unsigned char m[HV_NET_MOST], back[HV_NET_MOST];
    char why[160], name[17];
    int offer[4], n, x, y, i;
#define TELL() (o.game.steps += 2, hv_joiner_hear(&j, &basin, m, hv_opener_state(&o, m), offer, name))
    hv_arena_read(&basin, hv_arenas[0], strlen(hv_arenas[0]), why, sizeof why);
    hv_opener_start(&o, &basin, 0, HV_FLAGS, 4, "Game");
    hv_joiner_start(&j);
    n = hv_opener_hear(&o, 7, m, hv_joiner_join("A", m), back);
    hv_joiner_hear(&j, &basin, back, n, offer, name);
    o.game.flag[1].holder = 0;                  /* (already so when the joiner first hears: nothing new to it) */
    CHECK(TELL() == 2 && j.view.ev == 0 && j.view.flag[1].holder == 0, "the first state: nothing happened yet (ev %x)", j.view.ev);
    CHECK(TELL() == 2 && j.view.ev == 0, "a state like the one before: nothing happened");
    o.game.flag[1].holder = -1;
    CHECK(TELL() == 2 && j.view.ev == HV_EV_DROP, "a flag no longer carried: dropped (ev %x)", j.view.ev);
    o.game.flag[1].holder = 2;
    CHECK(TELL() == 2 && j.view.ev == HV_EV_TAKEN, "a flag carried: taken (ev %x)", j.view.ev);
    o.game.flag[1].holder = -1, o.game.score[0]++;
    CHECK(TELL() == 2 && j.view.ev == HV_EV_SCORE, "no longer carried and a point more: brought home, not dropped (ev %x)", j.view.ev);
    o.game.beacon[1] = 1, o.game.c[3].wet = 20;
    CHECK(TELL() == 2 && j.view.ev == (HV_EV_BEACON | HV_EV_SPLASH), "a beacon turned, a craft in the water (ev %x)", j.view.ev);
    CHECK(TELL() == 2 && j.view.ev == 0, "... and still in the water: said once");
    /* coasting: a craft goes on as it was going for three steps at most, not into a wall, not when set down */
    o.game.c[3].wet = 0;
    o.game.c[0].vx = 10 * 256, o.game.c[0].vy = -5 * 256;
    o.game.c[1].vx = 10 * 256, o.game.c[1].down = 1;
    o.game.flag[1].holder = 0;
    TELL();
    x = j.view.c[0].x, y = j.view.c[0].y;
    hv_joiner_coast(&j);
    CHECK(j.view.c[0].x == x + 10 && j.view.c[0].y == y - 5 && j.view.c[1].x == o.game.c[1].x && j.view.ev == 0,
          "a step without a state: on as it was going (%d,%d from %d,%d), one set down stays", j.view.c[0].x, j.view.c[0].y, x, y);
    CHECK(j.view.flag[1].x == j.view.c[0].x && j.view.flag[1].y == j.view.c[0].y, "the flag goes with its carrier");
    for (i = 0; i < 5; i++)
        hv_joiner_coast(&j);
    CHECK(j.view.c[0].x == x + 30 && j.view.c[0].y == y - 15, "three steps at most (%d,%d from %d,%d)", j.view.c[0].x, j.view.c[0].y, x, y);
    for (i = 0; i < basin.w && basin.sq[o.game.c[0].y >> 8][i].kind != HV_WALL; i++)
        ;
    o.game.c[0].x = i * 256 - 10, o.game.c[0].vx = 100 * 256, o.game.c[0].vy = 0;    /* (10 short of a wall to its right) */
    if (i < basin.w && i > 0) {
        TELL();
        hv_joiner_coast(&j);
        CHECK(j.view.c[0].x == i * 256 - 10, "never into a wall (%d)", j.view.c[0].x);
    }
    CHECK(TELL() == 2 && j.coasted == 0, "a state: the count begins again");
#undef TELL
}

int main(int argc, char **argv)
{
    static struct hv_arena a;
    static unsigned short dist[HV_GRID][HV_GRID];
    char why[160] = "";
    int r;
    if ((argc == 4 && !strcmp(argv[1], "demo")) || (argc == 5 && !strcmp(argv[1], "hold"))) {
        /* hv_test demo KIND STEPS: what HVCRAFT -demo says at that step; hv_test hold KIND CONTROLS STEPS: the same
           with the player's craft held at those controls all the while (a pad held so: the suite's) */
        static struct hv_game d;
        int i, hold = argc == 5;
        if (hv_arena_read(&a, HV_BASIN, sizeof HV_BASIN - 1, why, sizeof why) != 0 || hv_game_start(&d, &a, atoi(argv[2]), HV_DEMO_CRAFT) != 0)
            return 2;
        while ((long)d.steps < atol(argv[hold ? 4 : 3])) {
            unsigned char keys[HV_CRAFT];
            for (i = 0; i < d.ncraft; i++)
                keys[i] = (unsigned char)(hold && i == 0 ? (unsigned)atoi(argv[3]) : hv_auto(&d, i, HV_DEMO_STRENGTH));
            hv_game_step(&d, keys);
        }
        printf(HV_AT_LINE, d.steps, d.score[0], d.score[1], d.c[0].x, d.c[0].y, d.c[0].heading, d.c[1].x, d.c[1].y, d.lamp_holder, d.c[0].score);
        return 0;
    }
    r = hv_arena_read(&a, GOOD, strlen(GOOD), why, sizeof why);
    CHECK(r == 0, "the good arena: %s", why);
    CHECK(!strcmp(a.name, "The Test Basin") && a.light == 1 && a.w == 14 && a.h == 6, "its head and size: %s %d %dx%d", a.name, a.light, a.w, a.h);
    CHECK(a.ndock[0] == 2 && a.ndock[1] == 2 && a.dock[0][0].x == 1 && a.dock[0][0].y == 1 && a.dock[1][1].x == 12 && a.dock[1][1].y == 2,
          "the docks: %d %d", a.ndock[0], a.ndock[1]);
    CHECK(a.nbeacon == 2 && a.beacon[1].x == 10 && a.beacon[1].y == 3 && a.lamp.x == 6 && a.lamp.y == 4, "beacons %d, the lamp %d,%d", a.nbeacon,
          a.lamp.x, a.lamp.y);
    CHECK(a.nbuoy == 3 && a.buoy[0].x == 3 && a.buoy[1].x == 10 && a.buoy[2].x == 9 && a.buoy[2].y == 4, "the buoys: %d", a.nbuoy);
    CHECK(a.sq[0][0].kind == HV_WALL && a.sq[1][2].kind == HV_FLOOR && a.sq[1][6].level == 1 && a.sq[2][5].kind == HV_RAMP
          && a.sq[2][5].way == HV_E && a.sq[2][5].level == 0 && a.sq[2][8].way == HV_W && a.sq[3][6].kind == HV_WATER && a.sq[1][1].mark == 'A',
          "the squares");
    /* where a craft can go */
    CHECK(hv_arena_step(&a, 2, 1, HV_E) && hv_arena_step(&a, 3, 1, HV_W) && !hv_arena_step(&a, 1, 1, HV_W) && !hv_arena_step(&a, 1, 1, HV_N),
          "floor to floor, not into a wall");
    CHECK(!hv_arena_step(&a, 5, 1, HV_E) && hv_arena_step(&a, 6, 1, HV_W), "not up a step; down it");
    CHECK(hv_arena_step(&a, 4, 2, HV_E) && hv_arena_step(&a, 5, 2, HV_E) && hv_arena_step(&a, 6, 2, HV_W) && hv_arena_step(&a, 5, 2, HV_W),
          "up a ramp and down it");
    CHECK(!hv_arena_step(&a, 5, 1, HV_S) && !hv_arena_step(&a, 5, 2, HV_N) && !hv_arena_step(&a, 5, 3, HV_N) && !hv_arena_step(&a, 5, 2, HV_S),
          "not across a ramp's sides");
    CHECK(!hv_arena_step(&a, 5, 3, HV_E) && !hv_arena_step(&a, 6, 2, HV_S) && !hv_arena_step(&a, 6, 3, HV_W), "not into water, nor out of it");
    /* how far from the first dock's square */
    hv_arena_distances(&a, 1, 1, dist);
    CHECK(dist[1][1] == 0 && dist[1][2] == 1 && dist[2][1] == 1 && dist[4][1] == 3 && dist[0][0] == 0xFFFF && dist[3][6] == 0xFFFF,
          "distances near: %u %u %u", dist[1][2], dist[2][1], dist[4][1]);
    CHECK(dist[1][6] == 5 && dist[2][6] == 6 && dist[1][12] == 13, "the quay is left by its drop, the far dock is over the ramps: %u %u %u", dist[1][6],
          dist[2][6], dist[1][12]);
    /* a file with DOS's line ends, no head, empty lines after */
    CHECK(hv_arena_read(&a, "#####\r\n#A.B#\r\n#####\r\n\r\n\r\n", 25, why, sizeof why) == 0 && a.w == 5 && a.h == 3 && a.lamp.x == 2
          && !strcmp(a.name, "An arena"), "a small one with no head: %s", why);
    /* each fault */
    CHECK(refused("name: X\n\n#####\n#A?B#\n#####\n", "line 4: a square that is none of"), "a letter that is none");
    CHECK(refused("name: X\n\n#####\n#A?B#\n#####\n", "(column 3)"), "... and its column");
    CHECK(refused("\n#####\n#A.B#\n####\n", "line 4: this row is not as long as the first"), "a short row");
    CHECK(refused("#####\n#A..#\n#####\n", "no dock for side B"), "no dock for B");
    CHECK(refused("#####\n#..B#\n#####\n", "no dock for side A"), "no dock for A");
    CHECK(refused("#######\n#A.#.B#\n#######\n", "line 2: nobody can reach the square at column 5"), "a room walled off");
    CHECK(refused("#######\n#A1.1B#\n#######\n", "line 2: nobody can reach the square at column 3"), "a quay with no ramp up");
    CHECK(refused("########\n#A>1..B#\n########\n", "line 2: a craft at column 5 could never leave"), "a way down and none back");
    CHECK(refused("#######\n#A#>1B#\n#######\n", "line 2: a ramp with no floor behind it"), "a ramp from a wall");
    CHECK(refused("#######\n#A.>.B#\n#######\n", "line 2: a ramp that does not lead to a floor one level up"), "a ramp to nowhere");
    CHECK(refused("#######\n#Aa.cB#\n#######\n", "the buoys' letters leave one out"), "buoys a and c");
    CHECK(refused("#######\n#Aa.aB#\n#######\n", "line 2: a buoy's letter twice"), "a buoy twice");
    CHECK(refused("#######\n#A!.!B#\n#######\n", "line 2: a second lamp"), "two lamps");
    CHECK(refused("light: noon\n\n#####\n#A.B#\n#####\n", "line 1: the light is day, dusk or night"), "a light that is none");
    CHECK(refused("colour: red\n\n#####\n#A.B#\n#####\n", "line 1: a line of the head"), "a head's line that is none");
    CHECK(refused("name: Nothing\n", "no grid of squares"), "no grid");
    CHECK(refused("", "no grid of squares"), "an empty file");
    {
        char wide[200], *p = wide;
        int i;
        for (i = 0; i < 49; i++)
            *p++ = '#';
        strcpy(p, "\n#A.B#\n#####\n");
        CHECK(refused(wide, "line 1: a row longer than an arena is wide"), "49 squares wide");
    }
    game_tests();
    net_tests();
    view_tests();
    CHECK(hv_arena_read(&a, HV_BASIN, sizeof HV_BASIN - 1, why, sizeof why) == 0 && a.nbuoy == 4 && a.nbeacon == 4 && a.ndock[0] == 3,
          "the program's own arena: %s", why);
    {
        /* the arenas that come with the game: each is read, has what every kind of game wants, and the computer's
           craft (four of them, the middle strength) score in it at every kind within three minutes */
        int n, kind, i;
        for (n = 0; n < HV_ARENAS; n++) {
            why[0] = 0;
            r = hv_arena_read(&a, hv_arenas[n], strlen(hv_arenas[n]), why, sizeof why);
            CHECK(r == 0 && a.nbuoy >= 3 && a.nbeacon >= 3 && (a.nbeacon & 1) == 0 && a.ndock[0] >= 2 && a.ndock[1] >= 2,
                  "arena %d (%s): %s; buoys %d beacons %d", n, a.name, why, a.nbuoy, a.nbeacon);
            if (r != 0)
                continue;
            for (kind = HV_FLAGS; kind <= HV_LAPS; kind++) {
                long points, steps_n;
                hv_game_start(&g, &a, kind, 4);
                for (steps_n = 0; steps_n < 3 * 60 * HV_STEPS; steps_n++) {
                    unsigned char keys[HV_CRAFT];
                    for (i = 0; i < 4; i++)
                        keys[i] = (unsigned char)hv_auto(&g, i, 1);
                    hv_game_step(&g, keys);
                }
                points = kind <= HV_BEACONS ? g.score[0] + g.score[1] : g.c[0].score + g.c[1].score + g.c[2].score + g.c[3].score;
                CHECK(points >= (kind == HV_FLAGS ? 1 : 4), "arena %d (%s) kind %d: %ld points in three minutes", n, a.name, kind, points);
                printf("  %-18s kind %d: sides %d %d, craft %ld %ld %ld %ld\n", a.name, kind, g.score[0], g.score[1], g.c[0].score, g.c[1].score,
                       g.c[2].score, g.c[3].score);
            }
        }
    }
    printf("hv_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
