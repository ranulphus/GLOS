/* The system VM's own DOS on a native boot (docs/v1/native-dos.md, native boot, NB2): GLOSLDR.SYS started GLOS
 * with no DOS under it, so INT 20h and 21h in the system VM are GLOS's own DOS's (kernel/ndos/dos21.c), over the
 * VM's memory as DOS's own structures in DOS's layouts (the SDA, the List of Lists, the file table, the MCB chain,
 * PSPs), so that the kernel's readers of them, the stub, the agent, sessions, apps and the DPMI host work as they
 * do over a DOS.
 *
 * The loader (its PSP from bootinfo's int2f_entry, its memory to the word at PSP:2 the prelude wrote) is adopted
 * as the running program, its own parent; DOS's tables go in a block of DOS's own at the top of conventional
 * memory; INT 20h and 21h's vectors point at ARPLs there (a far call to "DOS's handler", as rm_call and the DPMI
 * host's reflection make, arrives as a #UD: vdos_arpl). A call that would wait for a key comes back to the program
 * at its INT, the VM idling in between as at DOS's own console wait (no wait in a trap: the VM thread is also the
 * apps' and the DOS server's). C: is the volume GLOS started from, mounted at the first call: fsv.c over GLOS's
 * IDE driver (in the trap, as the cache's own INT 13h is) when the BIOS's disk is an IDE disk GLOS drives, else
 * over the BIOS's own calls (a USB stick, a SCSI or SATA disk: "the BIOS's disks" below). */
#include <string.h>

#include "glos/bootinfo.h"
#include "agent.h"
#include "app.h"
#include "arch.h"
#include "clock.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "timer.h"
#include "vm.h"
#include "../drv/ide.h"
#include "../ndos/dos21.h"
#include "../ndos/dtime.h"
#include "cache.h"
#include "vdos.h"

static void vm_wr16(u32 lin, u32 v)
{
    vm_wr8(lin, (u8)v);
    vm_wr8(lin + 1, (u8)(v >> 8));
}

static struct dos_m vd;
static struct ata_dev *cdev;
static unsigned long cbase;
static int ready, mounted, unanswered;

static void vd_window(void);

/* GLOS's clock, read with interrupts off (a call's work runs with them on: vdos_int). */
static u32 local_now(void)
{
    u32 f = irq_save(), t = clock_local();

    irq_restore(f);
    return t;
}

/* ---- the BIOS's disks

   A disk GLOS has no driver for is read and written by the BIOS's INT 13h, as nested real-mode calls from the
   trap (the VM's thread: rm_call; the disk cache answers what it holds on the way, as for a program's INT 13h).
   The calls' stack and the sectors' buffer are a block of DOS's memory under its tables (dos_m_init_vm's extra;
   the BIOS wants its data below 1 MB), given back when C: turns out to be GLOS's own driver's: 2 KB for a
   request packet at its start and the stack above it, then 16 KB of sectors, the part of them used never across
   a 64 KB boundary (a BIOS's DMA). */
#define X_HEAD      0x800u
#define X_PARAS     (X_HEAD / 16 + 0x400)
static unsigned xseg;                           /* the block; 0: none */
static u32 xdata, xrun;                         /* the buffer (linear) and the sectors it holds */
static struct bi_disk *bdsk;                    /* the BIOS's disk C: is on (bootinfo's) */
static int cbios;                               /* C: is read by the BIOS's calls */

/* INT 13h: 0, or the BIOS's error (FFh: none said). */
static int bios13(struct rmregs *r)
{
    u32 f = irq_save();                         /* (a call's work runs with interrupts on: vdos_int) */
    int made;

    r->ss = (u16)xseg;
    r->sp = X_HEAD;
    r->flags = 2;
    made = rm_call(0, r, RM_INT, 0x13, 0, 0) == 0;
    irq_restore(f);
    if (!made)
        return 0xFF;
    if (!(r->flags & 1))
        return 0;
    return (r->eax >> 8) & 0xFF ? (int)((r->eax >> 8) & 0xFF) : 0xFF;
}

/* count sectors of the disk at lba: the BIOS's LBA calls where it has them, else cylinder, head and sector, a
   track's rest at a time; a call that fails is made again after a reset, three times. 0, or the BIOS's error. */
static int bios_rw(struct bi_disk *bdsk, int write, u32 lba, u8 *buf, u32 count)
{
    struct rmregs r;
    int e = 0, tries;

    if (!xseg || !bdsk)
        return 0xFF;
    while (count) {
        u32 k = count < xrun ? count : xrun, c = 0, h = 0, s = 0;

        if (!(bdsk->flags & BI_DISK_EDD)) {
            u32 track = bdsk->heads * bdsk->spt, t;

            if (!track)
                return 0x01;
            c = lba / track, t = lba % track, h = t / bdsk->spt, s = t % bdsk->spt;
            if (k > bdsk->spt - s)
                k = bdsk->spt - s;
            if (c > 1023)
                return 0x01;
        }
        if (write)
            memcpy(vm_ptr(xdata), buf, k * 512);
        for (tries = 0; tries < 3; tries++) {
            memset(&r, 0, sizeof r);
            if (bdsk->flags & BI_DISK_EDD) {
                u8 *p = vm_ptr((u32)xseg << 4);

                memset(p, 0, 16);
                p[0] = 16, p[2] = (u8)k, p[4] = (u8)(xdata & 15), p[6] = (u8)(xdata >> 4), p[7] = (u8)(xdata >> 12);
                p[8] = (u8)lba, p[9] = (u8)(lba >> 8), p[10] = (u8)(lba >> 16), p[11] = (u8)(lba >> 24);
                r.eax = write ? 0x4300 : 0x4200;
                r.ds = (u16)xseg;
            } else {
                r.eax = (write ? 0x0300u : 0x0200u) | k;
                r.ecx = ((c & 0xFF) << 8) | ((c >> 2) & 0xC0) | (s + 1);
                r.edx = h << 8;
                r.es = (u16)(xdata >> 4);
                r.ebx = xdata & 15;
            }
            r.edx |= bdsk->dl;
            if ((e = bios13(&r)) == 0)
                break;
            memset(&r, 0, sizeof r);
            r.edx = bdsk->dl;
            bios13(&r);
        }
        if (e)
            return e;
        if (!write)
            memcpy(buf, vm_ptr(xdata), k * 512);
        lba += k, buf += k * 512, count -= k;
    }
    return 0;
}

