/* kstub/io.h - the kernel's io.h for host tests of kernel code that only locks
 * (cfg_test.c): interrupts are nothing to turn off in a host process. */
#ifndef K_IO_H
#define K_IO_H
#include "types.h"
static inline u32 irq_save(void) { return 0; }
static inline void irq_restore(u32 f) { (void)f; }
#endif
