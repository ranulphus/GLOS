/* The kernel's address space (supervisor.md §4): its own page directory with
 *   PDE 0      0-10FFFFh identity (the same table in every address space)
 *   PDE 300h   the kernel image and bss at C0100000h (contiguous physically)
 *   PDE 301h   the kmalloc heap at C0400000h
 *   PDE 302h   memory devices read and write themselves (dma_alloc) at C0800000h
 *   PDE 37Fh   the kmap window at DFC00000h
 *   PDE 3FFh   the page directory itself (page tables visible at FFC00000h)
 * All of these tables live in the kernel's bss, so their physical addresses
 * are an offset from the image's. Then the frame bitmap and the heap. */
#include "glos/bootinfo.h"
#include "io.h"
#include "kprintf.h"
#include "cache.h"
#include "mm.h"

#define PTE_P 1u
#define PTE_W 2u
#define PTE_U 4u
#define MAX_FRAMES (1u << 20)                   /* 4 GB of physical memory tracked (1 GB until 2026-11-09) */

extern char __kernel_start[], __kernel_end[];

static u32 kpd[1024] __attribute__((aligned(4096)));
static u32 kpt_low[1024] __attribute__((aligned(4096)));
static u32 kpt_ndm[1024] __attribute__((aligned(4096)));
static u32 kpt_kernel[1024] __attribute__((aligned(4096)));
static u32 kpt_heap[1024] __attribute__((aligned(4096)));
static u32 kpt_kmap[1024] __attribute__((aligned(4096)));
static u32 kpt_dma[1024] __attribute__((aligned(4096)));
static u32 kpt_fb[FB_MAX >> 22][1024] __attribute__((aligned(4096)));
static u32 fb_pages;                    /* (mm_fb_map's last: the pages it mapped) */
static u32 kpt_win[WIN_MAX >> 22][1024] __attribute__((aligned(4096)));
static u32 kpt_dev[DEV_MAX >> 22][1024] __attribute__((aligned(4096)));
static u32 kpt_cache[CACHE_MAX >> 22][1024] __attribute__((aligned(4096)));
static u32 win_used[(WIN_MAX >> 12) / 32];      /* a bit a page of the windows' region */
/* The frame bitmap, as big as the RAM the loader found asks (mm_init): a static one for 4 GB was 128 KB of the
   kernel's bss on every PC (the memory diet, 2026-11-20). */
static u32 *frame_bits;
static struct pmm pmm;
static struct heap kheap;
static u32 image_phys, kmap_next, given;       /* given: the frames the loader handed over */
static u32 dma_next, dev_next;          /* pages handed out of the DMA and device regions */

u32 kernel_phys(const void *p) { return image_phys + ((u32)p - (u32)__kernel_start); }
u32 mm_cr3(void) { return kernel_phys(kpd); }

/* The frame bitmap and the heap belong to every thread, and a preemptible
   one (ssh, net) can lose the CPU to the VM thread in the middle of a call:
   each runs with interrupts off. */
#define LOCKED(type, expr) do { u32 f_ = irq_save(); type r_ = (expr); irq_restore(f_); return r_; } while (0)
#define LOCKED_V(stmt) do { u32 f_ = irq_save(); stmt; irq_restore(f_); } while (0)

/* The disk cache holds frames nobody else wants and gives them back the
   moment anything needs one (docs/disk-cache.md): a run, the blocks in the
   lowest window that would make one; a single frame, a whole window of them
   (1 MB, else 128 KB) found from where the last one ended, its next frames
   taken one after another as a fresh boot's are. Its coldest block's frame,
   wherever that was, scattered a program's pages over the whole memory, and
   a real PC's L2 cache and DRAM pages made GLQuake some 12% slower on
   bench-g400 once the cache had filled it (2026-11-17; 86Box models
   neither). The coldest block is still the last resort. With interrupts
   off, as everything here. */
/* A frame of the kernel's image or below it, which the allocator must never hand out (gate 52, 2026-11-24: the
   kernel's page table for the screen was given as the 486's recording buffer). One that comes out is logged with
   the caller and kept taken (it is the kernel's), and another is taken in its place: the screen's mapping is
   never zeroed again, whatever path set its frame loose; the warnings at the giving end name that path. */
static int kernel_frame(u32 phys, const char *what, void *from)
{
    if (!phys || phys >= image_phys + kernel_bytes())
        return 0;
    kprintf("GLOS-WARN pmm-take-kernel %s phys=%x from=%p\n", what, phys, from);
    return 1;
}

