/* A FAT volume read and added to (fatimg.h): FAT12 and FAT16.
 *
 * The volume's first sector says its shape: sectors to a cluster, the reserved sectors before the FATs, how
 * many FATs and how long, how many entries the root folder has room for. The root folder lies after the FATs,
 * the clusters after it. A FAT's entry for a cluster says the next cluster of the same file, or that it is the
 * last; 12 bits an entry in FAT12 (two entries in three bytes), 16 in FAT16. A folder is entries of 32 bytes: a
 * DOS name of eight and three, attributes, a time and a date, the first cluster, the size; a long name lies
 * before its entry in entries of their own (attribute 0Fh), thirteen UTF-16 characters each, the last first.
 *
 * The FAT is read once and kept; what is added changes it in memory and it is written (every copy) at the
 * close. Whatever the volume says: every cluster's number is held to the volume's count, every chain to as
 * many steps as there are clusters. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fatimg.h"

struct fatimg {
    fatimg_io io;
    void *ctx;
    int fat;
    unsigned spc, reserved, nfats, root_entries;
    unsigned long fat_sectors, root_start, root_sectors, data_start, sectors, clusters;
    unsigned char *table;
    int changed;
    char label[12];
    unsigned long base;                 /* (2026-11-23) the volume's first sector in the disk: a hard disk's image's
                                           first FAT partition, else 0 */
};

struct found {                          /* an entry found in a folder */
    unsigned first;                     /* its first cluster (0: none; the root, for a folder) */
    unsigned long size;
    unsigned char attr;
    unsigned long lba;                  /* where its entry lies: the sector, and the byte in it */
    unsigned at;
};

static unsigned rd16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned long rd32(const unsigned char *p) { return rd16(p) | (unsigned long)rd16(p + 2) << 16; }

static int vio(const struct fatimg *v, unsigned long lba, unsigned char *data, unsigned count, int write)
{
    return v->io(v->ctx, v->base + lba, data, count, write);    /* (the volume's sectors: from its partition's start) */
}

/* A first sector that is a FAT volume's boot sector, by its fields (a partition table's is not). */
static int is_boot(const unsigned char *s)
{
    unsigned spc = s[13];
    return rd16(s + 11) == 512 && spc && !(spc & (spc - 1)) && rd16(s + 14) && s[16] >= 1 && s[16] <= 2 && rd16(s + 17);
}
static void wr16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8); }
static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* ---- the FAT */

static unsigned next_of(const struct fatimg *v, unsigned c)     /* 0: the chain ends here (or goes nowhere good) */
{
    unsigned n;
    if (c < 2 || c >= v->clusters + 2)
        return 0;
    if (v->fat == 12) {
        n = rd16(v->table + c + c / 2);
        n = c & 1 ? n >> 4 : n & 0xFFF;
        return n >= 0xFF7 ? 0 : n;
    }
    n = rd16(v->table + 2ul * c);
    return n >= 0xFFF7 ? 0 : n;
}

static unsigned entry_of(const struct fatimg *v, unsigned c)    /* the entry as it is (0: a free cluster) */
{
    unsigned n = rd16(v->table + (v->fat == 12 ? c + c / 2 : 2ul * c));
    return v->fat == 12 ? (c & 1 ? n >> 4 : n & 0xFFF) : n;
}

static void set_entry(struct fatimg *v, unsigned c, unsigned n)
{
    unsigned char *p = v->table + (v->fat == 12 ? c + c / 2 : 2ul * c);
    if (c < 2 || c >= v->clusters + 2)          /* (no cluster of this volume's: nothing is written for it) */
        return;
    if (v->fat == 16)
        wr16(p, n);
    else if (c & 1)
        p[0] = (unsigned char)((p[0] & 0x0F) | (n << 4)), p[1] = (unsigned char)(n >> 4);
    else
        p[0] = (unsigned char)n, p[1] = (unsigned char)((p[1] & 0xF0) | (n >> 8 & 0x0F));
    v->changed = 1;
}

static unsigned long cluster_lba(const struct fatimg *v, unsigned c) { return v->data_start + (unsigned long)(c - 2) * v->spc; }

/* ---- a folder's sectors: the root's lie together; another's are its clusters' */

