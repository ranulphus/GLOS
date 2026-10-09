/* The kernel's address space (supervisor.md §4): its own page directory with
 *   PDE 0      0-10FFFFh identity (the same table in every address space)
 *   PDE 300h   the kernel image and bss at C0100000h (contiguous physically)
 *   PDE 301h   the kmalloc heap at C0400000h
 *   PDE 37Fh   the kmap window at DFC00000h
 *   PDE 3FFh   the page directory itself (page tables visible at FFC00000h)
 * All of these tables live in the kernel's bss, so their physical addresses
 * are an offset from the image's. Then the frame bitmap and the heap. */
#include "glos/bootinfo.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"

#define PTE_P 1u
#define PTE_W 2u
#define PTE_U 4u
#define MAX_FRAMES (1u << 18)                   /* 1 GB of physical memory tracked */

extern char __kernel_start[], __kernel_end[];

static u32 kpd[1024] __attribute__((aligned(4096)));
static u32 kpt_low[1024] __attribute__((aligned(4096)));
static u32 kpt_kernel[1024] __attribute__((aligned(4096)));
static u32 kpt_heap[1024] __attribute__((aligned(4096)));
static u32 kpt_kmap[1024] __attribute__((aligned(4096)));
static u32 frame_bits[MAX_FRAMES / 32];
static struct pmm pmm;
static struct heap kheap;
static u32 image_phys, kmap_next;

u32 kernel_phys(const void *p) { return image_phys + ((u32)p - (u32)__kernel_start); }
u32 mm_cr3(void) { return kernel_phys(kpd); }

u32 pmm_alloc(void) { return pmm_take(&pmm); }
void pmm_free(u32 phys) { pmm_give(&pmm, phys); }
u32 pmm_free_frames(void) { return pmm.free; }
u32 pmm_alloc_run(u32 n) { return pmm_take_run(&pmm, n); }
int pmm_claim(u32 phys, u32 n) { return pmm_take_at(&pmm, phys, n); }
void pmm_free_run(u32 phys, u32 n) { pmm_give_run(&pmm, phys, n); }
u32 pmm_largest(void) { return pmm_largest_run(&pmm); }

/* V86 code runs at CPL 3: the identity map of 0-10FFFFh becomes user pages
   (supervisor.md §4). Nothing of the kernel's is in them. */
void mm_vm_init(int a20)
{
    u32 i;
    for (i = 0; i < 0x110; i++)
        kpt_low[i] |= PTE_U;
    kpd[0] |= PTE_U;
    write_cr3(mm_cr3());
    mm_set_a20(a20);
}

/* The A20 gate stays on physically; with it virtually off, the HMA's 16 pages
   map onto 0-FFFFh, so FFFF:0010 wraps to 0:0000 as on an 8086. */
void mm_set_a20(int on)
{
    u32 i;
    for (i = 0; i < 16; i++) {
        kpt_low[0x100 + i] = ((on ? 0x100 + i : i) << 12) | PTE_P | PTE_W | PTE_U;
        invlpg(0x100000 + (i << 12));
    }
}

void *kmap(u32 phys)
{
    u32 slot = kmap_next++ % 1024, lin = KMAP_BASE + slot * PAGE_SIZE;
    kpt_kmap[slot] = (phys & ~0xFFFu) | PTE_P | PTE_W;
    invlpg(lin);
    return (void *)(lin + (phys & 0xFFF));
}

void kunmap(void *p)
{
    u32 lin = (u32)p & ~0xFFFu;
    kpt_kmap[(lin - KMAP_BASE) / PAGE_SIZE] = 0;
    invlpg(lin);
}

/* The heap grows by mapping fresh frames, zeroed. */
static int heap_grow(struct heap *h, u32 new_size)
{
    u32 have = ALIGN_UP(h->size, PAGE_SIZE), want = ALIGN_UP(new_size, PAGE_SIZE);
    for (; have < want; have += PAGE_SIZE) {
        u32 f = pmm_alloc();
        if (!f)
            return -1;
        kpt_heap[have / PAGE_SIZE] = f | PTE_P | PTE_W;
        invlpg(HEAP_BASE + have);
        memset((void *)(HEAP_BASE + have), 0, PAGE_SIZE);
    }
    return 0;
}

/* ---- DPMI address spaces (supervisor.md §4, §12.1): a page directory per
   context with the kernel's PDEs (identity, image, heap, kmap) and its own
   recursive slot; the user region's page tables are its own. Mapping works
   on the current space, through the recursive slot. */

#define CUR_PD  ((volatile u32 *)0xFFFFF000u)
#define CUR_PT  ((volatile u32 *)RECURSIVE)

u32 mm_space_new(void)
{
    u32 f = pmm_alloc(), i;
    u32 *pd;
    if (!f)
        return 0;
    pd = kmap(f);
    for (i = 0; i < 1024; i++)
        pd[i] = kpd[i];
    pd[1023] = f | PTE_P | PTE_W;
    kunmap(pd);
    return f;
}