static u32 take1(void)
{
    static const u32 sizes[2] = { 256, 32 };
    static u32 after;                           /* (where the last window ended: the next is looked for from there) */
    u32 r = pmm_take(&pmm), w, i;
    while (kernel_frame(r, "frame", __builtin_return_address(0)))
        r = pmm_take(&pmm);
    for (i = 0; !r && i < 2 && cache_frames(); i++)
        if ((w = pmm_window_from(&pmm, sizes[i], cache_frame_evictable, 0, after)) != 0xFFFFFFFFu) {
            cache_evict_frames(w, sizes[i]);
            pmm.next = w;
            after = w + sizes[i];
            r = pmm_take(&pmm);
            while (kernel_frame(r, "frame-evicted", __builtin_return_address(0)))
                r = pmm_take(&pmm);
        }
    if (!r && cache_reclaim(1))
        r = pmm_take(&pmm);
    return kernel_frame(r, "frame-reclaimed", __builtin_return_address(0)) ? 0 : r;
}

static u32 take_run(u32 n)
{
    u32 r = pmm_take_run(&pmm, n), w;
    while (kernel_frame(r, "run", __builtin_return_address(0)))
        r = pmm_take_run(&pmm, n);              /* (that run left taken: it held the kernel's) */
    if (!r && n && cache_frames() && (w = pmm_window(&pmm, n, cache_frame_evictable, 0)) != 0xFFFFFFFFu) {
        cache_evict_frames(w, n);
        r = pmm_take_run(&pmm, n);
        if (kernel_frame(r, "run-evicted", __builtin_return_address(0)))
            r = 0;
    }
    return r;
}

static int take_at(u32 phys, u32 n)
{
    u32 f = phys >> 12, i;
    if (pmm_take_at(&pmm, phys, n) == 0)
        return 0;
    for (i = 0; i < n && f + i < pmm.frames; i++)
        if (cache_frame_evictable(f + i, 0))
            cache_evict_frames(f + i, 1);
    return pmm_take_at(&pmm, phys, n);
}

u32 pmm_alloc(void) { LOCKED(u32, take1()); }

/* (A frame of the kernel's image, or below it, is never given back: one that comes is a bug found, named with
   its caller, and kept.) */
static int kernel_given(u32 phys, const char *what, void *from)
{
    if (phys >= image_phys + kernel_bytes())
        return 0;
    kprintf("GLOS-WARN pmm-free-kernel %s phys=%x from=%p\n", what, phys, from);
    return 1;
}

void pmm_free(u32 phys)
{
    if (kernel_given(phys, "frame", __builtin_return_address(0)))
        return;
    LOCKED_V(pmm_give(&pmm, phys));
}
u32 pmm_free_frames(void) { return pmm.free; }
u32 mm_avail_frames(void) { return pmm.free + cache_frames(); }
u32 pmm_frames(void) { return pmm.top; }
u32 pmm_alloc_high(u32 keep) { LOCKED(u32, pmm.free > keep ? pmm_take_high(&pmm) : 0); }
u32 pmm_total_frames(void) { return given; }
u32 kheap_bytes(void) { return kheap.size; }
u32 kernel_bytes(void) { return (u32)__kernel_end - (u32)__kernel_start; }
u32 pmm_alloc_run(u32 n) { LOCKED(u32, take_run(n)); }
int pmm_claim(u32 phys, u32 n) { LOCKED(int, take_at(phys, n)); }
void pmm_free_run(u32 phys, u32 n)
{
    if (kernel_given(phys, "run", __builtin_return_address(0)))
        return;
    LOCKED_V(pmm_give_run(&pmm, phys, n));
}
u32 pmm_largest(void) { LOCKED(u32, cache_frames() ? pmm_longest(&pmm, cache_frame_evictable, 0) : pmm_largest_run(&pmm)); }

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
static u32 kheap_mapped;                 /* the heap's pages that are there, in bytes: its size and beyond */

static int heap_map1(void)              /* one more page, cleared (interrupts off) */
{
    u32 f = pmm_alloc();
    if (!f)
        return -1;
    kpt_heap[kheap_mapped / PAGE_SIZE] = f | PTE_P | PTE_W;
    invlpg(HEAP_BASE + kheap_mapped);
    memset((void *)(HEAP_BASE + kheap_mapped), 0, PAGE_SIZE);
    kheap_mapped += PAGE_SIZE;
    return 0;
}