static int dir_sector(const struct fatimg *v, unsigned first, unsigned long n, unsigned long *lba)
{
    unsigned long steps;
    unsigned c = first;
    if (!first) {
        if (n >= v->root_sectors)
            return -1;
        *lba = v->root_start + n;
        return 0;
    }
    for (steps = n / v->spc; steps; steps--)
        if (!(c = next_of(v, c)))
            return -1;
    if (c < 2 || c >= v->clusters + 2 || n / v->spc > v->clusters)
        return -1;
    *lba = cluster_lba(v, c) + n % v->spc;
    return 0;
}

static unsigned put_utf8(char *out, unsigned u)
{
    if (u < 0x80) {
        out[0] = (char)u;
        return 1;
    }
    if (u < 0x800) {
        out[0] = (char)(0xC0 | u >> 6), out[1] = (char)(0x80 | (u & 63));
        return 2;
    }
    out[0] = (char)(0xE0 | u >> 12), out[1] = (char)(0x80 | (u >> 6 & 63)), out[2] = (char)(0x80 | (u & 63));
    return 3;
}

/* A folder gone through: fn for each file and folder in it (with its long name where it has one); fn's nonzero
   stops it. -1: a sector could not be read. */
typedef int (*each_fn)(void *arg, const struct fatimg_entry *e, const struct found *f);

