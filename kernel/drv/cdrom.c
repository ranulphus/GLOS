/* GLOS's own CD-ROM drives (docs/v1/native-dos.md, N4's CD-ROM, step C2): the ATAPI devices ide.c found, their
 * discs' 2048-byte sectors by READ(10) packets (ata.c's ata_packet), and each disc's ISO 9660 volume
 * (kernel/fs/iso.c) as the native DOS's CD drives ask for it (vdos.c's cdmedia).
 *
 * A disc changed (UNIT ATTENTION) is mounted afresh; none in (NOT READY), or one that isn't ISO 9660 (an audio
 * CD), is "not ready" to DOS. Whether a disc is there is asked (TEST UNIT READY) at most once a second while one
 * is mounted, so a program's reads don't each cost a packet more. Native only: a hosted GLOS leaves the drive to
 * DOS's own driver and MSCDEX.
 *
 * Images as drives (step C7): an ISO 9660 image, or a CUE sheet's disc (cue.c: its first track, a data one,
 * MODE1/2048 or MODE1/2352 whose sectors' user data is read past their 16-byte headers, the volume DOS reads), on
 * one of GLOS's FAT volumes, mounted as a unit of its own after the drives (cdrom_mount_image): read through
 * kernel/fs/fsv.c, whose lock per volume lets the VM's DOS calls and the command that mounted it share the file.
 * A sheet's audio tracks play the digital way (C6), from their files through a handle of the reader's own. An
 * image has no tray: "eject" takes it away. */
#include <string.h>

#include "cdrom.h"
#include "mm.h"
#include "ide.h"
#include "kprintf.h"
#include "mixer.h"
#include "sched.h"
#include "session.h"
#include "timer.h"
#include "io.h"
#include "cdda.h"
#include "cue.h"

#define CD_MAX 4

