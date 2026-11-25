/* GLOS's own CD-ROM drives (docs/v1/native-dos.md, N4's CD-ROM, step C2): the ATAPI devices ide.c found, their
 * discs' 2048-byte sectors by READ(10) packets (ata.c's ata_packet), and each disc's ISO 9660 volume
 * (kernel/fs/iso.c) as the native DOS's CD drives ask for it (vdos.c's cdmedia).
 *
 * A disc changed (UNIT ATTENTION) is mounted afresh; none in (NOT READY), or one that isn't ISO 9660 (an audio
 * CD), is "not ready" to DOS. Whether a disc is there is asked (TEST UNIT READY) at most once a second while one
 * is mounted, so a program's reads don't each cost a packet more. Native only: a hosted GLOS leaves the drive to
 * DOS's own driver and MSCDEX.
 *
 * Images as drives (step C7): an ISO 9660 image, or a CUE sheet's first data track (MODE1/2048, or MODE1/2352
 * whose sectors' user data is read past their 16-byte headers), on one of GLOS's FAT volumes, mounted as a unit
 * of its own after the drives (cdrom_mount_image): read through kernel/fs/fsv.c, whose lock per volume lets the
 * VM's DOS calls and the command that mounted it share the file. An image has no audio yet (a CUE sheet's audio
 * tracks wait for the digital way, C6) and no tray: "eject" takes it away. */
#include <string.h>

#include "cdrom.h"
#include "mm.h"
#include "ide.h"
#include "kprintf.h"
#include "timer.h"

#define CD_MAX 4

static struct cd {
    struct ata_dev *d;                  /* a drive's; 0: an image's, or a free slot */
    struct fsv_file *img;               /* an image's file */
    unsigned long img_off, img_bps;     /* where its data track begins, its sectors' bytes (2048, or 2352 raw) */
    char img_name[64];
    int mounted;
    u32 checked_at;
    int changed;                        /* mounted afresh since MSCDEX's "media changed" was last asked */
    struct iso_vol v;
} cds[CD_MAX];
static int ncd;                         /* the drives (first) and the images: slots 0 to ncd - 1 in use or free */

/* A packet with its sense key (0: none; -1: the command failed otherwise). */
static int packet(struct cd *c, const unsigned char cdb[12], void *buf, unsigned long len, unsigned long *got)
{
    int r = ata_packet(c->d, cdb, buf, len, got);
    if (r == ATA_OK)
        return 0;
    return r == ATA_EDEVICE ? (c->d->err >> 4) & 15 : -1;
}

static int read10(struct cd *c, unsigned long lba, unsigned count, unsigned char *buf)
{
    unsigned char cdb[12] = { 0x28 };
    unsigned long got;
    int k, tries;

    cdb[2] = (unsigned char)(lba >> 24), cdb[3] = (unsigned char)(lba >> 16);
    cdb[4] = (unsigned char)(lba >> 8), cdb[5] = (unsigned char)lba;
    cdb[7] = (unsigned char)(count >> 8), cdb[8] = (unsigned char)count;
    for (tries = 0; tries < 3; tries++) {
        k = packet(c, cdb, buf, (unsigned long)count * ISO_SECTOR, &got);
        if (!k && got == (unsigned long)count * ISO_SECTOR)
            return 0;
        if (k == 6)
            c->mounted = 0;                     /* (the disc changed under the read: what's read now is the new one's) */
        else if (k != 0 && k != 3 && k != 4)   /* (MEDIUM and HARDWARE ERROR are worth another try; the rest not) */
            break;
    }
    return -1;
}

static int img_read(struct cd *c, unsigned long lba, unsigned count, unsigned char *buf)
{
    unsigned k;
    if (c->img_bps == ISO_SECTOR) {
        if (fsv_seek(c->img, c->img_off + lba * ISO_SECTOR) != 0)
            return -1;
        return fsv_read(c->img, buf, (unsigned long)count * ISO_SECTOR) == (long)count * ISO_SECTOR ? 0 : -1;
    }
    for (k = 0; k < count; k++)                 /* (raw sectors: the user data past the sync and header) */
        if (fsv_seek(c->img, c->img_off + (lba + k) * c->img_bps + 16) != 0
            || fsv_read(c->img, buf + (unsigned long)k * ISO_SECTOR, ISO_SECTOR) != ISO_SECTOR)
            return -1;
    return 0;
}

