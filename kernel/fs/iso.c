/* ISO 9660, read-only (iso.h). */
#include "iso.h"

static unsigned long le32(const unsigned char *p)
{
    return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

/* The sector at lba into v->buf (kept: a folder's records are read a sector at a time). */
static int sector(struct iso_vol *v, unsigned long lba)
{
    if (v->cached == lba)
        return ISO_OK;
    v->cached = ~0UL;
    if (v->dev.read(v->dev.ctx, lba, 1, v->buf) != 0)
        return ISO_EIO;
    v->cached = lba;
    return ISO_OK;
}

/* A directory record's date (seven bytes: years since 1900, month, day, h, m, s, the zone) as DOS's words. */
static void dos_date(const unsigned char *d, unsigned short *date, unsigned short *time)
{
    int y = d[0] + 1900;
    if (y < 1980)
        y = 1980;
    if (y > 2107)
        y = 2107;
    *date = (unsigned short)((y - 1980) << 9 | (d[1] & 15) << 5 | (d[2] & 31));
    *time = (unsigned short)((d[3] & 31) << 11 | (d[4] & 63) << 5 | (d[5] & 63) / 2);
}

/* A record's name as DOS's 8.3: 0, or -1 if it isn't one. */
static int dos_name(const unsigned char *id, unsigned n, int dir, char out[13])
{
    unsigned i, base = 0, ext = 0, o = 0;
    int dot = 0;
    if (!dir) {                                 /* ";1": the version off; a trailing dot off */
        for (i = 0; i < n && id[i] != ';'; i++)
            ;
        n = i;
        if (n && id[n - 1] == '.')
            n--;
    }
    if (!n)
        return -1;
    for (i = 0; i < n; i++) {
        int c = upper(id[i]);
        if (c == '.') {
            if (dot || dir || !base)
                return -1;
            dot = 1;
            out[o++] = '.';
            continue;
        }
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '~'))
            return -1;
        if (dot ? ++ext > 3 : ++base > 8)
            return -1;
        out[o++] = (char)c;
    }
    out[o] = 0;
    return 0;
}

static void entry_of(const unsigned char *r, struct iso_entry *e)
{
    e->lba = le32(r + 2);
    e->size = le32(r + 10);
    e->dir = (r[25] & 0x02) != 0;
    e->attr = (unsigned char)(0x01 | (e->dir ? 0x10 : 0) | ((r[25] & 0x01) ? 0x02 : 0));
    dos_date(r + 18, &e->date, &e->time);
}

int iso_mount(struct iso_vol *v, const struct iso_dev *dev)
{
    unsigned long lba;
    int i;

    v->dev = *dev;
    v->cached = ~0UL;
    v->skipped = 0;
    for (lba = 16; lba < 16 + 32; lba++) {      /* (the set's descriptors: the primary one; a terminator ends it) */
        if (sector(v, lba) != ISO_OK)
            return ISO_EIO;
        if (v->buf[1] != 'C' || v->buf[2] != 'D' || v->buf[3] != '0' || v->buf[4] != '0' || v->buf[5] != '1')
            return ISO_ENOTISO;
        if (v->buf[0] == 255)
            return ISO_ENOTISO;
        if (v->buf[0] != 1)
            continue;
        if ((v->buf[128] | v->buf[129] << 8) != ISO_SECTOR)
            return ISO_ENOTISO;                 /* (a block size other than 2048: not a CD's) */
        v->sectors = le32(v->buf + 80);
        for (i = 0; i < 32; i++)
            v->label[i] = (char)v->buf[40 + i];
        for (i = 32; i > 0 && (v->label[i - 1] == ' ' || !v->label[i - 1]); i--)
            ;
        v->label[i] = 0;
        entry_of(v->buf + 156, &v->root);
        v->root.name[0] = 0;
        v->root.dir = 1;
        v->root.attr = 0x11;
        return ISO_OK;
    }
    return ISO_ENOTISO;
}

