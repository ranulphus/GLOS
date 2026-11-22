/* The hovercraft game on GLOS's network (hvlink.h): datagram ports (NET, API 2.2) under the game's messages.
 *
 * An opener has two ports: the game's well-known one (HV_PORT, shared: several games may be open on a PC),
 * where those who look for games ask everyone, and one of its own, from which it answers and at which its
 * joiners then speak to it. A joiner has one port of its own. Nothing here waits: a step takes what has come
 * in and sends what is due. */
#include <string.h>

#include <glos/glos.h>

#include "hvlink.h"

#define EVERYONE 0xFFFFFFFFu
#define HEAR_MOST 48                    /* datagrams taken in a step at most */

static int mode;
static glos_handle find_h, port_h;      /* the opener's shared port; the opener's or the joiner's own */
static struct hv_opener op;
static struct hv_joiner jn;
static const struct hv_arena *jn_arena;
static unsigned seat_addr[HV_CRAFT], seat_port[HV_CRAFT];
static unsigned host_addr, host_port;   /* a joiner's: the opener (0: none found yet) */
static char host_name[17], my_name[17];
static unsigned asked, looked, quiet;
static struct hvl_offer offers[HVL_OFFERS];
static int noffers;
static unsigned browse_asked;

int hvlink_mode(void) { return mode; }
struct hv_game *hvlink_game(void) { return mode == HVL_OPEN ? &op.game : &jn.view; }
int hvlink_craft(void) { return mode == HVL_OPEN ? op.own : jn.craft; }
const struct hv_joiner *hvlink_joiner(void) { return &jn; }
const char *hvlink_host(void) { return host_name; }

int hvlink_seats(void)
{
    int i, n = 0;
    for (i = 0; i < op.game.ncraft; i++)
        n += op.seat[i].taken != 0;
    return mode == HVL_OPEN ? n : 0;
}

static void shut(void)
{
    if (find_h)
        glos_net_close(find_h, 0);
    if (port_h)
        glos_net_close(port_h, 0);
    find_h = port_h = 0;
    mode = HVL_ALONE;
}

void hvlink_leave(void)
{
    unsigned char m[HV_NET_MOST];
    if (mode == HVL_JOINED && host_addr) {      /* (twice: one may be lost, and the opener would wait three seconds) */
        int n = hv_joiner_bye(&jn, m);
        glos_net_sendto(port_h, m, (unsigned)n, 0, host_addr, host_port);
        glos_net_sendto(port_h, m, (unsigned)n, 0, host_addr, host_port);
    }
    shut();
}

int hvlink_open(const struct hv_arena *a, int arena, int kind, int ncraft, const char *name)
{
    hvlink_leave();
    if (!(find_h = glos_net_datagram(HV_PORT, GLOS_NETF_REUSE)) || !(port_h = glos_net_datagram(0, 0))) {
        shut();
        return -1;
    }
    hv_opener_start(&op, a, arena, kind, ncraft, name);
    memset(seat_addr, 0, sizeof seat_addr);
    mode = HVL_OPEN;
    return 0;
}

int hvlink_look(const char *name)
{
    hvlink_leave();
    if (!(port_h = glos_net_datagram(0, GLOS_NETF_BROADCAST)))
        return -1;
    hv_joiner_start(&jn);
    jn_arena = 0;
    host_addr = host_port = 0;
    host_name[0] = 0;
    strncpy(my_name, name ? name : "", sizeof my_name - 1);
    asked = looked = quiet = 0;
    mode = HVL_LOOKING;
    return 0;
}

void hvlink_arena(const struct hv_arena *a)
{
    jn_arena = a;
    hv_game_start(&jn.view, a, jn.kind, jn.ncraft);     /* (something to show until the opener's first word) */
}

