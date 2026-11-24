/* FAT volumes read by GLOS itself (fat.h; docs/v1/native-dos.md, N1a). Also built 16-bit (Open Watcom), for
 * GLOS's own loader (N3): `unsigned` may be 16 bits, so code points and masks are longs. Microsoft's FAT specification
 * (FAT12/16/32 and VFAT's long names) and RBIL are the references; FreeDOS's kernel is a behavioural one only
 * (PRD D26). FAT_READ_ONLY leaves the writing half out. */
#include <string.h>

#include <glos/cp.h>
#include "fat.h"

static unsigned rd16(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8;
}

static unsigned long rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

/* ---- sectors, through the volume's few cached ones */

/* Sector lba's 512 bytes, cached: valid until the next call. NULL with *err set if the callback failed or lba is
   past the volume. */
static unsigned char *sector(struct fat_vol *v, unsigned long lba, int *err)
{
    int i, victim = -1;

    if (lba >= v->total) {
        *err = FAT_ECORRUPT;
        return 0;
    }
    for (i = 0; i < FAT_CACHE; i++)
        if (v->cache[i].used && v->cache[i].lba == lba) {
            v->cache[i].age = ++v->clock;
            return v->cache[i].data;
        }
    for (i = 0; i < FAT_CACHE; i++) {
        if (!v->cache[i].used) {
            victim = i;
            break;
        }
        if (victim < 0 || v->cache[i].held < v->cache[victim].held
            || (v->cache[i].held == v->cache[victim].held && v->cache[i].age < v->cache[victim].age))
            victim = i;                         /* (the oldest, one not held before one that is) */
    }
    if (v->cache[victim].used && v->cache[victim].held) {       /* a held sector goes to the disk first */
        if (v->write(v->ctx, v->cache[victim].lba, v->cache[victim].data, 1)) {
            *err = FAT_EIO;
            return 0;
        }
        v->cache[victim].held = 0;
    }
    if (v->read(v->ctx, lba, v->cache[victim].data, 1)) {
        v->cache[victim].used = 0;
        *err = FAT_EIO;
        return 0;
    }
    v->cache[victim].used = 1;
    v->cache[victim].lba = lba;
    v->cache[victim].age = ++v->clock;
    return v->cache[victim].data;
}

static int fat_raw_get(struct fat_vol *v, unsigned long n, unsigned long *next);

/* ---- the volume */

static void copy_label(char *out, const unsigned char *in)
{
    int n = 11;

    memcpy(out, in, 11);
    while (n && out[n - 1] == ' ')
        n--;
    out[n] = 0;
}

int fat_mount(struct fat_vol *v, fat_read_fn read, void *ctx, int cp)
{
    unsigned char *b;
    unsigned bps, rsv, nroot;
    unsigned long fatsz, total, rootsec, used, entries;
    int err = 0;

    memset(v, 0, sizeof *v);
    v->read = read;
    v->ctx = ctx;
    v->cp = cp ? cp : 437;
    v->total = 1;
    b = sector(v, 0, &err);
    if (!b)
        return err;
    if (b[0] != 0xEB && b[0] != 0xE9 && !(b[510] == 0x55 && b[511] == 0xAA))
        return FAT_ENOTFAT;
    bps = rd16(b + 11);
    if (bps != FAT_SECTOR)
        return bps >= 1024 && bps <= 4096 && !(bps & (bps - 1)) ? FAT_ENOTSUP : FAT_ENOTFAT;
    v->spc = b[13];
    rsv = rd16(b + 14);
    v->nfats = b[16];
    nroot = rd16(b + 17);
    total = rd16(b + 19);
    if (!total)
        total = rd32(b + 32);
    fatsz = rd16(b + 22);
    if (!fatsz)
        fatsz = rd32(b + 36);
    if (!v->spc || (v->spc & (v->spc - 1)) || !rsv || !v->nfats || v->nfats > 4 || !fatsz || !total)
        return FAT_ENOTFAT;
    rootsec = ((unsigned long)nroot * 32 + FAT_SECTOR - 1) / FAT_SECTOR;
    used = rsv + v->nfats * fatsz + rootsec;
    if (used >= total)
        return FAT_ENOTFAT;
    v->clusters = (total - used) / v->spc;
    if (!v->clusters)
        return FAT_ENOTFAT;
    /* The type by the count of clusters, as Microsoft's specification has it (not by the label's "FAT16"). */
    v->fat = v->clusters < 4085 ? 12 : v->clusters < 65525 ? 16 : 32;
    /* A FAT too small for its clusters (some formatters round): only the clusters it maps exist. */
    entries = fatsz * FAT_SECTOR * 8 / (unsigned long)v->fat;
    if (v->clusters + 2 > entries)
        v->clusters = entries - 2;
    v->total = total;
    v->fat_start = rsv;
    v->fat0 = rsv;
    v->mirror = 1;
    v->fat_sectors = fatsz;
    v->data_start = used;
    if (v->fat == 32) {
        unsigned flags = rd16(b + 40);

        if (nroot || rd16(b + 22))
            return FAT_ENOTFAT;
        /* Mirroring off: only the FAT the flags name is in use. */
        if (flags & 0x80) {
            if ((flags & 0x0F) >= v->nfats)
                return FAT_ENOTFAT;
            v->fat_start += (flags & 0x0F) * fatsz;
            v->mirror = 0;
        }
        v->root_cluster = rd32(b + 44);
        if (v->root_cluster < 2 || v->root_cluster > v->clusters + 1)
            return FAT_ENOTFAT;
        v->fsinfo = rd16(b + 48);
        if (b[66] == 0x29) {
            v->serial = rd32(b + 67);
            copy_label(v->label, b + 71);
        }
    } else {
        if (!nroot)
            return FAT_ENOTFAT;
        v->root_start = rsv + v->nfats * fatsz;
        v->root_sectors = rootsec;
        if (b[38] == 0x29) {
            v->serial = rd32(b + 39);
            copy_label(v->label, b + 43);
        }
    }
    v->fsinfo_free = 0xFFFFFFFFUL;
    v->free_count = 0xFFFFFFFFUL;
    v->next_free = 2;
    if (v->fsinfo && v->fsinfo < rsv) {
        b = sector(v, v->fsinfo, &err);
        if (!b)
            return err;
        if (rd32(b) == 0x41615252UL && rd32(b + 484) == 0x61417272UL && rd32(b + 508) == 0xAA550000UL) {
            v->fsinfo_free = rd32(b + 488);
            if (v->fsinfo_free <= v->clusters)
                v->free_count = v->fsinfo_free;
            if (rd32(b + 492) >= 2 && rd32(b + 492) <= v->clusters + 1)
                v->next_free = rd32(b + 492);
        } else {
            v->fsinfo = 0;
        }
    } else {
        v->fsinfo = 0;
    }
    /* FAT16's and FAT32's clean-shutdown bit in FAT[1] (FAT12 has none): cleared, the volume needs checking. */
    if (v->fat != 12) {
        unsigned long f1;

        err = fat_raw_get(v, 1, &f1);
        if (err)
            return err;
        v->was_dirty = !(f1 & (v->fat == 16 ? 0x8000UL : 0x08000000UL));
    }
    return FAT_OK;
}

/* ---- the FAT */

int fat_is_end(const struct fat_vol *v, unsigned long value)
{
    return v->fat == 12 ? value >= 0xFF7 : v->fat == 16 ? value >= 0xFFF7 : value >= 0x0FFFFFF7UL;
}

/* FAT entry n as it is, without the range check (FAT[0] and FAT[1] too). */
static int fat_raw_get(struct fat_vol *v, unsigned long n, unsigned long *next)
{
    unsigned long off, lba;
    unsigned char *b;
    unsigned val;
    int err = 0;

    if (v->fat == 12) {
        off = n + n / 2;
        lba = v->fat_start + off / FAT_SECTOR;
        b = sector(v, lba, &err);
        if (!b)
            return err;
        val = b[off % FAT_SECTOR];
        /* An entry across two sectors: its second byte is the next sector's first. */
        if (off % FAT_SECTOR == FAT_SECTOR - 1) {
            b = sector(v, lba + 1, &err);
            if (!b)
                return err;
            val |= (unsigned)b[0] << 8;
        } else {
            val |= (unsigned)b[off % FAT_SECTOR + 1] << 8;
        }
        *next = n & 1 ? val >> 4 : val & 0xFFF;
        return FAT_OK;
    }
    off = n * (v->fat / 8);
    b = sector(v, v->fat_start + off / FAT_SECTOR, &err);
    if (!b)
        return err;
    *next = v->fat == 16 ? rd16(b + off % FAT_SECTOR) : rd32(b + off % FAT_SECTOR) & 0x0FFFFFFFUL;
    return FAT_OK;
}

