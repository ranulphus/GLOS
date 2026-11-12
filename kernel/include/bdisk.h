/* The BIOS's disks by their sectors (kernel/disk/bdisk.c; docs/v1/shell.md, step S0). */
#ifndef K_BDISK_H
#define K_BDISK_H
#include "types.h"

struct bdisk_geo {
    u32 sectors;                        /* the disk's size */
    u16 cyls, heads, spt;               /* the BIOS's geometry (what a partition table is written by) */
    u8 edd;                             /* the BIOS's LBA calls reach it */
};

/* For a kernel thread that may wait (never the VM's, nor the ssh thread). The BIOS is called by the VM thread
   when the stub next asks what to do (nothing else is in DOS or the BIOS then), through the disk cache, which
   stays right. Each: 0; a positive number, the BIOS's error (INT 13h's AH); BDISK_NONE, BDISK_MEMORY or
   BDISK_BUSY. */
#define BDISK_NONE   (-1)               /* no such disk */
#define BDISK_MEMORY (-2)               /* DOS has no memory for the sectors' buffer */
#define BDISK_BUSY   (-3)               /* a program has the system VM, and it did not come round in time */

void bdisk_open(void);                  /* a user begins ... */
void bdisk_close(void);                 /* ... and ends: the buffer in DOS's memory goes with the last */
int bdisk_geo(u8 drive, struct bdisk_geo *g);           /* drive: the BIOS's number (00h, 80h, ...) */
u32 bdisk_run(void);                    /* the most sectors a call below takes (64, or what the buffer holds) */
int bdisk_read(u8 drive, u32 lba, u32 count, void *data);
int bdisk_write(u8 drive, u32 lba, u32 count, const void *data);

/* The VM thread's side (trap context, the stub idle). */
int bdisk_vm_pending(void);
void bdisk_vm_turn(void);

/* `glos disktest ...` (a stopgap for the tests until the shell's DISK and DD: S3): the work is a thread's, its
   lines the log's (GLOS-DISKTEST). 0, or -1: not a command of it. */
int bdisk_test(const char *args);

#endif
