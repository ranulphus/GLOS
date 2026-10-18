/* The SSH server in the kernel (sshd.c). */
#ifndef K_SSHD_H
#define K_SSHD_H

struct bootinfo;

void sshd_start(const struct bootinfo *bi);    /* the ssh thread; nothing without a host key */
void sshd_net_start(void (*kick)(void));        /* the net thread, after lwIP is up: listen on 22 */
void sshd_net_poll(void);                       /* the net thread, every turn */
void sshd_wake(void);                           /* the ssh thread to take a turn (VNC's damage: M10) */
void sshd_turn_at(unsigned tick);               /* ... and again at that tick at the latest (VNC's keys) */

#endif
