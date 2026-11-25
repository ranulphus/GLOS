/* GLOS's own CD-ROM drives (docs/v1/native-dos.md, N4's CD-ROM, step C2): the ATAPI devices ide.c found, their
 * discs' 2048-byte sectors by READ(10) packets (ata.c's ata_packet), and each disc's ISO 9660 volume
 * (kernel/fs/iso.c) as the native DOS's CD drives ask for it (vdos.c's cdmedia).
 *
 * A disc changed (UNIT ATTENTION) is mounted afresh; none in (NOT READY), or one that isn't ISO 9660 (an audio
 * CD), is "not ready" to DOS. Whether a disc is there is asked (TEST UNIT READY) at most once a second while one
 * is mounted, so a program's reads don't each cost a packet more. Native only: a hosted GLOS leaves the drive to
 * DOS's own driver and MSCDEX. */
#include "cdrom.h"
#include "ide.h"
#include "kprintf.h"
#include "timer.h"

#define CD_MAX 4

static struct cd {
    struct ata_dev *d;
    int mounted;
    u32 checked_at;
    int changed;                        /* mounted afresh since MSCDEX's "media changed" was last asked */
    struct iso_vol v;
} cds[CD_MAX];
static int ncd;

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

static int dev_read(void *ctx, unsigned long lba, unsigned count, unsigned char *buf)
{
    return read10(ctx, lba, count, buf);
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
    if (!c)
        return 0;
    *err = 0x15;
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
    return read10(&cds[i], lba, count, buf);
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
    if (i < 0 || i >= ncd || packet(&cds[i], cap, b, 8, &got) != 0 || got != 8)
        return -1;
    *sectors = ((unsigned long)b[0] << 24 | (unsigned long)b[1] << 16 | (unsigned long)b[2] << 8 | b[3]) + 1;
    return 0;
}

/* ---- audio and the disc's layout (MSCDEX's device requests: N4's C5, C6) */

int cdrom_toc(int i, unsigned char *buf, unsigned len, int msf)
{
    unsigned char cdb[12] = { 0x43 };
    unsigned long got;
    if (i < 0 || i >= ncd)
        return -1;
    cdb[1] = msf ? 0x02 : 0;
    cdb[7] = (unsigned char)(len >> 8), cdb[8] = (unsigned char)len;
    return packet(&cds[i], cdb, buf, len, &got) == 0 ? (int)got : -1;
}

int cdrom_play(int i, unsigned long lba, unsigned long count)
{
    unsigned char cdb[12] = { 0xA5 };           /* PLAY AUDIO(12) */
    if (i < 0 || i >= ncd)
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
    return i >= 0 && i < ncd ? packet(&cds[i], cdb, 0, 0, 0) : -1;
}

int cdrom_pause(int i, int resume)
{
    unsigned char cdb[12] = { 0x4B };           /* PAUSE/RESUME */
    if (i < 0 || i >= ncd)
        return -1;
    cdb[8] = resume ? 1 : 0;
    return packet(&cds[i], cdb, 0, 0, 0);
}

int cdrom_subq(int i, unsigned char *buf16)
{
    unsigned char cdb[12] = { 0x42, 0x02, 0x40, 0x01, 0, 0, 0, 0, 16 };   /* READ SUB-CHANNEL: MSF, Q, position */
    unsigned long got;
    if (i < 0 || i >= ncd)
        return -1;
    return packet(&cds[i], cdb, buf16, 16, &got) == 0 && got >= 16 ? 0 : -1;
}

int cdrom_tray(int i, int open)
{
    unsigned char cdb[12] = { 0x1B };           /* START STOP UNIT: 2 eject, 3 load */
    if (i < 0 || i >= ncd)
        return -1;
    cdb[4] = open ? 2 : 3;
    cds[i].mounted = 0;
    return packet(&cds[i], cdb, 0, 0, 0);
}