int vdos_c_bios(void) { return cbios; }

/* GLOS leaves the PC's USB controllers to the BIOS when C: is the BIOS's disk and the BIOS doesn't say that the
   disk is something other than a USB one: its calls for a USB stick stop the moment GLOS takes the controller
   the stick is on (kernel/drv/uhci.c). */
int vdos_usb_bios(void)
{
    if (ready && !mounted)
        kprintf("GLOS-WARN native: USB asked about before C: was mounted\n");
    return cbios && vm.bi->boot_if != BI_IF_ATA && vm.bi->boot_if != BI_IF_OTHER;
}

/* ---- the drives

   A volume: where it is (a disk GLOS's IDE driver has, else one of the BIOS's by its calls; its first sector
   there) and the file system on it. The volume GLOS started from is the first, kept here; the others the loader
   lettered (bootinfo's vols: stage 3) are made as they are mounted. */
struct nvol {
    struct fsv_vol v;
    struct ata_dev *dev;                        /* 0: the BIOS's calls */
    struct bi_disk *bd;                         /* the BIOS's disk (its number, its geometry) */
    unsigned long base;
    /* a floppy drive's (removable): its geometry the disk's own (its boot sector's), the disk in it mounted when
       the drive is used and again when the BIOS says it changed */
    int removable, mounted, line;               /* line: 1 the drive has a change line, 0 not, -1 not asked */
    u32 used_at, failed_at;
    struct bi_disk geo;
};
static struct nvol own;
static struct nvol *flop[2];                    /* A:, B: */

/* Whether the disk in a floppy drive may have changed since it was last used: the BIOS's change line (INT 13h
   16h); a drive without one, after two seconds unused (DOS's rule). */
static int flop_changed(struct nvol *n)
{
    struct rmregs r;
    int e;

    if (n->line < 0) {                          /* (asked once: 15h's type, 2 a drive that has the line) */
        memset(&r, 0, sizeof r);
        r.eax = 0x1500, r.edx = n->geo.dl;
        n->line = bios13(&r) == 0 && ((r.eax >> 8) & 0xFF) == 2;
    }
    if (!n->line)
        return timer_ticks() - n->used_at > 2048;
    memset(&r, 0, sizeof r);
    r.eax = 0x1600, r.edx = n->geo.dl;
    e = bios13(&r);
    return e != 0;                              /* (06h: changed; 80h: no disk, not ready) */
}

/* (through the disk cache, which a program's own INT 13h on the same disk goes through too) */
static int c_read(void *ctx, unsigned long lba, unsigned char *buf, unsigned count)
{
    struct nvol *n = ctx;

    vd_window();
    if (!n->dev)
        return bios_rw(n->bd, 0, (u32)(n->base + lba), buf, count) != 0;
    return cache_kread(n->bd ? n->bd->dl : 0x80, n->dev, (u32)(n->base + lba), count, buf) != 0;
}

static int c_write(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count)
{
    struct nvol *n = ctx;

    vd_window();
    if (n->removable && flop_changed(n)) {      /* (what was meant for the disk taken out: not onto another) */
        n->mounted = 0;
        return 1;
    }
    if (!n->dev)
        return bios_rw(n->bd, 1, (u32)(n->base + lba), (u8 *)buf, count) != 0;
    return cache_kwrite(n->bd ? n->bd->dl : 0x80, n->dev, (u32)(n->base + lba), count, buf) != 0;
}

static unsigned long c_now(void *ctx)
{
    struct dos_date d;
    struct dos_time t;
    u32 local = local_now();

    (void)ctx;
    if (local < 315532800u)
        return 0;
    dos_from_seconds(local - 315532800u, &d, &t);
    return (unsigned long)dos_pack_date(&d) << 16 | dos_pack_time(&t);
}

static int fat_first(const unsigned char *s);

/* GLOS's own DOS asks which volume is in floppy drive `drive` now (dos21.h: media): the one mounted while the
   disk hasn't changed, or while files are open on it (DOS's "invalid disk change": they stay that disk's); else
   the disk there now, mounted afresh, its geometry its boot sector's. 15h: no disk, or none that is a FAT
   volume. */
static struct fsv_vol *vd_media(void *ctx, int drive, unsigned *err)
{
    struct nvol *n = drive >= 0 && drive < 2 ? flop[drive] : 0;
    static unsigned char bs[512];
    int i, open = 0;

