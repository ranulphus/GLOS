/* Host test for the ZIPs written (apps/shared/arc.c: arcw_*, arc_zip_remove), in the folder of archives that
 * tests/host/arc_cases.py made:
 *   - a new ZIP of folders and files (text, noise, zeros, an empty one), given in odd pieces;
 *   - more entries added to it, to a ZIP Python made, to one with a comment and to one behind a program;
 *   - an addition given up, and one that fails for want of room: the archive is, byte for byte, as it was;
 *     a new archive given up or failed is not left behind;
 *   - entries taken out again (their packed bytes copied as they are, sizes after the bytes moved into the
 *     headers), and nothing taken out: the archive untouched.
 * Each result is read back here (apps/shared/arc.c's reader), and listed in DIR/WLIST for arcw_check.py, which
 * reads them with Python's zipfile, which wrote none of this.
 *
 *     arcw_test DIR */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>

#include "../../apps/shared/arc.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

static const char *dir;
static FILE *wlist;
static unsigned long seed = 777;
#define RND() (seed = seed * 1103515245ul + 12345ul, (seed >> 8) & 0xFFFFFFul)

static char *in_dir(const char *name)
{
    static char path[8][600];
    static int k;
    k = (k + 1) % 8;
    snprintf(path[k], sizeof path[k], "%s/%s", dir, name);
    return path[k];
}

static unsigned char *slurp(const char *path, unsigned long *n)
{
    FILE *f = fopen(path, "rb");
    unsigned char *p;
    long size;
    *n = 0;
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    rewind(f);
    p = malloc((size_t)size + 1);
    *n = (unsigned long)fread(p, 1, (size_t)size, f);
    fclose(f);
    return p;
}

static void copy(const char *from, const char *to)
{
    unsigned long n;
    unsigned char *p = slurp(in_dir(from), &n);
    FILE *f = fopen(in_dir(to), "wb");
    CHECK(p && f && fwrite(p, 1, n, f) == n, "%s copied to %s", from, to);
    if (f)
        fclose(f);
    free(p);
}

static int same_file(const char *a, const char *b)
{
    unsigned long na, nb;
    unsigned char *pa = slurp(in_dir(a), &na), *pb = slurp(in_dir(b), &nb);
    int r = pa && pb && na == nb && !memcmp(pa, pb, na);
    free(pa), free(pb);
    return r;
}

/* ---- the files put in: made by rule, so that what comes out can be made again and compared */
enum { TEXT, NOISE, ZEROS, MIXED };

static unsigned char *make(int kind, unsigned long n, unsigned long s)
{
    static const char *const words[] = { "archive ", "folder ", "the ", "file ", "GLOS ", "written\r\n", "and ", "a " };
    unsigned char *p = malloc(n + 16);
    unsigned long i = 0, keep = seed;
    seed = s;
    while (i < n) {
        int k = kind == MIXED ? (int)((i / 20000) % 3) : kind;
        if (k == TEXT) {
            const char *w = words[RND() % 8];
            size_t l = strlen(w);
            memcpy(p + i, w, l);
            i += l;
        } else {
            p[i++] = (unsigned char)(k == NOISE ? RND() : 0);
        }
    }
    seed = keep;
    return p;
}

static struct put {
    char name[80];
    int kind;
    unsigned long n, s;
    int folder;
} puts_[64];
static unsigned nputs;

static int add(struct arcw *w, const char *name, int kind, unsigned long n, unsigned long s)
{
    unsigned char *p = make(kind, n, s);
    unsigned long at = 0;
    int r = arcw_begin(w, name, (46u << 9) | (11u << 5) | 19u, (22u << 11) | (15u << 5));
    while (!r && at < n) {
        unsigned long k = 1 + RND() % 9000;
        if (k > n - at)
            k = n - at;
        r = arcw_data(w, p + at, (unsigned)k);
        at += k;
    }
    if (!r)
        r = arcw_end(w);
    free(p);
    if (!r) {
        struct put *q = &puts_[nputs++];
        snprintf(q->name, sizeof q->name, "%s", name);
        q->kind = kind, q->n = n, q->s = s, q->folder = 0;
    }
    return r;
}

static int add_folder(struct arcw *w, const char *name)
{
    int r = arcw_folder(w, name, (46u << 9) | (11u << 5) | 19u, 0);
    if (!r) {
        struct put *q = &puts_[nputs++];
        snprintf(q->name, sizeof q->name, "%s", name);
        q->folder = 1, q->n = 0;
    }
    return r;
}

struct got {
    unsigned long crc, n;
};

static int take(void *ctx, const void *p, unsigned n)
{
    struct got *g = ctx;
    g->crc = arc_crc32(g->crc, p, n);
    g->n += n;
    return 0;
}

