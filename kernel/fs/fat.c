/* FAT volumes read by GLOS itself (fat.h; docs/v1/native-dos.md, N1a). Microsoft's FAT specification
 * (FAT12/16/32 and VFAT's long names) and RBIL are the references; FreeDOS's kernel is a behavioural one only
 * (PRD D26). */
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
        if (victim < 0 || v->cache[i].age < v->cache[victim].age)
            victim = i;
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
    if (v->fsinfo && v->fsinfo < rsv) {
        b = sector(v, v->fsinfo, &err);
        if (!b)
            return err;
        if (rd32(b) == 0x41615252UL && rd32(b + 484) == 0x61417272UL && rd32(b + 508) == 0xAA550000UL)
            v->fsinfo_free = rd32(b + 488);
        else
            v->fsinfo = 0;
    } else {
        v->fsinfo = 0;
    }
    return FAT_OK;
}

/* ---- the FAT */

int fat_is_end(const struct fat_vol *v, unsigned long value)
{
    return v->fat == 12 ? value >= 0xFF7 : v->fat == 16 ? value >= 0xFFF7 : value >= 0x0FFFFFF7UL;
}

int fat_get(struct fat_vol *v, unsigned long n, unsigned long *next)
{
    unsigned long off, lba;
    unsigned char *b;
    unsigned val;
    int err = 0;

    if (n < 2 || n > v->clusters + 1)
        return FAT_ECORRUPT;
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

int fat_name_eq(const char *a, const char *b)
{
    while (*a && *b)
        if (fold(cp_utf8_next(&a)) != fold(cp_utf8_next(&b)))
            return 0;
    return !*a && !*b;
}

/* Code point u as UTF-8 at *p, if `room` bytes remain before end: the bytes written (0: no room). */
static int put_utf8(char *p, int room, unsigned u)
{
    if (u < 0x80 && room >= 1) {
        p[0] = (char)u;
        return 1;
    }
    if (u < 0x800 && room >= 2) {
        p[0] = (char)(0xC0 | u >> 6);
        p[1] = (char)(0x80 | (u & 0x3F));
        return 2;
    }
    if (u < 0x10000 && u >= 0x800 && room >= 3) {
        p[0] = (char)(0xE0 | u >> 12);
        p[1] = (char)(0x80 | (u >> 6 & 0x3F));
        p[2] = (char)(0x80 | (u & 0x3F));
        return 3;
    }
    if (u >= 0x10000 && room >= 4) {
        p[0] = (char)(0xF0 | u >> 18);
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
    unsigned u;

    for (i = 0; i < n && in[i]; i++) {
        u = in[i];
        if (u >= 0xD800 && u <= 0xDBFF && i + 1 < n && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            u = 0x10000 + ((u - 0xD800) << 10) + (in[i + 1] - 0xDC00);
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

/* Folder d's entry i (32 bytes in the cache, valid until the next sector read), NULL at the end of its space or
   with *err set. */
static unsigned char *dir_entry(struct fat_dir *d, unsigned long i, int *err)
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
    b = sector(v, lba, err);
    return b ? b + (i % (FAT_SECTOR / 32)) * 32 : 0;
}

int fat_readdir(struct fat_dir *d, struct fat_ent *e)
{
    static const unsigned char at[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
    unsigned short lfn[20 * 13];
    unsigned nseq = 0, have = 0, sum = 0, count = 0, k;
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
            have |= 1u << (seq - 1);
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
        if (nseq && have == (1u << nseq) - 1 && fat_lfn_sum(ent) == sum) {
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
    return FAT_OK;
}

int fat_seek(struct fat_file *f, unsigned long pos)
{
    f->pos = pos;
    return FAT_OK;
}

long fat_read(struct fat_file *f, void *buf, unsigned long n)
{
    struct fat_vol *v = f->v;
    unsigned long csize = v->spc * (unsigned long)FAT_SECTOR, done = 0;
    unsigned char *out = buf;

    if (f->pos >= f->size)
        return 0;
    if (n > f->size - f->pos)
        n = f->size - f->pos;
    while (done < n) {
        unsigned long ord = f->pos / csize, in = f->pos % csize, sec = in / FAT_SECTOR, off = in % FAT_SECTOR;
        unsigned long lba, take;
        int r = chain_at(v, f->first, ord, &f->cl, &f->cl_ord);

        if (r)
            return r > 0 ? FAT_ECORRUPT : r;
        lba = v->data_start + (f->cl - 2) * v->spc + sec;
        if (!off && n - done >= FAT_SECTOR) {
            /* Whole sectors straight into the caller's buffer, as many as this cluster has. */
            unsigned long secs = (n - done) / FAT_SECTOR, left = v->spc - sec;

            if (secs > left)
                secs = left;
            if (lba + secs > v->total)
                return FAT_ECORRUPT;
            if (v->read(v->ctx, lba, out + done, (unsigned)secs))
                return FAT_EIO;
            take = secs * FAT_SECTOR;
        } else {
            int err = 0;
            unsigned char *b = sector(v, lba, &err);

            if (!b)
                return err;
            take = FAT_SECTOR - off;
            if (take > n - done)
                take = n - done;
            memcpy(out + done, b + off, take);
        }
        done += take;
        f->pos += take;
    }
    return (long)done;
}