    (void)ctx;
    *err = 0x0F;
    if (!n)
        return 0;
    *err = 0x15;
    if (n->mounted && !flop_changed(n)) {
        n->used_at = timer_ticks();
        return &n->v;
    }
    if (n->mounted) {
        for (i = 0; i < FSV_NODES; i++)
            open |= n->v.node[i].refs != 0;
        if (open) {
            n->used_at = timer_ticks();
            return &n->v;
        }
    }
    if (!n->mounted && n->failed_at && timer_ticks() - n->failed_at < 2048 && flop_changed(n))
        return 0;                               /* (still no disk: no new wait for one) */
    n->mounted = 0;
    n->geo.heads = n->geo.spt = 1;              /* (sector 0 is the first of track 0, whatever the disk) */
    if (bios_rw(&n->geo, 0, 0, bs, 1) != 0 || !fat_first(bs) || !bs[0x18] || !bs[0x1A]) {
        n->failed_at = timer_ticks() | 1;
        return 0;
    }
    n->geo.spt = bs[0x18] | (u32)bs[0x19] << 8;
    n->geo.heads = bs[0x1A] | (u32)bs[0x1B] << 8;
    if (fsv_mount(&n->v, c_read, c_write, c_now, n, 437, 0) != 0) {
        n->failed_at = timer_ticks() | 1;
        return 0;
    }
    kprintf("GLOS-NATIVE step=floppy %c: mounted spt=%u heads=%u line=%d\n", 'A' + drive, n->geo.spt, n->geo.heads, n->line);
    n->mounted = 1, n->failed_at = 0, n->used_at = timer_ticks();
    return &n->v;
}

static int is_fat(unsigned t)
{
    return t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E;
}

/* A FAT volume's first sector, by its figures (512 bytes a sector, sectors a cluster a power of two, one or two
   FATs, a media byte), not a partition table's: a disk with no table, and a USB stick a BIOS starts as a floppy
   (its "disk" is then the stick's partition). */
static int fat_first(const unsigned char *s)
{
    return s[510] == 0x55 && s[511] == 0xAA && (s[0] == 0xEB || s[0] == 0xE9) && s[11] == 0 && s[12] == 2 && s[13]
           && !(s[13] & (s[13] - 1)) && (s[16] == 1 || s[16] == 2) && s[21] >= 0xF0;
}

/* The disk GLOS started from (the BIOS's number of it and its geometry from the loader: bootinfo's boot_disk):
   the one IDE disk GLOS drives whose first sector is that disk's as the BIOS reads it, and whose size is its size
   (the size the BIOS's 48h gives, or up to a cylinder more, a BIOS's rounding; at least the size of 08h's geometry
   on a BIOS without 48h), by GLOS's IDE driver; none, or two alike, and the disk is read by the BIOS's calls (so
   is every disk with /BIOSDISK, the tests', and a disk of the floppies' numbers: a USB stick a BIOS starts as
   one). If the BIOS can't be asked: the ATA disk of its size, else the first. Then the volume: where the loader
   found it (bootinfo's boot_lba); without its word, the disk itself if its first sector is a FAT volume's, else
   its active FAT partition. */
