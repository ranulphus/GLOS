/* Idle time, the screen saver and the screen's power (kernel/wm/idle.c; API 2.0's IDLE; docs/v1/shell.md). */
#ifndef K_IDLE_H
#define K_IDLE_H
#include "types.h"

void idle_touch(void);                  /* somebody touched the keyboard, the mouse or a pad (interrupts on or off) */
u32 idle_ms(void);                      /* how long since */
void idle_config(void);                 /* [saver]'s settings changed (or were read) */
void idle_awake(int delta);             /* an AWAKE handle opened (1) or closed (-1) */
u32 idle_awake_count(void);
int idle_saver(void);                   /* 1: the saver is on the screen */
u32 idle_saver_wait(void);              /* [saver] wait in seconds, 0 never */
int idle_saver_ask(int on);             /* start it now, or end it (the desktop's thread does): 0, or -1 no desktop */
/* The desktop's thread, each turn: screen: the desktop has the screen; busy: something on it must not be covered
   (a full-screen window has the focus). Starts and ends the saver, and turns the screen's power off and on. 1: the
   saver has the screen. */
int idle_turn(int screen, int busy);
void idle_hook(void (*told)(int on));   /* who is told when the saver comes on or goes off */
void idle_pretend(u32 seconds);         /* for tests: as if nobody had touched anything for so long more */
u32 idle_stat(char *buf, u32 max);      /* glos idle: a line */

#endif
