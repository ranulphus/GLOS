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

#define DOS_FILES     40u               /* the file table's entries ([dos] files' default) */
#define DOS_SDA_SIZE  0x30u             /* the swappable data area's bytes (5D06h) */

/* Where a machine's DOS keeps its tables (linear addresses; segments in paragraphs). A native machine's are
   dos_layout_machine's, in its DOS area (0500h-1280h); the system VM's on a native boot are after the loader's
   memory (kernel/vm/vdos.c). */
struct dos_layout {
    unsigned long lol;                  /* the List of Lists (52h; its words from lol - 12h) */
    unsigned long sda;                  /* the swappable data area in DOS's layout: 00h the critical-error flag,
                                           01h InDOS (34h), 0Ch the DTA, 10h the current PSP, 16h the current drive */
    unsigned long misc;                 /* an empty DBCS table (63h), +2 a RETF (38h's case map), +4 two bytes
                                           the machine may use (ndm.c: its INT 23h's ARPL) */
    unsigned long sft;                  /* the file table: DOS_FILES entries */
    unsigned root_psp;                  /* the root process's PSP (its own parent) */
    unsigned first_mcb, low_top;        /* the memory chain: its first MCB, its end (A000h) */
};
extern const struct dos_layout dos_layout_machine;

struct dos_m {
    unsigned char *mem;                 /* the machine's memory from its address 0 */
    unsigned long size;                 /* its bytes (640 KB: nothing at A0000h and above is the machine's yet) */
    struct dos_mem mcb;
    struct dos_files files;
    struct dos_cds cds;
    struct fsv_vol *vol[DOS_DRIVES];    /* each drive's volume, 0: none */
    int cp;                             /* its code page */
    struct dos_layout lay;              /* (the running program's PSP and its DTA are the SDA's: dos_cur_psp) */
    long time_offset;                   /* its own clock's offset from GLOS's (2Bh, 2Dh) */
    unsigned long (*now)(void *ctx);    /* GLOS's local time: seconds since 1980 */
    void (*out)(void *ctx, const char *s, unsigned n);  /* CON's output */
    void *ctx;
    unsigned char ctrl_break, verify;
    unsigned exit_code;                 /* 4Dh's: the last child's AL, AH its kind (00h normal) */
    unsigned last_error;                /* 59h's */
    unsigned unanswered;
    int ended;                          /* the root's child has ended: the machine with it */
    /* The console's input (NS3): the keys typed (scan code << 8 | character), a ring dos_type fills from the
       machine's terminal (another thread: one writer, one reader) and the machine reads; wait blocks until a key
       may have come (0) or the machine is to end (-1). */
    unsigned short keys[64];
    volatile unsigned khead, ktail;
    int (*wait)(void *ctx);
    void (*idle)(void *ctx);            /* a poll found no key (06h, 0Bh, 44h 06h): its time slice given up */
    unsigned char after_cr;             /* (dos_type's: an LF after a CR is the same Enter) */
    unsigned char pend_scan;            /* 01h/07h/08h: an extended key's scan code, the next call's AL */
    char line[132];                     /* 3Fh on CON, cooked: a line read, CR LF at its end, what's left of it */
    unsigned line_n, line_at;
};

/* Terminal bytes typed into the machine (its terminal's: UTF-8 none yet, VT100's keys' sequences as the PC's
   keys): 1 if a key went in, 0 if the ring was full or nothing made a key. */
int dos_type(struct dos_m *m, const unsigned char *b, unsigned n);

/* The keyboard as the BIOS's INT 16h shows it (ndm.c's): the next key without taking it (-1: none), the next key
   waited for and taken (-1: the machine is to end), a key put in (CX of 05h: 0, or 1 if the ring is full). */
int dos_key_peek(struct dos_m *m);
int dos_key_get(struct dos_m *m);
int dos_key_push(struct dos_m *m, unsigned key);

/* What a call came to: done (the registers are the caller's, answered); another program runs (EXEC, or an end
   into the parent: the registers are its); the machine's first program has ended (exit_code its). */
enum { DOS21_DONE, DOS21_SWITCH, DOS21_END, DOS21_UNANSWERED, DOS21_CTRLC, DOS21_GONE };
/* (DOS21_GONE: a call waiting for a key was told the machine is to end) */

/* DOS21_CTRLC: Ctrl-C was typed (or Ctrl-Break) during the call; r is as the call came in. The machine calls the
   program's INT 23h with it (dos_ctrl_c_end when the vector is GLOS's own): if the handler returns by IRET, or by
   RETF with CF clear, the call is made again; by RETF with CF set, the program ends (4Dh's AH 01h). */
int dos_ctrl_c_end(struct dos_m *m, struct dos_regs86 *r);

/* The machine's DOS made in its memory (size bytes from mem): the vector table is the caller's (ndm.c points
   every vector at its ARPL) and kept from 0 to 3FFh, DOS's data, the file table, the root PSP and its
   environment, the memory chain; drive (2: C:) volume v, the current drive. */
void dos_m_init(struct dos_m *m, unsigned char *mem, unsigned long size, const struct dos_layout *lay,
                struct fsv_vol *v, int drive, int cp);

/* The running program's PSP, as the SDA has it (a writer of the SDA, as an app's switch is, is obeyed). */
unsigned dos_cur_psp(struct dos_m *m);

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