static void mount_c(void)
{
    static unsigned char s[512], s0[512];
    struct bi_disk *bd = &vm.bi->boot_disk;
    int said = bd->bps == 512 && (bd->spt || (bd->flags & BI_DISK_PARAMS));    /* (the loader's word on it) */
    u32 dl = said ? bd->dl : 0x80;
    unsigned long want;
    struct ata_dev *d;
    int i, r = -1, hits = 0, asked, differ = 0, failed = 0, letter, needs_bios = 0;

    mounted = 1;
    if (said)
        bdsk = bd;
    for (i = 0; !bdsk && i < (int)vm.bi->n_disks; i++)
        if (vm.bi->disks[i].dl == dl && vm.bi->disks[i].bps == 512)
            bdsk = &vm.bi->disks[i];
    want = bdsk ? bdsk->sectors_lo : 0;
    /* The BIOS is asked first, and GLOS's IDE driver looks at the disks as it does beside a DOS: with no reset
       and no settings of its own (ATA_PROBE_OWN's). The BIOS goes on reading these disks: the one GLOS started
       from when it has no driver for it, any of them for DISK and for programs' INT 13h. After a reset a drive
       has lost what the BIOS set (the sectors a block of its READ MULTIPLE, its timing): 86Box's BIOS then read
       16 sectors of a longer call right and the rest wrong, and the owner's PCChips M925G gave a first sector
       that was none (2026-11-23). */
    asked = bios_rw(bdsk, 0, 0, s0, 1) == 0;
    ide_probe(0);
    for (i = 0; dl >= 0x80 && (d = ide_dev(i)) != 0; i++) {
        if (!d->present || d->atapi)
            continue;
        if (asked) {
            int size = d->sectors_hi || d->sectors_lo >= want;

            if (size && (bdsk->flags & BI_DISK_PARAMS) && !bdsk->sectors_hi)
                size = !d->sectors_hi && d->sectors_lo - want <= 255u * 63;
            if (size && !(vm.bi->flags & BI_F_BIOSDISK)) {
                if (ata_read(d, 0, 0, 1, s, 0) != 0)
                    failed++;
                else if (memcmp(s, s0, 512))
                    differ++;
                else
                    hits++, cdev = d;
            }
            continue;
        }
        if (want && d->sectors_lo == want) {
            cdev = d;
            break;
        }
        if (!cdev)
            cdev = d;
    }
    if (asked && hits != 1)
        cdev = 0, cbios = 1;
    /* (what was found, for a PC's log: the BIOS's sector's first bytes and its last two, the IDE disks that
       had the same sector, another, or none to give) */
    kprintf("GLOS-NATIVE step=c-disk dl=%02x chs=%u/%u/%u flags=%x asked=%d first=%02x%02x%02x sig=%02x%02x "
            "same=%d differ=%d failed=%d\n", dl, bdsk ? (u32)bdsk->cyls : 0, bdsk ? (u32)bdsk->heads : 0,
            bdsk ? (u32)bdsk->spt : 0, bdsk ? (u32)bdsk->flags : 0, asked, s0[0], s0[1], s0[2], s0[510], s0[511],
            hits, differ, failed);
    if (cbios) {
        memcpy(s, s0, 512);
        r = 0;
    } else if (cdev) {
        r = ata_read(cdev, 0, 0, 1, s, 0);
    }
    if (r == 0) {
        r = -1;
        cbase = 0;
        if (said)                               /* (the loader's volume: the partition GLOS started from, active
                                                   or not) */
            cbase = vm.bi->boot_lba;
        else if (!fat_first(s) && s[510] == 0x55 && s[511] == 0xAA)
            for (i = 0; i < 4; i++)
                if ((s[446 + i * 16] & 0x80) && is_fat(s[446 + i * 16 + 4])) {
                    cbase = (unsigned long)s[446 + i * 16 + 8] | (unsigned long)s[446 + i * 16 + 9] << 8
                            | (unsigned long)s[446 + i * 16 + 10] << 16 | (unsigned long)s[446 + i * 16 + 11] << 24;
                    break;
                }
        own.dev = cbios ? 0 : cdev;
        own.bd = bdsk;
        own.base = cbase;
        r = fsv_mount(&own.v, c_read, c_write, c_now, &own, 437, 0);
    }
    letter = vm.bi->boot_drive >= 1 && vm.bi->boot_drive <= DOS_DRIVES ? (int)vm.bi->boot_drive - 1 : 2;
    vd.vol[letter] = r == 0 ? &own.v : 0;
    vd.cds.valid = r == 0 ? 1u << letter : 0;
    kprintf("GLOS-NATIVE step=c dl=%02x disk=%s if=%u base=%u mount=%d drive=%c\n", dl,
            cbios ? "bios" : cdev ? cdev->model : "none", (u32)vm.bi->boot_if, (u32)cbase, r, 'A' + letter);
    /* The other volumes, by the letters the loader gave them (MS-DOS's order: kernel/disk/letters.c). A disk
       other than the one GLOS started from is GLOS's IDE driver's if exactly one of its disks, not yet another's,
       has the BIOS's first sector of it; else the BIOS's calls read it. A volume that doesn't mount (not
       formatted, not a FAT volume after all) has no drive. */
    for (i = 0; i < (int)vm.bi->n_vols && i < BI_VOLS; i++) {
        struct bi_vol *bv = &vm.bi->vols[i];
        struct bi_disk *bd = 0;
        struct ata_dev *dev = 0;
        struct nvol *n;
        int k, m;

        if ((bv->flags & BI_VOL_OWN) || bv->letter >= DOS_DRIVES || vd.vol[bv->letter])
            continue;
        if (bv->flags & BI_VOL_REMOVABLE) {     /* (a floppy drive: its disk looked at when it is used) */
            if (bv->letter > 1 || !(n = kmalloc(sizeof *n)))
                continue;
            memset(n, 0, sizeof *n);
            n->removable = 1, n->line = -1, n->bd = &n->geo;
            n->geo.dl = bv->dl, n->geo.bps = 512;
            flop[bv->letter] = n;
            vd.removable |= 1u << bv->letter;
            vd.cds.valid |= 1u << bv->letter;
            vd.media = vd_media;
            needs_bios = 1;
            kprintf("GLOS-NATIVE step=drive %c: dl=%02x removable\n", 'A' + (int)bv->letter, (u32)bv->dl);
            continue;
        }
        if (bdsk && bv->dl == bdsk->dl) {
            bd = bdsk, dev = cbios ? 0 : cdev;
        } else {
            for (k = 0; k < (int)vm.bi->n_disks; k++)
                if (vm.bi->disks[k].dl == bv->dl && vm.bi->disks[k].bps == 512)
                    bd = &vm.bi->disks[k];
            if (!bd || bios_rw(bd, 0, 0, s0, 1) != 0)
                continue;
            for (k = 0, hits = 0; !(vm.bi->flags & BI_F_BIOSDISK) && (d = ide_dev(k)) != 0; k++)
                if (d->present && !d->atapi && d != cdev && ata_read(d, 0, 0, 1, s, 0) == 0 && !memcmp(s, s0, 512))
                    hits++, dev = d;
            if (hits != 1)
                dev = 0;
        }
        if (!(n = kmalloc(sizeof *n)))
            break;
        memset(n, 0, sizeof *n);
        n->dev = dev, n->bd = bd, n->base = bv->start;
        m = fsv_mount(&n->v, c_read, c_write, c_now, n, 437, 0);
        kprintf("GLOS-NATIVE step=drive %c: dl=%02x disk=%s base=%u type=%02x mount=%d\n", 'A' + (int)bv->letter, (u32)bv->dl,
                dev ? dev->model : "bios", (u32)bv->start, (u32)bv->type, m);
        if (m != 0)
            continue;                           /* (its memory stays: a few KB, once) */
        vd.vol[bv->letter] = &n->v;
        vd.cds.valid |= 1u << bv->letter;
        needs_bios |= !dev;
    }
    if (!cbios && !needs_bios && xseg) {        /* (GLOS's own driver for all: the BIOS's calls' block back to DOS) */
        dos_mem_free(&vd.mcb, xseg);
        xseg = 0;
    }
}