static int walk(struct fatimg *v, unsigned first, each_fn fn, void *arg)
{
    unsigned char s[512];
    unsigned short lfn[261];
    unsigned long n, lba;
    unsigned i, k, have = 0, sum = 0;
    memset(lfn, 0, sizeof lfn);
    for (n = 0; n < 65536ul / 16 * 2 && dir_sector(v, first, n, &lba) == 0; n++) {
        if (vio(v, lba, s, 1, 0) != 0)
            return -1;
        for (i = 0; i < 512; i += 32) {
            const unsigned char *e = s + i;
            struct fatimg_entry out;
            struct found f;
            unsigned char check = 0;
            if (!e[0])
                return 0;                       /* the folder's end */
            if (e[0] == 0xE5) {
                have = 0;
                continue;
            }
            if ((e[11] & 0x3F) == 0x0F) {       /* a part of a long name: thirteen characters, at their place */
                static const unsigned char where[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
                unsigned seq = e[0] & 0x3F;
                if (e[0] & 0x40)
                    memset(lfn, 0, sizeof lfn), have = seq, sum = e[13];
                if (seq >= 1 && seq <= 20 && e[13] == sum)
                    for (k = 0; k < 13; k++)
                        lfn[(seq - 1) * 13 + k] = (unsigned short)rd16(e + where[k]);
                else
                    have = 0;
                continue;
            }
            if (e[11] & 0x08) {                 /* the volume's name: no file */
                have = 0;
                continue;
            }
            memset(&out, 0, sizeof out);
            for (k = 0; k < 11; k++)
                check = (unsigned char)(((check & 1) << 7) + (check >> 1) + e[k]);
            for (k = 0; k < 8 && e[k] != ' '; k++)
                out.dos[k] = (char)(k == 0 && e[0] == 0x05 ? 0xE5 : e[k]);
            if (e[8] != ' ') {
                unsigned d = k;
                out.dos[d++] = '.';
                for (k = 8; k < 11 && e[k] != ' '; k++)
                    out.dos[d++] = (char)e[k];
            }
            if (have && check == sum && lfn[0]) {               /* its long name, which is its own */
                unsigned o = 0;
                for (k = 0; k < 260 && lfn[k] && lfn[k] != 0xFFFF && o + 4 < sizeof out.name; k++)
                    o += put_utf8(out.name + o, lfn[k] >= 0xD800 && lfn[k] < 0xE000 ? '?' : lfn[k]);
                out.name[o] = 0;
            } else {                            /* its DOS name, in small letters where it was written so */
                unsigned o = 0, dot = 0;
                for (k = 0; out.dos[k]; k++) {
                    int c = (unsigned char)out.dos[k], small;
                    dot |= c == '.';
                    small = e[12] & (dot ? 0x10 : 0x08);
                    out.name[o++] = (char)(small && c >= 'A' && c <= 'Z' ? c + 32 : c);
                }
            }
            have = 0;
            if (!strcmp(out.dos, ".") || !strcmp(out.dos, ".."))
                continue;
            out.size = rd32(e + 28), out.time = (unsigned short)rd16(e + 22), out.date = (unsigned short)rd16(e + 24);
            out.attr = e[11], out.dir = (e[11] & 0x10) != 0;
            f.first = rd16(e + 26), f.size = out.size, f.attr = e[11], f.lba = lba, f.at = i;
            if (fn(arg, &out, &f))
                return 0;
        }
    }
    return 0;
}

/* ---- names and paths */

static int same(const char *a, const char *b, size_t n)        /* b is a's whole name, n letters long, whatever the case */
{
    size_t i;
    for (i = 0; i < n; i++)
        if (!a[i] || upper((unsigned char)a[i]) != upper((unsigned char)b[i]))
            return 0;
    return !a[n];
}

struct seek {
    const char *name;
    size_t n;
    struct found f;
    int hit;
};

static int seek_fn(void *arg, const struct fatimg_entry *e, const struct found *f)
{
    struct seek *s = arg;
    if (same(e->name, s->name, s->n) || same(e->dos, s->name, s->n)) {
        s->f = *f, s->hit = 1;
        return 1;
    }
    return 0;
}

/* A path's entry. 0; -1: not there. An empty path is the root (a folder with no cluster). */
static int find(struct fatimg *v, const char *path, struct found *out)
{
    memset(out, 0, sizeof *out);
    out->attr = 0x10;
    while (*path == '\\' || *path == '/')
        path++;
    while (*path) {
        struct seek s;
        size_t n = strcspn(path, "\\/");
        if (!(out->attr & 0x10))
            return -1;                          /* (a file has nothing in it) */
        s.name = path, s.n = n, s.hit = 0;
        if (walk(v, out->first, seek_fn, &s) != 0 || !s.hit)
            return -1;
        *out = s.f;
        path += n;
        while (*path == '\\' || *path == '/')
            path++;
    }
    return 0;
}

/* ---- opening */

struct fatimg *fatimg_open(fatimg_io io, void *ctx, char *why, int room)
{
    unsigned char s[512];
    struct fatimg *v = calloc(1, sizeof *v);
    unsigned long total, need, i;
    const char *bad = NULL;
    if (!v) {
        snprintf(why, (size_t)room, "no memory");
        return NULL;
    }
    v->io = io, v->ctx = ctx;
    if (io(ctx, 0, s, 1, 0) != 0)
        bad = "its first sector can't be read";
    else if (!is_boot(s) && s[510] == 0x55 && s[511] == 0xAA) {      /* a hard disk's: its first FAT partition */
        int k;
        for (k = 0; k < 4 && !v->base; k++) {
            const unsigned char *e = s + 446 + 16 * k;
            if ((e[4] == 0x01 || e[4] == 0x04 || e[4] == 0x06 || e[4] == 0x0E) && rd32(e + 8))
                v->base = rd32(e + 8);
        }
        if (!v->base)
            bad = "a hard disk with no FAT12 or FAT16 partition";
        else if (io(ctx, v->base, s, 1, 0) != 0)
            bad = "its partition's first sector can't be read";
    }
    if (!bad && rd16(s + 11) != 512)
        bad = "no FAT volume (its sectors are not of 512 bytes)";
    else if (!bad) {
        v->spc = s[13], v->reserved = rd16(s + 14), v->nfats = s[16], v->root_entries = rd16(s + 17);
        total = rd16(s + 19) ? rd16(s + 19) : rd32(s + 32);
        v->fat_sectors = rd16(s + 22);
        v->root_sectors = (v->root_entries * 32ul + 511) / 512;
        v->root_start = v->reserved + (unsigned long)v->nfats * v->fat_sectors;
        v->data_start = v->root_start + v->root_sectors;
        v->sectors = total;
        if (!v->spc || (v->spc & (v->spc - 1)) || !v->reserved || v->nfats < 1 || v->nfats > 2 || !v->fat_sectors || !v->root_entries
            || total <= v->data_start)
            bad = "no FAT volume of the kinds read here (FAT12, FAT16)";
        else {
            v->clusters = (total - v->data_start) / v->spc;
            v->fat = v->clusters < 4085 ? 12 : 16;
            need = v->fat == 12 ? (v->clusters + 2) * 3 / 2 + 1 : (v->clusters + 2) * 2;
            if (v->clusters >= 65525)
                bad = "a FAT32 volume: not read here";
            else if (!v->clusters || need > v->fat_sectors * 512)
                bad = "a FAT volume whose table is too short for it";
        }
    }
    if (!bad && !(v->table = malloc(v->fat_sectors * 512 + 2)))
        bad = "no memory";
    for (i = 0; !bad && i < v->fat_sectors; i += 32) {
        unsigned count = v->fat_sectors - i < 32 ? (unsigned)(v->fat_sectors - i) : 32;
        if (vio(v, v->reserved + i, v->table + i * 512, count, 0) != 0)
            bad = "its FAT can't be read";
    }
    if (bad) {
        snprintf(why, (size_t)room, "%s", bad);
        free(v->table);
        free(v);
        return NULL;
    }
    {                                           /* its name: the root's entry with the label's attribute, else the first sector's */
        unsigned long n, lba;
        unsigned k, got = 0;
        unsigned char d[512];
        for (n = 0; !got && n < v->root_sectors && dir_sector(v, 0, n, &lba) == 0 && vio(v, lba, d, 1, 0) == 0; n++)
            for (i = 0; i < 512 && d[i]; i += 32)
                if (d[i] != 0xE5 && (d[i + 11] & 0x3F) != 0x0F && (d[i + 11] & 0x08)) {
                    memcpy(v->label, d + i, 11);
                    got = 1;
                    break;
                }
        if (!got && s[38] == 0x29)
            memcpy(v->label, s + 43, 11);
        for (k = 11; k && (v->label[k - 1] == ' ' || !v->label[k - 1]); k--)
            v->label[k - 1] = 0;
        if (!strcmp(v->label, "NO NAME"))
            v->label[0] = 0;
    }
    return v;
}

int fatimg_close(struct fatimg *v)
{
    unsigned long i;
    unsigned f;
    int r = 0;
    if (!v)
        return 0;
    for (f = 0; v->changed && f < v->nfats; f++)
        for (i = 0; i < v->fat_sectors; i += 32) {
            unsigned count = v->fat_sectors - i < 32 ? (unsigned)(v->fat_sectors - i) : 32;
            if (vio(v, v->reserved + f * v->fat_sectors + i, v->table + i * 512, count, 1) != 0)
                r = -1;
        }
    free(v->table);
    free(v);
    return r;
}

void fatimg_info(struct fatimg *v, struct fatimg_info *out)
{
    unsigned c;
    memset(out, 0, sizeof *out);
    out->fat = v->fat, out->sectors = v->sectors, out->clusters = v->clusters;
    out->cluster_bytes = v->spc * 512, out->root_entries = v->root_entries;
    for (c = 2; c < v->clusters + 2; c++)
        out->free_clusters += entry_of(v, c) == 0;
    memcpy(out->label, v->label, sizeof out->label);
}

/* ---- reading */

struct lister {
    struct fatimg_entry *e;
    int n, room, failed;
};

static int list_fn(void *arg, const struct fatimg_entry *e, const struct found *f)
{
    struct lister *l = arg;
    (void)f;
    if (l->n == l->room) {
        int room = l->room ? l->room * 2 : 16;
        struct fatimg_entry *m = room <= 8192 ? realloc(l->e, (size_t)room * sizeof *m) : NULL;
        if (!m) {
            l->failed = 1;
            return 1;
        }
        l->e = m, l->room = room;
    }
    l->e[l->n++] = *e;
    return 0;
}

int fatimg_list(struct fatimg *v, const char *path, struct fatimg_entry **out)
{
    struct lister l = { NULL, 0, 0, 0 };
    struct found f;
    *out = NULL;
    if (find(v, path, &f) != 0 || !(f.attr & 0x10) || walk(v, f.first, list_fn, &l) != 0 || l.failed) {
        free(l.e);
        return -1;
    }
    *out = l.e;
    return l.n;
}

long fatimg_read(struct fatimg *v, const char *path, unsigned long at, void *buf, unsigned long n)
{
    unsigned char s[512], *to = buf;
    unsigned long bytes = (unsigned long)v->spc * 512, skip, done = 0, steps = 0;
    struct found f;
    unsigned c;
    if (find(v, path, &f) != 0 || (f.attr & 0x10))
        return -1;
    if (at >= f.size)
        return 0;
    n = n > f.size - at ? f.size - at : n;
    for (c = f.first, skip = at / bytes; skip; skip--)
        if (!(c = next_of(v, c)) || ++steps > v->clusters)
            return -1;
    at %= bytes;
    while (done < n) {
        unsigned long sector = at / 512, off = at % 512, take;
        if (c < 2 || c >= v->clusters + 2 || ++steps > 2 * v->clusters + 2)
            return -1;                          /* (the file says it is longer than its chain) */
        for (; sector < v->spc && done < n; sector++, off = 0) {
            if (vio(v, cluster_lba(v, c) + sector, s, 1, 0) != 0)
                return -1;
            take = 512 - off < n - done ? 512 - off : n - done;
            memcpy(to + done, s + off, take);
            done += take;
        }
        at = 0;
        c = next_of(v, c);
    }
    return (long)done;
}

/* ---- adding */

static int dos_name(const char *name, size_t n, unsigned char out[11])  /* NAME.EXT as a folder's entry has it; -1: no DOS name */
{
    static const char also[] = "!#$%&'()-@^_`{}~";
    size_t i, k = 0, dot = n;
    memset(out, ' ', 11);
    for (i = 0; i < n; i++)
        if (name[i] == '.') {
            if (dot != n)
                return -1;
            dot = i;
        }
    if (!dot || dot > 8 || n - dot > 4 || (dot < n && n - dot == 1))
        return -1;
    for (i = 0; i < n; i++) {
        int c = upper((unsigned char)name[i]);
        if (i == dot) {
            k = 8;
            continue;
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr(also, c)) || c >= 0x80))
            return -1;
        out[k++] = (unsigned char)c;
    }
    return 0;
}

