/* 7z archives read (arc7z.h), for apps/shared/arc.c. The archive's own format (its header, itself packed; solid
 * blocks of files packed as one stream through a chain of coders) and LZMA, LZMA2 and the filters are the LZMA
 * SDK's code, unchanged (third_party/lzma: Igor Pavlov's, public domain); this is the little between it and
 * arc.c: a file to read from, the names as UTF-8, the times as Unix's, and what cannot be unpacked said for each
 * file before it is tried.
 *
 * The SDK unpacks a solid block whole into memory, and so do we: a block larger than the memory there is gives
 * ARC_E_MEMORY for its files (Files says so). One block is kept at a time. */
#include <stdlib.h>
#include <string.h>

#include "arc.h"
#include "arc7z.h"
#include "../../third_party/lzma/7z.h"
#include "../../third_party/lzma/7zCrc.h"

UInt32 Z7_FASTCALL CrcCalc(const void *data, size_t size)       /* (the SDK's is tables for every CPU: ours does) */
{
    return (UInt32)arc_crc32(0, data, (unsigned)size);
}

static void *sz_alloc(ISzAllocPtr p, size_t size)
{
    (void)p;
    return size ? malloc(size) : NULL;
}

static void sz_free(ISzAllocPtr p, void *address)
{
    (void)p;
    free(address);
}

static const ISzAlloc alloc = { sz_alloc, sz_free };

struct sz_in {                          /* the archive's file, as the SDK reads one */
    ISeekInStream vt;
    FILE *f;
};

static SRes in_read(ISeekInStreamPtr pp, void *buf, size_t *size)
{
    struct sz_in *in = Z7_CONTAINER_FROM_VTBL(pp, struct sz_in, vt);
    size_t want = *size;
    *size = fread(buf, 1, want, in->f);
    return *size == 0 && want != 0 && ferror(in->f) ? SZ_ERROR_READ : SZ_OK;
}

static SRes in_seek(ISeekInStreamPtr pp, Int64 *pos, ESzSeek origin)
{
    struct sz_in *in = Z7_CONTAINER_FROM_VTBL(pp, struct sz_in, vt);
    long at;
    if (*pos > 0x7FFFFFFF || *pos < -0x7FFFFFFF
        || fseek(in->f, (long)*pos, origin == SZ_SEEK_SET ? SEEK_SET : origin == SZ_SEEK_CUR ? SEEK_CUR : SEEK_END) != 0
        || (at = ftell(in->f)) < 0)
        return SZ_ERROR_READ;
    *pos = at;
    return SZ_OK;
}

#define LOOK (1u << 14)

struct sz {
    CSzArEx db;
    UInt32 block;                       /* the solid block kept unpacked (0xFFFFFFFF: none) */
    Byte *out;
    size_t out_size;
    UInt32 bad;                         /* a block that did not unpack (0xFFFFFFFF: none), and why: not tried again */
    int bad_r;                          /* for each of its files */
};

struct look {                           /* a file being read: the SDK's buffer over it */
    struct sz_in in;
    CLookToRead2 look;
};

static int look_begin(struct look *l, FILE *f)
{
    l->in.vt.Read = in_read;
    l->in.vt.Seek = in_seek;
    l->in.f = f;
    LookToRead2_CreateVTable(&l->look, False);
    l->look.buf = malloc(LOOK);
    if (!l->look.buf)
        return ARC_E_MEMORY;
    l->look.bufSize = LOOK;
    l->look.realStream = &l->in.vt;
    LookToRead2_INIT(&l->look)
    return ARC_OK;
}

static int error_of(SRes res)
{
    switch (res) {
    case SZ_OK: return ARC_OK;
    case SZ_ERROR_MEM: return ARC_E_MEMORY;
    case SZ_ERROR_UNSUPPORTED: return ARC_E_METHOD;
    case SZ_ERROR_CRC: return ARC_E_CRC;
    case SZ_ERROR_NO_ARCHIVE: return ARC_E_KIND;
    default: return ARC_E_DAMAGED;
    }
}

