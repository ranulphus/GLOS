/* A FAT volume made new (disk.h): FAT12, FAT16 or FAT32 as Microsoft's FAT
 * specification lays them out, with the cluster sizes DOS chooses. No
 * division of 64 bits, no memory of its own but a sector on the stack. */
#include "disk.h"

static void wr16(unsigned char *p, unsigned v) { p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8); }

static void wr32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v, p[1] = (unsigned char)(v >> 8), p[2] = (unsigned char)(v >> 16), p[3] = (unsigned char)(v >> 24);
}

static void zero(unsigned char *s)
{
    unsigned i;
    for (i = 0; i < 512; i++)
        s[i] = 0;
}

struct plan {    /* (FAT16_MOST, disk.h: the largest volume DOS's FAT16 holds) */
    int fat;
    unsigned spc, reserved, root_entries, media;
    unsigned long fat_sectors, clusters, root_sectors;
};

/* The floppies, as DOS formats them. */
static int floppy(unsigned long sectors, struct plan *p, unsigned *heads, unsigned *spt)
{
    static const struct { unsigned sectors, spc, root, media, fatsz, spt; } f[] = {
        { 720, 2, 112, 0xFD, 2, 9 }, { 1440, 2, 112, 0xF9, 3, 9 }, { 2400, 1, 224, 0xF9, 7, 15 },
        { 2880, 1, 224, 0xF0, 9, 18 }, { 5760, 2, 240, 0xF0, 9, 36 } };
    unsigned i;
    for (i = 0; i < sizeof f / sizeof *f; i++)
        if (f[i].sectors == sectors) {
            p->fat = 12, p->spc = f[i].spc, p->reserved = 1, p->root_entries = f[i].root, p->media = f[i].media;
            p->fat_sectors = f[i].fatsz;
            p->root_sectors = (f[i].root * 32u + 511) / 512;
            p->clusters = (sectors - 1 - 2 * f[i].fatsz - p->root_sectors) / f[i].spc;
            *heads = 2, *spt = f[i].spt;
            return 0;
        }
    return -1;
}

/* A FAT's sectors for a volume with that cluster size: the least that hold an entry for every cluster. */
static unsigned long fat_size(int fat, unsigned long sectors, unsigned reserved, unsigned long root_sectors, unsigned spc,
                              unsigned long *clusters)
{
    unsigned long fatsz = 1, need, data, n;
    int turn;
    for (turn = 0; turn < 32; turn++) {         /* (it settles in a few turns: a larger FAT leaves fewer clusters) */
        if (sectors <= reserved + root_sectors + 2 * fatsz)
            return 0;
        data = sectors - reserved - root_sectors - 2 * fatsz;
        n = data / spc;
        need = fat == 12 ? ((n + 2) * 3 + 1) / 2 : fat == 16 ? (n + 2) * 2 : (n + 2) * 4;
        need = (need + 511) / 512;
        if (need <= fatsz) {
            *clusters = n;
            return fatsz;
        }
        fatsz = need;
    }
    return 0;
}

static int plan_disk(const struct fat_geo *g, struct plan *p)
{
    unsigned long s = g->sectors;
    int fat = g->fat;
    if (!fat)                                   /* DOS's own choice: FAT12 to 16 MB, FAT16 to 2 GB, FAT32 above */
        fat = s < 32680 ? 12 : s <= FAT16_MOST ? 16 : 32;
    p->fat = fat;
    p->media = 0xF8;
    if (fat == 32) {
        p->reserved = 32, p->root_entries = 0, p->root_sectors = 0;
        p->spc = s <= 532480UL ? 1 : s <= 16777216UL ? 8 : s <= 33554432UL ? 16 : s <= 67108864UL ? 32 : 64;
    } else {
        p->reserved = 1, p->root_entries = 512, p->root_sectors = 32;
        if (fat == 12)
            p->spc = s <= 4084 * 1UL + 100 ? 1 : s <= 8400 ? 2 : s <= 16500 ? 4 : 8;
        else
            p->spc = s <= 262144UL ? 4 : s <= 524288UL ? 8 : s <= 1048576UL ? 16 : s <= 2097152UL ? 32 : 64;
    }
    for (;;) {                                  /* a cluster count the kind of FAT can have, by larger clusters */
        p->fat_sectors = fat_size(fat, s, p->reserved, p->root_sectors, p->spc, &p->clusters);
        if (!p->fat_sectors)
            return -1;
        if ((fat == 12 && p->clusters < 4085) || (fat == 16 && p->clusters >= 4085 && p->clusters < 65525UL)
            || (fat == 32 && p->clusters >= 65525UL))
            return 0;
        if ((fat == 16 && p->clusters < 4085) || (fat == 32 && p->clusters < 65525UL)) {
            if (p->spc == 1)
                return -1;                      /* too small for that kind */
            p->spc /= 2;
        } else {
            if (p->spc == (fat == 32 ? 128u : 64u))
                return -1;                      /* too large for it (DOS has no clusters above 32 KB there) */
            p->spc *= 2;
        }
    }
}

/* What a PC shows if it is started from the volume: a line, then any key starts it again. */
static unsigned no_system(unsigned char *s, unsigned at)
{
    static const unsigned char code[] = { 0xBE, 0, 0, 0xAC, 0x08, 0xC0, 0x74, 0x09, 0xB4, 0x0E, 0xBB, 0x07, 0x00, 0xCD, 0x10,
                                          0xEB, 0xF2, 0x31, 0xC0, 0xCD, 0x16, 0xCD, 0x19 };
    static const char msg[] = "This disk has no system on it. Take it out, or put another in, and press a key.\r\n";
    unsigned i;
    for (i = 0; i < sizeof code; i++)
        s[at + i] = code[i];
    wr16(s + at + 1, 0x7C00 + at + (unsigned)sizeof code);
    for (i = 0; i < sizeof msg; i++)
        s[at + sizeof code + i] = (unsigned char)msg[i];
    return at;
}

