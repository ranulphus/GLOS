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
    u8 own_floppy;                              /* bit 0: GLOS or DOS runs from A:; bit 1: from B: */
    u8 dd;                                      /* the command is DD */
    u8 to_conn;                                 /* DD's output is the connection: what is said goes apart (errors) */
    u8 *data;                                   /* ... its bytes, a ring the reader empties */
    volatile u32 dh;                            /* (the worker's) */
    volatile u32 dt;                            /* (the reader's) */
    struct waitq roomq;                         /* the worker waits here for room in it */
};
#define DATA 32768u

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
        if (l == 'A' || l == 'B')
            c->own_floppy |= (u8)(1u << (l - 'A'));
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


/* ---- DD: a raw copy between disks, files and the connection
 *
 *   DD IF=source OF=destination [SKIP=n] [SEEK=n] [COUNT=n] [/Y]
 *
 * A source or destination is DISKn (hard disk n, all of it), DISKn:p (its partition p, one of the table's
 * four), A: or B: (a floppy, all of it), a file by its full path (C:\IMAGES\DISK.IMG), or - : the connection
 * the command was given over (as a destination: `ssh pc "glos dd if=disk1 of=-" > disk.img`). SKIP, SEEK and
 * COUNT are in sectors of 512 bytes: where the source is read from, where the destination is written from,
 * and how many (all of the source unless said). A disk is written only with /Y, after what it would
 * destroy is said, and never the disk, volume or floppy GLOS or DOS runs from. A file is written over, as COPY
 * does. From the connection to a disk is not here yet. */

struct dd_end {
    char kind;                                  /* 'd' a disk's sectors, 'f' a file, 'c' the connection */
    u8 drive;                                   /* the BIOS's number */
    u8 disk, part;                              /* DISKn, and :p (0: all of it); 0, 0: a floppy */
    u32 start, sectors;                         /* the range on the disk; a file's size, rounded up */
    u32 bytes;                                  /* a file's size */
    u16 handle;
    const char *name;
};

static void dq(struct dos_req *q, u16 ax, u16 bx, u16 cx, u16 dx)
{
    memset(q, 0, sizeof *q);
    q->n = 1;
    q->r[0].ax = ax, q->r[0].bx = bx, q->r[0].cx = cx, q->r[0].dx = dx;
    q->r[0].ds = q->r[0].es = DOS_XFER;
}

static int dq_made(struct dos_req *q) { return dos_call(q) != 0 || q->error || (q->r[0].flags & 1) ? -1 : 0; }

static int f_open(const char *path, int create, u16 *h)
{
    struct dos_req q;
    char p[128];
    memset(p, 0, sizeof p);
    strcpy(p, path);
    dq(&q, create ? 0x3C00 : 0x3D00, 0, 0, 0);
    q.in = (const u8 *)p, q.in_len = sizeof p, q.in_at = 0;
    if (dq_made(&q) != 0)
        return -1;
    *h = q.r[0].ax;
    return 0;
}

static int f_seek(u16 h, u32 pos, int from_end, u32 *at)
{
    struct dos_req q;
    dq(&q, from_end ? 0x4202 : 0x4200, h, (u16)(pos >> 16), (u16)pos);
    if (dq_made(&q) != 0)
        return -1;
    if (at)
        *at = (u32)q.r[0].dx << 16 | q.r[0].ax;
    return 0;
}

static int f_read(u16 h, u8 *buf, u32 n, u32 *got)
{
    struct dos_req q;
    dq(&q, 0x3F00, h, (u16)n, 0);
    q.out = buf, q.out_len = n, q.out_at = 0;
    if (dq_made(&q) != 0)
        return -1;
    *got = q.r[0].ax;
    return 0;
}

static int f_write(u16 h, const u8 *buf, u32 n)        /* 0; -1; -2: the disk is full */
{
    struct dos_req q;
    dq(&q, 0x4000, h, (u16)n, 0);
    q.in = buf, q.in_len = n, q.in_at = 0;
    if (dq_made(&q) != 0)
        return -1;
    return q.r[0].ax == n ? 0 : -2;
}

static void f_close(u16 h)
{
    struct dos_req q;
    dq(&q, 0x3E00, h, 0, 0);
    (void)dos_call(&q);
}

