/* ATA disks by programmed I/O (ata.h; docs/v1/native-dos.md, N1c). The ATA/ATAPI-6 and ATA-8 command sets are
 * the references (register protocol, IDENTIFY's words, PIO data-in and data-out); no code is anyone else's. */
#include "ata.h"

/* The command block's registers (base + n) and the control block's (ctl). */
#define R_DATA   0
#define R_ERR    1                      /* (written: features) */
#define R_COUNT  2
#define R_LBA0   3
#define R_LBA1   4
#define R_LBA2   5
#define R_DEV    6
#define R_STATUS 7                      /* (written: command) */

#define S_ERR  0x01
#define S_DRQ  0x08
#define S_DF   0x20
#define S_DRDY 0x40
#define S_BSY  0x80

#define C_NIEN 0x02                     /* device control: the interrupt held off */
#define C_SRST 0x04
#define C_HOB  0x80

static unsigned char in(struct ata_chan *ch, unsigned r)
{
    return ch->io->in8(ch->io->ctx, ch->base + r);
}

static void out(struct ata_chan *ch, unsigned r, unsigned v)
{
    ch->io->out8(ch->io->ctx, ch->base + r, (unsigned char)v);
}

static unsigned char alt(struct ata_chan *ch)
{
    return ch->io->in8(ch->io->ctx, ch->ctl);
}

static void control(struct ata_chan *ch, unsigned v)
{
    ch->io->out8(ch->io->ctx, ch->ctl, (unsigned char)v);
}

static unsigned long now(struct ata_chan *ch)
{
    return ch->io->ms(ch->io->ctx);
}

/* 400 ns, the time a drive has to show a new status: four reads of the alternate status. */
static void settle(struct ata_chan *ch)
{
    alt(ch);
    alt(ch);
    alt(ch);
    alt(ch);
}

/* BSY clear within `limit` ms: ATA_OK with the status, or ATA_ETIMEOUT. */
static int not_busy(struct ata_chan *ch, unsigned long limit, unsigned char *s)
{
    unsigned long t0 = now(ch);

    for (;;) {
        *s = alt(ch);
        if (!(*s & S_BSY))
            return ATA_OK;
        if (now(ch) - t0 >= limit)
            return ATA_ETIMEOUT;
    }
}

/* The device chosen, with the device register's other bits (LBA mode, LBA 24-27 or the head). */
static int select_dev(struct ata_dev *d, unsigned bits)
{
    struct ata_chan *ch = d->ch;
    unsigned char s;
    int r;

    if (ch->selected != d->slave) {
        r = not_busy(ch, ATA_T_CMD, &s);    /* (the other device must be done before the change) */
        if (r)
            return r;
    }
    out(ch, R_DEV, 0xA0 | (d->slave ? 0x10 : 0) | bits);
    settle(ch);
    ch->selected = d->slave;
    r = not_busy(ch, ATA_T_CMD, &s);
    if (r)
        return r;
    return (s & S_DRQ) ? ATA_EDEVICE : ATA_OK;  /* (DRQ left over from something unfinished) */
}

/* The end of a command: the status register read (which clears a pending interrupt), the interrupt allowed
   again, the error register kept when the drive says so. */
static int finish(struct ata_dev *d, int r)
{
    struct ata_chan *ch = d->ch;

    d->status = in(ch, R_STATUS);
    if (!r && (d->status & (S_ERR | S_DF)))
        r = ATA_EDEVICE;
    if (r == ATA_EDEVICE || (d->status & S_ERR))
        d->err = in(ch, R_ERR);
    control(ch, 0);
    return r;
}

/* After a command or a block: BSY clear, then the drive's verdict. ATA_OK with DRQ set (data to move), or an
   error. want_drq 0: the command's end (no data). */
static int block_ready(struct ata_dev *d, unsigned long limit, int want_drq)
{
    struct ata_chan *ch = d->ch;
    unsigned char s;
    int r = not_busy(ch, limit, &s);

    if (r)
        return r;
    d->status = s;
    if (s & (S_ERR | S_DF))
        return ATA_EDEVICE;
    if (want_drq && !(s & S_DRQ))
        return ATA_EDEVICE;             /* (no data asked for: the drive gave up without saying why) */
    return ATA_OK;
}

