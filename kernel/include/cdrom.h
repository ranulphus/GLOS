/* GLOS's own CD-ROM drives (kernel/drv/cdrom.c; docs/v1/native-dos.md, N4's CD-ROM). */
#ifndef K_CDROM_H
#define K_CDROM_H

#include "../fs/iso.h"
#include "../fs/fsv.h"

int cdrom_find(void);                   /* the ATAPI CD-ROM drives ide.c found: how many (native GLOS's alone) */
int cdrom_count(void);
/* Drive i's disc's ISO 9660 volume now (mounted afresh after a change), or 0 with *err: 0Fh no such drive, 15h
   not ready (no disc, or one that isn't ISO 9660). */
struct iso_vol *cdrom_media(int i, unsigned *err);
/* count 2048-byte sectors from lba into buf (a disc mounted first if none is): 0, or -1. */
int cdrom_read(int i, unsigned long lba, unsigned count, void *buf);
/* 1 if drive i's disc was mounted afresh since this was last asked (MSCDEX's "media changed"), else 0. */
int cdrom_changed(int i);
/* The disc's sectors (READ CAPACITY): 0, or -1 (no disc). */
int cdrom_capacity(int i, unsigned long *sectors);
/* READ TOC (format 0) into buf, len bytes at most; msf: addresses as minute, second, frame. The bytes, or -1. */
int cdrom_toc(int i, unsigned char *buf, unsigned len, int msf);
/* The drive's own audio: PLAY AUDIO(12) of count sectors from lba; STOP; PAUSE (resume 0) or RESUME; the Q
   channel's position (READ SUB-CHANNEL, 16 bytes: its header's audio status in byte 1); the tray opened or
   closed. 0, a sense key, or -1. */
int cdrom_play(int i, unsigned long lba, unsigned long count);
int cdrom_stop(int i);
int cdrom_pause(int i, int resume);
int cdrom_subq(int i, unsigned char *buf16);
int cdrom_tray(int i, int open);
/* An image as a unit (C7): an ISO 9660 file, or a CUE sheet's first data track, on volume v at path (shown: how
   it is named to people). Its unit, or -2 (none free), -3 (not opened), -4 (a sheet with no data track GLOS reads). */
int cdrom_mount_image(struct fsv_vol *v, const char *path, const char *shown);
void cdrom_unmount_image(int i);
int cdrom_is_image(int i);
struct iso_vol *cdrom_vol(int i);       /* unit i's volume (mounted or not): which open files are on it */
const char *cdrom_image_name(int i);

#endif
