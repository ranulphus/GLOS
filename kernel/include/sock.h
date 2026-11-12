/* Apps' TCP connections (M10 step B, kernel/net/sock.c; include/glos/api.h GLOS_FN_NET). */
#ifndef K_SOCK_H
#define K_SOCK_H
#include "types.h"

struct app;
struct trapframe;

void sock_net_poll(void);               /* the net thread, each turn: output into TCP, reads told, closes */
void sock_app_gone(struct app *a);      /* its connections closed (what they had to send still goes) */
u32 sock_count(void);                   /* connections in the table, closing ones too */

/* The calls themselves (1.x's sock_api and API 2.0's NET, kernel/app/api2net.c): 0, or a GLOS_E_ number.
   Addresses are IPv4 as they are in memory; times are milliseconds (0: no wait; FFFFFFFFh: for ever). */
int sock_resolve(struct app *a, const char *name, u32 ms, u32 *addr);
int sock_connect(struct app *a, u32 addr, u32 port, u32 ms, u32 flags, u32 *id);
int sock_send(struct app *a, u32 id, u16 ds, u32 off, u32 n, u32 ms, u32 *taken);
int sock_recv(struct app *a, u32 id, u16 ds, u32 off, u32 room, u32 ms, u32 *got, u32 *state);
int sock_close(struct app *a, u32 id, int reset);       /* (reset -1: as sock_hard says) */
int sock_hard(struct app *a, u32 id, int v);            /* its close resets it: read (-1) or set; -1 none, -2 a port */
int sock_state(struct app *a, u32 id, u32 *waiting, u32 *state, u32 *room);
int sock_peer(struct app *a, u32 id, u32 *addr, u32 *port);
int sock_listen(struct app *a, u32 port, u32 backlog, u32 flags, u32 *id);     /* a port listened on (0: any) */
int sock_accept(struct app *a, u32 id, u32 ms, u32 flags, u32 *conn, u32 *addr, u32 *port);

#endif
