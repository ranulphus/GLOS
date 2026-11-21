/* The hovercraft game over a network (hvnet.h): the messages made and read. Every number is written a byte at
 * a time, low byte first; nothing read is believed beyond its length and its ranges. */
#include <string.h>

#include "hvnet.h"

static void w16(unsigned char *p, int v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8); }
static void w32(unsigned char *p, unsigned long v) { w16(p, (int)(v & 0xFFFF)), w16(p + 2, (int)(v >> 16)); }
static int r16(const unsigned char *p) { return p[0] | p[1] << 8; }
static int s16(const unsigned char *p) { int v = r16(p); return v >= 0x8000 ? v - 0x10000 : v; }
static unsigned long r32(const unsigned char *p) { return (unsigned long)r16(p) | (unsigned long)r16(p + 2) << 16; }

static int head(unsigned char *m, int type)
{
    m[0] = 'H', m[1] = 'V', m[2] = HV_NET_VERSION, m[3] = (unsigned char)type;
    return 4;
}

/* 0: not a message of the game's; else its type (the caller looks at m[2] for the version). */
static int type_of(const unsigned char *m, int n)
{
    return n >= 4 && m[0] == 'H' && m[1] == 'V' && m[3] >= HV_M_FIND && m[3] <= HV_M_BYE ? m[3] : 0;
}

/* ---- the opener */

void hv_opener_start(struct hv_opener *o, const struct hv_arena *a, int arena, int kind, int ncraft, const char *name)
{
    memset(o, 0, sizeof *o);
    hv_game_start(&o->game, a, kind, ncraft);
    o->arena = arena;
    strncpy(o->name, name ? name : "", sizeof o->name - 1);
}

int hv_opener_hear(struct hv_opener *o, unsigned long who, const unsigned char *m, int n, unsigned char *back)
{
    int type = type_of(m, n), i, k, free_seats = 0;
    if (!type)
        return 0;
    if (m[2] != HV_NET_VERSION)         /* another release's: only one that asks to join is told so (by our version) */
        return type == HV_M_JOIN ? head(back, HV_M_FULL) : 0;
    for (i = 0; i < o->game.ncraft; i++)
        free_seats += i != o->own && !o->seat[i].taken;
    switch (type) {
    case HV_M_FIND:
        k = head(back, HV_M_OFFER);
        back[k++] = (unsigned char)o->game.kind, back[k++] = (unsigned char)o->arena;
        back[k++] = (unsigned char)o->game.ncraft, back[k++] = (unsigned char)free_seats;
        memset(back + k, 0, 16);
        memcpy(back + k, o->name, strlen(o->name) < 16 ? strlen(o->name) : 16);
        return k + 16;
    case HV_M_JOIN:
        if (n < 4 + 12)
            return 0;
        for (i = 0; i < o->game.ncraft; i++)    /* (asked again, its answer lost: the same craft) */
            if (o->seat[i].taken && o->seat[i].who == who)
                break;
        if (i == o->game.ncraft) {
            for (i = 0; i < o->game.ncraft && (i == o->own || o->seat[i].taken); i++)
                ;
            if (i == o->game.ncraft)
                return head(back, HV_M_FULL);
            memset(&o->seat[i], 0, sizeof o->seat[i]);
            o->seat[i].taken = 1;
            o->seat[i].who = who;
            memcpy(o->seat[i].name, m + 4, 12);
            o->seat[i].heard = o->game.steps;
        }
        k = head(back, HV_M_TAKEN);
        back[k++] = (unsigned char)i, back[k++] = (unsigned char)o->game.kind, back[k++] = (unsigned char)o->arena;
        back[k++] = (unsigned char)o->game.ncraft;
        return k;
    case HV_M_KEYS:
        if (n < 10 || m[4] >= o->game.ncraft || !o->seat[m[4]].taken || o->seat[m[4]].who != who)
            return 0;
        o->seat[m[4]].heard = o->game.steps;
        if (r32(m + 5) > o->seat[m[4]].count) { /* (one that comes late, after a newer, is not heard) */
            o->seat[m[4]].count = r32(m + 5);
            o->seat[m[4]].controls = m[9];
        }
        return 0;
    case HV_M_BYE:
        if (n >= 5 && m[4] < o->game.ncraft && o->seat[m[4]].taken && o->seat[m[4]].who == who)
            o->seat[m[4]].taken = 0;
        return 0;
    }
    return 0;
}

void hv_opener_step(struct hv_opener *o, unsigned own_controls)
{
    unsigned char keys[HV_CRAFT];
    int i;
    for (i = 0; i < o->game.ncraft; i++) {
        struct hv_seat *s = &o->seat[i];
        if (s->taken && o->game.steps - s->heard > HV_NET_QUIET)
            s->taken = 0;               /* gone quiet: the computer's from here */
        keys[i] = (unsigned char)(i == o->own ? own_controls : s->taken ? s->controls : hv_auto(&o->game, i, 1));
    }
    hv_game_step(&o->game, keys);
}