/* The opener: a datagram from either port answered from its own. */
static unsigned opener_hear(glos_handle from_h)
{
    unsigned char in[HV_NET_MOST], back[HV_NET_MOST];
    unsigned addr, prt, len, ev = 0;
    int n, r, k;
    for (n = 0; n < HEAR_MOST; n++) {
        addr = prt = len = 0;
        r = glos_net_recvfrom(from_h, in, sizeof in, 0, &addr, &prt, &len);
        if (r <= 0)
            break;
        /* (who: the address and the port as one number; two PCs' could only be the same by a chance in millions,
           and then one of the two would be taken for the other until it left) */
        k = hv_opener_hear(&op, (unsigned long)(addr ^ (prt * 0x9E3779B1u)), in, r, back);
        if (k <= 0)
            continue;
        if (back[3] == HV_M_TAKEN && back[4] < HV_CRAFT) {
            if (seat_addr[back[4]] != addr || seat_port[back[4]] != prt)
                ev |= HVL_SEATS;
            seat_addr[back[4]] = addr, seat_port[back[4]] = prt;
        }
        glos_net_sendto(port_h, back, (unsigned)k, 0, addr, prt);
    }
    return ev;
}

static unsigned opener_step(unsigned controls)
{
    unsigned char out[HV_NET_MOST];
    int before = hvlink_seats(), i, n;          /* (before anything is heard: one that says it leaves is a change too) */
    unsigned ev = opener_hear(find_h) | opener_hear(port_h);
    hv_opener_step(&op, controls);
    for (i = 0; i < op.game.ncraft; i++)
        if (!op.seat[i].taken)
            seat_addr[i] = seat_port[i] = 0;
    if (hvlink_seats() != before)
        ev |= HVL_SEATS;
    if (!(op.game.steps & 1)) {
        n = hv_opener_state(&op, out);
        for (i = 0; i < op.game.ncraft; i++)
            if (op.seat[i].taken && seat_addr[i])
                glos_net_sendto(port_h, out, (unsigned)n, 0, seat_addr[i], seat_port[i]);
    }
    return ev;
}

static unsigned joiner_step(unsigned controls)
{
    unsigned char in[HV_NET_MOST], out[HV_NET_MOST];
    char name[17];
    unsigned addr, prt, len, ev = 0, happened = 0;
    int offer[4], n, r, t, told = 0;
    if (mode == HVL_JOINED) {
        n = hv_joiner_keys(&jn, controls, out);
        glos_net_sendto(port_h, out, (unsigned)n, 0, host_addr, host_port);
    } else if (!host_addr) {
        if (asked++ % 15 == 0) {                /* twice a second: is anyone there? */
            n = hv_joiner_find(out);
            glos_net_sendto(port_h, out, (unsigned)n, 0, EVERYONE, HV_PORT);
        }
    } else if (asked++ % 8 == 0) {              /* a game found: a craft asked for, until one is given or refused */
        n = hv_joiner_join(my_name, out);
        glos_net_sendto(port_h, out, (unsigned)n, 0, host_addr, host_port);
    }
    for (n = 0; n < HEAR_MOST; n++) {
        addr = prt = len = 0;
        r = glos_net_recvfrom(port_h, in, sizeof in, 0, &addr, &prt, &len);
        if (r <= 0)
            break;
        if (host_addr && (addr != host_addr || prt != host_port))
            continue;                           /* (once a game is chosen, only its opener is heard) */
        t = hv_joiner_hear(&jn, jn_arena, in, r, offer, name);
        if (t == 1 && !host_addr && offer[3] > 0) {
            host_addr = addr, host_port = prt;
            memcpy(host_name, name, sizeof host_name);
            asked = 0;
        } else if (t == 2 && in[3] == HV_M_TAKEN && mode == HVL_LOOKING) {
            mode = HVL_JOINED;
            quiet = 0;
            ev |= HVL_CRAFT;
        } else if (t == 2 && in[3] == HV_M_STATE) {
            int waiting = 0;
            told = 1;
            happened |= jn.view.ev;
            /* One state a step: they come fifteen a second and the steps are thirty, so none waits long, and
               every one of them is seen (a PC that draws slowly hears them in bursts). Unless many wait: then
               the newest is what matters. */
            if (glos_get(port_h, GLOS_PROP_NET_WAITING, &waiting) != 0 || waiting <= 6)
                break;
        }
        if (jn.refused && mode == HVL_LOOKING) {        /* full, or another release's: another game, if there is one */
            jn.refused = 0;
            host_addr = host_port = 0;
        }
    }
    if (mode == HVL_JOINED) {
        if (told) {
            jn.view.ev = happened;
            quiet = 0;
            ev |= HVL_STATE;
        } else {
            hv_joiner_coast(&jn);
            if (++quiet > HV_NET_QUIET) {
                shut();
                ev |= HVL_ENDED;
            }
        }
    } else if (++looked > 10 * HV_STEPS) {
        shut();
        ev |= HVL_NOBODY;
    }
    return ev;
}

