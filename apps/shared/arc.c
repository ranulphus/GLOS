/* arc.c: see arc.h. The list is read whole when an archive is opened (a ZIP's is its central directory; a TAR's
   is every header in the file, a TAR inside a GZ unpacked from end to end for it). Unpacking is the loader's
   inflate (loader/inflate.c), with a state of the archive's own.

   In an app with threads the C library is used through libglos's lock (sdk/libglos/wrap.list): only functions
   of that list touch a file here (fgetc, not getc, which reads the file's buffer in place). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include "arc.h"
#include "arc7z.h"
#include "lzh.h"
#include "deflate.h"
#include "../../loader/inflate.h"

enum { K_ZIP, K_TAR, K_GZ, K_TGZ, K_LZH, K_ARJ, K_7Z };
#define BUF 4096u
#define CD_MAX (8ul << 20)              /* a ZIP's directory at most: some hundred thousand names */

struct ent {
    struct arc_entry e;
    char *name;
    unsigned long at;                   /* ZIP: its local header; TAR: its bytes (in the file, or in the GZ's stream);
                                           LZH, ARJ: its packed bytes; 7z: its number in the archive */
    unsigned long crc;
    unsigned short method;
};

struct arc {
    FILE *f;                            /* its file, open only while it is read (arc_use): the list is kept, not
                                           the file, so that it may be written again or taken away meanwhile */
    char *path;
    int users;
    int kind;
    unsigned long size;                 /* the file's */
    unsigned long base;                 /* ZIP: what lies before the archive in its file (a program that unpacks it) */
    unsigned long gz_at;                /* GZ: where the packed stream begins */
    struct ent *ents;
    unsigned n, room;
    struct ent **sorted;                /* by name */
    struct infl *z;                     /* the unpacker's state and window, had when first needed */
    struct sz *sz;                      /* 7z: the SDK's account of the archive (arc7z.c) */
    unsigned char *win;
    char **dos;                         /* each entry's path on a DOS disc (arc_dos_path), made when first asked */
    unsigned char buf[BUF];
};

/* ---- the files' own functions (arc_files) */

static void *open_plain(const char *path, const char *mode) { return fopen(path, mode); }
static void *(*file_open)(const char *path, const char *mode) = open_plain;
static int (*file_unlink)(const char *path) = remove;
static int (*file_rename)(const char *from, const char *to) = rename;

void arc_files(void *(*open)(const char *path, const char *mode), int (*unlink_)(const char *path),
               int (*rename_)(const char *from, const char *to))
{
    file_open = open ? open : open_plain;
    file_unlink = unlink_ ? unlink_ : remove;
    file_rename = rename_ ? rename_ : rename;
}

/* ---- small things */

static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned long le32(const unsigned char *p)
{
    return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

unsigned long arc_crc32(unsigned long crc, const void *data, unsigned n)
{
    static unsigned long table[256];
    static int made;
    const unsigned char *p = data;
    unsigned i;
    if (!made) {                        /* (two threads at once write the same numbers) */
        unsigned long k, c;
        int b;
        for (k = 0; k < 256; k++) {
            for (c = k, b = 0; b < 8; b++)
                c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
            table[k] = c;
        }
        made = 1;
    }
    crc = ~crc & 0xFFFFFFFFUL;
    for (i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc & 0xFFFFFFFFUL;
}

const char *arc_error(int err)
{
    switch (err) {
    case ARC_OK: return "done";
    case ARC_E_OPEN: return "the file cannot be read";
    case ARC_E_KIND: return "not an archive of a kind that can be opened";
    case ARC_E_DAMAGED: return "the archive is damaged";
    case ARC_E_MEMORY: return "not enough memory";
    case ARC_E_METHOD: return "packed in a way that is not unpacked here";
    case ARC_E_LOCKED: return "encrypted";
    case ARC_E_CRC: return "what was unpacked is not what was packed: the archive is damaged";
    case ARC_E_STOP: return "stopped";
    case ARC_E_BIG: return "too large, or in several parts";
    }
    return "an error";
}

static int ends_with(const char *name, const char *end)
{
    size_t n = strlen(name), k = strlen(end), i;
    if (n < k)
        return 0;
    for (i = 0; i < k; i++) {
        int c = name[n - k + i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        if (c != end[i])
            return 0;
    }
    return 1;
}

int arc_name_is(const char *name)
{
    return ends_with(name, ".ZIP") || ends_with(name, ".TAR") || ends_with(name, ".GZ") || ends_with(name, ".TGZ")
        || ends_with(name, ".LZH") || ends_with(name, ".LHA") || ends_with(name, ".ARJ") || ends_with(name, ".7Z");
}

/* An entry's name as it is kept (malloc'd): '\' is '/', no drive, no '/' before, after or doubled, no "."
   part, nothing below a space. NULL for a name with a ".." part, or with nothing left of it. *dir: it ended
   with a '/'. */
static char *clean_name(const char *raw, unsigned n, int *dir)
{
    char *out = malloc(n + 1);
    unsigned i = 0, o = 0, start;
    if (!out)
        return NULL;
    *dir = n > 0 && (raw[n - 1] == '/' || raw[n - 1] == '\\');
    if (n >= 2 && raw[1] == ':')
        i = 2;
    while (i < n) {
        while (i < n && (raw[i] == '/' || raw[i] == '\\'))
            i++;
        start = i;
        while (i < n && raw[i] != '/' && raw[i] != '\\')
            i++;
        if (i == start || (i - start == 1 && raw[start] == '.'))
            continue;
        if (i - start == 2 && raw[start] == '.' && raw[start + 1] == '.') {
            free(out);
            return NULL;
        }
        if (o)
            out[o++] = '/';
        for (; start < i; start++)
            out[o++] = (unsigned char)raw[start] < 0x20 ? '_' : raw[start];
    }
    if (!o) {
        free(out);
        return NULL;
    }
    out[o] = 0;
    return out;
}

static struct ent *add(struct arc *a, char *name)
{
    struct ent *e;
    if (a->n == a->room) {
        unsigned room = a->room ? a->room * 2 : 64;
        struct ent *m = realloc(a->ents, room * sizeof *m);
        if (!m)
            return NULL;
        a->ents = m, a->room = room;
    }
    e = &a->ents[a->n++];
    memset(e, 0, sizeof *e);
    e->name = name;
    e->e.name = name;
    return e;
}

/* Names in the list's order: as bytes, but '/' before everything, so that a folder's own entry and all that is
   in it lie together. */
static int name_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x == '/')
            x = 1;
        if (y == '/')
            y = 1;
        if (x != y)
            return x < y ? -1 : 1;
        if (!x)
            return 0;
    }
}

static int ent_cmp(const void *a, const void *b)
{
    return name_cmp((*(struct ent *const *)a)->name, (*(struct ent *const *)b)->name);
}

static int need_unpacker(struct arc *a)
{
    if (!a->z)
        a->z = malloc(inflate_state_size());
    if (!a->win)
        a->win = malloc(INFL_WINDOW);
    return a->z && a->win ? 0 : ARC_E_MEMORY;
}

/* A packed stream of the file's unpacked: `left` bytes of the file from where it is are the stream's, and what
   comes out goes to out, 32 KB at a time. */
struct pump {
    struct arc *a;
    unsigned long left;
    int (*out)(struct pump *p, const unsigned char *d, unsigned n);
    void *ctx;
    int (*data)(void *ctx, const void *p, unsigned n);
    unsigned long crc, total;
    unsigned long most;                 /* limited: more than this come out is damage (a ZIP's entry says its size) */
    int limited;
    int sum;                            /* the CRC is wanted */
    int err;                            /* what out stopped for */
};

static unsigned pump_fill(const unsigned char **pp, void *ctx)
{
    struct pump *p = ctx;
    unsigned n = p->left < BUF ? (unsigned)p->left : BUF;
    if (!n)
        return 0;
    n = (unsigned)fread(p->a->buf, 1, n, p->a->f);
    p->left -= n;
    *pp = p->a->buf;
    return n;
}

static int pump_flush(unsigned n, void *ctx)
{
    struct pump *p = ctx;
    if (p->sum)
        p->crc = arc_crc32(p->crc, p->a->win, n);
    p->total += n;
    if (p->limited && p->total > p->most) {     /* (before a small archive has filled the disc) */
        p->err = ARC_E_DAMAGED;
        return 1;
    }
    return p->out(p, p->a->win, n);
}

static int pump_run(struct pump *p)     /* inflate's 0, -1 or -2 */
{
    p->crc = 0, p->total = 0, p->err = 0;
    return inflate_stream_r(p->a->z, pump_fill, pump_flush, p, p->a->win);
}

static int out_data(struct pump *p, const unsigned char *d, unsigned n)
{
    if (p->data && p->data(p->ctx, d, n) != 0) {
        p->err = ARC_E_STOP;
        return 1;
    }
    return 0;
}

/* ---- ZIP */

/* A ZIP's directory, found from the file's end and read whole (z->cd malloc'd, and the comment after it). */
struct zipdir {
    unsigned long base;                 /* where the archive begins in its file (0, or a program's length) */
    unsigned long cd_at, cd_size;       /* the directory, from base */
    unsigned count;
    unsigned char *cd;
    unsigned char *comment;             /* the archive's comment (malloc'd too), kept when it is written again */
    unsigned ncomment;
};

static void zip_dir_free(struct zipdir *z)
{
    free(z->cd);
    free(z->comment);
    z->cd = z->comment = NULL;
}

