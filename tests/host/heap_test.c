/* Host test for kmalloc's allocator (kernel/mm/heap.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mm.h"

static u8 arena[1 << 20] __attribute__((aligned(16)));
static int grows, fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int grow(struct heap *h, u32 n) { (void)h; grows++; return n <= sizeof arena ? 0 : -1; }

int main(void)
{
    struct heap h = { arena, 0, sizeof arena, grow };
    void *p[500];
    u32 i, sizes[500];
    srand(1);
    for (i = 0; i < 500; i++) {
        sizes[i] = 1 + rand() % 2000;
        p[i] = heap_alloc(&h, sizes[i]);
        CHECK(p[i] && ((u32)(size_t)p[i] & 15) == 0);
        memset(p[i], (int)i, sizes[i]);
    }
    CHECK(heap_check(&h) == 0);
    for (i = 0; i < 500; i += 2) heap_free(&h, p[i]);
    CHECK(heap_check(&h) == 0);
    for (i = 1; i < 500; i += 2) {                      /* the survivors are intact */
        u32 k;
        for (k = 0; k < sizes[i]; k++)
            if (((u8 *)p[i])[k] != (u8)i) { CHECK(0); break; }
    }
    {
        u32 before = h.size;
        void *q = heap_alloc(&h, 100);                  /* reuses a freed block */
        CHECK(q && h.size == before);
        heap_free(&h, q);
    }
    for (i = 1; i < 500; i += 2) heap_free(&h, p[i]);
    CHECK(heap_check(&h) == 0);
    {
        void *big = heap_alloc(&h, h.size - 64);        /* everything merged back into one block */
        CHECK(big != NULL);
        heap_free(&h, big);
    }
    CHECK(heap_alloc(&h, 0) == NULL);
    CHECK(heap_alloc(&h, 2 << 20) == NULL);
    heap_free(&h, NULL);
    printf("heap_test: %d failures (%d grows)\n", fails, grows);
    return fails != 0;
}