static unsigned grab(struct fatimg *v, unsigned long count)     /* a chain of that many free clusters; 0: there are not so many */
{
    unsigned first = 0, last = 0, c;
    for (c = 2; c < v->clusters + 2 && count; c++)
        if (entry_of(v, c) == 0) {
            set_entry(v, c, v->fat == 12 ? 0xFFF : 0xFFFF);
            if (last)
                set_entry(v, last, c);
            else
                first = c;
            last = c, count--;
        }
    if (count) {                                /* not enough: what was taken is free again */
        for (c = first; c; ) {
            unsigned n = next_of(v, c);
            set_entry(v, c, 0);
            c = n;
        }
        return 0;
    }
    return first;
}

static void let_go(struct fatimg *v, unsigned first)
{
    unsigned long steps = 0;
    unsigned c = first;
    while (c >= 2 && c < v->clusters + 2 && steps++ <= v->clusters) {
        unsigned n = next_of(v, c);
        set_entry(v, c, 0);
        c = n;
    }
}

static int zero_cluster(struct fatimg *v, unsigned c)
{
    unsigned char s[512];
    unsigned i;
    memset(s, 0, sizeof s);
    for (i = 0; i < v->spc; i++)
        if (vio(v, cluster_lba(v, c) + i, s, 1, 1) != 0)
            return -1;
    return 0;
}

