/* Host test for the frame bitmap (kernel/mm/pmm.c). */
#include <stdio.h>
#include "mm.h"

static u32 bits[1024];
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static u32 cache_like;                          /* frames from here up are "the cache's" (0: none) */
static int also(u32 frame, void *arg) { (void)arg; return cache_like && frame >= cache_like; }

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

    /* Contiguous runs (XMS blocks). */
    pmm_setup(&p, bits, 4096);                          /* 16 MB */
    pmm_add_free(&p, 0x100000, 0xF00000);
    pmm_reserve(&p, 0x200000, 0x1000);                  /* a hole at 2 MB */
    CHECK(pmm_largest_run(&p) == (0x1000000 - 0x201000) / 4096);
    f = pmm_take_run(&p, 16);
    CHECK(f == 0x100000);                               /* lowest first */
    CHECK(pmm_take_run(&p, 0x100) == 0x201000);         /* 1 MB does not fit below the hole ... */
    CHECK(pmm_take_run(&p, 0xF0) == 0x110000);          /* ... but 240 frames do, exactly */
    CHECK(pmm_take_run(&p, 0x10000) == 0);              /* too big */
    i = p.free;
    pmm_give_run(&p, 0x100000, 16);
    CHECK(p.free == i + 16);
    CHECK(pmm_take_at(&p, 0x100000, 16) == 0);          /* claimed back exactly */
    CHECK(pmm_take_at(&p, 0x100000, 1) == -1);          /* already used */
    CHECK(pmm_take_at(&p, 0x301000, 2) == 0);           /* just after the 1 MB run */
    CHECK(pmm_take_run(&p, 0) == 0);

    /* The disk cache's: frames from the top, and runs that count its frames as free (docs/disk-cache.md). */
    {
        static u32 b2[64];
        struct pmm q;
        u32 top;
        pmm_setup(&q, b2, 2048);                        /* 8 MB */
        pmm_add_free(&q, 0x100000, 0x700000);
        top = pmm_take_high(&q);
        CHECK(top == 0x7FF000 && pmm_take_high(&q) == 0x7FE000);
        for (i = 0; i < 30; i++)                        /* across a whole word in use */
            pmm_take_high(&q);
        CHECK(pmm_take_high(&q) == 0x7DF000);          /* (7E0h-7FFh: a whole word, skipped) */
        CHECK(pmm_take_run(&q, 4) == 0x100000);         /* runs still come from the bottom */
        cache_like = 0x7DF;                             /* frames 7DFh-7FFh (taken) "the cache's" */
        CHECK(pmm_window(&q, 33, also, 0) == 0x104);    /* plenty free low down */
        pmm_reserve(&q, 0x104000, 0x6D9000);            /* only 7DDh and the cache's left */
        CHECK(pmm_window(&q, 33, also, 0) == 0x7DD);
        CHECK(pmm_window(&q, 36, also, 0) == 0xFFFFFFFFu);
        CHECK(pmm_longest(&q, also, 0) == 35);          /* 7DDh and 7DEh free, then the cache's 33 */
        cache_like = 0;
        CHECK(pmm_longest(&q, also, 0) == pmm_largest_run(&q));
        CHECK(pmm_window(&q, 0, also, 0) == 0xFFFFFFFFu);
        while (pmm_take_high(&q))
            ;
        CHECK(q.free == 0 && pmm_take_high(&q) == 0);
    }
    printf("pmm_test: %d failures\n", fails);
    return fails != 0;
}
