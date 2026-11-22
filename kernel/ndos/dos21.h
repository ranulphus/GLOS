/* GLOS's own DOS, its INT 21h (docs/v1/native-dos.md, N2; native-DOS sessions, NS2): a machine's DOS state and the
 * calls a program makes, over the pieces beside it (files.c, find.c, path.c, mcb.c, psp.c, exe.c, dtime.c) and a
 * volume a drive (fsv.c). Plain C over the machine's memory (a pointer to its address 0) and its registers, so
 * tests/host/dos21_test.c runs it with programs' calls simulated; kernel/vm/ndm.c calls it from a machine's traps.
 *
 * The machine's low memory: the vector table, a BIOS data area (zeros), DOS's own at 0500h, the ARPLs every vector
 * points at (0060:0000), the system file table (0080:0000), a root PSP of GLOS's own (its own parent: what the first
 * program inherits its five handles from, as COMMAND.COM's under DOS), then the memory chain to A000h. EXEC and a
 * program's end are DOS's own way: the parent's SS:SP in its PSP (2Eh), its return address in the child's (0Ah), so
 * that the registers given back are the program's that runs next. */
#ifndef K_NDOS_DOS21_H
#define K_NDOS_DOS21_H

#include "files.h"
#include "mcb.h"
#include "path.h"

struct dos_regs86 {
    unsigned ax, bx, cx, dx, si, di, bp;
    unsigned ds, es, cs, ip, ss, sp;
    unsigned flags;                     /* CF (bit 0), ZF (bit 6): the answer's; the rest as they came */
};

#define DOS_SFT_LIN   0x0800ul          /* the system file table: 0080:0000 (40 entries: to 113Dh) */
#define DOS_FILES     40u               /* its entries ([dos] files' default) */
#define DOS_LOL_LIN   0x0540ul          /* the List of Lists (52h): 052Eh-05A0h (its words before it too) */
#define DOS_DATA_LIN  0x05F0ul          /* DOS's own bytes, the swappable data area (5D06h): 05F0h the critical-error
                                           flag, 05F1h InDOS (34h), 05F2h an empty DBCS table (63h), 05F4h a RETF
                                           (38h's case map) */
#define DOS_ROOT_PSP  0x0118u           /* the root process's PSP: 1180h-127Fh, in DOS's area */
#define DOS_FIRST_MCB 0x0128u           /* the memory chain's first MCB (the root's environment its first block) */

struct dos_m {
    unsigned char *mem;                 /* the machine's memory from its address 0 */
    unsigned long size;                 /* its bytes (640 KB: nothing at A0000h and above is the machine's yet) */
    struct dos_mem mcb;
    struct dos_files files;
    struct dos_cds cds;
    struct fsv_vol *vol[DOS_DRIVES];    /* each drive's volume, 0: none */
    int cp;                             /* its code page */
    unsigned psp;                       /* the running program's */
    unsigned dta_seg, dta_off;
    long time_offset;                   /* its own clock's offset from GLOS's (2Bh, 2Dh) */
    unsigned long (*now)(void *ctx);    /* GLOS's local time: seconds since 1980 */
    void (*out)(void *ctx, const char *s, unsigned n);  /* CON's output */
    void *ctx;
    unsigned char ctrl_break, verify;
    unsigned exit_code;                 /* 4Dh's: the last child's AL, AH its kind (00h normal) */
    unsigned last_error;                /* 59h's */
    unsigned unanswered;
    int ended;                          /* the root's child has ended: the machine with it */
};

/* What a call came to: done (the registers are the caller's, answered); another program runs (EXEC, or an end
   into the parent: the registers are its); the machine's first program has ended (exit_code its). */
enum { DOS21_DONE, DOS21_SWITCH, DOS21_END, DOS21_UNANSWERED };

/* The machine's DOS made in its memory (size bytes from mem): the vector table is the caller's (ndm.c points
   every vector at its ARPL) and kept from 0 to 3FFh, DOS's data, the file table, the root PSP and its
   environment, the memory chain; drive (2: C:) volume v, the current drive. */
void dos_m_init(struct dos_m *m, unsigned char *mem, unsigned long size, struct fsv_vol *v, int drive, int cp);

/* A program started as the root's child (the kernel's: NDOS RUN): path on the machine's drives, its tail. 0 with
   *r its first registers, or a DOS error. */
int dos_m_start(struct dos_m *m, const char *path, const char *tail, struct dos_regs86 *r);

/* The same from an image in the kernel's memory (len bytes), named name in the current drive's root. */
int dos_m_start_image(struct dos_m *m, const char *name, const unsigned char *image, unsigned long len,
                      const char *tail, struct dos_regs86 *r);

/* The machine gone: every file still open closed, the volumes flushed. */
void dos_m_close(struct dos_m *m);

/* INT 21h with the caller's registers (cs:ip: the instruction after the INT). DOS21_*. */
int dos21(struct dos_m *m, struct dos_regs86 *r);

/* INT 20h: the program ends (code 0). */
int dos20(struct dos_m *m, struct dos_regs86 *r);

#endif