static struct cd {
    struct ata_dev *d;                  /* a drive's; 0: an image's, or a free slot */
    struct fsv_file *img;               /* an image's file */
    unsigned long img_off, img_bps;     /* where its data track begins, its sectors' bytes (2048, or 2352 raw) */
    char img_name[64];
    struct cue *cue;                    /* an image's CUE sheet laid out (0: an ISO image, one data track) */
    struct fsv_vol *vol;                /* the volume an image's files are on */
    char dir[128];                      /* the sheet's folder, which its FILEs are named from */
    struct fsv_file *afile;             /* the file the digital reader has open (audio, a handle of its own) */
    int afile_no;                       /* ... which of the sheet's FILEs (-1: none) */
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
    if (i >= 0 && i < ncd && cds[i].img && cds[i].cue) {   /* (a CUE sheet's image: its every track) */
        const struct cue *cu = cds[i].cue;
        unsigned char t[4 + 8 * (CUE_TRACKS + 1)];
        unsigned n = 4, k;
        memset(t, 0, sizeof t);
        t[2] = (unsigned char)cu->t[0].number, t[3] = (unsigned char)cu->t[cu->ntracks - 1].number;
        for (k = 0; k <= (unsigned)cu->ntracks; k++, n += 8) {
            unsigned long at = k < (unsigned)cu->ntracks ? cu->t[k].start : cu->leadout;
            t[n + 1] = k < (unsigned)cu->ntracks && cu->t[k].type == CUE_AUDIO ? 0x10 : 0x14;
            t[n + 2] = k < (unsigned)cu->ntracks ? (unsigned char)cu->t[k].number : 0xAA;
            if (msf) {
                at += 150;
                t[n + 5] = (unsigned char)(at / 4500), t[n + 6] = (unsigned char)(at / 75 % 60);
                t[n + 7] = (unsigned char)(at % 75);
            } else {
                t[n + 4] = (unsigned char)(at >> 24), t[n + 5] = (unsigned char)(at >> 16);
                t[n + 6] = (unsigned char)(at >> 8), t[n + 7] = (unsigned char)at;
            }
        }
        t[0] = (unsigned char)((n - 2) >> 8), t[1] = (unsigned char)(n - 2);
        if (len > n)
            len = n;
        memcpy(buf, t, len);
        return (int)len;
    }
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

/* ---- audio played digitally (C6's digital way): READ CD's audio sectors into cdda.c's ring by a thread of its
   own, the mixer playing them from the card's interrupt (cdaudio_mix). For a drive whose MODE SENSE page 2Ah
   says it reads CD-DA, while the mixer has a card and no session drives the card itself (direct mode); else the
   drive's own output, as before. One play at a time, one drive's. The reader's runs are short (CDA_RUN) because
   a DOS call of the VM's waits for the packet under way (ata.c's hold); cdda_want, cdda_commit and the controls
   are made with interrupts off against each other and the mixer. */
#define CDA_RUN 4                       /* sectors a packet (53 ms of audio) */
#define CDA_SECTORS 76                  /* the ring: a second, in whole runs (so its end never cuts one) */

static struct cdda cda;
static volatile int cda_unit = -1;      /* the unit cda's play is (-1: none) */
static unsigned char *cda_ring;
static struct waitq cda_wq;
static struct thread *cda_thread;
static signed char cda_can[CD_MAX];     /* 0: not asked yet; 1: the drive reads CD-DA; -1: it doesn't */
static unsigned char cda_toc[804];      /* the disc's tracks, by LBA, taken at the play's start */
static int cda_toc_len;
static u32 cda_fails;
static volatile int cda_busy = -1;      /* the unit the reader is reading now (-1: none) */

/* An image's audio sectors from its tracks' files (silence where the disc has none: gaps, a data track). */
static int img_cdda(struct cd *c, unsigned long lba, unsigned long n, unsigned char *buf)
{
    unsigned long k;
    for (k = 0; k < n; k++, buf += CDDA_SECTOR) {
        long fs;
        int tr = c->cue ? cue_find(c->cue, lba + k, &fs) : -1;
        if (tr < 0 || fs < 0 || c->cue->t[tr].type != CUE_AUDIO) {
            memset(buf, 0, CDDA_SECTOR);
            continue;
        }
        if (c->afile_no != c->cue->t[tr].file) {
            char path[192];
            if (c->afile_no >= 0)
                fsv_close(c->afile);
            c->afile_no = -1;
            ksnprintf(path, sizeof path, "%s%s", c->dir, c->cue->file[c->cue->t[tr].file]);
            if (fsv_open(c->vol, path, FSV_READ, FSV_DENY_WRITE, c->afile) != 0)
                return -1;
            c->afile_no = c->cue->t[tr].file;
        }
        if (fsv_seek(c->afile, (unsigned long)fs * CDDA_SECTOR) != 0
            || fsv_read(c->afile, buf, CDDA_SECTOR) != (long)CDDA_SECTOR)
            memset(buf, 0, CDDA_SECTOR);        /* (past its file's end: silence) */
    }
    return 0;
}

static int read_cdda(struct cd *c, unsigned long lba, unsigned long n, unsigned char *buf)
{
    unsigned char cdb[12] = { 0xBE, 0x04 };     /* READ CD: sectors of CD-DA */
    unsigned long got;
    cdb[2] = (unsigned char)(lba >> 24), cdb[3] = (unsigned char)(lba >> 16);
    cdb[4] = (unsigned char)(lba >> 8), cdb[5] = (unsigned char)lba;
    cdb[6] = (unsigned char)(n >> 16), cdb[7] = (unsigned char)(n >> 8), cdb[8] = (unsigned char)n;
    cdb[9] = 0x10;                              /* (the user data: all of a CD-DA sector's 2,352 bytes) */
    return packet(c, cdb, buf, n * CDDA_SECTOR, &got) == 0 && got == n * CDDA_SECTOR ? 0 : -1;
}

/* Whether drive i reads CD-DA (MODE SENSE(10), no block descriptors, page 2Ah: its byte 5 bit 0), asked once. */
static int reads_cdda(int i)
{
    if (!cda_can[i]) {
        unsigned char cdb[12] = { 0x5A, 0x08, 0x2A, 0, 0, 0, 0, 0, 40 }, b[40];
        unsigned long got = 0;
        unsigned bd;
        memset(b, 0, sizeof b);
        cda_can[i] = -1;
        if (packet(&cds[i], cdb, b, sizeof b, &got) == 0 && got >= 8) {
            bd = (unsigned)b[6] << 8 | b[7];
            if (bd + 14 <= got && (b[8 + bd] & 0x3F) == 0x2A && (b[8 + bd + 5] & 1))
                cda_can[i] = 1;
        }
        kprintf("GLOS-CDAUDIO drive=%d cdda=%d\n", i, cda_can[i] > 0);
    }
    return cda_can[i] > 0;
}

static void cda_reader(void *arg)
{
    (void)arg;
    for (;;) {
        unsigned long lba = 0, gen = 0, n = 0;
        unsigned char *into = 0;
        int u = cda_unit, k;
        u32 f = irq_save();
        if (u >= 0) {
            n = cdda_want(&cda, CDA_RUN, &lba, &gen, &into);
            if (n && n < CDA_RUN && cda.count - cda.done > n)
                n = 0;                          /* (room for a whole run first, but for the play's last sectors) */
        }
        irq_restore(f);
        if (!n) {
            thread_wait(&cda_wq, timer_ticks() + 16);       /* (the ring full, or nothing playing) */
            continue;
        }
        cda_busy = u;
        f = irq_save();
        if (u != cda_unit) {                    /* (the play went while it asked: an image's eject waits for this) */
            irq_restore(f);
            cda_busy = -1;
            continue;
        }
        irq_restore(f);
        k = cds[u].d ? read_cdda(&cds[u], lba, n, into) : img_cdda(&cds[u], lba, n, into);
        cda_busy = -1;
        if (k != 0) {
            if (cda_fails++ < 4)
                kprintf("GLOS-CDAUDIO read failed drive=%d lba=%lu\n", u, lba);
            f = irq_save();
            if (gen == cda.gen)
                cdda_stop(&cda);                /* (the play ends there: a damaged disc, or none) */
            irq_restore(f);
            continue;
        }
        f = irq_save();
        cdda_commit(&cda, gen, n);
        irq_restore(f);
    }
}

static int digital(int i)
{
    if (i < 0 || i >= ncd || !mixer_found() || session_direct())
        return 0;
    if (cds[i].img ? !cds[i].cue || !cds[i].afile : !cds[i].d || !reads_cdda(i))
        return 0;
    if (!cda_ring) {
        if (!(cda_ring = kmalloc(CDA_SECTORS * CDDA_SECTOR)))
            return 0;
        cdda_init(&cda, cda_ring, CDA_SECTORS * CDDA_SECTOR);
    }
    if (!cda_thread && !(cda_thread = thread_create("cdaudio", PRIO_URGENT, cda_reader, 0)))
        return 0;
    return 1;
}

/* The mixer's (mixer.c's fill, the card's interrupt); and whether it is to play now (card_want). */
void cdaudio_mix(int *acc, unsigned long frames, unsigned long rate)
{
    if (cda_unit >= 0)
        cdda_mix(&cda, acc, frames, rate);
}

int cdaudio_on(void) { return cda_unit >= 0 && cdda_state(&cda) == CDDA_PLAYING; }

static void msf(unsigned long f, unsigned char *p)
{
    p[0] = (unsigned char)(f / 4500), p[1] = (unsigned char)(f / 75 % 60), p[2] = (unsigned char)(f % 75);
}

int cdrom_play(int i, unsigned long lba, unsigned long count)
{
    unsigned char cdb[12] = { 0xA5 };           /* PLAY AUDIO(12) */
    if (i < 0 || i >= ncd || (!cds[i].d && !cds[i].img))
        return -1;
    if (digital(i)) {
        int len = cdrom_toc(i, cda_toc, sizeof cda_toc, 0);
        u32 f;
        cda_toc_len = len > 0 ? len : 0;
        f = irq_save();
        cda_unit = i;
        cdda_play(&cda, lba, count);
        thread_wake(&cda_wq);
        irq_restore(f);
        mixer_refresh();
        kprintf("GLOS-CDAUDIO play digital drive=%d lba=%lu sectors=%lu\n", i, lba, count);
        return 0;
    }
    if (!cds[i].d)
        return -1;                              /* (an image plays digitally or not at all) */
    cdb[2] = (unsigned char)(lba >> 24), cdb[3] = (unsigned char)(lba >> 16);
    cdb[4] = (unsigned char)(lba >> 8), cdb[5] = (unsigned char)lba;
    cdb[6] = (unsigned char)(count >> 24), cdb[7] = (unsigned char)(count >> 16);
    cdb[8] = (unsigned char)(count >> 8), cdb[9] = (unsigned char)count;
    return packet(&cds[i], cdb, 0, 0, 0);
}

int cdrom_stop(int i)
{
    static const unsigned char cdb[12] = { 0x4E };     /* STOP PLAY/SCAN */
    if (i >= 0 && i == cda_unit) {
        u32 f = irq_save();
        cdda_stop(&cda);
        irq_restore(f);
        mixer_refresh();
        return 0;
    }
    return i >= 0 && i < ncd && cds[i].d ? packet(&cds[i], cdb, 0, 0, 0) : -1;
}

int cdrom_pause(int i, int resume)
{
    unsigned char cdb[12] = { 0x4B };           /* PAUSE/RESUME */
    if (i < 0 || i >= ncd || (!cds[i].d && !cds[i].img))
        return -1;
    if (i == cda_unit && cdda_state(&cda) != CDDA_IDLE) {
        u32 f = irq_save();
        cdda_pause(&cda, resume);
        irq_restore(f);
        mixer_refresh();
        return 0;
    }
    if (!cds[i].d)
        return -1;
    cdb[8] = resume ? 1 : 0;
    return packet(&cds[i], cdb, 0, 0, 0);
}

int cdrom_subq(int i, unsigned char *buf16)
{
    unsigned char cdb[12] = { 0x42, 0x02, 0x40, 0x01, 0, 0, 0, 0, 16 };   /* READ SUB-CHANNEL: MSF, Q, position */
    unsigned long got;
    if (i < 0 || i >= ncd || (!cds[i].d && !cds[i].img))
        return -1;
    if (i == cda_unit && cdda_state(&cda) != CDDA_IDLE) {
        /* (as the drive says it: the audio status, then the track that holds the position, its index 1, the
           position on the disc and in the track) */
        static const unsigned char status[] = { 0x15, 0x11, 0x12, 0x13 };      /* (IDLE, PLAYING, PAUSED, DONE) */
        unsigned long at = cdda_position(&cda, 0), start = 0;
        unsigned char control = 0x10, track = 1;
        int k;
        for (k = 4; k + 8 <= cda_toc_len; k += 8) {
            unsigned long s = (unsigned long)cda_toc[k + 4] << 24 | (unsigned long)cda_toc[k + 5] << 16
                | (unsigned long)cda_toc[k + 6] << 8 | cda_toc[k + 7];
            if (cda_toc[k + 2] != 0xAA && s <= at)
                track = cda_toc[k + 2], start = s, control = cda_toc[k + 1];
        }
        memset(buf16, 0, 16);
        buf16[1] = status[cdda_state(&cda) & 3];
        buf16[3] = 12, buf16[4] = 1, buf16[5] = control, buf16[6] = track, buf16[7] = 1;
        msf(at + 150, buf16 + 9);
        msf(at - start, buf16 + 13);
        return 0;
    }
    if (!cds[i].d) {                            /* (an image not playing: no status) */
        memset(buf16, 0, 16);
        buf16[1] = 0x15, buf16[3] = 12, buf16[4] = 1;
        return 0;
    }
    return packet(&cds[i], cdb, buf16, 16, &got) == 0 && got >= 16 ? 0 : -1;
}

/* MSCDEX's audio channel control (IOCTL output 3): each output's volume, 0 to 255. A digital play's gain; the
   drive's own output keeps its own (as before). */
void cdrom_volume(int i, unsigned left, unsigned right)
{
    u32 f;
    if (i < 0 || i >= ncd || (!cds[i].d && !cds[i].img) || !cda_ring)
        return;
    f = irq_save();
    cdda_volume(&cda, left + (left >> 7), right + (right >> 7));
    irq_restore(f);
}

int cdrom_tray(int i, int open)
{
    unsigned char cdb[12] = { 0x1B };           /* START STOP UNIT: 2 eject, 3 load */
    if (i < 0 || i >= ncd || !cds[i].d)
        return -1;
    if (i == cda_unit)
        cdrom_stop(i);
    cdb[4] = open ? 2 : 3;
    cds[i].mounted = 0;
    return packet(&cds[i], cdb, 0, 0, 0);
}

/* ---- images as drives (C7) */

int cdrom_is_image(int i) { return i >= 0 && i < ncd && cds[i].img != 0; }

const char *cdrom_image_name(int i) { return cdrom_is_image(i) ? cds[i].img_name : ""; }

/* A CUE sheet's first track, if it is data: its file's name (beside the sheet), and its sectors' bytes. 0, or -1. */
/* (cue_parse's sizes: a FILE of the sheet's, named from its folder) */
struct cue_ctx {
    struct fsv_vol *v;
    const char *dir;
};

static long cue_size(void *ctx, const char *name)
{
    struct cue_ctx *x = ctx;
    struct fsv_file f;
    char path[192];
    long n;
    ksnprintf(path, sizeof path, "%s%s", x->dir, name);
    if (fsv_open(x->v, path, FSV_READ, FSV_DENY_WRITE, &f) != 0)
        return -1;
    n = (long)fsv_size(&f);
    fsv_close(&f);
    return n;
}

int cdrom_mount_image(struct fsv_vol *v, const char *path, const char *shown)
{
    struct fsv_file *f, *af = 0;
    struct cue *cu = 0;
    unsigned long bps = ISO_SECTOR, off = 0;
    int i, n = (int)strlen(path);
    char dir[128] = "";

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
        /* A CUE sheet: its disc laid out (cue.c); its first track, a data one, is the volume DOS reads, from its
           file at its INDEX 01; its audio tracks the digital reader's. A sheet of audio alone mounts as an audio
           CD does: not ready to DOS, its tracks to play. */
        const char *slash = strrchr(path, '\\');
        unsigned dl = slash ? (unsigned)(slash - path + 1) : 0;
        char *text = kmalloc(16384);
        const char *why = "memory";
        struct cue_ctx x;
        long got;
        int k;
        if (dl >= sizeof dir || !text || !(cu = kmalloc(sizeof *cu)) || !(af = kmalloc(sizeof *af))) {
            got = -1;
        } else {
            memcpy(dir, path, dl);
            dir[dl] = 0;
            got = fsv_read(f, text, 16383);
        }
        fsv_close(f);
        if (got > 0) {
            text[got] = 0;
            for (k = 0; k < got; k++)           /* (its FILEs' folders as GLOS's paths have them) */
                if (text[k] == '/')
                    text[k] = '\\';
            x.v = v, x.dir = dir;
            got = cue_parse(text, cue_size, &x, cu, &why) == 0 ? 1 : -1;
        }
        if (text)
            kfree(text);
        if (got > 0) {
            char data[192];
            bps = cu->t[0].type == CUE_AUDIO ? 2352 : cu->t[0].bps;
            off = cu->t[0].type == CUE_AUDIO ? 0 : cu->t[0].off * bps;
            ksnprintf(data, sizeof data, "%s%s", dir, cu->file[cu->t[0].file]);
            if (fsv_open(v, data, FSV_READ, FSV_DENY_WRITE, f) != 0)
                got = -3;
        }
        if (got <= 0) {
            kprintf("GLOS-CD image refused file=%s why=%s\n", shown, got == -3 ? "data-file" : why);
            if (cu)
                kfree(cu);
            if (af)
                kfree(af);
            kfree(f);
            return got == -3 ? -3 : -4;
        }
    }
    memset(&cds[i], 0, sizeof cds[i]);
    cds[i].img = f, cds[i].img_bps = bps, cds[i].img_off = off;
    cds[i].cue = cu, cds[i].vol = v, cds[i].afile = af, cds[i].afile_no = -1;
    memcpy(cds[i].dir, dir, sizeof cds[i].dir);
    strncpy(cds[i].img_name, shown, sizeof cds[i].img_name - 1);
    if (i >= ncd)
        ncd = i + 1;
    kprintf("GLOS-CD image=%d file=%s bps=%u tracks=%d\n", i, shown, (u32)bps, cu ? cu->ntracks : 1);
    return i;
}

void cdrom_unmount_image(int i)
{
    if (!cdrom_is_image(i))
        return;
    if (i == cda_unit) {                        /* (its play stopped, and the reader out of its files) */
        u32 f;
        cdrom_stop(i);
        f = irq_save();
        cda_unit = -1;
        irq_restore(f);
        while (cda_busy == i)
            thread_sleep_until(timer_ticks() + 2);
    }
    if (cds[i].afile) {
        if (cds[i].afile_no >= 0)
            fsv_close(cds[i].afile);
        kfree(cds[i].afile);
    }
    if (cds[i].cue)
        kfree(cds[i].cue);
    fsv_close(cds[i].img);
    kfree(cds[i].img);
    memset(&cds[i], 0, sizeof cds[i]);
    while (ncd && !cds[ncd - 1].d && !cds[ncd - 1].img)
        ncd--;
}

struct iso_vol *cdrom_vol(int i) { return i >= 0 && i < ncd ? &cds[i].v : 0; }
