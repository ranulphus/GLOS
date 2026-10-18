/* The VNC server (M10 step F, kernel/wm/vnc.c; PRD §9.8): RFB 3.8 on an SSH
 * direct-tcpip channel to 127.0.0.1:5900, when GLOS.CFG's [vnc] enable is
 * on. The ssh thread's: opened, fed and turned from kernel/ssh/sshd.c. */
#ifndef K_VNC_H
#define K_VNC_H
#include "types.h"

struct ssh_chan;
struct gfx_region;
struct gfx_surface;

int vnc_open(struct ssh_chan *ch);      /* a viewer: 0, or -1 (off, or too many) */
void vnc_input(struct ssh_chan *ch, const u8 *d, u32 n);
int vnc_turn(struct ssh_chan *ch, void *arg);   /* sshd's stream turn: an update when asked and due */
void vnc_gone(struct ssh_chan *ch);
int vnc_owns(const struct ssh_chan *ch);
/* The desktop's thread, before each compose (the desktop locked): what is about to change. */
void vnc_damage(const struct gfx_region *dirty, const struct gfx_surface *fb);
/* A window moved, in front of everything (deskin.c): its frame from src to (dx, dy) on. */
void vnc_moved(s32 x, s32 y, s32 w, s32 h, s32 dx, s32 dy);
u32 vnc_viewers(void);
void vnc_keys_turn(void);               /* the ssh thread, each turn: viewers' keys the keyboard now has room for (a
                                           viewer that has gone too: vncdo sends and leaves) */

#endif
