/* The shell's DISK command (docs/v1/shell.md, step S3): the hard disks as the BIOS has them, their
 * partitions; a disk laid out anew, a partition formatted, boot records written. What FreeDOS's FDISK,
 * FORMAT and SYS did for the setup floppy (docs/floppy.md), with kernel/disk's code (disk.h) over the BIOS's
 * sectors (bdisk.c).
 *
 *   DISK                             the hard disks and their partitions
 *   DISK n                           disk n's alone (1: the BIOS's first, 80h)
 *   DISK n NEW /Y                    a new table: one partition over the whole disk, the one the PC starts
 *                                    from, of the FAT kind its size asks for; GLOS's boot record
 *   DISK n FORMAT [p] [/V:label] /Y  partition p (1 unless said; one of the table's four) made an empty FAT
 *                                    volume
 *   DISK n BOOT [p] /Y               the disk's boot record and partition p's written: the PC starts FreeDOS's
 *                                    KERNEL.SYS from p (the files themselves are COPY's to put there)
 *
 * The rules of whatever writes a disk (the owner, 2026-11-12):
 *   - It says what would be lost, and does nothing without /Y: the command as typed is the question, the same
 *     command with /Y the plain yes (over SSH as at a terminal).
 *   - Never the disk GLOS runs from, nor the one DOS started from: a volume is known by its serial number,
 *     which DOS is asked for (INT 21h 440Dh, 66h) for the drives of GLOS.CFG, COMSPEC and DOS's start, and which
 *     each FAT partition's first sector says. Where DOS can't say (and the drive is no floppy), no hard disk
 *     is written at all.
 *   - DOS read the disk's table and the volume's shape when it started: after a write the PC is to be
 *     restarted before DOS uses that disk, and the command says so.
 *
 * The work is a thread's (the BIOS's calls wait for the stub's idle turn); what it says is read by the ssh
 * thread, a stream of the command's channel (sshd.c). */
#include <string.h>

#include "glos/bootinfo.h"
#include "bdisk.h"
#include "clock.h"
#include "diskcmd.h"
#include "dos.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "timer.h"
#include "vm.h"
#include "disk.h"

#define OUT_MAX 6144
#define VOLS    20                              /* a disk's partitions shown: the table's four, and logical drives */

struct diskcmd {
    char args[200];
    char out[OUT_MAX];
    volatile u32 head;                          /* (the worker's) */
    u32 tail;                                   /* (the reader's) */
    volatile u8 done, gone;
    int code;
    void (*kick)(void);
    u32 own[6];                                 /* the serial numbers of the volumes GLOS and DOS run from */
    u32 nown;
    u8 own_unknown;                             /* ... and one of them on a hard disk has none DOS could say */
};

struct vol {
    u8 n;                                       /* 1-4 the table's; 5 on: logical drives, in their order */
    u8 type, active;
    u8 fat;                                     /* 12, 16, 32 by its first sector; 0: not a FAT volume (yet) */
    u8 has_serial;
    u32 start, sectors, serial;
    char label[12];
};

struct layout {
    struct bdisk_geo g;
    int table;                                  /* mbr_parse's count; -1: no table; < -1: the sector wasn't read */
    int bios;                                   /* ... the BIOS's error then */
    u8 first[512];                              /* the disk's first sector */
    struct vol v[VOLS];
    u32 nv;
};

static void say(struct diskcmd *c, const char *fmt, ...)
{
    __builtin_va_list ap;
    u32 room = OUT_MAX - c->head;
    int n;
    if (room < 2)
        return;
    __builtin_va_start(ap, fmt);
    n = kvsnprintf(c->out + c->head, room, fmt, ap);
    __builtin_va_end(ap);
    if (n > 0)
        c->head += (u32)n < room ? (u32)n : room - 1;
    {
        void (*kick)(void) = c->kick;           /* (diskcmd_free() may take it away meanwhile) */
        if (kick)
            kick();
    }
}