static void label11(unsigned char *out, const char *label)
{
    unsigned i = 0;
    for (; label && label[i] && i < 11; i++) {
        char c = label[i];
        out[i] = (unsigned char)(c >= 'a' && c <= 'z' ? c - 32 : c);
    }
    for (; i < 11; i++)
        out[i] = ' ';
}

int fat_format(const struct fat_geo *g, fat_write write, void *ctx, struct fat_made *made)
{
    unsigned char s[512];
    struct plan p;
    unsigned heads = g->heads, spt = g->spt, i;
    unsigned long at, root_start, data_start, n;
    int is_floppy = g->drive < 0x80 && !g->hidden && !g->fat && floppy(g->sectors, &p, &heads, &spt) == 0;
    if (!is_floppy && (g->sectors < 64 || plan_disk(g, &p) != 0))
        return -1;
    root_start = p.reserved + 2 * p.fat_sectors;
    data_start = root_start + p.root_sectors;

    /* The boot sector. */
    zero(s);
    s[0] = 0xEB, s[1] = p.fat == 32 ? 0x58 : 0x3C, s[2] = 0x90;
    for (i = 0; i < 8; i++)
        s[3 + i] = (unsigned char)"GLOS 1.0"[i];
    wr16(s + 11, 512);
    s[13] = (unsigned char)p.spc;
    wr16(s + 14, p.reserved);
    s[16] = 2;
    wr16(s + 17, p.root_entries);
    wr16(s + 19, g->sectors < 65536UL ? (unsigned)g->sectors : 0);
    s[21] = (unsigned char)p.media;
    wr16(s + 22, p.fat == 32 ? 0 : (unsigned)p.fat_sectors);
    wr16(s + 24, spt);
    wr16(s + 26, heads);
    wr32(s + 28, g->hidden);
    wr32(s + 32, g->sectors < 65536UL ? 0 : g->sectors);
    if (p.fat == 32) {
        wr32(s + 36, p.fat_sectors);
        wr32(s + 44, 2);                        /* the root folder's first cluster */
        wr16(s + 48, 1);                        /* FSInfo */
        wr16(s + 50, 6);                        /* the boot sector's copy */
        s[64] = (unsigned char)g->drive, s[66] = 0x29;
        wr32(s + 67, g->serial);
        label11(s + 71, g->label ? g->label : "NO NAME");
        for (i = 0; i < 8; i++)
            s[82 + i] = (unsigned char)"FAT32   "[i];
        no_system(s, 90);
    } else {
        s[36] = (unsigned char)g->drive, s[38] = 0x29;
        wr32(s + 39, g->serial);
        label11(s + 43, g->label ? g->label : "NO NAME");
        for (i = 0; i < 8; i++)
            s[54 + i] = (unsigned char)(p.fat == 12 ? "FAT12   " : "FAT16   ")[i];
        no_system(s, 62);
    }
    s[510] = 0x55, s[511] = 0xAA;
    if (write(ctx, 0, s, 1) != 0)
        return -2;
    if (p.fat == 32) {
        if (write(ctx, 6, s, 1) != 0)
            return -2;
        zero(s);                                /* FSInfo: the free clusters, and where to look for one */
        wr32(s, 0x41615252UL);
        wr32(s + 484, 0x61417272UL);
        wr32(s + 488, p.clusters - 1);
        wr32(s + 492, 3);
        s[510] = 0x55, s[511] = 0xAA;
        if (write(ctx, 1, s, 1) != 0 || write(ctx, 7, s, 1) != 0)
            return -2;
        zero(s);
        for (at = 2; at < p.reserved; at++)     /* (the reserved sectors that hold nothing) */
            if (at != 6 && at != 7 && write(ctx, at, s, 1) != 0)
                return -2;
    }

    /* The FATs: every cluster free but the first two entries (and FAT32's root folder's cluster). */
    for (i = 0; i < 2; i++)
        for (n = 0; n < p.fat_sectors; n++) {
            zero(s);
            if (!n) {
                if (p.fat == 12) {
                    s[0] = (unsigned char)p.media, s[1] = 0xFF, s[2] = 0xFF;
                } else if (p.fat == 16) {
                    s[0] = (unsigned char)p.media, s[1] = 0xFF, s[2] = 0xFF, s[3] = 0xFF;
                } else {
                    wr32(s, 0x0FFFFF00UL | p.media);
                    wr32(s + 4, 0x0FFFFFFFUL);
                    wr32(s + 8, 0x0FFFFFFFUL);
                }
            }
            if (write(ctx, p.reserved + i * p.fat_sectors + n, s, 1) != 0)
                return -2;
        }

    /* The root folder, empty but for the volume's name. */
    n = p.fat == 32 ? p.spc : p.root_sectors;
    for (at = 0; at < n; at++) {
        zero(s);
        if (!at && g->label && g->label[0]) {
            label11(s, g->label);
            s[11] = 0x08;
        }
        if (write(ctx, (p.fat == 32 ? data_start : root_start) + at, s, 1) != 0)
            return -2;
    }
    if (made) {
        made->fat = p.fat, made->cluster_sectors = p.spc, made->clusters = p.clusters;
        made->fat_sectors = p.fat_sectors, made->data_start = data_start;
    }
    return 0;
}
