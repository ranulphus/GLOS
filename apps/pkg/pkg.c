/* PKG: GLOS's packages (M10 step E, PRD D58): installed from signed sources,
 * removed exactly. For the terminal and SSH.
 *
 *   PKG SOURCES                 the sources and how they're trusted
 *   PKG LIST [-I]               what can be installed (-I: what is)
 *   PKG INSTALL NAME...         from the first source that has it
 *   PKG REMOVE NAME...
 *   PKG UPGRADE [NAME...]       installed packages a source has newer
 *   options: --certs FILE, --resolve NAME:ADDRESS (as FETCH's)
 *
 * A GLOS source is a URL with INDEX.TXT, INDEX.SIG and the zips; INDEX.SIG
 * must be the Ed25519 signature over INDEX.TXT's bytes by a key in
 * KEYS\PKG\*.PUB, and each zip's size and SHA-256 must be INDEX.TXT's. A
 * FreeDOS source (its repositories' index.lst and zips) is unsigned: trusted
 * through HTTPS and its CRC-32 alone, and said so. Everything is under
 * GLOS's folder (PKG.EXE's; GLOSPKG in the environment names another):
 * PKG\SOURCES.TXT ("glos URL" or "freedos URL" a line), PKG\NAME.LST (what a
 * package put where, and its profile's keys), KEYS\PKG\, CERTS.DAT. A
 * GLOS package goes where its GLOSPKG.INI says (C:\APPS\NAME by default); a
 * FreeDOS one under DOSDIR (C:\FREEDOS), its sources left out. */
#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bearssl.h"
#include "crypto_sign_ed25519.h"
#include "sshbuf.h"
#include <glos/api.h>
#include <glos/http.h>
#include <glos/tls.h>

#include "pkg.h"

#define MAX_DOWNLOAD (16L << 20)
#define MAX_PKGS 512

static char base[260];                  /* GLOS's folder */
static int windowed;

