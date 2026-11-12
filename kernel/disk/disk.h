/* Disks as GLOS itself lays them out (docs/v1/shell.md, step S0): the partition table in a disk's first sector,
 * and FAT volumes made new. Plain C over buffers and a write callback: the kernel's shell commands use it
 * (DISK), and the host's tests check what it makes with mtools and fsck.fat (tests/host/disk_test.c). */
#ifndef K_DISK_H
#define K_DISK_H

/* ---- the partition table (the MBR) */
struct mbr_part {
    unsigned char active, type;         /* active: 1 if the PC starts from it; type 0: no partition */
    unsigned long start, sectors;
};

/* A first sector's four entries: how many are used, or -1 if the sector has no table (no 55AAh). */
int mbr_parse(const unsigned char *sector, struct mbr_part part[4]);
/* 1 for the types DOS reads (FAT12, FAT16, FAT32, with or without LBA). */
int mbr_is_fat(unsigned type);
/* One partition over a whole disk of `total` sectors with the BIOS's `heads` and `spt`: the table written into
   `sector` (the rest of it kept), the partition active, starting at the second track as DOS has it. Its type by
   its size: FAT12, FAT16 or FAT32, the LBA kinds where it ends past cylinder 1023. Its start and size are
   given back. -1: no room for a partition. */
int mbr_one(unsigned char *sector, unsigned long total, unsigned heads, unsigned spt, struct mbr_part *made);
/* GLOS's boot code into a first sector (its table kept): it starts the active partition, by the BIOS's LBA
   calls where it has them, else by cylinder, head and sector. */
void mbr_boot(unsigned char *sector);

/* ---- a FAT volume made new */
#define FAT16_MOST 4194000UL            /* sectors: just under 2 GB, the most FAT16 holds with DOS's 32 KB clusters */
struct fat_geo {
    unsigned long sectors;              /* the volume's size */
    unsigned long hidden;               /* sectors before it on the disk (a partition's start; a floppy: 0) */
    unsigned heads, spt;                /* the BIOS's geometry */
    unsigned drive;                     /* the BIOS's number: 00h a floppy, 80h a hard disk */
    unsigned long serial;
    const char *label;                  /* 11 characters at most, or 0 */
    int fat;                            /* 12, 16 or 32; 0: by the size */
};
struct fat_made {
    int fat;                            /* 12, 16 or 32 */
    unsigned cluster_sectors;
    unsigned long clusters, fat_sectors, data_start;
};
/* The volume's sectors `lba` on (from its start), `count` of them: 0, or anything else to stop. */
typedef int (*fat_write)(void *ctx, unsigned long lba, const unsigned char *data, unsigned count);

/* Writes a new, empty volume: its boot sector (with a short program that says the disk has no system), its
   FATs, its root folder. Floppies of 360 KB, 720 KB, 1.2 MB, 1.44 MB and 2.88 MB are as DOS formats them.
   0; -1: a size no FAT of that kind fits; -2: the write stopped. */
int fat_format(const struct fat_geo *g, fat_write write, void *ctx, struct fat_made *made);

/* GLOS's boot code that starts FreeDOS's kernel (KERNEL.SYS in the root folder; kernel/disk/fdboot.S) into a
   FAT12 or FAT16 volume's first sector, its parameters kept: what FreeDOS's SYS does to a volume. 0; -1: no
   such code for that kind of FAT (FAT32: not yet). */
int fat_boot(unsigned char *sector, int fat);

#endif