static int dev_read(void *ctx, unsigned long lba, unsigned count, unsigned char *buf)
{
    struct cd *c = ctx;
    return c->img ? img_read(c, lba, count, buf) : read10(c, lba, count, buf);
}

int cdrom_find(void)
{
    struct ata_dev *d;
    int k;

    ncd = 0;
    for (k = 0; ncd < CD_MAX && (d = ide_dev(k)) != 0; k++)
        if (d->present && d->atapi && (d->id[0] & 0x1F00) == 0x0500) {     /* (its device type: a CD-ROM's 05h) */
            cds[ncd].d = d;
            cds[ncd].mounted = 0;
            kprintf("GLOS-CD drive=%d dev=%d model=\"%s\"\n", ncd, k, d->model);
            ncd++;
        }
    return ncd;
}

int cdrom_count(void) { return ncd; }

struct iso_vol *cdrom_media(int i, unsigned *err)
{
    static const unsigned char tur[12] = { 0x00 };
    struct cd *c = i >= 0 && i < ncd ? &cds[i] : 0;
    struct iso_dev dev;
    int k, tries, r;

    *err = 0x0F;
    if (!c || (!c->d && !c->img))
        return 0;
    *err = 0x15;
    if (c->img) {                               /* (an image: mounted once, there till it is taken away) */
        if (!c->mounted) {
            dev.read = dev_read, dev.ctx = c;
            r = iso_mount(&c->v, &dev);
            kprintf("GLOS-CD image=%d mount=%d label=\"%s\"\n", i, r, r == ISO_OK ? c->v.label : "");
            if (r != ISO_OK)
                return 0;
            c->mounted = 1, c->changed = 1;
        }
        return &c->v;
    }
    if (c->mounted && timer_ticks() - c->checked_at < 1024)
        return &c->v;
    for (tries = 0, k = -1; tries < 3; tries++) {
        k = packet(c, tur, 0, 0, 0);
        if (k != 6)
            break;
        c->mounted = 0;                         /* (UNIT ATTENTION: a disc put in, or another) */
    }
    if (k != 0) {
        if (c->mounted)
            kprintf("GLOS-CD drive=%d no-disc sense=%d\n", i, k);
        c->mounted = 0;
        return 0;
    }
    c->checked_at = timer_ticks();
    if (c->mounted)
        return &c->v;
    dev.read = dev_read, dev.ctx = c;
    r = iso_mount(&c->v, &dev);
    kprintf("GLOS-CD drive=%d mount=%d label=\"%s\" sectors=%u\n", i, r, r == ISO_OK ? c->v.label : "",
            r == ISO_OK ? (u32)c->v.sectors : 0);
    if (r != ISO_OK)
        return 0;
    c->mounted = 1;
    c->changed = 1;
    return &c->v;
}

int cdrom_read(int i, unsigned long lba, unsigned count, void *buf)
{
    unsigned err;
    if (i < 0 || i >= ncd)
        return -1;
    if (!cds[i].mounted && !cdrom_media(i, &err))
        return -1;
    return dev_read(&cds[i], lba, count, buf);
}

int cdrom_changed(int i)
{
    int r;
    if (i < 0 || i >= ncd)
        return 0;
    r = cds[i].changed;
    cds[i].changed = 0;
    return r;
}

