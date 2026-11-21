/* The hovercraft game's arenas read (hvarena.h). */
#include <stdio.h>
#include <string.h>

#include "hvarena.h"

static const int DX[4] = { 0, 1, 0, -1 }, DY[4] = { -1, 0, 1, 0 };

static int fail(char *why, int room, int line, const char *what)
{
    if (line)
        snprintf(why, (size_t)room, "line %d: %s", line, what);
    else
        snprintf(why, (size_t)room, "%s", what);
    return -1;
}

/* The level at a square's edge towards `way`: -1 where nothing passes (a wall, water, a ramp's sides). */
static int edge(const struct hv_arena *a, int x, int y, int way)
{
    const struct hv_square *s;
    if (x < 0 || y < 0 || x >= a->w || y >= a->h)
        return -1;
    s = &a->sq[y][x];
    if (s->kind == HV_FLOOR)
        return s->level;
    if (s->kind == HV_RAMP)
        return way == s->way ? s->level + 1 : way == (s->way ^ 2) ? s->level : -1;
    return -1;
}

int hv_arena_step(const struct hv_arena *a, int x, int y, int way)
{
    int from = edge(a, x, y, way), to = edge(a, x + DX[way], y + DY[way], way ^ 2);
    return from >= 0 && to >= 0 && from >= to;         /* (level, or a drop) */
}

/* Squares gone through from each square to (x, y); back true: from (x, y) to each square. */
static void spread(const struct hv_arena *a, int x, int y, int back, unsigned short dist[HV_GRID][HV_GRID])
{
    static struct hv_spot queue[HV_GRID * HV_GRID];
    int head = 0, tail = 0, way;
    memset(dist, 0xFF, sizeof(unsigned short) * HV_GRID * HV_GRID);
    if (edge(a, x, y, 0) < 0 && edge(a, x, y, 1) < 0)
        return;
    dist[y][x] = 0;
    queue[tail].x = (unsigned char)x, queue[tail++].y = (unsigned char)y;
    while (head < tail) {
        int cx = queue[head].x, cy = queue[head].y;
        head++;
        for (way = 0; way < 4; way++) {
            int nx = cx + DX[way], ny = cy + DY[way];
            if (nx < 0 || ny < 0 || nx >= a->w || ny >= a->h || dist[ny][nx] != 0xFFFF)
                continue;
            if (back ? !hv_arena_step(a, cx, cy, way) : !hv_arena_step(a, nx, ny, way ^ 2))
                continue;
            dist[ny][nx] = (unsigned short)(dist[cy][cx] + 1);
            queue[tail].x = (unsigned char)nx, queue[tail++].y = (unsigned char)ny;
        }
    }
}

void hv_arena_distances(const struct hv_arena *a, int x, int y, unsigned short dist[HV_GRID][HV_GRID])
{
    spread(a, x, y, 0, dist);
}

