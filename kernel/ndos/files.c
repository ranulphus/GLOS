/* Files by handle in GLOS's own DOS (files.h). */
#include <string.h>

#include "files.h"
#include "share.h"

static unsigned rd16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }

static void wr16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

static void wr32(unsigned char *p, unsigned long v)
{
    wr16(p, (unsigned)(v & 0xFFFF));
    wr16(p + 2, (unsigned)(v >> 16));
}

static unsigned char *entry(struct dos_files *t, unsigned i)
{
    return t->m + t->table + 6 + (unsigned long)i * DOS_SFT_SIZE;
}

static unsigned refs(struct dos_files *t, unsigned i) { return rd16(entry(t, i)); }
static unsigned mode_of(struct dos_files *t, unsigned i) { return rd16(entry(t, i) + 2); }

/* A program's handle table: its size and where it is (its PSP's 32h and 34h). */
static unsigned char *jft(struct dos_files *t, unsigned psp, unsigned *size)
{
    const unsigned char *p = t->m + (unsigned long)psp * 16;

    *size = rd16(p + 0x32);
    return t->m + (unsigned long)rd16(p + 0x36) * 16 + rd16(p + 0x34);
}

/* The entry handle h of psp holds, or -1. */
static int held(struct dos_files *t, unsigned psp, unsigned h)
{
    unsigned size;
    unsigned char *j = jft(t, psp, &size);

    if (h >= size || j[h] == 0xFF || j[h] >= t->n || !refs(t, j[h]))
        return -1;
    return j[h];
}

int dos_entry(struct dos_files *t, unsigned psp, unsigned h)
{
    return held(t, psp, h);
}

static void set_device(struct dos_files *t, unsigned i, int dev, unsigned owner)
{
    static const unsigned info[] = { 0, 0x80D3, 0x80C0, 0xA8C0, 0x8084 };   /* (4400h's: CON, AUX, PRN, NUL) */
    static const char *const name[] = { "", "CON        ", "AUX        ", "PRN        ", "NUL        " };
    unsigned char *e = entry(t, i);

    memset(e, 0, DOS_SFT_SIZE);
    wr16(e + 2, 0x0002);                        /* (read-write) */
    wr16(e + 5, info[dev]);
    memcpy(e + 0x20, name[dev], 11);
    wr16(e + 0x31, owner);
    t->k[i].dev = dev;
}

void dos_files_init(struct dos_files *t, unsigned char *m, unsigned long table, unsigned n)
{
    unsigned char *h;

    memset(t, 0, sizeof *t);
    t->m = m;
    t->table = table;
    t->n = n > DOS_SFT_MAX ? DOS_SFT_MAX : n;
    h = m + table;
    wr16(h, 0xFFFF);                            /* (the last table: next FFFF:FFFF) */
    wr16(h + 2, 0xFFFF);
    wr16(h + 4, t->n);
    memset(h + 6, 0, (unsigned long)t->n * DOS_SFT_SIZE);
    set_device(t, 0, DOS_DEV_AUX, 0);           /* (DOS's first three: AUX, CON, PRN) */
    set_device(t, 1, DOS_DEV_CON, 0);
    set_device(t, 2, DOS_DEV_PRN, 0);
}

void dos_files_first_jft(struct dos_files *t, unsigned psp)
{
    static const unsigned char first[5] = { 1, 1, 1, 0, 2 };
    unsigned size, i;
    unsigned char *j = jft(t, psp, &size);

    for (i = 0; i < size; i++)
        j[i] = 0xFF;
    for (i = 0; i < 5 && i < size; i++) {
        j[i] = first[i];
        wr16(entry(t, first[i]), refs(t, first[i]) + 1);
    }
}

/* A free handle of psp's, or -1. */
static int free_handle(struct dos_files *t, unsigned psp)
{
    unsigned size, i;
    unsigned char *j = jft(t, psp, &size);

    for (i = 0; i < size; i++)
        if (j[i] == 0xFF)
            return (int)i;
    return -1;
}

static int free_entry(struct dos_files *t)
{
    unsigned i;

    for (i = 0; i < t->n; i++)
        if (!refs(t, i))
            return (int)i;
    return -1;
}

static int dos_err(int r)
{
    switch (r) {
    case FAT_ENOENT: return DOS_ENOENT;
    case FAT_ENOTDIR: return DOS_ENOPATH;
    case FAT_EEXIST: return DOS_EEXIST;
    case FSV_ESHARE: return DOS_ESHARE;
    case FSV_ENFILE: return DOS_ENFILE;
    case FAT_EINVAL: return DOS_ENOENT;
    default: return DOS_EACCES;                 /* (read-only, a folder, the root, no room, the disk's errors) */
    }
}

