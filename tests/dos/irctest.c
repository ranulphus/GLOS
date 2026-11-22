/* IRCTEST: Chat's connection and its IRC, on a real GLOS against a real server (Loop A's https suite runs one on
 * the host: tests/loopa/jobs.py's IrcServer): apps/shared/ircnet.c (the connection's thread, plain or TLS) and
 * apps/shared/irc.c, with no window.
 *
 *     IRCTEST HOST[=ADDRESS] PORT plain|tls NICK CHANNEL [ACCOUNT PASSWORD]
 *
 * Connects (HOST is the name checked in the certificate; ADDRESS where it is, if its name is not to be looked
 * up), is announced (by SASL with an account), joins CHANNEL, says "ping from GLOS", waits for an answer that
 * begins "pong", and leaves. A line for each step:
 *
 *     IRCTEST on nick=NICK network=NAME          (once the server has named its network, or 5 s after its welcome)
 *     IRCTEST joined CHANNEL users=N topic=TOPIC
 *     IRCTEST heard NICK at WHEN: TEXT
 *     IRCTEST ended: WHY
 *
 * or "IRCTEST failed: WHY". Exit 0 if all of it happened. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/tls.h>

#include "../../apps/shared/irc.h"
#include "../../apps/shared/ircnet.h"

static struct ircnet *net;
static struct irc *irc;
static int names_in, heard, lines_seen;
static struct irc_buffer *channel;

static void a_send(void *ctx, const char *bytes, unsigned n)
{
    (void)ctx;
    ircnet_write(net, bytes, n);
}

static void an_event(void *ctx, int what, struct irc_buffer *b)
{
    (void)ctx;
    if (what == IRC_E_BUFFER && b->kind == IRC_B_CHANNEL)
        channel = b;
    if (what == IRC_E_USERS && b == channel && b->nusers > 1)
        names_in = 1;
    if (what == IRC_E_LINE && b == channel) {
        const struct irc_line *l = irc_line_at(b, b->nlines - 1);
        if (l && l->kind == IRC_L_MSG && !l->mine) {
            printf("IRCTEST heard %s at %lu: %s\n", l->nick, l->when, l->text);
            if (!strncmp(l->text, "pong", 4))
                heard = 1;
        }
        lines_seen++;
    }
}

static unsigned long a_clock(void *ctx)
{
    struct glos_time t = { sizeof t, 0, 0, 0 };
    (void)ctx;
    glos_time(&t);
    return t.utc;
}

int main(int argc, char **argv)
{
    struct irc_opts o;
    char host[128], certs[300], why[80], piece[512], *address;
    const char *home;
    unsigned waited, on_at = 0;
    int state, said_on = 0, said_joined = 0, said_ping = 0, quit = 0, r;
    if (argc < 6) {
        printf("IRCTEST HOST[=ADDRESS] PORT plain|tls NICK CHANNEL [ACCOUNT PASSWORD]\n");
        return 2;
    }
    if (glos_init("IRCTEST", 0, 0) != 0) {          /* (an app: it has a thread) */
        printf("IRCTEST no GLOS\n");
        return 2;
    }
    snprintf(host, sizeof host, "%s", argv[1]);
    if ((address = strchr(host, '=')) != NULL)
        *address++ = 0;
    home = glos_home();
    snprintf(certs, sizeof certs, "%s%s%s", home, home[0] && home[strlen(home) - 1] != '\\' ? "\\" : "", TLS_CERTS);
    if (!(net = ircnet_open(host, (unsigned)atoi(argv[2]), !strcmp(argv[3], "tls"), certs, address))) {
        printf("IRCTEST failed: no thread\n");
        return 1;
    }
    for (waited = 0; (state = ircnet_state(net, why, sizeof why)) == IRCNET_CONNECTING && waited < 60000; waited += 50)
        glos_sleep(50);
    if (state != IRCNET_ON) {
        printf("IRCTEST failed: %s\n", state == IRCNET_ENDED ? why : "timed out");
        ircnet_close(net);
        return 1;
    }
    memset(&o, 0, sizeof o);
    o.nick = argv[4], o.alt = "", o.user = "glos", o.real = "IRCTEST on GLOS";
    o.account = argc > 7 ? argv[6] : "", o.password = argc > 7 ? argv[7] : "";
    o.send = a_send, o.event = an_event, o.now = a_clock;
    if (!(irc = irc_new(&o))) {
        printf("IRCTEST failed: no memory\n");
        return 1;
    }
    irc_start(irc);
    for (waited = 0; waited < 90000; waited += 20) {
        while ((r = ircnet_read(net, piece, sizeof piece)) > 0)
            irc_feed(irc, piece, (unsigned)r);
        if (irc_state(irc) == IRC_ON && !said_on && !on_at)
            on_at = waited + 1;
        /* (said once the network has named itself: 005 comes after the welcome, 001, which is when the state is
           ON, and over TLS on a slow PC they come in reads of their own; a server that names none: 5 s after) */
        if (on_at && !said_on && (irc_network(irc)[0] || waited + 1 - on_at >= 5000)) {
            said_on = 1;
            printf("IRCTEST on nick=%s network=%s\n", irc_nick(irc), irc_network(irc));
            snprintf(piece, sizeof piece, "/join %s", argv[5]);
            irc_input(irc, irc_buffer(irc, 0), piece);
        }
        if (names_in && !said_joined) {
            said_joined = 1;
            printf("IRCTEST joined %s users=%u topic=%s\n", channel->name, channel->nusers, channel->topic);
        }
        if (said_joined && !said_ping) {
            said_ping = 1;
            irc_input(irc, channel, "ping from GLOS");
        }
        if (heard && !quit) {
            quit = 1;
            irc_input(irc, channel, "/quit done");
        }
        if (ircnet_state(net, why, sizeof why) == IRCNET_ENDED) {
            while ((r = ircnet_read(net, piece, sizeof piece)) > 0)     /* (its last words) */
                irc_feed(irc, piece, (unsigned)r);
            break;
        }
        glos_sleep(20);
    }
    irc_stopped(irc);
    printf("IRCTEST ended: %s\n", ircnet_state(net, why, sizeof why) == IRCNET_ENDED ? why : "timed out");
    ircnet_close(net);
    irc_free(irc);
    return said_on && said_joined && heard && quit ? 0 : 1;
}