/* A free entry in a folder (one never used, or one deleted), a cluster added to the folder if it is full (the
   root has what it has): the sector and the byte in it. 0; -1: no room, or the disk failed. */
static int slot(struct fatimg *v, unsigned first, unsigned long *lba, unsigned *at)
{
    unsigned char s[512];
    unsigned long n;
    unsigned i, c, last;
    for (n = 0; n < 4096 && dir_sector(v, first, n, lba) == 0; n++) {
        if (vio(v, *lba, s, 1, 0) != 0)
            return -1;
        for (i = 0; i < 512; i += 32)
            if (!s[i] || s[i] == 0xE5) {
                *at = i;
                return 0;
            }
    }
    if (!first || n >= 4096 || first >= v->clusters + 2)
        return -1;
    for (last = first, n = 0; (c = next_of(v, last)) != 0 && n <= v->clusters; n++)
        last = c;
    if (n > v->clusters || !(c = grab(v, 1)))   /* (a chain that goes round is not added to) */
        return -1;
    if (zero_cluster(v, c) != 0) {
        set_entry(v, c, 0);
        return -1;
    }
    set_entry(v, last, c);
    *lba = cluster_lba(v, c), *at = 0;
    return 0;
}

/* Where a new file or folder would go: `path` up to its last part must be a folder that is there, and the last
   part a DOS name that is not there yet. 0, with the folder and the name as an entry has it; -1. */