/* ---- finding the devices */

static void copy_string(char *out, const unsigned short *w, int words)
{
    int i, n = words * 2;

    for (i = 0; i < words; i++) {
        out[2 * i] = (char)(w[i] >> 8);
        out[2 * i + 1] = (char)(w[i] & 0xFF);
    }
    while (n && (out[n - 1] == ' ' || !out[n - 1]))
        n--;
    out[n] = 0;
    for (i = 0; i < n && out[i] == ' '; i++)
        ;
    if (i) {
        int k;

        for (k = 0; i + k <= n; k++)
            out[k] = out[i + k];
    }
}

/* IDENTIFY's words read into d (cmd: ECh, or A1h for ATAPI's). */
static int identify(struct ata_dev *d, unsigned cmd)
{
    struct ata_chan *ch = d->ch;
    int r;

    control(ch, C_NIEN);
    r = select_dev(d, 0);
    if (r)
        return finish(d, r);
    out(ch, R_STATUS, cmd);
    settle(ch);
    if (!alt(ch)) {                     /* (status 0: no device there, the other answering for it) */
        control(ch, 0);
        return ATA_ENODEV;
    }
    r = block_ready(d, ATA_T_CMD, 1);
    if (r)
        return finish(d, r);
    ch->io->in16s(ch->io->ctx, ch->base + R_DATA, d->id, 256);
    return finish(d, ATA_OK);
}

/* SET MULTIPLE MODE with the most sectors a block the drive takes (a power of 2, 16 at most). */
static void set_multiple(struct ata_dev *d)
{
    struct ata_chan *ch = d->ch;
    unsigned max = d->id[47] & 0xFF, m = 1;

    d->multiple = 0;
    if (!max)
        return;
    while (m * 2 <= max && m * 2 <= 16)
        m *= 2;
    control(ch, C_NIEN);
    if (select_dev(d, 0)) {
        finish(d, ATA_ETIMEOUT);
        return;
    }
    out(ch, R_COUNT, m);
    out(ch, R_STATUS, 0xC6);
    settle(ch);
    if (finish(d, block_ready(d, ATA_T_CMD, 0)) == ATA_OK)
        d->multiple = m;
}

static void describe(struct ata_dev *d)
{
    const unsigned short *id = d->id;

    copy_string(d->model, id + 27, 20);
    copy_string(d->serial, id + 10, 10);
    if (d->atapi)
        return;
    if ((id[53] & 1) && id[54] && id[55] && id[56]) {
        d->cyls = id[54];
        d->heads = id[55];
        d->spt = id[56];
    } else {
        d->cyls = id[1];
        d->heads = id[3];
        d->spt = id[6];
    }
    d->lba = (id[49] & 0x200) != 0;
    d->lba48 = d->lba && (id[83] & 0x4400) == 0x4400 && (id[86] & 0x400);
    if (d->lba48) {
        d->sectors_lo = id[100] | (unsigned long)id[101] << 16;
        d->sectors_hi = id[102] | (unsigned long)id[103] << 16;
    } else if (d->lba) {
        d->sectors_lo = id[60] | (unsigned long)id[61] << 16;
        d->sectors_hi = 0;
    } else {
        d->sectors_lo = (unsigned long)d->cyls * d->heads * d->spt;
        d->sectors_hi = 0;
    }
}

/* Hosted: device i asked as it is, IDENTIFY and, where it aborts with ATAPI's signature, IDENTIFY PACKET. */
static void probe_as_is(struct ata_chan *ch, struct ata_dev *d)
{
    unsigned char s;
    int r;

    out(ch, R_DEV, d->slave ? 0xB0 : 0xA0);
    settle(ch);
    ch->selected = d->slave;
    s = alt(ch);
    if (s == 0xFF || not_busy(ch, ATA_T_CMD, &s))
        return;
    r = identify(d, 0xEC);
    if (r == ATA_EDEVICE) {
        unsigned char l1 = in(ch, R_LBA1), l2 = in(ch, R_LBA2);

        if (!((l1 == 0x14 && l2 == 0xEB) || (l1 == 0x69 && l2 == 0x96)))
            return;
        d->atapi = 1;
        r = identify(d, 0xA1);
    }
    if (r != ATA_OK || (!d->atapi && (d->id[0] & 0x8000)))
        return;
    d->present = 1;
    describe(d);
    if (!d->atapi && (d->id[59] & 0x100) && (d->id[59] & 0xFF) <= 16)
        d->multiple = d->id[59] & 0xFF;     /* (what the BIOS set; 0: single sectors) */
}

