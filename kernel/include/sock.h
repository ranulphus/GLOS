/* Apps' TCP connections (M10 step B, kernel/net/sock.c; include/glos/api.h GLOS_FN_NET). */
#ifndef K_SOCK_H
#define K_SOCK_H
#include "types.h"

struct app;
struct trapframe;

int sock_api(struct app *a, struct trapframe *tf);      /* GLOS_FN_NET: the frame's answer (api.c's convention) */
void sock_net_poll(void);               /* the net thread, each turn: output into TCP, reads told, closes */
void sock_app_gone(struct app *a);      /* its connections closed (what they had to send still goes) */
u32 sock_count(void);                   /* connections in the table, closing ones too */

#endif
