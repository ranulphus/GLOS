/* The partition table (disk.h). */
#include "disk.h"

extern const unsigned char mbr_code[], mbr_code_end[], mbr_code_part[];

static unsigned long rd32(const unsigned char *p)
{
    return (unsigned long)p[0] | (unsigned long)p[1] << 8 | (unsigned long)p[2] << 16 | (unsigned long)p[3] << 24;
}

static void wr32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

int mbr_parse(const unsigned char *s, struct mbr_part part[4])
{
    int i, used = 0;
    if (s[510] != 0x55 || s[511] != 0xAA)
        return -1;
    for (i = 0; i < 4; i++) {
        const unsigned char *e = s + 446 + i * 16;
        part[i].active = e[0] == 0x80;
        part[i].type = e[4];
        part[i].start = rd32(e + 8);
        part[i].sectors = rd32(e + 12);
        used += e[4] != 0;
    }
    return used;
}

int mbr_is_fat(unsigned t) { return t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E; }

/* A sector's cylinder, head and sector as an entry has them: the last the BIOS's calls reach (1023, the last head
   and sector) for one past them. */
static void chs(unsigned char *e, unsigned long lba, unsigned heads, unsigned spt)
{
    unsigned long c = lba / ((unsigned long)heads * spt);
    unsigned h = (unsigned)(lba / spt % heads), s = (unsigned)(lba % spt) + 1;
    if (c > 1023)
        c = 1023, h = heads - 1, s = spt;
    e[0] = (unsigned char)h;
    e[1] = (unsigned char)(s | ((c >> 2) & 0xC0));
    e[2] = (unsigned char)c;
}

int mbr_one(unsigned char *s, unsigned long total, unsigned heads, unsigned spt, struct mbr_part *made)
{
    unsigned char *e = s + 446;
    unsigned long start = spt, n, cyl;
    unsigned type;
    int i, far;
    if (!heads || !spt || heads > 255 || spt > 63 || total < start + 720)
        return -1;
    cyl = (unsigned long)heads * spt;
    n = total - start;
    if (total / cyl > 1 && total / cyl <= 1024)         /* whole cylinders, where the BIOS counts in them */
        n = total / cyl * cyl - start;
    far = (start + n - 1) / cyl > 1023;
    if (n < 32680)                              /* (16 MB: DOS's FAT12) */
        type = 0x01;
    else if (n < 65536)
        type = 0x04;
    else if (n <= FAT16_MOST)                   /* (2 GB: the most FAT16 has) */
        type = far ? 0x0E : 0x06;
    else
        type = far ? 0x0C : 0x0B;
    for (i = 0; i < 64; i++)
        e[i] = 0;
    e[0] = 0x80;
    chs(e + 1, start, heads, spt);
    e[4] = (unsigned char)type;
    chs(e + 5, start + n - 1, heads, spt);
    wr32(e + 8, start);
    wr32(e + 12, n);
    s[510] = 0x55, s[511] = 0xAA;
    if (made) {
        made->active = 1, made->type = (unsigned char)type;
        made->start = start, made->sectors = n;
    }
    return 0;
}

void mbr_boot(unsigned char *s)
{
    unsigned n = (unsigned)(mbr_code_end - mbr_code), i;
    for (i = 0; i < 440; i++)
        s[i] = i < n ? mbr_code[i] : 0;
    s[510] = 0x55, s[511] = 0xAA;
}

void mbr_boot_part(unsigned char *s, unsigned part)
{
    mbr_boot(s);
    s[mbr_code_part - mbr_code] = (unsigned char)(part >= 1 && part <= 4 ? part : 0);
}

unsigned mbr_boot_part_of(const unsigned char *s)
{
    unsigned at = (unsigned)(mbr_code_part - mbr_code), n = (unsigned)(mbr_code_end - mbr_code), i;
    for (i = 0; i < n; i++)
        if (i != at && i != at - 1 && s[i] != mbr_code[i])     /* (but the two bytes it keeps: the disk, the number) */
            return 0;
    return s[at] >= 1 && s[at] <= 4 ? s[at] : 0;
}

int mbr_add(unsigned char *s, unsigned long total, unsigned heads, unsigned spt, struct mbr_part *made, unsigned *slot)
{
    struct mbr_part part[4];
    unsigned long cyl, at, best = 0, best_n = 0, n;
    unsigned char *e;
    unsigned type;
    int i, k, free_slot = -1, active = 0, far;
    if (!heads || !spt || heads > 255 || spt > 63 || mbr_parse(s, part) < 0)
        return -1;
    cyl = (unsigned long)heads * spt;
    for (i = 0; i < 4; i++) {
        if (!part[i].type && free_slot < 0)
            free_slot = i;
        active |= part[i].type && part[i].active;
    }
    if (free_slot < 0)
        return -2;
    /* Each free stretch starts at the first track's end or at a partition's, and runs to the next partition's
       start above it, or the disk's end. */
    for (k = -1; k < 4; k++) {
        unsigned long end = total;
        if (k >= 0 && !part[k].type)
            continue;
        at = k < 0 ? spt : part[k].start + part[k].sectors;
        if (k >= 0 && at < part[k].start)       /* (an entry past 2^32 sectors: nothing after it) */
            continue;
        for (i = 0; i < 4; i++)
            if (part[i].type && part[i].start + part[i].sectors > at && part[i].start < end)
                end = part[i].start > at ? part[i].start : at;         /* (one that covers `at`: no stretch here) */
        if (at > spt)
            at = (at + cyl - 1) / cyl * cyl;    /* whole cylinders from here */
        end = end / cyl * cyl;
        if (end > at && end - at > best_n)
            best = at, best_n = end - at;
    }
    if (best_n < 65536UL)
        return -3;
    n = best_n;
    far = (best + n - 1) / cyl > 1023;
    type = n <= FAT16_MOST ? (far ? 0x0E : 0x06) : (far ? 0x0C : 0x0B);
    e = s + 446 + free_slot * 16;
    for (i = 0; i < 16; i++)
        e[i] = 0;
    e[0] = active ? 0 : 0x80;
    chs(e + 1, best, heads, spt);
    e[4] = (unsigned char)type;
    chs(e + 5, best + n - 1, heads, spt);
    wr32(e + 8, best);
    wr32(e + 12, n);
    if (made) {
        made->active = !active, made->type = (unsigned char)type;
        made->start = best, made->sectors = n;
    }
    if (slot)
        *slot = (unsigned)free_slot;
    return 0;
}
