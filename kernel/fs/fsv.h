/* Many callers on a FAT volume (docs/v1/native-dos.md, N1d): kernel/fs/fat.c under a lock per volume, with the
 * open files kept as nodes every handle of a file shares. Plain C over callbacks (the lock's too), as fat.c is,
 * so that tests/host/fsv_test.c runs the same code.
 *
 * What it keeps right:
 *   - a file open twice is one file: its size and first cluster are the node's, so a write through one handle
 *     is what a read through the other finds, and a handle's cached place in the cluster chain is dropped when
 *     another changes the chain (the node's generation);
 *   - an open file's directory entry is where the node says: entries never move when others are made, deleted
 *     or renamed, nor when a folder is renamed or moved (its first cluster stays); an open file itself can't be
 *     made again (FSV_ESHARE, as DOS with SHARE answers), but can be renamed (the node follows it to its new
 *     entry) and deleted (its entry gone, the node's dir FAT_NO_ENTRY, its chain freed at the last close), as
 *     MS-DOS 7.1 renames and deletes one;
 *   - sharing: each open says its access (read, write, both) and what it denies others (DOS's sharing modes);
 *     a new open that conflicts with one already there is refused (FSV_ESHARE). DOS's compatibility mode and
 *     whose process opened what are N2's (the DOS personality decides, with this as its mechanism).
 * A folder being listed holds no lock between entries: what others do meanwhile may or may not be seen, as
 * under DOS. */
#ifndef K_FSV_H
#define K_FSV_H

#include "fat.h"

struct fsv_lock {
    void (*lock)(void *ctx);
    void (*unlock)(void *ctx);
    void *ctx;
};

#define FSV_NODES 64                    /* files open at once on a volume */

enum { FSV_ESHARE = -20, FSV_ENFILE = -21, FSV_EBADF = -22 };

/* Access and sharing, as DOS's open (3Dh) has them in AL: access in bits 0-2, sharing in bits 4-6. */
enum { FSV_READ = 0, FSV_WRITE = 1, FSV_RW = 2 };
enum { FSV_COMPAT = 0, FSV_DENY_ALL = 1, FSV_DENY_WRITE = 2, FSV_DENY_READ = 3, FSV_DENY_NONE = 4 };

struct fsv_node {
    int refs;                           /* handles open on it (0: free) */
    unsigned long dir, index;           /* its short entry */
    unsigned long first, size;
    unsigned gen;                       /* changed when the cluster chain changes under the handles */
    unsigned readers, writers;          /* handles open with read, with write access */
    unsigned deny_read, deny_write;     /* handles that deny others reading, writing */
    int gone;                           /* deleted while open: no entry (dir FAT_NO_ENTRY), its chain freed at the
                                           last close */
};

struct fsv_vol {
    struct fat_vol fat;
    struct fsv_lock lk;
    struct fsv_node node[FSV_NODES];
};

struct fsv_file {
    struct fsv_vol *v;
    struct fsv_node *n;                 /* 0: closed */
    int access, share;
    unsigned long pos;
    unsigned long cl, cl_ord;           /* a place in the chain, good while gen is the node's */
    unsigned gen;
};

int fsv_mount(struct fsv_vol *v, fat_read_fn read, fat_write_fn write, fat_time_fn now, void *ctx, int cp,
              const struct fsv_lock *lk);
int fsv_flush(struct fsv_vol *v);

/* An existing file opened: FAT_ENOENT, FAT_EISDIR, FAT_EACCES (write access to a read-only file), FSV_ESHARE,
   FSV_ENFILE. */
int fsv_open(struct fsv_vol *v, const char *path, int access, int share, struct fsv_file *f);
/* A file made, or (replace) one there made empty, and opened: FAT_EEXIST (there, and !replace), FSV_ESHARE (it
   is open), FAT_EACCES (read-only, or a folder). */
int fsv_create(struct fsv_vol *v, const char *path, unsigned char attr, int replace, int access, int share,
               struct fsv_file *f);
long fsv_read(struct fsv_file *f, void *buf, unsigned long n);
long fsv_write(struct fsv_file *f, const void *buf, unsigned long n);
int fsv_seek(struct fsv_file *f, unsigned long pos);
/* The file cut (or lengthened with zeros) to the handle's position: DOS's write of 0 bytes. */
int fsv_truncate(struct fsv_file *f);
unsigned long fsv_size(struct fsv_file *f);
int fsv_close(struct fsv_file *f);

/* Folders and names, each under the lock; deleting, renaming and attributes of an open file: FSV_ESHARE. */
int fsv_lookup(struct fsv_vol *v, const char *path, struct fat_ent *e);
int fsv_unlink(struct fsv_vol *v, const char *path);
int fsv_rename(struct fsv_vol *v, const char *from, const char *to);
int fsv_mkdir(struct fsv_vol *v, const char *path);
int fsv_rmdir(struct fsv_vol *v, const char *path);
int fsv_setattr(struct fsv_vol *v, const char *path, unsigned char attr);
int fsv_settime(struct fsv_vol *v, const char *path, unsigned short date, unsigned short time);
int fsv_opendir(struct fsv_vol *v, const char *path, struct fat_dir *d);
int fsv_readdir(struct fsv_vol *v, struct fat_dir *d, struct fat_ent *e);
int fsv_free(struct fsv_vol *v, unsigned long *clusters);

#endif
