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
    static const unsigned info[] = { 0, 0x80D3, 0x80C0, 0xA8C0, 0x80C4 };   /* (4400h's: CON, AUX, PRN, NUL as MS-DOS 7.1) */
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

static int fcb_lru_drop(struct dos_files *t, const struct dos_fcb_k *but);

/* An entry for a handle's open: a free one, or the one the least recently used FCB held (FCBs left open don't
   fill the table). */
static int handle_entry(struct dos_files *t)
{
    int i = free_entry(t);

    if (i < 0 && fcb_lru_drop(t, 0))
        i = free_entry(t);
    return i;
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
    if (hd < 0 || (i = handle_entry(t)) < 0)
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

    if (hd < 0 || (i = handle_entry(t)) < 0)
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

/* ---- 5Ch's locks */

static int same_file(struct dos_files *t, unsigned i, unsigned j)
{
    struct fsv_file *a = &t->k[i].f, *b = &t->k[j].f;

    return i == j || (refs(t, j) && !t->k[j].dev && a->n && b->n && a->v == b->v && a->n->dir == b->n->dir
                      && a->n->index == b->n->index);
}

static int overlap(const struct dos_lock *l, unsigned long off, unsigned long len)
{
    return len && l->len && off <= l->off + (l->len - 1) && l->off <= off + (len - 1);
}

/* Whether n bytes at pos of entry i's file are another's to read and write: locked through another entry, or
   by another program. */
static int locked(struct dos_files *t, unsigned i, unsigned psp, unsigned long pos, unsigned long n)
{
    unsigned k;

    for (k = 0; k < DOS_LOCKS; k++) {
        const struct dos_lock *l = &t->lock[k];

        if (l->used && (l->entry != i || l->psp != psp) && same_file(t, i, l->entry) && overlap(l, pos, n))
            return 1;
    }
    return 0;
}

int dos_lock(struct dos_files *t, unsigned psp, unsigned h, int unlock, unsigned long off, unsigned long len)
{
    int i = held(t, psp, h);
    struct dos_lock *spare = 0;
    unsigned k;

    if (i < 0)
        return DOS_EBADF;
    if (t->k[i].dev)
        return 0x01;
    for (k = 0; k < DOS_LOCKS; k++) {
        struct dos_lock *l = &t->lock[k];

        if (!l->used) {
            if (!spare)
                spare = l;
            continue;
        }
        if (unlock) {
            if (l->entry == (unsigned)i && l->psp == psp && l->off == off && l->len == len) {
                l->used = 0;
                return 0;
            }
        } else if (same_file(t, (unsigned)i, l->entry) && overlap(l, off, len)) {
            return 0x21;
        }
    }
    if (unlock)
        return 0x21;
    if (!spare)
        return 0x24;
    spare->used = 1;
    spare->entry = (unsigned char)i;
    spare->psp = psp;
    spare->off = off;
    spare->len = len;
    return 0;
}

/* One handle's hold on entry i given up: the file closed when none is left, its locks gone. */
static void release(struct dos_files *t, unsigned i)
{
    unsigned n = refs(t, i), k;

    if (n > 1) {
        /* (one of several handles closed: the file's size goes to the disk now, as DOS writes it at each
           close; programs flush a file so, by closing a duplicate of its handle) */
        if (!t->k[i].dev && t->k[i].f.n)
            fsv_commit(&t->k[i].f);
        wr16(entry(t, i), n - 1);
        return;
    }
    for (k = 0; k < DOS_LOCKS; k++)
        if (t->lock[k].used && t->lock[k].entry == i)
            t->lock[k].used = 0;
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
    if (locked(t, (unsigned)i, psp, t->k[i].f.pos, n))
        return -0x21;
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
    if (t->k[i].f.pos >= 0x80000000UL)
        return 0;                               /* (a place before the start: dos_seek's) */
    if (locked(t, (unsigned)i, psp, t->k[i].f.pos, n ? n : 0xFFFFFFFFUL - t->k[i].f.pos))
        return -0x21;                           /* (a cut: everything from there on) */
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
    /* (before the start: kept as MS-DOS 7.1 keeps it, the 32 bits wrapped, and no error; what's read there is
       nothing and nothing is written: GLOS's, to check) */
    fsv_seek(&t->k[i].f, (unsigned long)base + (unsigned long)off);
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

/* ---- FCBs (files.h) */

/* An FCB's entry given up (its file closed); the FCB is still known. */
static void fcb_drop(struct dos_files *t, struct dos_fcb_k *f)
{
    if (f->entry >= 0)
        release(t, (unsigned)f->entry);
    f->entry = -1;
}

/* The least recently used FCB that holds an entry (but isn't `but`) made to give it up: 1, or 0 (there is
   none). */
static int fcb_lru_drop(struct dos_files *t, const struct dos_fcb_k *but)
{
    struct dos_fcb_k *old = 0;
    unsigned k;

    for (k = 0; k < DOS_FCBS; k++) {
        struct dos_fcb_k *f = &t->fcb[k];

        if (f->ticket && f->entry >= 0 && f != but && (!old || (int)(f->stamp - old->stamp) < 0))
            old = f;
    }
    if (!old)
        return 0;
    fcb_drop(t, old);
    return 1;
}

/* f's file opened (create: made or cut to nothing first) in an entry of the table: 0, or a DOS error. */
static int fcb_attach(struct dos_files *t, struct dos_fcb_k *f, int create, unsigned char attr)
{
    unsigned live = 0, k, mode = 0x8002;        /* (bit 15: an FCB's; read-write, compatibility) */
    struct fat_ent fe;
    int i, r;

    for (k = 0; k < DOS_FCBS; k++)
        live += t->fcb[k].ticket && t->fcb[k].entry >= 0 && &t->fcb[k] != f;
    if (live >= DOS_FCB_LIVE)
        fcb_lru_drop(t, f);
    if ((i = free_entry(t)) < 0 && fcb_lru_drop(t, f))
        i = free_entry(t);
    if (i < 0)
        return DOS_ENFILE;
    if (f->dev) {
        set_device(t, (unsigned)i, f->dev, f->psp);
        wr16(entry(t, (unsigned)i) + 2, mode);
    } else {
        if (create) {
            r = fsv_create(f->v, f->path, (unsigned char)(attr & (FAT_A_RDONLY | FAT_A_HIDDEN | FAT_A_SYSTEM)), 1,
                           FSV_RW, FSV_DENY_NONE, &t->k[i].f);
        } else {
            if ((r = share_with(t, f->v, f->path, mode)) != 0)
                return r;
            r = f->ro ? FAT_EACCES : fsv_open(f->v, f->path, FSV_RW, FSV_DENY_NONE, &t->k[i].f);
            if (r == FAT_EACCES && fsv_lookup(f->v, f->path, &fe) == FAT_OK && (fe.attr & FAT_A_RDONLY)) {
                mode = 0x8000;
                f->ro = 1;
                r = fsv_open(f->v, f->path, FSV_READ, FSV_DENY_NONE, &t->k[i].f);
            }
        }
        if (r)
            return dos_err(r);
        fill(t, (unsigned)i, f->v, f->drive, f->path, mode, f->psp);
    }
    wr16(entry(t, (unsigned)i), 1);
    f->entry = i;
    return 0;
}

int dos_fcb_open(struct dos_files *t, unsigned psp, struct fsv_vol *v, unsigned drive, const char *path, int dev,
                 int create, unsigned char attr, unsigned *slot, unsigned *ticket)
{
    struct dos_fcb_k *f = 0, *old = 0;
    unsigned k;
    int r;

    for (k = 0; k < DOS_FCBS && !f; k++) {
        if (!t->fcb[k].ticket)
            f = &t->fcb[k];
        else if (!old || (int)(t->fcb[k].stamp - old->stamp) < 0)
            old = &t->fcb[k];
    }
    if (!f) {                                   /* (all known: the least recently used is forgotten) */
        fcb_drop(t, old);
        f = old;
    }
    memset(f, 0, sizeof *f);
    f->entry = -1;
    f->psp = psp;
    f->v = v;
    f->drive = (unsigned char)drive;
    f->dev = dev;
    strncpy(f->path, path, sizeof f->path - 1);
    if ((r = fcb_attach(t, f, create, attr)) != 0)
        return r;
    if (++t->fcb_ticket > 0xFFFF || !t->fcb_ticket)
        t->fcb_ticket = 1;
    f->ticket = t->fcb_ticket;
    f->stamp = ++t->fcb_stamp;
    *slot = (unsigned)(f - t->fcb);
    *ticket = f->ticket;
    return 0;
}

int dos_fcb_entry(struct dos_files *t, unsigned slot, unsigned ticket)
{
    struct dos_fcb_k *f;

    if (slot >= DOS_FCBS || !ticket || (f = &t->fcb[slot])->ticket != ticket)
        return -1;
    f->stamp = ++t->fcb_stamp;
    if (f->entry < 0 && fcb_attach(t, f, 0, 0) != 0)
        return -1;
    return f->entry;
}

long dos_fcb_read(struct dos_files *t, int i, unsigned long pos, void *buf, unsigned long n)
{
    long r;

    if (i < 0 || (unsigned)i >= t->n || !refs(t, (unsigned)i))
        return -DOS_EBADF;
    if (t->k[i].dev)
        return t->k[i].dev == DOS_DEV_NUL ? 0 : -DOS_EACCES;
    fsv_seek(&t->k[i].f, pos);
    r = fsv_read(&t->k[i].f, buf, n);
    refresh(t, (unsigned)i);
    return r < 0 ? -dos_err((int)r) : r;
}

long dos_fcb_write(struct dos_files *t, int i, unsigned long pos, const void *buf, unsigned long n)
{
    long r;

    if (i < 0 || (unsigned)i >= t->n || !refs(t, (unsigned)i))
        return -DOS_EBADF;
    if (t->k[i].dev)
        return t->k[i].dev == DOS_DEV_NUL ? (long)n : -DOS_EACCES;
    if ((mode_of(t, (unsigned)i) & 7) == 0)
        return -DOS_EACCES;                     /* (a read-only file's) */
    fsv_seek(&t->k[i].f, pos);
    r = n ? fsv_write(&t->k[i].f, buf, n) : fsv_truncate(&t->k[i].f);
    entry(t, (unsigned)i)[5] &= (unsigned char)~0x40;  /* (written) */
    refresh(t, (unsigned)i);
    return r < 0 ? -dos_err((int)r) : r;
}

unsigned long dos_fcb_size(struct dos_files *t, int i)
{
    if (i < 0 || (unsigned)i >= t->n || !refs(t, (unsigned)i) || t->k[i].dev)
        return 0;
    return fsv_size(&t->k[i].f);
}

int dos_fcb_close(struct dos_files *t, unsigned slot, unsigned ticket)
{
    struct dos_fcb_k *f;

    if (slot >= DOS_FCBS || !ticket || (f = &t->fcb[slot])->ticket != ticket)
        return DOS_EBADF;
    fcb_drop(t, f);
    f->ticket = 0;
    return 0;
}

void dos_close_all(struct dos_files *t, unsigned psp)
{
    unsigned size, h;

    jft(t, psp, &size);
    for (h = 0; h < size; h++)
        if (held(t, psp, h) >= 0)
            dos_close(t, psp, h);
    for (h = 0; h < DOS_FCBS; h++)
        if (t->fcb[h].ticket && t->fcb[h].psp == psp) {
            fcb_drop(t, &t->fcb[h]);
            t->fcb[h].ticket = 0;
        }
    for (h = 0; h < DOS_LOCKS; h++)             /* (those on handles a child of its inherited, still open) */
        if (t->lock[h].used && t->lock[h].psp == psp)
            t->lock[h].used = 0;
}
