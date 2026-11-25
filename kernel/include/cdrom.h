/* GLOS's own CD-ROM drives (kernel/drv/cdrom.c; docs/v1/native-dos.md, N4's CD-ROM). */
#ifndef K_CDROM_H
#define K_CDROM_H

#include "../fs/iso.h"

int cdrom_find(void);                   /* the ATAPI CD-ROM drives ide.c found: how many (native GLOS's alone) */
int cdrom_count(void);
/* Drive i's disc's ISO 9660 volume now (mounted afresh after a change), or 0 with *err: 0Fh no such drive, 15h
   not ready (no disc, or one that isn't ISO 9660). */
struct iso_vol *cdrom_media(int i, unsigned *err);

#endif