/* (kprintf's formats have no capitals for hexadecimal and nothing set to the left) */
static const char *hex2(u32 v, char *b)
{
    b[0] = "0123456789ABCDEF"[v >> 4 & 15], b[1] = "0123456789ABCDEF"[v & 15], b[2] = 0;
    return b;
}

static void pad(char *s, u32 width)
{
    u32 n = (u32)strlen(s);
    while (n < width)
        s[n++] = ' ';
    s[n] = 0;
}

static u32 rd16(const u8 *p) { return p[0] | (u32)p[1] << 8; }
static u32 rd32(const u8 *p) { return rd16(p) | rd16(p + 2) << 16; }

static const char *type_name(u8 t)
{
    switch (t) {
    case 0x01: return "FAT12";
    case 0x04: case 0x06: case 0x0E: return "FAT16";
    case 0x0B: case 0x0C: return "FAT32";
    case 0x05: case 0x0F: case 0x85: return "extended";
    case 0x07: return "NTFS or HPFS";
    case 0x82: return "Linux swap";
    case 0x83: return "Linux";
    case 0x8E: return "Linux LVM";
    case 0xA5: case 0xA6: case 0xA9: return "BSD";
    case 0xEE: return "GPT";
    case 0xEF: return "EFI system";
    }
    return "another system's";
}

static int is_extended(u8 t) { return t == 0x05 || t == 0x0F || t == 0x85; }

static int type_fat(u8 t) { return t == 0x01 ? 12 : t == 0x0B || t == 0x0C ? 32 : mbr_is_fat(t) ? 16 : 0; }

/* A volume's first sector: the FAT kind its figures give, as Microsoft's specification counts it (by the
   number of clusters); 0 if it is no FAT volume's. */
static int fat_kind(const u8 *s)
{
    u32 spc = s[13], reserved = rd16(s + 14), fats = s[16], root = (rd16(s + 17) * 32 + 511) / 512;
    u32 fatsz = rd16(s + 22) ? rd16(s + 22) : rd32(s + 36), total = rd16(s + 19) ? rd16(s + 19) : rd32(s + 32);
    u32 meta, clusters;
    if (s[510] != 0x55 || s[511] != 0xAA || rd16(s + 11) != 512 || !spc || (spc & (spc - 1)) || !reserved || !fats
        || fats > 2 || !fatsz || !total)
        return 0;
    meta = reserved + fats * fatsz + root;
    if (fatsz > total || meta >= total)
        return 0;
    clusters = (total - meta) / spc;
    return clusters < 4085 ? 12 : clusters < 65525 ? 16 : 32;
}

static void look(struct vol *v, const u8 *s)
{
    u32 at, i;
    v->fat = (u8)fat_kind(s);
    v->has_serial = 0;
    v->label[0] = 0;
    if (!v->fat)
        return;
    at = rd16(s + 22) ? 38 : 66;                /* (the extended boot record: FAT12/16's, or FAT32's) */
    if (s[at] != 0x29)
        return;
    v->serial = rd32(s + at + 1);
    v->has_serial = 1;
    for (i = 0; i < 11; i++)
        v->label[i] = (char)(s[at + 5 + i] >= 32 && s[at + 5 + i] < 127 ? s[at + 5 + i] : ' ');
    for (i = 11; i && v->label[i - 1] == ' '; i--)
        ;
    v->label[i] = 0;
    if (!strcmp(v->label, "NO NAME"))
        v->label[0] = 0;
}

/* The disk's layout read. 0; BDISK_NONE: no such disk; other errors of the BIOS's are in the layout (the disk is
   there, its first sector isn't read). */
