/* The GLOS API's entry (PRD §5.6, §10; include/glos/api.h): the far
 * procedure DPMI 0A00h "GLOS" gives a program, a HLT in the host's
 * trampoline page. The function is in EAX; its results go into the frame,
 * and the host's far return follows (kernel/dpmi/host.c). 1: the program
 * becomes an app. 2: the call is to be made again from the same HLT, when a
 * handler of the app's has run (a sleep an interrupt of its own cut short).
 *
 * The API is 2.0's (api2.c and the areas' files). API 1.x (GLOS's first,
 * M6 to M10) is gone since phase B's step 15: a call with its numbers, a
 * program built for it, is refused with GLOS_E_VERSION, and such a program
 * carries on as it would on plain DOS. */
#include "api2int.h"
#include "arch.h"
#include "dpmi.h"
#include "mm.h"
#include "pci.h"
#include "vm.h"

/* DRAW's CACHE for memory of a video card's own that the program has mapped (API 2.11): 4 to 64 KB of it
   write-combining, or uncached again. The pages are looked up in the program's own map, so the range is one it
   may write already; by PCI it is inside one memory range of a video card's, so it is never RAM and never
   another device's. Its pages are mapped again with the cache bits that let the CPU's range through (or keep
   it out: GLOS_CACHE_UC). */
static u32 cache_card(u32 lin, u32 bytes, u32 type)
{
    u32 n = bytes >> 12, i, pte, phys;
    int r;
    if ((type != GLOS_CACHE_WC && type != GLOS_CACHE_UC) || (lin & 0xFFF) || bytes < 0x1000 || bytes > 0x10000
        || (bytes & (bytes - 1)) || lin < USER_BASE || lin + bytes < lin)
        return GLOS_E_ARGUMENT;
    pte = mm_lookup(lin);
    phys = pte & ~0xFFFu;
    for (i = 0; i < n; i++) {
        pte = mm_lookup(lin + (i << 12));
        if ((pte & (1u | MM_U | MM_W)) != (1u | MM_U | MM_W) || (pte & ~0xFFFu) != phys + (i << 12))
            return GLOS_E_ARGUMENT;             /* (not mapped, not the program's to write, or not one run of the card's) */
    }
    if ((phys & (bytes - 1)) || !pci_video_mem(phys, bytes))
        return GLOS_E_ARGUMENT;
    if (type == GLOS_CACHE_WC) {
        r = mtrr_wc_page(dctx, phys, bytes, 1);
        if (r < 0)
            return r == -1 ? GLOS_E_FUNCTION : r == -2 ? GLOS_E_ARGUMENT : GLOS_E_REFUSED;
    } else {
        mtrr_wc_page(dctx, 0, 0, 0);            /* (its own goes; the MTRR command's stays, with these pages out of it) */
    }
    for (i = 0; i < n; i++) {
        pte = mm_lookup(lin + (i << 12));
        mm_map(lin + (i << 12), phys + (i << 12), (pte & (MM_W | MM_U | MM_MAPPED)) | (type == GLOS_CACHE_WC ? 0x10u : MM_UC));
    }
    return 0;
}

/* How the CPU caches a range within one of the running program's own DOS blocks, or of a video card's memory
   that it has mapped (DRAW's CACHE): 0, GLOS_E_ARGUMENT (not such a range, or not a type), GLOS_E_FUNCTION (this
   CPU can't), GLOS_E_REFUSED (a card's: the CPU's ranges don't allow it, or another program has one). */
u32 glos_cache_range(u32 lin, u32 bytes, u32 type)
{
    const struct dosblk *d;
    u32 end = lin + bytes;
    int r;
    if (!dctx)
        return GLOS_E_ARGUMENT;
    if (lin >= 0x110000u)
        return cache_card(lin, bytes, type);
    for (d = dctx->dosblks; d; d = d->next) {
        u32 b = (u32)d->seg << 4, e = b + ((u32)vm_rd16(((u32)d->seg - 1) << 4 | 3) << 4);   /* (its MCB's size) */
        if (lin >= b && end <= e && end > lin)
            break;
    }
    if (!d)
        return GLOS_E_ARGUMENT;
    r = mtrr_set(dctx, lin, bytes, type);
    return r == -1 ? GLOS_E_FUNCTION : r ? GLOS_E_ARGUMENT : 0;
}

int glos_api(struct trapframe *tf)
{
    tf->eflags &= ~FL_CF;
    if ((tf->eax >> 16) == 2)
        return api2_call(tf);
    tf->eax = GLOS_E_VERSION;                   /* (1.x's numbers: its majors were in INIT's EBX, not here) */
    tf->eflags |= FL_CF;
    return 0;
}