/* A message: on stdout, and on the window's status line when there is one. */
void pkg_say(const char *fmt, ...)
{
    char line[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fputs(line, stdout);
    fflush(stdout);
    if (windowed) {
        line[strcspn(line, "\n")] = 0;
        pkg_status(strncmp(line, "PKG ", 4) ? (strncmp(line, "PKG: ", 5) ? line : line + 5) : line + 4);
    }
}
static const char *certs_opt, *resolve_opt;
static struct tls_anchors anchors;
static int anchors_loaded;

struct src {
    int freedos;
    char url[300];
    int signed_by;                      /* (GLOS: the index checked) */
    char key[64];
};

struct avail {
    char name[16], version[32], file[64], summary[100], sha[65];
    unsigned long size, crc;
    const struct src *src;
};

static struct src srcs[16];
static int nsrc;
static struct avail av[MAX_PKGS];
static int nav;

/* ---- downloads */

struct mem {
    unsigned char *p;
    size_t n, room;
    int too_big;
};

static int mem_put(void *arg, const void *d, unsigned n)
{
    struct mem *m = arg;
    if (m->n + n > (size_t)MAX_DOWNLOAD) {
        m->too_big = 1;
        return -1;
    }
    if (m->n + n > m->room) {
        size_t room = (m->n + n) * 2 + 4096;
        unsigned char *p = realloc(m->p, room);
        if (!p)
            return -1;
        m->p = p, m->room = room;
    }
    memcpy(m->p + m->n, d, n);
    m->n += n;
    return 0;
}

static int download(const char *url, struct mem *m, char *why, size_t room)
{
    struct http_get g;
    memset(m, 0, sizeof *m);
    if (!strncmp(url, "https://", 8) && !anchors_loaded) {
        char path[300];
        snprintf(path, sizeof path, "%s\\CERTS.DAT", base);
        if (tls_anchors_load(&anchors, certs_opt ? certs_opt : getenv("GLOSCERTS") ? getenv("GLOSCERTS") : path)
            != 0) {
            snprintf(why, room, "no trust anchors (%s)", certs_opt ? certs_opt : path);
            return -1;
        }
        anchors_loaded = 1;
    }
    memset(&g, 0, sizeof g);
    g.url = url;
    g.anchors = &anchors;
    g.resolve = resolve_opt;
    g.body = mem_put;
    g.arg = m;
    if (http_get(&g) != 0) {
        snprintf(why, room, "%s: %s", url, m->too_big ? "too big" : g.why);
        free(m->p);
        m->p = 0;
        return -1;
    }
    return 0;
}

/* ---- sources */

static void join(char *out, size_t room, const char *url, const char *file)
{
    snprintf(out, room, "%s%s%s", url, url[0] && url[strlen(url) - 1] == '/' ? "" : "/", file);
}

static void load_sources(void)
{
    char path[300], line[400];
    FILE *f;
    snprintf(path, sizeof path, "%s\\PKG\\SOURCES.TXT", base);
    nsrc = 0;
    if ((f = fopen(path, "r")) != NULL) {
        while (fgets(line, sizeof line, f) && nsrc < 16) {
            char kind[16], url[300];
            if (line[0] == '#' || sscanf(line, "%15s %299s", kind, url) != 2)
                continue;
            srcs[nsrc].freedos = !strcmp(kind, "freedos");
            if (!srcs[nsrc].freedos && strcmp(kind, "glos"))
                continue;
            snprintf(srcs[nsrc].url, sizeof srcs[nsrc].url, "%s", url);
            nsrc++;
        }
        fclose(f);
        return;
    }
    /* (none written: GLOS's own and FreeDOS 1.4's base and utilities) */
    snprintf(srcs[0].url, sizeof srcs[0].url, "https://raw.githubusercontent.com/ranulphus/glos-packages/main/");
    srcs[1].freedos = srcs[2].freedos = 1;
    snprintf(srcs[1].url, sizeof srcs[1].url, "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/base/");
    snprintf(srcs[2].url, sizeof srcs[2].url, "https://www.ibiblio.org/pub/micro/pc-stuff/freedos/files/repositories/1.4/util/");
    nsrc = 3;
}

/* INDEX.SIG against the keys in KEYS\PKG: the key's comment into who, 0; or -1. */
static int check_signature(const unsigned char *text, size_t n, const char *sigline, char *who, size_t room)
{
    char kdir[300], path[400], line[300];
    unsigned char sig[64], pk[40], *sm, *m;
    unsigned long long mlen;
    DIR *d;
    struct dirent *de;
    int ok = -1;
    const char *b = strchr(sigline, ' ');
    if (strncmp(sigline, "glos-pkg-ed25519 ", 17) || !b
        || b64_decode(sig, sizeof sig, b + 1, (uint32_t)strcspn(b + 1, " \r\n")) != 64)
        return -1;
    snprintf(kdir, sizeof kdir, "%s\\KEYS\\PKG", base);
    if (!(d = opendir(kdir)))
        return -1;
    sm = malloc(n + 64);
    m = malloc(n + 64);
    while (sm && m && ok != 0 && (de = readdir(d)) != NULL) {
        FILE *f;
        size_t l = strlen(de->d_name);
        if (l < 5 || strcasecmp(de->d_name + l - 4, ".PUB"))
            continue;
        snprintf(path, sizeof path, "%s\\%s", kdir, de->d_name);
        if (!(f = fopen(path, "r")))
            continue;
        if (fgets(line, sizeof line, f) && !strncmp(line, "glos-pkg-ed25519 ", 17)) {
            const char *k = line + 17, *c = strchr(k, ' ');
            if (b64_decode(pk, sizeof pk, k, (uint32_t)strcspn(k, " \r\n")) == 32) {
                memcpy(sm, sig, 64);
                memcpy(sm + 64, text, n);
                if (crypto_sign_ed25519_open(m, &mlen, sm, n + 64, pk) == 0) {
                    ok = 0;
                    snprintf(who, room, "%.*s", c ? (int)strcspn(c + 1, "\r\n") : 0, c ? c + 1 : "");
                }
            }
        }
        fclose(f);
    }
    closedir(d);
    free(sm);
    free(m);
    return ok;
}

static void add_glos(struct src *s, char *text)
{
    char *line = strtok(text, "\n");
    if (!line || strncmp(line, "GLOS-PKG-INDEX 1", 16))
        return;
    while ((line = strtok(NULL, "\n")) != NULL && nav < MAX_PKGS) {
        char *f[6];
        int i;
        for (i = 0; i < 6; i++) {
            f[i] = line;
            line = strchr(line, '\t');
            if (!line && i < 5)
                break;
            if (line)
                *line++ = 0;
            else
                line = f[i] + strlen(f[i]);
        }
        if (i < 5)
            continue;
        snprintf(av[nav].name, sizeof av[nav].name, "%s", f[0]);
        snprintf(av[nav].version, sizeof av[nav].version, "%s", f[1]);
        av[nav].size = strtoul(f[2], 0, 10);
        snprintf(av[nav].sha, sizeof av[nav].sha, "%s", f[3]);
        snprintf(av[nav].file, sizeof av[nav].file, "%s", f[4]);
        snprintf(av[nav].summary, sizeof av[nav].summary, "%s", f[5]);
        av[nav].summary[strcspn(av[nav].summary, "\r")] = 0;
        av[nav].src = s;
        nav++;
    }
}

static void add_freedos(struct src *s, char *text)
{
    char *line = strtok(text, "\n");
    if (!line || strncmp(line, "FD-REPOv1", 9))
        return;
    while ((line = strtok(NULL, "\n")) != NULL && nav < MAX_PKGS) {
        char name[64], version[64], *p = line, *t[4];
        int i;
        for (i = 0; i < 4 && p; i++) {
            t[i] = p;
            p = strchr(p, '\t');
            if (p)
                *p++ = 0;
        }
        if (i < 4)
            continue;
        snprintf(name, sizeof name, "%s", t[0]);
        snprintf(version, sizeof version, "%s", t[1]);
        for (p = name; *p; p++)
            *p = (char)toupper((unsigned char)*p);
        if (strlen(name) > 8)
            continue;
        snprintf(av[nav].name, sizeof av[nav].name, "%s", name);
        snprintf(av[nav].version, sizeof av[nav].version, "%s", version);
        snprintf(av[nav].summary, sizeof av[nav].summary, "%s", t[2]);
        av[nav].crc = strtoul(t[3], 0, 16);
        snprintf(av[nav].file, sizeof av[nav].file, "%s.zip", t[0]);
        av[nav].src = s;
        nav++;
    }
}

/* Every source's index read (a source that fails is said and passed over). */
static void load_indexes(int quiet)
{
    int i;
    nav = 0;
    for (i = 0; i < nsrc; i++) {
        struct src *s = &srcs[i];
        char url[400], why[200];
        struct mem m, sig;
        join(url, sizeof url, s->url, s->freedos ? "index.lst" : "INDEX.TXT");
        if (download(url, &m, why, sizeof why) != 0) {
            if (!quiet)
                pkg_say("PKG: %s\n", why);
            continue;
        }
        if (s->freedos) {
            mem_put(&m, "", 1);
            add_freedos(s, (char *)m.p);
        } else {
            join(url, sizeof url, s->url, "INDEX.SIG");
            if (download(url, &sig, why, sizeof why) != 0) {
                pkg_say("PKG: %s: no signature: passed over\n", s->url);
            } else {
                mem_put(&sig, "", 1);
                if (check_signature(m.p, m.n, (char *)sig.p, s->key, sizeof s->key) != 0) {
                    pkg_say("PKG: %s: the index isn't signed by a key in %s\\KEYS\\PKG: passed over\n", s->url, base);
                } else {
                    s->signed_by = 1;
                    mem_put(&m, "", 1);
                    add_glos(s, (char *)m.p);
                }
                free(sig.p);
            }
        }
        free(m.p);
    }
}

/* ---- versions */

static int vcmp(const char *a, const char *b)
{
    while (*a || *b) {
        if (isdigit((unsigned char)*a) && isdigit((unsigned char)*b)) {
            unsigned long x = strtoul(a, (char **)&a, 10), y = strtoul(b, (char **)&b, 10);
            if (x != y)
                return x < y ? -1 : 1;
        } else {
            int x = (unsigned char)*a, y = (unsigned char)*b;
            if (x != y)
                return x < y ? -1 : 1;
            if (*a) a++;
            if (*b) b++;
        }
    }
    return 0;
}

static const struct avail *best(const char *name)
{
    const struct avail *b = 0;
    int i;
    for (i = 0; i < nav; i++)
        if (!strcasecmp(av[i].name, name) && (!b || (b->src->freedos && !av[i].src->freedos)
                                               || (b->src == av[i].src && vcmp(av[i].version, b->version) > 0)))
            b = &av[i];
    return b;
}

/* ---- what is installed */

static void list_path(char *out, size_t room, const char *name)
{
    snprintf(out, room, "%s\\PKG\\%s.LST", base, name);
}

/* The installed version into out: 1, or 0 if it isn't installed. */
static int installed(const char *name, char *version, size_t room)
{
    char path[300], line[300];
    FILE *f;
    list_path(path, sizeof path, name);
    if (!(f = fopen(path, "r")))
        return 0;
    version[0] = 0;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "version ", 8))
            snprintf(version, room, "%.*s", (int)strcspn(line + 8, "\r\n"), line + 8);
    fclose(f);
    return 1;
}

