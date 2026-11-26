/* A display card the BIOS couldn't start, made GLOS's display ([display] card; kernel/drv/pcicard.c). */
#ifndef K_DRV_PCICARD_H
#define K_DRV_PCICARD_H

#include "types.h"

struct bootinfo;
void pcicard_place(const struct bootinfo *bi);  /* kmain, before display_init: its ranges placed */
int pcicard_named(void);                        /* 1: a card to start (pcicard_start) */
int pcicard_start(u32 stack_seg, u32 stack_bytes);      /* the system VM's thread: its BIOS run; 0 when it is up */

#endif