static int scan(u8 d, struct layout *L)
{
    struct mbr_part part[4];
    u8 s[512];
    u32 i, ext = 0, ext_n = 0, at, turns;
    int e;
    memset(L, 0, sizeof *L);
    if ((e = bdisk_geo(d, &L->g)) != 0)
        return e;
    if ((e = bdisk_read(d, 0, 1, L->first)) != 0) {
        L->table = -2, L->bios = e;
        return 0;
    }
    L->table = mbr_parse(L->first, part);
    if (L->table <= 0)
        return 0;
    for (i = 0; i < 4; i++) {
        struct vol *v;
        if (!part[i].type)
            continue;
        v = &L->v[L->nv++];
        v->n = (u8)(i + 1), v->type = part[i].type, v->active = part[i].active;
        v->start = (u32)part[i].start, v->sectors = (u32)part[i].sectors;
        if (is_extended(v->type) && !ext)
            ext = v->start, ext_n = v->sectors;
        else if (mbr_is_fat(v->type) && v->start && v->start < L->g.sectors && bdisk_read(d, v->start, 1, s) == 0)
            look(v, s);
    }
    for (at = ext, turns = 0; ext && turns < VOLS - 4 && L->nv < VOLS; turns++) {      /* the logical drives' chain */
        struct mbr_part lp[4];
        struct vol *v;
        if (at >= L->g.sectors || bdisk_read(d, at, 1, s) != 0 || mbr_parse(s, lp) < 0)
            break;
        if (lp[0].type && !is_extended(lp[0].type)) {
            v = &L->v[L->nv++];
            v->n = (u8)(5 + turns), v->type = lp[0].type, v->active = 0;
            v->start = at + (u32)lp[0].start, v->sectors = (u32)lp[0].sectors;
            if (mbr_is_fat(v->type) && v->start < L->g.sectors && bdisk_read(d, v->start, 1, s) == 0)
                look(v, s);                     /* (lp has the chain's sector already) */
        }
        if (!is_extended(lp[1].type) || !lp[1].start || (u32)lp[1].start >= ext_n)
            break;
        at = ext + (u32)lp[1].start;
    }
    return 0;
}

/* ---- the volumes GLOS and DOS run from */

static int dos_serial(u8 letter, u32 *serial)
{
    static const u16 kinds[] = { 0x0866, 0x4866 };      /* (a FAT32 volume's is asked for by its own category) */
    struct dos_req q;
    u8 id[32];
    u32 k;
    for (k = 0; k < 2; k++) {
        memset(&q, 0, sizeof q);
        memset(id, 0, sizeof id);
        q.n = 1;
        q.r[0].ax = 0x440D, q.r[0].bx = (u16)(letter - 'A' + 1), q.r[0].cx = kinds[k], q.r[0].dx = 0;
        q.r[0].ds = q.r[0].es = DOS_XFER;
        q.out = id, q.out_len = sizeof id, q.out_at = 0;
        if (dos_call(&q) != 0)
            return -1;
        if (!q.error && !(q.r[0].flags & 1)) {
            *serial = rd32(id + 2);
            return 0;
        }
    }
    return -1;
}

static void own_volumes(struct diskcmd *c)
{
    char letters[3];
    struct dos_req q;
    u32 i, k, serial;
    letters[0] = vm.bi->cfg_path[0], letters[1] = vm.bi->comspec[0], letters[2] = 0;
    dos_hold();
    memset(&q, 0, sizeof q);
    q.n = 1, q.r[0].ax = 0x3305;                /* the drive DOS started from, in DL */
    if (dos_call(&q) == 0 && !q.error && (q.r[0].dx & 0xFF) >= 1 && (q.r[0].dx & 0xFF) <= 26)
        letters[2] = (char)('A' + (q.r[0].dx & 0xFF) - 1);
    for (i = 0; i < 3; i++) {
        char l = letters[i];
        if (l >= 'a' && l <= 'z')
            l = (char)(l - 32);
        if (!l || l == 'A' || l == 'B')
            continue;                           /* (not said; or a floppy: no hard disk's) */
        for (k = 0; k < i && letters[k] != letters[i]; k++)
            ;
        if (k < i)
            continue;
        if (l < 'C' || l > 'Z' || dos_serial((u8)l, &serial) != 0)
            c->own_unknown = 1;                 /* (unknown where from, or DOS doesn't say which volume) */
        else if (c->nown < ARRAY_SIZE(c->own))
            c->own[c->nown++] = serial;
    }
    dos_release();
    for (i = 0; i < c->nown && !c->own_unknown; i++) {  /* each is to be found on a disk the BIOS has: one that
                                                   isn't (a volume read otherwise than here, a disk of a driver's)
                                                   could be written over unknown */
        static struct layout L;
        u32 n, k2, found = 0;
        for (n = 1; n <= 4 && !found && scan((u8)(0x7F + n), &L) == 0; n++)
            for (k2 = 0; k2 < L.nv; k2++)
                if (L.v[k2].has_serial && L.v[k2].serial == c->own[i])
                    found = 1;
        if (!found)
            c->own_unknown = 1;
    }
}