/* Bytes for the connection: into the ring, waiting for the reader while it is full. -1: the reader has gone. */
static int conn_put(struct diskcmd *c, const u8 *d, u32 n)
{
    while (n) {
        u32 room = DATA - (c->dh - c->dt), k = n < room ? n : room, i, f;
        void (*kick)(void) = c->kick;
        if (c->gone)
            return -1;
        for (i = 0; i < k; i++)
            c->data[(c->dh + i) & (DATA - 1)] = d[i];
        c->dh += k, d += k, n -= k;
        if (kick)
            kick();
        if (n) {
            f = irq_save();
            if (DATA - (c->dh - c->dt) == 0 && !c->gone)
                thread_wait(&c->roomq, timer_ticks() + 256);
            irq_restore(f);
        }
    }
    return 0;
}

/* One end, from its word (in capitals). 0, or -1 with the reason said. */
static int dd_end(struct diskcmd *c, struct dd_end *e, const char *w, int dest)
{
    static struct layout L;
    struct bdisk_geo g;
    u32 i = 0;
    memset(e, 0, sizeof *e);
    e->name = w;
    if (!strcmp(w, "-")) {
        e->kind = 'c';
        if (!dest)
            say(c, "DD: from the connection is not here yet (a file, sent by SFTP, can be the source)\n");
        return dest ? 0 : -1;
    }
    if ((w[0] == 'A' || w[0] == 'B') && w[1] == ':' && !w[2]) {
        e->kind = 'd', e->drive = (u8)(w[0] - 'A');
        if (bdisk_geo(e->drive, &g) != 0 || !g.sectors) {
            say(c, "DD: no floppy drive %s, or nothing in it\n", w);
            return -1;
        }
        if (dest && (c->own_floppy & (1u << e->drive))) {
            say(c, "DD: %s is the floppy GLOS or DOS runs from: it is not written.\n", w);
            return -1;
        }
        e->sectors = g.sectors;
        return 0;
    }
    if (!strncmp(w, "DISK", 4) && w[4] >= '1' && w[4] <= '4' && (!w[5] || (w[5] == ':' && w[6] >= '1' && w[6] <= '4' && !w[7]))) {
        e->kind = 'd', e->disk = (u8)(w[4] - '0'), e->part = (u8)(w[5] ? w[6] - '0' : 0);
        e->drive = (u8)(0x7F + e->disk);
        if (scan(e->drive, &L) != 0 || L.table < -1) {
            say(c, "DD: the BIOS has no hard disk %u, or it can't be read\n", e->disk);
            return -1;
        }
        e->sectors = L.g.sectors;
        if (e->part) {
            for (i = 0; i < L.nv && L.v[i].n != e->part; i++)
                ;
            if (i == L.nv || !L.v[i].start || L.v[i].start >= L.g.sectors || L.v[i].sectors > L.g.sectors - L.v[i].start) {
                say(c, "DD: disk %u has no partition %u\n", e->disk, e->part);
                return -1;
            }
            e->start = L.v[i].start, e->sectors = L.v[i].sectors;
        }
        if (!dest)
            return 0;
        if (c->own_unknown)
            say(c, "DD: DOS doesn't say which disk it and GLOS run from, so no hard disk is written from here.\n");
        else if (e->part ? own_vol(c, &L.v[i]) : own_disk(c, &L))
            say(c, "DD: %s is what GLOS or DOS runs from: it is not written.\n", w);
        else
            return 0;
        return -1;
    }
    if (!(w[0] >= 'A' && w[0] <= 'Z' && w[1] == ':' && w[2] == '\\' && w[3]) || strlen(w) > 79) {
        say(c, "DD: %s: DISKn, DISKn:p, A:, a file by its full path (C:\\FOLDER\\NAME), or -\n", w);
        return -1;
    }
    e->kind = 'f';
    if (dest)
        return 0;
    if (f_open(w, 0, &e->handle) != 0 || f_seek(e->handle, 0, 1, &e->bytes) != 0 || f_seek(e->handle, 0, 0, 0) != 0) {
        say(c, "DD: %s can't be opened\n", w);
        return -1;
    }
    e->sectors = e->bytes / 512 + (e->bytes % 512 != 0);
    return 0;
}

static int dd_fail(struct diskcmd *c, const char *what, const char *name, int e, u32 at)
{
    if (e == -2)
        say(c, "DD: %s is full, at sector %u\n", name, at);
    else if (e == BDISK_BUSY)
        say(c, "DD: a program has the PC: %s %s stopped at sector %u\n", what, name, at);
    else if (e > 0)
        say(c, "DD: %s %s stopped at sector %u: the BIOS's error %02xh\n", what, name, at, e);
    else
        say(c, "DD: %s %s stopped at sector %u\n", what, name, at);
    return 1;
}

