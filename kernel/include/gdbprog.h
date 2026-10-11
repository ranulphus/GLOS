/* glos gdb: gdb's remote protocol for the program in front (kernel/dbg/gdbprog.c). */
#ifndef K_GDBPROG_H
#define K_GDBPROG_H
#include "types.h"

struct trapframe;

#define GDBP_PORT 2345                  /* the port an ssh -L or -W asks for */

/* The VM thread, interrupts off. */
int gdbp_trap(struct trapframe *tf);    /* a client's #DB or INT3: 1 if the debugger took it */
void gdbp_return(struct trapframe *tf); /* on the way back to a client: stops it when asked to */
int gdbp_stopped(void);                 /* the program waits for gdb */
void gdbp_program_end(u32 code);        /* its program has ended */

/* The ssh thread. */
void gdbp_arm(void (*wake)(void));      /* glos gdb: stop the program when it next runs its 32-bit code */
int gdbp_armed(void);                   /* armed, stopped or attached */
int gdbp_attach(void (*send)(const u8 *d, u32 n));      /* gdb's channel has opened: 0, or -1 if there is no one to debug */
void gdbp_input(const u8 *d, u32 n);    /* its bytes */
void gdbp_turn(void);                   /* each turn: a stop gdb waits to hear of */
void gdbp_detach(void);                 /* the channel has gone, or gdb let go: the program runs on */

#endif