static int heap_grow(struct heap *h, u32 new_size)
{
    u32 want = ALIGN_UP(new_size, PAGE_SIZE);
    (void)h;
    while (kheap_mapped < want)
        if (heap_map1() != 0)
            return -1;
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

/* Its own page tables freed, then the directory. A table is its own if its frame is not the kernel's for that
   slot: the frames compared, not the whole entries, whose flags differ by themselves (the CPU sets a directory
   entry's Accessed bit in the directory it walks: the desktop's thread draws the screen with an app's space
   loaded, and that app's copy of the framebuffer's entry gained the bit the kernel's lacked; at the app's
   exit the kernel's own table for the screen was freed, and handed out again as the 486's recording buffer,
   zeroed: every page of the screen gone. Gate 52, 2026-11-24.) */
void mm_space_free(u32 pd_phys)
{
    u32 *pd = kmap(pd_phys), i, kept = 0, first = 0;
    for (i = 1; i < 1023; i++)
        if ((pd[i] & PTE_P) && (pd[i] & ~0xFFFu) != (kpd[i] & ~0xFFFu))
            pmm_free(pd[i] & ~0xFFFu);
        else if ((pd[i] & PTE_P) && pd[i] != kpd[i] && !kept++)
            first = i;                          /* (the kernel's, its flags apart: what was freed before) */
    if (kept)
        kprintf("GLOS-MM space-free kernel-tables-kept=%u first=%x pde=%x kernel=%x\n", kept, first, pd[first],
                kpd[first]);
    kunmap(pd);
    pmm_free(pd_phys);
}

void mm_space_enter(u32 pd_phys) { write_cr3(pd_phys ? pd_phys : mm_cr3()); }

/* A native machine's address space (docs/v1/native-dos.md, native-DOS sessions): the kernel's, but for PDE 0, a
   page table of its own over n fresh frames from address 0, zeroed (their frames into frames[]). 0 without
   memory. */
u32 mm_space_new_low(u32 *frames, u32 n)
{
    u32 pd = mm_space_new(), pt = pd ? pmm_alloc() : 0, i;
    u32 *p;

    if (!pd || !pt || n > 1024) {
        if (pt)
            pmm_free(pt);
        if (pd)
            mm_space_free(pd);
        return 0;
    }
    for (i = 0; i < n; i++) {
        if (!(frames[i] = pmm_alloc())) {
            while (i--)
                pmm_free(frames[i]);
            pmm_free(pt);
            mm_space_free(pd);
            return 0;
        }
        p = kmap(frames[i]);
        memset(p, 0, PAGE_SIZE);
        kunmap(p);
    }
    p = kmap(pt);
    memset(p, 0, PAGE_SIZE);
    for (i = 0; i < n; i++)
        p[i] = frames[i] | PTE_P | PTE_W | PTE_U;
    kunmap(p);
    p = kmap(pd);
    p[0] = pt | PTE_P | PTE_W | PTE_U;
    kunmap(p);
    return pd;
}

u8 *mm_ndm_map(u32 slot, const u32 *frames, u32 n)
{
    u32 i, base = slot * 256;

    if (slot >= NDM_SLOTS || n > 256)
        return 0;
    for (i = 0; i < 256; i++) {
        kpt_ndm[base + i] = i < n ? frames[i] | PTE_P | PTE_W : 0;
        invlpg(NDM_BASE + ((base + i) << 12));
    }
    return (u8 *)(NDM_BASE + (slot << 20));
}

void mm_ndm_unmap(u32 slot)
{
    u32 i, base = slot * 256;

    if (slot >= NDM_SLOTS)
        return;
    for (i = 0; i < 256; i++) {
        kpt_ndm[base + i] = 0;
        invlpg(NDM_BASE + ((base + i) << 12));
    }
}

/* ... and freed: its first MB's frames and table, then the rest as mm_space_free does (never while it is the
   loaded one). */
void mm_space_free_low(u32 pd)
{
    u32 *p = kmap(pd), pt = p[0] & ~0xFFFu, i;

    p[0] = kpd[0];                              /* (mm_space_free leaves PDE 0 alone) */
    kunmap(p);
    p = kmap(pt);
    for (i = 0; i < 1024; i++)
        if (p[i] & PTE_P)
            pmm_free(p[i] & ~0xFFFu);
    kunmap(p);
    pmm_free(pt);
    mm_space_free(pd);
}

/* glos fault's first steps (/SELFTEST; the sess suite's panic-*-fault): a kernel frame freed is refused; one set
   loose behind the guards' backs (as by a path not yet known) is never handed out: a DMA buffer, which takes the
   lowest free run, is given another. 0 if both hold. */
static u8 guard_page[4096] __attribute__((aligned(4096)));

int mm_guard_test(void)
{
    u32 k = kernel_phys(guard_page), phys = 0, f;
    void *p;
    guard_page[0] = 0x5A;
    pmm_free(k);                                /* (refused: GLOS-WARN pmm-free-kernel) */
    f = irq_save();
    pmm_give(&pmm, k);                          /* (loose, as a path of the kernel's own might have set it) */
    irq_restore(f);
    p = dma_alloc(1, &phys);                    /* (GLOS-WARN pmm-take-kernel run, and another frame) */
    kprintf("GLOS-SELFTEST pmm-guard loose=%x got=%x kept=%u\n", k, phys, guard_page[0] == 0x5A);
    return p && phys != k && phys >= image_phys + kernel_bytes() && guard_page[0] == 0x5A ? 0 : -1;
}

/* What a page fault in the kernel found (cpu.c's panic, 2026-11-24: the 486's audio case died writing the
   framebuffer's row 460 from the desktop's thread, its page not present, once in some twenty runs): the
   loaded directory against the kernel's, the kernel's own entries for the address, then the loaded
   directory's (read last: if it is a frame given to something else, reading it may fault again). */
void mm_fault_note(u32 lin)
{
    u32 pde = kpd[lin >> 22], pte = 0xFFFFFFFFu, cr3 = read_cr3() & ~0xFFFu, i = (lin >> 12) & 1023;
    if (lin >= FB_BASE && lin < FB_BASE + FB_MAX)
        pte = kpt_fb[(lin - FB_BASE) >> 22][i];
    else if (lin >= WIN_BASE && lin < WIN_BASE + WIN_MAX)
        pte = kpt_win[(lin - WIN_BASE) >> 22][i];
    else if (lin >= HEAP_BASE && lin < HEAP_BASE + 0x400000u)
        pte = kpt_heap[i];
    else if (lin >= DEV_BASE && lin < DEV_BASE + DEV_MAX)
        pte = kpt_dev[(lin - DEV_BASE) >> 22][i];
    kprintf("GLOS-PANIC fault lin=%p cr3=%p kernel=%p kpde=%p kpte=%p fb_mapped=%u\n", lin, cr3, mm_cr3(), pde, pte,
            fb_pages);
    kprintf("GLOS-PANIC fault loaded-pde=%p\n", CUR_PD[lin >> 22]);
    if (CUR_PD[lin >> 22] & PTE_P)
        kprintf("GLOS-PANIC fault loaded-pte=%p\n", CUR_PT[lin >> 12]);
}

/* (A user address alone: the kernel's own entries are never changed through these; one that tries is a bug
   found, and named with its caller.) */
static int kernel_lin(u32 lin, const char *what, void *from)
{
    if (lin < 0xC0000000u)
        return 0;
    kprintf("GLOS-WARN mm-kernel-lin %s lin=%p from=%p\n", what, lin, from);
    return 1;
}

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
    if (kernel_lin(lin, "map", __builtin_return_address(0)) || pt_for(lin) != 0)
        return -1;
    CUR_PT[lin >> 12] = (phys & ~0xFFFu) | PTE_P | (flags & (PTE_W | PTE_U | 0x18 | 0x60 | 0x200));
                                                                /* PWT, PCD; accessed, dirty (kept); AVL 9 */
    invlpg(lin);
    return 0;
}