int hv_arena_read(struct hv_arena *a, const char *text, size_t n, char *why, int room)
{
    static unsigned short to[HV_GRID][HV_GRID], from[HV_GRID][HV_GRID];
    int first_line[HV_GRID];            /* the file's line of each row */
    const char *p = text, *end = text + n;
    int line = 0, in_grid = 0, x, y, k, buoys[8] = { 0 };
    memset(a, 0, sizeof *a);
    snprintf(a->name, sizeof a->name, "An arena");
    while (p < end) {
        const char *nl = memchr(p, '\n', (size_t)(end - p)), *e = nl ? nl : end;
        size_t len;
        line++;
        while (e > p && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t'))
            e--;
        len = (size_t)(e - p);
        if (!in_grid && len == 0) {
            in_grid = 1;                /* (the empty line under the head) */
        } else if (!in_grid) {
            if (len > 5 && !strncmp(p, "name:", 5)) {
                const char *v = p + 5;
                while (v < e && *v == ' ')
                    v++;
                snprintf(a->name, sizeof a->name, "%.*s", (int)(e - v), v);
            } else if (len > 6 && !strncmp(p, "light:", 6)) {
                const char *v = p + 6;
                while (v < e && *v == ' ')
                    v++;
                if (e - v == 3 && !strncmp(v, "day", 3))
                    a->light = 0;
                else if (e - v == 4 && !strncmp(v, "dusk", 4))
                    a->light = 1;
                else if (e - v == 5 && !strncmp(v, "night", 5))
                    a->light = 2;
                else
                    return fail(why, room, line, "the light is day, dusk or night");
            } else if (p[0] == '#') {
                in_grid = 1;            /* (no empty line: the grid from its first row of wall, read below) */
            } else {
                return fail(why, room, line, "a line of the head is \"name: ...\" or \"light: ...\"; an empty line ends the head");
            }
        }
        if (in_grid && len) {
            if (a->h == HV_GRID)
                return fail(why, room, line, "more rows than an arena has (48)");
            if (len > HV_GRID)
                return fail(why, room, line, "a row longer than an arena is wide (48)");
            if (a->h && (int)len != a->w)
                return fail(why, room, line, "this row is not as long as the first");
            a->w = (int)len;
            first_line[a->h] = line;
            for (x = 0; x < a->w; x++) {
                struct hv_square *s = &a->sq[a->h][x];
                char c = p[x];
                s->kind = HV_FLOOR;
                if (c == '#') {
                    s->kind = HV_WALL;
                } else if (c == '.') {
                    ;
                } else if (c == '1' || c == '2') {
                    s->level = (unsigned char)(c - '0');
                } else if (c == '^' || c == '>' || c == 'v' || c == '<') {
                    s->kind = HV_RAMP;
                    s->way = (unsigned char)(c == '^' ? HV_N : c == '>' ? HV_E : c == 'v' ? HV_S : HV_W);
                } else if (c == '~') {
                    s->kind = HV_WATER;
                } else if (c == 'A' || c == 'B') {
                    int side = c - 'A';
                    if (a->ndock[side] == HV_SPOTS)
                        return fail(why, room, line, "a dock of more than sixteen squares");
                    a->dock[side][a->ndock[side]].x = (unsigned char)x, a->dock[side][a->ndock[side]++].y = (unsigned char)a->h;
                    s->mark = (unsigned char)c;
                } else if (c == '*') {
                    if (a->nbeacon == HV_SPOTS)
                        return fail(why, room, line, "more than sixteen beacons");
                    a->beacon[a->nbeacon].x = (unsigned char)x, a->beacon[a->nbeacon++].y = (unsigned char)a->h;
                    s->mark = '*';
                } else if (c == '!') {
                    if (a->lamp.x || a->lamp.y)
                        return fail(why, room, line, "a second lamp");
                    a->lamp.x = (unsigned char)x, a->lamp.y = (unsigned char)a->h;
                    s->mark = '!';
                } else if (c >= 'a' && c <= 'h') {
                    if (buoys[c - 'a'])
                        return fail(why, room, line, "a buoy's letter twice");
                    buoys[c - 'a'] = 1;
                    a->buoy[c - 'a'].x = (unsigned char)x, a->buoy[c - 'a'].y = (unsigned char)a->h;
                    s->mark = (unsigned char)c;
                } else {
                    snprintf(why, (size_t)room, "line %d: a square that is none of # . 1 2 ^ v < > ~ A B * ! or a to h (column %d)", line,
                             x + 1);
                    return -1;
                }
            }
            a->h++;
        }
        p = nl ? nl + 1 : end;
    }
    if (a->h < 3 || a->w < 3)
        return fail(why, room, 0, "no grid of squares (three rows of three at least, under an empty line)");
    /* a ramp comes up from the square behind it, to a floor one level higher */
    for (y = 0; y < a->h; y++)
        for (x = 0; x < a->w; x++) {
            struct hv_square *s = &a->sq[y][x];
            int bx, by, ux, uy;
            if (s->kind != HV_RAMP)
                continue;
            bx = x - DX[s->way], by = y - DY[s->way], ux = x + DX[s->way], uy = y + DY[s->way];
            if (bx < 0 || by < 0 || bx >= a->w || by >= a->h || a->sq[by][bx].kind != HV_FLOOR)
                return fail(why, room, first_line[y], "a ramp with no floor behind it to come up from");
            s->level = a->sq[by][bx].level;
            if (ux < 0 || uy < 0 || ux >= a->w || uy >= a->h || a->sq[uy][ux].kind != HV_FLOOR || a->sq[uy][ux].level != s->level + 1)
                return fail(why, room, first_line[y], "a ramp that does not lead to a floor one level up");
        }
    for (k = 0; k < 8 && buoys[k]; k++)
        a->nbuoy = k + 1;
    for (; k < 8; k++)
        if (buoys[k])
            return fail(why, room, 0, "the buoys' letters leave one out (a, b, c ... in order)");
    if (!a->ndock[0] || !a->ndock[1])
        return fail(why, room, 0, a->ndock[0] ? "no dock for side B (a square B)" : "no dock for side A (a square A)");
    /* every place can be reached from a dock, and a dock from every place */
    spread(a, a->dock[0][0].x, a->dock[0][0].y, 0, to);
    spread(a, a->dock[0][0].x, a->dock[0][0].y, 1, from);
    for (y = 0; y < a->h; y++)
        for (x = 0; x < a->w; x++) {
            int kind = a->sq[y][x].kind;
            if (kind != HV_FLOOR && kind != HV_RAMP)
                continue;
            if (from[y][x] == 0xFFFF) {
                snprintf(why, (size_t)room, "line %d: nobody can reach the square at column %d", first_line[y], x + 1);
                return -1;
            }
            if (to[y][x] == 0xFFFF) {
                snprintf(why, (size_t)room, "line %d: a craft at column %d could never leave", first_line[y], x + 1);
                return -1;
            }
        }
    if (!a->lamp.x && !a->lamp.y) {     /* no lamp named: the floor square nearest the arena's middle */
        int best = 1 << 30;
        for (y = 0; y < a->h; y++)
            for (x = 0; x < a->w; x++) {
                int d = (2 * x - a->w) * (2 * x - a->w) + (2 * y - a->h) * (2 * y - a->h);
                if (a->sq[y][x].kind == HV_FLOOR && !a->sq[y][x].mark && d < best)
                    best = d, a->lamp.x = (unsigned char)x, a->lamp.y = (unsigned char)y;
            }
    }
    return 0;
}