static int zip_dir(FILE *f, unsigned long size, struct zipdir *z)
{
    unsigned long tail = size < 22 + 65535ul ? size : 22 + 65535ul, at;
    unsigned char *t;
    long i, eocd = -1;
    unsigned here, k;
    int r;
    memset(z, 0, sizeof *z);
    if (size < 22)
        return ARC_E_KIND;
    if (!(t = malloc(tail)))
        return ARC_E_MEMORY;
    if (fseek(f, (long)(size - tail), SEEK_SET) != 0 || fread(t, 1, tail, f) != tail) {
        free(t);
        return ARC_E_OPEN;
    }
    for (i = (long)tail - 22; i >= 0; i--)      /* the end's record: the last one whose comment fits the file */
        if (t[i] == 0x50 && t[i + 1] == 0x4B && t[i + 2] == 5 && t[i + 3] == 6
            && (unsigned long)i + 22 + le16(t + i + 20) <= tail) {
            eocd = i;
            break;
        }
    if (eocd < 0) {
        free(t);
        return ARC_E_KIND;
    }
    here = le16(t + eocd + 8);
    z->count = le16(t + eocd + 10);
    z->cd_size = le32(t + eocd + 12);
    z->cd_at = le32(t + eocd + 16);
    k = le16(t + eocd + 4) | le16(t + eocd + 6);        /* (this disc's number, the directory's disc) */
    at = size - tail + (unsigned long)eocd;             /* where the record is in the file */
    z->ncomment = le16(t + eocd + 20);
    if (!(z->comment = malloc(z->ncomment + 1u))) {
        free(t);
        return ARC_E_MEMORY;
    }
    memcpy(z->comment, t + eocd + 22, z->ncomment);
    free(t);
    r = ARC_E_BIG;
    if (k || here != z->count)                  /* (on several discs) */
        goto bad;
    if (z->count == 0xFFFF || z->cd_size == 0xFFFFFFFFul || z->cd_at == 0xFFFFFFFFul || z->cd_size > CD_MAX)
        goto bad;                               /* (ZIP64, or a directory of a size not read here) */
    r = ARC_E_DAMAGED;
    if (z->cd_size > at || z->cd_at > at - z->cd_size)
        goto bad;
    z->base = at - z->cd_size - z->cd_at;
    r = ARC_E_MEMORY;
    if (!(z->cd = malloc(z->cd_size ? z->cd_size : 1)))
        goto bad;
    r = ARC_E_OPEN;
    if (fseek(f, (long)(z->base + z->cd_at), SEEK_SET) != 0 || fread(z->cd, 1, z->cd_size, f) != z->cd_size)
        goto bad;
    return ARC_OK;
bad:
    zip_dir_free(z);
    return r;
}

/* The directory's entry at *at (and *at moved past it): its 46 bytes and what follows them, or NULL: there is
   no whole entry there. */
static const unsigned char *zip_dir_next(const struct zipdir *z, unsigned long *at)
{
    const unsigned char *h = z->cd + *at;
    unsigned long len;
    if (z->cd_size - *at < 46 || le32(h) != 0x02014B50ul)
        return NULL;
    len = 46ul + le16(h + 28) + le16(h + 30) + le16(h + 32);
    if (z->cd_size - *at < len)
        return NULL;
    *at += len;
    return h;
}

static int zip_open(struct arc *a)
{
    struct zipdir z;
    unsigned long at;
    unsigned char *cd;
    unsigned count, k;
    int r = zip_dir(a->f, a->size, &z);
    if (r != ARC_OK)
        return r;
    a->base = z.base;
    cd = z.cd, count = z.count;
    free(z.comment);
    for (at = 0, k = 0; k < count; k++) {
        const unsigned char *h = zip_dir_next(&z, &at);
        unsigned flags, method;
        struct ent *e;
        char *name;
        int dir;
        if (!h) {
            free(cd);
            return ARC_E_DAMAGED;
        }
        name = clean_name((const char *)h + 46, le16(h + 28), &dir);
        if (!name)
            continue;                           /* (a name that would leave the folder, or none) */
        if (!(e = add(a, name))) {
            free(name);
            free(cd);
            return ARC_E_MEMORY;
        }
        flags = le16(h + 8), method = le16(h + 10);
        e->method = (unsigned short)method;
        e->e.time = (unsigned short)le16(h + 12), e->e.date = (unsigned short)le16(h + 14);
        e->crc = le32(h + 16);
        e->e.packed = le32(h + 20), e->e.size = le32(h + 24);
        e->e.dir = (unsigned char)(dir || (le32(h + 38) & 0x10));
        e->at = le32(h + 42);
        if (e->e.dir)
            e->e.size = e->e.packed = 0;
        else if (flags & 0x41)
            e->e.how = ARC_E_LOCKED;
        else if (e->e.size == 0xFFFFFFFFul || e->e.packed == 0xFFFFFFFFul || e->at == 0xFFFFFFFFul)
            e->e.how = ARC_E_BIG;
        else if (method != 0 && method != 8)
            e->e.how = ARC_E_METHOD;
    }
    free(cd);
    return ARC_OK;
}

static int zip_extract(struct arc *a, const struct ent *e, int (*data)(void *ctx, const void *p, unsigned n), void *ctx)
{
    unsigned char h[30];
    unsigned long left, crc = 0;
    if (e->e.how)
        return e->e.how;
    if (fseek(a->f, (long)(a->base + e->at), SEEK_SET) != 0 || fread(h, 1, 30, a->f) != 30 || le32(h) != 0x04034B50ul
        || fseek(a->f, (long)(le16(h + 26) + le16(h + 28)), SEEK_CUR) != 0)
        return ARC_E_DAMAGED;
    if (e->method == 0) {
        if (e->e.packed != e->e.size)
            return ARC_E_DAMAGED;
        for (left = e->e.size; left; ) {
            unsigned n = left < BUF ? (unsigned)left : BUF;
            if (fread(a->buf, 1, n, a->f) != n)
                return ARC_E_DAMAGED;
            crc = arc_crc32(crc, a->buf, n);
            if (data && data(ctx, a->buf, n) != 0)
                return ARC_E_STOP;
            left -= n;
        }
    } else {
        struct pump p;
        int r;
        if (need_unpacker(a) != 0)
            return ARC_E_MEMORY;
        memset(&p, 0, sizeof p);
        p.a = a, p.left = e->e.packed, p.out = out_data, p.data = data, p.ctx = ctx, p.sum = 1;
        p.most = e->e.size, p.limited = 1;
        r = pump_run(&p);
        if (r == -2)
            return p.err ? p.err : ARC_E_STOP;
        if (r != 0 || p.total != e->e.size)
            return ARC_E_DAMAGED;
        crc = p.crc;
    }
    return crc == e->crc ? ARC_OK : ARC_E_CRC;
}

/* ---- TAR: its bytes are given to this as they come (from the file, or out of a GZ) */

struct tar {
    struct arc *a;
    int listing;                        /* arc_open's pass: the entries are added to the list */
    const struct arc_sink *s;           /* a walk's */
    void *ctx;
    unsigned char head[512];
    unsigned have;                      /* of a header's bytes */
    unsigned long left;                 /* of an entry's bytes */
    unsigned pad;                       /* ... and after them to the block's end */
    int taking;                         /* those bytes are: 0 nobody's, 1 the sink's, 2 the next entry's name (GNU's),
                                           3 a pax header (the next entry's name may be in it) */
    char *longname;                     /* the next entry's name, if a header before it said so */
    unsigned longn, longroom;
    unsigned next;                      /* a walk: the next entry's number */
    unsigned cur;                       /* ... and the one whose bytes are coming */
    unsigned long pos;                  /* bytes had so far */
    int done;                           /* the end's empty block has come */
    int err;
};

static unsigned long octal(const unsigned char *p, unsigned n)
{
    unsigned long v = 0;
    unsigned i = 0;
    while (i < n && p[i] == ' ')
        i++;
    for (; i < n && p[i] >= '0' && p[i] <= '7'; i++)
        v = v << 3 | (unsigned long)(p[i] - '0');
    return v;
}

static int tar_header_ok(const unsigned char *h)
{
    unsigned long u = 0, want;
    long s = 0;
    unsigned i;
    for (i = 0; i < 512; i++) {
        unsigned char c = i >= 148 && i < 156 ? ' ' : h[i];
        u += c;
        s += (signed char)c;
    }
    if (u == 8 * ' ')                   /* (an empty block) */
        return 0;
    want = octal(h + 148, 8);
    return want == u || want == (unsigned long)s;
}

static void dos_stamp(unsigned long t, unsigned short *date, unsigned short *time)
{
    static const unsigned char mdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    unsigned long days = t / 86400ul, secs = t % 86400ul, len;
    unsigned y = 1970, m;
    *date = *time = 0;
    if (!t)
        return;
    for (;; y++) {
        len = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        if (days < len)
            break;
        days -= len;
    }
    for (m = 0; m < 12; m++) {
        len = mdays[m] + (m == 1 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 1u : 0u);
        if (days < len)
            break;
        days -= len;
    }
    if (y < 1980 || y > 2107) {         /* (DOS's years) */
        *date = 1 << 5 | 1;
        return;
    }
    *date = (unsigned short)((y - 1980) << 9 | (m + 1) << 5 | (unsigned)(days + 1));
    *time = (unsigned short)((unsigned)(secs / 3600) << 11 | (unsigned)(secs / 60 % 60) << 5 | (unsigned)(secs % 60 / 2));
}

static int long_put(struct tar *t, const unsigned char *p, unsigned n)
{
    if (t->longn + n + 1 > t->longroom) {
        unsigned room = t->longn + n + 1 + 256;
        char *m = room > 65536u ? NULL : realloc(t->longname, room);
        if (!m)
            return -1;
        t->longname = m, t->longroom = room;
    }
    memcpy(t->longname + t->longn, p, n);
    t->longn += n;
    t->longname[t->longn] = 0;
    return 0;
}

/* A pax header's records ("27 path=some/long/name\n") are in longname: the path's value is left there, or
   nothing. */
static void pax_path(struct tar *t)
{
    char *p = t->longname, *end = p ? p + t->longn : NULL, *found = NULL;
    unsigned flen = 0;
    while (p && p < end) {
        unsigned long len = strtoul(p, NULL, 10);
        char *sp = memchr(p, ' ', (size_t)(end - p));
        if (!len || len > (unsigned long)(end - p) || !sp || sp >= p + len)
            break;
        if (p + len - sp > 6 && !memcmp(sp + 1, "path=", 5)) {
            found = sp + 6;
            flen = (unsigned)(p + len - found);
            if (flen && found[flen - 1] == '\n')
                flen--;
        }
        p += len;
    }
    if (found) {
        memmove(t->longname, found, flen);
        t->longname[flen] = 0;
        t->longn = flen;
    } else {
        t->longn = 0;
    }
}