static int probe_(struct ata_chan *ch, struct ata_dev d[2], int flags)
{
    unsigned char s0, s1;
    unsigned long t0;
    int i;

    for (i = 0; i < 2; i++) {
        unsigned char *p = (unsigned char *)&d[i];
        unsigned k;

        for (k = 0; k < sizeof d[i]; k++)
            p[k] = 0;
        d[i].ch = ch;
        d[i].slave = i;
    }
    ch->selected = -1;
    /* A floating bus reads FFh from every register: no channel. */
    out(ch, R_DEV, 0xA0);
    settle(ch);
    s0 = alt(ch);
    out(ch, R_DEV, 0xB0);
    settle(ch);
    s1 = alt(ch);
    if (s0 == 0xFF && s1 == 0xFF)
        return ATA_ENODEV;
    if (!(flags & ATA_PROBE_OWN)) {
        probe_as_is(ch, &d[0]);
        probe_as_is(ch, &d[1]);
        control(ch, 0);
        return ATA_OK;
    }
    /* The soft reset: both devices post their signatures. */
    control(ch, C_NIEN | C_SRST);
    t0 = now(ch);
    while (now(ch) - t0 < 2)            /* (5 us at least; 2 ms is the clock's grain) */
        ;
    control(ch, C_NIEN);
    t0 = now(ch);
    while (now(ch) - t0 < 3)
        ;
    /* The first device that answered, ready (a channel with device 1 alone, against the rules but seen). */
    i = s0 == 0xFF;
    out(ch, R_DEV, i ? 0xB0 : 0xA0);
    settle(ch);
    ch->selected = i;
    if (not_busy(ch, ATA_T_RESET, &s0)) {
        control(ch, 0);
        return ATA_ETIMEOUT;
    }
    /* Both signatures first: the devices latch every register write, so the first command would overwrite the
       other's. */
    for (i = 0; i < 2; i++) {
        unsigned char cnt, l0, l1, l2, s;

        out(ch, R_DEV, i ? 0xB0 : 0xA0);
        settle(ch);
        ch->selected = i;
        s = alt(ch);
        if (s == 0xFF)
            continue;                   /* (nothing there; an absent device 1 reads 00h, which IDENTIFY tells) */
        if (not_busy(ch, ATA_T_RESET, &s))
            continue;
        cnt = in(ch, R_COUNT);
        l0 = in(ch, R_LBA0);
        l1 = in(ch, R_LBA1);
        l2 = in(ch, R_LBA2);
        if (cnt != 1 || l0 != 1)
            continue;
        if ((l1 == 0x14 && l2 == 0xEB) || (l1 == 0x69 && l2 == 0x96))
            d[i].atapi = 1;
        else if (!((l1 == 0 && l2 == 0) || (l1 == 0x3C && l2 == 0xC3)))
            continue;
        d[i].present = -1;              /* (a signature: IDENTIFY next) */
    }
    for (i = 0; i < 2; i++) {
        if (d[i].present != -1)
            continue;
        d[i].present = 0;
        if (identify(&d[i], d[i].atapi ? 0xA1 : 0xEC) != ATA_OK)
            continue;
        if (!d[i].atapi && (d[i].id[0] & 0x8000))
            continue;                   /* (word 0 bit 15: not an ATA device after all) */
        d[i].present = 1;
        describe(&d[i]);
        if (!d[i].atapi)
            set_multiple(&d[i]);
    }
    control(ch, 0);
    return ATA_OK;
}

int ata_probe(struct ata_chan *ch, struct ata_dev d[2], int flags)
{
    const struct ata_io *io = ch->io;
    int r;

    if (io->hold)
        io->hold(io->ctx, 1);
    r = probe_(ch, d, flags);
    if (io->hold)
        io->hold(io->ctx, 0);
    return r;
}

/* ---- reading and writing */

