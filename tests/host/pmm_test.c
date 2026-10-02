/* Host test for the frame bitmap (kernel/mm/pmm.c). */
#include <stdio.h>
#include "mm.h"

static u32 bits[1024];
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    struct pmm p;
    u32 f, i, got[64];
    pmm_setup(&p, bits, 32768);                         /* 128 MB */
    CHECK(p.free == 0);
    CHECK(pmm_take(&p) == 0);
    pmm_add_free(&p, 0x100000, 0x7F00000);              /* 1 MB to 128 MB */
    CHECK(p.free == (0x8000000 - 0x100000) / 4096);
    pmm_reserve(&p, 0, 0x110000);                       /* low memory and the HMA */
    pmm_reserve(&p, 0x110000, 0x1B000);                 /* a kernel */
    CHECK(p.free == (0x8000000 - 0x110000 - 0x1B000) / 4096);
    pmm_add_free(&p, 0x200800, 0x1000);                 /* partial frames don't become free */
    CHECK(p.free == (0x8000000 - 0x110000 - 0x1B000) / 4096);
    pmm_reserve(&p, 0x300800, 0x10);                    /* but a partial reserve takes the frame */
    CHECK(p.free == (0x8000000 - 0x110000 - 0x1B000) / 4096 - 1);
    for (i = 0; i < 64; i++) {
        got[i] = pmm_take(&p);
        CHECK(got[i] >= 0x12B000 && !(got[i] & 0xFFF));
        CHECK(i == 0 || got[i] != got[i - 1]);
    }
    f = p.free;
    for (i = 0; i < 64; i++) pmm_give(&p, got[i]);
    CHECK(p.free == f + 64);
    pmm_give(&p, got[0]);                               /* double free is ignored */
    CHECK(p.free == f + 64);
    printf("pmm_test: %d failures\n", fails);
    return fails != 0;
}
