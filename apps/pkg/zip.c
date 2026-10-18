/* Zip files for PKG (M10 step E): the central directory read, entries stored
 * or deflated (stb_image's inflater, third_party/stb), each one's CRC-32
 * checked. Names are taken only as DOS can have them: relative, 8.3 parts,
 * no "..". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG                           /* (PNG's zlib is what's wanted: the inflater) */
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"    /* (helpers of the decoders left out) */
#include "stb_image.h"
#pragma GCC diagnostic pop

#include "pkg.h"

static unsigned le16(const unsigned char *p) { return p[0] | p[1] << 8; }
static unsigned long le32(const unsigned char *p) { return p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24; }

unsigned long pkg_crc32(unsigned long crc, const void *data, size_t n)
{
    static unsigned long table[256];
    const unsigned char *p = data;
    size_t i;
    if (!table[1]) {
        unsigned long k, c;
        int b;
        for (k = 0; k < 256; k++) {
            for (c = k, b = 0; b < 8; b++)
                c = c & 1 ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
            table[k] = c;
        }
    }
    crc = ~crc & 0xFFFFFFFFUL;
    for (i = 0; i < n; i++)
        crc = table[(crc ^ p[i]) & 255] ^ (crc >> 8);
    return ~crc & 0xFFFFFFFFUL;
}

/* A name as DOS takes it: upper case, '\' between its 8.3 parts. 0, or -1. */
static int dos_name(const char *in, size_t n, char *out, size_t room)
{
    size_t i, part = 0, ext = 0, o = 0;
    int dot = 0;
    if (!n || n >= room || in[0] == '/' || in[0] == '\\' || (n > 1 && in[1] == ':'))
        return -1;
    for (i = 0; i < n; i++) {
        char c = in[i];
        if (c == '/' || c == '\\') {
            if (!part || (dot && !ext))
                return -1;                      /* ("a//b", "x./") */
            out[o++] = '\\';
            part = ext = 0;
            dot = 0;
            continue;
        }
        if (c == '.') {
            if (dot || !part)
                return -1;                      /* ("..", ".x", "a.b.c") */
            dot = 1;
            out[o++] = c;
            continue;
        }
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 32);
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || strchr("_$~!#%&-{}()@'^", c)) || c == 0)
            return -1;
        if (dot ? ++ext > 3 : ++part > 8)
            return -1;
        out[o++] = c;
    }
    out[o] = 0;
    return part || o ? 0 : -1;
}

int zip_open(struct zip *z, const unsigned char *data, size_t size, char *why, size_t room)
{
    size_t at, i;
    unsigned long cd, cdsize;
    unsigned n;
    memset(z, 0, sizeof *z);
    if (size < 22) {
        snprintf(why, room, "not a zip");
        return -1;
    }
    for (at = size - 22;; at--) {               /* the end record, behind a comment of up to 64 KB */
        if (le32(data + at) == 0x06054B50UL)
            break;
        if (!at || size - at > 65536 + 22) {
            snprintf(why, room, "not a zip");
            return -1;
        }
    }
    n = le16(data + at + 10);
    cdsize = le32(data + at + 12);
    cd = le32(data + at + 16);
    if (cd + cdsize > at || n > ZIP_MAX) {
        snprintf(why, room, "a broken zip (or more than %d files)", ZIP_MAX);
        return -1;
    }
    z->data = data;
    z->size = size;
    for (i = 0, at = cd; i < n; i++) {
        struct zip_entry *e = &z->e[z->n];
        unsigned nl, xl, cl, flags;
        unsigned long lh;
        if (at + 46 > cd + cdsize || le32(data + at) != 0x02014B50UL) {
            snprintf(why, room, "a broken zip directory");
            return -1;
        }
        flags = le16(data + at + 8);
        e->method = le16(data + at + 10);
        e->crc = le32(data + at + 16);
        e->csize = le32(data + at + 20);
        e->usize = le32(data + at + 24);
        nl = le16(data + at + 28), xl = le16(data + at + 30), cl = le16(data + at + 32);
        lh = le32(data + at + 42);
        if (at + 46 + nl > cd + cdsize) {
            snprintf(why, room, "a broken zip directory");
            return -1;
        }
        e->dir = nl && (data[at + 46 + nl - 1] == '/' || data[at + 46 + nl - 1] == '\\');
        if (dos_name((const char *)data + at + 46, nl - (unsigned)e->dir, e->name, sizeof e->name) != 0) {
            snprintf(why, room, "a name DOS can't have: %.*s", (int)(nl > 60 ? 60 : nl), data + at + 46);
            return -1;
        }
        if (flags & 1) {
            snprintf(why, room, "%s is encrypted", e->name);
            return -1;
        }
        if (!e->dir && e->method != 0 && e->method != 8) {
            snprintf(why, room, "%s: compression method %u", e->name, e->method);
            return -1;
        }
        if (lh + 30 > size || le32(data + lh) != 0x04034B50UL) {
            snprintf(why, room, "%s: a broken local header", e->name);
            return -1;
        }
        e->offset = lh + 30 + le16(data + lh + 26) + le16(data + lh + 28);
        if (e->offset + e->csize > size) {
            snprintf(why, room, "%s: past the zip's end", e->name);
            return -1;
        }
        z->n++;
        at += 46 + nl + xl + cl;
    }
    return 0;
}

const struct zip_entry *zip_find(const struct zip *z, const char *name)
{
    int i;
    for (i = 0; i < z->n; i++)
        if (!z->e[i].dir && !strcmp(z->e[i].name, name))
            return &z->e[i];
    return 0;
}

unsigned char *zip_read(const struct zip *z, const struct zip_entry *e, char *why, size_t room)
{
    unsigned char *out = malloc(e->usize ? e->usize : 1);
    if (!out) {
        snprintf(why, room, "%s: out of memory", e->name);
        return 0;
    }
    if (e->method == 0) {
        if (e->csize != e->usize) {
            free(out);
            snprintf(why, room, "%s: a broken entry", e->name);
            return 0;
        }
        memcpy(out, z->data + e->offset, e->usize);
    } else if (stbi_zlib_decode_noheader_buffer((char *)out, (int)e->usize, (const char *)z->data + e->offset,
                                                (int)e->csize) != (int)e->usize) {
        free(out);
        snprintf(why, room, "%s doesn't inflate", e->name);
        return 0;
    }
    if (pkg_crc32(0, out, e->usize) != e->crc) {
        free(out);
        snprintf(why, room, "%s: a bad CRC", e->name);
        return 0;
    }
    return out;
}