u32 mm_unmap(u32 lin)
{
    u32 pte;
    if (kernel_lin(lin, "unmap", __builtin_return_address(0)) || !(CUR_PD[lin >> 22] & PTE_P))
        return 0;
    pte = CUR_PT[lin >> 12];
    CUR_PT[lin >> 12] = 0;
    invlpg(lin);
    return (pte & PTE_P) ? pte & ~0xFFFu : 0;
}

/* One page's entry in any space, the current one or another (pd_phys its
   directory), through kmap: an app's registers taken from it while another
   thread runs (kernel/gfx/gpu.c). -1 if it has no page table there. */
int mm_space_set(u32 pd_phys, u32 lin, u32 pte)
{
    u32 *pd, pde, *pt;
    if (kernel_lin(lin, "space-set", __builtin_return_address(0)))
        return -1;
    pd = kmap(pd_phys), pde = pd[lin >> 22];
    kunmap(pd);
    if (!(pde & PTE_P))
        return -1;
    pt = kmap(pde & ~0xFFFu);
    pt[(lin >> 12) & 1023] = pte;
    kunmap(pt);
    if ((read_cr3() & ~0xFFFu) == pd_phys)
        invlpg(lin);
    return 0;
}

/* A page of a DPMI block that gets its frame when first touched (the host's
   page fault, kernel/dpmi/mem.c lin_fault()): a not-present PTE marked. */
