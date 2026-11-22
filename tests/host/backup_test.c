/* backup_test.c - Disk Tools' backups (apps/shared/backup.c; make host-test; phase F): a folder backed up to a
 * store that is a folder here; everything, then what changed (a file grown, one new, one gone, one only re-dated):
 * only those copied, the rest held by the set before; every set complete and newest first; the newest and the
 * first restored, each the folder as it was then, dates and an empty folder too; a run stopped leaves no set; a
 * store that fails ends the run; a set with no manifest is none. Argument: a fresh folder to work in. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <utime.h>

#include "../../apps/shared/backup.h"

static int fails;
static char base[200];

#define CHECK(c, ...) do { if (!(c)) { printf("backup_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- the store: a folder here */

static void there(char *out, size_t room, const char *path) { snprintf(out, room, "%s/there/%s", base, path); }

static int copy(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b;
    char buf[4096];
    size_t n;
    if (!a)
        return -1;
    if (!(b = fopen(to, "wb"))) {
        fclose(a);
        return -1;
    }
    while ((n = fread(buf, 1, sizeof buf, a)) > 0)
        fwrite(buf, 1, n, b);
    fclose(a);
    return fclose(b) == 0 ? 0 : -1;
}

static int fail_puts;                   /* (a store that fails: puts refused after this many; -1 never) */

static int s_mkdir(void *ctx, const char *path)
{
    char p[400];
    struct stat st;
    (void)ctx;
    there(p, sizeof p, path);
    return stat(p, &st) == 0 || mkdir(p, 0777) == 0 ? 0 : -1;
}

static int s_put(void *ctx, const char *local, const char *path)
{
    char p[400];
    (void)ctx;
    if (fail_puts == 0)
        return -1;
    if (fail_puts > 0)
        fail_puts--;
    there(p, sizeof p, path);
    return copy(local, p);
}

static int s_get(void *ctx, const char *path, const char *local)
{
    char p[400];
    (void)ctx;
    there(p, sizeof p, path);
    return copy(p, local);
}

static int s_list(void *ctx, const char *dir, int (*each)(void *arg, const char *name, int dir), void *arg)
{
    char p[400], q[700];
    DIR *d;
    struct dirent *e;
    (void)ctx;
    there(p, sizeof p, dir);
    if (!(d = opendir(p)))
        return -1;
    while ((e = readdir(d)) != NULL) {
        struct stat st;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;
        snprintf(q, sizeof q, "%s/%s", p, e->d_name);
        if (stat(q, &st) == 0 && each(arg, e->d_name, S_ISDIR(st.st_mode)))
            break;
    }
    closedir(d);
    return 0;
}

static const struct bk_store store = { NULL, s_mkdir, s_put, s_get, s_list };

/* ---- the folder here */

static void make(const char *rel, const char *text, int day)   /* a file, dated 2026-11-<day> 10:20:30 */
{
    char p[400];
    FILE *f;
    struct tm tm;
    struct utimbuf u;
    snprintf(p, sizeof p, "%s/%s", base, rel);
    f = fopen(p, "wb");
    if (!f) {
        CHECK(0, "make %s", p);
        return;
    }
    fputs(text, f);
    fclose(f);
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 126, tm.tm_mon = 10, tm.tm_mday = day, tm.tm_hour = 10, tm.tm_min = 20, tm.tm_sec = 30, tm.tm_isdst = -1;
    u.actime = u.modtime = mktime(&tm);
    utime(p, &u);
}

static void dir(const char *rel)
{
    char p[400];
    snprintf(p, sizeof p, "%s/%s", base, rel);
    mkdir(p, 0777);
}

static void same(const char *a_rel, const char *b_rel, const char *step)       /* the bytes and the date */
{
    char a[400], b[400], ba[8192], bb[8192];
    FILE *fa, *fb;
    size_t na = 0, nb = 0;
    struct stat sa, sb;
    snprintf(a, sizeof a, "%s/%s", base, a_rel);
    snprintf(b, sizeof b, "%s/%s", base, b_rel);
    fa = fopen(a, "rb"), fb = fopen(b, "rb");
    if (fa)
        na = fread(ba, 1, sizeof ba, fa), fclose(fa);
    if (fb)
        nb = fread(bb, 1, sizeof bb, fb), fclose(fb);
    CHECK(fa && fb && na == nb && !memcmp(ba, bb, na), "%s: %s and %s differ", step, a_rel, b_rel);
    CHECK(stat(a, &sa) == 0 && stat(b, &sb) == 0 && sa.st_mtime == sb.st_mtime, "%s: %s's date", step, b_rel);
}

static int exists(const char *rel)
{
    char p[400];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", base, rel);
    return stat(p, &st) == 0;
}

static int stop_at;                     /* (progress: stop at the nth file; 0 never) */

static int progress(void *ctx, const char *path, unsigned long long done, unsigned long long total)
{
    (void)ctx, (void)done, (void)total;
    return path[0] && stop_at && --stop_at == 0;
}