static void mkdirs(const char *path)          /* each folder of path (a file's) made */
{
    char p[300];
    char *s;
    snprintf(p, sizeof p, "%s", path);
    for (s = p + 3; (s = strchr(s, '\\')) != NULL; s++) {
        *s = 0;
        mkdir(p, 0777);
        *s = '\\';
    }
}

static int remove_pkg(const char *name, int quiet)
{
    char path[300], line[400], dirs[64][260];
    int nd = 0, files = 0, keys = 0, i;
    FILE *f;
    list_path(path, sizeof path, name);
    if (!(f = fopen(path, "r"))) {
        pkg_say("PKG: %s isn't installed\n", name);
        return 1;
    }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "file ", 5)) {
            if (remove(line + 5) == 0 || errno == ENOENT)
                files++;
        } else if (!strncmp(line, "dir ", 4) && nd < 64) {
            snprintf(dirs[nd++], sizeof dirs[0], "%s", line + 4);
        } else if (!strncmp(line, "cfg ", 4)) {
            char *sec = line + 4, *key = strchr(sec, '\t');
            if (key) {
                *key++ = 0;
                if (glos_config_unset(sec, key) == 0)
                    keys++;
            }
        }
    }
    fclose(f);
    for (i = nd - 1; i >= 0; i--)               /* (deepest first: they were written in the order made) */
        rmdir(dirs[i]);
    remove(path);
    if (!quiet)
        pkg_say("PKG removed %s: %d files, %d settings\n", name, files, keys);
    return 0;
}