/* The entry's fields programs read, from its file. */
static void refresh(struct dos_files *t, unsigned i)
{
    unsigned char *e = entry(t, i);
    struct fsv_file *f = &t->k[i].f;

    if (t->k[i].dev || !f->n)
        return;
    wr16(e + 0x0B, (unsigned)(f->n->first & 0xFFFF));
    wr32(e + 0x11, f->n->size);
    wr32(e + 0x15, f->pos);
}

/* An entry for a file just opened: its fields from its directory entry. */
static void fill(struct dos_files *t, unsigned i, struct fsv_vol *v, unsigned drive, const char *path, unsigned mode,
                 unsigned owner)
{
    unsigned char *e = entry(t, i);
    struct fat_ent fe;
    const char *s;
    int k;

    memset(e, 0, DOS_SFT_SIZE);
    wr16(e + 2, mode);
    wr16(e + 5, (drive & 0x3F) | 0x40);         /* (bit 6: not written since it was opened) */
    memset(e + 0x20, ' ', 11);
    if (fsv_lookup(v, path, &fe) == FAT_OK) {
        e[4] = fe.attr;
        wr16(e + 0x0D, fe.wtime);
        wr16(e + 0x0F, fe.wdate);
        for (s = fe.sname, k = 0; *s && *s != '.' && k < 8; s++)
            e[0x20 + k++] = (unsigned char)*s;
        if (*s == '.')
            for (s++, k = 8; *s && k < 11; s++)
                e[0x20 + k++] = (unsigned char)*s;
    }
    wr16(e + 0x31, owner);
    t->k[i].dev = DOS_DEV_NONE;
    refresh(t, i);
}

/* Whether a new open with mode of the file at (dir, index) goes beside the entries already open on it. */
static int share_with(struct dos_files *t, struct fsv_vol *v, const char *path, unsigned mode)
{
    struct fat_ent fe;
    unsigned i;
    int r = fsv_lookup(v, path, &fe);

    if (r)
        return dos_err(r);
    for (i = 0; i < t->n; i++) {
        struct fsv_file *f = &t->k[i].f;

        if (!refs(t, i) || t->k[i].dev || !f->n || f->v != v || f->n->dir != fe.dir || f->n->index != fe.index)
            continue;
        r = dos_share(mode_of(t, i) & 0xFF, mode & 0xFF);
        if (r == DOS_SHARE_NO)
            return DOS_EACCES;
        if (r == DOS_SHARE_INT24)
            return DOS_ESHARE;
    }
    return 0;
}

static int fsv_access(unsigned mode)
{
    unsigned a = mode & 7;

    return a == 1 ? FSV_WRITE : a == 2 ? FSV_RW : FSV_READ;
}

int dos_open(struct dos_files *t, unsigned psp, struct fsv_vol *v, unsigned drive, const char *path, int dev,
             unsigned mode, unsigned *h)
{
    int hd = free_handle(t, psp), i, r;
    unsigned size;
    unsigned char *j = jft(t, psp, &size);

    if ((mode & 7) == 3 || (mode & 7) > 4 || ((mode >> 4) & 7) > 4)
        return DOS_EINVALID;                    /* (an access or sharing code DOS has not: 0Ch) */
    if (hd < 0 || (i = free_entry(t)) < 0)
        return DOS_ENFILE;
    if (dev) {
        set_device(t, (unsigned)i, dev, psp);
        wr16(entry(t, (unsigned)i) + 2, mode);
    } else {
        r = share_with(t, v, path, mode);
        if (r)
            return r;
        r = fsv_open(v, path, fsv_access(mode), FSV_DENY_NONE, &t->k[i].f);
        if (r)
            return dos_err(r);
        fill(t, (unsigned)i, v, drive, path, mode, psp);
    }
    wr16(entry(t, (unsigned)i), 1);
    j[hd] = (unsigned char)i;
    *h = (unsigned)hd;
    return 0;
}

int dos_create(struct dos_files *t, unsigned psp, struct fsv_vol *v, unsigned drive, const char *path,
               unsigned char attr, int replace, unsigned *h)
{
    int hd = free_handle(t, psp), i, r;
    unsigned size;
    unsigned char *j = jft(t, psp, &size);

    if (hd < 0 || (i = free_entry(t)) < 0)
        return DOS_ENFILE;
    r = fsv_create(v, path, (unsigned char)(attr & (FAT_A_RDONLY | FAT_A_HIDDEN | FAT_A_SYSTEM)), replace, FSV_RW,
                   FSV_DENY_NONE, &t->k[i].f);
    if (r)
        return dos_err(r);
    fill(t, (unsigned)i, v, drive, path, 0x0002, psp);
    wr16(entry(t, (unsigned)i), 1);
    j[hd] = (unsigned char)i;
    *h = (unsigned)hd;
    return 0;
}

