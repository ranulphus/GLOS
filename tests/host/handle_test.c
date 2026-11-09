/* Host test for API 2.0's handle tables (kernel/app/handle.c). */
#include <stdio.h>
#include "../../kernel/app/handle.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static struct htab t;
static int objs[HANDLE_MAX + 1];

int main(void)
{
    u32 h, h2, a, b, i, n, hh;
    void *o;

    htab_init(&t);
    CHECK(handle_new(&t, 0, &objs[0]) == 0);                    /* kind 0 is no kind */
    h = handle_new(&t, 1, &objs[1]);
    CHECK(h && (h >> 24) == 1 && t.used == 1);
    CHECK(handle_obj(&t, h, 1) == &objs[1]);
    CHECK(handle_obj(&t, h, 2) == 0);                           /* another kind */
    CHECK(handle_kind(&t, h) == 1);
    CHECK(handle_kind(&t, 0) == 0 && handle_obj(&t, 0, 1) == 0);
    CHECK(handle_kind(&t, (1u << 24) | 0xFFFFu) == 0);          /* an index past the table */
    CHECK(handle_kind(&t, h ^ 0x10000u) == 0);                  /* another generation */
    CHECK(handle_kind(&t, (h & 0x00FFFFFFu) | (2u << 24)) == 0); /* the kind byte changed */

    CHECK(handle_close(&t, h) == &objs[1] && t.used == 0);
    CHECK(handle_close(&t, h) == 0);                            /* closed already */
    CHECK(handle_obj(&t, h, 1) == 0);
    h2 = handle_new(&t, 1, &objs[2]);                           /* the same entry, a new generation */
    CHECK((h2 & 0xFFFFu) == (h & 0xFFFFu) && h2 != h);
    CHECK(handle_obj(&t, h, 1) == 0 && handle_obj(&t, h2, 1) == &objs[2]);
    handle_close(&t, h2);

    for (n = 0; n < HANDLE_MAX; n++)                            /* full */
        CHECK(handle_new(&t, (u8)(1 + n % 3), &objs[n]) != 0);
    CHECK(t.used == HANDLE_MAX && handle_new(&t, 1, &objs[0]) == 0);
    i = 0;
    n = 0;
    while (handle_next(&t, &i, &hh, &o) == 0) {                 /* every one, each once, in order */
        CHECK(o == &objs[hh & 0xFFFFu] && handle_obj(&t, hh, (u8)(hh >> 24)) == o);
        n++;
    }
    CHECK(n == HANDLE_MAX);
    i = 0;
    while (handle_next(&t, &i, &hh, &o) == 0)                   /* all closed, as an app's end does */
        handle_close(&t, hh);
    CHECK(t.used == 0);

    a = handle_new(&t, 5, &objs[5]);                            /* a stale handle comes back only after 256 */
    for (i = 0; i < 255; i++) {
        handle_close(&t, a);
        a = handle_new(&t, 5, &objs[5]);
    }
    b = a;
    handle_close(&t, a);
    a = handle_new(&t, 5, &objs[5]);
    CHECK(a != b);

    printf("handle_test: %d failures\n", fails);
    return fails != 0;
}
