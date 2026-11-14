/* The PC's own screen and keyboard as a terminal of GLOS's shell (kernel/shell/lcon.c; docs/v1/shell.md, S2d). */
#ifndef K_LCON_H
#define K_LCON_H
#include "types.h"

struct shsess;

void lcon_init(void);                   /* its thread (agent mode alone); it waits for lcon_ready() */
void lcon_ready(void);                  /* the VM thread, at its idle screen for the first time */
int lcon_on(void);                      /* 0 once it is known to be off ([shell] prompt = off, no agent, leaving) */
void lcon_leave(void);                  /* GLOS is leaving: the screen and the keyboard are DOS's */
int lcon_key(u8 b);                     /* (interrupts off) a byte from the keyboard: 1 if the terminal took it */
void lcon_note(const char *text);       /* (any context) a line the person at the PC should see: above the prompt */
void lcon_network(const char *ip, const char *name);     /* ... the network has an address (ip), or has lost it (0) */
void lcon_program(void);                /* (the VM thread, interrupts off) a DOS program is about to have the screen */
void lcon_poke(void);                   /* (any context) a turn now: a program that had the screen has ended */
void lcon_settle(struct shsess *s);     /* (its session's thread) wait until what it wrote is on the screen */

#endif
