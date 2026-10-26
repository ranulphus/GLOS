/* Who keeps each device, and taking an interrupt line that others share
 * (kernel/drv/dev.c; docs/devices.md, step D1). */
#ifndef K_DEV_H
#define K_DEV_H
#include "pci.h"

/* May one of GLOS's drivers take d's IRQ line? Every other function on it must be quiet, or made quiet now by
   its class's quiet driver: 0. Otherwise -1, and with names the function in the way ("00:02.2"). */
int dev_take_line(struct pci_dev *d, char *with, u32 room);

/* DOS wrote the configuration space a CF8h address selects (vm/vdev.c): a quiet device it writes to is its again. */
void dev_dos_wrote(u32 cf8);

#endif
