/* A program's PSP and environment as GLOS's own DOS makes them (docs/v1/native-dos.md, N2, area 1), laid out as
 * RBIL's table 01378 has them and MS-DOS 7.1 fills them. Plain C over the machine's memory, host-tested
 * (tests/host/psp_test.c). What the caller decides (the handles a child inherits, the FCBs parsed from its
 * command line, the vectors to keep) comes in. */
#ifndef K_NDOS_PSP_H
#define K_NDOS_PSP_H

struct dos_psp_init {
    unsigned seg;                       /* the PSP's own segment (its handle table's pointer) */
    unsigned top;                       /* the segment past the program's memory */
    unsigned parent;
    unsigned env;
    const unsigned char *jft;           /* its 20 handles (FFh closed), or 0: all closed */
    unsigned long int22, int23, int24;  /* segment:offset, as the vector table has them */
    unsigned version;                   /* what 30h says to it: AL major, AH minor (0A07h: 7.10) */
    const unsigned char *fcb1, *fcb2;   /* 16 bytes each, or 0: blank FCBs (drive 0, names blank) */
    const char *tail;                   /* its command tail without the length or the 0Dh ("" none) */
};

/* The 256 bytes of a PSP at p. A command tail over 126 bytes is cut there (DOS's limit), its length said as 127
   (Windows 9x's way: the whole line is in the environment's CMDLINE). */
void dos_psp_make(unsigned char *p, const struct dos_psp_init *in);

/* An environment for a child at dst (room bytes): the parent's strings (env: "A=1\0B=2\0\0"), then the word
   0001h and the program's full name. Its size in bytes, or 0 if it doesn't fit or would pass DOS's 32 KB. */
unsigned long dos_env_make(unsigned char *dst, unsigned long room, const unsigned char *env, unsigned long env_max,
                           const char *program);

#endif