static int place(struct fatimg *v, const char *path, struct found *parent, unsigned char name[11])
{
    const char *leaf = path + strlen(path);
    char up[300];
    struct found was;
    while (leaf > path && leaf[-1] != '\\' && leaf[-1] != '/')
        leaf--;
    if ((size_t)(leaf - path) >= sizeof up || dos_name(leaf, strlen(leaf), name) != 0)
        return -1;
    memcpy(up, path, (size_t)(leaf - path));
    up[leaf - path] = 0;
    if (find(v, up, parent) != 0 || !(parent->attr & 0x10) || find(v, path, &was) == 0)
        return -1;
    return 0;
}

/* The entry written in the folder (one found by place). 0; -1: the folder has no room, or the disk failed. */
static int enter(struct fatimg *v, const struct found *parent, const unsigned char name[11], unsigned first, unsigned long size,
                 unsigned attr, unsigned short date, unsigned short time)
{
    unsigned char s[512];
    unsigned long lba;
    unsigned at;
    if (slot(v, parent->first, &lba, &at) != 0 || vio(v, lba, s, 1, 0) != 0)
        return -1;
    memset(s + at, 0, 32);
    memcpy(s + at, name, 11);
    s[at + 11] = (unsigned char)attr;
    wr16(s + at + 22, time), wr16(s + at + 24, date), wr16(s + at + 26, first);
    wr16(s + at + 28, (unsigned)(size & 0xFFFF)), wr16(s + at + 30, (unsigned)(size >> 16));
    return vio(v, lba, s, 1, 1) == 0 ? 0 : -1;
}

int fatimg_mkdir(struct fatimg *v, const char *path, unsigned short date, unsigned short time)
{
    unsigned char s[512], name[11];
    struct found parent;
    unsigned c;
    if (!date)
        date = 0x21;                            /* (no date is no day at all: the first DOS has) */
    if (place(v, path, &parent, name) != 0 || !(c = grab(v, 1)))
        return -1;
    memset(s, 0, sizeof s);                     /* its own two entries: itself, and the folder it is in */
    memset(s, ' ', 11), memset(s + 32, ' ', 11);
    s[0] = '.', s[11] = 0x10, wr16(s + 22, time), wr16(s + 24, date), wr16(s + 26, c);
    s[32] = s[33] = '.', s[32 + 11] = 0x10, wr16(s + 32 + 22, time), wr16(s + 32 + 24, date), wr16(s + 32 + 26, parent.first);
    if (zero_cluster(v, c) != 0 || vio(v, cluster_lba(v, c), s, 1, 1) != 0 || enter(v, &parent, name, c, 0, 0x10, date, time) != 0) {
        let_go(v, c);
        return -1;
    }
    return 0;
}

int fatimg_add(struct fatimg *v, const char *path, const void *data, unsigned long n, unsigned short date, unsigned short time)
{
    const unsigned char *from = data;
    unsigned char s[512], name[11];
    unsigned long bytes = (unsigned long)v->spc * 512, count = (n + bytes - 1) / bytes, done = 0;
    struct found parent;
    unsigned first = 0, c, i;
    if (place(v, path, &parent, name) != 0)
        return -1;
    if (count && !(first = grab(v, count)))
        return -2;
    for (c = first; c && done < n; c = next_of(v, c))
        for (i = 0; i < v->spc && done < n; i++) {
            unsigned long take = n - done < 512 ? n - done : 512;
            memset(s, 0, sizeof s);
            memcpy(s, from + done, take);
            if (vio(v, cluster_lba(v, c) + i, s, 1, 1) != 0) {
                let_go(v, first);
                return -2;
            }
            done += take;
        }
    if (enter(v, &parent, name, first, n, 0x20, date ? date : 0x21, time) != 0) {
        let_go(v, first);
        return -2;
    }
    return 0;
}