/* ---- the machine */

/* A program's CON output, a character at a time where a DOS's console driver would make INT 29h: the agent's
   hook there first (a console job's window has it, and nothing else; a job whose output is kept has it too), then
   the screen, as the BIOS's teletype writes it. */
static void vd_out(void *ctx, const char *s, unsigned n)
{
    struct trapframe f;
    u32 fl = irq_save();                        /* (the agent's and the console's own state: interrupts off, as in
                                                   every trap's path; the call's work runs with them on) */

    (void)ctx;
    for (; n; n--, s++) {
        f.eax = (u8)*s;
        if (!agent_vm_int29(&f))
            vcon_tty(s, 1);
    }
    irq_restore(fl);
}

static unsigned long vd_now(void *ctx)
{
    u32 local = local_now();

    (void)ctx;
    return local > 315532800u ? local - 315532800u : 0;
}

/* A key the BIOS's INT 09h has put in its buffer (the VM's keyboard: the PC's, lcon's and the shell's typing, glos
   type's) into GLOS's DOS's ring when it is empty, one at a time, as DOS takes them through INT 16h: so a program
   that reads a line through DOS and then a key through INT 16h finds that key still in the BIOS's buffer. For a
   call that isn't input, only Ctrl-C is taken. */
static void vd_fill1(int input);

static void vd_fill(void *ctx, int input)
{
    u32 f = irq_save();

    (void)ctx;
    vd_fill1(input);
    irq_restore(f);
}

static void vd_fill1(int input)
{
    u32 head = vm_rd16(0x41A), tail = vm_rd16(0x41C), start = vm_rd16(0x480), end = vm_rd16(0x482), key;

    if (head == tail || dos_key_peek(&vd) >= 0)
        return;
    if (start < 0x1E || end > 0x100 || start >= end)
        start = 0x1E, end = 0x3E;
    key = vm_rd16(0x400 + head);
    if (!input && (key & 0xFF) != 0x03)
        return;
    dos_key_push(&vd, key);
    head += 2;
    if (head >= end)
        head = start;
    vm_wr16(0x41A, head);
}

static int vd_wait(void *ctx)
{
    vd_fill(ctx, 1);
    return dos_key_peek(&vd) >= 0 ? 0 : -1;     /* (none yet: the call comes back to its INT, no waiting in a trap) */
}

/* DOS's bytes for the vectors (the layout's misc + 4 on): ARPLs for INT 21h, 20h, 29h and 23h (DOS's own Ctrl-C
   handler), one a program's INT 23h returns to, and an IRET that ends INT 2Fh's chain. */
enum { V21, V20, V29, V23, VC23, NV };
static u32 arpl[NV];                            /* (seg << 16 | off) */

/* ---- interrupts during a call

   A DOS under GLOS does a call's work in the VM, as any program's code: interrupts on, the PC's own taken by
   their handlers there, other threads given their turns by the scheduler. GLOS's own DOS does that work in the
   kernel, in a trap's path, where interrupts are off: the clock stood still for a call's length, no other thread
   ran but at the disk's waits, and a timer's tick that came stayed unacknowledged (the line with it) until the
   VM was back in its own code, which for an app's calls, one after another, is hardly ever. An app's first thread
   got 7 to 14 of 27 alarms on the 486 while two others wrote files (thr's `interrupts`; 22 under a DOS). So:

   - a call's work runs with interrupts on (vdos_int: sti to cli; the IDE driver's waits take no moment of their
     own then, ide_patient); what it calls that keeps its own state by having them off turns them off for
     itself (the console's output and keys, the BIOS's disk calls, the windows below);
   - an interrupt of the VM's that waits is taken, as DOS takes it, when a call begins, at each sector read or
     written and at each turn of a disk wait (ide_wait_hook): three bytes of code among DOS's tables (STI, NOP,
     RETF) run in the VM, a nested real-mode call, on the DPMI host's stack where a client runs, else on the
     caller's own a little below its top. A program's handler that calls DOS from there, InDOS set or not, is
     refused (vdos_int): DOS is not entered twice. */
static u32 gone_at, gone_if;                    /* a call that went back to wait for a key (not through an ARPL):
                                                   where its INT is (+ 1), and its caller's interrupt flag */
static u32 dirty_at[DOS_DRIVES];                /* each drive's volume: when it was first seen to have something
                                                   unwritten (0: nothing) */
static struct trapframe *call_tf;               /* the caller's frame while a call is at work */
static u32 win;                                 /* the three bytes (seg << 16 | off) */
static u8 in_window;
static u32 reentered;

/* A disk wait's sleep to the next tick, where the VM's thread sleeps for anything (vm.waitq): an interrupt of
   the PC's wakes it there at once and ahead of other threads (vm_kick), so the wait's next look takes that
   interrupt (the window) before an app's thread has its own of the same tick. An app's timer handler that
   reads the BIOS's count of ticks (DJGPP's, for its signals) found it one behind otherwise, whenever the app's
   thread was the one running when the tick came: half an app's timer signals lost while its other threads
   were in DOS (thr's `interrupts` case natively, on the 486 and the bf6: 8 to 14 of 27; 22 under FreeDOS, whose
   code in the VM is woken so). */