int fat_get(struct fat_vol *v, unsigned long n, unsigned long *next)
{
    if (n < 2 || n > v->clusters + 1)
        return FAT_ECORRUPT;
    return fat_raw_get(v, n, next);
}

/* The cluster `ord` places along the chain from `first`, into (*cl, *cl_ord), going on from them when they are
   not past it. 0; 1 if the chain ends before; an error (a loop, a cluster off the volume, a free one). */
static int chain_at(struct fat_vol *v, unsigned long first, unsigned long ord, unsigned long *cl, unsigned long *cl_ord)
{
    unsigned long c, o, next, steps = 0;
    int r;

    if (*cl && *cl_ord <= ord) {
        c = *cl;
        o = *cl_ord;
    } else {
        c = first;
        o = 0;
    }
    if (c < 2 || c > v->clusters + 1)
        return FAT_ECORRUPT;
    while (o < ord) {
        r = fat_get(v, c, &next);
        if (r)
            return r;
        if (fat_is_end(v, next))
            return 1;
        if (next < 2 || next > v->clusters + 1 || ++steps > v->clusters)
            return FAT_ECORRUPT;
        c = next;
        o++;
    }
    *cl = c;
    *cl_ord = o;
    return 0;
}

int fat_free(struct fat_vol *v, unsigned long *clusters)
{
    unsigned long n, next, free = 0;
    int r;

    for (n = 2; n < v->clusters + 2; n++) {
        r = fat_get(v, n, &next);
        if (r)
            return r;
        free += !next;
    }
    *clusters = free;
    return FAT_OK;
}

/* ---- names */

unsigned char fat_lfn_sum(const unsigned char name11[11])
{
    unsigned char sum = 0;
    int i;

    for (i = 0; i < 11; i++)
        sum = (unsigned char)(((sum & 1) << 7) + (sum >> 1) + name11[i]);
    return sum;
}

/* The simple upper case of what the DOS code pages hold: Latin (Basic, Latin-1, Extended-A), Greek, Cyrillic. */
static unsigned fold(unsigned u)
{
    if (u >= 'a' && u <= 'z')
        return u - 32;
    if (u < 0x80)
        return u;
    if (u >= 0xE0 && u <= 0xFE && u != 0xF7)
        return u - 32;
    if (u == 0xFF)
        return 0x178;
    if (u >= 0x100 && u <= 0x17F) {
        if (u == 0x131 || u == 0x138 || u == 0x149 || u == 0x17F)
            return u;
        /* Between 139h and 148h, and 179h and 17Eh, the capitals are the odd ones; elsewhere the even ones. */
        if ((u >= 0x139 && u <= 0x148) || (u >= 0x179 && u <= 0x17E))
            return u & 1 ? u : u - 1;
        return u & 1 ? u - 1 : u;
    }
    if (u == 0x3AC)
        return 0x386;
    if (u >= 0x3AD && u <= 0x3AF)
        return u - 37;
    if (u == 0x3CC)
        return 0x38C;
    if (u == 0x3CD || u == 0x3CE)
        return u - 63;
    if (u == 0x3C2)
        return 0x3A3;
    if (u >= 0x3B1 && u <= 0x3CB)
        return u - 32;
    if (u >= 0x430 && u <= 0x44F)
        return u - 32;
    if (u >= 0x450 && u <= 0x45F)
        return u - 80;
    return u;
}

unsigned fat_fold(unsigned u)
{
    return fold(u);
}

int fat_name_eq(const char *a, const char *b)
{
    while (*a && *b)
        if (fold(cp_utf8_next(&a)) != fold(cp_utf8_next(&b)))
            return 0;
    return !*a && !*b;
}

/* Code point u as UTF-8 at *p, if `room` bytes remain before end: the bytes written (0: no room). */
static int put_utf8(char *p, int room, unsigned long u)
{
    if (u < 0x80 && room >= 1) {
        p[0] = (char)u;
        return 1;
    }
    if (u < 0x800 && room >= 2) {
        p[0] = (char)(0xC0 | (u >> 6));
        p[1] = (char)(0x80 | (u & 0x3F));
        return 2;
    }
    if (u < 0x10000UL && u >= 0x800 && room >= 3) {
        p[0] = (char)(0xE0 | (u >> 12));
        p[1] = (char)(0x80 | (u >> 6 & 0x3F));
        p[2] = (char)(0x80 | (u & 0x3F));
        return 3;
    }
    if (u >= 0x10000UL && room >= 4) {
        p[0] = (char)(0xF0 | (u >> 18));
        p[1] = (char)(0x80 | (u >> 12 & 0x3F));
        p[2] = (char)(0x80 | (u >> 6 & 0x3F));
        p[3] = (char)(0x80 | (u & 0x3F));
        return 4;
    }
    return 0;
}

/* A long name's UTF-16 (n units, ended early by 0000h) as UTF-8 into out (FAT_NAME_MAX bytes). */
static void lfn_to_utf8(const unsigned short *in, int n, char *out)
{
    int i, o = 0, w;
    unsigned long u;

    for (i = 0; i < n && in[i]; i++) {
        u = in[i];
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            u = 0x10000UL + ((u - 0xD800) << 10) + (unsigned long)(in[i + 1] - 0xDC00);
            i++;
        } else if (u >= 0xD800 && u <= 0xDFFF) {
            u = 0xFFFD;
        }
        w = put_utf8(out + o, FAT_NAME_MAX - 1 - o, u);
        if (!w)
            break;
        o += w;
    }
    out[o] = 0;
}