/* One handle's hold on entry i given up: the file closed when none is left. */
static void release(struct dos_files *t, unsigned i)
{
    unsigned n = refs(t, i);

    if (n > 1) {
        wr16(entry(t, i), n - 1);
        return;
    }
    if (!t->k[i].dev && t->k[i].f.n)
        fsv_close(&t->k[i].f);
    wr16(entry(t, i), 0);
}

int dos_close(struct dos_files *t, unsigned psp, unsigned h)
{
    int i = held(t, psp, h);
    unsigned size;
    unsigned char *j = jft(t, psp, &size);

    if (i < 0)
        return DOS_EBADF;
    j[h] = 0xFF;
    release(t, (unsigned)i);
    return 0;
}

int dos_dup(struct dos_files *t, unsigned psp, unsigned h, unsigned *h2)
{
    int i = held(t, psp, h), n;
    unsigned size;
    unsigned char *j = jft(t, psp, &size);

    if (i < 0)
        return DOS_EBADF;
    if ((n = free_handle(t, psp)) < 0)
        return DOS_ENFILE;
    j[n] = (unsigned char)i;
    wr16(entry(t, (unsigned)i), refs(t, (unsigned)i) + 1);
    *h2 = (unsigned)n;
    return 0;
}

int dos_dup2(struct dos_files *t, unsigned psp, unsigned h, unsigned h2)
{
    int i = held(t, psp, h);
    unsigned size;
    unsigned char *j = jft(t, psp, &size);

    if (i < 0 || h2 >= size)
        return DOS_EBADF;
    if (h2 == h)
        return 0;
    if (held(t, psp, h2) >= 0)
        dos_close(t, psp, h2);
    j[h2] = (unsigned char)i;
    wr16(entry(t, (unsigned)i), refs(t, (unsigned)i) + 1);
    return 0;
}

long dos_read(struct dos_files *t, unsigned psp, unsigned h, void *buf, unsigned long n)
{
    int i = held(t, psp, h);
    long r;

    if (i < 0)
        return -DOS_EBADF;
    if (t->k[i].dev)
        return t->k[i].dev == DOS_DEV_NUL ? 0 : -DOS_EACCES;
    if ((mode_of(t, (unsigned)i) & 7) == 1)
        return -DOS_EACCES;                     /* (write-only) */
    r = fsv_read(&t->k[i].f, buf, n);
    refresh(t, (unsigned)i);
    return r < 0 ? -dos_err((int)r) : r;
}

long dos_write(struct dos_files *t, unsigned psp, unsigned h, const void *buf, unsigned long n)
{
    int i = held(t, psp, h);
    long r;

    if (i < 0)
        return -DOS_EBADF;
    if (t->k[i].dev)
        return t->k[i].dev == DOS_DEV_NUL ? (long)n : -DOS_EACCES;
    if ((mode_of(t, (unsigned)i) & 7) == 0 || (mode_of(t, (unsigned)i) & 7) == 4)
        return -DOS_EACCES;                     /* (read-only) */
    r = n ? fsv_write(&t->k[i].f, buf, n) : fsv_truncate(&t->k[i].f);
    entry(t, (unsigned)i)[5] &= (unsigned char)~0x40;  /* (written) */
    refresh(t, (unsigned)i);
    return r < 0 ? -dos_err((int)r) : r;
}

int dos_seek(struct dos_files *t, unsigned psp, unsigned h, int whence, long off, unsigned long *pos)
{
    int i = held(t, psp, h);
    long base;

    if (i < 0)
        return DOS_EBADF;
    if (whence < 0 || whence > 2)
        return 0x01;                            /* (an origin DOS has not: invalid function) */
    if (t->k[i].dev) {
        *pos = 0;
        return 0;
    }
    base = whence == 0 ? 0 : whence == 1 ? (long)t->k[i].f.pos : (long)fsv_size(&t->k[i].f);
    if (base + off < 0)
        return 0x19;                            /* (before the start: seek error) */
    fsv_seek(&t->k[i].f, (unsigned long)(base + off));
    *pos = t->k[i].f.pos;
    refresh(t, (unsigned)i);
    return 0;
}

void dos_inherit(struct dos_files *t, unsigned parent_psp, unsigned char child_jft[20])
{
    unsigned h;

    for (h = 0; h < 20; h++) {
        int i = held(t, parent_psp, h);

        if (i < 0 || (mode_of(t, (unsigned)i) & 0x80)) {
            child_jft[h] = 0xFF;
            continue;
        }
        child_jft[h] = (unsigned char)i;
        wr16(entry(t, (unsigned)i), refs(t, (unsigned)i) + 1);
    }
}

void dos_close_all(struct dos_files *t, unsigned psp)
{
    unsigned size, h;

    jft(t, psp, &size);
    for (h = 0; h < size; h++)
        if (held(t, psp, h) >= 0)
            dos_close(t, psp, h);
}
