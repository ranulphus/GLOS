/* Many callers on a FAT volume (fsv.h; docs/v1/native-dos.md, N1d). */
#include <string.h>

#include "fsv.h"

static void lock(struct fsv_vol *v)
{
    if (v->lk.lock)
        v->lk.lock(v->lk.ctx);
}

static void unlock(struct fsv_vol *v)
{
    if (v->lk.unlock)
        v->lk.unlock(v->lk.ctx);
}

static int reads(int access) { return access == FSV_READ || access == FSV_RW; }
static int writes(int access) { return access == FSV_WRITE || access == FSV_RW; }
static int denies_read(int share) { return share == FSV_DENY_ALL || share == FSV_DENY_READ; }
static int denies_write(int share) { return share == FSV_DENY_ALL || share == FSV_DENY_WRITE; }

/* The node of the file whose short entry is (dir, index), or 0 if it isn't open. */
static struct fsv_node *node_at(struct fsv_vol *v, unsigned long dir, unsigned long index)
{
    int i;

    for (i = 0; i < FSV_NODES; i++)
        if (v->node[i].refs && v->node[i].dir == dir && v->node[i].index == index)
            return &v->node[i];
    return 0;
}

static int is_open(struct fsv_vol *v, const struct fat_ent *e)
{
    return !(e->attr & FAT_A_DIR) && node_at(v, e->dir, e->index) != 0;
}

/* Whether a new open (access, share) conflicts with those already on n. */
static int conflict(const struct fsv_node *n, int access, int share)
{
    return (reads(access) && n->deny_read) || (writes(access) && n->deny_write) ||
           (denies_read(share) && n->readers) || (denies_write(share) && n->writers);
}

/* A handle on the file of entry e: its node made, or shared. FSV_ESHARE, FSV_ENFILE. */
static int attach(struct fsv_vol *v, const struct fat_ent *e, int access, int share, struct fsv_file *f)
{
    struct fsv_node *n = node_at(v, e->dir, e->index);
    int i;

    if (n) {
        if (conflict(n, access, share))
            return FSV_ESHARE;
    } else {
        for (i = 0; i < FSV_NODES && v->node[i].refs; i++)
            ;
        if (i == FSV_NODES)
            return FSV_ENFILE;
        n = &v->node[i];
        memset(n, 0, sizeof *n);
        n->dir = e->dir;
        n->index = e->index;
        n->first = e->cluster;
        n->size = e->size;
    }
    n->refs++;
    n->readers += (unsigned)reads(access);
    n->writers += (unsigned)writes(access);
    n->deny_read += (unsigned)denies_read(share);
    n->deny_write += (unsigned)denies_write(share);
    memset(f, 0, sizeof *f);
    f->v = v;
    f->n = n;
    f->access = access;
    f->share = share;
    f->gen = n->gen;
    return FAT_OK;
}

/* The handle's file as fat.c takes it: the node's size, chain and entry, the handle's place. */
static void to_fat(const struct fsv_file *h, struct fat_file *ff)
{
    const struct fsv_node *n = h->n;

    memset(ff, 0, sizeof *ff);
    ff->v = &h->v->fat;
    ff->first = n->first;
    ff->size = n->size;
    ff->pos = h->pos;
    ff->dir = n->dir;
    ff->index = n->index;
    ff->changed = n->changed;
    if (h->gen == n->gen) {
        ff->cl = h->cl;
        ff->cl_ord = h->cl_ord;
    }
}

/* ... and back: a chain that changed other than by growing (a new first cluster, a cut) is a new generation,
   so the other handles' places in it are dropped. */
static void from_fat(struct fsv_file *h, const struct fat_file *ff)
{
    struct fsv_node *n = h->n;

    if (ff->first != n->first || ff->size < n->size)
        n->gen++;
    n->first = ff->first;
    n->size = ff->size;
    n->changed = ff->changed;
    h->pos = ff->pos;
    h->cl = ff->cl;
    h->cl_ord = ff->cl_ord;
    h->gen = n->gen;
}

int fsv_mount(struct fsv_vol *v, fat_read_fn read, fat_write_fn write, fat_time_fn now, void *ctx, int cp,
              const struct fsv_lock *lk)
{
    memset(v, 0, sizeof *v);
    if (lk)
        v->lk = *lk;
    return write ? fat_mount_rw(&v->fat, read, write, now, ctx, cp) : fat_mount(&v->fat, read, ctx, cp);
}

/* A node's entry and the FATs written, if its file changed since they last were. */
static int node_out(struct fsv_vol *v, struct fsv_node *n)
{
    struct fat_file ff;
    int r;

    if (!n->changed)
        return FAT_OK;
    memset(&ff, 0, sizeof ff);
    ff.v = &v->fat;
    ff.first = n->first;
    ff.size = n->size;
    ff.dir = n->dir;
    ff.index = n->index;
    ff.changed = 1;
    r = fat_commit(&ff);
    if (!r)
        n->changed = 0;
    return r;
}