int iso_next(struct iso_vol *v, const struct iso_entry *dir, unsigned long *pos, struct iso_entry *e)
{
    while (*pos < dir->size) {
        unsigned long lba = dir->lba + *pos / ISO_SECTOR;
        unsigned at = (unsigned)(*pos % ISO_SECTOR), len;
        const unsigned char *r;
        if (sector(v, lba) != ISO_OK)
            return ISO_EIO;
        r = v->buf + at;
        len = r[0];
        if (!len || at + len > ISO_SECTOR) {    /* (the sector's end: the next sector's first record) */
            *pos = (*pos / ISO_SECTOR + 1) * ISO_SECTOR;
            continue;
        }
        *pos += len;
        if (len < 34 || 33u + r[32] > len)
            continue;                           /* (a record too short for its name: passed over) */
        if (r[32] == 1 && (r[33] == 0 || r[33] == 1))
            continue;                           /* ("." and "..") */
        if (dos_name(r + 33, r[32], (r[25] & 0x02) != 0, e->name) != 0) {
            v->skipped++;
            continue;
        }
        entry_of(r, e);
        return ISO_OK;
    }
    return 1;
}

int iso_lookup(struct iso_vol *v, const char *path, struct iso_entry *e)
{
    struct iso_entry cur = v->root, up[16];
    int depth = 0;

    for (;;) {
        char part[13];
        unsigned n = 0, i;
        unsigned long pos = 0;
        int r, found = 0;
        while (*path == '\\' || *path == '/')
            path++;
        if (!*path) {
            *e = cur;
            return ISO_OK;
        }
        while (*path && *path != '\\' && *path != '/') {
            if (n < sizeof part - 1)
                part[n++] = (char)upper((unsigned char)*path);
            else
                n = sizeof part;
            path++;
        }
        if (n >= sizeof part)
            return *path ? ISO_ENOPATH : ISO_ENOENT;
        part[n] = 0;
        if (!cur.dir)
            return ISO_ENOPATH;
        if (n == 1 && part[0] == '.')
            continue;
        if (n == 2 && part[0] == '.' && part[1] == '.') {
            if (depth)
                cur = up[--depth];
            continue;
        }
        while ((r = iso_next(v, &cur, &pos, e)) == ISO_OK) {
            for (i = 0; part[i] && part[i] == e->name[i]; i++)
                ;
            if (!part[i] && !e->name[i]) {
                found = 1;
                break;
            }
        }
        if (r == ISO_EIO)
            return ISO_EIO;
        if (!found) {
            while (*path == '\\' || *path == '/')
                path++;
            return *path ? ISO_ENOPATH : ISO_ENOENT;
        }
        if (depth < 16)
            up[depth++] = cur;
        cur = *e;
    }
}

long iso_read(struct iso_vol *v, const struct iso_entry *f, unsigned long off, void *buf, unsigned long n)
{
    unsigned char *out = buf;
    unsigned long done = 0;

    if (off >= f->size)
        return 0;
    if (n > f->size - off)
        n = f->size - off;
    while (done < n) {
        unsigned long lba = f->lba + (off + done) / ISO_SECTOR, at = (off + done) % ISO_SECTOR, k;
        unsigned long take = ISO_SECTOR - at;
        if (take > n - done)
            take = n - done;
        if (!at && n - done >= ISO_SECTOR) {    /* (whole sectors straight into the caller's buffer) */
            unsigned long whole = (n - done) / ISO_SECTOR;
            if (whole > 32)
                whole = 32;
            if (v->dev.read(v->dev.ctx, lba, (unsigned)whole, out + done) != 0)
                return ISO_EIO;
            done += whole * ISO_SECTOR;
            continue;
        }
        if (sector(v, lba) != ISO_OK)
            return ISO_EIO;
        for (k = 0; k < take; k++)
            out[done + k] = v->buf[at + k];
        done += take;
    }
    return (long)done;
}
