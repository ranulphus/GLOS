/* A folder's entries for API 2.0's FS (fslist.h). */
#include <glos/cp.h>
#include "fslist.h"

static u32 le16(const u8 *p) { return p[0] | (u32)p[1] << 8; }
static u32 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }

static void text(char *dst, const u8 *src, u32 room)   /* a NUL-ended text of at most room bytes */
{
    u32 i;
    for (i = 0; i < room - 1 && src[i]; i++)
        dst[i] = (char)src[i];
    dst[i] = 0;
}

void fs_from_dta(const u8 *d, struct fs_found *e)
{
    e->attr = d[0x15];
    e->mtime = le16(d + 0x18) << 16 | le16(d + 0x16);
    e->size_lo = le32(d + 0x1A);
    e->size_hi = 0;
    text(e->name, d + 0x1E, 13);
    e->short_name[0] = 0;
}

void fs_from_lfn(const u8 *d, struct fs_found *e)
{
    e->attr = le32(d);
    e->mtime = le16(d + 0x16) << 16 | le16(d + 0x14);   /* (the last write's, as a DOS date and time) */
    e->size_hi = le32(d + 0x1C);
    e->size_lo = le32(d + 0x20);
    text(e->name, d + 0x2C, FS_NAME);
    text(e->short_name, d + 0x130, sizeof e->short_name);
}

int fs_dots(const struct fs_found *e)
{
    return e->name[0] == '.' && (!e->name[1] || (e->name[1] == '.' && !e->name[2]));
}

/* A name's bytes as UTF-8 into out (room at least 1): its length. */
static u32 utf8(int page, const char *in, char *out, u32 room)
{
    int n = cp_to_utf8(page, in, out, (int)room);
    return n < 0 ? 0 : (u32)n;
}

u32 fs_record_size(const struct fs_found *e, int page)
{
    char name[FS_NAME * 3];
    u32 n = utf8(page, e->name, name, sizeof name);
    return (u32)(__builtin_offsetof(struct fs_record, name) + n + 1 + 3) & ~3u;
}

int fs_pack(u8 *buf, u32 room, u32 *used, const struct fs_found *e, int page)
{
    char name[FS_NAME * 3];
    u32 n = utf8(page, e->name, name, sizeof name), size, i;
    struct fs_record *r;
    size = (u32)(__builtin_offsetof(struct fs_record, name) + n + 1 + 3) & ~3u;
    if (*used > room || size > room - *used)
        return 0;
    r = (struct fs_record *)(buf + *used);
    for (i = 0; i < size; i++)
        ((u8 *)r)[i] = 0;
    r->size = size;
    r->attr = e->attr;
    r->bytes_lo = e->size_lo;
    r->bytes_hi = e->size_hi;
    r->mtime = e->mtime;
    for (i = 0; i < sizeof r->short_name - 1 && e->short_name[i]; i++)
        r->short_name[i] = e->short_name[i];
    for (i = 0; i < n; i++)
        r->name[i] = name[i];
    *used += size;
    return 1;
}