int fsv_dirty(const struct fsv_vol *v)
{
    int i;

    if (v->fat.dirty)
        return 1;
    for (i = 0; i < FSV_NODES; i++)
        if (v->node[i].refs && v->node[i].changed)
            return 1;
    return 0;
}

int fsv_flush(struct fsv_vol *v)
{
    int r = FAT_OK, e, i;

    lock(v);
    if (fsv_dirty(v)) {
        for (i = 0; i < FSV_NODES; i++)
            if (v->node[i].refs && (e = node_out(v, &v->node[i])) != 0)
                r = e;
        if ((e = fat_flush(&v->fat)) != 0)
            r = e;
    }
    unlock(v);
    return r;
}

int fsv_commit(struct fsv_file *f)
{
    int r;

    if (!f->n)
        return FSV_EBADF;
    lock(f->v);
    r = node_out(f->v, f->n);
    unlock(f->v);
    return r;
}

int fsv_open(struct fsv_vol *v, const char *path, int access, int share, struct fsv_file *f)
{
    struct fat_ent e;
    int r;

    if (access < FSV_READ || access > FSV_RW || share < FSV_COMPAT || share > FSV_DENY_NONE)
        return FAT_EINVAL;
    lock(v);
    r = fat_lookup(&v->fat, path, &e);
    if (!r && (e.attr & FAT_A_DIR))
        r = FAT_EISDIR;
    if (!r && writes(access) && (e.attr & FAT_A_RDONLY))
        r = FAT_EACCES;
    if (!r && e.size && !e.cluster)
        r = FAT_ECORRUPT;
    if (!r)
        r = attach(v, &e, access, share, f);
    unlock(v);
    return r;
}

int fsv_create(struct fsv_vol *v, const char *path, unsigned char attr, int replace, int access, int share,
               struct fsv_file *f)
{
    struct fat_ent e;
    struct fat_file ff;
    int r;

    if (access < FSV_READ || access > FSV_RW || share < FSV_COMPAT || share > FSV_DENY_NONE)
        return FAT_EINVAL;
    lock(v);
    r = fat_lookup(&v->fat, path, &e);
    if (r == FAT_OK) {
        if (!replace)
            r = FAT_EEXIST;
        else if (e.attr & (FAT_A_DIR | FAT_A_RDONLY))
            r = FAT_EACCES;
        else if (is_open(v, &e))
            r = FSV_ESHARE;
        else if ((r = attach(v, &e, access, share, f)) == FAT_OK) {
            to_fat(f, &ff);
            r = fat_truncate(&ff, 0);
            from_fat(f, &ff);
            if (!r)
                r = fat_setattr(&v->fat, path, (unsigned char)(attr | FAT_A_ARCHIVE));
            if (r) {
                unlock(v);
                fsv_close(f);
                return r;
            }
        }
    } else if (r == FAT_ENOENT) {
        r = fat_create(&v->fat, path, attr, &ff);
        if (!r) {
            memset(&e, 0, sizeof e);
            e.dir = ff.dir;
            e.index = ff.index;
            r = attach(v, &e, access, share, f);
        }
    }
    unlock(v);
    return r;
}

long fsv_read(struct fsv_file *f, void *buf, unsigned long n)
{
    struct fat_file ff;
    long r;

    if (!f->n)
        return FSV_EBADF;
    if (!reads(f->access))
        return FAT_EACCES;
    lock(f->v);
    to_fat(f, &ff);
    r = fat_read(&ff, buf, n);
    from_fat(f, &ff);
    unlock(f->v);
    return r;
}

long fsv_write(struct fsv_file *f, const void *buf, unsigned long n)
{
    struct fat_file ff;
    long r;

    if (!f->n)
        return FSV_EBADF;
    if (!writes(f->access))
        return FAT_EACCES;
    lock(f->v);
    to_fat(f, &ff);
    r = fat_write_later(&ff, buf, n);           /* (its entry and the FATs: at a close, a commit, a flush) */
    from_fat(f, &ff);
    unlock(f->v);
    return r;
}

int fsv_seek(struct fsv_file *f, unsigned long pos)
{
    if (!f->n)
        return FSV_EBADF;
    f->pos = pos;                       /* (the cached place stays: fat.c goes on from it when it is before) */
    return FAT_OK;
}

int fsv_truncate(struct fsv_file *f)
{
    struct fat_file ff;
    int r;

    if (!f->n)
        return FSV_EBADF;
    if (!writes(f->access))
        return FAT_EACCES;
    lock(f->v);
    to_fat(f, &ff);
    r = fat_truncate(&ff, f->pos);
    from_fat(f, &ff);
    unlock(f->v);
    return r;
}

unsigned long fsv_size(struct fsv_file *f)
{
    unsigned long n;

    if (!f->n)
        return 0;
    lock(f->v);
    n = f->n->size;
    unlock(f->v);
    return n;
}

