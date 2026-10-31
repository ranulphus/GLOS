/* Host test for the upper-memory page rules (kernel/mm/umapage.c; docs/upper-memory.md, Which page): open
 * bus, option ROMs that reach into a page from below, a page whose bytes change between reads, PCI claims,
 * the range a setting may name, and auto's order (C8000h up, E0000h-EFFFFh only by name). */
#include <stdio.h>
#include <string.h>
#include "umapage.h"

static u8 uma[0x40000];                         /* C0000h-FFFFFh */
static u32 flip = 0xFFFFFFFFu;                  /* a page whose first byte changes on every read */
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static void rd(u32 phys, u8 *out, u32 n)
{
    memcpy(out, uma + (phys - 0xC0000u), n);
    if (phys == flip)
        uma[flip - 0xC0000u] ^= 1;
}

static void rom(u32 at, u32 kb)
{
    memset(uma + (at - 0xC0000u), 0x5A, kb * 1024);
    uma[at - 0xC0000u] = 0x55;
    uma[at - 0xC0000u + 1] = 0xAA;
    uma[at - 0xC0000u + 2] = (u8)(kb * 2);
}

int main(void)
{
    struct upage_env e = { rd, 0, 0 };
    u32 claims[2];

    memset(uma, 0xFF, sizeof uma);
    rom(0xC0000, 32);                           /* a video BIOS, C0000h-C7FFFh */
    CHECK(upage_check(&e, 0xC8000) == UP_OK);
    CHECK(upage_auto(&e) == 0xC8000);
    CHECK(upage_check(&e, 0xC0000) == UP_RANGE);        /* below C8000h: never */
    CHECK(upage_check(&e, 0xC8800) == UP_RANGE);        /* not a page's start */
    CHECK(upage_check(&e, 0xF0000) == UP_RANGE);
    CHECK(upage_check(&e, 0xEF000) == UP_OK);           /* by name */

    rom(0xC8000, 2);                            /* 2 KB at C8000h: the whole page is out */
    CHECK(upage_check(&e, 0xC8000) == UP_ROM);
    CHECK(upage_check(&e, 0xC9000) == UP_OK);
    rom(0xC8800, 4);                            /* C8800h-C97FFh: into the next page from below */
    CHECK(upage_check(&e, 0xC9000) == UP_ROM);
    CHECK(upage_auto(&e) == 0xCA000);

    uma[0xCA123 - 0xC0000] = 0x00;              /* data in a page */
    CHECK(upage_check(&e, 0xCA000) == UP_DATA);
    CHECK(upage_auto(&e) == 0xCB000);
    flip = 0xCB000;                             /* a register: FFh on one read, FEh on the next */
    CHECK(upage_check(&e, 0xCB000) == UP_DATA);
    flip = 0xFFFFFFFFu;
    uma[0xCB000 - 0xC0000] = 0xFF;

    claims[0] = 0xCB000;                        /* a BAR at CB000h, another inside CC000h */
    claims[1] = 0xCC800;
    e.claims = claims;
    e.nclaims = 2;
    CHECK(upage_check(&e, 0xCB000) == UP_BAR);
    CHECK(upage_check(&e, 0xCC000) == UP_BAR);
    CHECK(upage_auto(&e) == 0xCD000);

    memset(uma + (0xCD000 - 0xC0000), 0, 0xE0000 - 0xCD000);  /* nothing open below E0000h: auto finds none */
    CHECK(upage_auto(&e) == 0);
    CHECK(upage_check(&e, 0xE0000) == UP_OK);           /* though E0000h can be named */
    CHECK(!strcmp(upage_why(UP_ROM), "rom") && !strcmp(upage_why(UP_DATA), "data"));

    printf("%s: %s\n", __FILE__, fails ? "FAILED" : "ok");
    return fails != 0;
}