static int own_vol(const struct diskcmd *c, const struct vol *v)
{
    u32 i;
    for (i = 0; v->has_serial && i < c->nown; i++)
        if (c->own[i] == v->serial)
            return 1;
    return 0;
}

static int own_disk(const struct diskcmd *c, const struct layout *L)
{
    u32 i;
    for (i = 0; i < L->nv; i++)
        if (own_vol(c, &L->v[i]))
            return 1;
    return 0;
}

/* ---- DISK, DISK n */

static u32 mb(u32 sectors) { return sectors / 2048; }

static void show(struct diskcmd *c, u32 n, const struct layout *L)
{
    char type[40], what[80], kind[12], h[3];
    u32 i;
    say(c, "Disk %u: %u MB, %u cylinders, %u heads, %u sectors%s\n", n, mb(L->g.sectors), L->g.cyls, L->g.heads, L->g.spt,
        L->g.edd ? " (LBA)" : "");
    if (L->table < -1)
        say(c, "  its first sector can't be read (the BIOS's error %sh)\n", hex2(L->bios < 0 ? 0xFF : (u32)L->bios, h));
    else if (L->table < 0)
        say(c, "  no partition table\n");
    else if (!L->nv)
        say(c, "  no partitions\n");
    for (i = 0; i < L->nv; i++) {
        const struct vol *v = &L->v[i];
        ksnprintf(type, 24, "%s [%s]", type_name(v->type), hex2(v->type, h));
        pad(type, 18);
        strcpy(kind, v->active ? "active" : v->n > 4 ? "logical" : "");
        pad(kind, 7);
        what[0] = 0;
        if (mbr_is_fat(v->type))
            ksnprintf(what, sizeof what, "%s%s%s", !v->fat ? "not formatted" : v->label[0] ? "volume " : "no label",
                      v->fat ? v->label : "", own_vol(c, v) ? ", GLOS or DOS runs from it" : "");
        say(c, "  %u  %s %6u MB  %s %s\n", v->n, type, mb(v->sectors), kind, what);
    }
}

static int list(struct diskcmd *c, u32 only)
{
    static struct layout L;                     /* (one DISK at a time: start()) */
    u32 n, found = 0;
    int e;
    for (n = only ? only : 1; n <= (only ? only : 4); n++) {
        if ((e = scan((u8)(0x7F + n), &L)) != 0) {
            if (e == BDISK_BUSY || e == BDISK_MEMORY) {
                say(c, e == BDISK_BUSY ? "DISK: a program has the PC: the disks are read when it has ended\n"
                    : "DISK: DOS has no memory for the sectors' buffer\n");
                return 1;
            }
            break;
        }
        show(c, n, &L);
        found++;
    }
    if (!found)
        say(c, only ? "DISK: the BIOS has no hard disk %u\n" : "The BIOS has no hard disk.\n", only);
    return found ? 0 : 1;
}

/* ---- the writes */

static struct layout W;                         /* (one DISK at a time) */

