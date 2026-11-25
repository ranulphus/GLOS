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
    unsigned long misc;                 /* DOS_MISC bytes: an empty DBCS table (63h), +2 a RETF (38h's case map),
                                           +4 two bytes the machine may use (ndm.c: its INT 23h's ARPL; the
                                           system VM's to +1Fh: vdos.c). (A machine's ends where its vectors'
                                           ARPLs begin, 0600h: nothing more fits there.) */
    unsigned long sft;                  /* the file table: DOS_FILES entries */
    unsigned root_psp;                  /* the root process's PSP (its own parent) */
    unsigned first_mcb, low_top;        /* the memory chain: its first MCB, its end (A000h) */
    unsigned long tables;               /* DOS_TABLES bytes: TAB_MEDIA a drive's media byte (1Bh, 1Ch), TAB_DPB a
                                           drive's parameter block (1Fh, 32h), TAB_UCASE on the country's tables
                                           (65h: upper case, file names' upper case, file names' ends). 0 in a
                                           layout given: a block of DOS's own memory, the chain's first */
};
extern const struct dos_layout dos_layout_machine;
#define DOS_MISC    0x20
#define DOS_TABLES  0x160
#define TAB_MEDIA   0x00
#define TAB_DPB     0x02                /* (3Dh bytes) */
#define TAB_UCASE   0x40                /* (a word 128, 128 bytes) */
#define TAB_FUCASE  0xC2
#define TAB_FTERM   0x144               /* (24 bytes) */

#define DOS_LFN_FINDS 8                 /* long-name searches open at once (the oldest gives way to a ninth) */

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
    unsigned temp_n;                    /* 5Ah's: names made so far */
    unsigned boot_drive;                /* 3305h's: 1 A:, 3 C: (0: C:) */
    /* Drives whose disk may change (a floppy drive: bit 0 A:): at each use the machine's media() says which
       volume is in it now (vol[] is set from its answer), or 0 and *err (15h: not ready, no disk; 0Fh). */
    unsigned removable;
    struct fsv_vol *(*media)(void *ctx, int drive, unsigned *err);
    /* CD drives (N4: as MSCDEX's, redirected drives, read only; bit 0 A:): at each use cdmedia says which disc's
       ISO 9660 volume is in it now, or 0 and *err (15h: not ready, no disc). Every other call on such a drive
       is refused (05h, access denied, as MSCDEX's answer to a write). */
    unsigned cdrives;
    struct iso_vol *(*cdmedia)(void *ctx, int drive, unsigned *err);
    unsigned unanswered;
    int ended;                          /* the root's child has ended: the machine with it */
    int root_runs;                      /* the root is a program that runs on (the system VM's stub): its children's
                                           ends go back to it, as any parent's */
    /* The console's input (NS3): the keys typed (scan code << 8 | character), a ring dos_type fills from the
       machine's terminal (another thread: one writer, one reader) and the machine reads; wait blocks until a key
       may have come (0) or the machine is to end (-1). */
    unsigned short keys[64];
    volatile unsigned khead, ktail;
    int (*wait)(void *ctx);
    void (*idle)(void *ctx);            /* a poll found no key (06h, 0Bh, 44h 06h): its time slice given up */
    void (*fill)(void *ctx, int input); /* before a call, with the ring empty: the next key typed elsewhere put in
                                           it (the system VM's: from the BIOS's buffer, vdos.c), for an input call;
                                           for any other, only if it is Ctrl-C (as DOS's check takes it) */
    unsigned char after_cr;             /* (dos_type's: an LF after a CR is the same Enter) */
    unsigned char pend_scan;            /* 01h/07h/08h: an extended key's scan code, the next call's AL */
    /* Long names (71xxh): the searches open (714Eh's handles, 1 on: a folder being listed, the pattern, what its
       entries must have), and room for the calls' paths (too long for a kernel thread's stack). */
    struct dos_lfn_find {
        int used;
        unsigned psp, age;
        struct fsv_vol *v;
        struct fat_dir dir;
        char pat[DOS_LONG_PATH * 2];    /* (UTF-8) */
        unsigned char search, must, dos_dates;
    } lfn[DOS_LFN_FINDS];
    unsigned lfn_age;
    char lfn_in[DOS_LONG_PATH + 2], lfn_res[DOS_LONG_PATH + 2], lfn_p[2][DOS_LONG_PATH * 2];
    char line[132];                     /* 3Fh on CON, cooked: a line read, CR LF at its end, what's left of it */
    unsigned line_n, line_at;
    unsigned char cooked[132];          /* ... and the line being read for it */
    unsigned rl_n;                      /* a line being read (0Ah, cooked 3Fh) when its call went back to wait for
                                           a key (the system VM's way: DOS21_GONE): its characters so far, kept */
    int rl_live;
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

/* The system VM's DOS on a native boot (kernel/vm/vdos.c): the program already there (the loader: its PSP at psp,
   its memory to top) adopted as the running process and its own parent, its PSP made again (its five handles,
   no environment); the memory chain from the MCB before it to low_top; the tables in a block of DOS's own (owner
   0008h) at the top of conventional memory, and under them another of extra paragraphs for the caller (*extra_seg:
   vdos.c's stack and buffer for the BIOS's disk calls; 0: none). 0, or -1 if they don't fit. */
int dos_m_init_vm(struct dos_m *m, unsigned char *mem, unsigned long size, unsigned psp, unsigned top,
                  unsigned low_top, struct fsv_vol *v, int drive, int cp, unsigned extra, unsigned *extra_seg);

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