/* The archive read back: every entry unpacks, the ones put in (puts_[from...]) are there with their bytes, and
   it has `count` entries in all. Its entries listed for the Python check. */
static void verify(const char *file, unsigned from, unsigned count)
{
    int err;
    struct arc *a = arc_open(in_dir(file), &err);
    unsigned i;
    CHECK(a != NULL, "%s: not opened: %s", file, arc_error(err));
    if (!a)
        return;
    CHECK(arc_count(a) == count, "%s: %u entries, not %u", file, arc_count(a), count);
    fprintf(wlist, "A\t%s\n", file);
    for (i = 0; i < arc_count(a); i++) {
        const struct arc_entry *e = arc_at(a, i);
        struct got g = { 0, 0 };
        int r = arc_extract(a, i, take, &g);
        CHECK(r == 0 && (e->dir || g.n == e->size), "%s %s: unpacking gave %d, %lu bytes of %lu", file, e->name, r, g.n, e->size);
        fprintf(wlist, "E\t%s\t%lu\t%08lx\t%s\n", e->name, e->dir ? 0 : e->size, g.crc, e->dir ? "d" : "f");
    }
    for (i = from; i < nputs; i++) {
        const struct put *q = &puts_[i];
        int k = arc_find(a, q->name);
        CHECK(k >= 0, "%s: no %s", file, q->name);
        if (k >= 0) {
            const struct arc_entry *e = arc_at(a, (unsigned)k);
            unsigned char *p = make(q->kind, q->n, q->s);
            struct got g = { 0, 0 };
            arc_extract(a, (unsigned)k, take, &g);
            CHECK(e->dir == q->folder && (q->folder || (e->size == q->n && g.crc == arc_crc32(0, p, (unsigned)q->n))),
                  "%s %s: not what was put in", file, q->name);
            CHECK(e->date == ((46u << 9) | (11u << 5) | 19u), "%s %s: its date", file, q->name);
            free(p);
        }
    }
    arc_close(a);
}

static int drop_some(void *ctx, const char *name)
{
    (void)ctx;
    return !strcmp(name, "DIR/BIG.BIN") || !strncmp(name, "DIR/SUB", 7) || !strcmp(name, "TWO.TXT");
}

static int drop_last(void *ctx, const char *name) { (void)ctx; return !strcmp(name, "LAST.BIN"); }
static int drop_none(void *ctx, const char *name) { (void)ctx, (void)name; return 0; }
static int drop_all(void *ctx, const char *name) { (void)ctx, (void)name; return 1; }

