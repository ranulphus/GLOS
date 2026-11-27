/* Places for PCI ranges the BIOS left unplaced (docs/v1/native-dos.md, N4: a display card the BIOS couldn't start,
 * step D1): a memory BAR, or a ROM, of a device behind a PCI-to-PCI bridge, and the bridge's window widened to
 * take it. Plain C over lists of ranges, so tests/host/pcialloc_test.c runs it; kernel/drv/pci.c gathers the
 * ranges and writes the registers.
 *
 * A place is aligned to its size (a power of two, as PCI's BARs are). It is looked for first in the bridge's
 * window as it is; then anywhere between floor and ceil, from the top down (where BIOSes put PCI memory), with the
 * window made the smallest one, in whole megabytes (a bridge's windows are), that holds the new range and every
 * range already behind the bridge in that window (keep). Neither the range nor the window may touch a busy
 * range: other functions' BARs, other bridges' windows, this bridge's other window, RAM and the firmware's. */
#ifndef K_DRV_PCIALLOC_H
#define K_DRV_PCIALLOC_H

struct pa_range {
    unsigned long base, last;           /* the first and the last byte (a window with base > last is empty) */
};

/* 0 with *at (and *win, widened or moved if it had to be), or -1: no place. */
int pa_place(const struct pa_range *busy, int nbusy, const struct pa_range *keep, int nkeep, struct pa_range *win,
             unsigned long size, unsigned long floor, unsigned long ceil, unsigned long *at);

#endif
