/* Physical frames, the kernel's address space and its heap (supervisor.md §4, §5). */
#ifndef K_MM_H
#define K_MM_H
#include "types.h"

struct bootinfo;

#define KMAP_BASE   0xDFC00000u         /* 1024 one-page windows onto any frame */
#define HEAP_BASE   0xC0400000u         /* kmalloc's region, grown a page at a time */
#define HEAP_MAX    0x00400000u
#define RECURSIVE   0xFFC00000u         /* PDE 1023 maps the page directory itself */

void mm_init(struct bootinfo *bi);
u32 mm_cr3(void);                       /* the kernel's page directory (physical) */
u32 kernel_phys(const void *p);         /* physical address of a kernel image/bss address */

u32 pmm_alloc(void);                    /* a free frame, 0 when none */
void pmm_free(u32 phys);
u32 pmm_free_frames(void);

u32 pmm_alloc_run(u32 n);                /* the kernel's frames: n contiguous, 0 if none */
int pmm_claim(u32 phys, u32 n);
void pmm_free_run(u32 phys, u32 n);
u32 pmm_largest(void);

void mm_vm_init(int a20);               /* 0-10FFFFh user-accessible for V86 mode */
void mm_set_a20(int on);                /* 100000h-10FFFFh: the HMA, or wrapped onto 0-FFFFh */

void *kmap(u32 phys);
void kunmap(void *p);

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
