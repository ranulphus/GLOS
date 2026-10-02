/* The kernel's gdb remote stub on a serial port (supervisor.md §19). */
#ifndef K_GDB_H
#define K_GDB_H
#include "types.h"

void gdb_init(u16 port);
static inline void gdb_breakpoint(void) { __asm__ volatile("int3"); }

#endif
