/* Drive letters as MS-DOS 7.1 and Windows 98 give them (docs/v1/native-dos.md, N1: "Drive letters in MS-DOS's
 * order" and "Drive letters as the DOS beside GLOS sees them"), for a GLOS with no DOS under it: the volumes of
 * each hard disk found from its partition tables (its four primary entries, and the logical drives of its
 * extended partition), then lettered, so that C:\GAMES is the same folder under GLOS as under the DOS or the
 * Windows on the same PC. Plain C over a callback that reads a disk's sectors; host-tested
 * (tests/host/letters_test.c). */
#ifndef K_DISK_LETTERS_H
#define K_DISK_LETTERS_H

enum { LET_PRIMARY, LET_LOGICAL, LET_WHOLE };

struct let_vol {
    unsigned char disk;                 /* the disk's place in the BIOS's order: 0 its first hard disk */
    unsigned char kind;                 /* LET_PRIMARY; LET_LOGICAL (in the extended partition); LET_WHOLE (a disk
                                           with no table whose first sector is a FAT volume's) */
    unsigned char slot;                 /* a primary's entry (0 to 3); a logical drive's place in its chain */
    unsigned char type, active;         /* as the table has them */
    unsigned char own;                  /* the caller's: the volume GLOS started from */
    unsigned char dos;                  /* the caller's: a DOS's or a Windows's own volume (its system files) */
    unsigned long start, sectors;       /* on the disk */
    int letter;                         /* let_assign's: 2 is C:; -1: none */
};

/* A disk's sector `lba` into `sector` (512 bytes): 0, or anything else if it can't be read. */
typedef int (*let_read_fn)(void *ctx, unsigned long lba, unsigned char *sector);

/* The FAT volumes of disk `disk` (`sectors` long; 0: not known) added to vols[n..max-1], in the table's order,
   the logical drives after the primaries: the new count. Partitions DOS gives no letter (hidden ones, other
   systems') are left out; so is what lies outside the disk, and an extended partition's chain is followed no
   further than 32 links. */
int let_scan(unsigned disk, unsigned long sectors, let_read_fn read, void *ctx, struct let_vol *vols, int n, int max);

/* The letters, from `first` on (2: C:), in MS-DOS's order: on each disk in turn its first primary (the active
   one; else the first in the table. Where the active one is GLOS's own and the disk has a DOS's volume too, that
   DOS's, as the DOS would have it when it is the one started); then each disk's logical drives; then each
   disk's other primaries. A volume past Z: gets none, but GLOS's own always has one (the last, if need be). */
void let_assign(struct let_vol *vols, int n, int first);

#endif