/* Whether sectors [lba, lba + n) are on the drive. */
static int in_range(const struct ata_dev *d, unsigned long lo, unsigned long hi, unsigned long n)
{
    unsigned long end_lo = lo + n, end_hi = hi + (end_lo < lo);

    return end_hi < d->sectors_hi || (end_hi == d->sectors_hi && end_lo <= d->sectors_lo);
}

/* One command's registers written: count sectors (1-256, or 1-65536 with ext) at lba, then cmd. */
static int issue(struct ata_dev *d, unsigned long lo, unsigned long hi, unsigned long count, int ext, unsigned cmd)
{
    struct ata_chan *ch = d->ch;
    int r;

    if (ext) {
        r = select_dev(d, 0x40);
        if (r)
            return r;
        out(ch, R_COUNT, (count >> 8) & 0xFF);  /* (the "previous" bytes first: 15-8 and 47-24) */
        out(ch, R_LBA0, (lo >> 24) & 0xFF);
        out(ch, R_LBA1, hi & 0xFF);
        out(ch, R_LBA2, (hi >> 8) & 0xFF);
        out(ch, R_COUNT, count & 0xFF);
        out(ch, R_LBA0, lo & 0xFF);
        out(ch, R_LBA1, (lo >> 8) & 0xFF);
        out(ch, R_LBA2, (lo >> 16) & 0xFF);
    } else if (d->lba) {
        r = select_dev(d, 0x40 | ((lo >> 24) & 0x0F));
        if (r)
            return r;
        out(ch, R_COUNT, count & 0xFF);
        out(ch, R_LBA0, lo & 0xFF);
        out(ch, R_LBA1, (lo >> 8) & 0xFF);
        out(ch, R_LBA2, (lo >> 16) & 0xFF);
    } else {
        unsigned long track = lo / d->spt, cyl = track / d->heads;
        unsigned head = (unsigned)(track % d->heads), sec = (unsigned)(lo % d->spt) + 1;

        r = select_dev(d, head);
        if (r)
            return r;
        out(ch, R_COUNT, count & 0xFF);
        out(ch, R_LBA0, sec);
        out(ch, R_LBA1, cyl & 0xFF);
        out(ch, R_LBA2, (cyl >> 8) & 0xFF);
    }
    out(ch, R_STATUS, cmd);
    settle(ch);
    return ATA_OK;
}

/* Sectors in one command from lo: ext (48-bit) where the range needs it, else at most 256. */
static unsigned long chunk(const struct ata_dev *d, unsigned long lo, unsigned long hi, unsigned long n, int *ext)
{
    *ext = d->lba48 && (hi || lo + (n < 256 ? n : 256) > 0x0FFFFFFFUL || lo + (n < 256 ? n : 256) < lo);
    if (*ext)
        return n < 65536UL ? n : 65536UL;
    return n < 256 ? n : 256;
}

static int transfer(struct ata_dev *d, unsigned long lo, unsigned long hi, unsigned long n, unsigned char *buf,
                    unsigned long *done, int writing)
{
    struct ata_chan *ch = d->ch;
    unsigned long did = 0;
    int r = ATA_OK;

    if (done)
        *done = 0;
    if (!d->present || d->atapi)
        return d->present ? ATA_ENOTSUP : ATA_ENODEV;
    if (!n || !in_range(d, lo, hi, n))
        return ATA_ERANGE;
    while (did < n && !r) {
        int ext;
        unsigned long c = chunk(d, lo, hi, n - did, &ext), left = c, blk = d->multiple ? d->multiple : 1, pend = 0;
        unsigned cmd = writing ? (d->multiple ? (ext ? 0x39 : 0xC5) : (ext ? 0x34 : 0x30))
                               : (d->multiple ? (ext ? 0x29 : 0xC4) : (ext ? 0x24 : 0x20));

        control(ch, C_NIEN);
        r = issue(d, lo, hi, c == 65536UL ? 0 : c, ext, cmd);
        while (!r && left) {
            unsigned long k = left < blk ? left : blk;

            r = block_ready(d, ATA_T_BLOCK, 1);
            if (r)
                break;
            did += pend;                        /* (a write's last block: taken, as the drive asks for more) */
            pend = 0;
            if (writing)
                ch->io->out16s(ch->io->ctx, ch->base + R_DATA, (const unsigned short *)buf, (unsigned)(k * 256));
            else
                ch->io->in16s(ch->io->ctx, ch->base + R_DATA, (unsigned short *)buf, (unsigned)(k * 256));
            buf += k * 512;
            left -= k;
            /* A read's block is had once moved; a write's once the drive says it took it. */
            if (writing)
                pend = k;
            else
                did += k;
        }
        if (!r && pend) {
            r = block_ready(d, ATA_T_BLOCK, 0);
            if (!r)
                did += pend;
        }
        r = finish(d, r);
        lo += c;
        hi += lo < c;
    }
    if (done)
        *done = did;
    return r;
}

