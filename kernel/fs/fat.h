/* FAT volumes read by GLOS itself (docs/v1/native-dos.md, N1a): FAT12, FAT16 and FAT32 with long names (VFAT).
 * Plain C over a sector callback, as kernel/disk is: no kernel calls, so that the host's tests run the same code
 * (tests/host/fat_test.c reads what mkfs.fat, mtools and kernel/disk's fat_format made). Names are UTF-8; short
 * names are bytes in a DOS code page (glos/cp.h), as DOS keeps them on the disk.
 *
 * N1a reads; N1b writes (fat_mount_rw), write-through in the order that keeps a volume repairable at any moment:
 * a cluster's FAT entries before the directory entry that points at it, a file's data before the size that
 * covers it. The disk cache and several callers (N1c, N1d) come after. */
#ifndef K_FAT_H
#define K_FAT_H

/* `count` sectors from the volume's sector `lba` (counted from its start) into `buf`: 0, or anything else for
   an error. */
typedef int (*fat_read_fn)(void *ctx, unsigned long lba, unsigned char *buf, unsigned count);
/* ... and written: 0, or anything else for an error. */
typedef int (*fat_write_fn)(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count);
/* The time now, local, as DOS keeps it: the date in the high 16 bits, the time in the low. */
typedef unsigned long (*fat_time_fn)(void *ctx);

enum {
    FAT_OK = 0,
    FAT_ENOENT = -2,                    /* no such file or folder */
    FAT_ENOTDIR = -3,                   /* a part of the path is a file */
    FAT_EISDIR = -4,                    /* a folder where a file was wanted */
    FAT_EIO = -5,                       /* the callback said no */
    FAT_ECORRUPT = -6,                  /* a chain leaves the volume or loops, an entry makes no sense */
    FAT_ENOTFAT = -7,                   /* the first sector is no FAT volume's */
    FAT_EINVAL = -8,                    /* a name or path that can't be */
    FAT_ENOTSUP = -9,                   /* a FAT volume GLOS doesn't read (sectors other than 512 bytes) */
    FAT_EEXIST = -10,                   /* the name is there already */
    FAT_ENOSPC = -11,                   /* no free cluster, or no room in a FAT12/16 root folder */
    FAT_ENOTEMPTY = -12,                /* a folder to remove still has entries */
    FAT_EROFS = -13,                    /* mounted for reading only */
    FAT_EACCES = -14                    /* read-only, a volume label, or the root */
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
    fat_write_fn write;                 /* 0: mounted for reading only */
    fat_time_fn now;
    void *ctx;
    int fat;                            /* 12, 16 or 32 */
    int cp;                             /* the code page of its short names (437 unless said) */
    unsigned spc;                       /* sectors a cluster */
    unsigned long total;                /* sectors in the volume */
    unsigned long fat_start, fat_sectors;   /* the FAT in use (FAT32's mirroring off: the active one) */
    unsigned long fat0;                 /* the first FAT's sector (the reserved sectors' end) */
    int mirror;                         /* writes go to every FAT copy (FAT32 with mirroring off: the active one) */
    unsigned nfats;
    unsigned long root_start, root_sectors;     /* FAT12 and FAT16: the root folder's fixed place */
    unsigned long root_cluster;         /* FAT32: the root folder's first cluster */
    unsigned long data_start;           /* cluster 2's first sector */
    unsigned long clusters;             /* clusters 2 .. clusters + 1 */
    unsigned long fsinfo;               /* FAT32: FSInfo's sector, 0 if none */
    unsigned long fsinfo_free;          /* FSInfo's free count, 0xFFFFFFFF if it doesn't say */
    unsigned long serial;
    unsigned long free_count;           /* free clusters as far as known (0xFFFFFFFF: unknown) */
    unsigned long next_free;            /* where the next search for one starts */
    int dirty;                          /* the clean bit cleared on the disk, until fat_flush */
    int was_dirty;                      /* it was already when mounted: fat_flush leaves it so (ScanDisk's) */
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

/* A file being read or written. */
struct fat_file {
    struct fat_vol *v;
    unsigned long first, size, pos;
    unsigned long cl, cl_ord;           /* the cluster that holds byte cl_ord * cluster size, and that ordinal */
    unsigned long dir, index;           /* its short entry (struct fat_ent's), for its size and first cluster */
    int changed;                        /* written since its entry was last brought up to date */
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

/* ---- writing (N1b): a volume mounted with a write callback; every call writes what it changed before it
   returns (write-through), FSInfo's count and hint and the clean bit at fat_flush */

/* The volume, for reading and writing. `now` 0: times are left 0. */
int fat_mount_rw(struct fat_vol *v, fat_read_fn read, fat_write_fn write, fat_time_fn now, void *ctx, int cp);
/* FSInfo's free count and hint written, and the volume marked clean (unless it came dirty). */
int fat_flush(struct fat_vol *v);

/* A new, empty file (FAT_EEXIST if the name is there), opened for writing at 0. attr: FAT_A_RDONLY, HIDDEN,
   SYSTEM (ARCHIVE is added). */
int fat_create(struct fat_vol *v, const char *path, unsigned char attr, struct fat_file *f);
/* n bytes at the file's position (past its end: the gap is zeros): how many, or an error. */
long fat_write(struct fat_file *f, const void *buf, unsigned long n);
/* The file made `size` bytes long: clusters past it freed, or zeros added. */
int fat_truncate(struct fat_file *f, unsigned long size);
/* Its directory entry brought up to date (size, first cluster, written time): fat_write does it as it goes. */
int fat_close(struct fat_file *f);
int fat_unlink(struct fat_vol *v, const char *path);
int fat_mkdir(struct fat_vol *v, const char *path);
int fat_rmdir(struct fat_vol *v, const char *path);
/* A file or folder given another name, in the same folder or another (a folder never into itself). */
int fat_rename(struct fat_vol *v, const char *from, const char *to);
int fat_setattr(struct fat_vol *v, const char *path, unsigned char attr);
int fat_settime(struct fat_vol *v, const char *path, unsigned short date, unsigned short time);
/* The short name Windows 9x would give `name` in folder `dir` (its first cluster; 0 the root): 11 bytes, the
   code page's. 0 and *lfn whether the name needs long-name entries; an error. */
int fat_short_name(struct fat_vol *v, unsigned long dir, const char *name, unsigned char out[11], int *lfn);

/* Free clusters, counted in the FAT (FSInfo's count is a hint, which Windows doesn't always keep). */
int fat_free(struct fat_vol *v, unsigned long *clusters);

/* Names: UTF-8 compared without regard to case (the simple upper case of Latin, Greek and Cyrillic, which the
   DOS code pages hold), and a long name's checksum of its short entry's 11 bytes. */
int fat_name_eq(const char *a, const char *b);
unsigned char fat_lfn_sum(const unsigned char name11[11]);

#endif