int mm_lazy(u32 lin)
{
    if (kernel_lin(lin, "lazy", __builtin_return_address(0)) || pt_for(lin) != 0)
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

/* n frames in a row, zeroed and mapped in the kernel for good, for a device
   that reads and writes memory itself (a network card's rings): where they
   are, and physically in *phys; 0 without memory. */
/* Windows' pixels (kernel/wm/desk.c): `bytes` of memory, zeroed, in whole
   pages at WIN_BASE, each page a frame of its own (so a window's can also
   be mapped into its app: mm_win_frame). NULL without memory or room. */
void *mm_win_alloc(u32 bytes)
{
    u32 n = (bytes + 0xFFF) >> 12, i, run = 0, first = 0, f = irq_save();
    u8 *p = 0;
    for (i = 0; n && i < WIN_MAX >> 12 && run < n; i++) {
        if (win_used[i >> 5] & (1u << (i & 31)))
            run = 0;
        else if (!run++)
            first = i;
    }
    if (n && run == n && mm_avail_frames() > n + 64) {   /* (never the last frames: the kernel's own needs first) */
        for (i = first; i < first + n; i++) {
            win_used[i >> 5] |= 1u << (i & 31);
            kpt_win[i >> 10][i & 1023] = take1() | PTE_P | PTE_W;
            invlpg(WIN_BASE + (i << 12));
        }
        p = (u8 *)(WIN_BASE + (first << 12));
    }
    irq_restore(f);
    if (p)
        memset(p, 0, n << 12);
    return p;
}

/* Sparse windows (a DPMI context's LDT: the memory diet, 2026-11-20): whole pages at WIN_BASE, every one mapped,
   read-only, onto one zeroed frame until mm_win_back gives it a frame of its own. Reading an unbacked page reads
   zeros, as a fully allocated one would (86Box's VERR reads a descriptor even past the LDT's limit: it must not
   fault there). With interrupts off, as everything here. */
static u32 zero_frame;

void *mm_win_sparse(u32 bytes)
{
    u32 n = (bytes + 0xFFF) >> 12, i, run = 0, first = 0, f = irq_save();
    u8 *p = 0;
    if (!zero_frame && (zero_frame = take1()) != 0) {
        void *z = kmap(zero_frame);
        memset(z, 0, PAGE_SIZE);
        kunmap(z);
    }
    for (i = 0; zero_frame && n && i < WIN_MAX >> 12 && run < n; i++) {
        if (win_used[i >> 5] & (1u << (i & 31)))
            run = 0;
        else if (!run++)
            first = i;
    }
    if (zero_frame && n && run == n) {
        for (i = first; i < first + n; i++) {
            win_used[i >> 5] |= 1u << (i & 31);
            kpt_win[i >> 10][i & 1023] = zero_frame | PTE_P;
            invlpg(WIN_BASE + (i << 12));
        }
        p = (u8 *)(WIN_BASE + (first << 12));
    }
    irq_restore(f);
    return p;
}

/* The pages of a sparse window from mem for bytes, those still on the zero frame, each given a zeroed frame of its
   own, writable. 0; -1 without memory (those backed so far stay so). */
int mm_win_back(void *mem, u32 bytes)
{
    u32 first = ((u32)mem - WIN_BASE) >> 12, last = ((u32)mem + bytes - 1 - WIN_BASE) >> 12, i, f = irq_save();
    int r = 0;
    for (i = first; i <= last && i < WIN_MAX >> 12; i++) {
        u32 *pte = &kpt_win[i >> 10][i & 1023], fr;
        if ((*pte & ~0xFFFu) != zero_frame)
            continue;
        if (mm_avail_frames() <= 64 || !(fr = take1())) {
            r = -1;
            break;
        }
        *pte = fr | PTE_P | PTE_W;
        invlpg(WIN_BASE + (i << 12));
        memset((void *)(WIN_BASE + (i << 12)), 0, PAGE_SIZE);
    }
    irq_restore(f);
    return r;
}

/* A sparse window given back: the frames of its backed pages, and its pages. */
void mm_win_free_sparse(void *mem, u32 bytes)
{
    u32 n = (bytes + 0xFFF) >> 12, first = ((u32)mem - WIN_BASE) >> 12, i, f = irq_save();
    for (i = first; i < first + n && i < WIN_MAX >> 12; i++) {
        u32 fr = kpt_win[i >> 10][i & 1023] & ~0xFFFu;
        if (fr && fr != zero_frame && !kernel_given(fr, "win-sparse", __builtin_return_address(0)))
            pmm_give(&pmm, fr);
        kpt_win[i >> 10][i & 1023] = 0;
        invlpg(WIN_BASE + (i << 12));
        win_used[i >> 5] &= ~(1u << (i & 31));
    }
    irq_restore(f);
}

void mm_win_free(void *mem, u32 bytes)
{
    u32 n = (bytes + 0xFFF) >> 12, first = ((u32)mem - WIN_BASE) >> 12, i, f = irq_save();
    for (i = first; i < first + n && i < WIN_MAX >> 12; i++) {
        u32 fr = kpt_win[i >> 10][i & 1023] & ~0xFFFu;
        if (fr && fr != zero_frame && !kernel_given(fr, "win", __builtin_return_address(0)))    /* (an unbacked page's 0 would free frame 0) */
            pmm_give(&pmm, fr);
        kpt_win[i >> 10][i & 1023] = 0;
        invlpg(WIN_BASE + (i << 12));
        win_used[i >> 5] &= ~(1u << (i & 31));
    }
    irq_restore(f);
}

/* The disk cache's tables (its blocks' descriptors, hash chains, count
   mode's sectors): bytes of zeroed pages at CACHE_BASE + off, mapped where
   they aren't yet; NULL without room or memory. */
void *mm_cache_map(u32 off, u32 bytes)
{
    u32 first = off >> 12, last = (off + bytes + 0xFFF) >> 12, i, f, ok;
    if (last > CACHE_MAX >> 12 || last <= first)
        return 0;
    f = irq_save();
    ok = pmm.free > (last - first) + 256;
    for (i = first; ok && i < last; i++)
        if (!(kpt_cache[i >> 10][i & 1023] & PTE_P)) {
            kpt_cache[i >> 10][i & 1023] = take1() | PTE_P | PTE_W;
            invlpg(CACHE_BASE + (i << 12));
            memset((void *)(CACHE_BASE + (i << 12)), 0, PAGE_SIZE);
        }
    irq_restore(f);
    return ok ? (void *)(CACHE_BASE + off) : 0;
}

/* The frame behind a page of a window's pixels. */
u32 mm_win_frame(const void *p) { u32 i = ((u32)p - WIN_BASE) >> 12; return kpt_win[i >> 10][i & 1023] & ~0xFFFu; }

/* A page of the first MB backed by a frame of GLOS's (frame), or by what is
   there again (0): the resident stub's page of upper memory (docs/upper-
   memory.md). In every address space, as mm_vga_hide's. */
void mm_low_page(u32 lin, u32 frame)
{
    u32 i = lin >> 12, f = irq_save();
    kpt_low[i] = (kpt_low[i] & 0xFFF) | PTE_P | (frame ? frame : i << 12);
    invlpg(lin);
    irq_restore(f);
}

/* The VGA's memory at A0000h-BFFFFh out of reach (1) or back (0): while the
   desktop has the screen, a program that writes text there (straight to
   B8000h, as DOS programs do) must not hit the card, whose window onto the
   same memory may be anywhere in the picture. One spare frame stands in for
   all 32 pages. In every address space: they share this page table. */
void mm_vga_hide(int on)
{
    static u32 spare;
    u32 i, f = irq_save();
    if (on && !spare)
        spare = take1();
    for (i = 0xA0; i < 0xC0; i++) {
        if (on == 2)                            /* not there at all: a touch faults (a console job's, M9) */
            kpt_low[i] = (kpt_low[i] & 0xFFF & ~PTE_P) | (i << 12);
        else
            kpt_low[i] = ((kpt_low[i] & 0xFFF) | PTE_P) | (on && spare ? spare : i << 12);
        invlpg(i << 12);
    }
    irq_restore(f);
}

/* The screen's framebuffer (kernel/gfx/display.c): `bytes` of a device's
   memory at phys, at FB_BASE. One at a time: a new one replaces the last.
   Cached as the machine has that memory (its chipset's or MTRRs' say). */
void *mm_fb_map(u32 phys, u32 bytes)
{
    u32 n = ((phys & 0xFFF) + bytes + 0xFFF) >> 12, i, f;
    if (!bytes || n > FB_MAX >> 12)
        return 0;
    f = irq_save();
    fb_pages = n;
    for (i = 0; i < FB_MAX >> 12; i++) {
        kpt_fb[i >> 10][i & 1023] = i < n ? ((phys & ~0xFFFu) + (i << 12)) | PTE_P | PTE_W : 0;
        invlpg(FB_BASE + (i << 12));
    }
    irq_restore(f);
    return (void *)(FB_BASE + (phys & 0xFFF));
}

/* A device's registers or memory (the HAL's: hal/port/glos.c): `bytes` at
   phys, uncached, in the device region, from where the last ended. Undone
   by mm_dev_unmap; the region's room comes back only from its end. */
void *mm_dev_map(u32 phys, u32 bytes)
{
    u32 n = ((phys & 0xFFF) + bytes + 0xFFF) >> 12, i, f = irq_save();
    u8 *p = 0;
    if (bytes && n <= (DEV_MAX >> 12) - dev_next) {
        for (i = 0; i < n; i++) {
            kpt_dev[(dev_next + i) >> 10][(dev_next + i) & 1023] = ((phys & ~0xFFFu) + (i << 12)) | PTE_P | PTE_W | MM_UC;
            invlpg(DEV_BASE + ((dev_next + i) << 12));
        }
        p = (u8 *)(DEV_BASE + (dev_next << 12) + (phys & 0xFFF));
        dev_next += n;
    }
    irq_restore(f);
    return p;
}

void mm_dev_unmap(void *p, u32 bytes)
{
    u32 first = ((u32)p - DEV_BASE) >> 12, n = (((u32)p & 0xFFF) + bytes + 0xFFF) >> 12, i, f;
    if ((u32)p < DEV_BASE || first >= DEV_MAX >> 12)
        return;
    f = irq_save();
    for (i = first; i < first + n && i < DEV_MAX >> 12; i++) {
        kpt_dev[i >> 10][i & 1023] = 0;
        invlpg(DEV_BASE + (i << 12));
    }
    if (first + n == dev_next)
        dev_next = first;
    irq_restore(f);
}

void *dma_alloc(u32 n, u32 *phys)
{
    u32 f = irq_save(), base = 0, i;
    u8 *p = 0;
    if (n && dma_next + n <= 1024 && (base = take_run(n)) != 0) {
        for (i = 0; i < n; i++)
            kpt_dma[dma_next + i] = (base + (i << 12)) | PTE_P | PTE_W;
        p = (u8 *)(DMA_BASE + (dma_next << 12));
        dma_next += n;
    }
    irq_restore(f);
    if (p) {
        for (i = 0; i < n << 12; i++)
            p[i] = 0;
        *phys = base;
    }
    return p;
}

/* A block that needs the heap to grow by more than a page has its pages mapped and cleared first, one page an
   interrupts-off stretch: the allocation itself is then short. (All of it in one stretch, the 190 KB of the
   on-screen prompt's terminal took several milliseconds of a 486 at start-up, and a serial port's FIFO, 1.4 ms
   deep at 115,200 baud, overran: an echo request over the cable lost, about one start in three; 2026-11-15.) */
void *kmalloc(size_t n)
{
    if (n >= PAGE_SIZE && n <= HEAP_MAX) {
        for (;;) {
            u32 f = irq_save();
            int more = !heap_has(&kheap, n) && kheap_mapped < HEAP_MAX
                && kheap_mapped < ALIGN_UP(kheap.size + (u32)n + 64, PAGE_SIZE) && heap_map1() == 0;
            irq_restore(f);
            if (!more)
                break;
        }
    }
    LOCKED(void *, heap_alloc(&kheap, n));
}
void kfree(void *p) { LOCKED_V(heap_free(&kheap, p)); }

/* One past the highest frame of the loader's free ranges (4 GB at most). */
static u32 frames_present(const struct bootinfo *bi)
{
    u32 i, top = 0;

    for (i = 0; i < bi->n_ranges; i++)
        if (bi->range[i].type == BI_MEM_FREE) {
            u32 end = bi->range[i].base + bi->range[i].length, f;

            f = end < bi->range[i].base ? MAX_FRAMES : end >> 12;
            if (f > top)
                top = f;
        }
    return top < MAX_FRAMES ? top : MAX_FRAMES;
}

/* Where `bytes` (whole pages) of the bitmap can live: the first free range with room above the HMA and clear of
   the kernel's image (image_phys up to image_end); 0 if none has. */
static u32 bitmap_home(const struct bootinfo *bi, u32 bytes, u32 image_end)
{
    u32 i;

    for (i = 0; i < bi->n_ranges; i++) {
        u32 b = bi->range[i].base, e = b + bi->range[i].length;

        if (bi->range[i].type != BI_MEM_FREE)
            continue;
        if (e < b || e > 0xFFFFF000u)
            e = 0xFFFFF000u;
        if (b < 0x110000)
            b = 0x110000;
        b = (b + 0xFFF) & ~0xFFFu;
        if (b < image_end && b + bytes > image_phys)
            b = image_end;
        if (b < e && e - b >= bytes)
            return b;
    }
    return 0;
}

void mm_init(struct bootinfo *bi)
{
    u32 i, pages = ((u32)__kernel_end - (u32)__kernel_start) / PAGE_SIZE;
    u32 frames, bits_bytes, bits_pages, bits_phys;

    image_phys = bi->kernel_phys;
    for (i = 0; i < 0x110; i++)
        kpt_low[i] = (i << 12) | PTE_P | PTE_W;
    for (i = 0; i < pages; i++)
        kpt_kernel[0x100 + i] = (image_phys + (i << 12)) | PTE_P | PTE_W;
    /* The frame bitmap's pages: in the kernel's page table just past the image, wherever in RAM they are. */
    frames = frames_present(bi);
    bits_bytes = (frames + 31) / 32 * 4;
    bits_pages = (bits_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    bits_phys = bitmap_home(bi, bits_pages * PAGE_SIZE, image_phys + pages * PAGE_SIZE);
    if (!frames || !bits_phys || 0x100 + pages + bits_pages > 1024) {
        kprintf("GLOS-PANIC frame-bitmap frames=%u pages=%u home=%x\n", frames, bits_pages, bits_phys);
        for (;;)
            hlt();
    }
    for (i = 0; i < bits_pages; i++)
        kpt_kernel[0x100 + pages + i] = (bits_phys + (i << 12)) | PTE_P | PTE_W;
    frame_bits = (u32 *)((u32)__kernel_start + pages * PAGE_SIZE);
    kpd[0] = kernel_phys(kpt_low) | PTE_P | PTE_W;
    kpd[VMLOW_BASE >> 22] = kernel_phys(kpt_low) | PTE_P | PTE_W;     /* (the same table, the kernel's alone) */
    kpd[NDM_BASE >> 22] = kernel_phys(kpt_ndm) | PTE_P | PTE_W;
    kpd[KERNEL_PDE] = kernel_phys(kpt_kernel) | PTE_P | PTE_W;
    kpd[HEAP_BASE >> 22] = kernel_phys(kpt_heap) | PTE_P | PTE_W;
    kpd[KMAP_BASE >> 22] = kernel_phys(kpt_kmap) | PTE_P | PTE_W;
    kpd[DMA_BASE >> 22] = kernel_phys(kpt_dma) | PTE_P | PTE_W;
    for (i = 0; i < FB_MAX >> 22; i++)          /* (every address space copies these: before the first is made) */
        kpd[(FB_BASE >> 22) + i] = kernel_phys(kpt_fb[i]) | PTE_P | PTE_W;
    for (i = 0; i < WIN_MAX >> 22; i++)
        kpd[(WIN_BASE >> 22) + i] = kernel_phys(kpt_win[i]) | PTE_P | PTE_W;
    for (i = 0; i < DEV_MAX >> 22; i++)
        kpd[(DEV_BASE >> 22) + i] = kernel_phys(kpt_dev[i]) | PTE_P | PTE_W;
    for (i = 0; i < CACHE_MAX >> 22; i++)
        kpd[(CACHE_BASE >> 22) + i] = kernel_phys(kpt_cache[i]) | PTE_P | PTE_W;
    kpd[1023] = kernel_phys(kpd) | PTE_P | PTE_W;
    write_cr3(mm_cr3());

    pmm_setup(&pmm, frame_bits, frames);
    for (i = 0; i < bi->n_ranges; i++)
        if (bi->range[i].type == BI_MEM_FREE) {
            pmm_add_free(&pmm, bi->range[i].base, bi->range[i].length);
            given += (u32)(bi->range[i].length >> 12);
        }
    pmm_reserve(&pmm, 0, 0x110000);                     /* conventional memory, UMA, HMA */
    pmm_reserve(&pmm, image_phys, pages * PAGE_SIZE);   /* the kernel */
    pmm_reserve(&pmm, bits_phys, bits_pages * PAGE_SIZE);   /* ... and the frame bitmap */

    kheap.base = (u8 *)HEAP_BASE;
    kheap.size = 0;
    kheap.max = HEAP_MAX;
    kheap.grow = heap_grow;
}