void mm_space_free(u32 pd_phys)
{
    u32 *pd = kmap(pd_phys), i;
    for (i = 1; i < 1023; i++)
        if ((pd[i] & PTE_P) && pd[i] != kpd[i])
            pmm_free(pd[i] & ~0xFFFu);
    kunmap(pd);
    pmm_free(pd_phys);
}

void mm_space_enter(u32 pd_phys) { write_cr3(pd_phys ? pd_phys : mm_cr3()); }

/* The page table for lin in the current space, made if there is none: 0, or -1. */
static int pt_for(u32 lin)
{
    u32 i = lin >> 22;
    if (!(CUR_PD[i] & PTE_P)) {
        u32 f = pmm_alloc(), k;
        if (!f)
            return -1;
        CUR_PD[i] = f | PTE_P | PTE_W | PTE_U;
        invlpg(RECURSIVE + (i << 12));
        for (k = 0; k < 1024; k++)
            CUR_PT[(i << 10) + k] = 0;
    }
    return 0;
}

int mm_map(u32 lin, u32 phys, u32 flags)
{
    if (pt_for(lin) != 0)
        return -1;
    CUR_PT[lin >> 12] = (phys & ~0xFFFu) | PTE_P | (flags & (PTE_W | PTE_U | 0x18 | 0x60 | 0x200));
                                                                /* PWT, PCD; accessed, dirty (kept); AVL 9 */
    invlpg(lin);
    return 0;
}

u32 mm_unmap(u32 lin)
{
    u32 pte;
    if (!(CUR_PD[lin >> 22] & PTE_P))
        return 0;
    pte = CUR_PT[lin >> 12];
    CUR_PT[lin >> 12] = 0;
    invlpg(lin);
    return (pte & PTE_P) ? pte & ~0xFFFu : 0;
}

/* A page of a DPMI block that gets its frame when first touched (the host's
   page fault, kernel/dpmi/mem.c lin_fault()): a not-present PTE marked. */
int mm_lazy(u32 lin)
{
    if (pt_for(lin) != 0)
        return -1;
    CUR_PT[lin >> 12] = MM_LAZY | MM_W | MM_U;
    invlpg(lin);
    return 0;
}

u32 mm_lookup(u32 lin)
{
    if (!(CUR_PD[lin >> 22] & PTE_P))
        return 0;
    return CUR_PT[lin >> 12];
}

/* The DPMI host's trampoline page (supervisor.md §3.1): a frame at
   TRAMP_LIN in PDE 0's table, so in every address space; ring 3 may run
   and read it, not write it. Returns it for the host to fill. */
void *mm_tramp_page(u32 lin)
{
    u32 f = pmm_alloc();
    if (!f)
        return 0;
    kpt_low[lin >> 12] = f | PTE_P | PTE_U;
    invlpg(lin);
    return (void *)lin;
}

void *kmalloc(size_t n) { return heap_alloc(&kheap, n); }
void kfree(void *p) { heap_free(&kheap, p); }

void mm_init(struct bootinfo *bi)
{
    u32 i, pages = ((u32)__kernel_end - (u32)__kernel_start) / PAGE_SIZE;

    image_phys = bi->kernel_phys;
    for (i = 0; i < 0x110; i++)
        kpt_low[i] = (i << 12) | PTE_P | PTE_W;
    for (i = 0; i < pages; i++)
        kpt_kernel[0x100 + i] = (image_phys + (i << 12)) | PTE_P | PTE_W;
    kpd[0] = kernel_phys(kpt_low) | PTE_P | PTE_W;
    kpd[KERNEL_PDE] = kernel_phys(kpt_kernel) | PTE_P | PTE_W;
    kpd[HEAP_BASE >> 22] = kernel_phys(kpt_heap) | PTE_P | PTE_W;
    kpd[KMAP_BASE >> 22] = kernel_phys(kpt_kmap) | PTE_P | PTE_W;
    kpd[1023] = kernel_phys(kpd) | PTE_P | PTE_W;
    write_cr3(mm_cr3());

    pmm_setup(&pmm, frame_bits, MAX_FRAMES);
    for (i = 0; i < bi->n_ranges; i++)
        if (bi->range[i].type == BI_MEM_FREE)
            pmm_add_free(&pmm, bi->range[i].base, bi->range[i].length);
    pmm_reserve(&pmm, 0, 0x110000);                     /* conventional memory, UMA, HMA */
    pmm_reserve(&pmm, image_phys, pages * PAGE_SIZE);   /* the kernel */

    kheap.base = (u8 *)HEAP_BASE;
    kheap.size = 0;
    kheap.max = HEAP_MAX;
    kheap.grow = heap_grow;
}