int cdrom_capacity(int i, unsigned long *sectors)
{
    static const unsigned char cap[12] = { 0x25 };
    unsigned char b[8];
    unsigned long got;
    if (i >= 0 && i < ncd && cds[i].img) {
        *sectors = (fsv_size(cds[i].img) - cds[i].img_off) / cds[i].img_bps;
        return 0;
    }
    if (i < 0 || i >= ncd || !cds[i].d || packet(&cds[i], cap, b, 8, &got) != 0 || got != 8)
        return -1;
    *sectors = ((unsigned long)b[0] << 24 | (unsigned long)b[1] << 16 | (unsigned long)b[2] << 8 | b[3]) + 1;
    return 0;
}

/* ---- audio and the disc's layout (MSCDEX's device requests: N4's C5, C6) */

int cdrom_toc(int i, unsigned char *buf, unsigned len, int msf)
{
    unsigned char cdb[12] = { 0x43 };
    unsigned long got;
    if (i >= 0 && i < ncd && cds[i].img) {      /* (an image: one data track, the lead-out at its end) */
        unsigned char t[20];
        unsigned long end = 0, k;
        cdrom_capacity(i, &end);
        memset(t, 0, sizeof t);
        t[1] = 18, t[2] = 1, t[3] = 1;
        t[5] = 0x14, t[6] = 1, t[13] = 0x14, t[14] = 0xAA;
        if (msf) {
            t[10] = 2;                          /* (track 1 at 00:02:00) */
            k = end + 150;
            t[17] = (unsigned char)(k / 4500), t[18] = (unsigned char)(k / 75 % 60), t[19] = (unsigned char)(k % 75);
        } else {
            t[16] = (unsigned char)(end >> 24), t[17] = (unsigned char)(end >> 16);
            t[18] = (unsigned char)(end >> 8), t[19] = (unsigned char)end;
        }
        if (len > sizeof t)
            len = sizeof t;
        memcpy(buf, t, len);
        return (int)len;
    }
    if (i < 0 || i >= ncd || !cds[i].d)
        return -1;
    cdb[1] = msf ? 0x02 : 0;
    cdb[7] = (unsigned char)(len >> 8), cdb[8] = (unsigned char)len;
    return packet(&cds[i], cdb, buf, len, &got) == 0 ? (int)got : -1;
}

int cdrom_play(int i, unsigned long lba, unsigned long count)
{
    unsigned char cdb[12] = { 0xA5 };           /* PLAY AUDIO(12) */
    if (i < 0 || i >= ncd || !cds[i].d)
        return -1;
    cdb[2] = (unsigned char)(lba >> 24), cdb[3] = (unsigned char)(lba >> 16);
    cdb[4] = (unsigned char)(lba >> 8), cdb[5] = (unsigned char)lba;
    cdb[6] = (unsigned char)(count >> 24), cdb[7] = (unsigned char)(count >> 16);
    cdb[8] = (unsigned char)(count >> 8), cdb[9] = (unsigned char)count;
    return packet(&cds[i], cdb, 0, 0, 0);
}

int cdrom_stop(int i)
{
    static const unsigned char cdb[12] = { 0x4E };     /* STOP PLAY/SCAN */
    return i >= 0 && i < ncd && cds[i].d ? packet(&cds[i], cdb, 0, 0, 0) : -1;
}

int cdrom_pause(int i, int resume)
{
    unsigned char cdb[12] = { 0x4B };           /* PAUSE/RESUME */
    if (i < 0 || i >= ncd || !cds[i].d)
        return -1;
    cdb[8] = resume ? 1 : 0;
    return packet(&cds[i], cdb, 0, 0, 0);
}

int cdrom_subq(int i, unsigned char *buf16)
{
    unsigned char cdb[12] = { 0x42, 0x02, 0x40, 0x01, 0, 0, 0, 0, 16 };   /* READ SUB-CHANNEL: MSF, Q, position */
    unsigned long got;
    if (i < 0 || i >= ncd || !cds[i].d)
        return -1;
    return packet(&cds[i], cdb, buf16, 16, &got) == 0 && got >= 16 ? 0 : -1;
}

