/* GLOS's own IDE driver (docs/v1/native-dos.md, N1c): the channels, kernel/drv/ata.c over them. */
#ifndef K_IDE_H
#define K_IDE_H

#include "ata.h"

/* The channels found and their devices asked (once; again after ide_forget). own: ATA_PROBE_OWN (native GLOS
   alone drives them) or 0 (hosted: the BIOS beside). How many ATA disks were found. */
int ide_probe(int own);
/* While on, a wait for a disk gives the processor to other threads (each look at the clock: interrupts on and a
   yield) rather than keeping it: for the VM's thread in a call of GLOS's own DOS (kernel/vm/vdos.c), which is
   long and which nothing else waits behind. Off (the disk cache's INT 13h, in a trap's path): as before. */
void ide_patient(int on);
/* Device i (0 ...), or 0 past the last; present ones and absent alike. */
struct ata_dev *ide_dev(int i);
/* An ATA error as INT 13h's status (AH): 10h a bad sector, 04h not found, CCh a write fault, 80h a time out ... */
unsigned ide_bios_error(const struct ata_dev *d, int r);

#endif