static void vd_nap(void)
{
    u32 now = timer_ticks();

    thread_wait(&vm.waitq, now + 1 ? now + 1 : 1);
}

static void vd_window(void)
{
    struct rmregs r;
    u32 f;

    if (current != vm.thread || !call_tf || in_window || !vpic_pending(&vm.pic))
        return;
    memset(&r, 0, sizeof r);
    if (!dctx) {
        u32 sp = call_tf->esp & 0xFFFF;

        if (sp < 0x180)
            return;
        r.ss = (u16)call_tf->ss, r.sp = (u16)(sp - 0x40);
    }
    r.cs = (u16)(win >> 16), r.ip = (u16)win;
    r.flags = 2;
    f = irq_save();
    in_window = 1;
    rm_call(0, &r, RM_FAR, 0, 0, 0);
    in_window = 0;
    irq_restore(f);
}
static struct dos_regs86 c23;                   /* the call Ctrl-C stopped: its registers, where its INT was (a */
static u32 c23_sp, c23_at;                      /* call made again may wait), whether it came through the vector */
static int c23_via;

static void set_vec(u32 n, u32 v)
{
    vm_wr16(n * 4, v & 0xFFFF);
    vm_wr16(n * 4 + 2, v >> 16);
}

static u32 vec(u32 n) { return (u32)vm_rd16(n * 4 + 2) << 16 | vm_rd16(n * 4); }

int vdos_init(struct bootinfo *bi)
{
    unsigned psp = bi->int2f_entry >> 16;
    unsigned top = vm_rd16((u32)psp * 16 + 2), low_top = vm_rd16(0x413) * 64u;
    unsigned long a;
    u8 *mem = vm_ptr(0);
    int i;

    if (dos_m_init_vm(&vd, mem, (unsigned long)low_top * 16, psp, top, low_top, 0,
                      bi->boot_drive >= 1 && bi->boot_drive <= DOS_DRIVES ? (int)bi->boot_drive - 1 : 2, 437, X_PARAS,
                      &xseg) != 0) {
        kprintf("GLOS-NATIVE step=dos error psp=%04x top=%04x low_top=%04x\n", psp, top, low_top);
        return -1;
    }
    {                                           /* the sectors' buffer: the larger side of a 64 KB boundary in it */
        u32 lin = ((u32)xseg << 4) + X_HEAD, end = ((u32)xseg + X_PARAS) << 4, edge = (lin + 0xFFFF) & ~0xFFFFu;

        if (edge > lin && edge < end) {
            if (edge - lin >= end - edge)
                end = edge;
            else
                lin = edge;
        }
        xdata = lin, xrun = (end - lin) / 512;
    }
    vd.out = vd_out, vd.now = vd_now, vd.wait = vd_wait, vd.fill = vd_fill;
    vd.boot_drive = bi->boot_drive >= 1 && bi->boot_drive <= DOS_DRIVES ? bi->boot_drive : 3;
    bi->indos = vd.lay.sda + 1;
    bi->sda = vd.lay.sda;
    bi->lol = vd.lay.lol;
    a = vd.lay.misc + 4;
    for (i = 0; i < NV; i++) {
        mem[a + 2 * i] = 0x63, mem[a + 2 * i + 1] = 0xC0;
        arpl[i] = (u32)(a >> 4) << 16 | (u32)((a & 0xF) + 2 * i);
    }
    mem[a + 2 * NV] = 0xCF;                     /* (IRET) */
    a = vd.lay.misc + 0x10;                     /* the window: STI, NOP, RETF */
    mem[a] = 0xFB, mem[a + 1] = 0x90, mem[a + 2] = 0xCB;
    win = (u32)(a >> 4) << 16 | (u32)(a & 0xF);
    ide_wait_hook(vd_window, vd_nap);
    set_vec(0x21, arpl[V21]), set_vec(0x20, arpl[V20]), set_vec(0x29, arpl[V29]), set_vec(0x23, arpl[V23]);
    set_vec(0x2F, arpl[V21] + 2 * NV);          /* (INT 2Fh's chain ends here: GLOS's int2f_hook answers before it) */
    ready = 1;
    kprintf("GLOS-NATIVE step=dos psp=%04x top=%04x tables=%05x sda=%05x\n", psp, top, (u32)(vd.lay.lol - 0x12),
            (u32)vd.lay.sda);
    return 0;
}

int vdos_on(void) { return ready; }

#define LO16(x, v) ((x) = ((x) & 0xFFFF0000u) | ((v) & 0xFFFF))

static void to_regs(const struct trapframe *tf, struct dos_regs86 *r, u32 ret)
{
    r->ax = tf->eax & 0xFFFF, r->bx = tf->ebx & 0xFFFF, r->cx = tf->ecx & 0xFFFF, r->dx = tf->edx & 0xFFFF;
    r->si = tf->esi & 0xFFFF, r->di = tf->edi & 0xFFFF, r->bp = tf->ebp & 0xFFFF;
    r->ds = tf->v86_ds & 0xFFFF, r->es = tf->v86_es & 0xFFFF;
    r->cs = tf->cs & 0xFFFF, r->ip = ret & 0xFFFF, r->ss = tf->ss & 0xFFFF, r->sp = tf->esp & 0xFFFF;
    r->flags = tf->eflags & 0xFFFF;
}

