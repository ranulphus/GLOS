/* Chat's connection to a server (ircnet.h) where there is no network: the host's stand-in for the toolkit's
   tests, in which Chat talks to a server made up inside it (CHAT -demo). Every connection ends at once. */
#include <stdio.h>
#include <stdlib.h>

#include "ircnet.h"

struct ircnet {
    int unused;
};

struct ircnet *ircnet_open(const char *host, unsigned port, int tls, const char *certs, const char *address)
{
    (void)host, (void)port, (void)tls, (void)certs, (void)address;
    return calloc(1, sizeof(struct ircnet));
}

int ircnet_state(struct ircnet *n, char *why, int room)
{
    (void)n;
    if (why && room > 0)
        snprintf(why, (size_t)room, "no network");
    return IRCNET_ENDED;
}

int ircnet_read(struct ircnet *n, char *buf, int room) { (void)n, (void)buf, (void)room; return 0; }
int ircnet_write(struct ircnet *n, const char *bytes, unsigned len) { (void)n, (void)bytes, (void)len; return -1; }
void ircnet_close(struct ircnet *n) { free(n); }
