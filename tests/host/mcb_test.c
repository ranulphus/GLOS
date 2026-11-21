/* mcb_test: kernel/ndos/mcb.c, a machine's memory chain as MS-DOS keeps it (RBIL INT 21h 48h, 49h, 4Ah, 58h,
 * table 01628): allocation by first, best and last fit, the largest block told when it fails, free blocks
 * joined when allocating or resizing but not when freeing, a resize that fails making the block as large as it
 * can, a program's blocks freed at its exit, upper memory linked and unlinked, the high strategies, a broken
 * chain found. "mcb_test: 0 failures" when all held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/ndos/mcb.h"

static int fails;
static unsigned char *mem;
static struct dos_mem d;

#define CHECK(cond, ...)                                                                                       \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            printf("FAIL %s:%d: ", __func__, __LINE__);                                                        \
            printf(__VA_ARGS__);                                                                               \
            printf("\n");                                                                                      \
            fails++;                                                                                           \
        }                                                                                                      \
    } while (0)

#define MEM (0x110000UL)
#define FIRST 0x1000u
#define LOW_END 0xA000u

static unsigned owner_at(unsigned seg) { return mem[(seg - 1) * 16 + 1] | mem[(seg - 1) * 16 + 2] << 8; }
static unsigned size_at(unsigned seg) { return mem[(seg - 1) * 16 + 3] | mem[(seg - 1) * 16 + 4] << 8; }
static unsigned type_at(unsigned seg) { return mem[(seg - 1) * 16]; }

/* Blocks in the chain from its start (unlinked: the low ones). */
static int blocks(void)
{
    unsigned s = FIRST;
    int n = 1;

    while (mem[s * 16] == 'M') {
        s += (mem[s * 16 + 3] | mem[s * 16 + 4] << 8) + 1;
        n++;
    }
    return n;
}

static void fresh(int umbs)
{
    static const unsigned umb[] = { 0xC800, 0xD000, 0xD800, 0xE000 };

    memset(mem, 0xCC, MEM);
    dos_mem_init(&d, mem, MEM, FIRST, LOW_END, umb, umbs);
}

static void t_alloc_free(void)
{
    unsigned a, b, c, big, all;
    int r;

    fresh(0);
    all = LOW_END - FIRST - 1;
    CHECK(dos_mem_check(&d) == 0 && blocks() == 1, "a fresh chain");
    r = dos_mem_alloc(&d, 0xFFFF, 0x500, &a, &big);
    CHECK(r == 0x08 && big == all, "all of it asked: %d largest %x (want %x)", r, big, all);
    CHECK(dos_mem_alloc(&d, 0x100, 0x500, &a, &big) == 0 && a == FIRST + 1 && owner_at(a) == 0x500 &&
              size_at(a) == 0x100, "first: %x", a);
    CHECK(dos_mem_alloc(&d, 0x200, 0x500, &b, &big) == 0 && b == a + 0x101, "second: %x", b);
    CHECK(dos_mem_alloc(&d, 0x10, 0x600, &c, &big) == 0 && c == b + 0x201, "third: %x", c);
    CHECK(blocks() == 4, "blocks %d", blocks());
    /* Freed, not joined; the next allocation joins them. */
    CHECK(dos_mem_free(&d, a) == 0 && dos_mem_free(&d, b) == 0, "free");
    CHECK(blocks() == 4 && owner_at(a) == 0 && size_at(a) == 0x100, "not joined when freed");
    CHECK(dos_mem_alloc(&d, 0x250, 0x700, &a, &big) == 0 && a == FIRST + 1 && size_at(a) == 0x250, "joined: %x",
          a);
    CHECK(size_at(a + 0x251) == 0x300 - 0x250 + 1 - 1 - 0 && owner_at(a + 0x251) == 0, "the rest: %x",
          size_at(a + 0x251));
    CHECK(dos_mem_free(&d, 0x1234) == 0x09, "an address that is no block");
    CHECK(dos_mem_free(&d, 0) == 0x09, "segment 0");
    /* A program's blocks freed at its exit. */
    dos_mem_free_owner(&d, 0x700);
    CHECK(owner_at(a) == 0 && owner_at(c) == 0x600, "its exit: %x %x", owner_at(a), owner_at(c));
    CHECK(dos_mem_check(&d) == 0, "still a chain");
}

static void t_fits(void)
{
    unsigned a, b, c, e, x, big;

    fresh(0);
    /* Holes of 0x300, 0x100 and 0x200 paragraphs, then the rest. */
    dos_mem_alloc(&d, 0x300, 1, &a, &big);
    dos_mem_alloc(&d, 0x10, 1, &x, &big);
    dos_mem_alloc(&d, 0x100, 1, &b, &big);
    dos_mem_alloc(&d, 0x10, 1, &x, &big);
    dos_mem_alloc(&d, 0x200, 1, &c, &big);
    dos_mem_alloc(&d, 0x10, 1, &x, &big);
    dos_mem_free(&d, a);
    dos_mem_free(&d, b);
    dos_mem_free(&d, c);
    CHECK(dos_mem_alloc(&d, 0xF0, 2, &e, &big) == 0 && e == a, "first fit: %x (want %x)", e, a);
    dos_mem_free(&d, e);
    CHECK(dos_mem_strategy(&d, 1) == 0, "best fit");
    CHECK(dos_mem_alloc(&d, 0xF0, 2, &e, &big) == 0 && e == b, "best fit: %x (want %x)", e, b);
    dos_mem_free(&d, e);
    CHECK(dos_mem_strategy(&d, 2) == 0, "last fit");
    CHECK(dos_mem_alloc(&d, 0xF0, 2, &e, &big) == 0 && e == LOW_END - 0xF0, "last fit: %x", e);
    CHECK(type_at(e) == 'Z' && owner_at(e) == 2, "last fit's block ends the chain");
    CHECK(dos_mem_strategy(&d, 3) == 0x01 && dos_mem_strategy(&d, 0x103) == 0x01 && dos_mem_strategy(&d, 0xC0) == 1,
          "strategies that aren't");
    CHECK(dos_mem_check(&d) == 0, "still a chain");
}