static void from_regs(struct trapframe *tf, const struct dos_regs86 *r)
{
    LO16(tf->eax, r->ax), LO16(tf->ebx, r->bx), LO16(tf->ecx, r->cx), LO16(tf->edx, r->dx);
    LO16(tf->esi, r->si), LO16(tf->edi, r->di), LO16(tf->ebp, r->bp);
    tf->v86_ds = r->ds, tf->v86_es = r->es;
    tf->cs = r->cs, tf->eip = r->ip, tf->ss = r->ss;
    LO16(tf->esp, r->sp);
    tf->eflags = (tf->eflags & ~0x0FD5u) | (r->flags & 0x0FD5u);
}

static void push16(struct trapframe *tf, u32 v)
{
    LO16(tf->esp, (tf->esp - 2) & 0xFFFF);
    vm_wr16((tf->ss & 0xFFFF) * 16 + (tf->esp & 0xFFFF), v);
}

/* The program ends as DOS's own Ctrl-C handler ends it (4Dh's AH 01h): its parent runs on. */
static void ctrl_c_end(struct trapframe *tf, struct dos_regs86 *r)
{
    if (dos_ctrl_c_end(&vd, r) == DOS21_END)
        kprintf("GLOS-NATIVE dos end-of-root ctrl-c psp=%04x\n", dos_cur_psp(&vd));
    from_regs(tf, r);
}

/* INT n (20h, 21h) for the program at the trap frame: ret its return address (CS:IP after the INT), at where the
   INT was (for a call made again after a wait), via_arpl when it came through the vector (a far call). */
void vdos_int(struct trapframe *tf, u8 n, u32 ret, u32 at, int via_arpl)
{
    struct dos_regs86 r;
    u32 vif0 = vm.thread->vif;
    int res, d;

    if (call_tf) {                              /* from an interrupt taken in a call's window: not twice */
        if (reentered++ < 8)
            kprintf("GLOS-WARN native: INT %02xh ax=%04x from an interrupt while DOS is in a call: refused\n", n,
                    tf->eax & 0xFFFF);
        tf->eflags |= 1;
        return;
    }
    if (gone_at == at + 1 && !via_arpl)         /* (the INT made again after a wait for a key: its caller's own
                                                   interrupt flag, kept from the first time) */
        vif0 = gone_if;
    gone_at = 0;
    if (!mounted)
        mount_c();
    to_regs(tf, &r, ret);
    /* DOS turns interrupts on as a call begins, whatever the caller had: a call that came through the vector (a
       DPMI program's, reflected: interrupts off, as an INT leaves them) and takes its time in the kernel here (a
       file read) is no stretch of the program's with its interrupts off. Left off, the watch on such stretches
       (vdev.c: vif-stuck) put the VM's thread before every other for 50 ms of each long call and then warned:
       95 of the 163 jobs on the owner's bench G (2026-11-23), none on the hosted benches. The caller's own flag
       comes back at the call's end. */
    vm.thread->vif = 1;
    call_tf = tf;
    vd_window();                                /* (one that waits now is taken at once, as at DOS's own STI: a call
                                                   with no disk wait in it has no other moment, and an app's calls
                                                   come one after another with the VM never back in its own code) */
    /* The call, its work with interrupts on ("interrupts during a call", above): an app's own threads, the mixer
       and the network go on while a program is in DOS, as they do while a DOS under GLOS runs in the VM. */
    ide_patient(1);
    vm_wr8(vm.indos, (u8)(vm_rd8(vm.indos) + 1));        /* (InDOS, as a DOS has it during a call: the agent's hooks
                                                           and the kill look at it) */
    sti();
    res = n == 0x20 ? dos20(&vd, &r) : dos21(&vd, &r);
    /* What files' writes left for later (kernel/fs/fsv.c: a written file's entry and the FATs go to the disk at
       its close, as DOS's do) is flushed here when it has waited two seconds, and the volume marked clean: a
       program that keeps a file open and goes on calling DOS loses no more than that to a power cut. */
    for (d = 0; d < DOS_DRIVES; d++) {
        struct fsv_vol *fv = vd.vol[d];

        if (!fv || !fsv_dirty(fv))
            dirty_at[d] = 0;
        else if (!dirty_at[d])
            dirty_at[d] = timer_ticks() | 1;
        else if (timer_ticks() - dirty_at[d] >= 2048) {
            fsv_flush(fv);
            dirty_at[d] = 0;
        }
    }
    cli();
    vm_wr8(vm.indos, (u8)(vm_rd8(vm.indos) - 1));
    ide_patient(0);
    call_tf = 0;
    vm.thread->vif = vif0;
    if (res == DOS21_GONE) {                    /* a key waited for: back to the INT, the VM idling meanwhile */
        if (via_arpl) {
            push16(tf, (tf->eflags & 0xFDFF) | vif0 << 9);     /* (the caller's own interrupt flag in the frame) */
            push16(tf, tf->cs & 0xFFFF);
            push16(tf, ret & 0xFFFF);
            tf->cs = arpl[n == 0x20 ? V20 : V21] >> 16;
            tf->eip = arpl[n == 0x20 ? V20 : V21] & 0xFFFF;
        } else {
            tf->eip = at;
            gone_at = at + 1, gone_if = vif0;
        }
        /* DOS waits for a key with interrupts on, whatever its caller had: the VM idles so. (With the caller's
           flag, off for a call made through the DPMI host, the watchdog saw a VM halted with its interrupts
           off: GLOS-WARN vif-stuck, 2026-11-24.) The caller's own comes back with the call made again. */
        vm.thread->vif = 1;
        app_vm_idle_turn(0);                    /* (an app's own calls meanwhile, as at a DOS's wait for a key: its
                                                   console job's keys may be what it is about to type) */
        if (vm_rd16(0x41A) == vm_rd16(0x41C))
            vm_idle(timer_ticks() + 8);
        return;
    }
    if (res == DOS21_CTRLC) {
        int up = vec(0x23) == arpl[V23] && dctx && !dctx->ending;

        if (vec(0x23) == arpl[V23] && !up) {    /* (no handler of the program's: DOS's own ends it) */
            ctrl_c_end(tf, &r);
            return;
        }
        c23 = r;                                /* its INT 23h, the call's registers, back by an ARPL of GLOS's */
        c23_at = at, c23_via = via_arpl;
        from_regs(tf, &r);
        /* A DPMI program's, its real-mode vector still DOS's: INT 23h goes up to its protected-mode handler, or is
           ignored where it has none, as for a DOS in the VM (v86.c's INT trap: deliver.c's dpmi_passup); either way
           the call is then made again (VC23), as DOS does. Ended by DOS's own handler here, a DJGPP program lost
           the SIGINT it raises once the call is back (APITEST's job-ctrl-c natively: code 0, not 7; 2026-11-24).
           Nothing of GLOS's is on the program's stack, so VC23's look for a RETF with CF set never matches. */
        if (up) {
            c23_sp = (tf->esp + 4) & 0xFFFF;
            tf->cs = arpl[VC23] >> 16;
            if (dpmi_passup(tf, 0x23, arpl[VC23] & 0xFFFF))
                return;
            ctrl_c_end(tf, &r);                 /* (not taken: as before; from_regs puts its CS:IP back) */
            return;
        }
        c23_sp = tf->esp & 0xFFFF;
        push16(tf, tf->eflags & 0xFFFF);
        push16(tf, arpl[VC23] >> 16);
        push16(tf, arpl[VC23] & 0xFFFF);
        tf->cs = vec(0x23) >> 16;
        tf->eip = vec(0x23) & 0xFFFF;
        tf->eflags &= ~0x0300u;                 /* (IF and TF, as an interrupt clears them) */
        return;
    }
    if (res == DOS21_END)
        kprintf("GLOS-NATIVE dos end-of-root ax=%04x psp=%04x\n", r.ax, dos_cur_psp(&vd));     /* (the stub never ends) */
    if (res == DOS21_UNANSWERED && unanswered++ < 40)
        kprintf("GLOS-NATIVE unanswered int=%02x ax=%04x psp=%04x\n", n, tf->eax & 0xFFFF, dos_cur_psp(&vd));
    from_regs(tf, &r);
}

