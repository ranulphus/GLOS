/* sdk/gui/piece.c on the host: random insertions and deletions against a plain copy of the text, each step checked
 * (length, lines, every line's start, lines of random bytes, bytes read), and the edge cases by name. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../sdk/gui/piece.h"

static int fails;
static unsigned seed = 12345;

static unsigned rnd(unsigned n) { seed = seed * 1103515245u + 12345u; return (seed >> 8) % (n ? n : 1); }

#define CHECK(c, ...) do { if (!(c)) { printf("piece_test: FAIL %s: ", #c); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

static size_t ref_lines(const char *s, size_t n)
{
    size_t k = 0, i;
    for (i = 0; i < n; i++)
        k += s[i] == '\n';
    return k + 1;
}

static int same(struct ptable *t, const char *ref, size_t n, int step)
{
    char *s = pt_text(t), buf[64];
    size_t line, i;
    int ok = s && pt_length(t) == n && !memcmp(s, ref, n) && pt_lines(t) == ref_lines(ref, n);
    CHECK(ok, "step %d: text or count differs (length %u for %u, lines %u for %u)", step, (unsigned)pt_length(t),
          (unsigned)n, (unsigned)pt_lines(t), (unsigned)ref_lines(ref, n));
    free(s);
    if (!ok)
        return 0;
    {
        static size_t starts[30000];            /* the copy's line starts, in one pass */
        size_t nl = 1, j;
        starts[0] = 0;
        for (j = 0; j < n && nl < 30000; j++)
            if (ref[j] == '\n')
                starts[nl++] = j + 1;
        for (line = 0; line <= nl; line++) {
            size_t want = line < nl ? starts[line] : n;
            if (step % 100 && rnd(16))          /* (every line every hundredth step; else one in 16 of them) */
                continue;
            if (pt_line_start(t, line) != want) {
                CHECK(0, "step %d: line %u starts at %u, not %u", step, (unsigned)line, (unsigned)pt_line_start(t, line),
                      (unsigned)want);
                return 0;
            }
        }
    }
    for (i = 0; i < 8 && n; i++) {
        size_t at = rnd((unsigned)n), k = 0, j, len = rnd(64);
        for (j = 0; j < at; j++)
            k += ref[j] == '\n';
        if (pt_line_of(t, at) != k || pt_byte(t, at) != (unsigned char)ref[at]) {
            CHECK(0, "step %d: at %u line %u for %u, byte %d for %d", step, (unsigned)at, (unsigned)pt_line_of(t, at),
                  (unsigned)k, pt_byte(t, at), (unsigned char)ref[at]);
            return 0;
        }
        if (pt_read(t, at, buf, len) != (at + len > n ? n - at : len) || memcmp(buf, ref + at, at + len > n ? n - at : len)) {
            CHECK(0, "step %d: reading %u bytes at %u", step, (unsigned)len, (unsigned)at);
            return 0;
        }
    }
    return 1;
}

int main(void)
{
    static char ref[200000];
    struct ptable t;
    size_t n;
    char *init = malloc(64);
    int step;
    /* edge cases */
    pt_init(&t, NULL, 0);
    CHECK(pt_length(&t) == 0 && pt_lines(&t) == 1 && pt_line_start(&t, 1) == 0 && pt_byte(&t, 0) == -1, "empty");
    pt_insert(&t, 0, "abc", 3);
    pt_insert(&t, 3, "def", 3);                 /* (typing on: one piece) */
    CHECK(t.n == 1, "typing on makes %d pieces", t.n);
    pt_insert(&t, 0, "\n", 1);
    pt_delete(&t, 2, 3);
    {
        char *s = pt_text(&t);
        CHECK(s && !strcmp(s, "\naef"), "edges: \"%s\"", s ? s : "(none)");
        free(s);
    }
    pt_delete(&t, 0, 100);
    CHECK(pt_length(&t) == 0 && pt_lines(&t) == 1, "all deleted");
    pt_free(&t);
    /* random */
    strcpy(init, "first line\nsecond line\nthird\n");
    n = strlen(init);
    memcpy(ref, init, n);
    pt_init(&t, init, n);
    for (step = 0; step < 10000 && !fails; step++) {
        unsigned op = rnd(10);
        if (op < 7 && n < 20000) {              /* an insertion (the text kept to 20 KB): letters, a line's end now
                                                   and then */
            char text[40];
            size_t k = 1 + rnd(30), i, at = rnd((unsigned)n + 1);
            if (op == 0 && step > 0)
                at = n;                         /* (typing on at the end) */
            for (i = 0; i < k; i++)
                text[i] = rnd(6) == 0 ? '\n' : (char)('a' + rnd(26));
            pt_insert(&t, at, text, k);
            memmove(ref + at + k, ref + at, n - at);
            memcpy(ref + at, text, k);
            n += k;
        } else if (n) {                         /* a deletion, now and then a long one */
            size_t at = rnd((unsigned)n), k = 1 + rnd(op == 9 ? 60 : 12);
            if (k > n - at)
                k = n - at;
            pt_delete(&t, at, k);
            memmove(ref + at, ref + at + k, n - at - k);
            n -= k;
        }
        same(&t, ref, n, step);
    }
    printf("piece_test: %d failures (%d steps, %d pieces, %u bytes)\n", fails, step, t.n, (unsigned)n);
    pt_free(&t);
    return fails != 0;
}