static void t_resize(void)
{
    unsigned a, b, big;

    fresh(0);
    dos_mem_alloc(&d, 0x100, 1, &a, &big);
    dos_mem_alloc(&d, 0x100, 1, &b, &big);
    /* Smaller: the rest a free block. Larger, into the free block after it. */
    CHECK(dos_mem_resize(&d, a, 0x80, &big) == 0 && size_at(a) == 0x80 && owner_at(a + 0x81) == 0, "smaller");
    CHECK(dos_mem_resize(&d, a, 0x100, &big) == 0 && size_at(a) == 0x100 && type_at(a) == 'M', "back again");
    /* Larger than there is room for (b after it): as large as it can be, and that size said. */
    CHECK(dos_mem_resize(&d, a, 0x200, &big) == 0x08 && big == 0x100 && size_at(a) == 0x100, "no room: %x", big);
    dos_mem_free(&d, b);
    CHECK(dos_mem_resize(&d, a, 0xFFFF, &big) == 0x08 && big == LOW_END - FIRST - 1 &&
              size_at(a) == LOW_END - FIRST - 1 && type_at(a) == 'Z',
          "all after it: %x, made that large (%x, %c)", big, size_at(a), type_at(a));
    CHECK(dos_mem_resize(&d, 0x1234, 1, &big) == 0x09, "an address that is no block");
    CHECK(dos_mem_check(&d) == 0, "still a chain");
}

static void t_upper(void)
{
    unsigned a, big;

    fresh(2);
    CHECK(dos_mem_check(&d) == 0, "a chain with upper memory");
    /* Unlinked: low memory only, whatever the strategy. */
    CHECK(dos_mem_strategy(&d, 0x80) == 0, "high first");
    CHECK(dos_mem_alloc(&d, 0x10, 1, &a, &big) == 0 && a < 0xA000, "unlinked: low (%x)", a);
    dos_mem_free(&d, a);
    CHECK(dos_mem_link(&d, 1) == 0 && d.linked, "linked");
    CHECK(dos_mem_alloc(&d, 0x10, 1, &a, &big) == 0 && a == 0xC801, "linked, high first: %x", a);
    CHECK(dos_mem_strategy(&d, 0x40) == 0 && dos_mem_alloc(&d, 0x1000, 1, &a, &big) == 0x08 &&
              big == 0xE000 - 0xD800 - 1,
          "high only, too much: largest %x", big);
    CHECK(dos_mem_strategy(&d, 0x00) == 0, "low first fit");
    CHECK(dos_mem_alloc(&d, 0x10, 1, &a, &big) == 0 && a < 0xA000, "linked, low first: %x", a);
    /* Low memory full: low first fit goes on into upper memory. */
    CHECK(dos_mem_alloc(&d, 0xFFFF, 1, &a, &big) == 0x08, "all");
    CHECK(dos_mem_alloc(&d, big, 1, &a, &big) == 0 && a < 0xA000, "the rest of low memory");
    CHECK(dos_mem_alloc(&d, 0x100, 1, &a, &big) == 0 && a > 0xC000, "then upper: %x", a);
    CHECK(dos_mem_link(&d, 0) == 0 && !d.linked, "unlinked");
    CHECK(dos_mem_check(&d) == 0, "unlinked: still a chain (upper memory too)");
    dos_mem_free_owner(&d, 1);
    CHECK(owner_at(a) == 0, "a program's upper memory freed at its exit, unlinked");
    fresh(0);
    CHECK(dos_mem_link(&d, 1) == 0x01, "no upper memory to link");
}

static void t_broken(void)
{
    unsigned a, b, big;

    fresh(0);
    dos_mem_alloc(&d, 0x100, 1, &a, &big);
    dos_mem_alloc(&d, 0x100, 1, &b, &big);
    mem[(b - 1) * 16] = 'X';                    /* a program wrote over an MCB */
    CHECK(dos_mem_check(&d) == 0x07, "a broken chain found");
    CHECK(dos_mem_alloc(&d, 0x10, 1, &a, &big) == 0x07, "allocation through it: 07h");
}

int main(void)
{
    mem = malloc(MEM);
    if (!mem)
        return 2;
    t_alloc_free();
    t_fits();
    t_resize();
    t_upper();
    t_broken();
    printf("mcb_test: %d failures\n", fails);
    return fails != 0;
}
