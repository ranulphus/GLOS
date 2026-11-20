/* Host test for the archives' reader (apps/shared/arc.c): the archives tests/host/arc_cases.py makes with
 * Python's zipfile, tarfile and gzip, those LHA's and ARJ's own tools made (tests/host/arcfix) and LZH and ARJ
 * archives written bit by bit (arcfix/pack.py), each against the script's list of what it must be read as.
 *
 *     arc_test DIR        (DIR/MANIFEST and the archives beside it)
 *
 * Every entry's name, size, kind, date and bytes (unpacked one by one, and all in one walk); every folder's
 * contents; archives that are damaged, cut short, encrypted, packed in ways not read, with names that would leave
 * their folder, and files that are no archives. Then: two archives unpacked at once (one inside the other's
 * callback: each has a state of its own), a callback that says stop, a walk that takes some entries and stops. */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../apps/shared/arc.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

#define MAXE 4096
static struct want {
    char name[1400];
    unsigned long size, crc;
    int dir, how, get;                  /* get: 0, an error, or 1 for any error */
} want[MAXE];
static unsigned nwant;

struct got {
    unsigned long crc, n;
    int stop_after;                     /* pieces before the callback says stop (0: never) */
    int pieces;
};

static int take(void *ctx, const void *p, unsigned n)
{
    struct got *g = ctx;
    g->crc = arc_crc32(g->crc, p, n);
    g->n += n;
    g->pieces++;
    return g->stop_after && g->pieces >= g->stop_after;
}

/* ---- a walk of everything */
static struct {
    struct got g[MAXE];
    int begun[MAXE], ended[MAXE], err[MAXE];
    unsigned cur;
    int every;                          /* take each every-th entry (1: all) */
    int stop_at;                        /* begin says stop at this entry (-1: never) */
} W;

static int w_begin(void *ctx, unsigned i, const struct arc_entry *e)
{
    (void)ctx, (void)e;
    if (i >= MAXE)
        return -1;
    if (W.stop_at >= 0 && (int)i == W.stop_at)
        return -1;
    W.begun[i]++;
    W.cur = i;
    return i % (unsigned)W.every == 0;
}

static int w_data(void *ctx, const void *p, unsigned n)
{
    (void)ctx;
    return take(&W.g[W.cur], p, n);
}

static void w_end(void *ctx, unsigned i, int err)
{
    (void)ctx;
    if (i < MAXE)
        W.ended[i]++, W.err[i] = err;
}

static const struct arc_sink w_sink = { w_begin, w_data, w_end };

static int walk(struct arc *a, int every, int stop_at)
{
    memset(&W, 0, sizeof W);
    W.every = every, W.stop_at = stop_at;
    return arc_walk(a, &w_sink, NULL);
}

/* ---- a folder's contents, as the list's line has them */
static char listed[70000];

static int l_each(void *ctx, const char *name, int is_dir, const struct arc_entry *e)
{
    size_t n = strlen(listed);
    (void)ctx, (void)e;
    snprintf(listed + n, sizeof listed - n, "%s%s%s", n ? "|" : "", name, is_dir ? "/" : "");
    return 0;
}

static char *field(char **p)            /* the line's next field (tabs between them) */
{
    char *s = *p, *t;
    if (!s)
        return "";
    t = strchr(s, '\t');
    if (t)
        *t = 0, *p = t + 1;
    else
        *p = NULL;
    return s;
}

static int get_of(const char *s) { return !strcmp(s, "x") ? 1 : atoi(s); }
static int same(int r, int get) { return get == 1 ? r != 0 : r == get; }

/* ---- the entries' paths on a DOS disc: each part a name DOS can have, no two entries with one path, and an
   entry under a folder that has an entry of its own is under that folder's path. */
static int by_path(const void *x, const void *y) { return strcmp(*(const char *const *)x, *(const char *const *)y); }

