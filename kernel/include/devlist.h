/* Every device GLOS finds, and what serves each one: the Control Panel's Devices page, the shell's DEVICES and
 * SYS's STAT GLOS_STAT_DEVICES (kernel/drv/devlist.c; API 2.12). Also [devices] off: the devices whose GLOS
 * drivers leave them to DOS, which each driver asks as it looks for its device. */
#ifndef K_DEVLIST_H
#define K_DEVLIST_H
#include "pci.h"

#define DEVS_ON      1                  /* one of GLOS's drivers has it */
#define DEVS_OFF     2                  /* its driver left it to DOS: [devices] off names it */
#define DEVS_REFUSED 3                  /* its driver could not have it (why) */

/* What a driver did with a device, for the list: id is a PCI function's place ("00:03.0") or an ISA device's
   name (sb16, crystal, ne2000); res its ISA resources, as the list writes them ("io=0220-022f irq=5"), or 0. */
void dev_note(const char *id, const char *driver, int state, const char *why, const char *res);
/* 1 if [devices] off names the device (the driver then leaves it alone, and notes it as off). */
int dev_turned_off(const char *id);
int dev_pci_off(struct pci_dev *d, const char *driver);         /* the same for a PCI function, noted if so */
u32 dev_list(char *out, u32 room);      /* the list: a line a device, its details under it (devlist.c says how) */

#endif