/* ---- taken away, renamed */

static int mark_deleted(struct fatimg *v, unsigned long lba, unsigned at)
{
    unsigned char s[512];
    if (vio(v, lba, s, 1, 0) != 0)
        return -1;
    s[at] = 0xE5;
    return vio(v, lba, s, 1, 1) == 0 ? 0 : -1;
}

/* The parts of a long name just before the entry at (lba, at) in the folder `first` marked deleted, and (entry
   nonzero) the entry too. 0; -1: the entry was not found, or the disk failed. */
static int unenter(struct fatimg *v, unsigned first, unsigned long lba, unsigned at, int entry)
{
    unsigned char s[512];
    unsigned long n, l, plba[20];
    unsigned i, pat[20], np = 0, k;
    for (n = 0; n < 65536ul / 16 * 2 && dir_sector(v, first, n, &l) == 0; n++) {
        if (vio(v, l, s, 1, 0) != 0)
            return -1;
        for (i = 0; i < 512; i += 32) {
            if (!s[i])
                return -1;
            if (l == lba && i == at) {
                if (entry && mark_deleted(v, lba, at) != 0)
                    return -1;
                for (k = 0; k < np; k++)
                    if (mark_deleted(v, plba[k], pat[k]) != 0)
                        return -1;
                return 0;
            }
            if (s[i] != 0xE5 && (s[i + 11] & 0x3F) == 0x0F) {
                if (np < 20)
                    plba[np] = l, pat[np++] = i;
            } else {
                np = 0;
            }
        }
    }
    return -1;
}

/* path's folder (up: the path up to its last part) and that part. -1: the root itself, or a path too long. */
static int split(const char *path, char *up, size_t room, const char **leaf)
{
    const char *end = path + strlen(path), *p;
    while (end > path && (end[-1] == '\\' || end[-1] == '/'))
        end--;
    for (p = end; p > path && p[-1] != '\\' && p[-1] != '/'; p--)
        ;
    if (p == end || (size_t)(p - path) >= room)
        return -1;
    memcpy(up, path, (size_t)(p - path));
    up[p - path] = 0;
    *leaf = p;
    return 0;
}

int fatimg_remove(struct fatimg *v, const char *path)
{
    struct found f, parent;
    char up[300];
    const char *leaf;
    if (split(path, up, sizeof up, &leaf) != 0 || find(v, up, &parent) != 0 || find(v, path, &f) != 0)
        return -1;
    if (f.attr & 0x10) {                        /* a folder: only an empty one */
        struct fatimg_entry *e = NULL;
        int n = fatimg_list(v, path, &e);
        free(e);
        if (n != 0)
            return -1;
    }
    if (unenter(v, parent.first, f.lba, f.at, 1) != 0)
        return -1;
    if (f.first)
        let_go(v, f.first);
    return 0;
}

int fatimg_rename(struct fatimg *v, const char *path, const char *name)
{
    struct found f, parent, other;
    unsigned char s[512], dn[11];
    char up[300], to[600];
    const char *leaf;
    if (split(path, up, sizeof up, &leaf) != 0 || find(v, up, &parent) != 0 || find(v, path, &f) != 0
        || dos_name(name, strlen(name), dn) != 0)
        return -1;
    snprintf(to, sizeof to, "%s%s", up, name);
    if (find(v, to, &other) == 0 && (other.lba != f.lba || other.at != f.at))
        return -1;                              /* (another has that name; the same one in other letters is itself) */
    if (vio(v, f.lba, s, 1, 0) != 0)
        return -1;
    memcpy(s + f.at, dn, 11);
    s[f.at + 12] = 0;                           /* (no small letters: the name is as given, in capitals) */
    if (vio(v, f.lba, s, 1, 1) != 0)
        return -1;
    return unenter(v, parent.first, f.lba, f.at, 0);    /* (its long name, which was the old one's) */
}