static void dos_checks(struct arc *a, const char *cur)
{
    unsigned n = arc_count(a), i;
    const char **paths = malloc((n ? n : 1) * sizeof *paths);
    for (i = 0; i < n; i++) {
        const char *d = arc_dos_path(a, i), *p, *slash;
        const char *name = arc_at(a, i)->name;
        int k;
        paths[i] = d ? d : "";
        CHECK(d && d[0], "%s: no DOS path for %s", cur, name);
        for (p = d; p && *p; p = *slash ? slash + 1 : slash) {
            const char *dot;
            size_t len, base;
            slash = strchr(p, '\\');
            if (!slash)
                slash = p + strlen(p);
            len = (size_t)(slash - p);
            dot = memchr(p, '.', len);
            base = dot ? (size_t)(dot - p) : len;
            CHECK(len >= 1 && len <= 12 && base >= 1 && base <= 8 && len - base <= 4 && (!dot || len - base >= 2),
                  "%s: %s has the part '%.*s'", cur, d, (int)len, p);
            for (k = 0; k < (int)len; k++)
                CHECK((p[k] >= 'A' && p[k] <= 'Z') || (p[k] >= '0' && p[k] <= '9') || (k == (int)base && p[k] == '.')
                      || (p[k] && strchr("!#$%&'()-@^_`{}~", p[k])), "%s: %s has the character %d", cur, d, p[k]);
        }
        slash = strrchr(name, '/');     /* its folder's own entry, if there is one: the path begins with that one's */
        if (slash && d) {
            char parent[1400];
            int j;
            snprintf(parent, sizeof parent, "%.*s", (int)(slash - name), name);
            j = arc_find(a, parent);
            if (j >= 0) {
                const char *pd = arc_dos_path(a, (unsigned)j);
                size_t pl = pd ? strlen(pd) : 0;
                CHECK(pd && !strncmp(d, pd, pl) && d[pl] == '\\' && !strchr(d + pl + 1, '\\'),
                      "%s: %s is at %s, its folder at %s", cur, name, d, pd ? pd : "(none)");
            }
        }
    }
    qsort(paths, n, sizeof *paths, by_path);
    for (i = 1; i < n; i++)
        CHECK(strcmp(paths[i - 1], paths[i]) != 0, "%s: two entries at %s", cur, paths[i]);
    free(paths);
}

/* ---- two at once: inside the first piece of one archive's file, another archive's file is unpacked whole */
static struct {
    struct arc *other;
    int other_i, done, r;
    struct got in, out;
} N;

static int nest(void *ctx, const void *p, unsigned n)
{
    (void)ctx;
    if (!N.done) {
        N.done = 1;
        N.r = arc_extract(N.other, (unsigned)N.other_i, take, &N.in);
    }
    return take(&N.out, p, n);
}

static const char *hung_at = "";
static const unsigned char zeros[300000];

static void hung(int sig)
{
    (void)sig;
    printf("FAIL arc_test: the reader did not come back from %s\n", hung_at);
    fflush(stdout);
    _exit(1);
}

