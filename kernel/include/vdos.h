/* The system VM's own DOS on a native boot (kernel/vm/vdos.c; docs/v1/native-dos.md, native boot, NB2). */
#ifndef K_VDOS_H
#define K_VDOS_H
#include "types.h"

struct bootinfo;
struct trapframe;

int vdos_c_bios(void);                  /* C: is read and written by the BIOS's calls (no driver of GLOS's has it) */
int vdos_usb_bios(void);                /* ... and may be a USB disk: the USB controllers are left to the BIOS */
int vdos_init(struct bootinfo *bi);     /* (vm_start, BI_F_NATIVE): the loader adopted, DOS's tables made; bootinfo's
                                           indos, sda and lol filled; 0, or -1 */
int vdos_on(void);                      /* 1 once it answers the system VM's INT 20h and 21h */
/* INT n (20h, 21h): ret the program's return address, at the INT's own (a call made again after a wait),
   via_arpl when it came through the vector (a far call). */
void vdos_int(struct trapframe *tf, u8 n, u32 ret, u32 at, int via_arpl);
int vdos_arpl(struct trapframe *tf);    /* a #UD at INT 21h's or 20h's ARPL: 1 if it was one, answered */

#endif
