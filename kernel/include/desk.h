/* The desktop (kernel/wm/desk.c): the window manager's core (wm.h) in the
 * kernel, with memory, the screen, a thread of its own, and (steps E and F)
 * input and apps. */
#ifndef K_DESK_H
#define K_DESK_H
#include "wm.h"

void desk_init(void);                   /* the manager, its thread, and the display's painter */
struct wm *desk_lock(void);             /* the manager, for the caller alone until desk_unlock() (it may wait) */
void desk_unlock(void);                 /* ... and the composer told to look */
void desk_kick(void);                   /* something changed (from anywhere, interrupts on or off) */
void desk_note(const char *note);       /* the task bar's note: a remote session's (PRD D35), or "" */
int desk_demo(int on);                  /* glos desk demo|clear: wm/demo.c's windows; -1: no memory */
u32 desk_stat(char *buf, u32 max);      /* glos desk: its windows, a line each */

#endif
