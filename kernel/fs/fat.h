/* FAT volumes read by GLOS itself (docs/v1/native-dos.md, N1a): FAT12, FAT16 and FAT32 with long names (VFAT).
 * Plain C over a sector callback, as kernel/disk is: no kernel calls, so that the host's tests run the same code
 * (tests/host/fat_test.c reads what mkfs.fat, mtools and kernel/disk's fat_format made). Names are UTF-8; short
 * names are bytes in a DOS code page (glos/cp.h), as DOS keeps them on the disk.
 *
 * N1a reads. Writing (N1b), the disk cache and several callers (N1c) come after. */
#ifndef K_FAT_H
#define K_FAT_H

/* `count` sectors from the volume's sector `lba` (counted from its start) into `buf`: 0, or anything else for
   an error. */
typedef int (*fat_read_fn)(void *ctx, unsigned long lba, unsigned char *buf, unsigned count);

enum {
    FAT_OK = 0,
    FAT_ENOENT = -2,                    /* no such file or folder */
    FAT_ENOTDIR = -3,                   /* a part of the path is a file */
    FAT_EISDIR = -4,                    /* a folder where a file was wanted */
    FAT_EIO = -5,                       /* the callback said no */
    FAT_ECORRUPT = -6,                  /* a chain leaves the volume or loops, an entry makes no sense */
    FAT_ENOTFAT = -7,                   /* the first sector is no FAT volume's */
    FAT_EINVAL = -8,                    /* a name or path that can't be */
    FAT_ENOTSUP = -9                    /* a FAT volume GLOS doesn't read (sectors other than 512 bytes) */
};

#define FAT_SECTOR 512
#define FAT_NAME_MAX 768                /* 255 UTF-16 units as UTF-8, and the NUL */
#define FAT_CACHE 4                     /* sectors kept: the FAT's and folders' */

enum {
    FAT_A_RDONLY = 0x01, FAT_A_HIDDEN = 0x02, FAT_A_SYSTEM = 0x04, FAT_A_LABEL = 0x08, FAT_A_DIR = 0x10,
    FAT_A_ARCHIVE = 0x20, FAT_A_LFN = 0x0F
};

struct fat_vol {
    fat_read_fn read;
    void *ctx;
    int fat;                            /* 12, 16 or 32 */
    int cp;                             /* the code page of its short names (437 unless said) */
    unsigned spc;                       /* sectors a cluster */
    unsigned long total;                /* sectors in the volume */
    unsigned long fat_start, fat_sectors;   /* the FAT in use (FAT32's mirroring off: the active one) */
    unsigned nfats;
    unsigned long root_start, root_sectors;     /* FAT12 and FAT16: the root folder's fixed place */
    unsigned long root_cluster;         /* FAT32: the root folder's first cluster */
    unsigned long data_start;           /* cluster 2's first sector */
    unsigned long clusters;             /* clusters 2 .. clusters + 1 */
    unsigned long fsinfo;               /* FAT32: FSInfo's sector, 0 if none */
    unsigned long fsinfo_free;          /* FSInfo's free count, 0xFFFFFFFF if it doesn't say */
    unsigned long serial;
    char label[12];                     /* the boot sector's, the code page's bytes, trailing blanks gone */
    struct {
        unsigned long lba;
        int used;
        unsigned age;
        unsigned char data[FAT_SECTOR];
    } cache[FAT_CACHE];
    unsigned clock;
};

/* An entry of a folder. */
struct fat_ent {
    char name[FAT_NAME_MAX];            /* UTF-8: the long name, else the short one (with NT's case bits) */
    char sname[13];                     /* the short name, "NAME.EXT", in the code page */
    int has_long;
    unsigned char attr;
    unsigned long size;
    unsigned long cluster;              /* the first (0: an empty file; a folder's 0 is the root) */
    unsigned short wtime, wdate, ctime, cdate, adate;
    unsigned char ctime_10ms;
    unsigned long dir;                  /* where its short entry is: its folder's first cluster (0: the root) */
    unsigned long index;                /* ... and its index there */
    unsigned lfn_entries;               /* the long-name entries before it */
};

/* A folder being read. */
struct fat_dir {
    struct fat_vol *v;
    unsigned long first;                /* its first cluster; 0: the root (FAT32's too) */
    unsigned long index;                /* the next entry's */
    unsigned long cl, cl_ord;           /* the cluster that holds entry cl_ord * per-cluster, and that ordinal */
};

/* A file being read. */
struct fat_file {
    struct fat_vol *v;
    unsigned long first, size, pos;
    unsigned long cl, cl_ord;           /* the cluster that holds byte cl_ord * cluster size, and that ordinal */
};

/* The volume whose first sector `read` gives: 0, FAT_ENOTFAT, FAT_ENOTSUP, FAT_EIO. `cp` 0: 437. */
int fat_mount(struct fat_vol *v, fat_read_fn read, void *ctx, int cp);

/* Cluster n's entry in the FAT: the next cluster, 0 if free, or past v->clusters + 1 for the end (or bad). */
int fat_get(struct fat_vol *v, unsigned long n, unsigned long *next);
/* 1 if an entry's value ends a chain (or marks a bad cluster). */
int fat_is_end(const struct fat_vol *v, unsigned long value);

/* A folder by its path ("" or "\" the root; '\' or '/' between parts, no drive letter). */
int fat_opendir(struct fat_vol *v, const char *path, struct fat_dir *d);
/* Its next entry, long name and all (deleted entries and the volume's label skipped; "." and ".." given):
   1, 0 at the end, or an error. */
int fat_readdir(struct fat_dir *d, struct fat_ent *e);
/* A file or folder by its path, either name of each part, case not minded. */
int fat_lookup(struct fat_vol *v, const char *path, struct fat_ent *e);

/* A file by its path (a folder: FAT_EISDIR). */
int fat_open(struct fat_vol *v, const char *path, struct fat_file *f);
/* Up to n bytes from the file's position: how many (0 at its end), or an error. */
long fat_read(struct fat_file *f, void *buf, unsigned long n);
/* The position (past the end: there, reading gives 0). */
int fat_seek(struct fat_file *f, unsigned long pos);

/* Free clusters, counted in the FAT (FSInfo's count is a hint, which Windows doesn't always keep). */
int fat_free(struct fat_vol *v, unsigned long *clusters);

/* Names: UTF-8 compared without regard to case (the simple upper case of Latin, Greek and Cyrillic, which the
   DOS code pages hold), and a long name's checksum of its short entry's 11 bytes. */
int fat_name_eq(const char *a, const char *b);
unsigned char fat_lfn_sum(const unsigned char name11[11]);

#endif