/* The disk for a write: 0, or its exit code with the reason said. */
static int target(struct diskcmd *c, u32 n)
{
    int e = scan((u8)(0x7F + n), &W);
    if (e == BDISK_BUSY)
        say(c, "DISK: a program has the PC: try again when it has ended\n");
    else if (e == BDISK_MEMORY)
        say(c, "DISK: DOS has no memory for the sectors' buffer\n");
    else if (e)
        say(c, "DISK: the BIOS has no hard disk %u\n", n);
    else if (W.table < -1)
        say(c, "DISK: disk %u's first sector can't be read (the BIOS's error %02xh)\n", n, W.bios < 0 ? 0xFF : W.bios);
    else if (c->own_unknown)
        say(c, "DISK: DOS doesn't say which disk it and GLOS run from, so no hard disk is written from here.\n"
            "Started from GLOS's setup floppy, the hard disks are free to write.\n");
    else
        return 0;
    return 1;
}

static const char restart[] = "Restart the PC before DOS uses the disk (REBOOT): DOS knows it as it was.\n";

static int bios_failed(struct diskcmd *c, int e, const char *what)
{
    if (e == BDISK_BUSY)
        say(c, "DISK: %s stopped: a program has the PC\n", what);
    else
        say(c, "DISK: %s stopped: the BIOS's error %02xh\n", what, e < 0 ? 0xFF : e);
    return 1;
}

static int do_new(struct diskcmd *c, u32 n, int yes)
{
    struct mbr_part made;
    u8 s[512], zero[512], back[512];
    u8 d = (u8)(0x7F + n);
    u32 i, junk = 0;
    int e;
    if ((e = target(c, n)) != 0)
        return e;
    if (own_disk(c, &W)) {
        say(c, "DISK: disk %u is the one GLOS or DOS runs from: it is not written.\n", n);
        return 1;
    }
    memset(s, 0, sizeof s);
    if (W.table >= 0)
        memcpy(s, W.first, 512);
    for (i = 0; i < 512; i++)
        junk |= W.first[i];
    if (mbr_one(s, W.g.sectors, W.g.heads, W.g.spt, &made) != 0) {
        say(c, "DISK: disk %u is too small for a partition, or the BIOS's figures for it make no sense\n", n);
        return 1;
    }
    mbr_boot(s);
    say(c, "Disk %u (%u MB): one %s partition of %u MB over all of it, which the PC starts from.\n", n, mb(W.g.sectors),
        type_name(made.type), mb((u32)made.sectors));
    if (W.nv)
        say(c, "Its %u partition%s and EVERYTHING ON THE DISK %s LOST.\n", W.nv, W.nv == 1 ? "" : "s", yes ? "ARE" : "WOULD BE");
    else if (W.table < 0 && junk)
        say(c, "It has no partition table, but its first sector isn't empty: whatever is on the disk %s lost.\n",
            yes ? "is" : "would be");
    if (!yes) {
        say(c, "Nothing was done. To do it: DISK %u NEW /Y\n", n);
        return 2;
    }
    memset(zero, 0, sizeof zero);               /* (an old volume's first sector there would be taken for the new one's) */
    if ((e = bdisk_write(d, (u32)made.start, 1, zero)) != 0 || (e = bdisk_write(d, 0, 1, s)) != 0
        || (e = bdisk_read(d, 0, 1, back)) != 0)
        return bios_failed(c, e, "the write");
    if (memcmp(s, back, 512)) {
        say(c, "DISK: the disk's first sector reads back differently: the table was not written\n");
        return 1;
    }
    kprintf("GLOS-DISK new disk=%02x type=%02x start=%u sectors=%u\n", d, made.type, (u32)made.start, (u32)made.sectors);
    say(c, "Done. Next: DISK %u FORMAT /Y\n%s", n, restart);
    return 0;
}

