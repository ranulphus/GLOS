/* PCI devices' names, from PCIIDS.DAT (tools/mkpciids.py makes it from the PCI ID Project's list; THIRD_PARTY.md):
   the Control Panel's Devices page. One pass through the packed list finds every name asked for; nothing of it
   is kept but those. */
#ifndef PCIIDS_H
#define PCIIDS_H

struct pci_name {
    unsigned short vendor, device;      /* asked */
    char vendor_name[64], device_name[96];      /* -> the list's ("" where it has none) */
};

/* The names of n devices, from the file at path: the number of the n whose vendor it knows; -1 no such file, or
   not one of mkpciids.py's (the names are then ""). */
int pciids_lookup(const char *path, struct pci_name *want, int n);
/* The same from a file the caller opened (a FILE *: the toolkit's gui_open_beside, say), which it closes. */
int pciids_lookup_file(void *file, struct pci_name *want, int n);

#endif
