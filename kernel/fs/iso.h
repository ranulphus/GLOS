/* ISO 9660, read-only (docs/v1/native-dos.md, N4's CD-ROM, step C3): a CD's files as DOS sees them under MSCDEX,
 * over a reader of 2048-byte sectors (a drive, kernel/drv/cdrom.c; or an image file, step C7). Plain C over
 * callbacks, as fat.c is, so that tests/host/iso_test.c runs it against images made by tools/ndos/mkiso.py.
 *
 * Names are DOS's: a file's "NAME.EXT;1" shows as NAME.EXT (no version, no dot for "NAME.;1"), a folder's as it
 * is, in upper case; a name that isn't 8.3 (an ISO level 2 or 3 one) isn't shown, as under MSCDEX, and is
 * counted (iso_vol.skipped). Folders are walked from the root (no path table needed). A file of several extents
 * (ISO's multi-extent flag) is read as its first. Dates are the recording's, as DOS's date and time words. */
#ifndef K_ISO_H
#define K_ISO_H

#define ISO_SECTOR 2048

struct iso_dev {
    int (*read)(void *ctx, unsigned long lba, unsigned count, unsigned char *buf);     /* 0, or not 0: an error */
    void *ctx;
};

enum { ISO_OK = 0, ISO_EIO = -1, ISO_ENOTISO = -2, ISO_ENOENT = -3, ISO_ENOPATH = -4, ISO_ENOTDIR = -5 };

struct iso_entry {
    char name[13];                      /* DOS's 8.3, upper case; the root: "" */
    unsigned long lba, size;            /* its extent: where it begins, its bytes */
    int dir;
    unsigned short date, time;          /* DOS's: ((year - 1980) << 9 | month << 5 | day), (h << 11 | m << 5 | s / 2) */
    unsigned char attr;                 /* DOS's: 01h read-only always, 10h a folder, 02h hidden (ISO's "existence") */
};

struct iso_vol {
    struct iso_dev dev;
    unsigned long sectors;              /* the volume's, as its descriptor says */
    char label[33];                     /* the volume's identifier, its spaces off the end */
    struct iso_entry root;
    unsigned long skipped;              /* names seen that aren't 8.3 (not shown) */
    unsigned long cached;               /* the sector in buf (~0: none) */
    unsigned char buf[ISO_SECTOR];
};

/* The volume's primary descriptor found (sectors 16 on, to the set's terminator): ISO_OK, ISO_ENOTISO, ISO_EIO. */
int iso_mount(struct iso_vol *v, const struct iso_dev *dev);

/* A folder's next entry from *pos (0: its first; "." and ".." left out): ISO_OK with *pos moved on, 1 at its
   end, or ISO_EIO. */
int iso_next(struct iso_vol *v, const struct iso_entry *dir, unsigned long *pos, struct iso_entry *e);

/* A path's entry: "\", "\GAMES", "\GAMES\README.TXT" (backslashes or slashes; any case; "." and ".." taken).
   ISO_OK; ISO_ENOENT (its last part isn't there), ISO_ENOPATH (a folder on the way isn't), ISO_EIO. */
int iso_lookup(struct iso_vol *v, const char *path, struct iso_entry *e);

/* Up to n bytes of a file from off: the bytes read (0 at or past its end), or ISO_EIO. */
long iso_read(struct iso_vol *v, const struct iso_entry *f, unsigned long off, void *buf, unsigned long n);

#endif