int main(int argc, char **argv)
{
    static char line[80000], path[600], cur[200];
    struct arc *a = NULL;
    FILE *m;
    unsigned idx = 0, archives = 0, entries = 0, i;
    unsigned date = 0, tm = 0;
    int have_stamp = 0, err, r;
    const char *dir = argc > 1 ? argv[1] : ".";

    snprintf(path, sizeof path, "%s/MANIFEST", dir);
    if (!(m = fopen(path, "r"))) {
        printf("arc_test: no %s\n", path);
        return 1;
    }
    while (fgets(line, sizeof line, m)) {
        char *p = line, *what;
        line[strcspn(line, "\n")] = 0;
        what = field(&p);
        if (!strcmp(what, "A")) {
            const char *file, *kind;
            if (a) {
                CHECK(arc_count(a) == idx, "%s: %u entries, %u wanted", cur, arc_count(a), idx);
                arc_close(a);
            }
            file = field(&p), kind = field(&p);
            snprintf(cur, sizeof cur, "%s", file);
            snprintf(path, sizeof path, "%s/%s", dir, file);
            idx = nwant = 0;
            have_stamp = 0;
            archives++;
            a = arc_open(path, &err);
            if (!strcmp(kind, "!")) {
                CHECK(!a && err != ARC_OK, "%s opened, and should not", cur);
                if (a)
                    arc_close(a), a = NULL;
            } else if (strcmp(kind, "?")) {
                CHECK(a != NULL, "%s: not opened: %s", cur, arc_error(err));
                if (a)
                    CHECK(!strcmp(arc_kind(a), kind), "%s is a %s, not a %s", cur, arc_kind(a), kind);
            }
        } else if (!a) {
            continue;
        } else if (!strcmp(what, "D")) {
            date = (unsigned)atoi(field(&p)), tm = (unsigned)atoi(field(&p));
            have_stamp = 1;
        } else if (!strcmp(what, "E")) {
            struct want *w = &want[nwant < MAXE ? nwant : MAXE - 1];
            const struct arc_entry *e = arc_at(a, idx);
            struct got g;
            snprintf(w->name, sizeof w->name, "%s", field(&p));
            w->size = strtoul(field(&p), NULL, 10);
            w->crc = strtoul(field(&p), NULL, 16);
            w->dir = !strcmp(field(&p), "d");
            w->how = atoi(field(&p));
            w->get = get_of(field(&p));
            nwant++, entries++;
            CHECK(e != NULL, "%s: no entry %u (%s)", cur, idx, w->name);
            if (!e) {
                idx++;
                continue;
            }
            CHECK(!strcmp(e->name, w->name), "%s entry %u is '%s', not '%s'", cur, idx, e->name, w->name);
            CHECK(e->dir == w->dir && e->how == w->how, "%s %s: dir=%d how=%d", cur, w->name, e->dir, e->how);
            CHECK(w->dir || e->size == w->size, "%s %s: size %lu, not %lu", cur, w->name, e->size, w->size);
            if (have_stamp)
                CHECK(e->date == date && e->time == tm, "%s %s: date %u time %u, not %u %u", cur, w->name, e->date, e->time,
                      date, tm);
            CHECK(arc_find(a, w->name) == (int)idx, "%s: '%s' found at %d, not %u", cur, w->name, arc_find(a, w->name), idx);
            memset(&g, 0, sizeof g);
            r = arc_extract(a, idx, take, &g);
            CHECK(same(r, w->get), "%s %s: unpacking gave %d (%s)", cur, w->name, r, arc_error(r));
            if (!r && !w->dir)
                CHECK(g.n == w->size && g.crc == w->crc, "%s %s: %lu bytes crc %08lx, not %lu and %08lx", cur, w->name, g.n,
                      g.crc, w->size, w->crc);
            idx++;
        } else if (!strcmp(what, "P")) {
            unsigned k = (unsigned)atoi(field(&p));
            const char *wantp = field(&p), *gotp = arc_dos_path(a, k);
            CHECK(gotp && !strcmp(gotp, wantp), "%s entry %u (%s) on a DOS disc is '%s', not '%s'", cur, k,
                  arc_at(a, k) ? arc_at(a, k)->name : "?", gotp ? gotp : "(none)", wantp);
        } else if (!strcmp(what, "L")) {
            const char *d = field(&p), *kids = field(&p);
            listed[0] = 0;
            arc_list(a, d, l_each, NULL);
            CHECK(!strcmp(listed, kids), "%s: '%s' holds\n  %.300s\nnot\n  %.300s", cur, d, listed, kids);
        } else if (!strcmp(what, "W")) {
            int wantr = get_of(field(&p));
            dos_checks(a, cur);
            r = walk(a, 1, -1);
            CHECK(same(r, wantr), "%s: the walk gave %d (%s)", cur, r, arc_error(r));
            for (i = 0; i < nwant && i < MAXE && (wantr == 0 || wantr == ARC_E_CRC); i++) {
                struct want *w = &want[i];
                CHECK(W.begun[i] == 1 && W.ended[i] == 1, "%s %s: begun %d times, ended %d", cur, w->name, W.begun[i],
                      W.ended[i]);
                CHECK(same(W.err[i], w->get), "%s %s: the walk's end said %d", cur, w->name, W.err[i]);
                if (!W.err[i] && !w->dir)
                    CHECK(W.g[i].n == w->size && W.g[i].crc == w->crc, "%s %s: the walk's bytes: %lu crc %08lx", cur, w->name,
                          W.g[i].n, W.g[i].crc);
            }
            /* ... and every third entry alone: the others are neither given nor ended */
            if (wantr == 0 && nwant > 3) {
                r = walk(a, 3, -1);
                CHECK(r == 0, "%s: a walk of every third gave %d", cur, r);
                for (i = 0; i < nwant && i < MAXE; i++) {
                    struct want *w = &want[i];
                    CHECK(W.ended[i] == (i % 3 == 0) && (i % 3 == 0 || W.g[i].n == 0), "%s %s: third's walk: ended %d, %lu bytes",
                          cur, w->name, W.ended[i], W.g[i].n);
                    if (i % 3 == 0 && !w->dir && !w->get)
                        CHECK(W.g[i].crc == w->crc, "%s %s: third's walk: crc", cur, w->name);
                }
                r = walk(a, 1, 2);      /* ... and one that stops before the third */
                CHECK(r == ARC_E_STOP && W.begun[0] == 1 && W.begun[1] == 1 && W.begun[2] == 0, "%s: a stopped walk gave %d", cur,
                      r);
            }
        }
    }
    if (a) {
        CHECK(arc_count(a) == idx, "%s: %u entries, %u wanted", cur, arc_count(a), idx);
        arc_close(a);
    }
    fclose(m);
    CHECK(archives >= 30 && entries >= 3500, "the list is short: %u archives, %u entries", archives, entries);
    {
        char out[13];
        arc_dos_part("x", 1, 123456, out);
        CHECK(!strcmp(out, "X~123456"), "a part numbered high: %s", out);
        arc_dos_part("abcdefgh.xyz", 12, 1234567, out);
        CHECK(!strcmp(out, "~1234567.XYZ"), "... and higher: %s", out);
        arc_dos_part("...", 3, 0, out);
        CHECK(!strcmp(out, "_"), "a part of dots: %s", out);
    }

    /* Names. */
    CHECK(arc_name_is("A.ZIP") && arc_name_is("c:\\x\\b.tar") && arc_name_is("x.tar.gz") && arc_name_is("Y.TGZ"), "names");
    CHECK(!arc_name_is("A.ZIPX") && !arc_name_is("ZIP") && !arc_name_is("a.txt") && !arc_name_is(""), "names that are none");
    snprintf(path, sizeof path, "%s/no-such-file.zip", dir);
    CHECK(!arc_open(path, &err) && err == ARC_E_OPEN, "a file that is not there: %d", err);
    CHECK(!arc_open(path, NULL), "... with nowhere to say so");

    /* Two at once, and things asked for that are not there. */
    snprintf(path, sizeof path, "%s/plain.zip", dir);
    a = arc_open(path, &err);
    snprintf(path, sizeof path, "%s/plain.tgz", dir);
    N.other = arc_open(path, &err);
    CHECK(a && N.other, "plain.zip and plain.tgz again");
    if (a && N.other) {
        int g = arc_find(a, "DOCS/GUIDE.TXT"), z = arc_find(N.other, "big/zeros.bin");
        CHECK(g >= 0 && z >= 0, "their files");
        N.other_i = z;
        r = arc_extract(a, (unsigned)g, nest, NULL);
        CHECK(r == 0 && N.r == 0 && N.done, "one inside the other: %d and %d", r, N.r);
        CHECK(N.out.n == 70000 && N.in.n == 300000 && N.in.crc == arc_crc32(0, zeros, sizeof zeros), "... and their bytes");
        memset(&N.out, 0, sizeof N.out);
        r = arc_extract(a, (unsigned)g, take, &N.out);  /* (and the same file after it, as before) */
        CHECK(r == 0 && N.out.n == 70000, "the first again: %d", r);
        CHECK(arc_find(a, "DOCS/NONE.TXT") < 0 && arc_find(a, "") < 0 && arc_find(a, "DOCS/") < 0, "names that are not there");
        CHECK(arc_list(a, "NO/SUCH", l_each, NULL) == 0 && arc_list(a, "DOC", l_each, NULL) == 0, "folders that are not there");
        CHECK(arc_list(a, "", NULL, NULL) == 6 && arc_list(a, "DOCS", NULL, NULL) == 4, "counted alone");
        CHECK(arc_extract(a, 9999, take, &N.out) != 0 && !arc_at(a, 9999), "an entry past the last");
        {
            char d[80];
            CHECK(arc_has_folder(a, "") && arc_has_folder(a, "DOCS") && arc_has_folder(a, "DOCS/DEEP")
                  && arc_has_folder(a, "DOCS/DEEP/DOWN") && arc_has_folder(a, "big") && arc_has_folder(a, "EMPTY_DIR"),
                  "the folders it has");
            CHECK(!arc_has_folder(a, "DOC") && !arc_has_folder(a, "READ.ME") && !arc_has_folder(a, "DOCS/a")
                  && !arc_has_folder(a, "DOCS/DEEP/DOWN/A.BIN") && !arc_has_folder(a, "zzz"), "... and has not");
            CHECK(arc_dos_of(a, "DOCS/DEEP", d, sizeof d) == 0 && !strcmp(d, "DOCS\\DEEP"), "a folder's DOS path: %s", d);
            CHECK(arc_dos_of(a, "big", d, sizeof d) == 0 && !strcmp(d, "BIG"), "... one only in a name: %s", d);
            CHECK(arc_dos_of(a, "DOCS/DEEP/DOWN/A.BIN", d, sizeof d) == 0 && !strcmp(d, "DOCS\\DEEP\\DOWN\\A.BIN"),
                  "... a file's: %s", d);
            CHECK(arc_dos_of(a, "", d, sizeof d) == 0 && !d[0], "... the top's");
            CHECK(arc_dos_of(a, "nothing", d, sizeof d) != 0 && arc_dos_of(a, "DOC", d, sizeof d) != 0
                  && arc_dos_of(a, "DOCS/DEEP", d, 5) != 0, "... and none");
        }
        CHECK(arc_extract(a, (unsigned)g, NULL, NULL) == 0, "unpacked for nobody: checked all the same");
    }
    arc_close(a);
    arc_close(N.other);
    arc_close(NULL);

    /* An archive taken away after it was opened (its file is open only while it is read): said, each time. */
    {
        unsigned long n = 0;
        unsigned char *bytes;
        FILE *g;
        struct got gg;
        snprintf(path, sizeof path, "%s/plain.zip", dir);
        g = fopen(path, "rb");
        bytes = malloc(400000);
        n = g ? (unsigned long)fread(bytes, 1, 400000, g) : 0;
        if (g)
            fclose(g);
        snprintf(path, sizeof path, "%s/GONE.ZIP", dir);
        g = fopen(path, "wb");
        CHECK(g && n && fwrite(bytes, 1, n, g) == n, "GONE.ZIP written");
        if (g)
            fclose(g);
        free(bytes);
        a = arc_open(path, &err);
        CHECK(a != NULL, "GONE.ZIP opened");
        remove(path);
        if (a) {
            memset(&gg, 0, sizeof gg);
            CHECK(arc_extract(a, 0, take, &gg) == ARC_E_OPEN && gg.n == 0, "a file of an archive that has gone");
            CHECK(walk(a, 1, -1) == ARC_E_OPEN, "... and a walk of it");
            CHECK(arc_count(a) == 10 && arc_find(a, "READ.ME") == 0, "... its list is still its list");
        }
        arc_close(a);
    }

    /* A callback that says stop, in each kind (the big file: its second piece). */
    {
        static const char *const files[] = { "plain.zip", "stored.zip", "plain.tar", "plain.tgz", "single.gz" };
        for (i = 0; i < sizeof files / sizeof files[0]; i++) {
            struct got g;
            int k;
            snprintf(path, sizeof path, "%s/%s", dir, files[i]);
            a = arc_open(path, &err);
            CHECK(a != NULL, "%s again", files[i]);
            if (!a)
                continue;
            k = arc_find(a, i == 4 ? "GUIDE.TXT" : "DOCS/GUIDE.TXT");
            memset(&g, 0, sizeof g);
            g.stop_after = 2;
            r = k < 0 ? 0 : arc_extract(a, (unsigned)k, take, &g);
            CHECK(r == ARC_E_STOP && g.pieces == 2 && g.n < 70000, "%s: stopped: %d after %d pieces", files[i], r, g.pieces);
            memset(&g, 0, sizeof g);
            r = k < 0 ? -1 : arc_extract(a, (unsigned)k, take, &g);
            CHECK(r == 0 && g.n == 70000, "%s: and whole after that: %d", files[i], r);
            arc_close(a);
        }
    }

    /* Archives with bytes changed, cut short, or with a piece of another put in: whatever comes of them (most
       are refused, some give their files with errors), the reader comes back, and what it gives is no more than
       it says. Each of the list's archives, forty ways. */
    {
        unsigned long seed = 20261119ul, made = 0, opened = 0;
        static unsigned char *bytes;
        static char names[128][200];
        unsigned n = 0, round;
        snprintf(path, sizeof path, "%s/MANIFEST", dir);
        m = fopen(path, "r");
        while (m && fgets(line, sizeof line, m) && n < 128)
            if (line[0] == 'A' && line[1] == '\t') {
                char *p = line + 2;
                snprintf(names[n++], sizeof names[0], "%s", field(&p));
            }
        if (m)
            fclose(m);
        signal(SIGALRM, hung);
        for (i = 0; i < n; i++) {
            FILE *f;
            long size;
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            if (!(f = fopen(path, "rb")))
                continue;
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            rewind(f);
            if (size <= 0 || size > 400000 || !(bytes = malloc((size_t)size * 2))) {
                fclose(f);              /* (the largest are left out: they would take the time) */
                continue;
            }
            for (round = 0; round < 40; round++) {
                unsigned long len = (unsigned long)size, k, changes;
                rewind(f);
                if (fread(bytes, 1, len, f) != len)
                    break;
#define RND() (seed = seed * 1103515245ul + 12345ul, (seed >> 8) & 0xFFFFFFul)
                changes = 1 + RND() % 6;
                for (k = 0; k < changes; k++) {
                    unsigned long at = RND() % len, how = RND() % 8;
                    if (how < 4)
                        bytes[at] ^= (unsigned char)(1u << (RND() % 8));
                    else if (how < 6)
                        bytes[at] = (unsigned char)RND();
                    else if (how == 6 && len > 64)
                        len -= RND() % (len / 4);                       /* cut short */
                    else if (len > 64) {
                        unsigned long from = RND() % len, cnt = RND() % 600;    /* a piece from elsewhere */
                        if (cnt > len - at)
                            cnt = len - at;
                        if (cnt > len - from)
                            cnt = len - from;
                        memmove(bytes + at, bytes + from, cnt);
                    }
                }
                snprintf(path, sizeof path, "%s/FUZZ.TMP", dir);
                m = fopen(path, "wb");
                if (!m || fwrite(bytes, 1, len, m) != len) {
                    CHECK(0, "FUZZ.TMP not written");
                    break;
                }
                fclose(m);
                made++;
                alarm(20);
                snprintf(cur, sizeof cur, "%.150s, changed (round %u)", names[i], round);
                hung_at = cur;
                a = arc_open(path, &err);
                if (a) {
                    unsigned long total = 0;
                    unsigned j;
                    opened++;
                    walk(a, 1, -1);
                    for (j = 0; j < arc_count(a) && j < MAXE; j++) {
                        const struct arc_entry *e = arc_at(a, j);
                        CHECK(W.g[j].n <= e->size || W.err[j] != 0, "%s: entry %u gave %lu bytes of %lu and said it was well",
                              cur, j, W.g[j].n, e->size);
                        total += W.g[j].n;
                    }
                    for (j = 0; j < arc_count(a) && j < 40; j++) {
                        struct got g;
                        memset(&g, 0, sizeof g);
                        arc_extract(a, j, take, &g);
                    }
                    listed[0] = 0;
                    arc_list(a, "", l_each, NULL);
                    arc_close(a);
                }
                alarm(0);
            }
            free(bytes);
            fclose(f);
        }
        snprintf(path, sizeof path, "%s/FUZZ.TMP", dir);
        remove(path);
        CHECK(made >= 1000, "only %lu changed archives were made", made);
        printf("arc_test: %lu changed archives, %lu of them opened\n", made, opened);
    }

    printf("arc_test: %d failures (%u archives, %u entries, %d checks)\n", fails, archives, entries, checks);
    return fails != 0;
}