static int num32(const char *w, u32 *v)         /* a count of sectors: decimal, up to 2^32 - 1 */
{
    u32 n = 0;
    if (!*w)
        return -1;
    for (; *w; w++) {
        if (*w < '0' || *w > '9' || n > 429496729u || (n == 429496729u && *w > '5'))
            return -1;
        n = n * 10 + (u32)(*w - '0');
    }
    *v = n;
    return 0;
}

static int run_dd(struct diskcmd *c)
{
    struct dd_end in, out;
    char *w[8], *a = c->args, *src = 0, *dst = 0;
    u32 nw = 0, i, skip = 0, seek = 0, count = 0xFFFFFFFFu, done = 0, run, k, got, next_say = 0, t0, bytes;
    int yes = 0, e = 0, ok = 1, has_count = 0;
    u8 *buf;
    while (*a) {
        while (*a == ' ' || *a == '\t')
            *a++ = 0;
        if (!*a)
            break;
        if (nw == ARRAY_SIZE(w)) {
            nw = 0;
            break;
        }
        w[nw++] = a;
        for (; *a && *a != ' ' && *a != '\t'; a++)
            if (*a >= 'a' && *a <= 'z')
                *a = (char)(*a - 32);
    }
    for (i = 0; i < nw && ok; i++) {
        if (!strcmp(w[i], "/Y"))
            yes = 1;
        else if (!strncmp(w[i], "IF=", 3) && w[i][3])
            src = w[i] + 3;
        else if (!strncmp(w[i], "OF=", 3) && w[i][3])
            dst = w[i] + 3;
        else if (!strncmp(w[i], "SKIP=", 5))
            ok = num32(w[i] + 5, &skip) == 0;
        else if (!strncmp(w[i], "SEEK=", 5))
            ok = num32(w[i] + 5, &seek) == 0;
        else if (!strncmp(w[i], "COUNT=", 6))
            ok = num32(w[i] + 6, &count) == 0, has_count = 1;
        else
            ok = 0;
    }
    if (!nw || !ok || !src || !dst) {
        say(c, "DD IF=source OF=destination [SKIP=n] [SEEK=n] [COUNT=n] [/Y]\n"
            "A source or destination: DISKn (a hard disk), DISKn:p (its partition p), A: (a floppy),\n"
            "a file by its full path, or - (the connection, as a destination). SKIP, SEEK and COUNT\n"
            "are in sectors of 512 bytes. A disk is written only with /Y, and never the one GLOS or\n"
            "DOS runs from.\n");
        return 2;
    }
    c->to_conn = !strcmp(dst, "-");             /* (before anything is said: it goes apart from the bytes) */
    own_volumes(c);
    dos_hold();
    if (dd_end(c, &in, src, 0) != 0 || dd_end(c, &out, dst, 1) != 0) {
        if (in.kind == 'f' && in.handle)
            f_close(in.handle);
        dos_release();
        return 1;
    }
    if (skip > in.sectors)
        skip = in.sectors;
    if (count > in.sectors - skip)
        count = in.sectors - skip;
    if (in.kind == 'd' && out.kind == 'd' && in.drive == out.drive
        && in.start + skip < out.start + seek + count && out.start + seek < in.start + skip + count) {
        say(c, "DD: the source and the destination overlap\n");
        dos_release();
        return 1;
    }
    if (out.kind == 'd') {
        if (seek > out.sectors || count > out.sectors - seek) {
            say(c, "DD: %u sectors from sector %u don't fit in %s (%u sectors)\n", count, seek, dst, out.sectors);
            ok = 0;
        } else {
            say(c, "%u sectors (%u MB) of %s to %s, from its sector %u: what is there %s LOST.\n", count, mb(count), src,
                dst, seek, yes ? "IS" : "WOULD BE");
            if (!yes)
                say(c, "Nothing was done. To do it: the same command with /Y\n");
        }
        if (!ok || !yes) {
            if (in.kind == 'f')
                f_close(in.handle);
            dos_release();
            return ok ? 2 : 1;
        }
    }
    if (!(buf = kmalloc(64 * 512)) || (out.kind == 'c' && !(c->data = kmalloc(DATA)))) {
        say(c, "DD: no memory\n");
        ok = 0;
    } else if (out.kind == 'f' && (f_open(dst, 1, &out.handle) != 0 || (seek && f_seek(out.handle, seek * 512, 0, 0) != 0))) {
        say(c, "DD: %s can't be made\n", dst);
        ok = 0;
    } else if (in.kind == 'f' && skip && f_seek(in.handle, skip * 512, 0, 0) != 0) {
        say(c, "DD: %s can't be read from sector %u\n", src, skip);
        ok = 0;
    }
    run = bdisk_run();                          /* (the most the BIOS's calls take here) */
    if (run > 64)
        run = 64;
    if ((in.kind == 'f' || out.kind == 'f') && run > dos_xfer_size() / 512)
        run = dos_xfer_size() / 512;            /* (a file's pieces by the DOS server's buffer) */
    if (!run)
        run = 1;
    t0 = timer_ticks();
    while (ok && done < count) {
        k = count - done < run ? count - done : run;
        bytes = k * 512;
        if (in.kind == 'd') {
            if ((e = bdisk_read(in.drive, in.start + skip + done, k, buf)) != 0)
                ok = !dd_fail(c, "reading", src, e, skip + done);
        } else {
            if (f_read(in.handle, buf, bytes, &got) != 0 || !got)
                ok = !dd_fail(c, "reading", src, 0, skip + done);
            else if (got < bytes) {             /* the file's last sector, part of one: the rest zeros for a disk */
                memset(buf + got, 0, bytes - got);
                k = got / 512 + (got % 512 != 0);
                bytes = out.kind == 'd' ? k * 512 : got;
            }
        }
        if (!ok)
            break;
        if (in.kind == 'f' && !has_count && done + k >= count && in.bytes % 512 && out.kind != 'd')
            bytes = (k - 1) * 512 + in.bytes % 512;         /* (a file's own length, to the byte) */
        if (out.kind == 'd')
            e = bdisk_write(out.drive, out.start + seek + done, k, buf);
        else if (out.kind == 'f')
            e = f_write(out.handle, buf, bytes);
        else
            e = conn_put(c, buf, bytes) ? -9 : 0;
        if (e == -9)                            /* (the connection has gone: nobody to tell) */
            break;
        if (e) {
            ok = !dd_fail(c, "writing", dst, e, seek + done);
            break;
        }
        done += k;
        if (done / 204800 > next_say) {         /* every 100 MB */
            next_say = done / 204800;
            say(c, "  %u MB\n", mb(done));
        }
    }
    if (in.kind == 'f')
        f_close(in.handle);
    if (out.kind == 'f' && out.handle)
        f_close(out.handle);
    dos_release();
    kfree(buf);
    if (ok && done == count) {
        u32 secs = (timer_ticks() - t0) >> 10;
        kprintf("GLOS-DISK dd from=%s to=%s skip=%u seek=%u sectors=%u seconds=%u\n", src, dst, skip, seek, done, secs);
        say(c, "%u sectors (%u MB) copied in %u seconds.\n", done, mb(done), secs);
        if (out.kind == 'd' && out.disk)
            say(c, "%s", restart);
        return 0;
    }
    return 1;
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
    c->code = c->dd ? run_dd(c) : run(c);
    bdisk_close();
    f = irq_save();
    busy = 0;
    gone = c->gone, kick = c->kick;
    c->done = 1;                                /* (the reader's to free from here, unless it has gone) */
    irq_restore(f);
    if (gone) {
        kfree(c->data);
        kfree(c);
    } else if (kick)
        kick();
    thread_exit();
}

struct diskcmd *diskcmd_start(const char *args, void (*kick)(void), int dd)
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
    c->dd = (u8)(dd != 0);
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
    if (!c->done) {                             /* (its channel has gone: the work goes on to its end; a DD
                                                   that waits for its reader stops) */
        c->gone = 1;
        c->kick = 0;
        thread_wake(&c->roomq);
        irq_restore(f);
        return;
    }
    irq_restore(f);
    kfree(c->data);
    kfree(c);
}

int diskcmd_errors_apart(const struct diskcmd *c) { return c->to_conn; }

u32 diskcmd_data(struct diskcmd *c, u8 *buf, u32 room)
{
    u32 n = c->dh - c->dt, i, f;
    if (!c->data || !n)
        return 0;
    if (n > room)
        n = room;
    for (i = 0; i < n; i++)
        buf[i] = c->data[(c->dt + i) & (DATA - 1)];
    f = irq_save();
    c->dt += n;
    thread_wake(&c->roomq);
    irq_restore(f);
    return n;
}
