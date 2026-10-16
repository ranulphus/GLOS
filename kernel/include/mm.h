/* Physical frames, the kernel's address space and its heap (supervisor.md §4, §5). */
#ifndef K_MM_H
#define K_MM_H
#include "types.h"

struct bootinfo;

#define KMAP_BASE   0xDFC00000u         /* 1024 one-page windows onto any frame */
#define HEAP_BASE   0xC0400000u         /* kmalloc's region, grown a page at a time */
#define HEAP_MAX    0x00400000u
#define DMA_BASE    0xC0800000u         /* dma_alloc's region: 4 MB at most */
#define FB_BASE     0xC0C00000u         /* mm_fb_map's region: the screen's framebuffer */
#define FB_MAX      0x00800000u         /* 8 MB: 1280x1024 at 32 bits is 5 */
#define WIN_BASE    0xC2000000u         /* mm_win_alloc's region: windows' pixels */
#define WIN_MAX     0x02000000u         /* 32 MB of addresses (32 KB of page tables, there from the start) */
#define DEV_BASE    0xC4000000u         /* mm_dev_map's region: devices' registers and memory (the Matrox card's) */
#define DEV_MAX     0x02800000u         /* 40 MB: a G450's 32 MB and its registers */
#define RECURSIVE   0xFFC00000u         /* PDE 1023 maps the page directory itself */

void mm_init(struct bootinfo *bi);
u32 mm_cr3(void);                       /* the kernel's page directory (physical) */
u32 kernel_phys(const void *p);         /* physical address of a kernel image/bss address */

u32 pmm_alloc(void);                    /* a free frame, 0 when none */
void pmm_free(u32 phys);
u32 pmm_free_frames(void);
u32 pmm_total_frames(void);             /* frames the loader gave GLOS (in XMS mode, its pool) */
u32 kheap_bytes(void);                  /* the heap's size now */
u32 kernel_bytes(void);                 /* the image and its bss */

u32 pmm_alloc_run(u32 n);                /* the kernel's frames: n contiguous, 0 if none */
int pmm_claim(u32 phys, u32 n);
void pmm_free_run(u32 phys, u32 n);
u32 pmm_largest(void);

void mm_vm_init(int a20);               /* 0-10FFFFh user-accessible for V86 mode */
void mm_set_a20(int on);                /* 100000h-10FFFFh: the HMA, or wrapped onto 0-FFFFh */

/* DPMI address spaces (the current one is changed through its recursive slot). */
#define MM_W 2u                         /* writable */
#define MM_U 4u                         /* user (ring 3) */
#define MM_UC 0x18u                     /* uncached (PCD, PWT): device memory */
#define MM_MAPPED 0x200u                /* (an AVL bit) not a frame of ours: 0508h/0509h mappings */
#define MM_LAZY 0x800u                  /* (an AVL bit, in a not-present PTE) memory to back when first touched */
u32 mm_space_new(void);                 /* a page directory with the kernel's PDEs; 0 without memory */
void mm_space_free(u32 pd_phys);        /* not the current one: its page tables and itself */
void mm_space_enter(u32 pd_phys);       /* CR3; 0: the kernel's own */
int mm_map(u32 lin, u32 phys, u32 flags);       /* one page in the current space; -1 without memory */
u32 mm_unmap(u32 lin);                  /* the frame that was there, or 0 */
u32 mm_lookup(u32 lin);                 /* the PTE, 0 if none */
int mm_space_set(u32 pd_phys, u32 lin, u32 pte);       /* a page's entry in any space; -1 without a page table */
int mm_lazy(u32 lin);                   /* not present, MM_LAZY | W | U: backed on the first touch; -1 without memory */
void *mm_tramp_page(u32 lin);           /* a ring-3 read-only page in PDE 0's table, for the host to fill */

void *kmap(u32 phys);
void kunmap(void *p);

void *dma_alloc(u32 frames, u32 *phys); /* contiguous, zeroed, mapped for good: a device's own memory; 0 if none */
void *mm_win_alloc(u32 bytes);          /* a window's pixels: whole pages at WIN_BASE, zeroed; NULL if none */
void mm_win_free(void *mem, u32 bytes);
u32 mm_win_frame(const void *p);        /* the frame behind a page of them */
void mm_vga_hide(int on);                /* A0000h-BFFFFh: a spare frame's (1), the VGA's again (0), or not
                                           present (2: a console job's, whose touch makes it full-screen) */
void *mm_fb_map(u32 phys, u32 bytes);   /* a framebuffer's memory at FB_BASE (in place of the last); 0 if too big */
void *mm_dev_map(u32 phys, u32 bytes);  /* a device's physical range, uncached, at DEV_BASE for good; 0 if no room */
void mm_dev_unmap(void *p, u32 bytes);

void *kmalloc(size_t n);
void kfree(void *p);

/* The heap allocator itself, independent of paging (tests/host/kmalloc_test.c). */
struct heap {
    u8 *base;
    u32 size;                           /* bytes in use by blocks, from base */
    u32 max;
    int (*grow)(struct heap *h, u32 new_size);  /* make base..base+new_size usable; 0 on success */
};
void *heap_alloc(struct heap *h, size_t n);
void heap_free(struct heap *h, void *p);
int heap_check(struct heap *h);         /* 0 when the block list is consistent */

/* The frame bitmap itself (tests/host/pmm_test.c). */
struct pmm {
    u32 *bits;                          /* 1 = used */
    u32 frames;
    u32 free;
    u32 next;
};
void pmm_setup(struct pmm *p, u32 *bits, u32 frames);
void pmm_add_free(struct pmm *p, u32 base, u32 length);
void pmm_reserve(struct pmm *p, u32 base, u32 length);
u32 pmm_take(struct pmm *p);
void pmm_give(struct pmm *p, u32 phys);
u32 pmm_take_run(struct pmm *p, u32 n);         /* n contiguous frames, lowest first; 0 if none */
int pmm_take_at(struct pmm *p, u32 phys, u32 n);        /* exactly these frames if all free; 0 or -1 */
void pmm_give_run(struct pmm *p, u32 phys, u32 n);
u32 pmm_largest_run(const struct pmm *p);       /* in frames */

#endif