/* ---- the lobby's list of the network's open games */

int hvlink_browse(void)
{
    hvlink_leave();
    if (!(port_h = glos_net_datagram(0, GLOS_NETF_BROADCAST)))
        return -1;
    noffers = 0;
    browse_asked = 0;
    mode = HVL_BROWSING;
    return 0;
}

int hvlink_browse_step(void)
{
    unsigned char in[HV_NET_MOST], out[HV_NET_MOST];
    unsigned addr, prt, len, now = glos_ticks();
    char name[17];
    int offer[4], n, r, i;
    if (mode != HVL_BROWSING)
        return 0;
    if (!browse_asked || now - browse_asked >= 1000) {  /* once a second: who has a game open? */
        n = hv_joiner_find(out);
        glos_net_sendto(port_h, out, (unsigned)n, 0, EVERYONE, HV_PORT);
        browse_asked = now ? now : 1;
    }
    for (n = 0; n < HEAR_MOST; n++) {
        addr = prt = len = 0;
        r = glos_net_recvfrom(port_h, in, sizeof in, 0, &addr, &prt, &len);
        if (r <= 0)
            break;
        if (hv_joiner_hear(&jn, NULL, in, r, offer, name) != 1)
            continue;
        for (i = 0; i < noffers && (offers[i].addr != addr || offers[i].port != prt); i++)
            ;
        if (i == noffers) {
            if (noffers == HVL_OFFERS)
                continue;
            noffers++;
        }
        offers[i].addr = addr, offers[i].port = prt;
        memcpy(offers[i].name, name, sizeof offers[i].name);
        offers[i].kind = offer[0], offers[i].arena = offer[1], offers[i].ncraft = offer[2], offers[i].free = offer[3];
        offers[i].heard = now;
    }
    for (i = 0; i < noffers;)                   /* (gone quiet: gone) */
        if (now - offers[i].heard > 4000)
            offers[i] = offers[--noffers];
        else
            i++;
    return noffers;
}

const struct hvl_offer *hvlink_offer(int i) { return i >= 0 && i < noffers ? &offers[i] : NULL; }

int hvlink_join_offer(int i, const char *name)
{
    if (mode != HVL_BROWSING || i < 0 || i >= noffers)
        return -1;
    hv_joiner_start(&jn);
    jn_arena = 0;
    host_addr = offers[i].addr, host_port = offers[i].port;
    memcpy(host_name, offers[i].name, sizeof host_name);
    strncpy(my_name, name ? name : "", sizeof my_name - 1);
    asked = looked = quiet = 0;
    mode = HVL_LOOKING;                         /* (the port stays: JOIN is sent from the next step) */
    return 0;
}

int hvlink_set_strength(int strength)
{
    op.strength = strength < 0 ? 0 : strength > 2 ? 2 : strength;
    return op.strength;
}

unsigned hvlink_step(unsigned controls)
{
    return mode == HVL_OPEN ? opener_step(controls) : mode == HVL_ALONE || mode == HVL_BROWSING ? 0 : joiner_step(controls);
}