static int read_(struct ata_dev *d, unsigned long lba_lo, unsigned long lba_hi, unsigned long n, void *buf,
             unsigned long *done)
{
    return transfer(d, lba_lo, lba_hi, n, (unsigned char *)buf, done, 0);
}

int ata_read(struct ata_dev *d, unsigned long lba_lo, unsigned long lba_hi, unsigned long n, void *buf,
             unsigned long *done)
{
    const struct ata_io *io = d->ch->io;
    int r;

    if (io->hold)
        io->hold(io->ctx, 1);
    r = read_(d, lba_lo, lba_hi, n, buf, done);
    if (io->hold)
        io->hold(io->ctx, 0);
    return r;
}

static int write_(struct ata_dev *d, unsigned long lba_lo, unsigned long lba_hi, unsigned long n, const void *buf,
              unsigned long *done)
{
    return transfer(d, lba_lo, lba_hi, n, (unsigned char *)buf, done, 1);
}

int ata_write(struct ata_dev *d, unsigned long lba_lo, unsigned long lba_hi, unsigned long n, const void *buf,
              unsigned long *done)
{
    const struct ata_io *io = d->ch->io;
    int r;

    if (io->hold)
        io->hold(io->ctx, 1);
    r = write_(d, lba_lo, lba_hi, n, buf, done);
    if (io->hold)
        io->hold(io->ctx, 0);
    return r;
}

static int verify_(struct ata_dev *d, unsigned long lba_lo, unsigned long lba_hi, unsigned long n, unsigned long *done)
{
    struct ata_chan *ch = d->ch;
    unsigned long did = 0;
    int r = ATA_OK;

    if (done)
        *done = 0;
    if (!d->present || d->atapi)
        return d->present ? ATA_ENOTSUP : ATA_ENODEV;
    if (!n || !in_range(d, lba_lo, lba_hi, n))
        return ATA_ERANGE;
    while (did < n && !r) {
        int ext;
        unsigned long c = chunk(d, lba_lo, lba_hi, n - did, &ext);

        control(ch, C_NIEN);
        r = issue(d, lba_lo, lba_hi, c == 65536UL ? 0 : c, ext, ext ? 0x42 : 0x40);
        if (!r)
            r = block_ready(d, ATA_T_CMD, 0);
        if (r == ATA_EDEVICE && d->lba && !ext) {
            /* (the drive leaves the bad sector's address in its LBA registers) */
            unsigned long at = in(ch, R_LBA0) | (unsigned long)in(ch, R_LBA1) << 8 |
                               (unsigned long)in(ch, R_LBA2) << 16 | (unsigned long)(in(ch, R_DEV) & 0x0F) << 24;

            if (at >= lba_lo && at < lba_lo + c)
                did += at - lba_lo;
        }
        r = finish(d, r);
        if (!r)
            did += c;
        lba_lo += c;
        lba_hi += lba_lo < c;
    }
    if (done)
        *done = did;
    return r;
}

int ata_verify(struct ata_dev *d, unsigned long lba_lo, unsigned long lba_hi, unsigned long n, unsigned long *done)
{
    const struct ata_io *io = d->ch->io;
    int r;

    if (io->hold)
        io->hold(io->ctx, 1);
    r = verify_(d, lba_lo, lba_hi, n, done);
    if (io->hold)
        io->hold(io->ctx, 0);
    return r;
}

static int flush_(struct ata_dev *d)
{
    struct ata_chan *ch = d->ch;
    int r;

    if (!d->present || d->atapi)
        return d->present ? ATA_ENOTSUP : ATA_ENODEV;
    control(ch, C_NIEN);
    r = select_dev(d, d->lba ? 0x40 : 0);
    if (!r) {
        out(ch, R_STATUS, d->lba48 ? 0xEA : 0xE7);
        settle(ch);
        r = block_ready(d, ATA_T_CMD * 3, 0);   /* (a big cache takes a while) */
    }
    return finish(d, r);
}