static void tar_entry_end(struct tar *t)
{
    if (t->taking == 1)
        t->s->end(t->ctx, t->cur, ARC_OK);
    else if (t->taking == 3)
        pax_path(t);
    t->taking = 0;
}

/* A header has come whole. 0, or 1 to stop (t->err says why, if it is an error). */
static int tar_header(struct tar *t)
{
    const unsigned char *h = t->head;
    unsigned long size;
    char raw[260], *name;
    unsigned n;
    int dir, type = h[156], file;
    if (!tar_header_ok(h)) {
        for (n = 0; n < 512 && !h[n]; n++)
            ;
        if (n == 512)
            t->done = 1;                /* the end */
        else
            t->err = ARC_E_DAMAGED;
        return !t->done;
    }
    if (h[124] & 0x80) {                /* (GNU's: a binary number, for sizes octal has no room for) */
        for (n = 124; n < 132; n++)
            if (h[n] & (n == 124 ? 0x7F : 0xFF)) {
                t->err = ARC_E_BIG;
                return 1;
            }
        size = (unsigned long)h[132] << 24 | (unsigned long)h[133] << 16 | (unsigned long)h[134] << 8 | h[135];
    } else {
        for (n = 124; n < 136 && h[n] == ' '; n++)
            ;
        if (n + 11 < 136 && h[n] > '3' && h[n + 11] >= '0' && h[n + 11] <= '7') {
            t->err = ARC_E_BIG;         /* (eleven digits and the first above 3: 4 GB or more) */
            return 1;
        }
        size = octal(h + 124, 12);
    }
    t->left = size;
    t->pad = (unsigned)((512 - (size & 511)) & 511);
    t->taking = 0;
    if (type == 'L' || type == 'x') {   /* the next entry's name comes as this one's bytes */
        t->longn = 0;
        t->taking = type == 'L' ? 2 : 3;
        return 0;
    }
    file = type == '0' || type == 0 || type == '7';
    if (!file && type != '5') {         /* (links, devices, a pax header for all, GNU's other extras: not listed) */
        if (type != 'g' && type != 'K')
            t->longn = 0;
        return 0;
    }
    if (t->longn) {
        name = clean_name(t->longname, (unsigned)strlen(t->longname), &dir);
        t->longn = 0;
    } else {
        n = 0;
        if (!memcmp(h + 257, "ustar", 5) && h[345]) {   /* (the name's first part, apart) */
            while (n < 155 && h[345 + n])
                raw[n] = (char)h[345 + n], n++;
            raw[n++] = '/';
        }
        for (dir = 0; dir < 100 && h[dir]; dir++)
            raw[n++] = (char)h[dir];
        name = clean_name(raw, n, &dir);
    }
    if (type == '5')
        dir = 1;
    if (!name)
        return 0;                       /* (a name that would leave the folder: not listed, its bytes passed over) */
    if (t->listing) {
        struct ent *e = add(t->a, name);
        if (!e) {
            free(name);
            t->err = ARC_E_MEMORY;
            return 1;
        }
        e->e.dir = (unsigned char)dir;
        e->e.size = e->e.packed = dir ? 0 : size;
        e->at = t->pos;
        dos_stamp(octal(h + 136, 12), &e->e.date, &e->e.time);
    } else {
        int r;
        free(name);
        if (t->next >= t->a->n) {       /* (not the archive that was listed) */
            t->err = ARC_E_DAMAGED;
            return 1;
        }
        t->cur = t->next++;
        r = t->s->begin ? t->s->begin(t->ctx, t->cur, &t->a->ents[t->cur].e) : 0;
        if (r < 0) {
            t->err = ARC_E_STOP;
            return 1;
        }
        if (r > 0) {
            if (dir || !size)
                t->s->end(t->ctx, t->cur, ARC_OK);
            else
                t->taking = 1;
        }
    }
    return 0;
}

/* More of the TAR's bytes. 0 to go on; anything else: stop (the end has come, or t->err). */
static int tar_feed(struct tar *t, const unsigned char *p, unsigned n)
{
    while (n && !t->done) {
        unsigned k;
        if (t->left) {
            k = t->left < n ? (unsigned)t->left : n;
            if (t->taking == 1 && t->s->data && t->s->data(t->ctx, p, k) != 0) {
                t->s->end(t->ctx, t->cur, ARC_E_STOP);
                t->taking = 0;
                t->err = ARC_E_STOP;
                return 1;
            }
            if (t->taking >= 2 && long_put(t, p, k) != 0) {
                t->err = ARC_E_DAMAGED;
                return 1;
            }
            t->left -= k;
            if (!t->left)
                tar_entry_end(t);
        } else if (t->pad) {
            k = t->pad < n ? t->pad : n;
            t->pad -= k;
        } else {
            k = 512 - t->have < n ? 512 - t->have : n;
            memcpy(t->head + t->have, p, k);
            t->have += k;
            if (t->have == 512) {
                t->have = 0;
                t->pos += k;
                p += k, n -= k;
                if (tar_header(t) != 0)
                    return 1;
                if (!t->left && t->taking >= 2)
                    tar_entry_end(t);
                continue;
            }
        }
        t->pos += k;
        p += k, n -= k;
    }
    return 0;
}

/* The bytes have ended: 0, or what was wrong (cut short inside an entry or a header). */
static int tar_finish(struct tar *t)
{
    if (!t->err && !t->done && (t->left || t->pad >= 512 || t->have))
        t->err = ARC_E_DAMAGED;
    if (t->taking == 1) {
        t->s->end(t->ctx, t->cur, t->err ? t->err : ARC_E_DAMAGED);
        t->taking = 0;
    }
    free(t->longname);
    t->longname = NULL;
    return t->err;
}

static void tar_begin(struct tar *t, struct arc *a, const struct arc_sink *s, void *ctx)
{
    memset(t, 0, sizeof *t);
    t->a = a, t->s = s, t->ctx = ctx, t->listing = s == NULL;
}

/* A TAR in a file of its own gone through: entries nobody takes are passed over without being read. */
static int tar_pass(struct arc *a, const struct arc_sink *s, void *ctx)
{
    struct tar t;
    unsigned n;
    tar_begin(&t, a, s, ctx);
    if (fseek(a->f, 0, SEEK_SET) != 0)
        return ARC_E_OPEN;
    for (;;) {
        if (!t.taking && t.left + t.pad >= BUF && !t.have) {
            unsigned long skip = t.left + t.pad;
            if (skip > a->size || t.pos > a->size - skip) {
                t.err = ARC_E_DAMAGED;  /* (the file ends before the entry does) */
                break;
            }
            if (fseek(a->f, (long)skip, SEEK_CUR) != 0) {
                t.err = ARC_E_OPEN;
                break;
            }
            t.pos += skip;
            t.left = 0, t.pad = 0;
        }
        n = (unsigned)fread(a->buf, 1, BUF, a->f);
        if (!n || tar_feed(&t, a->buf, n) != 0 || t.done)
            break;
    }
    return tar_finish(&t);
}

/* ---- GZ, and a TAR inside one */

/* The GZ's header read: the packed stream's place (a->gz_at), the name and the time in it (name[0] 0: none). */
static int gz_header(struct arc *a, char *name, unsigned room, unsigned long *mtime)
{
    unsigned char h[10];
    unsigned flags, n;
    int c;
    name[0] = 0;
    if (fseek(a->f, 0, SEEK_SET) != 0 || fread(h, 1, 10, a->f) != 10 || h[0] != 0x1F || h[1] != 0x8B)
        return ARC_E_KIND;
    if (h[2] != 8 || (h[3] & 0xE0))
        return ARC_E_METHOD;
    flags = h[3];
    *mtime = le32(h + 4);
    if (flags & 4) {                    /* extra bytes */
        if (fread(h, 1, 2, a->f) != 2 || fseek(a->f, (long)le16(h), SEEK_CUR) != 0)
            return ARC_E_DAMAGED;
    }
    if (flags & 8) {                    /* the file's name */
        for (n = 0; (c = fgetc(a->f)) > 0; )
            if (n + 1 < room)
                name[n++] = (char)c;
        name[n] = 0;
        if (c != 0)
            return ARC_E_DAMAGED;
    }
    if (flags & 16)                     /* a comment */
        while ((c = fgetc(a->f)) > 0)
            ;
    if (flags & 2)                      /* the header's own check */
        fgetc(a->f), fgetc(a->f);
    a->gz_at = (unsigned long)ftell(a->f);
    if (feof(a->f) || a->size < a->gz_at + 8)
        return ARC_E_DAMAGED;
    return ARC_OK;
}

static int gz_tail(struct arc *a, unsigned long *crc, unsigned long *size)
{
    unsigned char t[8];
    if (fseek(a->f, (long)(a->size - 8), SEEK_SET) != 0 || fread(t, 1, 8, a->f) != 8)
        return ARC_E_OPEN;
    *crc = le32(t), *size = le32(t + 4);
    return ARC_OK;
}

static int gz_pump(struct arc *a, struct pump *p)
{
    if (need_unpacker(a) != 0)
        return ARC_E_MEMORY;
    if (fseek(a->f, (long)a->gz_at, SEEK_SET) != 0)
        return ARC_E_OPEN;
    p->a = a;
    p->left = a->size - a->gz_at - 8;
    return pump_run(p) == -1 ? ARC_E_DAMAGED : ARC_OK;  /* (-2: out stopped it, and knows why) */
}

static int out_first(struct pump *p, const unsigned char *d, unsigned n)   /* is what comes out a TAR? */
{
    *(int *)p->ctx = n >= 512 && tar_header_ok(d);
    return 1;
}

static int out_tar(struct pump *p, const unsigned char *d, unsigned n)
{
    return tar_feed(p->ctx, d, n);
}

static int tgz_pass(struct arc *a, const struct arc_sink *s, void *ctx)
{
    struct tar t;
    struct pump p;
    unsigned long crc, size;
    int r;
    tar_begin(&t, a, s, ctx);
    memset(&p, 0, sizeof p);
    p.out = out_tar, p.ctx = &t, p.sum = s != NULL;
    r = gz_pump(a, &p);
    if (r != ARC_OK && !t.err)
        t.err = r;
    r = tar_finish(&t);
    if (r == ARC_OK && s && !p.err && gz_tail(a, &crc, &size) == ARC_OK && (crc != p.crc || size != p.total))
        r = ARC_E_CRC;                  /* (gone through to its end, and not what was packed) */
    return r;
}

