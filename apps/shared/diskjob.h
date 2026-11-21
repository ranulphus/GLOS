/* Disk Tools' long jobs (phase F; docs/v1/apps/disk-tools.md): a disk's sectors to a file, a file to a disk's
   sectors, every sector read to see that it can be. Over a call that reads and writes sectors (the app's goes
   to GLOS_FS_SECTORS; a test's to memory) and stdio's files: one code for the app and for the host's tests.
   A sector that fails is tried again alone, three times; one that stays bad is counted, its place noted, and
   (read to a file) written as zeros, so that the image has every sector at its own place. */
#ifndef DISKJOB_H
#define DISKJOB_H

#include <stdio.h>

/* `count` sectors of 512 bytes from `lba` read into data, or (write nonzero) written: 0, or the disk's error. */
typedef int (*diskjob_io)(void *ctx, unsigned long lba, unsigned char *data, unsigned count, int write);
/* Told as the job goes (done of total sectors): nonzero stops it. */
typedef int (*diskjob_tick)(void *arg, unsigned long done, unsigned long total);

struct diskjob_report {
    unsigned long done;                 /* sectors gone through */
    unsigned long bad;                  /* ... of which so many could not be read (or written, or were not as written) */
    unsigned long first_bad;            /* the first of those */
    unsigned long bad_list[16];         /* the first sixteen of them */
    int stopped;                        /* the tick stopped it */
    char why[96];                       /* why it failed, when it did (a file's error, sizes that differ) */
};

/* `run`, in each: how many sectors go at a call, 1 to 32 (0: 32). A floppy's sectors a track make each call one
   turn of its disk; runs that cross tracks wait for the disk to come round again in between. */

/* The disk's `total` sectors written to the file. 0: all of it (bad sectors as zeros, counted); -1: stopped, or
   the file could not be written (why). */
int diskjob_to_file(diskjob_io io, void *ctx, unsigned long total, unsigned run, FILE *out, diskjob_tick tick, void *arg,
                    struct diskjob_report *r);
/* The file's bytes (which must be `total` sectors) written to the disk, then (verify) read back and compared.
   0: written (and the same); -1: stopped, the file is of another size or could not be read, or sectors failed. */
int diskjob_from_file(diskjob_io io, void *ctx, unsigned long total, unsigned run, FILE *in, int verify, diskjob_tick tick,
                      void *arg, struct diskjob_report *r);
/* Every sector read. 0: all of them could be; -1: stopped, or some could not (counted). */
int diskjob_check(diskjob_io io, void *ctx, unsigned long total, unsigned run, diskjob_tick tick, void *arg, struct diskjob_report *r);

#endif