/* What the SDK unpacks (7zDec.c as it is built here), and the coder that means a password. */
static int folder_how(const CSzAr *p, UInt32 fi)
{
    CSzFolder folder;
    CSzData sd;
    UInt32 k;
    sd.Data = p->CodersData + p->FoCodersOffsets[fi];
    sd.Size = p->FoCodersOffsets[fi + 1] - p->FoCodersOffsets[fi];
    if (SzGetNextFolderItem(&folder, &sd) != SZ_OK)
        return ARC_E_METHOD;
    for (k = 0; k < folder.NumCoders; k++)
        if (folder.Coders[k].MethodID == 0x6F10701ul)
            return ARC_E_LOCKED;        /* AES */
    for (k = 0; k < folder.NumCoders; k++)
        switch (folder.Coders[k].MethodID) {
        case 0: case 0x21: case 0x30101ul: case 0x303011Bul:            /* not packed, LZMA2, LZMA, BCJ2 */
        case 3: case 0xa: case 0xb:                                     /* delta, ARM64, RISC-V */
        case 0x3030103ul: case 0x3030205ul: case 0x3030401ul:           /* the programs' filters: x86, PowerPC, */
        case 0x3030501ul: case 0x3030701ul: case 0x3030805ul:           /* IA-64, ARM, ARM Thumb, SPARC */
            break;
        default:
            return ARC_E_METHOD;        /* (PPMd, BZip2, Deflate ...) */
        }
    return ARC_OK;
}

/* An archive the SDK would not open as "unsupported": is its header locked with a password? (The header is
   then packed through AES: the coder's number is in the few bytes that say how.) */
static int header_locked(FILE *f)
{
    unsigned char h[32], t[64];
    unsigned long at;
    size_t n, i;
    if (fseek(f, 0, SEEK_SET) != 0 || fread(h, 1, 32, f) != 32 || h[16] | h[17] | h[18] | h[19])
        return 0;
    at = 32 + (h[12] | (unsigned long)h[13] << 8 | (unsigned long)h[14] << 16 | (unsigned long)h[15] << 24);
    if (fseek(f, (long)at, SEEK_SET) != 0 || (n = fread(t, 1, sizeof t, f)) < 8 || t[0] != 0x17)
        return 0;
    for (i = 0; i + 4 <= n; i++)
        if (t[i] == 0x06 && t[i + 1] == 0xF1 && t[i + 2] == 0x07 && t[i + 3] == 0x01)
            return 1;
    return 0;
}

