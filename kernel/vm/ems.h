/* LIM EMS 4.0 of GLOS's own (docs/v1/native-dos.md, N4's EMS, step E1): the expanded memory manager's bookkeeping as
 * INT 67h's functions see it, plain C over callbacks so that tests/host/ems_test.c runs it. The page frame's four
 * 16 KB physical pages are mapped by the caller (map: GLOS's VM page tables, E2); the logical pages are frames the
 * caller allocates (alloc, free) and reaches (page_at, for 57h's moves and exchanges); conventional memory is the
 * machine's own bytes (mem). Handle 0 is the operating system's (no pages, never freed).
 *
 * Answered (AH): 40h-48h, 4Bh-54h (4Fh partial maps, 50h many at once, 51h reallocation, 52h volatile only, 53h
 * and 54h names), 57h (move and exchange, conventional or expanded either end, overlaps as the spec has them),
 * 58h, 59h, 5Ah (raw pages are standard ones here), 5Bh (no alternate register sets), 5Ch. Not: 55h, 56h (map and
 * jump or call), 5Dh (the OS's functions): 84h. */
#ifndef K_VM_EMS_H
#define K_VM_EMS_H

#define EMS_HANDLES 64
#define EMS_PHYS    4                   /* the page frame's pages */
#define EMS_PAGE    16384u
#define EMS_UNMAPPED 0xFFFFFFFFu

struct ems_ops {
    void *ctx;
    /* physical page phys (0-3) to show logical frame id (EMS_UNMAPPED: nothing, the memory there as it was) */
    void (*map)(void *ctx, int phys, unsigned long id);
    /* a new 16 KB frame's id, or EMS_UNMAPPED when none is left; one given back */
    unsigned long (*alloc)(void *ctx);
    void (*free)(void *ctx, unsigned long id);
    /* the byte at off (0 to 16383) of frame id, valid up to the next 4 KB boundary */
    unsigned char *(*page_at)(void *ctx, unsigned long id, unsigned off);
};

struct ems_handle {
    int used;
    unsigned n;                         /* its logical pages, ... */
    unsigned long *page;                /* ... their frames */
    unsigned cap;
    char name[8];                       /* (all zero: none) */
    int saved;                          /* 47h's map, kept */
    unsigned short save_h[EMS_PHYS], save_l[EMS_PHYS];
};

struct ems {
    struct ems_ops ops;
    unsigned frame_seg;                 /* the page frame's segment (E000h, say) */
    unsigned total, free;               /* pages in all, and not allocated (16 KB each) */
    unsigned char *mem;                 /* conventional memory: the machine's first MB */
    unsigned long mem_size;
    struct ems_handle h[EMS_HANDLES];
    unsigned short map_h[EMS_PHYS], map_l[EMS_PHYS];    /* each physical page's handle and logical page (map_h
                                                           0xFFFF: unmapped) */
    void *(*grow)(void *old, unsigned long bytes);      /* a handle's page list grown (realloc's way); 0 fails */
};

struct ems_regs {                       /* INT 67h's registers, in and out */
    unsigned ax, bx, cx, dx, si, di, ds, es;
};

/* total pages (16 KB each) at most; frame_seg the page frame's segment. */
void ems_init(struct ems *e, const struct ems_ops *ops, unsigned total, unsigned frame_seg, unsigned char *mem,
              unsigned long mem_size, void *(*grow)(void *old, unsigned long bytes));

/* INT 67h: AH the function; AH the status on return (00h, or the spec's error codes). */
void ems_call(struct ems *e, struct ems_regs *r);

#endif
