/* Chat's connection to a server (ircnet.h), on GLOS: a thread that looks the name up, connects, does TLS's
 * handshake where asked, and then goes round: what is queued to go is sent, what comes within a tenth of a
 * second is queued for the window's thread. A lock is round the queues and the state alone: the network and
 * TLS are the thread's own. (A program with this is linked with gloswrap.opt, as every program with threads.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/tls.h>

#include "ircnet.h"

#define QUEUE 16384

struct ircnet {
    char host[128], certs[260], address[20];
    unsigned port;
    int tls;
    glos_handle thread, lock;
    int state, leave;                   /* leave: the window's thread wants it ended */
    char why[64];
    char in[QUEUE], out[QUEUE];         /* rings */
    unsigned in_at, in_n, out_at, out_n;
};

static void ended(struct ircnet *n, const char *why)
{
    glos_take(n->lock, 0xFFFFFFFFu);
    snprintf(n->why, sizeof n->why, "%s", why);
    n->state = IRCNET_ENDED;
    glos_give(n->lock);
}

static unsigned dotted(const char *s, unsigned *address)        /* "10.0.2.2" as GLOS has addresses: 1, or 0 */
{
    unsigned a, b, c, d;
    char after;
    if (sscanf(s, "%u.%u.%u.%u%c", &a, &b, &c, &d, &after) != 4 || a > 255 || b > 255 || c > 255 || d > 255)
        return 0;
    *address = a | b << 8 | c << 16 | d << 24;
    return 1;
}

static void run(void *arg)
{
    struct ircnet *n = arg;
    struct tls_anchors anchors = { 0, 0 };
    struct tls *t = NULL;
    glos_handle conn;
    unsigned address = 0, st = 0;
    char why[64], piece[1024];
    int r;
    if (!dotted(n->address[0] ? n->address : n->host, &address) && glos_net_resolve(n->host, 15000, &address) != 0) {
        ended(n, "no such name");
        return;
    }
    if (!(conn = glos_net_connect(address, n->port, 20000, 0))) {
        ended(n, glos_error() == GLOS_E_NET_DOWN ? "no network" : "no answer, or refused");
        return;
    }
    if (n->tls) {
        if (tls_anchors_load(&anchors, n->certs) != 0) {
            glos_net_close(conn, 1);
            ended(n, "no trust anchors (CERTS.DAT)");
            return;
        }
        if (!(t = tls_open(conn, n->host, &anchors, 20000, why, sizeof why))) {        /* (it closed the connection) */
            tls_anchors_free(&anchors);
            ended(n, why);
            return;
        }
    }
    glos_take(n->lock, 0xFFFFFFFFu);
    n->state = IRCNET_ON;
    glos_give(n->lock);
    for (;;) {
        unsigned len = 0, i;
        int leave;
        glos_take(n->lock, 0xFFFFFFFFu);        /* what is to go: a piece of it */
        leave = n->leave;
        for (; len < sizeof piece && n->out_n; len++, n->out_n--)
            piece[len] = n->out[n->out_at], n->out_at = (n->out_at + 1) % QUEUE;
        glos_give(n->lock);
        if (len) {
            if (t) {
                r = tls_write(t, piece, len);
            } else {                            /* (a plain send may take a part of it) */
                for (i = 0, r = 1; i < len && r > 0; i += r > 0 ? (unsigned)r : 0)
                    r = glos_net_send(conn, piece + i, len - i, 20000);
            }
            if (r <= 0) {
                snprintf(why, sizeof why, "%s", t ? tls_why(t) : "connection lost");
                break;
            }
            continue;                           /* (more may be queued: all of it before listening again) */
        }
        if (leave) {
            snprintf(why, sizeof why, "closed");
            break;
        }
        r = t ? tls_read_wait(t, piece, sizeof piece, 100) : glos_net_receive(conn, piece, sizeof piece, 100, &st);
        if (t ? r == -2 : (r == 0 && st != GLOS_NETS_CLOSED))
            continue;                           /* nothing just now */
        if (r <= 0) {
            snprintf(why, sizeof why, "%s", r == 0 ? "closed by the server" : t ? tls_why(t) : "connection lost");
            break;
        }
        glos_take(n->lock, 0xFFFFFFFFu);
        for (i = 0; i < (unsigned)r && n->in_n < QUEUE; i++, n->in_n++)
            n->in[(n->in_at + n->in_n) % QUEUE] = piece[i];
        glos_give(n->lock);
        while (i < (unsigned)r && !n->leave) {  /* (the window's thread is behind: wait for room, drop nothing) */
            glos_sleep(20);
            glos_take(n->lock, 0xFFFFFFFFu);
            for (; i < (unsigned)r && n->in_n < QUEUE; i++, n->in_n++)
                n->in[(n->in_at + n->in_n) % QUEUE] = piece[i];
            glos_give(n->lock);
        }
    }
    if (t)
        tls_close(t);
    else
        glos_net_close(conn, 0);
    if (n->tls)
        tls_anchors_free(&anchors);
    ended(n, why);
}

struct ircnet *ircnet_open(const char *host, unsigned port, int tls, const char *certs, const char *address)
{
    struct ircnet *n = calloc(1, sizeof *n);
    if (!n)
        return NULL;
    snprintf(n->host, sizeof n->host, "%s", host);
    snprintf(n->certs, sizeof n->certs, "%s", certs ? certs : "");
    snprintf(n->address, sizeof n->address, "%s", address ? address : "");
    n->port = port, n->tls = tls;
    if (!(n->lock = glos_lock()) || !(n->thread = glos_thread(run, n, 96 * 1024, 0))) {
        if (n->lock)
            glos_close(n->lock);
        free(n);
        return NULL;
    }
    return n;
}

int ircnet_state(struct ircnet *n, char *why, int room)
{
    int state;
    glos_take(n->lock, 0xFFFFFFFFu);
    state = n->state;
    if (why && room > 0)
        snprintf(why, (size_t)room, "%s", n->why);
    glos_give(n->lock);
    return state;
}

int ircnet_read(struct ircnet *n, char *buf, int room)
{
    int got = 0;
    glos_take(n->lock, 0xFFFFFFFFu);
    for (; got < room && n->in_n; got++, n->in_n--)
        buf[got] = n->in[n->in_at], n->in_at = (n->in_at + 1) % QUEUE;
    glos_give(n->lock);
    return got;
}

int ircnet_write(struct ircnet *n, const char *bytes, unsigned len)
{
    unsigned i;
    int r = -1;
    glos_take(n->lock, 0xFFFFFFFFu);
    if (n->state != IRCNET_ENDED && len <= QUEUE - n->out_n) {
        for (i = 0; i < len; i++, n->out_n++)
            n->out[(n->out_at + n->out_n) % QUEUE] = bytes[i];
        r = 0;
    }
    glos_give(n->lock);
    return r;
}

void ircnet_close(struct ircnet *n)
{
    unsigned code;
    if (!n)
        return;
    glos_take(n->lock, 0xFFFFFFFFu);
    n->leave = 1;
    glos_give(n->lock);
    if (glos_join(n->thread, 2500, &code) != 1) {       /* (in a handshake, or a name being looked up: it is left to
                                                           end by itself, and what it uses is left with it) */
        glos_detach(n->thread);
        return;
    }
    glos_close(n->lock);
    free(n);
}