int cdrom_tray(int i, int open)
{
    unsigned char cdb[12] = { 0x1B };           /* START STOP UNIT: 2 eject, 3 load */
    if (i < 0 || i >= ncd || !cds[i].d)
        return -1;
    cdb[4] = open ? 2 : 3;
    cds[i].mounted = 0;
    return packet(&cds[i], cdb, 0, 0, 0);
}

/* ---- images as drives (C7) */

int cdrom_is_image(int i) { return i >= 0 && i < ncd && cds[i].img != 0; }

const char *cdrom_image_name(int i) { return cdrom_is_image(i) ? cds[i].img_name : ""; }

/* A CUE sheet's first track, if it is data: its file's name (beside the sheet), and its sectors' bytes. 0, or -1. */
static int cue_data(struct fsv_file *f, char *file, unsigned room, unsigned long *bps)
{
    static char t[4096];
    long n = fsv_read(f, t, sizeof t - 1);
    char *p, *q;
    unsigned k;

    if (n <= 0)
        return -1;
    t[n] = 0;
    if (!(p = strstr(t, "FILE")) || !(p = strchr(p, '"')) || !(q = strchr(p + 1, '"')) || (unsigned)(q - p - 1) >= room)
        return -1;
    memcpy(file, p + 1, (unsigned)(q - p - 1));
    file[q - p - 1] = 0;
    if (!(p = strstr(q, "TRACK")))
        return -1;
    for (k = 0; p[k] && p[k] != '\n'; k++)
        ;
    if (strstr(p, "MODE1/2048") && strstr(p, "MODE1/2048") < p + k)
        *bps = 2048;
    else if (strstr(p, "MODE1/2352") && strstr(p, "MODE1/2352") < p + k)
        *bps = 2352;
    else
        return -1;                              /* (an audio CD's sheet, or a mode GLOS doesn't read) */
    return 0;
}

int cdrom_mount_image(struct fsv_vol *v, const char *path, const char *shown)
{
    struct fsv_file *f;
    unsigned long bps = ISO_SECTOR;
    int i, n = (int)strlen(path);
    char file[128];

    for (i = 0; i < CD_MAX && (cds[i].d || cds[i].img); i++)
        ;
    if (i == CD_MAX)
        return -2;                              /* (no unit free) */
    if (!(f = kmalloc(sizeof *f)))
        return -1;
    if (fsv_open(v, path, FSV_READ, FSV_DENY_WRITE, f) != 0) {
        kfree(f);
        return -3;
    }
    if (n > 4 && (path[n - 4] == '.') && ((path[n - 3] | 32) == 'c') && ((path[n - 2] | 32) == 'u') && ((path[n - 1] | 32) == 'e')) {
        char dir[128];
        const char *slash = strrchr(path, '\\');
        unsigned dl = slash ? (unsigned)(slash - path + 1) : 0;
        if (cue_data(f, file, sizeof file - dl, &bps) != 0 || dl + strlen(file) >= sizeof dir) {
            fsv_close(f);
            kfree(f);
            return -4;
        }
        fsv_close(f);
        memcpy(dir, path, dl);
        strcpy(dir + dl, file);
        if (fsv_open(v, dir, FSV_READ, FSV_DENY_WRITE, f) != 0) {
            kfree(f);
            return -3;
        }
    }
    memset(&cds[i], 0, sizeof cds[i]);
    cds[i].img = f, cds[i].img_bps = bps, cds[i].img_off = 0;
    strncpy(cds[i].img_name, shown, sizeof cds[i].img_name - 1);
    if (i >= ncd)
        ncd = i + 1;
    kprintf("GLOS-CD image=%d file=%s bps=%u\n", i, shown, (u32)bps);
    return i;
}

void cdrom_unmount_image(int i)
{
    if (!cdrom_is_image(i))
        return;
    fsv_close(cds[i].img);
    kfree(cds[i].img);
    memset(&cds[i], 0, sizeof cds[i]);
    while (ncd && !cds[ncd - 1].d && !cds[ncd - 1].img)
        ncd--;
}

struct iso_vol *cdrom_vol(int i) { return i >= 0 && i < ncd ? &cds[i].v : 0; }