static unsigned utf8(unsigned long c, char *out)
{
    if (c < 0x80) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 0x800) {
        out[0] = (char)(0xC0 | c >> 6), out[1] = (char)(0x80 | (c & 0x3F));
        return 2;
    }
    if (c < 0x10000ul) {
        out[0] = (char)(0xE0 | c >> 12), out[1] = (char)(0x80 | (c >> 6 & 0x3F)), out[2] = (char)(0x80 | (c & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | c >> 18), out[1] = (char)(0x80 | (c >> 12 & 0x3F));
    out[2] = (char)(0x80 | (c >> 6 & 0x3F)), out[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

int sz_open(FILE *f, struct sz **out,
            int (*entry)(void *ctx, unsigned i, const char *name, unsigned n, int dir, unsigned long size,
                         unsigned long when, int how),
            void *ctx)
{
    struct sz *z = calloc(1, sizeof *z);
    struct look l;
    UInt16 *wide = NULL;
    char *name = NULL;
    size_t room = 0;
    UInt32 i;
    int r;
    SRes res;
    *out = NULL;
    if (!z)
        return ARC_E_MEMORY;
    SzArEx_Init(&z->db);
    z->block = z->bad = 0xFFFFFFFFul;
    if (fseek(f, 0, SEEK_SET) != 0) {             /* (the SDK takes the place the file is at for the archive's start) */
        free(z);
        return ARC_E_OPEN;
    }
    if ((r = look_begin(&l, f)) != ARC_OK) {
        free(z);
        return r;
    }
    res = SzArEx_Open(&z->db, &l.look.vt, &alloc, &alloc);
    free(l.look.buf);
    if (res != SZ_OK) {
        r = res == SZ_ERROR_UNSUPPORTED && header_locked(f) ? ARC_E_LOCKED : error_of(res);
        goto out;
    }
    for (i = 0; i < z->db.NumFiles && r == ARC_OK; i++) {
        size_t len = SzArEx_GetFileNameUtf16(&z->db, i, NULL), k;
        UInt64 size = SzArEx_GetFileSize(&z->db, i);
        UInt32 folder = z->db.FileToFolder[i];
        unsigned long when = 0;
        unsigned n = 0;
        int dir = SzArEx_IsDir(&z->db, i), how = ARC_OK;
        if (len > room) {
            UInt16 *w2 = realloc(wide, (len + 64) * sizeof *wide);
            char *n2 = w2 ? realloc(name, (len + 64) * 3 + 1) : NULL;
            if (w2)
                wide = w2;
            if (n2)
                name = n2;
            if (!w2 || !n2) {
                r = ARC_E_MEMORY;
                break;
            }
            room = len + 64;
        }
        SzArEx_GetFileNameUtf16(&z->db, i, wide);
        for (k = 0; k + 1 < len; k++) {                 /* (the last is its zero) */
            unsigned long c = wide[k];
            if (c >= 0xD800 && c < 0xDC00 && k + 2 < len && wide[k + 1] >= 0xDC00 && wide[k + 1] < 0xE000)
                c = 0x10000ul + ((c - 0xD800) << 10) + (wide[++k] - 0xDC00);
            n += utf8(c, name + n);
        }
        if (SzBitWithVals_Check(&z->db.MTime, i)) {     /* 100 ns since 1601 */
            UInt64 t = (UInt64)z->db.MTime.Vals[i].High << 32 | z->db.MTime.Vals[i].Low;
            t /= 10000000ul;
            if (t >= 11644473600ull && t - 11644473600ull <= 0xFFFFFFFFul)
                when = (unsigned long)(t - 11644473600ull);
        }
        if (!dir && size) {
            if (size > 0x7FFFFFFFul)
                how = ARC_E_BIG;
            else if (folder == 0xFFFFFFFFul)
                how = ARC_E_DAMAGED;
            else
                how = folder_how(&z->db.db, folder);
        }
        r = entry(ctx, i, name, n, dir, size > 0xFFFFFFFFul ? 0xFFFFFFFFul : (unsigned long)size, when, how);
    }
out:
    free(wide);
    free(name);
    if (r != ARC_OK) {
        sz_close(z);
        return r;
    }
    *out = z;
    return ARC_OK;
}

int sz_extract(struct sz *z, FILE *f, unsigned i, int (*data)(void *ctx, const void *p, unsigned n), void *ctx)
{
    struct look l;
    size_t offset = 0, size = 0, at;
    UInt32 folder;
    SRes res;
    int r, fresh;
    if (i >= z->db.NumFiles)
        return ARC_E_DAMAGED;
    folder = z->db.FileToFolder[i];
    if (folder == 0xFFFFFFFFul)
        return ARC_OK;                  /* a file of no bytes (the SDK would give the block kept away for it) */
    if (folder == z->bad)
        return z->bad_r;
    fresh = !(z->out && z->block == folder);
    if ((r = look_begin(&l, f)) != ARC_OK)
        return r;
    res = SzArEx_Extract(&z->db, &l.look.vt, i, &z->block, &z->out, &z->out_size, &offset, &size, &alloc, &alloc);
    free(l.look.buf);
    if (res != SZ_OK) {
        /* The block is good to keep only if it unpacked and this one file's sum alone is not its bytes'. (After
           any other error the SDK's "kept" block is rubbish.) */
        const CSzAr *p = &z->db.db;
        r = error_of(res);
        if (res != SZ_ERROR_CRC || !z->out
            || (fresh && SzBitWithVals_Check(&p->FolderCRCs, folder)
                && CrcCalc(z->out, z->out_size) != p->FolderCRCs.Vals[folder])) {
            sz_drop(z);
            if (res != SZ_ERROR_MEM)
                z->bad = folder, z->bad_r = r;
        }
        return r;
    }
    for (at = 0; at < size; ) {
        unsigned n = size - at > 4096 ? 4096 : (unsigned)(size - at);
        if (data && data(ctx, z->out + offset + at, n) != 0)
            return ARC_E_STOP;
        at += n;
    }
    return ARC_OK;
}

unsigned long sz_kept(const struct sz *z)
{
    return z->out ? (unsigned long)z->out_size : 0;
}

void sz_drop(struct sz *z)
{
    free(z->out);
    z->out = NULL;
    z->out_size = 0;
    z->block = 0xFFFFFFFFul;
}

void sz_close(struct sz *z)
{
    if (!z)
        return;
    sz_drop(z);
    SzArEx_Free(&z->db, &alloc);
    free(z);
}