static struct vol *part_of(struct diskcmd *c, u32 n, u32 p)
{
    u32 i;
    for (i = 0; i < W.nv; i++)
        if (W.v[i].n == p)
            break;
    if (p < 1 || p > 4)
        say(c, "DISK: partition %u: one of the table's four (1 to 4)\n", p);
    else if (i == W.nv)
        say(c, W.nv ? "DISK: disk %u has no partition %u\n" : "DISK: disk %u has no partitions (DISK %u NEW makes one)\n",
            n, W.nv ? p : n);
    else if (!mbr_is_fat(W.v[i].type))
        say(c, "DISK: partition %u of disk %u is %s [%02x], not one of DOS's\n", p, n, type_name(W.v[i].type), W.v[i].type);
    else if (!W.v[i].start || W.v[i].start >= W.g.sectors || W.v[i].sectors > W.g.sectors - W.v[i].start)
        say(c, "DISK: partition %u of disk %u doesn't lie within the disk\n", p, n);
    else if (own_vol(c, &W.v[i]))
        say(c, "DISK: partition %u of disk %u is the volume GLOS or DOS runs from: it is not written.\n", p, n);
    else
        return &W.v[i];
    return 0;
}

struct writer {
    u8 drive;
    u32 base;                                   /* the volume's start */
    u8 *buf;
    u32 at, n, run, sectors;
    int e;
};

static int flush(struct writer *w)
{
    if (w->n && !w->e) {
        w->e = bdisk_write(w->drive, w->base + w->at, w->n, w->buf);
        w->sectors += w->n;
    }
    w->n = 0;
    return w->e;
}

static int put(void *ctx, unsigned long lba, const unsigned char *data, unsigned count)
{
    struct writer *w = ctx;
    for (; count && !w->e; count--, lba++, data += 512) {
        if (w->n && (lba != w->at + w->n || w->n == w->run))
            flush(w);
        if (!w->n)
            w->at = (u32)lba;
        memcpy(w->buf + w->n * 512, data, 512);
        w->n++;
    }
    return w->e;
}

static int do_format(struct diskcmd *c, u32 n, u32 p, const char *label, int yes)
{
    struct fat_geo g;
    struct fat_made made;
    struct writer w;
    struct vol *v;
    char lab[12];
    u32 i;
    int e;
    if ((e = target(c, n)) != 0)
        return e;
    if (!(v = part_of(c, n, p)))
        return 1;
    for (i = 0; label && label[i] && i < 11; i++)
        lab[i] = label[i];
    lab[i] = 0;
    if (label && (label[i] || strpbrk(lab, "*?/\\|.,;:+=<>[]\""))) {
        say(c, "DISK: a label is 11 characters at most, with none of * ? / \\ | . , ; : + = < > [ ] \"\n");
        return 1;
    }
    say(c, "Partition %u of disk %u (%s, %u MB%s%s): made an empty volume%s%s.\n", p, n, type_name(v->type), mb(v->sectors),
        v->fat ? v->label[0] ? ", volume " : ", no label" : ", not formatted", v->fat ? v->label : "",
        lab[0] ? " named " : "", lab);
    if (v->fat)
        say(c, "EVERYTHING IN IT %s LOST.\n", yes ? "IS" : "WOULD BE");
    if (!yes) {
        say(c, "Nothing was done. To do it: the same command with /Y\n");
        return 2;
    }
    memset(&w, 0, sizeof w);
    w.drive = (u8)(0x7F + n), w.base = v->start, w.run = bdisk_run();
    if (w.run > 64)
        w.run = 64;
    if (!(w.buf = kmalloc(64 * 512))) {
        say(c, "DISK: no memory\n");
        return 1;
    }
    memset(&g, 0, sizeof g);
    g.sectors = v->sectors, g.hidden = v->start, g.heads = W.g.heads, g.spt = W.g.spt, g.drive = 0x80;
    g.serial = clock_utc() ^ timer_ticks() << 12 ^ v->start;
    g.label = lab[0] ? lab : 0;
    g.fat = type_fat(v->type);
    e = fat_format(&g, put, &w, &made);
    if (!e)
        flush(&w);
    kfree(w.buf);
    if (e == -1) {
        say(c, "DISK: a partition of this size and type [%02x] holds no %s volume (DISK %u NEW lays the disk out anew)\n",
            v->type, type_name(v->type), n);
        return 1;
    }
    if (w.e)
        return bios_failed(c, w.e, "the format");
    kprintf("GLOS-DISK format disk=%02x part=%u fat=%d clusters=%u cluster=%u wrote=%u\n", w.drive, p, made.fat,
            (u32)made.clusters, made.cluster_sectors * 512, w.sectors);
    say(c, "Done: FAT%d, %u clusters of %u bytes, %u MB for files.\n%s", made.fat, (u32)made.clusters,
        made.cluster_sectors * 512, (u32)(made.clusters / 2048 * made.cluster_sectors
                                          + made.clusters % 2048 * made.cluster_sectors / 2048), restart);
    return 0;
}

