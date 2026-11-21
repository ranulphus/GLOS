/* Programs loaded by GLOS's own DOS (docs/v1/native-dos.md, N2, area 1: EXEC): what a .COM or .EXE file asks for,
 * where it goes, its relocations, the registers it starts with. Plain C over buffers, host-tested
 * (tests/host/exe_test.c). The rules are RBIL's (INT 21h 4Bh, table 01594) and MS-DOS's as programs see them:
 * a file is an .EXE by its first two bytes ("MZ" or "ZM"), whatever its name says. */
#ifndef K_NDOS_EXE_H
#define K_NDOS_EXE_H

struct dos_exe {
    int exe;                            /* 0: a .COM (the whole file is the image) */
    unsigned long image_off;            /* the image's place in the file (the header's size) */
    unsigned long image_len;            /* its bytes as the header gives them (the file may hold fewer) */
    unsigned long image_paras;
    unsigned min_alloc, max_alloc;      /* paragraphs beyond the image */
    unsigned ss, sp, cs, ip;            /* relative to the image */
    unsigned nrel;
    unsigned long rel_off;              /* the relocation table's place in the file */
    int high;                           /* min and max both 0: loaded at the top of its block */
};

/* The file's first bytes (28 at least, or all of a shorter file) and its size: 0, or -1 if the header makes no
   sense (an image ending before it starts, a relocation table past the file). */
int dos_exe_header(const unsigned char *head, unsigned long head_len, unsigned long file_size, struct dos_exe *x);

/* Paragraphs the program's block must have, PSP included: at least *need, and *want if there is room (a .COM
   wants all there is: 0xFFFF). */
void dos_exe_memory(const struct dos_exe *x, unsigned long file_size, unsigned long *need, unsigned long *want);

/* Its image's segment in a block of `paras` paragraphs at `psp` (the PSP first, 10h paragraphs). */
unsigned dos_exe_image_seg(const struct dos_exe *x, unsigned psp, unsigned long paras);

/* n relocations (4 bytes each, offset then segment, from the file) applied to memory: the word at
   (segment * 16 + offset) from mem's start gets factor added. mem_len: the bytes there (from the image's start
   to its block's end); a relocation past them is left alone. How many were applied. */
unsigned dos_exe_relocate(unsigned char *mem, unsigned long mem_len, const unsigned char *rel, unsigned n,
                          unsigned factor);

/* The registers a program starts with (4B00h): fcb1_ok and fcb2_ok whether the command tail's first and second
   names had a valid drive (AL, AH: 00h or FFh). For a .COM: CS, DS, ES, SS the PSP; SP the last word of its
   first 64 KB (or of its block, when smaller), a 0000h word pushed there first (*push0). */
struct dos_regs0 {
    unsigned ax, cs, ip, ss, sp, ds, es;
    int push0;
};
void dos_exe_regs(const struct dos_exe *x, unsigned psp, unsigned long paras, int fcb1_ok, int fcb2_ok,
                  struct dos_regs0 *r);

#endif