int hv_opener_state(const struct hv_opener *o, unsigned char *m)
{
    const struct hv_game *g = &o->game;
    int k = head(m, HV_M_STATE), i;
    w32(m + k, g->steps), k += 4;
    m[k++] = (unsigned char)g->kind, m[k++] = (unsigned char)g->ncraft;
    w16(m + k, g->score[0]), w16(m + k + 2, g->score[1]), k += 4;
    m[k++] = (unsigned char)g->lamp_holder;
    for (i = 0; i < HV_SPOTS; i++)
        m[k++] = (unsigned char)g->beacon[i];
    for (i = 0; i < 2; i++) {
        w16(m + k, g->flag[i].x), w16(m + k + 2, g->flag[i].y);
        m[k + 4] = (unsigned char)g->flag[i].holder;
        w16(m + k + 5, g->flag[i].away);
        k += 7;
    }
    for (i = 0; i < g->ncraft; i++) {
        const struct hv_craft *c = &g->c[i];
        w16(m + k, c->x), w16(m + k + 2, c->y), w16(m + k + 4, c->vx), w16(m + k + 6, c->vy), w16(m + k + 8, c->heading);
        m[k + 10] = (unsigned char)c->tank;
        m[k + 11] = (unsigned char)((c->down ? 1 : 0) | (c->wet ? 2 : 0) | (c->carrying ? 4 : 0) | (c->side ? 8 : 0));
        w16(m + k + 12, (int)(c->score > 0x7FFF ? 0x7FFF : c->score));
        m[k + 14] = (unsigned char)c->buoy;
        k += 15;
    }
    return k;
}

/* ---- a joiner */

void hv_joiner_start(struct hv_joiner *j) { memset(j, 0, sizeof *j); }

int hv_joiner_find(unsigned char *m) { return head(m, HV_M_FIND); }

int hv_joiner_join(const char *name, unsigned char *m)
{
    int k = head(m, HV_M_JOIN);
    memset(m + k, 0, 12);
    memcpy(m + k, name ? name : "", name && strlen(name) < 12 ? strlen(name) : name ? 12 : 0);
    return k + 12;
}

int hv_joiner_keys(struct hv_joiner *j, unsigned controls, unsigned char *m)
{
    int k = head(m, HV_M_KEYS);
    m[k++] = (unsigned char)j->craft;
    w32(m + k, ++j->sent), k += 4;
    m[k++] = (unsigned char)controls;
    return k;
}

int hv_joiner_bye(const struct hv_joiner *j, unsigned char *m)
{
    int k = head(m, HV_M_BYE);
    m[k++] = (unsigned char)j->craft;
    return k;
}

int hv_joiner_hear(struct hv_joiner *j, const struct hv_arena *a, const unsigned char *m, int n, int *offer, char *name17)
{
    int type = type_of(m, n), i, k;
    struct hv_game *g = &j->view;
    if (!type)
        return 0;
    if (m[2] != HV_NET_VERSION) {       /* another release's game: not one to join */
        if (type == HV_M_TAKEN || type == HV_M_FULL)
            j->refused = 2;
        return 0;
    }
    switch (type) {
    case HV_M_OFFER:
        if (n < 8 + 16 || m[4] > HV_LAPS || m[6] < 1 || m[6] > HV_CRAFT)
            return 0;
        if (offer)
            offer[0] = m[4], offer[1] = m[5], offer[2] = m[6], offer[3] = m[7];
        if (name17) {
            memcpy(name17, m + 8, 16);
            name17[16] = 0;
        }
        return 1;
    case HV_M_TAKEN:
        if (n < 8 || m[7] < 1 || m[7] > HV_CRAFT || m[4] >= m[7] || m[5] > HV_LAPS)
            return 0;
        j->joined = 1, j->refused = 0;
        j->craft = m[4], j->kind = m[5], j->arena = m[6], j->ncraft = m[7];
        return 2;
    case HV_M_FULL:
        if (!j->joined)
            j->refused = 1;
        return 0;
    case HV_M_STATE:
        if (!j->joined || !a || n < 45 || m[9] != j->ncraft || n < 45 + 15 * m[9] || m[8] != j->kind)
            return 0;
        if (j->step && r32(m + 4) <= j->step)   /* (an older one, come late or twice) */
            return 0;
        j->step = (unsigned)r32(m + 4);
        g->a = a;
        g->kind = m[8], g->ncraft = m[9];
        g->steps = j->step;
        g->score[0] = s16(m + 10), g->score[1] = s16(m + 12);
        g->lamp_holder = m[14] == 0xFF || m[14] >= g->ncraft ? -1 : m[14];
        for (i = 0; i < HV_SPOTS; i++)
            g->beacon[i] = (signed char)(m[15 + i] <= 1 ? m[15 + i] : -1);
        k = 31;
        for (i = 0; i < 2; i++, k += 7) {
            g->flag[i].x = r16(m + k), g->flag[i].y = r16(m + k + 2);
            g->flag[i].holder = m[k + 4] < g->ncraft ? m[k + 4] : -1;
            g->flag[i].away = r16(m + k + 5);
        }
        for (i = 0; i < g->ncraft; i++, k += 15) {
            struct hv_craft *c = &g->c[i];
            int x = r16(m + k), y = r16(m + k + 2);
            if (x >= a->w * 256 || y >= a->h * 256)     /* (never a place off the arena, whatever the bytes) */
                x = y = 128;
            c->x = x, c->y = y;
            c->vx = s16(m + k + 4), c->vy = s16(m + k + 6);
            c->heading = r16(m + k + 8) & 1023;
            c->tank = m[k + 10] > HV_TANK ? HV_TANK : m[k + 10];
            c->down = m[k + 11] & 1, c->wet = m[k + 11] & 2 ? 1 : 0, c->carrying = (m[k + 11] >> 2) & 1, c->side = (m[k + 11] >> 3) & 1;
            c->score = s16(m + k + 12);
            c->buoy = a->nbuoy ? m[k + 14] % a->nbuoy : 0;
        }
        if (g->lamp_holder >= 0)
            g->lamp_x = g->c[g->lamp_holder].x, g->lamp_y = g->c[g->lamp_holder].y;
        else
            g->lamp_x = a->lamp.x * 256 + 128, g->lamp_y = a->lamp.y * 256 + 128;
        return 2;
    }
    return 0;
}
