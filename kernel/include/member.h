/* A cable's far PC as a member of this PC's network (kernel/net/member.c; docs/v1/cable-network.md, N1). The
 * net thread's. */
#ifndef K_MEMBER_H
#define K_MEMBER_H
#include "types.h"

struct netif;
struct pbuf;

void member_links(struct netif *card, struct netif *cable);     /* the interfaces as they are now: the card's (or
                                                            NULL), and the cable's when this PC is its server */
void member_card_frame(const u8 *frame, u32 len);        /* every frame the card receives, before lwIP has it */
void member_turn(void);                                  /* now and then: leases that have run out */
u32 member_stat(char *buf, u32 max);                     /* glos net's lines for it */
/* lwIP's hooks (kernel/net/port/lwipopts.h): */
int member_ip_hook(struct pbuf *p, struct netif *inp);
struct netif *member_route(const void *src, const void *dest);

#endif