int main(int argc, char **argv)
{
    static char big[5001];
    struct bk_job j;
    char sets[16][BK_SET], here[300], to[300], temp[300], name[BK_SET], first[BK_SET];
    int n, r;
    if (argc < 2)
        return 2;
    snprintf(base, sizeof base, "%s", argv[1]);
    bk_set_name(name, sizeof name, 2026, 11, 22, 9, 5, 7);
    CHECK(!strcmp(name, "20261122-090507"), "set name %s", name);
    dir("here"), dir("here/SUB"), dir("here/SUB/DEEP"), dir("here/EMPTY"), dir("there");
    memset(big, 'x', sizeof big - 1);
    make("here/A.TXT", "first words", 1);
    make("here/SUB/B.TXT", "the second file's words", 2);
    make("here/SUB/DEEP/C.BIN", big, 3);
    snprintf(here, sizeof here, "%s/here", base);
    snprintf(temp, sizeof temp, "%s/MANIFEST.TMP", base);
    memset(&j, 0, sizeof j);
    j.store = &store, j.where = "backups/DATA", j.temp = temp, j.progress = progress;
    fail_puts = -1;

    r = bk_backup(&j, here);                    /* everything */
    CHECK(r == BK_OK && j.files == 3 && j.copied == 3 && j.kept == 0 && j.folders == 1 && j.bytes == 11 + 23 + 5000,
          "full: r=%d files=%u copied=%u kept=%u folders=%u bytes=%llu (%s)", r, j.files, j.copied, j.kept, j.folders,
          j.bytes, j.why);
    snprintf(first, sizeof first, "%s", j.set);
    n = bk_sets(&store, "backups/DATA", sets, 16);
    CHECK(n == 1 && !strcmp(sets[0], first), "sets after one: %d", n);

    make("here/A.TXT", "first words, and more", 1);     /* grown */
    make("here/NEW.TXT", "new", 4);                     /* new */
    {
        char p[400];
        snprintf(p, sizeof p, "%s/here/SUB/B.TXT", base);
        remove(p);                                      /* gone */
    }
    j.changes = 1;
    r = bk_backup(&j, here);
    CHECK(r == BK_OK && j.files == 3 && j.copied == 2 && j.kept == 1, "changes: r=%d files=%u copied=%u kept=%u (%s)",
          r, j.files, j.copied, j.kept, j.why);
    CHECK(strcmp(j.set, first) != 0, "changes: the same set's name %s", j.set);
    make("here/SUB/DEEP/C.BIN", big, 5);                /* the same bytes, another date */
    r = bk_backup(&j, here);
    CHECK(r == BK_OK && j.copied == 1 && j.kept == 2, "re-dated: r=%d copied=%u kept=%u", r, j.copied, j.kept);
    n = bk_sets(&store, "backups/DATA", sets, 16);
    CHECK(n == 3 && !strcmp(sets[2], first) && strcmp(sets[0], sets[1]) > 0, "sets after three: %d", n);

    snprintf(to, sizeof to, "%s/back1", base);          /* the newest: as the folder is now */
    r = bk_restore(&j, sets[0], to);
    CHECK(r == BK_OK && j.files == 3 && j.copied == 3 && j.failed == 0 && j.folders == 1, "restore newest: r=%d %u %u %u (%s)",
          r, j.files, j.copied, j.failed, j.why);
    same("here/A.TXT", "back1/A.TXT", "newest");
    same("here/NEW.TXT", "back1/NEW.TXT", "newest");
    same("here/SUB/DEEP/C.BIN", "back1/SUB/DEEP/C.BIN", "newest");
    CHECK(!exists("back1/SUB/B.TXT") && exists("back1/EMPTY"), "newest: B.TXT there, or EMPTY not");

    snprintf(to, sizeof to, "%s/back2", base);          /* the first: as it was */
    r = bk_restore(&j, first, to);
    CHECK(r == BK_OK && j.copied == 3, "restore first: r=%d copied=%u", r, j.copied);
    CHECK(exists("back2/SUB/B.TXT") && !exists("back2/NEW.TXT"), "first: B.TXT not there, or NEW.TXT");
    {
        char p[400], t[64] = "";
        FILE *f;
        snprintf(p, sizeof p, "%s/back2/A.TXT", base);
        if ((f = fopen(p, "rb")) != NULL) {
            t[fread(t, 1, sizeof t - 1, f)] = 0;
            fclose(f);
        }
        CHECK(!strcmp(t, "first words"), "first: A.TXT is \"%s\"", t);
    }

    make("here/A.TXT", "changed again", 1);              /* a run stopped: no set */
    stop_at = 1;
    r = bk_backup(&j, here);
    stop_at = 0;
    CHECK(r == BK_E_STOPPED && bk_sets(&store, "backups/DATA", sets, 16) == 3, "stopped: r=%d", r);
    fail_puts = 0;                                       /* the store failing */
    r = bk_backup(&j, here);
    CHECK(r == BK_E_STORE && bk_sets(&store, "backups/DATA", sets, 16) == 3, "store failing: r=%d", r);
    fail_puts = -1;
    r = bk_restore(&j, "20000101-000000", to);
    CHECK(r == BK_E_NONE, "no such set: r=%d", r);
    r = bk_backup(&j, "/no/such/folder");
    CHECK(r == BK_E_LOCAL, "no such folder: r=%d", r);
    if (!fails)
        printf("backup_test: ok\n");
    return fails != 0;
}