int main(int argc, char **argv)
{
    struct arcw *w;
    struct rlimit lim, was;
    unsigned long n;
    unsigned char *p, *q;
    int err, r;
    FILE *f;

    dir = argc > 1 ? argv[1] : ".";
    if (!(wlist = fopen(in_dir("WLIST"), "w"))) {
        printf("arcw_test: %s cannot be written\n", in_dir("WLIST"));
        return 1;
    }

    /* A new one. */
    w = arcw_open(in_dir("w-new.zip"), 0, 2, &err);
    CHECK(w != NULL, "w-new.zip: %s", arc_error(err));
    if (!w)
        return 1;
    r = add_folder(w, "DIR");
    r |= add(w, "HELLO.TXT", TEXT, 900, 1);
    r |= add_folder(w, "DIR/SUB");
    r |= add(w, "DIR/BIG.BIN", MIXED, 300000, 2);
    r |= add(w, "DIR/SUB/EMPTY", TEXT, 0, 3);
    r |= add(w, "ZEROS", ZEROS, 100000, 4);
    r |= add(w, "NOISE", NOISE, 50000, 5);
    CHECK(r == 0, "w-new.zip: its entries: %d", r);
    r = arcw_close(w, 1);
    CHECK(r == 0, "w-new.zip: closed: %s", arc_error(r));
    verify("w-new.zip", 0, 7);

    /* More for it (at the quick level); then again. */
    w = arcw_open(in_dir("w-new.zip"), 1, 1, &err);
    CHECK(w != NULL, "w-new.zip to add to: %s", arc_error(err));
    r = w ? add(w, "MORE/LATER.TXT", TEXT, 70000, 6) | add(w, "TWO.TXT", TEXT, 12, 7) : -1;
    CHECK(r == 0 && arcw_close(w, 1) == 0, "w-new.zip added to: %d", r);
    verify("w-new.zip", 0, 9);
    w = arcw_open(in_dir("w-new.zip"), 1, 2, &err);
    r = w ? add(w, "THIRD.BIN", NOISE, 3000, 8) : -1;
    CHECK(r == 0 && arcw_close(w, 1) == 0, "w-new.zip added to again: %d", r);
    verify("w-new.zip", 0, 10);

    /* Added to one that is not there: a new one. An empty one: read as an archive of nothing. */
    remove(in_dir("w-fresh.zip"));
    w = arcw_open(in_dir("w-fresh.zip"), 1, 2, &err);
    nputs = 0;
    r = w ? add(w, "ONLY.TXT", TEXT, 5000, 9) : -1;
    CHECK(r == 0 && arcw_close(w, 1) == 0, "w-fresh.zip: %d", r);
    verify("w-fresh.zip", 0, 1);
    w = arcw_open(in_dir("w-empty.zip"), 0, 2, &err);
    CHECK(w && arcw_close(w, 1) == 0, "w-empty.zip");
    nputs = 0;
    verify("w-empty.zip", 0, 0);

    /* Python's, one with a comment, one behind a program, one with sizes after the bytes: added to. */
    {
        static const struct { const char *from, *to; unsigned had; } olds[] = {
            { "plain.zip", "w-plain.zip", 10 }, { "comment.zip", "w-comment.zip", 8 }, { "sfx.zip", "w-sfx.zip", 10 },
            { "descr.zip", "w-descr.zip", 3 }, { "evil.zip", "w-evil.zip", 5 } };
        unsigned i;
        for (i = 0; i < sizeof olds / sizeof olds[0]; i++) {
            copy(olds[i].from, olds[i].to);
            nputs = 0;
            w = arcw_open(in_dir(olds[i].to), 1, 2, &err);
            CHECK(w != NULL, "%s to add to: %s", olds[i].to, arc_error(err));
            r = w ? add(w, "ADDED/NEW.TXT", TEXT, 40000, 10 + i) | add_folder(w, "ADDED/DIR") | add(w, "LAST.BIN", MIXED, 90000, 20 + i)
                  : -1;
            CHECK(r == 0 && arcw_close(w, 1) == 0, "%s added to: %d", olds[i].to, r);
            verify(olds[i].to, 0, olds[i].had + 3);
        }
        p = slurp(in_dir("w-comment.zip"), &n);
        CHECK(p && n > 320 && !memcmp(p + n - 320, "A comment at the archive's end. ", 32), "the comment is kept");
        free(p);
        p = slurp(in_dir("w-sfx.zip"), &n);
        q = slurp(in_dir("sfx.zip"), &n);
        CHECK(p && q && !memcmp(p, q, 5000), "the program before the archive is kept");
        free(p), free(q);
    }

    /* Given up: an addition (the archive as it was, to the byte), and a new one (not left behind). */
    copy("plain.zip", "w-gaveup.zip");
    w = arcw_open(in_dir("w-gaveup.zip"), 1, 2, &err);
    nputs = 0;
    r = w ? add(w, "A.TXT", TEXT, 200000, 30) : -1;
    if (w) {
        arcw_begin(w, "HALF.BIN", 0, 0);
        p = make(NOISE, 60000, 31);
        arcw_data(w, p, 60000);
        free(p);
    }
    CHECK(r == 0 && arcw_close(w, 0) == ARC_E_STOP, "an addition given up");
    CHECK(same_file("plain.zip", "w-gaveup.zip"), "... the archive is as it was");
    w = arcw_open(in_dir("w-never.zip"), 0, 2, &err);
    r = w ? add(w, "A.TXT", TEXT, 20000, 32) : -1;
    CHECK(r == 0 && arcw_close(w, 0) == ARC_E_STOP, "a new one given up");
    CHECK(!(f = fopen(in_dir("w-never.zip"), "rb")), "... is not left behind");
    if (f)
        fclose(f);
    /* An entry not ended, names that cannot be: the archive is not kept. */
    w = arcw_open(in_dir("w-never.zip"), 0, 2, &err);
    r = w ? add(w, "A.TXT", TEXT, 2000, 33) : -1;
    if (w)
        arcw_begin(w, "OPEN.TXT", 0, 0);
    CHECK(r == 0 && arcw_close(w, 1) != 0, "closed with an entry still open");
    w = arcw_open(in_dir("w-never.zip"), 0, 2, &err);
    CHECK(w && arcw_begin(w, "", 0, 0) != 0 && arcw_data(w, "x", 1) != 0 && arcw_close(w, 1) != 0, "an entry with no name");
    w = arcw_open(in_dir("w-never.zip"), 0, 2, &err);
    CHECK(w && arcw_begin(w, "/ABS.TXT", 0, 0) != 0 && arcw_close(w, 1) != 0, "a name from the root");
    CHECK(!(f = fopen(in_dir("w-never.zip"), "rb")), "... none of them left behind");
    if (f)
        fclose(f);
    /* What is no ZIP is not added to (and is as it was). */
    copy("plain.tar", "w-not.zip");
    CHECK(!arcw_open(in_dir("w-not.zip"), 1, 2, &err) && err == ARC_E_KIND && same_file("plain.tar", "w-not.zip"),
          "a TAR added to as a ZIP: %d", err);
    copy("baddir.zip", "w-bad.zip");
    CHECK(!arcw_open(in_dir("w-bad.zip"), 1, 2, &err) && err == ARC_E_DAMAGED && same_file("baddir.zip", "w-bad.zip"),
          "a ZIP with a bad directory added to: %d", err);

    /* No room: files may grow to a size and no further. An addition that fails leaves the archive as it was;
       a new archive that fails is not left behind. */
    signal(SIGXFSZ, SIG_IGN);
    getrlimit(RLIMIT_FSIZE, &was);
    copy("plain.zip", "w-full.zip");
    p = slurp(in_dir("plain.zip"), &n);
    free(p);
    lim = was;
    lim.rlim_cur = n + 30000;
    setrlimit(RLIMIT_FSIZE, &lim);
    w = arcw_open(in_dir("w-full.zip"), 1, 2, &err);
    nputs = 0;
    r = w ? add(w, "FITS.TXT", TEXT, 20000, 40) : -1;
    CHECK(r == 0, "what fits: %d", r);
    r = w ? add(w, "TOOBIG.BIN", NOISE, 200000, 41) : 0;
    CHECK(r == ARC_E_OPEN, "what does not fit: %d", r);
    r = w ? add(w, "AFTER.TXT", TEXT, 10, 42) : 0;
    CHECK(r == ARC_E_OPEN, "... and nothing after it: %d", r);
    CHECK(arcw_close(w, 1) == ARC_E_OPEN, "... and the close says so");
    CHECK(same_file("plain.zip", "w-full.zip"), "... and the archive is as it was");
    lim.rlim_cur = 50000;
    setrlimit(RLIMIT_FSIZE, &lim);
    w = arcw_open(in_dir("w-never.zip"), 0, 2, &err);
    r = w ? add(w, "TOOBIG.BIN", NOISE, 200000, 43) : 0;
    CHECK(r == ARC_E_OPEN && arcw_close(w, 1) == ARC_E_OPEN, "a new archive with no room: %d", r);
    CHECK(!(f = fopen(in_dir("w-never.zip"), "rb")), "... is not left behind");
    if (f)
        fclose(f);
    setrlimit(RLIMIT_FSIZE, &was);

    /* Taken out. */
    copy("w-new.zip", "w-less.zip");
    r = arc_zip_remove(in_dir("w-less.zip"), drop_some, NULL);
    CHECK(r == 4, "four taken out of w-less.zip: %d", r);
    nputs = 0;
    verify("w-less.zip", 0, 6);
    {
        struct arc *a = arc_open(in_dir("w-less.zip"), &err);
        CHECK(a && arc_find(a, "DIR/BIG.BIN") < 0 && arc_find(a, "DIR/SUB") < 0 && arc_find(a, "DIR/SUB/EMPTY") < 0
              && arc_find(a, "TWO.TXT") < 0 && arc_find(a, "DIR") >= 0 && arc_find(a, "NOISE") >= 0, "... the right four");
        arc_close(a);
    }
    copy("w-less.zip", "w-same.zip");
    CHECK(arc_zip_remove(in_dir("w-same.zip"), drop_none, NULL) == 0 && same_file("w-less.zip", "w-same.zip"),
          "nothing taken out: untouched");
    CHECK(!(f = fopen(in_dir("w-same.$$$"), "rb")), "... and nothing left beside it");
    if (f)
        fclose(f);
    copy("w-sfx.zip", "w-sfxless.zip");
    r = arc_zip_remove(in_dir("w-sfxless.zip"), drop_last, NULL);
    CHECK(r == 1, "one out of the archive behind a program: %d", r);
    verify("w-sfxless.zip", 0, 12);
    p = slurp(in_dir("w-sfxless.zip"), &n);
    q = slurp(in_dir("sfx.zip"), &n);
    CHECK(p && q && !memcmp(p, q, 5000), "... the program still before it");
    free(p), free(q);
    copy("w-descr.zip", "w-descrless.zip");
    r = arc_zip_remove(in_dir("w-descrless.zip"), drop_some, NULL);
    CHECK(r == 1, "TWO.TXT out of the one with sizes after the bytes: %d", r);
    verify("w-descrless.zip", 0, 5);
    copy("w-new.zip", "w-none.zip");
    r = arc_zip_remove(in_dir("w-none.zip"), drop_all, NULL);
    CHECK(r == 10, "all taken out: %d", r);
    verify("w-none.zip", 0, 0);
    CHECK(arc_zip_remove(in_dir("plain.tar"), drop_all, NULL) == ARC_E_KIND, "a TAR's entries taken out as a ZIP's");
    CHECK(arc_zip_remove(in_dir("no-such.zip"), drop_all, NULL) == ARC_E_OPEN, "... and a file's that is not there");

    fclose(wlist);
    printf("arcw_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