int fsv_close(struct fsv_file *f)
{
    struct fsv_node *n = f->n;
    int r = FAT_OK;

    if (!n)
        return FSV_EBADF;
    lock(f->v);
    r = node_out(f->v, n);                      /* (what its writes left for now: DOS's close) */
    n->readers -= (unsigned)reads(f->access);
    n->writers -= (unsigned)writes(f->access);
    n->deny_read -= (unsigned)denies_read(f->share);
    n->deny_write -= (unsigned)denies_write(f->share);
    n->refs--;
    f->n = 0;
    if (!n->refs && n->gone) {
        int e = fat_free_chain(&f->v->fat, n->first);

        if (e)
            r = e;
        n->gone = 0;
    }
    if (!n->refs)
        n->changed = 0;
    unlock(f->v);
    return r;
}

/* ---- names and folders */

/* path's entry, and FSV_ESHARE if it is an open file. */
static int not_open(struct fsv_vol *v, const char *path)
{
    struct fat_ent e;
    int r = fat_lookup(&v->fat, path, &e);

    return r ? r : is_open(v, &e) ? FSV_ESHARE : FAT_OK;
}

int fsv_lookup(struct fsv_vol *v, const char *path, struct fat_ent *e)
{
    int r;

    lock(v);
    r = fat_lookup(&v->fat, path, e);
    if (!r && !(e->attr & (FAT_A_DIR | FAT_A_LABEL))) {
        const struct fsv_node *n = node_at(v, e->dir, e->index);

        if (n && n->changed)                    /* (an open file being written: as it is now, not as its entry
                                                   still has it) */
            e->size = n->size, e->cluster = n->first;
    }
    unlock(v);
    return r;
}

/* (An open file may be deleted, as MS-DOS 7.1 deletes one: its entry goes now, its clusters stay its handles'
   until the last of them closes.) */
int fsv_unlink(struct fsv_vol *v, const char *path)
{
    struct fat_ent e;
    struct fsv_node *n;
    unsigned long first;
    int r;

    lock(v);
    r = fat_lookup(&v->fat, path, &e);
    if (!r && (n = node_at(v, e.dir, e.index)) != 0) {
        r = fat_unlink_keep(&v->fat, path, &first);
        if (!r)
            n->dir = n->index = FAT_NO_ENTRY, n->gone = 1;
    } else if (!r) {
        r = fat_unlink(&v->fat, path);
    }
    unlock(v);
    return r;
}

/* (An open file may be renamed, as MS-DOS 7.1 renames one: its handles follow it to its new entry.) */
int fsv_rename(struct fsv_vol *v, const char *from, const char *to)
{
    struct fat_ent a, b;
    struct fsv_node *n;
    int r;

    lock(v);
    r = fat_lookup(&v->fat, from, &a);
    if (!r) {
        n = node_at(v, a.dir, a.index);
        r = fat_rename(&v->fat, from, to);
        if (!r && n && fat_lookup(&v->fat, to, &b) == 0)
            n->dir = b.dir, n->index = b.index;
    }
    unlock(v);
    return r;
}

int fsv_mkdir(struct fsv_vol *v, const char *path)
{
    int r;

    lock(v);
    r = fat_mkdir(&v->fat, path);
    unlock(v);
    return r;
}

int fsv_rmdir(struct fsv_vol *v, const char *path)
{
    int r;

    lock(v);
    r = fat_rmdir(&v->fat, path);
    unlock(v);
    return r;
}

int fsv_setattr(struct fsv_vol *v, const char *path, unsigned char attr)
{
    int r;

    lock(v);
    r = not_open(v, path);
    if (!r)
        r = fat_setattr(&v->fat, path, attr);
    unlock(v);
    return r;
}

int fsv_settime(struct fsv_vol *v, const char *path, unsigned short date, unsigned short time)
{
    int r;

    lock(v);
    r = fat_settime(&v->fat, path, date, time);
    unlock(v);
    return r;
}

int fsv_opendir(struct fsv_vol *v, const char *path, struct fat_dir *d)
{
    int r;

    lock(v);
    r = fat_opendir(&v->fat, path, d);
    unlock(v);
    return r;
}

int fsv_readdir(struct fsv_vol *v, struct fat_dir *d, struct fat_ent *e)
{
    int r;

    lock(v);
    d->cl = d->cl_ord = 0;              /* (others may have grown or changed the folder since: from its start) */
    r = fat_readdir(d, e);
    if (r == 1 && !(e->attr & (FAT_A_DIR | FAT_A_LABEL))) {
        const struct fsv_node *n = node_at(v, e->dir, e->index);

        if (n && n->changed)                    /* (a file being written: as it is now) */
            e->size = n->size, e->cluster = n->first;
    }
    unlock(v);
    return r;
}

int fsv_free(struct fsv_vol *v, unsigned long *clusters)
{
    int r;

    lock(v);
    r = fat_free(&v->fat, clusters);
    unlock(v);
    return r;
}
