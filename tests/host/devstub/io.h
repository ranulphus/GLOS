/* devstub/io.h - port I/O for dev_test.c: a 64 KB array of ports the test sets and reads. */
#ifndef K_IO_H
#define K_IO_H
#include "types.h"
extern u16 fake_ports[65536];
static inline u16 inw(u16 p) { return fake_ports[p]; }
static inline void outw(u16 p, u16 v) { fake_ports[p] = v; }
static inline u32 irq_save(void) { return 0; }
static inline void irq_restore(u32 f) { (void)f; }
#endif
