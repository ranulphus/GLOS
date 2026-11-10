/* Host test for the locks and signals of an app's threads (kernel/app/tsync.c). */
#include <stdio.h>
#include "../../kernel/app/tsync.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

int main(void)
{
    struct tlock l = { 0, 0 };
    struct tsignal s = { 0, 0 }, one = { 0, 1 };

    /* A lock: free, one thread's, its again (as many gives), another's only once it is free. */
    CHECK(tlock_give(&l, 1) == -1);                     /* nobody holds it */
    CHECK(tlock_take(&l, 1) == 1 && l.owner == 1 && l.depth == 1);
    CHECK(tlock_take(&l, 2) == 0 && l.owner == 1);
    CHECK(tlock_take(&l, 1) == 1 && l.depth == 2);      /* once more for its holder */
    CHECK(tlock_give(&l, 2) == -1 && l.depth == 2);     /* not the other's to give */
    CHECK(tlock_give(&l, 0) == -1);
    CHECK(tlock_give(&l, 1) == 0 && l.owner == 1);      /* still held: taken twice */
    CHECK(tlock_take(&l, 2) == 0);
    CHECK(tlock_give(&l, 1) == 1 && l.owner == 0 && l.depth == 0);
    CHECK(tlock_take(&l, 2) == 1 && l.owner == 2);
    /* Its holder ends: free, however often it was taken. Another thread's end changes nothing. */
    CHECK(tlock_take(&l, 2) == 1 && l.depth == 2);
    CHECK(tlock_drop(&l, 1) == 0 && l.owner == 2 && l.depth == 2);
    CHECK(tlock_drop(&l, 0) == 0);
    CHECK(tlock_drop(&l, 2) == 1 && l.owner == 0 && l.depth == 0);
    CHECK(tlock_drop(&l, 2) == 0);
    CHECK(tlock_take(&l, 17) == 1 && tlock_give(&l, 17) == 1);
    /* Taken too often to count: refused, and what was taken can still be given. */
    l.owner = 3;
    l.depth = 0xFFFFFFFFu;
    CHECK(tlock_take(&l, 3) == 0 && l.depth == 0xFFFFFFFFu);
    CHECK(tlock_give(&l, 3) == 0 && l.depth == 0xFFFFFFFEu);

    /* A signal that stays: set for every wait until it is cleared. */
    CHECK(tsignal_take(&s) == 0);
    tsignal_set(&s, 1);
    CHECK(tsignal_take(&s) == 1 && tsignal_take(&s) == 1);
    tsignal_set(&s, 0);
    CHECK(tsignal_take(&s) == 0);
    tsignal_set(&s, 7);                                 /* (any value but 0 sets it) */
    CHECK(s.set == 1);
    /* One that lets one waiter go: the wait that finds it set clears it; two sets before a wait are one. */
    CHECK(tsignal_take(&one) == 0);
    tsignal_set(&one, 1);
    tsignal_set(&one, 1);
    CHECK(tsignal_take(&one) == 1 && tsignal_take(&one) == 0);
    tsignal_set(&one, 1);
    tsignal_set(&one, 0);
    CHECK(tsignal_take(&one) == 0);

    printf(fails ? "tsync_test: %d FAILED\n" : "tsync_test: ok\n", fails);
    return fails != 0;
}