/* A short entry's 11 bytes as "NAME.EXT" (the code page's bytes), lowered where NT's case bits say (`lower`). */
static void short_name(const unsigned char *ent, char *out, int lower)
{
    int i, n = 0, base = 8, ext = 3;

    while (base && ent[base - 1] == ' ')
        base--;
    while (ext && ent[8 + ext - 1] == ' ')
        ext--;
    for (i = 0; i < base; i++) {
        unsigned char c = i == 0 && ent[0] == 0x05 ? 0xE5 : ent[i];
        out[n++] = (char)(lower & 0x08 && c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    if (ext) {
        out[n++] = '.';
        for (i = 0; i < ext; i++) {
            unsigned char c = ent[8 + i];
            out[n++] = (char)(lower & 0x10 && c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
    }
    out[n] = 0;
}

/* ---- folders */

/* Folder d's entry i (32 bytes in the cache, valid until the next sector read) and, if lba isn't 0, its sector;
   NULL at the end of its space or with *err set. */
static unsigned char *dir_slot(struct fat_dir *d, unsigned long i, unsigned long *lbap, int *err)
{
    struct fat_vol *v = d->v;
    unsigned long per = v->spc * (FAT_SECTOR / 32), lba;
    unsigned char *b;
    int r;

    if (!d->first && v->fat != 32) {
        if (i >= v->root_sectors * (FAT_SECTOR / 32))
            return 0;
        lba = v->root_start + i / (FAT_SECTOR / 32);
    } else {
        r = chain_at(v, d->first ? d->first : v->root_cluster, i / per, &d->cl, &d->cl_ord);
        if (r > 0)
            return 0;
        if (r < 0) {
            *err = r;
            return 0;
        }
        lba = v->data_start + (d->cl - 2) * v->spc + (i % per) / (FAT_SECTOR / 32);
    }
    if (lbap)
        *lbap = lba;
    b = sector(v, lba, err);
    return b ? b + (i % (FAT_SECTOR / 32)) * 32 : 0;
}

static unsigned char *dir_entry(struct fat_dir *d, unsigned long i, int *err)
{
    return dir_slot(d, i, 0, err);
}

int fat_readdir(struct fat_dir *d, struct fat_ent *e)
{
    static const unsigned char at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    unsigned short lfn[20 * 13];
    unsigned nseq = 0, sum = 0, count = 0, k;
    unsigned long have = 0;
    unsigned char ent[32];

    for (;;) {
        int err = 0;
        unsigned char *p = dir_entry(d, d->index, &err);

        if (!p)
            return err;
        memcpy(ent, p, 32);
        if (!ent[0])
            return 0;
        d->index++;
        if (ent[0] == 0xE5) {
            nseq = 0;
            continue;
        }
        if ((ent[11] & 0x3F) == FAT_A_LFN) {
            unsigned seq = ent[0] & 0x1F;

            if (ent[0] & 0x40) {
                nseq = seq;
                sum = ent[13];
                have = 0;
                count = 0;
                for (k = 0; k < sizeof lfn / sizeof lfn[0]; k++)
                    lfn[k] = 0;
            }
            if (!nseq || !seq || seq > nseq || seq > 20 || ent[13] != sum) {
                nseq = 0;
                continue;
            }
            for (k = 0; k < 13; k++)
                lfn[(seq - 1) * 13 + k] = (unsigned short)rd16(ent + at[k]);
            have |= 1UL << (seq - 1);
            count++;
            continue;
        }
        if (ent[11] & FAT_A_LABEL) {
            nseq = 0;
            continue;
        }
        memset(e, 0, sizeof *e);
        short_name(ent, e->sname, 0);
        e->attr = ent[11];
        e->size = rd32(ent + 28);
        e->cluster = rd16(ent + 26) | (d->v->fat == 32 ? (unsigned long)rd16(ent + 20) << 16 : 0);
        e->ctime_10ms = ent[13];
        e->ctime = (unsigned short)rd16(ent + 14);
        e->cdate = (unsigned short)rd16(ent + 16);
        e->adate = (unsigned short)rd16(ent + 18);
        e->wtime = (unsigned short)rd16(ent + 22);
        e->wdate = (unsigned short)rd16(ent + 24);
        e->dir = d->first;
        e->index = d->index - 1;
        if (nseq && have == (1UL << nseq) - 1 && fat_lfn_sum(ent) == sum) {
            lfn_to_utf8(lfn, (int)nseq * 13, e->name);
            e->has_long = e->name[0] != 0;
            e->lfn_entries = count;
        }
        if (!e->has_long) {
            char lowered[13];

            short_name(ent, lowered, ent[12]);
            cp_to_utf8(d->v->cp, lowered, e->name, FAT_NAME_MAX);
        }
        return 1;
    }
}

/* Folder `first` (0: the root) searched for `want` by either name: 0 with *e, FAT_ENOENT, or an error. */
static int find_in(struct fat_vol *v, unsigned long first, const char *want, struct fat_ent *e)
{
    struct fat_dir d;
    char sname[FAT_NAME_MAX];
    int r;

    memset(&d, 0, sizeof d);
    d.v = v;
    d.first = first;
    while ((r = fat_readdir(&d, e)) > 0) {
        if (fat_name_eq(e->name, want))
            return FAT_OK;
        cp_to_utf8(v->cp, e->sname, sname, sizeof sname);
        if (fat_name_eq(sname, want))
            return FAT_OK;
    }
    return r < 0 ? r : FAT_ENOENT;
}

static void root_ent(struct fat_ent *e)
{
    memset(e, 0, sizeof *e);
    e->name[0] = '\\';
    e->sname[0] = '\\';
    e->attr = FAT_A_DIR;
}

int fat_lookup(struct fat_vol *v, const char *path, struct fat_ent *e)
{
    char part[FAT_NAME_MAX];
    unsigned long cur = 0;
    int is_dir = 1, r;

    root_ent(e);
    for (;;) {
        unsigned n = 0;

        while (*path == '\\' || *path == '/')
            path++;
        if (!*path)
            return FAT_OK;
        if (!is_dir)
            return FAT_ENOTDIR;
        while (*path && *path != '\\' && *path != '/') {
            if (*path == '*' || *path == '?' || *path == ':' || n + 1 >= sizeof part)
                return FAT_EINVAL;
            part[n++] = *path++;
        }
        part[n] = 0;
        /* The root has no "." or ".." of its own: both are the root again. */
        if (!cur && (!strcmp(part, ".") || !strcmp(part, ".."))) {
            root_ent(e);
            continue;
        }
        r = find_in(v, cur, part, e);
        if (r)
            return r;
        is_dir = (e->attr & FAT_A_DIR) != 0;
        cur = is_dir ? e->cluster : 0;
        if (is_dir && !cur)
            root_ent(e);
    }
}

int fat_opendir(struct fat_vol *v, const char *path, struct fat_dir *d)
{
    struct fat_ent e;
    int r = fat_lookup(v, path, &e);

    if (r)
        return r;
    if (!(e.attr & FAT_A_DIR))
        return FAT_ENOTDIR;
    memset(d, 0, sizeof *d);
    d->v = v;
    d->first = e.cluster;
    return FAT_OK;
}

/* ---- files */

int fat_open(struct fat_vol *v, const char *path, struct fat_file *f)
{
    struct fat_ent e;
    int r = fat_lookup(v, path, &e);

    if (r)
        return r;
    if (e.attr & FAT_A_DIR)
        return FAT_EISDIR;
    if (e.size && !e.cluster)
        return FAT_ECORRUPT;
    memset(f, 0, sizeof *f);
    f->v = v;
    f->first = e.cluster;
    f->size = e.size;
    f->dir = e.dir;
    f->index = e.index;
    return FAT_OK;
}

int fat_seek(struct fat_file *f, unsigned long pos)
{
    f->pos = pos;
    return FAT_OK;
}

#define GET_RUN 64                      /* sectors in one read of a file's data, at most */

long fat_read(struct fat_file *f, void *buf, unsigned long n)
{
    struct fat_vol *v = f->v;
    unsigned long csize = v->spc * (unsigned long)FAT_SECTOR, done = 0;
    /* Whole sectors go straight into the caller's buffer, and those of clusters that follow one another on the
       disk in one read (GET_RUN sectors at most: a disk read through a BIOS pays for every call; the owner's
       PCChips M925G took 12 ms for each from a USB stick): the run so far, its place in the buffer, and the file's
       position at its start (where the file is left if the read fails). */
    unsigned long run_lba = 0, run_secs = 0, run_done = 0, run_pos = 0;
    unsigned char *out = buf;
    int r = FAT_OK;

    if (f->pos >= f->size)
        return 0;
    if (n > f->size - f->pos)
        n = f->size - f->pos;
    while (done < n) {
        unsigned long ord = f->pos / csize, in = f->pos % csize, sec = in / FAT_SECTOR, off = in % FAT_SECTOR;
        unsigned long lba, take;

        r = chain_at(v, f->first, ord, &f->cl, &f->cl_ord);
        if (r) {
            r = r > 0 ? FAT_ECORRUPT : r;
            break;
        }
        lba = v->data_start + (f->cl - 2) * v->spc + sec;
        if (!off && n - done >= FAT_SECTOR) {
            unsigned long secs = (n - done) / FAT_SECTOR, left = v->spc - sec;

            if (secs > left)
                secs = left;
            if (lba + secs > v->total) {
                r = FAT_ECORRUPT;
                break;
            }
            if (run_secs && run_lba + run_secs == lba && run_secs + secs <= GET_RUN) {
                run_secs += secs;
            } else {
                if (run_secs && v->read(v->ctx, run_lba, out + run_done, (unsigned)run_secs)) {
                    r = FAT_EIO;
                    break;
                }
                run_lba = lba, run_secs = secs, run_done = done, run_pos = f->pos;
            }
            take = secs * FAT_SECTOR;
        } else {
            int err = 0;
            unsigned char *b;

            if (run_secs && v->read(v->ctx, run_lba, out + run_done, (unsigned)run_secs)) {
                r = FAT_EIO;
                break;
            }
            run_secs = 0;
            b = sector(v, lba, &err);
            if (!b) {
                r = err;
                break;
            }
            take = FAT_SECTOR - off;
            if (take > n - done)
                take = n - done;
            memcpy(out + done, b + off, take);
        }
        done += take;
        f->pos += take;
    }
    if (run_secs && r != FAT_EIO) {             /* the run still to read */
        if (v->read(v->ctx, run_lba, out + run_done, (unsigned)run_secs))
            r = FAT_EIO;
        else
            run_secs = 0;
    }
    if (run_secs)                               /* a run that wasn't read: the file is left before it */
        f->pos = run_pos;
    return r ? r : (long)done;
}

/* ---- writing (N1b); left out of a build with FAT_READ_ONLY (GLOS's own loader, N3, reads only) */
#ifndef FAT_READ_ONLY

#define FREE_UNKNOWN 0xFFFFFFFFUL

static void wr16(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void wr32(unsigned char *p, unsigned long v)
{
    wr16(p, v & 0xFFFF);
    wr16(p + 2, v >> 16);
}

/* Sector lba written from data (a cached sector the caller changed in place). */
/* A sector written. During a file's write (hold) a FAT's sector stays in the cache instead, marked, and is
   written once when the write's data is out (held_out) or when the cache needs its place: a file of many clusters
   changes the same FAT sectors for each of them. The order a volume stays repairable by is kept: the FATs' entries
   before the directory entry that points at the chain. */
static int put_sector(struct fat_vol *v, unsigned long lba, const unsigned char *data)
{
    int i;

    for (i = 0; i < FAT_CACHE; i++)
        if (v->cache[i].used && v->cache[i].lba == lba && v->cache[i].data == data) {
            if (v->hold && lba >= v->fat0 && lba < v->fat0 + v->nfats * v->fat_sectors) {
                v->cache[i].held = 1;
                return FAT_OK;
            }
            v->cache[i].held = 0;
        }
    return v->write(v->ctx, lba, data, 1) ? FAT_EIO : FAT_OK;
}

/* The held sectors written, in the disk's order. */
static int held_out(struct fat_vol *v)
{
    int r = FAT_OK;

    for (;;) {
        int i, next = -1;

        for (i = 0; i < FAT_CACHE; i++)
            if (v->cache[i].used && v->cache[i].held && (next < 0 || v->cache[i].lba < v->cache[next].lba))
                next = i;
        if (next < 0)
            return r;
        v->cache[next].held = 0;
        if (v->write(v->ctx, v->cache[next].lba, v->cache[next].data, 1))
            r = FAT_EIO;
    }
}

/* count sectors written straight from buf; the cache's copies of any of them brought up to date. */
static int write_run(struct fat_vol *v, unsigned long lba, const unsigned char *buf, unsigned long count)
{
    int i;

    if (lba + count > v->total)
        return FAT_ECORRUPT;
    if (v->write(v->ctx, lba, buf, (unsigned)count))
        return FAT_EIO;
    for (i = 0; i < FAT_CACHE; i++)
        if (v->cache[i].used && v->cache[i].lba >= lba && v->cache[i].lba < lba + count) {
            memcpy(v->cache[i].data, buf + (v->cache[i].lba - lba) * FAT_SECTOR, FAT_SECTOR);
            v->cache[i].held = 0;
        }
    return FAT_OK;
}

static unsigned long eoc(const struct fat_vol *v)
{
    return v->fat == 12 ? 0xFFF : v->fat == 16 ? 0xFFFF : 0x0FFFFFFFUL;
}

/* A cut entry's value is safe if it is the old or the new one, or past the volume's clusters (not the bad mark):
   fsck and ScanDisk end the chain there, which is the old state for a link and the new one for an end. */
static int safe12(const struct fat_vol *v, unsigned long x, unsigned long oldv, unsigned long newv)
{
    return x == oldv || x == newv || (x >= v->clusters + 2 && x != 0xFF7);
}

/* A FAT12 entry across two sectors (byte 511 of lba and byte 0 of the next) can't be written at once: a power
   cut between the two leaves it half old and half new. The half written first is the one whose half-way value
   is safe (safe12): writing the low byte first is safe for a link (end -> next: 0xFxx), the high first for an
   end (next -> end); either for a free entry no chain reaches. */
static int fat12_split(struct fat_vol *v, unsigned long n, unsigned long lba, unsigned long value)
{
    unsigned char *b, x0, x1, n0, n1;
    unsigned long oldv, lo, hi;
    int err = 0, r, k, hi_first;

    b = sector(v, lba, &err);
    if (!b)
        return err;
    x0 = b[FAT_SECTOR - 1];
    b = sector(v, lba + 1, &err);
    if (!b)
        return err;
    x1 = b[0];
    if (n & 1) {
        n0 = (unsigned char)((x0 & 0x0F) | ((value << 4) & 0xF0));
        n1 = (unsigned char)((value >> 4) & 0xFF);
        oldv = (unsigned long)(x0 >> 4) | (unsigned long)x1 << 4;
        lo = (unsigned long)(n0 >> 4) | (unsigned long)x1 << 4;
        hi = (unsigned long)(x0 >> 4) | (unsigned long)n1 << 4;
    } else {
        n0 = (unsigned char)(value & 0xFF);
        n1 = (unsigned char)((x1 & 0xF0) | ((value >> 8) & 0x0F));
        oldv = x0 | (unsigned long)(x1 & 0x0F) << 8;
        lo = n0 | (unsigned long)(x1 & 0x0F) << 8;
        hi = x0 | (unsigned long)(n1 & 0x0F) << 8;
    }
    hi_first = !safe12(v, lo, oldv, value) && safe12(v, hi, oldv, value);
    for (k = 0; k < 2; k++) {
        int second = (k == 0) == hi_first;      /* (this pass writes the next sector's byte) */

        b = sector(v, lba + (unsigned long)second, &err);
        if (!b)
            return err;
        if (second)
            b[0] = n1;
        else
            b[FAT_SECTOR - 1] = n0;
        r = put_sector(v, lba + (unsigned long)second, b);
        if (r)
            return r;
    }
    return FAT_OK;
}

/* One FAT copy's entry n set (the FAT starting at sector start). */
static int fat_put1(struct fat_vol *v, unsigned long start, unsigned long n, unsigned long value)
{
    unsigned long off, lba;
    unsigned char *b;
    int err = 0;

    if (v->fat == 12) {
        off = n + n / 2;
        lba = start + off / FAT_SECTOR;
        b = sector(v, lba, &err);
        if (!b)
            return err;
        if (n & 1)
            b[off % FAT_SECTOR] = (unsigned char)((b[off % FAT_SECTOR] & 0x0F) | ((value << 4) & 0xF0));
        else
            b[off % FAT_SECTOR] = (unsigned char)value;
        if (off % FAT_SECTOR != FAT_SECTOR - 1) {
            unsigned char *c = b + off % FAT_SECTOR + 1;

            *c = (unsigned char)(n & 1 ? (value >> 4) & 0xFF : (*c & 0xF0) | ((value >> 8) & 0x0F));
            return put_sector(v, lba, b);
        }
        return fat12_split(v, n, lba, value);
    }
    off = n * (v->fat / 8);
    lba = start + off / FAT_SECTOR;
    b = sector(v, lba, &err);
    if (!b)
        return err;
    if (v->fat == 16)
        wr16(b + off % FAT_SECTOR, value);
    else
        wr32(b + off % FAT_SECTOR, (rd32(b + off % FAT_SECTOR) & 0xF0000000UL) | (value & 0x0FFFFFFFUL));
    return put_sector(v, lba, b);
}

/* Entry n set in every FAT copy written (FAT32 with mirroring off: the active one alone). */
static int fat_put(struct fat_vol *v, unsigned long n, unsigned long value)
{
    unsigned k;
    int r;

    if (!v->mirror)
        return fat_put1(v, v->fat_start, n, value);
    for (k = 0; k < v->nfats; k++) {
        r = fat_put1(v, v->fat0 + k * v->fat_sectors, n, value);
        if (r)
            return r;
    }
    return FAT_OK;
}

/* Before the first write since mounting (or flushing): FAT16's and FAT32's clean bit cleared on the disk. */
static int begin_write(struct fat_vol *v)
{
    unsigned long f1;
    int r;

    if (!v->write)
        return FAT_EROFS;
    if (v->dirty)
        return FAT_OK;
    if (v->fat != 12) {
        r = fat_raw_get(v, 1, &f1);
        if (!r)
            r = fat_put(v, 1, f1 & ~(v->fat == 16 ? 0x8000UL : 0x08000000UL));
        if (r)
            return r;
    }
    v->dirty = 1;
    return FAT_OK;
}

int fat_flush(struct fat_vol *v)
{
    unsigned long f1;
    unsigned char *b;
    int r, err = 0;

    if (!v->write)
        return FAT_EROFS;
    if ((r = held_out(v)) != 0)
        return r;
    if (v->fsinfo) {
        b = sector(v, v->fsinfo, &err);
        if (!b)
            return err;
        wr32(b + 488, v->free_count);
        wr32(b + 492, v->next_free);
        r = put_sector(v, v->fsinfo, b);
        if (r)
            return r;
    }
    if (v->dirty && !v->was_dirty && v->fat != 12) {
        r = fat_raw_get(v, 1, &f1);
        if (!r)
            r = fat_put(v, 1, f1 | (v->fat == 16 ? 0x8000UL : 0x08000000UL));
        if (r)
            return r;
    }
    v->dirty = 0;
    return FAT_OK;
}

int fat_mount_rw(struct fat_vol *v, fat_read_fn read, fat_write_fn write, fat_time_fn now, void *ctx, int cp)
{
    int r = fat_mount(v, read, ctx, cp);

    if (r)
        return r;
    v->write = write;
    v->now = now;
    return FAT_OK;
}

static unsigned long now_of(const struct fat_vol *v)
{
    return v->now ? v->now(v->ctx) : 0;
}

/* A free cluster, marked as a chain's end, then linked after prev (0: none). The search starts at the hint. */
static int alloc_cluster(struct fat_vol *v, unsigned long prev, unsigned long *out)
{
    unsigned long n = v->next_free, tries, val;
    int r;

    if (n < 2 || n > v->clusters + 1)
        n = 2;
    for (tries = 0; tries < v->clusters; tries++, n = n >= v->clusters + 1 ? 2 : n + 1) {
        r = fat_get(v, n, &val);
        if (r)
            return r;
        if (val)
            continue;
        r = fat_put(v, n, eoc(v));
        if (!r && prev)
            r = fat_put(v, prev, n);
        if (r)
            return r;
        v->next_free = n + 1 > v->clusters + 1 ? 2 : n + 1;
        if (v->free_count != FREE_UNKNOWN && v->free_count)
            v->free_count--;
        *out = n;
        return FAT_OK;
    }
    return FAT_ENOSPC;
}

/* The chain from first freed. (Its directory entry no longer points at it: the caller saw to that first.) */
static int free_chain(struct fat_vol *v, unsigned long first)
{
    unsigned long n = first, next, steps = 0;
    int r;

    while (n >= 2 && n <= v->clusters + 1) {
        r = fat_get(v, n, &next);
        if (!r)
            r = fat_put(v, n, 0);
        if (r)
            return r;
        if (v->free_count != FREE_UNKNOWN && v->free_count < v->clusters)
            v->free_count++;
        if (n < v->next_free)
            v->next_free = n;
        if (!next || fat_is_end(v, next) || ++steps > v->clusters)
            break;
        n = next;
    }
    return FAT_OK;
}

/* A cluster's sectors written with zeros. */
static int zero_cluster(struct fat_vol *v, unsigned long cl)
{
    static const unsigned char zero[FAT_SECTOR];
    unsigned long lba = v->data_start + (cl - 2) * v->spc, i;
    int r;

    for (i = 0; i < v->spc; i++) {
        r = write_run(v, lba + i, zero, 1);
        if (r)
            return r;
    }
    return FAT_OK;
}

/* ---- names for writing */

/* Not in a long name: controls and \ / : * ? " < > |. */
static int bad_long(unsigned long u)
{
    return u < 0x20 || u == '\\' || u == '/' || u == ':' || u == '*' || u == '?' || u == '"' || u == '<' ||
           u == '>' || u == '|';
}

/* Not in a short name either: + , ; = [ ] and the space (and the dot, which only parts name from extension). */
static int bad_short(unsigned long u)
{
    return bad_long(u) || u == '+' || u == ',' || u == ';' || u == '=' || u == '[' || u == ']' || u == ' ' ||
           u == '.';
}

/* A short entry's 11 bytes in folder dir: 1 if some entry has them, 0 if none, or an error. */
static int short_taken(struct fat_vol *v, unsigned long dir, const unsigned char name11[11])
{
    struct fat_dir d;
    unsigned long i;
    unsigned char *p;
    int err = 0;

    memset(&d, 0, sizeof d);
    d.v = v;
    d.first = dir;
    for (i = 0; (p = dir_entry(&d, i, &err)) != 0; i++) {
        if (!p[0])
            return 0;
        if (p[0] != 0xE5 && (p[11] & 0x3F) != FAT_A_LFN && !memcmp(p, name11, 11))
            return 1;
    }
    return err;
}

#define TAIL_SEEN 1024                  /* the tails ~1 to ~1024 found in one pass; past them, a pass a number */

/* One pass over folder dir: whether a short entry has the 11 bytes `plain` (*taken), and which of the tails ~1 to
   ~TAIL_SEEN that base (its nb bytes) would take with ext (3 bytes) are there, as bits in seen. */
static int scan_names(struct fat_vol *v, unsigned long dir, const unsigned char plain[11], const unsigned char *base,
                      int nb, const unsigned char ext[3], int *taken, unsigned char seen[TAIL_SEEN / 8])
{
    struct fat_dir d;
    unsigned long i, num;
    unsigned char *p;
    int err = 0, t, k, nd;

    memset(&d, 0, sizeof d);
    d.v = v;
    d.first = dir;
    *taken = 0;
    memset(seen, 0, TAIL_SEEN / 8);
    for (i = 0; (p = dir_entry(&d, i, &err)) != 0; i++) {
        if (!p[0])
            break;
        if (p[0] == 0xE5 || (p[11] & 0x3F) == FAT_A_LFN || (p[11] & FAT_A_LABEL))
            continue;                           /* (the volume's label is no file's name: a folder GLOS on a volume
                                                   named GLOS is GLOS, as under DOS) */
        if (!memcmp(p, plain, 11))
            *taken = 1;
        if (memcmp(p + 8, ext, 3))
            continue;
        for (t = 1; t < 7 && p[t] != '~'; t++)
            ;
        if (p[t] != '~' || p[t + 1] < '1' || p[t + 1] > '9')
            continue;
        for (num = 0, k = t + 1; k < 8 && p[k] >= '0' && p[k] <= '9'; k++)
            num = num * 10 + (unsigned long)(p[k] - '0');
        nd = k - t - 1;
        while (k < 8 && p[k] == ' ')
            k++;
        if (k < 8 || t != (7 - nd < nb ? 7 - nd : nb) || memcmp(p, base, (unsigned)t))
            continue;
        if (num <= TAIL_SEEN)
            seen[(num - 1) / 8] |= (unsigned char)(1u << ((num - 1) % 8));
    }
    return err;
}

/* A long name's UTF-16 into u16 (room 255 units): how many units, or FAT_EINVAL. Trailing dots and spaces go, as
   Windows drops them. */
static int to_utf16(const char *name, unsigned short *u16)
{
    const char *p = name;
    int n = 0;

    while (*p) {
        unsigned long u = cp_utf8_next(&p);

        if (bad_long(u))
            return FAT_EINVAL;
        if (u >= 0x10000UL) {
            if (n + 2 > 255)
                return FAT_EINVAL;
            u -= 0x10000UL;
            u16[n++] = (unsigned short)(0xD800 + (u >> 10));
            u16[n++] = (unsigned short)(0xDC00 + (u & 0x3FF));
        } else {
            if (n + 1 > 255)
                return FAT_EINVAL;
            u16[n++] = (unsigned short)u;
        }
    }
    while (n && (u16[n - 1] == '.' || u16[n - 1] == ' '))
        n--;
    if (!n || (n == 1 && u16[0] == '.') || (n == 2 && u16[0] == '.' && u16[1] == '.'))
        return FAT_EINVAL;
    return n;
}

/* fat_short_name's work, the name already UTF-16 (n units). */
static int short_of(struct fat_vol *v, unsigned long dir, const unsigned short *u16, int n, unsigned char out[11],
                    int *lfn)
{
    unsigned char base[8], ext[3];
    int i, dot = -1, nb = 0, ne = 0, lossy = 0, cased = 0, fits = 1, start = 0, r;
    unsigned long tail;

    for (i = 0; i < n; i++)
        if (u16[i] == '.')
            dot = i;
    if (dot == 0)
        dot = -1;                               /* (".hidden": all base, its dot dropped) */
    while (start < n && (u16[start] == '.' || u16[start] == ' ')) {
        start++;
        lossy = 1;
    }
    for (i = start; i < n; i++) {
        unsigned long u = u16[i], up = fold(u);
        int b, is_ext = dot >= 0 && i > dot;

        if (i == dot)
            continue;
        if (u >= 0xD800 && u <= 0xDFFF) {
            b = -1;
        } else {
            b = bad_short(u) ? -1 : cp_byte(v->cp, up);
        }
        if (u == ' ' || (u == '.' && i != dot)) {
            lossy = 1;                          /* (spaces and other dots: left out) */
            continue;
        }
        if (b < 0) {
            b = '_';
            lossy = 1;
        } else if (up != u) {
            cased = 1;
        }
        if (is_ext) {
            if (ne < 3)
                ext[ne++] = (unsigned char)b;
            else
                fits = 0;
        } else {
            if (nb < 8)
                base[nb++] = (unsigned char)b;
            else
                fits = 0;
        }
    }
    if (!nb) {
        base[nb++] = '_';
        lossy = 1;
    }
    if (base[0] == 0xE5)
        base[0] = 0x05;                         /* (0xE5 marks a deleted entry: DOS keeps the byte as 0x05) */
    memset(out, ' ', 11);
    memcpy(out, base, nb);
    memcpy(out + 8, ext, ne);
    *lfn = lossy || !fits || cased;
    {
        unsigned char plain[11], seen[TAIL_SEEN / 8];
        int taken;

        memcpy(plain, out, 11);
        r = scan_names(v, dir, plain, base, nb, plain + 8, &taken, seen);
        if (r)
            return r;
        if (fits && !lossy && !taken)
            return FAT_OK;
        for (tail = 1; tail <= TAIL_SEEN && (seen[(tail - 1) / 8] >> ((tail - 1) % 8) & 1); tail++)
            ;
    }
    /* A numeric tail (Windows 9x's): ~1, ~2 ... the base cut short to make room; the first free one (past
       TAIL_SEEN, a pass over the folder each). */
    for (; tail < 1000000UL; tail++) {
        char t[8];
        int nt = 0, keep;
        unsigned long k = tail;

        t[nt++] = '~';
        {
            char dg[7];
            int nd = 0;

            do
                dg[nd++] = (char)('0' + k % 10);
            while (k /= 10);
            while (nd)
                t[nt++] = dg[--nd];
        }
        keep = 8 - nt < nb ? 8 - nt : nb;
        memset(out, ' ', 8);
        memcpy(out, base, keep);
        memcpy(out + keep, t, nt);
        r = tail <= TAIL_SEEN ? 0 : short_taken(v, dir, out);
        if (r < 0)
            return r;
        if (!r) {
            *lfn = 1;
            return FAT_OK;
        }
    }
    return FAT_ENOSPC;
}

int fat_short_name(struct fat_vol *v, unsigned long dir, const char *name, unsigned char out[11], int *lfn)
{
    unsigned short u16[255];
    int n = to_utf16(name, u16);

    return n < 0 ? n : short_of(v, dir, u16, n, out, lfn);
}

/* ---- directory entries */

/* Entry index of folder dir: 32 bytes in the cache, and its sector. */
static unsigned char *slot_of(struct fat_vol *v, unsigned long dir, unsigned long index, unsigned long *lba, int *err)
{
    struct fat_dir d;

    memset(&d, 0, sizeof d);
    d.v = v;
    d.first = dir;
    return dir_slot(&d, index, lba, err);
}

/* Entry index of folder dir written with ent's 32 bytes. */
static int put_slot(struct fat_vol *v, unsigned long dir, unsigned long index, const unsigned char ent[32])
{
    unsigned long lba;
    int err = 0;
    unsigned char *p = slot_of(v, dir, index, &lba, &err);

    if (!p)
        return err ? err : FAT_ECORRUPT;
    memcpy(p, ent, 32);
    return put_sector(v, lba, p - (index % (FAT_SECTOR / 32)) * 32);
}

/* ... and read into ent. */
static int get_slot(struct fat_vol *v, unsigned long dir, unsigned long index, unsigned char ent[32])
{
    int err = 0;
    unsigned char *p = slot_of(v, dir, index, 0, &err);

    if (!p)
        return err ? err : FAT_ECORRUPT;
    memcpy(ent, p, 32);
    return FAT_OK;
}

/* Folder dir (not FAT12's or FAT16's root) one cluster longer: a free cluster zeroed, then linked after its last. */
static int grow_dir(struct fat_vol *v, unsigned long dir)
{
    unsigned long last = dir ? dir : v->root_cluster, next, fresh, steps = 0;
    int r;

    for (;;) {
        r = fat_get(v, last, &next);
        if (r)
            return r;
        if (fat_is_end(v, next))
            break;
        if (next < 2 || next > v->clusters + 1 || ++steps > v->clusters)
            return FAT_ECORRUPT;
        last = next;
    }
    r = alloc_cluster(v, 0, &fresh);
    if (!r)
        r = zero_cluster(v, fresh);
    if (!r)
        r = fat_put(v, last, fresh);
    return r;
}

/* n free entries in a row in folder dir (deleted ones, or past its end): the first's index. The folder grows
   where it has no room (FAT12's and FAT16's root can't: FAT_ENOSPC). */
static int find_slots(struct fat_vol *v, unsigned long dir, unsigned n, unsigned long *first)
{
    struct fat_dir d;
    unsigned long i = 0, run = 0, start = 0, grown = 0xFFFFFFFFUL;
    unsigned char *p;
    int err = 0, r;

    memset(&d, 0, sizeof d);
    d.v = v;
    d.first = dir;
    for (;;) {
        if (i >= 65536UL)
            return FAT_ENOSPC;                  /* (a folder holds 65,536 entries at most) */
        p = dir_entry(&d, i, &err);
        if (!p) {
            if (err)
                return err;
            if (!dir && v->fat != 32)
                return FAT_ENOSPC;
            if (i == grown)
                return FAT_ECORRUPT;            /* grown, and still no entry i: the link didn't hold */
            r = grow_dir(v, dir);
            if (r)
                return r;
            grown = i;
            d.cl = d.cl_ord = 0;
            continue;                           /* (entry i is there now, free: the run goes on into it) */
        }
        if (!p[0] || p[0] == 0xE5) {
            if (!run++)
                start = i;
            if (run == n) {
                *first = start;
                return FAT_OK;
            }
        } else {
            run = 0;
        }
        i++;
    }
}

/* A name's entries written into folder dir: its long-name entries (where it needs them), then its short entry
   with tmpl's bytes 11-31 (attributes, times, cluster, size). *index: the short entry's. */
static int add_entry(struct fat_vol *v, unsigned long dir, const char *name, const unsigned char tmpl[32],
                     unsigned long *index)
{
    static const unsigned char at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    unsigned short u16[255];
    unsigned char sn[11], ent[32], sum;
    unsigned long first;
    int n, lfn, nl, k, c, r;

    n = to_utf16(name, u16);
    if (n < 0)
        return n;
    r = short_of(v, dir, u16, n, sn, &lfn);
    if (r)
        return r;
    nl = lfn ? (n + 12) / 13 : 0;
    r = find_slots(v, dir, (unsigned)nl + 1, &first);
    if (r)
        return r;
    sum = fat_lfn_sum(sn);
    for (k = 0; k < nl; k++) {
        int seq = nl - k;

        memset(ent, 0, 32);
        ent[0] = (unsigned char)(seq | (k == 0 ? 0x40 : 0));
        ent[11] = FAT_A_LFN;
        ent[13] = sum;
        for (c = 0; c < 13; c++) {
            int i = (seq - 1) * 13 + c;

            wr16(ent + at[c], i < n ? u16[i] : i == n ? 0x0000 : 0xFFFF);
        }
        r = put_slot(v, dir, first + (unsigned long)k, ent);
        if (r)
            return r;
    }
    memcpy(ent, tmpl, 32);
    memcpy(ent, sn, 11);
    r = put_slot(v, dir, first + (unsigned long)nl, ent);
    if (r)
        return r;
    *index = first + (unsigned long)nl;
    return FAT_OK;
}

/* A template short entry: attributes, created, written and accessed now, cluster, size (name left blank). */
static void make_tmpl(struct fat_vol *v, unsigned char t[32], unsigned char attr, unsigned long cluster,
                      unsigned long size)
{
    unsigned long now = now_of(v);

    memset(t, 0, 32);
    memset(t, ' ', 11);
    t[11] = attr;
    wr16(t + 14, now & 0xFFFF);                 /* created */
    wr16(t + 16, now >> 16);
    wr16(t + 18, now >> 16);                    /* accessed */
    wr16(t + 20, v->fat == 32 ? cluster >> 16 : 0);
    wr16(t + 22, now & 0xFFFF);                 /* written */
    wr16(t + 24, now >> 16);
    wr16(t + 26, cluster & 0xFFFF);
    wr32(t + 28, size);
}

/* path's folder (its first cluster, 0 the root) and its last part, into name (FAT_NAME_MAX bytes): FAT_ENOENT,
   FAT_ENOTDIR, FAT_EINVAL. */
static int split(struct fat_vol *v, const char *path, unsigned long *dir, char *name)
{
    const char *p = path, *last = 0, *part;
    struct fat_ent e;
    unsigned n, k = 0;
    int r;

    for (; *p; p++)
        if ((*p == '\\' || *p == '/') && p[1] && p[1] != '\\' && p[1] != '/')
            last = p;
    if (!last) {
        while (*path == '\\' || *path == '/')
            path++;
        *dir = 0;
        part = path;
    } else {
        /* The folder (its path, borrowed into name for the look-up). */
        n = (unsigned)(last - path);
        if (n + 1 > FAT_NAME_MAX)
            return FAT_EINVAL;
        memcpy(name, path, n);
        name[n] = 0;
        r = fat_lookup(v, name, &e);
        if (r)
            return r;
        if (!(e.attr & FAT_A_DIR))
            return FAT_ENOTDIR;
        *dir = e.cluster;
        part = last + 1;
    }
    /* The last part, a trailing separator dropped ("a\\b\\" is "a\\b"). */
    while (part[k] && part[k] != '\\' && part[k] != '/') {
        if (k + 1 >= FAT_NAME_MAX)
            return FAT_EINVAL;
        name[k] = part[k];
        k++;
    }
    name[k] = 0;
    if (!k || !strcmp(name, ".") || !strcmp(name, ".."))
        return FAT_EINVAL;
    return FAT_OK;
}

int fat_create(struct fat_vol *v, const char *path, unsigned char attr, struct fat_file *f)
{
    char name[FAT_NAME_MAX];
    unsigned char t[32];
    unsigned long dir, index;
    struct fat_ent e;
    int r = begin_write(v);

    if (r)
        return r;
    r = split(v, path, &dir, name);
    if (r)
        return r;
    if (fat_lookup(v, path, &e) == FAT_OK)
        return FAT_EEXIST;
    make_tmpl(v, t, (unsigned char)((attr & (FAT_A_RDONLY | FAT_A_HIDDEN | FAT_A_SYSTEM)) | FAT_A_ARCHIVE), 0, 0);
    r = add_entry(v, dir, name, t, &index);
    if (r)
        return r;
    memset(f, 0, sizeof *f);
    f->v = v;
    f->dir = dir;
    f->index = index;
    return FAT_OK;
}

/* The file's entry brought up to date: its first cluster, size, written time and the archive bit. */
static int update_file(struct fat_file *f)
{
    struct fat_vol *v = f->v;
    unsigned char ent[32];
    unsigned long now = now_of(v);
    int r;

    if ((r = held_out(v)) != 0)                 /* (the FATs a write left in the cache, before the entry that
                                                   points at their chain: fat_write_later) */
        return r;
    if (f->dir == FAT_NO_ENTRY) {               /* (a deleted file's: no entry to keep up) */
        f->changed = 0;
        return FAT_OK;
    }
    if ((r = get_slot(v, f->dir, f->index, ent)) != 0)
        return r;
    wr16(ent + 20, v->fat == 32 ? f->first >> 16 : 0);
    wr16(ent + 26, f->first & 0xFFFF);
    wr32(ent + 28, f->size);
    wr16(ent + 22, now & 0xFFFF);
    wr16(ent + 24, now >> 16);
    wr16(ent + 18, now >> 16);
    ent[11] |= FAT_A_ARCHIVE;
    r = put_slot(v, f->dir, f->index, ent);
    if (!r)
        f->changed = 0;
    return r;
}

int fat_close(struct fat_file *f)
{
    return f->changed ? update_file(f) : FAT_OK;
}

/* The cluster holding byte pos, the chain grown by one where pos is just past its end. */
static int cluster_for(struct fat_file *f, unsigned long ord)
{
    struct fat_vol *v = f->v;
    unsigned long c;
    int r;

    if (!f->first) {
        r = alloc_cluster(v, 0, &c);
        if (r)
            return r;
        f->first = c;
        f->cl = c;
        f->cl_ord = 0;
        return ord ? cluster_for(f, ord) : FAT_OK;
    }
    r = chain_at(v, f->first, ord, &f->cl, &f->cl_ord);
    if (r <= 0)
        return r;
    /* The chain ends before ord (chain_at left cl where it was): along it to its last cluster, then one more
       after it each time round. */
    if (!f->cl || f->cl_ord > ord) {
        f->cl = f->first;
        f->cl_ord = 0;
    }
    while (f->cl_ord < ord) {
        unsigned long next;

        r = fat_get(v, f->cl, &next);
        if (r)
            return r;
        if (!fat_is_end(v, next)) {
            if (next < 2 || next > v->clusters + 1)
                return FAT_ECORRUPT;
            f->cl = next;
            f->cl_ord++;
            continue;
        }
        r = alloc_cluster(v, f->cl, &c);
        if (r)
            return r;
        f->cl = c;
        f->cl_ord++;
    }
    return FAT_OK;
}

#define PUT_RUN 128                     /* sectors in one write of a file's data, at most */

/* n bytes from buf (0: zeros) at the file's position, no size or entry change. */
static long put_bytes(struct fat_file *f, const unsigned char *buf, unsigned long n)
{
    static const unsigned char zero[FAT_SECTOR];
    struct fat_vol *v = f->v;
    unsigned long csize = v->spc * (unsigned long)FAT_SECTOR, done = 0;
    /* Whole sectors of clusters that follow one another on the disk go out in one write (PUT_RUN sectors at
       most): the run so far, its place, and the file's position at its start (where the file ends if it fails). */
    unsigned long run_lba = 0, run_secs = 0, run_pos = 0, run_done = 0;
    int r = FAT_OK;

    while (done < n) {
        unsigned long ord = f->pos / csize, in = f->pos % csize, sec = in / FAT_SECTOR, off = in % FAT_SECTOR;
        unsigned long lba, take;

        r = cluster_for(f, ord);
        if (r)
            break;
        lba = v->data_start + (f->cl - 2) * v->spc + sec;
        if (!off && n - done >= FAT_SECTOR) {
            unsigned long secs = (n - done) / FAT_SECTOR, left = v->spc - sec, k;

            if (secs > left)
                secs = left;
            if (buf) {
                if (run_secs && run_lba + run_secs == lba && run_secs + secs <= PUT_RUN) {
                    run_secs += secs;
                } else {
                    if (run_secs && (r = write_run(v, run_lba, buf + run_done, run_secs)) != 0)
                        break;
                    run_lba = lba, run_secs = secs, run_pos = f->pos, run_done = done;
                }
            } else {
                for (k = 0; k < secs && !r; k++)
                    r = write_run(v, lba + k, zero, 1);
                if (r)
                    break;
            }
            take = secs * FAT_SECTOR;
        } else {
            int err = 0;
            unsigned char *b;

            if (run_secs && (r = write_run(v, run_lba, buf + run_done, run_secs)) != 0)
                break;
            run_secs = 0;
            b = sector(v, lba, &err);
            if (!b) {
                r = err;
                break;
            }
            take = FAT_SECTOR - off;
            if (take > n - done)
                take = n - done;
            if (buf)
                memcpy(b + off, buf + done, take);
            else
                memset(b + off, 0, take);
            r = put_sector(v, lba, b);
            if (r)
                break;
        }
        done += take;
        f->pos += take;
    }
    if (run_secs && r != FAT_EIO) {             /* the run still to write (a full disk stopped the loop, or its end) */
        int w = write_run(v, run_lba, buf + run_done, run_secs);

        if (w)
            r = w;
        else
            run_secs = 0;
    }
    if (run_secs)                               /* a run that wasn't written: the file ends before it */
        f->pos = run_pos;
    return r ? r : (long)done;
}

/* What a put_bytes that stopped part way wrote still counts: the size covers it (DOS's short write when the disk
   is full). The entry is brought up to date after the data, whatever came of it. */
/* A write's work. later: what it changed in the FATs stays in the cache (their sectors held) and the file's
   entry as it was (f->changed), for fat_commit; else both are written before it returns. */
static long write_file(struct fat_file *f, const void *buf, unsigned long n, int later)
{
    unsigned long pos = f->pos, start;
    long r = 0;
    int e = begin_write(f->v);

    if (e)
        return e;
    if (!n)
        return 0;
    if (pos > f->size) {                        /* the gap: zeros */
        f->pos = f->size;
        r = put_bytes(f, 0, pos - f->size);
        if (f->pos > f->size) {
            f->size = f->pos;
            f->changed = 1;
        }
    }
    start = f->pos;
    if (r >= 0) {
        f->v->hold = f->v->fat != 12;           /* (FAT12's entries across two sectors keep their own order) */
        r = put_bytes(f, buf, n);
        f->v->hold = 0;
        e = later ? FAT_OK : held_out(f->v);    /* the FATs, before the entry */
        if (e)
            return e;
    }
    if (f->pos > f->size)
        f->size = f->pos;
    if (f->pos != start)
        f->changed = 1;
    e = f->changed && !later ? update_file(f) : FAT_OK;
    if (e)
        return e;
    return f->pos != start ? (long)(f->pos - start) : r < 0 ? r : 0;
}

long fat_write(struct fat_file *f, const void *buf, unsigned long n) { return write_file(f, buf, n, 0); }

long fat_write_later(struct fat_file *f, const void *buf, unsigned long n) { return write_file(f, buf, n, 1); }

int fat_commit(struct fat_file *f)
{
    return f->changed ? update_file(f) : held_out(f->v);
}

int fat_truncate(struct fat_file *f, unsigned long size)
{
    struct fat_vol *v = f->v;
    unsigned long csize = v->spc * (unsigned long)FAT_SECTOR, keep, rest, pos = f->pos;
    int r = begin_write(v);

    if (r)
        return r;
    if (size > f->size) {
        long n;

        f->pos = f->size;
        n = put_bytes(f, 0, size - f->size);
        f->pos = pos;
        if (n < 0)
            return (int)n;
        f->size = size;
        return update_file(f);
    }
    if (size == f->size)
        return FAT_OK;
    keep = (size + csize - 1) / csize;
    f->size = size;
    if (!keep) {
        rest = f->first;
        f->first = 0;
        r = update_file(f);                     /* (the entry first: it no longer points at the chain) */
        f->cl = f->cl_ord = 0;
        return r ? r : free_chain(v, rest);
    }
    /* The entry's smaller size first (a chain longer than its file is repairable, a shorter one loses data), then
       the chain's new end, then the rest freed. */
    r = update_file(f);
    if (!r)
        r = chain_at(v, f->first, keep - 1, &f->cl, &f->cl_ord);
    if (r)
        return r > 0 ? FAT_ECORRUPT : r;
    r = fat_get(v, f->cl, &rest);
    if (!r)
        r = fat_put(v, f->cl, eoc(v));
    if (!r && rest && !fat_is_end(v, rest))
        r = free_chain(v, rest);
    return r;
}

/* An entry and its long-name entries marked deleted: the short entry first, then the long-name entries from the
   nearest back to the first (which has the start bit), so that what a power cut leaves is always a run that
   starts right and has no short entry after it (an orphan fsck and ScanDisk delete), never fragments without
   their start in front of a live entry (which fsck.fat won't touch). */
static int del_entry(struct fat_vol *v, const struct fat_ent *e)
{
    unsigned char ent[32];
    unsigned long k;
    int r;

    for (k = 0; k <= e->lfn_entries; k++) {
        unsigned long i = e->index - k;

        r = get_slot(v, e->dir, i, ent);
        if (!r) {
            ent[0] = 0xE5;
            r = put_slot(v, e->dir, i, ent);
        }
        if (r)
            return r;
    }
    return FAT_OK;
}

/* path's entry, a file or a folder but never the root (FAT_EACCES). */
static int entry_of(struct fat_vol *v, const char *path, struct fat_ent *e)
{
    int r = fat_lookup(v, path, e);

    if (r)
        return r;
    if (e->name[0] == '\\' && !e->name[1])
        return FAT_EACCES;
    if (!strcmp(e->sname, ".") || !strcmp(e->sname, ".."))
        return FAT_EINVAL;
    return FAT_OK;
}

int fat_unlink(struct fat_vol *v, const char *path)
{
    struct fat_ent e;
    int r = begin_write(v);

    if (!r)
        r = entry_of(v, path, &e);
    if (r)
        return r;
    if (e.attr & FAT_A_DIR)
        return FAT_EISDIR;
    if (e.attr & FAT_A_RDONLY)
        return FAT_EACCES;
    r = del_entry(v, &e);                       /* (the entry first, then its clusters) */
    return r ? r : e.cluster ? free_chain(v, e.cluster) : FAT_OK;
}

int fat_unlink_keep(struct fat_vol *v, const char *path, unsigned long *first)
{
    struct fat_ent e;
    int r = begin_write(v);

    if (!r)
        r = entry_of(v, path, &e);
    if (r)
        return r;
    if (e.attr & FAT_A_DIR)
        return FAT_EISDIR;
    if (e.attr & FAT_A_RDONLY)
        return FAT_EACCES;
    *first = e.cluster;
    return del_entry(v, &e);
}

int fat_free_chain(struct fat_vol *v, unsigned long first)
{
    int r = begin_write(v);

    return r ? r : first ? free_chain(v, first) : FAT_OK;
}

int fat_mkdir(struct fat_vol *v, const char *path)
{
    char name[FAT_NAME_MAX];
    unsigned char t[32], dot[32];
    unsigned long dir, c, index;
    struct fat_ent e;
    int r = begin_write(v);

    if (!r)
        r = split(v, path, &dir, name);
    if (r)
        return r;
    if (fat_lookup(v, path, &e) == FAT_OK)
        return FAT_EEXIST;
    r = alloc_cluster(v, 0, &c);
    if (r)
        return r;
    r = zero_cluster(v, c);
    if (!r) {
        make_tmpl(v, dot, FAT_A_DIR, c, 0);
        memcpy(dot, ".          ", 11);
        r = put_slot(v, c, 0, dot);
    }
    if (!r) {
        make_tmpl(v, dot, FAT_A_DIR, dir, 0);
        memcpy(dot, "..         ", 11);
        r = put_slot(v, c, 1, dot);
    }
    if (!r) {
        make_tmpl(v, t, FAT_A_DIR, c, 0);
        r = add_entry(v, dir, name, t, &index);
    }
    if (r)
        free_chain(v, c);
    return r;
}

int fat_rmdir(struct fat_vol *v, const char *path)
{
    struct fat_ent e, x;
    struct fat_dir d;
    int r = begin_write(v);

    if (!r)
        r = entry_of(v, path, &e);
    if (r)
        return r;
    if (!(e.attr & FAT_A_DIR))
        return FAT_ENOTDIR;
    memset(&d, 0, sizeof d);
    d.v = v;
    d.first = e.cluster;
    while ((r = fat_readdir(&d, &x)) > 0)
        if (strcmp(x.sname, ".") && strcmp(x.sname, ".."))
            return FAT_ENOTEMPTY;
    if (r < 0)
        return r;
    r = del_entry(v, &e);
    return r ? r : free_chain(v, e.cluster);
}

int fat_rename(struct fat_vol *v, const char *from, const char *to)
{
    char name[FAT_NAME_MAX];
    unsigned char ent[32], dd[32];
    unsigned long dir, index, up, steps = 0;
    struct fat_ent e, t;
    int r = begin_write(v), same;

    if (!r)
        r = entry_of(v, from, &e);
    if (!r)
        r = split(v, to, &dir, name);
    if (r)
        return r;
    same = 0;
    if (fat_lookup(v, to, &t) == FAT_OK) {
        if (t.dir != e.dir || t.index != e.index)
            return FAT_EEXIST;
        same = 1;                               /* (the same entry: another case of its name) */
    }
    /* A folder never into itself: the new parent and its parents, up to the root, are not it. */
    for (up = dir; (e.attr & FAT_A_DIR) && up; ) {
        if (up == e.cluster)
            return FAT_EINVAL;
        if (++steps > v->clusters)
            return FAT_ECORRUPT;
        r = get_slot(v, up, 1, dd);             /* its "..": its parent */
        if (r)
            return r;
        if (memcmp(dd, "..         ", 11))
            return FAT_ECORRUPT;
        up = rd16(dd + 26) | (v->fat == 32 ? (unsigned long)rd16(dd + 20) << 16 : 0);
    }
    r = get_slot(v, e.dir, e.index, ent);
    if (r)
        return r;
    ent[12] = 0;                                /* (NT's case bits: the old name's) */
    if (same) {
        r = del_entry(v, &e);                   /* its old entries go first, so its short name is free again */
        return r ? r : add_entry(v, dir, name, ent, &index);
    }
    /* In steps, so that a cut between any two (a power cut) leaves no cluster in two chains and no folder whose
       ".." is wrong: the new name first as an empty file (a cut: the old as it was, and an empty file); the old
       entry gone (a cut: its chain lost, which a check finds and keeps); a folder moved, its ".." the new parent
       while its chain is lost; then the new entry given the chain, the size and the attributes. (Until
       2026-11-23 the name was put with its chain first, then the old went, then the ".." was mended: a cut
       between the first two left both names on one chain, the native-dos session's find, and one before the
       last a ".." fsck.fat calls invalid.) */
    {
        unsigned char empty[32];
        memcpy(empty, ent, 32);
        empty[11] &= (unsigned char)~(FAT_A_DIR | FAT_A_LABEL);
        wr16(empty + 20, 0);
        wr16(empty + 26, 0);
        memset(empty + 28, 0, 4);
        r = add_entry(v, dir, name, empty, &index);
    }
    if (r)
        return r;
    if ((r = del_entry(v, &e)) != 0) {
        struct fat_ent n;                       /* (the old can't go: the new name taken away again) */
        if (fat_lookup(v, to, &n) == FAT_OK)
            (void)del_entry(v, &n);
        return r;
    }
    if ((e.attr & FAT_A_DIR) && dir != e.dir) {
        r = get_slot(v, e.cluster, 1, dd);      /* its "..": the new parent */
        if (!r) {
            wr16(dd + 20, v->fat == 32 ? dir >> 16 : 0);
            wr16(dd + 26, dir & 0xFFFF);
            r = put_slot(v, e.cluster, 1, dd);
        }
    }
    {                                           /* (on: a ".." not mended still leaves the folder found) */
        unsigned char now[32];
        int r2 = get_slot(v, dir, index, now);  /* (its short name and checksum add_entry's: kept) */
        if (!r2) {
            memcpy(now + 11, ent + 11, 21);
            r2 = put_slot(v, dir, index, now);
        }
        if (!r)
            r = r2;
    }
    return r;
}

static int change_entry(struct fat_vol *v, const char *path, int what, unsigned a, unsigned b)
{
    unsigned char ent[32];
    struct fat_ent e;
    int r = begin_write(v);

    if (!r)
        r = entry_of(v, path, &e);
    if (!r)
        r = get_slot(v, e.dir, e.index, ent);
    if (r)
        return r;
    if (what == 0) {
        ent[11] = (unsigned char)((ent[11] & (FAT_A_DIR | FAT_A_LABEL)) |
                                  (a & (FAT_A_RDONLY | FAT_A_HIDDEN | FAT_A_SYSTEM | FAT_A_ARCHIVE)));
    } else {
        wr16(ent + 24, a);
        wr16(ent + 22, b);
    }
    return put_slot(v, e.dir, e.index, ent);
}

int fat_setattr(struct fat_vol *v, const char *path, unsigned char attr)
{
    return change_entry(v, path, 0, attr, 0);
}

int fat_settime(struct fat_vol *v, const char *path, unsigned short date, unsigned short time)
{
    return change_entry(v, path, 1, date, time);
}

#endif /* FAT_READ_ONLY */
