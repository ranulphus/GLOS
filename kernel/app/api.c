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
#include "dpmi.h"
#include "vm.h"

/* How the CPU caches a range within one of the running program's own DOS blocks (DRAW's CACHE): 0, GLOS_E_ARGUMENT (not such a range, or not a type), GLOS_E_FUNCTION (this CPU can't). */
u32 glos_cache_range(u32 lin, u32 bytes, u32 type)
{
    const struct dosblk *d;
    u32 end = lin + bytes;
    int r;
    if (!dctx)
        return GLOS_E_ARGUMENT;
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