static int gz_open(struct arc *a, const char *path)
{
    char name[260], *clean;
    unsigned long mtime, crc, size;
    struct pump p;
    struct ent *e;
    int tar = 0, r, dir;
    if ((r = gz_header(a, name, sizeof name, &mtime)) != ARC_OK)
        return r;
    memset(&p, 0, sizeof p);
    p.out = out_first, p.ctx = &tar;
    if ((r = gz_pump(a, &p)) != ARC_OK)
        return r;
    if (tar) {
        a->kind = K_TGZ;
        return tgz_pass(a, NULL, NULL);
    }
    a->kind = K_GZ;
    if ((r = gz_tail(a, &crc, &size)) != ARC_OK)
        return r;
    if (name[0]) {                      /* the name in it, without any folders */
        const char *b = name + strlen(name);
        while (b > name && b[-1] != '/' && b[-1] != '\\')
            b--;
        memmove(name, b, strlen(b) + 1);
    }
    if (!name[0]) {                     /* or the file's own, without its ending */
        const char *b = path + strlen(path);
        while (b > path && b[-1] != '/' && b[-1] != '\\' && b[-1] != ':')
            b--;
        snprintf(name, sizeof name, "%s", b);
        if (ends_with(name, ".GZ"))
            name[strlen(name) - 3] = 0;
    }
    clean = clean_name(name, (unsigned)strlen(name), &dir);
    if (!clean && !(clean = clean_name("DATA", 4, &dir)))
        return ARC_E_MEMORY;
    if (!(e = add(a, clean))) {
        free(clean);
        return ARC_E_MEMORY;
    }
    e->e.size = size, e->e.packed = a->size, e->crc = crc;
    dos_stamp(mtime, &e->e.date, &e->e.time);
    return ARC_OK;
}

static int gz_extract(struct arc *a, const struct ent *e, int (*data)(void *ctx, const void *p, unsigned n), void *ctx)
{
    struct pump p;
    int r;
    memset(&p, 0, sizeof p);
    p.out = out_data, p.data = data, p.ctx = ctx, p.sum = 1;
    if ((r = gz_pump(a, &p)) != ARC_OK)
        return r;
    if (p.err)
        return p.err;
    return p.crc == e->crc && p.total == e->e.size ? ARC_OK : ARC_E_CRC;
}

/* ---- the archive */

/* ---- LZH (LHA's archives) and ARJ: a header before each file's packed bytes, one after another to the end. Both
   pack with the one method lzh.c unpacks (ARJ has a second, its "fastest"), or not at all.

   An LZH header is of one of three levels (its byte 20). Level 0: its size and a sum, the method ("-lh5-"), the
   packed and the real size, DOS's time and date, an attribute, the name with its path, the file's CRC-16. Level
   1: the same with the name alone, and then extended headers (each: its size, a type, its bytes), of which 01h is
   the name and 02h the path (FFh between its parts); their sizes are counted in "the packed size". Level 2: the
   whole header's size first, Unix time, the CRC, then extended headers. "-lhd-" is a folder. */

static unsigned crc16(unsigned crc, const unsigned char *p, unsigned n)        /* LHA's: x^16 + x^15 + x^2 + 1 */
{
    unsigned k;
    while (n--) {
        crc ^= *p++;
        for (k = 0; k < 8; k++)
            crc = crc & 1 ? crc >> 1 ^ 0xA001u : crc >> 1;
    }
    return crc & 0xFFFF;
}

static int lzh_method(const unsigned char *m)   /* 0 stored, LZH_ of lzh.h, -1 a folder, -2 one not unpacked here */
{
    if (!memcmp(m, "-lh0-", 5) || !memcmp(m, "-lz4-", 5))
        return 0;
    if (!memcmp(m, "-lh5-", 5))
        return LZH_LH5;
    if (!memcmp(m, "-lh6-", 5))
        return LZH_LH6;
    if (!memcmp(m, "-lh7-", 5))
        return LZH_LH7;
    if (!memcmp(m, "-lhd-", 5))
        return -1;
    return -2;
}

#define LZH_NAME 1024

static int lzh_open(struct arc *a)
{
    unsigned long at = 0;
    unsigned char *h = malloc(65536 + 64);
    char *path = malloc(2 * LZH_NAME + 2);
    int r = ARC_OK;
    if (!h || !path) {
        free(h);
        free(path);
        return ARC_E_MEMORY;
    }
    for (;;) {
        unsigned long packed, size, data, tstamp;
        unsigned level, hsize, nname = 0, ndir = 0, crc = 0, i, next;
        unsigned short date = 0, time = 0;
        const unsigned char *name = NULL;
        char dir[LZH_NAME + 1];
        struct ent *e;
        char *clean;
        int method, isdir = 0;
        if (at + 21 > a->size || fseek(a->f, (long)at, SEEK_SET) != 0 || fread(h, 1, 21, a->f) != 21 || h[0] == 0)
            break;                              /* (the end: a zero, or nothing more) */
        if (h[2] != '-' || h[3] != 'l' || h[6] != '-') {
            r = a->n ? ARC_OK : ARC_E_DAMAGED;  /* (what follows the last entry is not ours to judge) */
            break;
        }
        level = h[20];
        packed = le32(h + 7), size = le32(h + 11), tstamp = le32(h + 15);
        method = lzh_method(h + 2);
        dir[0] = 0;
        if (level <= 1) {
            hsize = h[0] + 2u;
            if (hsize < 24 || at + hsize > a->size || fread(h + 21, 1, hsize - 21, a->f) != hsize - 21) {
                r = ARC_E_DAMAGED;
                break;
            }
            nname = h[21];
            if (22 + nname + 2 > hsize) {
                r = ARC_E_DAMAGED;
                break;
            }
            name = h + 22;
            crc = le16(h + 22 + nname);
            time = (unsigned short)tstamp, date = (unsigned short)(tstamp >> 16);
            data = at + hsize;
            if (level == 1) {                   /* the extended headers: counted in the packed size */
                next = le16(h + hsize - 2);
                while (next) {
                    unsigned char x[LZH_NAME + 4];
                    unsigned take = next < sizeof x ? next : (unsigned)sizeof x;
                    if (next < 3 || data + next > a->size || packed < next || fseek(a->f, (long)data, SEEK_SET) != 0
                        || fread(x, 1, take, a->f) != take) {
                        r = ARC_E_DAMAGED;
                        break;
                    }
                    if (x[0] == 0x01 && next - 3 <= LZH_NAME && take == next) {
                        nname = next - 3;       /* (the name: over the header's own) */
                        memcpy(h + 22, x + 1, nname);
                        name = h + 22;
                    } else if (x[0] == 0x02 && next - 3 <= LZH_NAME && take == next) {
                        ndir = next - 3;
                        memcpy(dir, x + 1, ndir);
                    }
                    data += next, packed -= next;
                    if (take == next) {
                        next = le16(x + next - 2);
                    } else {                    /* (a long one of another kind: its last two bytes read apart) */
                        unsigned char t[2];
                        if (fseek(a->f, (long)(data - 2), SEEK_SET) != 0 || fread(t, 1, 2, a->f) != 2) {
                            r = ARC_E_DAMAGED;
                            break;
                        }
                        next = le16(t);
                    }
                }
                if (r != ARC_OK)
                    break;
            }
        } else {                                /* level 2: the header's whole size first */
            unsigned long p;
            hsize = le16(h);
            if (hsize < 26 || at + hsize > a->size || fread(h + 21, 1, hsize - 21, a->f) != hsize - 21) {
                r = ARC_E_DAMAGED;
                break;
            }
            crc = le16(h + 21);
            dos_stamp(tstamp, &date, &time);
            data = at + hsize;
            next = le16(h + 24);
            for (p = 26; next; ) {
                if (next < 3 || p + next > hsize) {
                    r = ARC_E_DAMAGED;
                    break;
                }
                if (h[p] == 0x01 && next - 3 <= LZH_NAME) {
                    nname = next - 3;
                    name = h + p + 1;
                } else if (h[p] == 0x02 && next - 3 <= LZH_NAME) {
                    ndir = next - 3;
                    memcpy(dir, h + p + 1, ndir);
                }
                p += next;
                next = le16(h + p - 2);
            }
            if (r != ARC_OK)
                break;
        }
        if (data > a->size || packed > a->size - data) {
            r = ARC_E_DAMAGED;
            break;
        }
        for (i = 0; i < ndir; i++)              /* the path's parts, then the name: as one name */
            path[i] = (unsigned char)dir[i] == 0xFF ? '/' : dir[i];
        if (ndir && path[ndir - 1] != '/')
            path[ndir++] = '/';
        if (nname > LZH_NAME)
            nname = LZH_NAME;
        for (i = 0; i < nname; i++)
            path[ndir + i] = name[i] == 0xFF ? '/' : (char)name[i];
        isdir = method == -1;
        clean = clean_name(path, ndir + nname, &isdir);
        if (method == -1)
            isdir = 1;
        if (clean) {
            if (!(e = add(a, clean))) {
                free(clean);
                r = ARC_E_MEMORY;
                break;
            }
            e->at = data, e->crc = crc;
            e->method = (unsigned short)(method > 0 ? method : 0);
            e->e.date = date, e->e.time = time;
            e->e.dir = (unsigned char)isdir;
            if (!isdir) {
                e->e.size = size, e->e.packed = packed;
                if (method == -2)
                    e->e.how = ARC_E_METHOD;
            }
        }
        at = data + packed;
    }
    free(h);
    free(path);
    return r;
}

/* ARJ: 60h EAh, the header's size, the header (its own first part's size, versions, the host, flags, the method,
   the kind of entry, the time and date (DOS's, or Unix's from ARJ on Unix), the packed and the real size, the
   file's CRC-32, ...), the name and a
   comment, each to a zero, the header's CRC; then extended headers (none are made), then the packed bytes. The
   first header is the archive's own; a header of no size ends it. A header that is none, or whose CRC is of other
   bytes: the archive is damaged (what is after it cannot be found). */