static int do_boot(struct diskcmd *c, u32 n, u32 p, int yes)
{
    struct vol *v;
    u8 s[512], m[512];
    u8 d = (u8)(0x7F + n);
    u32 i, copy;
    int e;
    if ((e = target(c, n)) != 0)
        return e;
    if (own_disk(c, &W)) {
        say(c, "DISK: disk %u is the one GLOS or DOS runs from: its boot records are not written.\n", n);
        return 1;
    }
    if (!(v = part_of(c, n, p)))
        return 1;
    if (!v->fat) {
        say(c, "DISK: partition %u of disk %u isn't formatted (DISK %u FORMAT %u)\n", p, n, n, p);
        return 1;
    }
    say(c, "Disk %u: its boot record and partition %u's written anew. The PC then starts from partition %u, which\n"
        "starts FreeDOS (KERNEL.SYS in its first folder). No file is touched; another system's start-up code or a\n"
        "boot manager there %s lost.\n", n, p, p, yes ? "is" : "would be");
    if (!yes) {
        say(c, "Nothing was done. To do it: the same command with /Y\n");
        return 2;
    }
    if ((e = bdisk_read(d, v->start, 1, s)) != 0)
        return bios_failed(c, e, "the read");
    copy = v->fat == 32 ? rd16(s + 50) : 0;     /* (FAT32's copy of its first sector: the sixth, as a rule) */
    if (fat_boot(s, v->fat) != 0) {
        say(c, "DISK: no boot code for this volume\n");
        return 1;
    }
    memcpy(m, W.first, 512);
    mbr_boot(m);
    for (i = 0; i < 4; i++)                     /* the PC starts from p, and from no other */
        if (m[446 + i * 16 + 4])
            m[446 + i * 16] = i + 1 == p ? 0x80 : 0;
    if ((e = bdisk_write(d, v->start, 1, s)) != 0
        || (copy && copy < rd16(s + 14) && (e = bdisk_write(d, v->start + copy, 1, s)) != 0)
        || (e = bdisk_write(d, 0, 1, m)) != 0)
        return bios_failed(c, e, "the write");
    kprintf("GLOS-DISK boot disk=%02x part=%u fat=%u\n", d, p, v->fat);
    say(c, "Done. The volume needs FreeDOS's KERNEL.SYS and COMMAND.COM in its first folder (COPY, once DOS sees it).\n");
    return 0;
}

/* ---- the command */

static const char usage[] =
    "DISK                              the hard disks and their partitions\n"
    "DISK n                            disk n's alone (1: the first)\n"
    "DISK n NEW /Y                     a new table: one partition over all of the disk\n"
    "DISK n FORMAT [p] [/V:label] /Y   partition p (1 unless said) made an empty FAT volume\n"
    "DISK n BOOT [p] /Y                boot records: the PC starts FreeDOS from partition p\n"
    "Without /Y a command says what would be lost and does nothing. The disk GLOS or DOS runs\n"
    "from is never written.\n";

static int number(const char *w, u32 *v)
{
    u32 n = 0;
    if (!*w)
        return -1;
    for (; *w; w++) {
        if (*w < '0' || *w > '9' || n > 9999)
            return -1;
        n = n * 10 + (u32)(*w - '0');
    }
    *v = n;
    return 0;
}

