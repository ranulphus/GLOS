/* ne2kstub/io.h - port I/O for ne2k_test.c: the test's own model of an NE2000 answers. */
#ifndef K_IO_H
#define K_IO_H
#include "types.h"
u8 inb(u16 p);
void outb(u16 p, u8 v);
u16 inw(u16 p);
void outw(u16 p, u16 v);
#endif