static int arj_open(struct arc *a)
{
    unsigned long at = 0;
    unsigned char *h = malloc(2600 + 16);
    int first = 1, r = ARC_OK;
    if (!h)
        return ARC_E_MEMORY;
    for (;;) {
        unsigned char q[4];
        unsigned hsize, fsize, flags, method, kind, nname;
        unsigned long packed, size, data;
        struct ent *e;
        char *clean;
        int isdir;
        if (at == a->size && !first)
            break;                              /* (its end was not written: read all the same) */
        if (at + 4 > a->size || fseek(a->f, (long)at, SEEK_SET) != 0 || fread(q, 1, 4, a->f) != 4 || q[0] != 0x60
            || q[1] != 0xEA) {
            r = ARC_E_DAMAGED;
            break;
        }
        hsize = le16(q + 2);
        if (!hsize)
            break;                              /* the end */
        if (hsize > 2600 || hsize < 30 || fread(h, 1, hsize + 4, a->f) != hsize + 4
            || arc_crc32(0, h, hsize) != le32(h + hsize)) {
            r = ARC_E_DAMAGED;
            break;
        }
        data = at + 4 + hsize + 4;
        for (;;) {                              /* extended headers: each its size, its bytes, their CRC */
            unsigned x;
            if (data + 2 > a->size || fseek(a->f, (long)data, SEEK_SET) != 0 || fread(q, 1, 2, a->f) != 2) {
                r = ARC_E_DAMAGED;
                break;
            }
            data += 2;
            if (!(x = le16(q)))
                break;
            data += x + 4;
        }
        if (r != ARC_OK)
            break;
        fsize = h[0], flags = h[4], method = h[5], kind = h[6];
        packed = le32(h + 12), size = le32(h + 16);
        if (first) {                            /* the archive's own header: no bytes follow it */
            first = 0;
            at = data;
            continue;
        }
        if (fsize < 30 || fsize >= hsize || data > a->size || packed > a->size - data) {
            r = ARC_E_DAMAGED;
            break;
        }
        for (nname = 0; fsize + nname < hsize && h[fsize + nname]; nname++)
            ;
        isdir = kind == 3;
        clean = clean_name((const char *)h + fsize, nname, &isdir);
        if (kind == 3)
            isdir = 1;
        if (clean) {
            if (!(e = add(a, clean))) {
                free(clean);
                r = ARC_E_MEMORY;
                break;
            }
            e->at = data, e->crc = le32(h + 20);
            e->method = (unsigned short)(method == 0 ? 0 : method <= 3 ? LZH_ARJ : LZH_ARJ4);
            if (h[3] == 2 || h[3] == 8)         /* made on Unix (or NeXT): its time is Unix's */
                dos_stamp(le32(h + 8), &e->e.date, &e->e.time);
            else
                e->e.time = (unsigned short)le16(h + 8), e->e.date = (unsigned short)le16(h + 10);
            e->e.dir = (unsigned char)isdir;
            if (!isdir) {
                e->e.size = size, e->e.packed = packed;
                if (flags & 0x01)
                    e->e.how = ARC_E_LOCKED;
                else if (flags & 0x0C)          /* (a piece of a file that goes on in another volume) */
                    e->e.how = ARC_E_BIG;
                else if (method > 4 || kind > 1)
                    e->e.how = ARC_E_METHOD;
            }
        }
        at = data + packed;
    }
    free(h);
    return r;
}

struct lzh_run {                        /* an entry's bytes going through lzh_unpack */
    struct arc *a;
    unsigned have, at;                  /* of buf */
    unsigned long left;                 /* packed bytes not yet read */
    unsigned long crc, total;
    int crc32;
    int (*data)(void *ctx, const void *p, unsigned n);
    void *ctx;
};

static int lzh_get(void *ctx)
{
    struct lzh_run *g = ctx;
    if (g->at == g->have) {
        unsigned n = g->left < BUF ? (unsigned)g->left : BUF;
        if (!n || (g->have = (unsigned)fread(g->a->buf, 1, n, g->a->f)) == 0)
            return -1;
        g->left -= g->have, g->at = 0;
    }
    return g->a->buf[g->at++];
}

static int lzh_put(void *ctx, const void *p, unsigned n)
{
    struct lzh_run *g = ctx;
    g->crc = g->crc32 ? arc_crc32(g->crc, p, n) : crc16((unsigned)g->crc, p, n);
    g->total += n;
    return g->data && g->data(g->ctx, p, n) != 0 ? ARC_E_STOP : 0;
}

static int lzh_extract(struct arc *a, const struct ent *e, int (*data)(void *ctx, const void *p, unsigned n), void *ctx)
{
    struct lzh_run g;
    int r;
    if (e->e.how)
        return e->e.how;
    if (fseek(a->f, (long)e->at, SEEK_SET) != 0)
        return ARC_E_DAMAGED;
    memset(&g, 0, sizeof g);
    g.a = a, g.left = e->e.packed, g.data = data, g.ctx = ctx, g.crc32 = a->kind == K_ARJ;
    if (e->method == 0) {                       /* not packed */
        unsigned long left;
        if (e->e.packed != e->e.size)
            return ARC_E_DAMAGED;
        for (left = e->e.size; left; ) {
            unsigned char piece[512];           /* (the archive's own buffer is lzh_get's) */
            unsigned n = left < sizeof piece ? (unsigned)left : (unsigned)sizeof piece;
            if (fread(piece, 1, n, a->f) != n)
                return ARC_E_DAMAGED;
            if ((r = lzh_put(&g, piece, n)) != 0)
                return r;
            left -= n;
        }
    } else {
        r = lzh_unpack(e->method, e->e.packed, e->e.size, lzh_get, lzh_put, &g);
        if (r == ARC_E_STOP)
            return r;
        if (r == LZH_MEMORY)
            return ARC_E_MEMORY;
        if (r != 0 || g.total != e->e.size)
            return ARC_E_DAMAGED;
    }
    return g.crc == e->crc ? ARC_OK : ARC_E_CRC;
}

/* ---- 7z: arc7z.c reads it (the LZMA SDK's reader); here, its files as entries. A solid block is unpacked whole
   into memory and kept while its files are taken one after another: given back when a walk ends, and after one
   file's bytes if it is a big one (SZ_KEEP). */
#define SZ_KEEP (1ul << 20)

static int sz_entry(void *ctx, unsigned i, const char *name, unsigned n, int dir, unsigned long size,
                    unsigned long when, int how)
{
    struct arc *a = ctx;
    struct ent *e;
    int isdir = dir;
    char *clean = clean_name(name, n, &isdir);
    if (!clean)
        return ARC_OK;                          /* (a name that would leave the folder: not listed) */
    if (!(e = add(a, clean))) {
        free(clean);
        return ARC_E_MEMORY;
    }
    e->at = i;
    e->e.dir = (unsigned char)(dir || isdir);
    if (when)
        dos_stamp(when, &e->e.date, &e->e.time);
    if (!e->e.dir) {
        e->e.size = e->e.packed = size;
        e->e.how = (signed char)how;
    }
    return ARC_OK;
}

static int sz_take(struct arc *a, const struct ent *e, int (*data)(void *ctx, const void *p, unsigned n), void *ctx)
{
    if (e->e.how)
        return e->e.how;
    return sz_extract(a->sz, a->f, (unsigned)e->at, data, ctx);
}

struct arc *arc_open(const char *path, int *err)
{
    struct arc *a = calloc(1, sizeof *a);
    unsigned char h[512];
    unsigned n, i;
    long size;
    int r = ARC_E_MEMORY, dummy;
    if (!err)
        err = &dummy;
    if (!a) {
        *err = ARC_E_MEMORY;
        return NULL;
    }
    if (!(a->path = strdup(path)))
        goto bad;
    if (!(a->f = file_open(path, "rb")) || fseek(a->f, 0, SEEK_END) != 0 || (size = ftell(a->f)) < 0) {
        r = a->f ? ARC_E_BIG : ARC_E_OPEN;
        goto bad;
    }
    a->size = (unsigned long)size;
    rewind(a->f);
    n = (unsigned)fread(h, 1, sizeof h, a->f);
    if (n >= 2 && h[0] == 0x1F && h[1] == 0x8B) {
        r = gz_open(a, path);
    } else if (n >= 6 && !memcmp(h, "7z\xBC\xAF\x27\x1C", 6)) {
        a->kind = K_7Z;
        r = sz_open(a->f, &a->sz, sz_entry, a);
    } else if (n >= 2 && h[0] == 0x60 && h[1] == 0xEA) {
        a->kind = K_ARJ;
        r = arj_open(a);
    } else if (n >= 21 && h[2] == '-' && h[3] == 'l' && h[6] == '-' && h[20] <= 2) {
        a->kind = K_LZH;
        r = lzh_open(a);
    } else if (n == 512 && tar_header_ok(h)) {
        a->kind = K_TAR;
        r = tar_pass(a, NULL, NULL);
    } else {
        a->kind = K_ZIP;
        r = zip_open(a);
    }
    if (r != ARC_OK)
        goto bad;
    if (a->n) {
        if (!(a->sorted = malloc(a->n * sizeof *a->sorted))) {
            r = ARC_E_MEMORY;
            goto bad;
        }
        for (i = 0; i < a->n; i++) {
            a->ents[i].e.name = a->ents[i].name;
            a->sorted[i] = &a->ents[i];
        }
        qsort(a->sorted, a->n, sizeof *a->sorted, ent_cmp);
    }
    fclose(a->f);                       /* (opened again when its bytes are wanted) */
    a->f = NULL;
    *err = ARC_OK;
    return a;
bad:
    *err = r;
    arc_close(a);
    return NULL;
}

void arc_close(struct arc *a)
{
    unsigned i;
    if (!a)
        return;
    if (a->f)
        fclose(a->f);
    free(a->path);
    for (i = 0; i < a->n; i++)
        free(a->ents[i].name);
    for (i = 0; a->dos && i < a->n; i++)
        free(a->dos[i]);
    free(a->dos);
    free(a->ents);
    free(a->sorted);
    free(a->z);
    free(a->win);
    sz_close(a->sz);
    free(a);
}

const char *arc_kind(const struct arc *a)
{
    static const char *const names[] = { "ZIP", "TAR", "GZ", "TAR.GZ", "LZH", "ARJ", "7Z" };
    return names[a->kind];
}