static unsigned char *fetch_zip(const struct avail *a, size_t *n, char *why, size_t room)
{
    char url[400];
    struct mem m;
    join(url, sizeof url, a->src->url, a->file);
    if (download(url, &m, why, room) != 0)
        return 0;
    if (a->src->freedos) {
        if (pkg_crc32(0, m.p, m.n) != a->crc) {
            snprintf(why, room, "%s: its CRC-32 isn't the index's", a->file);
            free(m.p);
            return 0;
        }
    } else {
        br_sha256_context h;
        unsigned char sum[32];
        char hex[65];
        int i;
        br_sha256_init(&h);
        br_sha256_update(&h, m.p, m.n);
        br_sha256_out(&h, sum);
        for (i = 0; i < 32; i++)
            sprintf(hex + 2 * i, "%02x", sum[i]);
        if (m.n != a->size || strcmp(hex, a->sha)) {
            snprintf(why, room, "%s: its size or SHA-256 isn't the signed index's: refused", a->file);
            free(m.p);
            return 0;
        }
    }
    *n = m.n;
    return m.p;
}

/* GLOSPKG.INI: [package]'s dir, and each [program X] key into the list (cfg lines) as set. */
static int apply_manifest(const char *ini, char *dir, size_t room, FILE *lst, int dry, char *why, size_t wroom)
{
    char copy[8192], *line, section[64] = "";
    snprintf(copy, sizeof copy, "%s", ini);
    for (line = strtok(copy, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq, *k, *v, *e;
        line[strcspn(line, "\r")] = 0;
        while (*line == ' ' || *line == '\t')
            line++;
        if (!*line || *line == ';' || *line == '#')
            continue;
        if (*line == '[') {
            snprintf(section, sizeof section, "%.*s", (int)strcspn(line + 1, "]"), line + 1);
            continue;
        }
        if (!(eq = strchr(line, '=')))
            continue;
        *eq = 0;
        k = line, v = eq + 1;
        for (e = k + strlen(k); e > k && (e[-1] == ' ' || e[-1] == '\t'); e--) ;
        *e = 0;
        while (*v == ' ' || *v == '\t')
            v++;
        if (!strcasecmp(section, "package")) {
            if (!strcasecmp(k, "dir"))
                snprintf(dir, room, "%s", v);
        } else if (!strncasecmp(section, "program ", 8) && !dry) {
            char sec[80];
            int i;
            snprintf(sec, sizeof sec, "program %s", section + 8);
            for (i = 8; sec[i]; i++)
                sec[i] = (char)toupper((unsigned char)sec[i]);
            if (glos_config_set(sec, k, v, why, (unsigned)wroom) < 0) {
                return -1;
            }
            fprintf(lst, "cfg %s\t%s\n", sec, k);
        }
    }
    return 0;
}

static int install(const char *name, int quiet)
{
    const struct avail *a = best(name);
    char why[300], ver[32], dir[260] = "", lpath[300], *ini = 0;
    unsigned char *zip;
    size_t n;
    static struct zip z;
    FILE *lst;
    int i, files = 0;
    if (!a) {
        pkg_say("PKG: no source has %s\n", name);
        return 1;
    }
    if (installed(a->name, ver, sizeof ver)) {
        pkg_say("PKG: %s %s is installed already%s\n", a->name, ver,
               vcmp(a->version, ver) > 0 ? " (PKG UPGRADE for the newer one)" : "");
        return 1;
    }
    if (!(zip = fetch_zip(a, &n, why, sizeof why))) {
        pkg_say("PKG: %s\n", why);
        return 1;
    }
    if (zip_open(&z, zip, n, why, sizeof why) != 0) {
        pkg_say("PKG: %s: %s\n", a->file, why);
        free(zip);
        return 1;
    }
    if (a->src->freedos) {
        const char *d = getenv("DOSDIR");
        snprintf(dir, sizeof dir, "%s", d && *d ? d : "C:\\FREEDOS");
    } else {
        const struct zip_entry *e = zip_find(&z, "GLOSPKG.INI");
        if (!e || !(ini = (char *)zip_read(&z, e, why, sizeof why))) {
            pkg_say("PKG: %s: no GLOSPKG.INI%s%s\n", a->file, e ? ": " : "", e ? why : "");
            free(zip);
            return 1;
        }
        ini = realloc(ini, e->usize + 1);
        ini[e->usize] = 0;
        snprintf(dir, sizeof dir, "C:\\APPS\\%s", a->name);
        apply_manifest(ini, dir, sizeof dir, 0, 1, why, sizeof why);
    }
    for (i = 0; i < z.n; i++) {                 /* every entry read and checked before anything is written */
        unsigned char *d;
        if (z.e[i].dir || !strcmp(z.e[i].name, "GLOSPKG.INI") || (a->src->freedos && !strncmp(z.e[i].name, "SOURCE\\", 7)))
            continue;
        if (!(d = zip_read(&z, &z.e[i], why, sizeof why))) {
            pkg_say("PKG: %s: %s: refused\n", a->file, why);
            free(zip);
            free(ini);
            return 1;
        }
        free(d);
    }
    snprintf(lpath, sizeof lpath, "%s\\PKG\\X", base);
    mkdirs(lpath);
    list_path(lpath, sizeof lpath, a->name);
    if (!(lst = fopen(lpath, "w"))) {
        pkg_say("PKG: can't write %s\n", lpath);
        free(zip);
        free(ini);
        return 1;
    }
    fprintf(lst, "GLOS-PKG-LIST 1\nname %s\nversion %s\nsource %s %s\n", a->name, a->version,
            a->src->freedos ? "freedos" : "glos", a->src->url);
    {
        char p[300], made[64][260];
        int nm = 0, j;
        snprintf(p, sizeof p, "%s\\X", dir);    /* the folders made, outermost first, for the list (and removal) */
        for (j = 3; p[j]; j++)
            if (p[j] == '\\') {
                struct stat st;
                p[j] = 0;
                if (stat(p, &st) != 0 && nm < 64) {
                    mkdir(p, 0777);
                    snprintf(made[nm++], sizeof made[0], "%s", p);
                }
                p[j] = '\\';
            }
        for (i = 0; i < z.n; i++) {
            char path[300];
            const struct zip_entry *e = &z.e[i];
            unsigned char *d;
            FILE *f;
            if (e->dir || !strcmp(e->name, "GLOSPKG.INI") || (a->src->freedos && !strncmp(e->name, "SOURCE\\", 7)))
                continue;
            snprintf(path, sizeof path, "%s\\%s", dir, e->name);
            for (j = (int)strlen(dir) + 1; path[j]; j++)    /* its folders inside the package's */
                if (path[j] == '\\') {
                    struct stat st;
                    path[j] = 0;
                    if (stat(path, &st) != 0 && nm < 64) {
                        mkdir(path, 0777);
                        snprintf(made[nm++], sizeof made[0], "%s", path);
                    }
                    path[j] = '\\';
                }
            if ((f = fopen(path, "rb")) != NULL) {
                fclose(f);
                pkg_say("PKG: %s is there already: left as it is, not the package's\n", path);
                continue;
            }
            d = zip_read(&z, e, why, sizeof why);
            if (!d || !(f = fopen(path, "wb")) || fwrite(d, 1, e->usize, f) != e->usize || fclose(f) != 0) {
                pkg_say("PKG: can't write %s\n", path);
                free(d);
                continue;
            }
            free(d);
            fprintf(lst, "file %s\n", path);
            files++;
        }
        for (j = 0; j < nm; j++)
            fprintf(lst, "dir %s\n", made[j]);
    }
    if (ini && apply_manifest(ini, dir, sizeof dir, lst, 0, why, sizeof why) != 0)
        pkg_say("PKG: a setting refused: %s\n", why);
    fclose(lst);
    free(zip);
    free(ini);
    if (!quiet)
        pkg_say("PKG installed %s %s in %s: %d files (%s)\n", a->name, a->version, dir, files,
               a->src->freedos ? "FreeDOS's, unsigned: trusted through HTTPS" : "signed");
    return 0;
}

/* ---- for the window (win.c) */

int pkg_count(void) { return nav; }

void pkg_row(int i, const char **cells, char *inst, size_t room)
{
    cells[0] = av[i].name;
    cells[1] = av[i].version;
    if (!installed(av[i].name, inst, room))
        inst[0] = 0;
    cells[2] = inst;
    cells[3] = av[i].src->freedos ? "FreeDOS" : "signed";
    cells[4] = av[i].summary;
}

void pkg_reload(void) { load_indexes(0); }
int pkg_install(const char *name) { return install(name, 0); }
int pkg_remove(const char *name) { return remove_pkg(name, 0); }

int pkg_upgrade_all(void)
{
    char ver[32];
    int i, rc = 0, any = 0;
    for (i = 0; i < nav; i++) {
        const struct avail *b;
        if (!installed(av[i].name, ver, sizeof ver) || (b = best(av[i].name)) != &av[i] || vcmp(b->version, ver) <= 0)
            continue;
        pkg_say("PKG upgrading %s %s to %s\n", b->name, ver, b->version);
        if (remove_pkg(b->name, 1) != 0 || install(b->name, 0) != 0)
            rc = 1;
        any = 1;
    }
    if (!any)
        pkg_say("PKG nothing to upgrade\n");
    return rc;
}

/* ---- the commands */

static int cmd_list(int inst_only)
{
    char path[300], ver[32];
    int i, n = 0;
    if (inst_only) {
        DIR *d;
        struct dirent *de;
        snprintf(path, sizeof path, "%s\\PKG", base);
        if ((d = opendir(path)) != NULL) {
            while ((de = readdir(d)) != NULL) {
                size_t l = strlen(de->d_name);
                char name[16];
                if (l < 5 || strcasecmp(de->d_name + l - 4, ".LST") || l - 4 >= sizeof name)
                    continue;
                snprintf(name, sizeof name, "%.*s", (int)(l - 4), de->d_name);
                for (i = 0; name[i]; i++)
                    name[i] = (char)toupper((unsigned char)name[i]);
                if (installed(name, ver, sizeof ver)) {
                    pkg_say("PKG installed %-8s %s\n", name, ver);
                    n++;
                }
            }
            closedir(d);
        }
        pkg_say("PKG %d installed\n", n);
        return 0;
    }
    load_indexes(0);
    for (i = 0; i < nav; i++)
        pkg_say("PKG %-8s %-10s %s%s%s\n", av[i].name, av[i].version, av[i].summary,
               installed(av[i].name, ver, sizeof ver) ? " [installed]" : "", av[i].src->freedos ? " (FreeDOS)" : "");
    pkg_say("PKG %d available\n", nav);
    return 0;
}

int main(int argc, char **argv)
{
    const char *e = getenv("GLOSPKG");
    char *p;
    int i, rc = 0;
    if (e && *e) {
        snprintf(base, sizeof base, "%s", e);
    } else {
        snprintf(base, sizeof base, "%s", argv[0]);
        for (p = base; *p; p++)
            *p = *p == '/' ? '\\' : (char)toupper((unsigned char)*p);
        if ((p = strrchr(base, '\\')) != NULL)
            *p = 0;
        else
            snprintf(base, sizeof base, "C:\\GLOS");
    }
    while (argc > 2 && argv[1][0] == '-') {
        if (!strcmp(argv[1], "--certs"))
            certs_opt = argv[2];
        else if (!strcmp(argv[1], "--resolve"))
            resolve_opt = argv[2];
        else
            break;
        argv += 2, argc -= 2;
    }
    if (argc < 2) {                             /* the window */
        windowed = 1;
        load_sources();
        return pkg_window(argc, argv);
    }
    for (p = argv[1]; *p; p++)
        *p = (char)toupper((unsigned char)*p);
    if (glos_init() != 0) {
        pkg_say("PKG: this needs GLOS\n");
        return 1;
    }
    load_sources();
    if (!strcmp(argv[1], "SOURCES")) {
        load_indexes(0);
        for (i = 0; i < nsrc; i++)
            pkg_say("PKG source %s %s %s%s\n", srcs[i].freedos ? "freedos" : "glos", srcs[i].url,
                   srcs[i].freedos ? "unsigned: trusted through HTTPS" : srcs[i].signed_by ? "signed by " : "not verified",
                   srcs[i].signed_by ? srcs[i].key : "");
    } else if (!strcmp(argv[1], "LIST")) {
        rc = cmd_list(argc > 2 && !strcasecmp(argv[2], "-I"));
    } else if (!strcmp(argv[1], "INSTALL") && argc > 2) {
        load_indexes(1);
        for (i = 2; i < argc; i++)
            rc |= install(argv[i], 0);
    } else if (!strcmp(argv[1], "REMOVE") && argc > 2) {
        for (i = 2; i < argc; i++)
            rc |= remove_pkg(argv[i], 0);
    } else if (!strcmp(argv[1], "UPGRADE")) {
        char ver[32];
        int any = 0;
        load_indexes(1);
        for (i = 0; i < nav; i++) {
            int j, wanted = argc <= 2;
            const struct avail *b;
            for (j = 2; j < argc; j++)
                wanted |= !strcasecmp(argv[j], av[i].name);
            if (!wanted || !installed(av[i].name, ver, sizeof ver) || (b = best(av[i].name)) != &av[i]
                || vcmp(b->version, ver) <= 0)
                continue;
            pkg_say("PKG upgrading %s %s to %s\n", b->name, ver, b->version);
            if (remove_pkg(b->name, 1) != 0 || install(b->name, 0) != 0)
                rc = 1;
            any = 1;
        }
        if (!any)
            pkg_say("PKG nothing to upgrade\n");
    } else {
        pkg_say("PKG: %s? (SOURCES, LIST, INSTALL, REMOVE, UPGRADE)\n", argv[1]);
        rc = 2;
    }
    return rc;
}
