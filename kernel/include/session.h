/* Exclusive sessions (supervisor.md §11; kernel/vm/session.c). */
#ifndef K_SESSION_H
#define K_SESSION_H
#include "arch.h"

void session_exec(const struct trapframe *tf);  /* INT 21h 4B00h from V86 code */
void session_terminate(void);                   /* INT 20h, INT 21h 4Ch or 00h from V86 code */
void session_exit_code(void);                   /* INT 21h 4Dh from V86 code */
void session_stub_next(void);                   /* GLOS.EXE's stub's NEXT */
int session_active(void);
#endif