unsigned arc_count(const struct arc *a) { return a->n; }
const struct arc_entry *arc_at(const struct arc *a, unsigned i) { return i < a->n ? &a->ents[i].e : NULL; }

int arc_find(const struct arc *a, const char *name)
{
    unsigned lo = 0, hi = a->n;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        int c = name_cmp(a->sorted[mid]->name, name);
        if (!c)
            return (int)(a->sorted[mid] - a->ents);
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

int arc_list(const struct arc *a, const char *dir,
             int (*each)(void *ctx, const char *name, int is_dir, const struct arc_entry *e), void *ctx)
{
    size_t dlen = strlen(dir), plen = dlen ? dlen + 1 : 0, clen, last_len = 0;
    const char *last = NULL;
    char part[260];
    unsigned i;
    int count = 0;
    for (i = 0; i < a->n; i++) {
        const struct ent *e = a->sorted[i];
        const char *rest, *slash;
        if (dlen && (strncmp(e->name, dir, dlen) != 0 || e->name[dlen] != '/'))
            continue;
        rest = e->name + plen;
        slash = strchr(rest, '/');
        clen = slash ? (size_t)(slash - rest) : strlen(rest);
        if (!clen || (last && clen == last_len && !memcmp(rest, last, clen)))
            continue;                   /* (more of a folder told already) */
        last = rest, last_len = clen;
        snprintf(part, sizeof part, "%.*s", (int)clen, rest);
        count++;
        if (each && each(ctx, part, slash != NULL || e->e.dir, slash ? NULL : &e->e) != 0)
            break;
    }
    return count;
}

/* The archive's file open for reading while its bytes are wanted, and shut again after. */
static int arc_use(struct arc *a)
{
    if (!a->users && !(a->f = file_open(a->path, "rb")))
        return ARC_E_OPEN;
    a->users++;
    return ARC_OK;
}

static void arc_unuse(struct arc *a)
{
    if (!--a->users) {
        fclose(a->f);
        a->f = NULL;
    }
}

static int walk(struct arc *a, const struct arc_sink *s, void *ctx)
{
    unsigned i;
    int r;
    if (a->kind == K_TAR)
        return tar_pass(a, s, ctx);
    if (a->kind == K_TGZ)
        return tgz_pass(a, s, ctx);
    for (i = 0; i < a->n; i++) {
        const struct ent *e = &a->ents[i];
        r = s->begin ? s->begin(ctx, i, &e->e) : 0;
        if (r < 0)
            break;
        if (!r)
            continue;
        if (e->e.dir)
            r = ARC_OK;
        else if (a->kind == K_ZIP)
            r = zip_extract(a, e, s->data, ctx);
        else if (a->kind == K_LZH || a->kind == K_ARJ)
            r = lzh_extract(a, e, s->data, ctx);
        else if (a->kind == K_7Z)
            r = sz_take(a, e, s->data, ctx);
        else
            r = gz_extract(a, e, s->data, ctx);
        s->end(ctx, i, r);
        if (r == ARC_E_STOP)
            break;
    }
    if (a->sz)
        sz_drop(a->sz);
    return i < a->n ? ARC_E_STOP : ARC_OK;
}

int arc_walk(struct arc *a, const struct arc_sink *s, void *ctx)
{
    int r = arc_use(a);
    if (r != ARC_OK)
        return r;
    r = walk(a, s, ctx);
    arc_unuse(a);
    return r;
}

/* One entry out of a TAR inside a GZ: the walk, taking it alone and stopping after it. */
struct one {
    unsigned want;
    int (*data)(void *ctx, const void *p, unsigned n);
    void *ctx;
    int got, err;
};

static int one_begin(void *ctx, unsigned i, const struct arc_entry *e)
{
    struct one *o = ctx;
    (void)e;
    return o->got ? -1 : i == o->want;
}

static int one_data(void *ctx, const void *p, unsigned n)
{
    struct one *o = ctx;
    return o->data ? o->data(o->ctx, p, n) : 0;
}

static void one_end(void *ctx, unsigned i, int err)
{
    struct one *o = ctx;
    (void)i;
    o->got = 1, o->err = err;
}

static int extract(struct arc *a, unsigned i, int (*data)(void *ctx, const void *p, unsigned n), void *ctx);

int arc_extract(struct arc *a, unsigned i, int (*data)(void *ctx, const void *p, unsigned n), void *ctx)
{
    int r;
    if (i >= a->n)
        return ARC_E_DAMAGED;
    if (a->ents[i].e.dir)
        return ARC_OK;
    if ((r = arc_use(a)) != ARC_OK)
        return r;
    r = extract(a, i, data, ctx);
    arc_unuse(a);
    return r;
}

static int extract(struct arc *a, unsigned i, int (*data)(void *ctx, const void *p, unsigned n), void *ctx)
{
    const struct ent *e = &a->ents[i];
    if (a->kind == K_ZIP)
        return zip_extract(a, e, data, ctx);
    if (a->kind == K_LZH || a->kind == K_ARJ)
        return lzh_extract(a, e, data, ctx);
    if (a->kind == K_7Z) {
        int r = sz_take(a, e, data, ctx);
        if (sz_kept(a->sz) > SZ_KEEP)
            sz_drop(a->sz);
        return r;
    }
    if (a->kind == K_GZ)
        return gz_extract(a, e, data, ctx);
    if (a->kind == K_TAR) {             /* its bytes are where the list says */
        unsigned long left;
        if (e->at > a->size || e->e.size > a->size - e->at || fseek(a->f, (long)e->at, SEEK_SET) != 0)
            return ARC_E_DAMAGED;
        for (left = e->e.size; left; ) {
            unsigned n = left < BUF ? (unsigned)left : BUF;
            if (fread(a->buf, 1, n, a->f) != n)
                return ARC_E_DAMAGED;
            if (data && data(ctx, a->buf, n) != 0)
                return ARC_E_STOP;
            left -= n;
        }
        return ARC_OK;
    } else {
        static const struct arc_sink sink = { one_begin, one_data, one_end };
        struct one o;
        int r;
        memset(&o, 0, sizeof o);
        o.want = i, o.data = data, o.ctx = ctx;
        r = walk(a, &sink, &o);
        if (o.got)
            return o.err;
        return r == ARC_OK ? ARC_E_DAMAGED : r;
    }
}

/* ---- names for a DOS disc */

void arc_dos_part(const char *part, unsigned len, unsigned n, char *out)
{
    static const char *const devices[] = { "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "LPT1", "LPT2",
                                           "LPT3", "CLOCK$" };
    char base[9], ext[4], num[12];
    unsigned i, b = 0, x = 0, dot = len, k;
    while (len && (*part == '.' || *part == ' '))       /* (".profile" is PROFILE) */
        part++, len--;
    for (i = 0, dot = len; i < len; i++)
        if (part[i] == '.')
            dot = i;
    for (i = 0; i < len; i++) {
        int c = (unsigned char)part[i];
        if (c == ' ' || c == '.')
            continue;
        if (c >= 'a' && c <= 'z')
            c -= 32;
        else if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c && strchr("!#$%&'()-@^_`{}~", c))))
            c = '_';
        if (i < dot) {
            if (b < 8)
                base[b++] = (char)c;
        } else if (x < 3) {
            ext[x++] = (char)c;
        }
    }
    if (!b)
        base[b++] = '_';
    base[b] = 0, ext[x] = 0;
    for (i = 0; i < sizeof devices / sizeof devices[0]; i++)
        if (!strcmp(base, devices[i])) {                /* (DOS's devices are no files' names) */
            if (b == 8)
                b--;
            base[b++] = '_';
            base[b] = 0;
        }
    if (n) {
        k = (unsigned)snprintf(num, sizeof num, "~%u", n % 10000000u);  /* (eight characters at most) */
        if (b + k > 8)
            b = 8 - k;
        memcpy(base + b, num, k + 1);
    }
    snprintf(out, 13, "%s%s%s", base, x ? "." : "", ext);
}

struct used {                           /* the names taken in one folder */
    char (*names)[13];
    unsigned n, room;
};

static int used_has(const struct used *u, const char *name)
{
    unsigned i;
    for (i = 0; i < u->n; i++)
        if (!strcmp(u->names[i], name))
            return 1;
    return 0;
}

static int used_add(struct used *u, const char *name)
{
    if (u->n == u->room) {
        unsigned room = u->room ? u->room * 2 : 32;
        void *m = realloc(u->names, room * sizeof *u->names);
        if (!m)
            return -1;
        u->names = m, u->room = room;
    }
    strcpy(u->names[u->n++], name);
    return 0;
}

#define DEPTH 32                        /* folders deep; what lies deeper goes into the last of them (DOS's own
                                           paths end long before) */

/* Every entry's path, in the names' order: a folder's things lie together there, so the names taken in each
   folder on the way down to the entry in hand are all that has to be remembered. used[L]: the names taken in the
   folder L parts down; part[L]: the name given to the entry in hand's part L. */
