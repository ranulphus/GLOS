/* Files by handle in GLOS's own DOS (docs/v1/native-dos.md, N2, area 2): the system file table in the machine's
 * memory as programs read it (RBIL table 01642, DOS 4-7: 3Bh bytes an entry), each program's handle table
 * (its PSP's: RBIL table 01378, 32h and 34h), open, create, close, duplicate, inherit, a program's handles closed
 * at its exit, DOS 7's sharing rules between the entries open on one file (share.c). Each entry's file is
 * kernel/fs/fsv.c's; its fields in the table are brought up to date at each call, so a program reading them sees
 * what DOS would show. Devices are entries with no file: what they read and write is the console's (N2's CON).
 * Plain C over the machine's memory, host-tested (tests/host/files_test.c). */
#ifndef K_NDOS_FILES_H
#define K_NDOS_FILES_H

#include "../fs/fsv.h"

#define DOS_SFT_MAX 255                 /* entries at most (a handle table holds a byte: FFh is "closed") */
#define DOS_SFT_SIZE 0x3B

enum {                                  /* DOS's error codes these give */
    DOS_ENOENT = 0x02, DOS_ENOPATH = 0x03, DOS_ENFILE = 0x04, DOS_EACCES = 0x05, DOS_EBADF = 0x06,
    DOS_ESHARE = 0x20, DOS_EEXIST = 0x50, DOS_EINVALID = 0x0C
};

/* A device the table may hold (its entry is the device's; reading and writing it is the caller's). */
enum { DOS_DEV_NONE = 0, DOS_DEV_CON, DOS_DEV_AUX, DOS_DEV_PRN, DOS_DEV_NUL };

struct dos_sft_k {                      /* the kernel's side of an entry */
    int dev;                            /* DOS_DEV_*, or 0: a file */
    struct fsv_file f;
};

/* A file open through an FCB (0Fh, 16h: no handle). The program's FCB keeps a place here and a ticket (in its
   reserved bytes); this keeps where the file is. So an FCB a program never closes (CP/M's way, and DOS 1's:
   only files written were closed) holds an entry of the system file table only while it is among the last
   DOS_FCB_LIVE used; one that lost its entry is opened again at its next call, and only the least recently
   used of DOS_FCBS is forgotten (MS-DOS: FCBS=, and "FCB unavailable"). */
#define DOS_FCBS 24
#define DOS_FCB_LIVE 8

#define DOS_LOCKS 32                    /* regions locked at once in a machine */

struct dos_fcb_k {
    unsigned ticket;                    /* 0: free */
    unsigned psp, stamp;                /* who opened it; when it was last used */
    int entry;                          /* its entry of the table, or -1 (given to another) */
    int dev;                            /* DOS_DEV_*, or 0: a file */
    struct fsv_vol *v;
    unsigned char drive, ro;            /* (ro: a read-only file, open for reading alone) */
    char path[128];
};

struct dos_files {
    unsigned char *m;                   /* the machine's memory from address 0 */
    unsigned long table;                /* the table's linear address (its header: next, count; then entries) */
    unsigned n;                         /* its entries */
    struct dos_sft_k k[DOS_SFT_MAX];
    struct dos_fcb_k fcb[DOS_FCBS];
    unsigned fcb_stamp, fcb_ticket;
    struct dos_lock {                   /* 5Ch's: a region of an entry's file, its owner's alone to read and write */
        unsigned char used, entry;
        unsigned psp;
        unsigned long off, len;
    } lock[DOS_LOCKS];
};

/* The table at linear address table, n entries, all free but the first three: AUX, CON and PRN as DOS has them
   (entries 0, 1, 2), each held by the handles a first process gets (dos_files_first_jft). */
void dos_files_init(struct dos_files *t, unsigned char *m, unsigned long table, unsigned n);
/* A first process's handles: 0-2 CON, 3 AUX, 4 PRN, the rest closed; the entries' counts raised. */
void dos_files_first_jft(struct dos_files *t, unsigned psp);

