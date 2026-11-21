/* Chat's connection to a server (phase F; docs/v1/apps/chat.md): made and kept on a thread of its own (the
   name looked up, the connection made, TLS's handshake: seconds on a 486), with what came and what is to go in
   two queues between it and the window's thread. No IRC here: bytes. apps/shared/ircnet.c is GLOS's (NET,
   THREAD, sdk/tls); ircnet_none.c is the host's, where there is no network. */
#ifndef IRCNET_H
#define IRCNET_H

struct ircnet;

/* A connection begun to host:port. tls: checked against the trust anchors in `certs` (CERTS.DAT's path), by the
   host's name. address: where the host is, as dotted numbers, or NULL to look its name up. NULL: no memory, no
   thread. */
struct ircnet *ircnet_open(const char *host, unsigned port, int tls, const char *certs, const char *address);

enum { IRCNET_CONNECTING, IRCNET_ON, IRCNET_ENDED };
/* How it is; once ended, why (a few words: "no such name", "refused", "not trusted", "closed", ...). */
int ircnet_state(struct ircnet *n, char *why, int room);
int ircnet_read(struct ircnet *n, char *buf, int room);         /* bytes that have come: how many (0: none now) */
int ircnet_write(struct ircnet *n, const char *bytes, unsigned len);    /* queued to go: 0; -1: no room, or ended */
void ircnet_close(struct ircnet *n);    /* what is queued is sent first (a QUIT), within a second or two; freed */

#endif
