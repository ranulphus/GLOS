/* EMS for the system VM on a native boot (docs/v1/native-dos.md, N4's EMS, step E2): kernel/vm/ems.c's bookkeeping
 * over GLOS's memory. The page frame is a free 64 KB of upper memory (kernel/mm/umapage.c's rules: no ROM, no
 * card's BAR, nothing that reads as RAM already, so GLOS's own pages there are passed over), below E0000h as
 * `auto` keeps; its physical pages show frames of GLOS's (four 4 KB ones a 16 KB page) by the VM's page tables
 * (mm_low_page), the memory there its own again when nothing is mapped. Frames are taken when a program
 * allocates pages, zeroed, and given back when it frees them: an EMS no program uses costs nothing but the
 * window. At most 8 MB, and no more than half the memory free at the start. INT 67h's entry is vdos.c's. */
#include <string.h>

#include "glos/bootinfo.h"
#include "kprintf.h"
#include "mm.h"
#include "pci.h"
#include "umapage.h"
#include "vm.h"
#include "ems.h"

#define AUTO_PAGES 512                  /* auto: 8 MB */
#define MAX_PAGES 2048                  /* [memory] ems's most: 32 MB */

static struct ems e;
static u32 (*frames)[4];                /* each logical page's four frames (0: the slot free) */
static unsigned nslots;
static u32 frame_lin;                   /* the page frame's linear address */
static int on;

static void em_map(void *ctx, int phys, unsigned long id)
{
    int k;
    (void)ctx;
    for (k = 0; k < 4; k++)
        mm_low_page(frame_lin + (u32)phys * EMS_PAGE + (u32)k * 4096, id == EMS_UNMAPPED ? 0 : frames[id][k]);
}

static unsigned long em_alloc(void *ctx)
{
    unsigned i;
    int k;
    (void)ctx;
    for (i = 0; i < nslots && frames[i][0]; i++)
        ;
    if (i == nslots)
        return EMS_UNMAPPED;
    for (k = 0; k < 4; k++) {
        if (!(frames[i][k] = pmm_alloc())) {
            while (k--)
                pmm_free(frames[i][k]), frames[i][k] = 0;
            return EMS_UNMAPPED;
        }
        memset(kmap(frames[i][k]), 0, 4096);    /* (another program's bytes not seen) */
    }
    return i;
}

static void em_free(void *ctx, unsigned long id)
{
    int k;
    (void)ctx;
    for (k = 0; k < 4; k++)
        if (frames[id][k])
            pmm_free(frames[id][k]), frames[id][k] = 0;
}

static unsigned char *em_page_at(void *ctx, unsigned long id, unsigned off)
{
    (void)ctx;
    return (unsigned char *)kmap(frames[id][off >> 12]) + (off & 0xFFF);
}

/* realloc's way for a handle's page list (its size kept in the word before it). */
static void *em_grow(void *old, unsigned long bytes)
{
    u32 *o = old ? (u32 *)old - 1 : 0, *n;
    if (!bytes) {
        if (o)
            kfree(o);
        return 0;
    }
    if (!(n = kmalloc(bytes + 4)))
        return 0;
    n[0] = bytes;
    if (o) {
        memcpy(n + 1, o + 1, o[0] < bytes ? o[0] : bytes);
        kfree(o);
    }
    return n + 1;
}

static void uma_read(u32 phys, u8 *out, u32 n) { memcpy(out, vm_ptr(phys), n); }

/* The first 64 KB at a 16 KB boundary from C8000h to DFFFFh whose sixteen pages are free: its address, or 0. */
static u32 find_window(void)
{
    u32 claims[32], at, k;
    struct upage_env env = { uma_read, claims, 0 };
    env.nclaims = pci_low_claims(claims, 32);
    for (at = UP_FIRST; at + 0x10000 <= UP_AUTO_END; at += EMS_PAGE) {
        for (k = 0; k < 16 && upage_check(&env, at + k * 4096) == UP_OK; k++)
            ;
        if (k == 16)
            return at;
    }
    return 0;
}

int ems_vm_init(void)
{
    static const struct ems_ops ops = { 0, em_map, em_alloc, em_free, em_page_at };
    unsigned total = pmm_free_frames() / 8, want = AUTO_PAGES;     /* (half the free memory, in 16 KB pages) */
    u32 kb = vm.bi->ems_kb;                     /* ([memory] ems as the loader read it: GLOS.CFG isn't the
                                                   kernel's yet when EMS starts) */

    if (kb == BI_EMS_OFF) {
        kprintf("GLOS-EMS off reason=setting\n");
        return -1;
    }
    if (kb >= 64)
        want = kb / 16 > MAX_PAGES ? MAX_PAGES : kb / 16;
    if (total > want)
        total = want;
    if (!(frame_lin = find_window())) {
        kprintf("GLOS-EMS off reason=no-window\n");
        return -1;
    }
    if (total < 4 || !(frames = kmalloc(sizeof *frames * total))) {
        kprintf("GLOS-EMS off reason=memory\n");
        return -1;
    }
    memset(frames, 0, sizeof *frames * total);
    nslots = total;
    ems_init(&e, &ops, total, frame_lin >> 4, vm_ptr(0), 0x110000, em_grow);
    on = 1;
    kprintf("GLOS-EMS on frame=%04x pages=%u kb=%u\n", frame_lin >> 4, total, total * 16);
    return 0;
}

int ems_vm_on(void) { return on; }

/* (MEM, the shell's: C1d) Its pages (16 KB each), in all and not handed out; 0 and 0 with EMS off. */
void ems_vm_figures(u32 *total, u32 *free)
{
    *total = on ? e.total : 0;
    *free = on ? e.free : 0;
}

void ems_vm_call(struct ems_regs *r)
{
    static u32 said;
    unsigned fn = r->ax >> 8 & 0xFF;

    if (fn >= 0x40 && fn < 0x60 && !(said & (1u << (fn - 0x40)))) {
        said |= 1u << (fn - 0x40);
        kprintf("GLOS-EMS first ah=%02x\n", fn);
    }
    ems_call(&e, r);
}
