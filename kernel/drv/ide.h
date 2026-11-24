/* GLOS's own IDE driver (docs/v1/native-dos.md, N1c): the channels, kernel/drv/ata.c over them. */
#ifndef K_IDE_H
#define K_IDE_H

#include "ata.h"

/* The channels found and their devices asked (once; again after ide_forget). own: ATA_PROBE_OWN (native GLOS
   alone drives them) or 0 (hosted: the BIOS beside). How many ATA disks were found. */
int ide_probe(int own);
/* While on, the calling thread's waits for a disk leave the processor to other threads (interrupts on; after 64
   looks at the clock within a tick, asleep to the next) rather than keeping it: for the VM's thread in a call of
   GLOS's own DOS (kernel/vm/vdos.c), which is long and which nothing else waits behind. Off (the disk cache's
   INT 13h, in a trap's path): as before. */
void ide_patient(int on);
/* A patient wait's two calls out (0: none), the system VM's DOS's: fn at every look at the clock (it takes its
   waiting interrupts there); nap for the sleep to the next tick (it sleeps where an interrupt wakes it). */
void ide_wait_hook(void (*fn)(void), void (*nap)(void));
/* Device i (0 ...), or 0 past the last; present ones and absent alike. */
struct ata_dev *ide_dev(int i);
/* An ATA error as INT 13h's status (AH): 10h a bad sector, 04h not found, CCh a write fault, 80h a time out ... */
unsigned ide_bios_error(const struct ata_dev *d, int r);

#endif