static int run(struct diskcmd *c)
{
    char *w[8], *label = 0, *verb;
    u32 nw = 0, i, n = 0, p = 1;
    int yes = 0;
    char *a = c->args;
    while (*a) {                                /* words, in capitals; the switches taken out */
        while (*a == ' ' || *a == '\t')
            *a++ = 0;
        if (!*a)
            break;
        if (nw == ARRAY_SIZE(w)) {
            say(c, "%s", usage);
            return 2;
        }
        w[nw++] = a;
        for (; *a && *a != ' ' && *a != '\t'; a++)
            if (*a >= 'a' && *a <= 'z')
                *a = (char)(*a - 32);
    }
    for (i = 0; i < nw;) {
        if (w[i][0] != '/') {
            i++;
            continue;
        }
        if (!strcmp(w[i], "/Y"))
            yes = 1;
        else if (!strncmp(w[i], "/V:", 3))
            label = w[i] + 3;
        else {
            say(c, "%s", usage);
            return !strcmp(w[i], "/?") ? 0 : 2;
        }
        memmove(w + i, w + i + 1, (nw - i - 1) * sizeof *w);
        nw--;
    }
    if (!nw || (nw == 1 && !strcmp(w[0], "LIST"))) {
        own_volumes(c);
        return list(c, 0);
    }
    if (number(w[0], &n) != 0 || n < 1 || n > 4) {
        say(c, "%s", usage);
        return 2;
    }
    own_volumes(c);
    if (nw == 1)
        return list(c, n);
    verb = w[1];
    if (!strcmp(verb, "NEW") && nw == 2)
        return do_new(c, n, yes);
    if ((!strcmp(verb, "FORMAT") || !strcmp(verb, "BOOT")) && (nw == 2 || (nw == 3 && number(w[2], &p) == 0)))
        return verb[0] == 'F' ? do_format(c, n, p, label, yes) : do_boot(c, n, p, yes);
    say(c, "%s", usage);
    return 2;
}

static u8 busy;                                 /* one DISK at a time (the layouts above are its) */

static void worker(void *arg)
{
    struct diskcmd *c = arg;
    void (*kick)(void);
    u32 f;
    int gone;
    bdisk_open();
    c->code = run(c);
    bdisk_close();
    f = irq_save();
    busy = 0;
    gone = c->gone, kick = c->kick;
    c->done = 1;                                /* (the reader's to free from here, unless it has gone) */
    irq_restore(f);
    if (gone)
        kfree(c);
    else if (kick)
        kick();
    thread_exit();
}

struct diskcmd *diskcmd_start(const char *args, void (*kick)(void))
{
    struct diskcmd *c;
    u32 f = irq_save();
    if (busy || strlen(args) >= sizeof c->args) {
        irq_restore(f);
        return 0;
    }
    busy = 1;
    irq_restore(f);
    if (!(c = kmalloc(sizeof *c))) {
        busy = 0;
        return 0;
    }
    memset(c, 0, sizeof *c);
    strcpy(c->args, args);
    c->kick = kick;
    if (!thread_create("disk", PRIO_NORMAL, worker, c)) {
        kfree(c);
        busy = 0;
        return 0;
    }
    return c;
}

u32 diskcmd_read(struct diskcmd *c, char *buf, u32 room)
{
    u32 n = c->head - c->tail;
    if (n > room)
        n = room;
    memcpy(buf, c->out + c->tail, n);
    c->tail += n;
    return n;
}

int diskcmd_done(const struct diskcmd *c, int *code)
{
    if (!c->done)
        return 0;
    *code = c->code;
    return 1;
}

void diskcmd_free(struct diskcmd *c)
{
    u32 f = irq_save();
    if (!c->done) {                             /* (its channel has gone: the work goes on to its end) */
        c->gone = 1;
        c->kick = 0;
        irq_restore(f);
        return;
    }
    irq_restore(f);
    kfree(c);
}