static int dos_paths(struct arc *a)
{
    struct used used[DEPTH];
    char part[DEPTH][13];
    const char *prev = "";
    unsigned i, level, depth = 0;       /* depth: the parts of prev whose names are in part[] */
    int r = -1;
    char **dos = calloc(a->n ? a->n : 1, sizeof *dos);
    if (!dos)
        return -1;
    memset(used, 0, sizeof used);
    for (i = 0; i < a->n; i++) {
        const struct ent *e = a->sorted[i];
        const char *p = e->name, *q = prev, *pe;
        char path[DEPTH * 13 + 1];
        unsigned same = 0, o = 0, parts;
        /* The folders it shares with the one before keep their names. */
        while (same < depth && (pe = strchr(p, '/')) != NULL) {
            const char *qe = strchr(q, '/');
            size_t ql = qe ? (size_t)(qe - q) : strlen(q);
            if ((size_t)(pe - p) != ql || memcmp(p, q, ql) != 0)
                break;
            same++;
            p = pe + 1;
            q = qe ? qe + 1 : q + ql;
        }
        for (level = same + 1; level < DEPTH; level++)  /* the folders left behind: their names are forgotten */
            used[level].n = 0;
        for (level = same; ; level++, p = pe + 1) {
            unsigned len, n, at;
            pe = strchr(p, '/');
            len = pe ? (unsigned)(pe - p) : (unsigned)strlen(p);
            if (pe && level >= DEPTH - 1)
                continue;               /* (a folder too deep: left out) */
            at = level < DEPTH ? level : DEPTH - 1;
            for (n = 0; ; n++) {
                arc_dos_part(p, len, n, part[at]);
                if (!used_has(&used[at], part[at]))
                    break;
            }
            if (used_add(&used[at], part[at]) != 0)
                goto out;
            if (!pe)
                break;
        }
        parts = level < DEPTH ? level + 1 : DEPTH;
        depth = level < DEPTH ? parts : DEPTH - 1;      /* (a file put into the deepest folder is no folder's name) */
        for (level = 0; level < parts; level++)
            o += (unsigned)snprintf(path + o, sizeof path - o, "%s%s", level ? "\\" : "", part[level]);
        if (!(dos[e - a->ents] = malloc(o + 1)))
            goto out;
        memcpy(dos[e - a->ents], path, o + 1);
        prev = e->name;
    }
    r = 0;
out:
    for (level = 0; level < DEPTH; level++)
        free(used[level].names);
    if (r != 0) {
        for (i = 0; i < a->n; i++)
            free(dos[i]);
        free(dos);
        return -1;
    }
    a->dos = dos;
    return 0;
}

const char *arc_dos_path(struct arc *a, unsigned i)
{
    if (i >= a->n || (!a->dos && dos_paths(a) != 0))
        return NULL;
    return a->dos[i];
}

/* The first entry, in the names' order, that is `name` or lies under it; -1 if none does. */
static int first_under(const struct arc *a, const char *name)
{
    size_t n = strlen(name);
    unsigned lo = 0, hi = a->n;
    while (lo < hi) {                   /* (the first whose name is not before `name`) */
        unsigned mid = lo + (hi - lo) / 2;
        if (name_cmp(a->sorted[mid]->name, name) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo < a->n && !strncmp(a->sorted[lo]->name, name, n) && (!a->sorted[lo]->name[n] || a->sorted[lo]->name[n] == '/'))
        return (int)(a->sorted[lo] - a->ents);
    return -1;
}

int arc_has_folder(const struct arc *a, const char *name)
{
    int i;
    if (!name[0])
        return 1;
    i = first_under(a, name);
    return i >= 0 && (a->ents[i].e.dir || a->ents[i].name[strlen(name)] == '/');
}

int arc_dos_of(struct arc *a, const char *name, char *out, unsigned room)
{
    int i = first_under(a, name);
    const char *d, *p, *end;
    unsigned parts = 1;
    if (!room)
        return -1;
    out[0] = 0;
    if (!name[0])
        return 0;
    if (i < 0 || !(d = arc_dos_path(a, (unsigned)i)))
        return -1;
    for (p = name; *p; p++)
        parts += *p == '/';
    for (end = d; *end && (*end != '\\' || --parts); end++)     /* as many parts of its path as the name has */
        ;
    if ((unsigned)(end - d) >= room)
        return -1;
    memcpy(out, d, (size_t)(end - d));
    out[end - d] = 0;
    return 0;
}

/* ---- ZIPs written */

struct arcw {
    FILE *f;
    char path[260];
    struct defl *d;
    int level, err;
    int created;                        /* the file is this writer's own (given up: taken away) */
    unsigned long base;                 /* where the archive begins in its file */
    unsigned char *cd;                  /* the directory so far */
    unsigned long cd_n, cd_room;
    unsigned count;
    unsigned char *comment;
    unsigned ncomment;
    unsigned char *old;                 /* added to: the old directory and end, to put back if it is given up ... */
    unsigned long old_at, old_n;        /* ... and where they were */
    int open;                           /* an entry is begun */
    unsigned long head_at, crc, size, packed, cd_entry;
};

static void wr16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8); }
static void wr32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

static int w_fail(struct arcw *w, int err)
{
    if (!w->err)
        w->err = err;
    return w->err;
}

static int w_write(struct arcw *w, const void *p, unsigned long n)
{
    if (!w->err && n && fwrite(p, 1, n, w->f) != n)
        w->err = ARC_E_OPEN;
    return w->err;
}

struct arcw *arcw_open(const char *path, int add, int level, int *err)
{
    struct arcw *w = calloc(1, sizeof *w);
    int dummy, r = ARC_E_MEMORY;
    if (!err)
        err = &dummy;
    if (!w || !(w->d = malloc(deflate_state_size())) || strlen(path) >= sizeof w->path)
        goto bad;
    strcpy(w->path, path);
    w->level = level;
    if (add && (w->f = file_open(path, "r+b")) != NULL) {
        struct zipdir z;
        long size;
        if (fseek(w->f, 0, SEEK_END) != 0 || (size = ftell(w->f)) < 0) {
            r = ARC_E_BIG;
            goto bad;
        }
        if ((r = zip_dir(w->f, (unsigned long)size, &z)) != ARC_OK)
            goto bad;
        w->base = z.base;
        w->cd = z.cd, w->cd_n = w->cd_room = z.cd_size;
        w->comment = z.comment, w->ncomment = z.ncomment;
        w->count = z.count;
        {                               /* (a directory that is not what it says is not added to) */
            unsigned long at = 0;
            unsigned k;
            for (k = 0; k < z.count; k++)
                if (!zip_dir_next(&z, &at)) {
                    r = ARC_E_DAMAGED;
                    goto bad;
                }
        }
        w->old_at = z.base + z.cd_at;
        w->old_n = (unsigned long)size - w->old_at;
        r = ARC_E_MEMORY;
        if (!(w->old = malloc(w->old_n ? w->old_n : 1)))
            goto bad;
        r = ARC_E_OPEN;
        if (fseek(w->f, (long)w->old_at, SEEK_SET) != 0 || fread(w->old, 1, w->old_n, w->f) != w->old_n
            || fseek(w->f, (long)w->old_at, SEEK_SET) != 0)
            goto bad;                   /* (new entries go where the old directory began) */
    } else {
        if (!(w->f = file_open(path, "wb"))) {
            r = ARC_E_OPEN;
            goto bad;
        }
        w->created = 1;
    }
    *err = ARC_OK;
    return w;
bad:
    *err = r;
    if (w) {
        if (w->f)
            fclose(w->f);
        free(w->d);
        free(w->cd);
        free(w->comment);
        free(w->old);
        free(w);
    }
    return NULL;
}

/* An entry's two headers: the one before its bytes written (its sizes and check are put in at its end), and
   the directory's kept. */
static int w_entry(struct arcw *w, const char *name, int dir, unsigned date, unsigned time)
{
    unsigned char h[46];
    size_t nlen = strlen(name);
    long at;
    unsigned method = dir ? 0 : 8;
    if (w->err)
        return w->err;
    if (w->open)
        return w_fail(w, ARC_E_DAMAGED);
    if (!nlen || nlen > 1000 || name[0] == '/' || w->count >= 0xFFFE)
        return w_fail(w, ARC_E_BIG);
    if ((at = ftell(w->f)) < 0 || (unsigned long)at < w->base)
        return w_fail(w, ARC_E_OPEN);
    w->head_at = (unsigned long)at;
    if (w->cd_n + 46 + nlen > w->cd_room) {
        unsigned long room = (w->cd_n + 46 + nlen) * 2 + 1024;
        unsigned char *m = realloc(w->cd, room);
        if (!m)
            return w_fail(w, ARC_E_MEMORY);
        w->cd = m, w->cd_room = room;
    }
    memset(h, 0, sizeof h);
    wr32(h, 0x04034B50ul);
    wr16(h + 4, 20);
    wr16(h + 8, method);
    wr16(h + 10, time), wr16(h + 12, date);
    wr16(h + 26, (unsigned)nlen);
    w_write(w, h, 30);
    w_write(w, name, nlen);
    memset(h, 0, sizeof h);
    wr32(h, 0x02014B50ul);
    wr16(h + 4, 20), wr16(h + 6, 20);
    wr16(h + 10, method);
    wr16(h + 12, time), wr16(h + 14, date);
    wr16(h + 28, (unsigned)nlen);
    wr32(h + 38, dir ? 0x10 : 0x20);
    wr32(h + 42, w->head_at - w->base);
    w->cd_entry = w->cd_n;
    memcpy(w->cd + w->cd_n, h, 46);
    memcpy(w->cd + w->cd_n + 46, name, nlen);
    w->cd_n += 46 + nlen;
    w->count++;
    return w->err;
}

int arcw_folder(struct arcw *w, const char *name, unsigned date, unsigned time)
{
    char with[1002];
    size_t n = strlen(name);
    if (n > 999)
        return w_fail(w, ARC_E_BIG);
    snprintf(with, sizeof with, "%s%s", name, n && name[n - 1] == '/' ? "" : "/");
    return w_entry(w, with, 1, date, time);
}

static int w_put(void *ctx, const void *p, unsigned n)
{
    struct arcw *w = ctx;
    if (w->packed + n < w->packed)
        return ARC_E_BIG;
    w->packed += n;
    return w_write(w, p, n);
}

int arcw_begin(struct arcw *w, const char *name, unsigned date, unsigned time)
{
    if (w_entry(w, name, 0, date, time) != 0)
        return w->err;
    w->open = 1;
    w->crc = 0, w->size = 0, w->packed = 0;
    deflate_begin(w->d, w->level, w_put, w);
    return ARC_OK;
}

int arcw_data(struct arcw *w, const void *p, unsigned n)
{
    int r;
    if (w->err)
        return w->err;
    if (!w->open)
        return w_fail(w, ARC_E_DAMAGED);
    if (w->size + n < w->size)
        return w_fail(w, ARC_E_BIG);
    w->crc = arc_crc32(w->crc, p, n);
    w->size += n;
    if ((r = deflate_data(w->d, p, n)) != 0)
        return w_fail(w, r);
    return ARC_OK;
}

