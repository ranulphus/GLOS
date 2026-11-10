/* Host test for the clipboard's store (kernel/app/clipstore.c; API 2.0 step 6). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../kernel/app/clipstore.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int live, refuse;                /* blocks out; the next allocation fails */
static long bytes;

static void *t_alloc(u32 n)
{
    if (refuse) {
        refuse = 0;
        return 0;
    }
    live++;
    bytes += n;
    return malloc(n);
}

static void t_free(void *p, u32 n)
{
    live--;
    bytes -= n;
    free(p);
}

static const struct clip_host host = { t_alloc, t_free };
static struct clipstore s;

static struct clip_content *content(const char *text, u32 extra)       /* text, and `extra` more formats */
{
    struct clip_content *c = clip_new(&s);
    u32 i;
    void *p = clip_add(&s, c, 1, (u32)strlen(text));
    memcpy(p, text, strlen(text));
    for (i = 0; i < extra; i++)
        memset(clip_add(&s, c, 2 + i, 100 + i), (int)i, 100 + i);
    return c;
}

int main(void)
{
    struct clip_content *a, *b, *r1, *r2;
    const struct clip_item *it;
    char name[80];
    u32 i, f, g;

    clipstore_init(&s, &host);
    CHECK(clip_ref(&s) == 0 && clip_find(0, 1) == 0 && s.serial == 0);
    clip_unref(&s, 0);

    /* A content written and committed; read whole. */
    a = content("hello", 2);
    CHECK(a && a->n == 3 && a->refs == 1 && a->serial == 0 && live == 4);
    clip_commit(&s, a);
    CHECK(s.now == a && s.serial == 1 && a->serial == 1 && a->refs == 1);
    it = clip_find(s.now, 1);
    CHECK(it && it->len == 5 && !memcmp(it->data, "hello", 5));
    CHECK(clip_find(s.now, 3)->len == 101 && clip_find(s.now, 9) == 0);
    CHECK(clip_add(&s, a, 5, 10) == 0);                 /* committed: no longer written to */

    /* A reader keeps the content it began with, whatever is put meanwhile. */
    r1 = clip_ref(&s);
    CHECK(r1 == a && a->refs == 2);
    b = content("world!", 0);
    clip_commit(&s, b);
    CHECK(s.now == b && s.serial == 2 && a->refs == 1 && live == 6);
    CHECK(!memcmp(clip_find(r1, 1)->data, "hello", 5));
    r2 = clip_ref(&s);
    CHECK(r2 == b && !memcmp(clip_find(r2, 1)->data, "world!", 6));
    clip_unref(&s, r1);                                 /* the old content goes with its last reader */
    CHECK(live == 2);
    clip_unref(&s, r2);
    CHECK(b->refs == 1 && live == 2);

    /* A format put twice in one content: the later data. As many formats as there is room for, and no more. */
    a = clip_new(&s);
    memcpy(clip_add(&s, a, 1, 3), "one", 3);
    memcpy(clip_add(&s, a, 1, 5), "three", 5);
    CHECK(a->n == 1 && clip_find(a, 1)->len == 5 && !memcmp(clip_find(a, 1)->data, "three", 5) && live == 4);
    for (i = 2; i <= CLIP_ITEMS; i++)
        CHECK(clip_add(&s, a, i, 0) != 0);              /* (empty data is data) */
    CHECK(a->n == CLIP_ITEMS && clip_add(&s, a, 99, 1) == 0 && clip_add(&s, a, 0, 1) == 0);
    CHECK(clip_add(&s, a, 4, 7) != 0 && a->n == CLIP_ITEMS && clip_find(a, 4)->len == 7);
    refuse = 1;                                         /* no memory: as it was */
    CHECK(clip_add(&s, a, 4, 9) == 0 && clip_find(a, 4)->len == 7);
    clip_unref(&s, a);                                  /* never committed: gone */
    CHECK(live == 2 && s.now == b);
    refuse = 1;
    CHECK(clip_new(&s) == 0);

    /* A kept reference (a drag's data, a transaction's): each unref takes one. */
    a = content("drag", 1);
    CHECK(clip_keep(a) == a && a->refs == 2 && clip_keep(0) == 0);
    clip_unref(&s, a);
    CHECK(a->refs == 1 && live == 5);
    clip_unref(&s, a);
    CHECK(live == 2);

    /* Emptied. */
    clip_commit(&s, 0);
    CHECK(s.now == 0 && s.serial == 3 && live == 0 && bytes == 0 && clip_ref(&s) == 0);

    /* Private formats: a number a name, the same again; bad names; no more than there is room for. */
    f = clip_format(&s, "glos.gui.widgets");
    g = clip_format(&s, "glos.ide.form");
    CHECK(f == CLIP_PRIVATE && g == CLIP_PRIVATE + 1 && clip_format(&s, "glos.gui.widgets") == f);
    CHECK(!strcmp(clip_format_name(&s, f), "glos.gui.widgets") && !strcmp(clip_format_name(&s, g), "glos.ide.form"));
    CHECK(clip_format_name(&s, 1) == 0 && clip_format_name(&s, g + 1) == 0);
    CHECK(clip_format(&s, "") == 0 && clip_format(&s, "two words") == 0 && clip_format(&s, "glos.gui") != f);
    memset(name, 'n', sizeof name);
    name[CLIP_NAME_MAX - 1] = 0;
    CHECK(clip_format(&s, name) != 0);                  /* the longest */
    name[CLIP_NAME_MAX - 1] = 'n';
    name[CLIP_NAME_MAX] = 0;
    CHECK(clip_format(&s, name) == 0);
    for (i = s.nnames; i < CLIP_NAMES; i++) {
        snprintf(name, sizeof name, "fmt.%u", i);
        CHECK(clip_format(&s, name) == CLIP_PRIVATE + i);
    }
    CHECK(clip_format(&s, "one.more") == 0 && clip_format(&s, "fmt.40") == CLIP_PRIVATE + 40);

    printf("clip_test: %d failures\n", fails);
    return fails != 0;
}