/* A #UD at one of DOS's ARPLs (its INT frame on the stack, but VC23's: a RETF or an IRET to it). 1 if it was. */
int vdos_arpl(struct trapframe *tf)
{
    u32 at = (tf->cs & 0xFFFF) << 16 | (tf->eip & 0xFFFF), lin, ip, cs, f;
    struct dos_regs86 r;
    int i;
    char c;

    if (!ready)
        return 0;
    for (i = 0; i < NV && arpl[i] != at; i++)
        ;
    if (i == NV)
        return 0;
    lin = (tf->ss & 0xFFFF) * 16 + (tf->esp & 0xFFFF);
    if (i == VC23) {                            /* back from the program's INT 23h */
        if ((tf->esp & 0xFFFF) == ((c23_sp - 2) & 0xFFFF) && (vm_rd16(lin) & 1)) {
            LO16(tf->esp, (tf->esp + 2) & 0xFFFF);      /* (RETF with CF set: the program ends) */
            to_regs(tf, &r, tf->eip);
            ctrl_c_end(tf, &r);
            return 1;
        }
        r = c23;                                /* (IRET, or RETF with CF clear: the call again) */
        from_regs(tf, &r);
        vdos_int(tf, 0x21, r.ip, c23_at, c23_via);
        return 1;
    }
    ip = vm_rd16(lin), cs = vm_rd16(lin + 2), f = vm_rd16(lin + 4);
    LO16(tf->esp, (tf->esp + 6) & 0xFFFF);
    tf->cs = cs;
    tf->eip = ip;
    tf->eflags = (tf->eflags & ~0x0FD5u) | (f & 0x0FD5u);
    vm.thread->vif = (f >> 9) & 1;              /* (the IRET's: the caller's interrupt flag, as the frame has it;
                                                   left off until 2026-11-24 for a call chained to by a far jump) */
    switch (i) {
    case V21:
    case V20:
        vdos_int(tf, i == V21 ? 0x21 : 0x20, ip, ip, 1);
        break;
    case V29:                                   /* DOS's fast console output */
        c = (char)(tf->eax & 0xFF);
        vcon_tty(&c, 1);
        break;
    case V23:                                   /* DOS's own Ctrl-C handler, called or chained to: the end */
        to_regs(tf, &r, ip);
        ctrl_c_end(tf, &r);
        break;
    }
    return 1;
}
