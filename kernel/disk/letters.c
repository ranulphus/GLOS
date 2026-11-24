/* Drive letters in MS-DOS's order (letters.h). */
#include "letters.h"
#include "disk.h"

static int is_extended(unsigned type) { return type == 0x05 || type == 0x0F; }

/* A first sector that is a FAT volume's own (a jump, 512-byte sectors, a power of two of them a cluster, one or
   two FATs, a media byte): a disk formatted whole. */
static int fat_first(const unsigned char *s)
{
    return s[510] == 0x55 && s[511] == 0xAA && (s[0] == 0xEB || s[0] == 0xE9) && s[11] == 0 && s[12] == 2 && s[13]
           && !(s[13] & (s[13] - 1)) && (s[16] == 1 || s[16] == 2) && s[21] >= 0xF0;
}

static int add(struct let_vol *vols, int n, int max, unsigned disk, unsigned kind, unsigned slot, unsigned type,
               unsigned active, unsigned long start, unsigned long sectors, unsigned long disk_sectors)
{
    struct let_vol *v;
    unsigned i;

    if (n >= max || !sectors || (disk_sectors && (start >= disk_sectors || sectors > disk_sectors - start)))
        return n;
    v = &vols[n];
    for (i = 0; i < sizeof *v; i++)
        ((unsigned char *)v)[i] = 0;
    v->disk = (unsigned char)disk;
    v->kind = (unsigned char)kind;
    v->slot = (unsigned char)slot;
    v->type = (unsigned char)type;
    v->active = (unsigned char)active;
    v->start = start;
    v->sectors = sectors;
    v->letter = -1;
    return n + 1;
}

int let_scan(unsigned disk, unsigned long sectors, let_read_fn read, void *ctx, struct let_vol *vols, int n, int max)
{
    unsigned char s[512];
    struct mbr_part p[4];
    unsigned long ext = 0, ebr, next;
    unsigned i, links;

    if (read(ctx, 0, s) != 0)
        return n;
    if (fat_first(s))                           /* (a volume's own first sector has no table to trust) */
        return add(vols, n, max, disk, LET_WHOLE, 0, 0, 0, 0, sectors ? sectors : 0xFFFFFFFFUL, 0);
    if (mbr_parse(s, p) < 0)
        return n;
    for (i = 0; i < 4; i++) {
        if (mbr_is_fat(p[i].type))
            n = add(vols, n, max, disk, LET_PRIMARY, i, p[i].type, p[i].active, p[i].start, p[i].sectors, sectors);
        else if (is_extended(p[i].type) && !ext && p[i].start)
            ext = p[i].start;                   /* (DOS follows the first one) */
    }
    /* The extended partition: a chain of tables, each with one logical drive (its start counted from that
       table) and the next table (counted from the extended partition's start). */
    for (ebr = ext, links = 0; ebr && links < 32; ebr = next, links++) {
        next = 0;
        if ((sectors && ebr >= sectors) || read(ctx, ebr, s) != 0 || mbr_parse(s, p) < 0)
            break;
        for (i = 0; i < 4; i++) {
            if (mbr_is_fat(p[i].type) && p[i].start)
                n = add(vols, n, max, disk, LET_LOGICAL, links, p[i].type, 0, ebr + p[i].start, p[i].sectors, sectors);
            else if (is_extended(p[i].type) && p[i].start && !next)
                next = ext + p[i].start;
        }
        if (next <= ebr)                        /* (a chain goes forward: a table that points back ends it) */
            break;
    }
    return n;
}

void let_assign(struct let_vol *vols, int n, int first)
{
    int letter = first, pass, i, j, own = -1;
    unsigned disk, disks = 0;

    for (i = 0; i < n; i++) {
        vols[i].letter = -1;
        if (vols[i].disk >= disks)
            disks = vols[i].disk + 1u;
        if (vols[i].own)
            own = i;
    }
    for (pass = 0; pass < 3; pass++)
        for (disk = 0; disk < disks; disk++) {
            int pick = -1, dos = -1;

            /* this disk's first primary: the active one, else the first; a DOS's own in the place of GLOS's */
            for (i = 0; i < n; i++) {
                if (vols[i].disk != disk || vols[i].kind == LET_LOGICAL)
                    continue;
                if (pick < 0 || (vols[i].active && !vols[pick].active))
                    pick = i;
                if (vols[i].dos && !vols[i].own && dos < 0)
                    dos = i;
            }
            if (pick >= 0 && vols[pick].own && vols[pick].active && dos >= 0)
                pick = dos;
            for (i = 0; i < n; i++) {
                if (vols[i].disk != disk || vols[i].letter >= 0 || letter > 25)
                    continue;
                if (pass == 0 ? i == pick : pass == 1 ? vols[i].kind == LET_LOGICAL : 1)
                    vols[i].letter = letter++;
            }
        }
    if (own >= 0 && vols[own].letter < 0) {     /* (more volumes than letters: GLOS's own has the last) */
        for (j = 0; j < n; j++)
            if (vols[j].letter == 25)
                vols[j].letter = -1;
        vols[own].letter = 25;
    }
}