/* 3Dh: path on volume v, drive (0 A:, 2 C:), or a device (dev not 0), opened with mode (3Dh's AL) for psp: 0
   and its handle in *h, or a DOS error. DOS_ESHARE: the sharing table says INT 24h (the caller makes it). */
int dos_open(struct dos_files *t, unsigned psp, struct fsv_vol *v, unsigned drive, const char *path, int dev,
             unsigned mode, unsigned *h);
/* 3Ch (replace) and 5Bh (not replace): made with attr, opened read-write, compatibility mode. */
int dos_create(struct dos_files *t, unsigned psp, struct fsv_vol *v, unsigned drive, const char *path,
               unsigned char attr, int replace, unsigned *h);
/* 3Eh. */
int dos_close(struct dos_files *t, unsigned psp, unsigned h);
/* 45h, 46h. */
int dos_dup(struct dos_files *t, unsigned psp, unsigned h, unsigned *h2);
int dos_dup2(struct dos_files *t, unsigned psp, unsigned h, unsigned h2);
/* 3Fh, 40h (n bytes, 0 for 40h: the file cut at the position), 42h: bytes done, or a DOS error negated. A
   device's reading and writing is the caller's (dos_entry, then the entry's dev); NUL's are done here (nothing
   read, all written). */
long dos_read(struct dos_files *t, unsigned psp, unsigned h, void *buf, unsigned long n);
long dos_write(struct dos_files *t, unsigned psp, unsigned h, const void *buf, unsigned long n);
int dos_seek(struct dos_files *t, unsigned psp, unsigned h, int whence, long off, unsigned long *pos);
/* 5Ch: len bytes at off of handle h's file locked (unlock 0), or that lock given up (unlock 1: the same handle's,
   the same region). 0; 06h; 21h (it overlaps a region locked, whoever locked it; or no such lock to give up); 24h
   (no room for another: DOS's "sharing buffer overflow"); 01h (a device). While it holds, a read or a write
   there through another entry, or by another program, is refused (21h). An entry's locks go when its last
   handle is closed, a program's at its end. Between machines there are none yet (N6). */
int dos_lock(struct dos_files *t, unsigned psp, unsigned h, int unlock, unsigned long off, unsigned long len);
/* The entry a handle holds, or -1. */
int dos_entry(struct dos_files *t, unsigned psp, unsigned h);

/* EXEC: the child's 20 handles from the parent's (those opened private, mode bit 7, closed), the counts raised. */
void dos_inherit(struct dos_files *t, unsigned parent_psp, unsigned char child_jft[20]);
/* A program's exit: every handle it has closed, and every FCB it opened forgotten. */
void dos_close_all(struct dos_files *t, unsigned psp);

/* 0Fh (create 0) and 16h (create 1: made, or cut to nothing, with attr): path on v, drive (0 A:), or a device,
   opened read-write in compatibility mode for psp (a read-only file for reading alone, as DOS opens it for an
   FCB): 0, with its place and ticket for the FCB; or a DOS error. The entry's mode has bit 15, as DOS marks an
   FCB's. */
int dos_fcb_open(struct dos_files *t, unsigned psp, struct fsv_vol *v, unsigned drive, const char *path, int dev,
                 int create, unsigned char attr, unsigned *slot, unsigned *ticket);
/* An FCB's file by its place and ticket, opened again if its entry went to another: the entry (k[]'s index: its
   dev says a device's), or -1 (not an open FCB's, or its file is no longer there). */
int dos_fcb_entry(struct dos_files *t, unsigned slot, unsigned ticket);
/* Bytes read or written at pos of an entry dos_fcb_entry gave: the count done, or a DOS error negated. A write
   of 0 bytes cuts the file at pos, or lengthens it to there. A device's are the caller's (NUL's are done here). */
long dos_fcb_read(struct dos_files *t, int entry, unsigned long pos, void *buf, unsigned long n);
long dos_fcb_write(struct dos_files *t, int entry, unsigned long pos, const void *buf, unsigned long n);
unsigned long dos_fcb_size(struct dos_files *t, int entry);
/* 10h: 0, or DOS_EBADF (not an open FCB's). */
int dos_fcb_close(struct dos_files *t, unsigned slot, unsigned ticket);

#endif
