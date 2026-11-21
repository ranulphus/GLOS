/* A machine's memory as GLOS's own DOS keeps it (docs/v1/native-dos.md, N2): the chain of memory control blocks,
 * 48h, 49h, 4Ah, 58h. Plain C over the machine's memory (a byte array from address 0), host-tested
 * (tests/host/mcb_test.c). The rules are RBIL's and MS-DOS's as programs see them:
 *   - a block is an MCB (RBIL table 01628: 'M', or 'Z' for the last; owner; size in paragraphs) and the memory
 *     after it; 0000h owns a free block, 0008h DOS;
 *   - free blocks next to each other are joined when memory is allocated or resized, not when it is freed;
 *   - an allocation that fails says the largest free block; a resize that fails makes the block as large as it
 *     can be and says that size (DOS 2.1-6.0's way; to be checked against MS-DOS 7.1, N7);
 *   - strategies 00h-02h (first, best, last fit), 40h-42h (upper memory only), 80h-82h (upper first);
 *   - upper memory is in the chain while linked (5803h): the last block below A000h is then 'M' and goes on into
 *     a DOS block ("SC") over the gap up to the first UMB. */
#ifndef K_NDOS_MCB_H
#define K_NDOS_MCB_H

struct dos_mem {
    unsigned char *m;                   /* the machine's memory from address 0 */
    unsigned long size;                 /* its bytes */
    unsigned first;                     /* the first MCB's segment */
    unsigned link;                      /* the "SC" MCB over A000h onwards (0: no upper memory) */
    int linked;
    unsigned strategy;
};

/* DOS's error codes these give. */
enum { DOS_EMCB = 0x07, DOS_ENOMEM = 0x08, DOS_EBLOCK = 0x09, DOS_EFUNC = 0x01 };

/* One free block from segment first to low_end (A000h), then, if any, the upper memory blocks [umb[2i],
   umb[2i+1]) (segments, rising), the gaps between them DOS's: unlinked, as DOS starts. */
void dos_mem_init(struct dos_mem *d, unsigned char *m, unsigned long size, unsigned first, unsigned low_end,
                  const unsigned *umb, int n_umb);

/* 48h: paras for owner. 0 with *seg (the memory after the MCB); DOS_ENOMEM with *largest; DOS_EMCB. */
int dos_mem_alloc(struct dos_mem *d, unsigned paras, unsigned owner, unsigned *seg, unsigned *largest);
/* 49h: the block whose memory starts at seg. 0, DOS_EBLOCK. */
int dos_mem_free(struct dos_mem *d, unsigned seg);
/* 4Ah: 0; DOS_ENOMEM with *largest (and the block that large); DOS_EBLOCK; DOS_EMCB. */
int dos_mem_resize(struct dos_mem *d, unsigned seg, unsigned paras, unsigned *largest);
/* Every block owner has, freed (its exit). */
void dos_mem_free_owner(struct dos_mem *d, unsigned owner);
/* 5801h and 5803h: 0 or DOS_EFUNC. */
int dos_mem_strategy(struct dos_mem *d, unsigned strategy);
int dos_mem_link(struct dos_mem *d, int on);
/* The chain checked from its start to its end: 0, or DOS_EMCB. */
int dos_mem_check(const struct dos_mem *d);

#endif
