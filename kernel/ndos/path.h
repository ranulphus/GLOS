/* Paths in GLOS's own DOS (docs/v1/native-dos.md, N2, area 3): a program's path made a drive and a whole path,
 * as DOS's short-name calls take them and 60h (TRUENAME) shows them; each drive's current folder (3Bh, 47h).
 * Plain C, host-tested (tests/host/path_test.c). Marked "to check": what MS-DOS 7.1 is to be compared with (N7)
 * where RBIL doesn't say. */
#ifndef K_NDOS_PATH_H
#define K_NDOS_PATH_H

#define DOS_DRIVES 26
#define DOS_CDS_PATH 67                 /* the CDS's path field: "X:\" and the folder, NUL-ended */

struct dos_cds {
    unsigned valid;                     /* bit n: drive n has a volume (A: 0) */
    int current;                        /* the current drive */
    char cwd[DOS_DRIVES][DOS_CDS_PATH - 2];     /* each drive's current folder, "\" or "\DOS\UTIL" */
};

/* path (bytes of code page cp) made a drive (*drive) and a whole path ("\DIR\FILE.EXT", out: room bytes, upper
   case, '.' and '..' gone, each part cut to 8.3 as MS-DOS's short-name calls cut them: to check). 0, 0Fh (no
   such drive), 03h (a path too long for DOS, or nothing after the drive where something must be), 02h (a part
   that can't be a name). A '\' or '/' at the end is dropped. */
int dos_resolve(const struct dos_cds *c, int cp, const char *path, int *drive, char *out, unsigned room);

/* 60h's form: "C:\DIR\FILE.EXT". */
int dos_truename(const struct dos_cds *c, int cp, const char *path, char *out, unsigned room);

/* 47h's form of drive's current folder (0 current, 1 A:): "DIR\SUB", no drive, no leading '\'. 0 or 0Fh. */
int dos_getcwd(const struct dos_cds *c, int drive, char *out, unsigned room);

#endif