int arcw_end(struct arcw *w)
{
    unsigned char h[12];
    long end;
    int r;
    if (w->err)
        return w->err;
    if (!w->open)
        return w_fail(w, ARC_E_DAMAGED);
    w->open = 0;
    if ((r = deflate_end(w->d)) != 0)
        return w_fail(w, r);
    wr32(h, w->crc), wr32(h + 4, w->packed), wr32(h + 8, w->size);
    if ((end = ftell(w->f)) < 0 || fseek(w->f, (long)(w->head_at + 14), SEEK_SET) != 0)
        return w_fail(w, ARC_E_OPEN);
    w_write(w, h, 12);
    if (fseek(w->f, end, SEEK_SET) != 0)
        return w_fail(w, ARC_E_OPEN);
    memcpy(w->cd + w->cd_entry + 16, h, 12);
    return w->err;
}

int arcw_close(struct arcw *w, int keep)
{
    unsigned char e[22];
    long at = 0;
    int r;
    if (!w)
        return ARC_E_MEMORY;
    if (keep && w->open)
        w_fail(w, ARC_E_DAMAGED);
    if (keep && !w->err) {
        if ((at = ftell(w->f)) < 0 || (unsigned long)at - w->base > 0xFFFFFFFEul - w->cd_n)
            w_fail(w, at < 0 ? ARC_E_OPEN : ARC_E_BIG);
        memset(e, 0, sizeof e);
        wr32(e, 0x06054B50ul);
        wr16(e + 8, w->count), wr16(e + 10, w->count);
        wr32(e + 12, w->cd_n);
        wr32(e + 16, (unsigned long)at - w->base);
        wr16(e + 20, w->ncomment);
        w_write(w, w->cd, w->cd_n);
        w_write(w, e, 22);
        w_write(w, w->comment, w->ncomment);
        if (!w->err && fflush(w->f) != 0)
            w->err = ARC_E_OPEN;
    }
    r = keep ? w->err : ARC_E_STOP;
    if (r != ARC_OK && !w->created) {           /* the old archive again: its directory back where it was */
        clearerr(w->f);
        if (fseek(w->f, (long)w->old_at, SEEK_SET) != 0 || fwrite(w->old, 1, w->old_n, w->f) != w->old_n
            || fflush(w->f) != 0 || ftruncate(fileno(w->f), (off_t)(w->old_at + w->old_n)) != 0)
            r = keep ? r : ARC_E_OPEN;          /* (and if that cannot be done, it is damaged: said even to one who gave up) */
    }
    if (fclose(w->f) != 0 && r == ARC_OK)
        r = ARC_E_OPEN;
    if (r != ARC_OK && w->created)
        file_unlink(w->path);
    free(w->d);
    free(w->cd);
    free(w->comment);
    free(w->old);
    free(w);
    return r;
}

/* A ZIP written again beside itself and put in its own place: without the entries drop says 1 for (drop NULL:
   none), and with `from` (an entry, or a folder and all under it) under the name `to` (from NULL: none). Each
   entry's packed bytes go over as they are. How many went or were renamed, or an error (the old archive is as it
   was). */
static int zip_rewrite(const char *path, int (*drop)(void *ctx, const char *name), void *ctx, const char *from,
                       const char *to)
{
    struct zipdir z;
    char tmp[260];
    FILE *in = file_open(path, "rb"), *out = NULL;
    unsigned char *cd = NULL, *buf = NULL, e[22], h[30];
    unsigned long at = 0, cd_n = 0, left;
    unsigned k, kept = 0, gone = 0;
    long size, pos;
    size_t n = strlen(path), dot, flen = from ? strlen(from) : 0, tlen = to ? strlen(to) : 0;
    int r;
    memset(&z, 0, sizeof z);
    if (!in)
        return ARC_E_OPEN;
    r = ARC_E_BIG;
    if (n + 5 > sizeof tmp || fseek(in, 0, SEEK_END) != 0 || (size = ftell(in)) < 0)
        goto out;
    if ((r = zip_dir(in, (unsigned long)size, &z)) != ARC_OK)
        goto out;
    for (dot = n; dot > 0 && path[dot - 1] != '.' && path[dot - 1] != '/' && path[dot - 1] != '\\' && path[dot - 1] != ':'; dot--)
        ;
    if (dot > 0 && path[dot - 1] == '.')        /* beside it, with another ending: ARCHIVE.$$$ */
        snprintf(tmp, sizeof tmp, "%.*s$$$", (int)dot, path);
    else
        snprintf(tmp, sizeof tmp, "%s.$$$", path);
    r = ARC_E_MEMORY;                           /* (a renamed entry's line in the directory may be longer) */
    if (!(cd = malloc(z.cd_size + (unsigned long)z.count * (tlen + 2) + 1)) || !(buf = malloc(BUF)))
        goto out;
    r = ARC_E_OPEN;
    if (!(out = file_open(tmp, "wb")))
        goto out;
    rewind(in);
    for (left = z.base; left; left -= k) {      /* (a program before the archive stays before it) */
        k = left < BUF ? (unsigned)left : BUF;
        if (fread(buf, 1, k, in) != k || fwrite(buf, 1, k, out) != k)
            goto out;
    }
    for (k = 0; k < z.count; k++) {
        const unsigned char *c = zip_dir_next(&z, &at);
        unsigned long len;
        unsigned nlen, wlen;
        char *name, *renamed = NULL;
        const unsigned char *wname;
        int dir, go;
        r = ARC_E_DAMAGED;
        if (!c)
            goto out;
        r = ARC_E_BIG;
        if (le32(c + 20) == 0xFFFFFFFFul || le32(c + 42) == 0xFFFFFFFFul)
            goto out;
        nlen = le16(c + 28);
        len = 46ul + nlen + le16(c + 30) + le16(c + 32);
        name = clean_name((const char *)c + 46, nlen, &dir);
        go = name && drop && drop(ctx, name);
        if (name && !go && from && !strncmp(name, from, flen) && (!name[flen] || name[flen] == '/')) {
            size_t rest = strlen(name + flen);  /* the entry itself, or one under the folder: its new name */
            r = ARC_E_BIG;
            if (tlen + rest + 1 > 0xFFFF) {
                free(name);
                goto out;
            }
            r = ARC_E_MEMORY;
            if (!(renamed = malloc(tlen + rest + 2))) {
                free(name);
                goto out;
            }
            memcpy(renamed, to, tlen);
            memcpy(renamed + tlen, name + flen, rest);
            if (dir)
                renamed[tlen + rest++] = '/';
            renamed[tlen + rest] = 0;
            gone++;
        }
        free(name);
        if (go) {
            gone++;
            continue;
        }
        wname = renamed ? (const unsigned char *)renamed : c + 46;
        wlen = renamed ? (unsigned)strlen(renamed) : nlen;
        /* Its bytes as they are, behind a header made from the directory's (which has its sizes for certain). */
        r = ARC_E_DAMAGED;
        if (fseek(in, (long)(z.base + le32(c + 42)), SEEK_SET) != 0 || fread(h, 1, 30, in) != 30 || le32(h) != 0x04034B50ul
            || fseek(in, (long)(le16(h + 26) + le16(h + 28)), SEEK_CUR) != 0) {
            free(renamed);
            goto out;
        }
        r = ARC_E_OPEN;
        if ((pos = ftell(out)) < 0) {
            free(renamed);
            goto out;
        }
        memset(h, 0, sizeof h);
        wr32(h, 0x04034B50ul);
        wr16(h + 4, le16(c + 6));
        wr16(h + 6, le16(c + 8) & ~8u);         /* (its sizes are in the header now, not after its bytes) */
        memcpy(h + 8, c + 10, 18);              /* how it is packed, its time, its check and sizes */
        wr16(h + 26, wlen);
        if (fwrite(h, 1, 30, out) != 30 || fwrite(wname, 1, wlen, out) != wlen) {
            free(renamed);
            goto out;
        }
        memcpy(cd + cd_n, c, 46);               /* its line in the directory: the name it has now, the rest as it was */
        memcpy(cd + cd_n + 46, wname, wlen);
        memcpy(cd + cd_n + 46 + wlen, c + 46 + nlen, len - 46 - nlen);
        wr16(cd + cd_n + 28, wlen);
        free(renamed);
        for (left = le32(c + 20); left; left -= n) {
            n = left < BUF ? (size_t)left : BUF;
            r = ARC_E_DAMAGED;
            if (fread(buf, 1, n, in) != n)
                goto out;
            r = ARC_E_OPEN;
            if (fwrite(buf, 1, n, out) != n)
                goto out;
        }
        wr16(cd + cd_n + 8, le16(c + 8) & ~8u);
        wr32(cd + cd_n + 42, (unsigned long)pos - z.base);
        cd_n += len - nlen + wlen;
        kept++;
    }
    r = ARC_E_OPEN;
    if ((pos = ftell(out)) < 0)
        goto out;
    memset(e, 0, sizeof e);
    wr32(e, 0x06054B50ul);
    wr16(e + 8, kept), wr16(e + 10, kept);
    wr32(e + 12, cd_n);
    wr32(e + 16, (unsigned long)pos - z.base);
    wr16(e + 20, z.ncomment);
    if (fwrite(cd, 1, cd_n, out) != cd_n || fwrite(e, 1, 22, out) != 22
        || fwrite(z.comment, 1, z.ncomment, out) != z.ncomment || fflush(out) != 0)
        goto out;
    r = fclose(out) == 0 ? ARC_OK : ARC_E_OPEN;
    out = NULL;
    fclose(in);
    in = NULL;
    if (r == ARC_OK && gone) {              /* in the old one's place (a rename that will not write over it: by hand) */
        if (file_rename(tmp, path) != 0 && (file_unlink(path) != 0 || file_rename(tmp, path) != 0))
            r = ARC_E_OPEN;
    } else {
        file_unlink(tmp);                    /* (nothing went: the archive is as it is) */
    }
out:
    if (out) {
        fclose(out);
        file_unlink(tmp);
    }
    if (in)
        fclose(in);
    zip_dir_free(&z);
    free(cd);
    free(buf);
    return r == ARC_OK ? (int)gone : r;
}

int arc_zip_remove(const char *path, int (*drop)(void *ctx, const char *name), void *ctx)
{
    return zip_rewrite(path, drop, ctx, NULL, NULL);
}

int arc_zip_rename(const char *path, const char *from, const char *to)
{
    if (!from || !to || !from[0] || !to[0])
        return 0;
    return zip_rewrite(path, NULL, NULL, from, to);
}
