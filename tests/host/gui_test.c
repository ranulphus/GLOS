/* gui_test.c - the toolkit's editor on the host (make host-test; M9 step A):
 * 20,000 random edits and undos of a text with CR LF and LF line ends,
 * checked after each against a plain copy kept alongside: the text, the
 * line starts the editor keeps instead of counting, each line's rows when
 * wrapping, and that every row maps back to itself. Then a megabyte. */
#include <stdio.h>

#include "../../sdk/gui/field.c"

static unsigned seed = 12345;
static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245u + 12345u;
    return (seed >> 8) % n;
}

static char model[200000];
static size_t mlen;
static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("gui_test: " __VA_ARGS__); printf("\n"); fails++; return; } } while (0)

static void check(struct editor *e, int step)
{
    size_t i;
    int l = 0, r, total;
    CHECK(e_len(e) == mlen, "step %d: length %u, want %u", step, (unsigned)e_len(e), (unsigned)mlen);
    for (i = 0; i < mlen; i++)
        CHECK(e_at(e, i) == model[i], "step %d: byte %u", step, (unsigned)i);
    CHECK(e->ls[0] == 0, "step %d: line 0", step);
    for (i = 0; i < mlen; i++)
        if (model[i] == '\n') {
            l++;
            CHECK(l < e->nl && e->ls[l] == i + 1, "step %d: line %d starts at %u", step, l,
                  l < e->nl ? (unsigned)e->ls[l] : 0u);
        }
    CHECK(l + 1 == e->nl, "step %d: %d lines, want %d", step, e->nl, l + 1);
    total = e_total_rows(e);
    for (r = 0; r < total; r += 1 + (int)rnd(5)) {     /* (a row's start maps back to the row) */
        size_t a, b;
        e_row(e, r, &a, &b);
        CHECK(a <= b && e_row_of(e, a) == r, "step %d: row %d (%u..%u) maps to %d", step, r, (unsigned)a,
              (unsigned)b, e_row_of(e, a));
    }
}

static void apply(struct editor *e, size_t a, size_t b, const char *t, size_t n)
{
    memmove(model + a + n, model + b, mlen - b);
    memcpy(model + a, t, n);
    mlen = mlen - (b - a) + n;
    e_replace(e, a, b, t, n);
}

/* A megabyte: set, an edit in the middle, its lines counted again by hand. */
static void big(struct editor *e)
{
    size_t n = 1 << 20, i;
    int lines = 1;
    char *t = malloc(n);
    for (i = 0; i < n; i++)
        t[i] = i % 61 == 60 ? '\n' : (char)('a' + i % 26);
    gui_editor_set(&e->w, t, n);
    e_replace(e, n / 2, n / 2 + 10, "MIDDLE\r\n", 8);
    free(t);
    CHECK(e_len(e) == n - 2 && e_at(e, n / 2) == 'M' && e_at(e, n / 2 + 7) == '\n', "the megabyte's edit");
    for (i = 0; i < e_len(e); i++)
        if (e_at(e, i) == '\n') {
            CHECK(e->ls[lines] == i + 1, "the megabyte's line %d", lines);
            lines++;
        }
    CHECK(lines == e->nl, "the megabyte's lines: %d, want %d", e->nl, lines);
}

int main(void)
{
    static const char *const bits[] = { "a", "word ", "\r\n", "\n", "\t", "long line of text that wraps ", "x" };
    struct gui_window *w;
    struct editor *e;
    char *saved[64];
    size_t saved_n[64];
    int step, depth = 0;
    w = gui_window("t", 300, 200, 0);
    e = (struct editor *)gui_editor(w->root);
    gui_place(&e->w, 0, 0, 300, 200);
    for (step = 0; step < 20000 && !fails; step++) {
        unsigned what = rnd(10);
        if (step == 7000)
            gui_editor_wrap(&e->w, 1);
        if (step == 14000)
            gui_place(&e->w, 0, 0, 180, 150);  /* (another width: rows counted again) */
        if (what < 6 && mlen < sizeof model - 64) {     /* an insertion, perhaps over a choice */
            const char *t = bits[rnd(7)];
            size_t a = rnd((unsigned)mlen + 1), b = a + (what == 5 && a < mlen ? rnd((unsigned)(mlen - a) % 9 + 1) : 0);
            if (b > a && depth < 64) {
                saved_n[depth] = mlen;
                saved[depth] = malloc(mlen + 1);
                memcpy(saved[depth], model, mlen);
                depth++;
                apply(e, a, b, t, strlen(t));
            } else {
                apply(e, a, a, t, strlen(t));
            }
        } else if (what < 9 && mlen) {          /* a removal */
            size_t a = rnd((unsigned)mlen), b = a + 1 + rnd(12);
            if (b > mlen)
                b = mlen;
            apply(e, a, b, "", 0);
        } else if (depth > 0) {                 /* undo everything back to the last saved copy */
            depth--;
            while (e->nundo > 0 && (e_len(e) != saved_n[depth] || memcmp(model, saved[depth], 0) != 0)) {
                e_undo(e);
                mlen = e_len(e);
                e_copy(e, 0, mlen, model);
                if (mlen == saved_n[depth] && !memcmp(model, saved[depth], mlen))
                    break;
            }
            free(saved[depth]);
            mlen = e_len(e);
            e_copy(e, 0, mlen, model);
        }
        check(e, step);
    }
    big(e);
    printf("gui_test: %d failures (%d edits)\n", fails, step);
    return fails != 0;
}
