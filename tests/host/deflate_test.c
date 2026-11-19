/* Host test for the packer (apps/shared/deflate.c): the inputs tests/host/deflate_cases.py makes (text, noise,
 * runs, a program's kind of bytes, lengths round the window's edges), each packed at both levels and
 *   - given whole, 4096 bytes at a time, 7 at a time and (the short ones) a byte at a time: the same packed
 *     bytes come out however it is given;
 *   - unpacked again by the loader's inflate (loader/inflate.c): the input;
 *   - written as NAME.L1 and NAME.L2, which deflate_cases.py's check gives to zlib.
 * Then: two streams packed in turns (each has its state), and an output that says stop.
 *
 *     deflate_test DIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/deflate.h"
#include "../../loader/inflate.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

struct buf {
    unsigned char *p;
    unsigned long n, room;
    unsigned long stop_at;              /* put says stop once this many bytes have come (0: never) */
};

static int put(void *ctx, const void *p, unsigned n)
{
    struct buf *b = ctx;
    if (b->stop_at && b->n + n >= b->stop_at)
        return 7;
    if (b->n + n > b->room) {
        b->room = (b->n + n) * 2 + 4096;
        b->p = realloc(b->p, b->room);
    }
    memcpy(b->p + b->n, p, n);
    b->n += n;
    return 0;
}

static int pack(const unsigned char *in, unsigned long n, int level, unsigned long chunk, struct buf *out)
{
    struct defl *d = malloc(deflate_state_size());
    unsigned long at;
    int r = 0;
    memset(d, 0xA5, deflate_state_size());      /* (what is in it does not matter) */
    out->n = 0;
    deflate_begin(d, level, put, out);
    for (at = 0; at < n && !r; at += chunk)
        r = deflate_data(d, in + at, n - at < chunk ? n - at : chunk);
    if (!r)
        r = deflate_end(d);
    free(d);
    return r;
}

/* ---- unpacked again */
static struct {
    const unsigned char *in;
    unsigned long in_n, in_at;
    unsigned char *out;
    unsigned long out_n, out_room;
    unsigned char win[INFL_WINDOW];
} U;

static unsigned u_fill(const unsigned char **p, void *ctx)
{
    unsigned n = U.in_n - U.in_at > 1000 ? 1000 : (unsigned)(U.in_n - U.in_at);
    (void)ctx;
    *p = U.in + U.in_at;
    U.in_at += n;
    return n;
}

static int u_flush(unsigned n, void *ctx)
{
    (void)ctx;
    if (U.out_n + n > U.out_room)
        return 1;
    memcpy(U.out + U.out_n, U.win, n);
    U.out_n += n;
    return 0;
}

static int unpack(const struct buf *b, unsigned char *out, unsigned long room)
{
    U.in = b->p, U.in_n = b->n, U.in_at = 0;
    U.out = out, U.out_n = 0, U.out_room = room;
    return inflate_stream(u_fill, u_flush, NULL, U.win);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[600], name[100];
    FILE *list, *f;
    unsigned inputs = 0;
    snprintf(path, sizeof path, "%s/LIST", dir);
    if (!(list = fopen(path, "r"))) {
        printf("deflate_test: no %s\n", path);
        return 1;
    }
    while (fgets(name, sizeof name, list)) {
        unsigned char *in, *back;
        unsigned long n;
        int level;
        name[strcspn(name, "\n")] = 0;
        if (!name[0])
            continue;
        snprintf(path, sizeof path, "%s/%s", dir, name);
        if (!(f = fopen(path, "rb"))) {
            CHECK(0, "no %s", path);
            continue;
        }
        fseek(f, 0, SEEK_END);
        n = (unsigned long)ftell(f);
        rewind(f);
        in = malloc(n + 1), back = malloc(n + 1);
        CHECK(fread(in, 1, n, f) == n, "%s read", name);
        fclose(f);
        inputs++;
        for (level = 1; level <= 2; level++) {
            static const unsigned long chunks[] = { 4096, 7, 1, 32768, 65536, 100000 };
            struct buf whole = { 0, 0, 0, 0 }, part = { 0, 0, 0, 0 };
            unsigned i;
            int r = pack(in, n, level, n ? n : 1, &whole);
            CHECK(r == 0, "%s level %d: packing said %d", name, level, r);
            r = unpack(&whole, back, n);
            CHECK(r == 0 && U.out_n == n && !memcmp(back, in, n), "%s level %d: unpacked %lu bytes of %lu (%d)", name, level,
                  U.out_n, n, r);
            CHECK(U.in_at == whole.n, "%s level %d: %lu packed bytes, %lu of them the stream", name, level, whole.n, U.in_at);
            for (i = 0; i < sizeof chunks / sizeof chunks[0]; i++) {
                if ((chunks[i] == 7 && n > 150000) || (chunks[i] == 1 && n > 40000))
                    continue;
                r = pack(in, n, level, chunks[i], &part);
                CHECK(r == 0 && part.n == whole.n && !memcmp(part.p, whole.p, whole.n),
                      "%s level %d: given %lu at a time it packs to %lu bytes, whole to %lu", name, level, chunks[i], part.n,
                      whole.n);
            }
            snprintf(path, sizeof path, "%s/%s.L%d", dir, name, level);
            f = fopen(path, "wb");
            CHECK(f && fwrite(whole.p, 1, whole.n, f) == whole.n, "%s not written", path);
            if (f)
                fclose(f);
            /* An output that says stop: that is what comes back, and nothing more is given to it. */
            if (whole.n > 9000) {
                struct buf cut = { 0, 0, 0, 0 };
                cut.stop_at = whole.n / 2;
                r = pack(in, n, level, 5000, &cut);
                CHECK(r == 7 && cut.n < cut.stop_at, "%s level %d: a stopped output: %d, %lu bytes", name, level, r, cut.n);
                free(cut.p);
            }
            free(whole.p);
            free(part.p);
        }
        free(in);
        free(back);
    }
    fclose(list);
    CHECK(inputs >= 20, "only %u inputs", inputs);

    /* Two streams in turns. */
    {
        static unsigned char a[90000], b[90000], back[90000];
        struct defl *da = malloc(deflate_state_size()), *db = malloc(deflate_state_size());
        struct buf oa = { 0, 0, 0, 0 }, ob = { 0, 0, 0, 0 };
        unsigned long i, seed = 12345;
        for (i = 0; i < sizeof a; i++) {
            seed = seed * 1103515245ul + 12345ul;
            a[i] = (unsigned char)("abcdefgh"[(seed >> 16) & 7]);
            b[i] = (unsigned char)(i / 300);
        }
        deflate_begin(da, 2, put, &oa);
        deflate_begin(db, 1, put, &ob);
        for (i = 0; i < sizeof a; i += 3000) {
            deflate_data(da, a + i, 3000);
            deflate_data(db, b + i, 3000);
        }
        CHECK(deflate_end(da) == 0 && deflate_end(db) == 0, "two in turns");
        CHECK(unpack(&oa, back, sizeof back) == 0 && U.out_n == sizeof a && !memcmp(back, a, sizeof a), "... the first");
        CHECK(unpack(&ob, back, sizeof back) == 0 && U.out_n == sizeof b && !memcmp(back, b, sizeof b), "... the second");
        free(da), free(db), free(oa.p), free(ob.p);
    }

    printf("deflate_test: %d failures (%u inputs, %d checks; the state is %u bytes)\n", fails, inputs, checks,
           deflate_state_size());
    return fails != 0;
}