int ata_flush(struct ata_dev *d)
{
    const struct ata_io *io = d->ch->io;
    int r;

    if (io->hold)
        io->hold(io->ctx, 1);
    r = flush_(d);
    if (io->hold)
        io->hold(io->ctx, 0);
    return r;
}

/* ---- ATAPI packets (N4: the CD-ROM drive) */

static int packet_(struct ata_dev *d, const unsigned char cdb[12], unsigned char *buf, unsigned long len,
                   unsigned long *got)
{
    struct ata_chan *ch = d->ch;
    unsigned long limit = len < 0xF800UL ? len : 0xF800UL, have = 0;
    unsigned short w[6], junk[256];
    unsigned char s;
    int r, i;

    if (got)
        *got = 0;
    if (!d->present || !d->atapi)
        return ATA_ENOTSUP;
    if (limit & 1)
        limit++;                                /* (an even byte count, as the drive moves words) */
    if (!limit)
        limit = 2;
    control(ch, C_NIEN);
    if ((r = select_dev(d, 0)) != ATA_OK)
        return finish(d, r);
    out(ch, R_ERR, 0);                          /* (features: PIO, no overlap) */
    out(ch, R_LBA1, limit & 0xFF);
    out(ch, R_LBA2, (limit >> 8) & 0xFF);
    out(ch, R_STATUS, 0xA0);
    settle(ch);
    if ((r = not_busy(ch, ATA_T_CMD, &s)) != ATA_OK)
        return finish(d, r);
    if ((s & (S_ERR | S_DF)) || !(s & S_DRQ) || (in(ch, R_COUNT) & 3) != 1)
        return finish(d, ATA_EDEVICE);          /* (it wants no packet: not ATAPI after all, or confused) */
    for (i = 0; i < 6; i++)
        w[i] = (unsigned short)(cdb[2 * i] | cdb[2 * i + 1] << 8);
    ch->io->out16s(ch->io->ctx, ch->base + R_DATA, w, 6);
    settle(ch);
    for (;;) {
        unsigned long n, take, k;

        if ((r = not_busy(ch, ATA_T_PACKET, &s)) != ATA_OK)
            return finish(d, r);
        if (s & (S_ERR | S_DF))
            break;
        if (!(s & S_DRQ))
            break;                              /* (the status phase: done) */
        if ((in(ch, R_COUNT) & 3) != 2)         /* (data to the host is IO 1, CoD 0; anything else isn't ours) */
            return finish(d, ATA_EDEVICE);
        n = in(ch, R_LBA1) | (unsigned long)in(ch, R_LBA2) << 8;
        if (!n)
            return finish(d, ATA_EDEVICE);
        take = have < len ? len - have : 0;
        if (take > n)
            take = n;
        if (take >= 2) {
            ch->io->in16s(ch->io->ctx, ch->base + R_DATA, (unsigned short *)(void *)(buf + have), (unsigned)(take / 2));
            have += take & ~1UL;
        }
        for (k = take & ~1UL; k < n;) {         /* (an odd last byte, or more than was wanted: read, kept where it fits) */
            unsigned words = (unsigned)((n - k + 1) / 2 > 256 ? 256 : (n - k + 1) / 2), j;
            ch->io->in16s(ch->io->ctx, ch->base + R_DATA, junk, words);
            for (j = 0; j < words * 2 && k < n; j++, k++)
                if (have < len)
                    buf[have++] = ((unsigned char *)junk)[j];
        }
        settle(ch);
    }
    if (got)
        *got = have;
    return finish(d, ATA_OK);
}

int ata_packet(struct ata_dev *d, const unsigned char cdb[12], void *buf, unsigned long len, unsigned long *got)
{
    const struct ata_io *io = d->ch->io;
    int r;

    if (io->hold)
        io->hold(io->ctx, 1);
    r = packet_(d, cdb, (unsigned char *)buf, len, got);
    if (io->hold)
        io->hold(io->ctx, 0);
    return r;
}
