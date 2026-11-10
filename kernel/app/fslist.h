/* A folder's entries for API 2.0's FS (LIST, INFO; api2fs.c): what DOS says of a file, from either of its find
 * calls, and the records an app is given, packed one after another into its buffer. Names leave here as UTF-8
 * (glos/cp.h's pages). No DOS, no kernel: built for the host too (tests/host/fslist_test.c). */
#ifndef K_FSLIST_H
#define K_FSLIST_H
#include "types.h"

#define FS_DTA   43                     /* FindFirst's data (INT 21h 4Eh, 4Fh) */
#define FS_LFN   318                    /* the long-name calls' (INT 21h 714Eh, 714Fh) */
#define FS_NAME  260                    /* a name's bytes at most, as DOS gives it, with its NUL */

struct fs_found {                       /* an entry as DOS gave it: its names in DOS's code page */
    u32 attr;                           /* DOS's attribute bits */
    u32 size_lo, size_hi;
    u32 mtime;                          /* DOS's date (high word) and time (low word) of its last write */
    char name[FS_NAME];                 /* its name: the long one where there is one */
    char short_name[14];                /* its 8.3 name ("" if the name is it) */
};

void fs_from_dta(const u8 *dta, struct fs_found *e);    /* 4Eh/4Fh's data */
void fs_from_lfn(const u8 *data, struct fs_found *e);   /* 714Eh/714Fh's, asked for DOS's dates (SI 1) */
int fs_dots(const struct fs_found *e);                  /* it is "." or "..": not listed */

/* A record as the app gets it (struct glos_fs_entry): its size first, a multiple of 4. */
struct fs_record {
    u32 size;                           /* the whole record's */
    u32 attr;
    u32 bytes_lo, bytes_hi;
    u32 mtime;
    char short_name[16];                /* (8.3 and a NUL, padded) */
    char name[4];                       /* UTF-8, a NUL after it; the record is as long as it needs */
};

/* The record for e at buf + *used, its names through the code page: 1, and *used past it; 0 if it doesn't fit
   in room (nothing is written). */
int fs_pack(u8 *buf, u32 room, u32 *used, const struct fs_found *e, int page);
u32 fs_record_size(const struct fs_found *e, int page); /* what it would take */

#endif
