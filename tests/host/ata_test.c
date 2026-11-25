/* ata_test: kernel/drv/ata.c (N1c) against a model of a channel and its two drives, in the ATA register
 * protocol: writes to the command block reach both devices, the selected one answers reads (an absent device 1
 * reads status 00h, a channel with nothing FFh), a soft reset posts signatures, data moves in DRQ blocks (READ/WRITE
 * MULTIPLE's size once set), INTRQ is raised unless nIEN holds it off and cleared by reading the status.
 * Faults: a sector that won't read, a write that fails, a drive that stays busy. Sectors hold a pattern of their
 * own LBA unless written, so a wrong address shows as wrong data. An ATAPI drive (a CD-ROM, N4) takes PACKET:
 * its 12 bytes, then its answer in pieces no longer than the host's byte count (nor its own piece size), then
 * the status; CHECK CONDITION with sense data, a disc changed (UNIT ATTENTION), none in (NOT READY).
 * "ata_test: 0 failures" when all held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/drv/ata.h"

static int fails;

#define CHECK(cond, ...)                                                                                       \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            printf("FAIL %s:%d: ", __func__, __LINE__);                                                        \
            printf(__VA_ARGS__);                                                                               \
            printf("\n");                                                                                      \
            fails++;                                                                                           \
        }                                                                                                      \
    } while (0)

/* ---- the model */

typedef unsigned long long u64;

#define MAXW 8192                       /* written sectors kept */

struct drive {
    int present, atapi, lba, lba48;
    u64 sectors;
    unsigned cyls, heads, spt, max_mult, mult;
    /* registers: [0] the last written, [1] the one before (48-bit's "previous") */
    unsigned char feat, count[2], lba0[2], lba1[2], lba2[2], dev, status, err;
    /* a transfer */
    int dir;                            /* 0 none, 1 to the host, 2 from it */
    unsigned char block[16 * 512];
    unsigned words, wpos;               /* the block's words, those moved */
    u64 at;                             /* its first sector */
    unsigned long left;                 /* sectors still to come after this block */
    unsigned busy;                      /* status reads before BSY clears */
    int ext_used, cmds;
    unsigned last_cmd;
    /* faults */
    u64 bad_read, bad_write;            /* (~0: none) */
    int stuck;                          /* BSY for ever */
    /* written sectors */
    u64 wlba[MAXW];
    unsigned char *wdata[MAXW];
    int nw;
    char model[41];
    /* ATAPI (a CD-ROM drive) */
    int pkt;                            /* 0 none, 1 its packet wanted, 2 its answer under way */
    unsigned char cdb[12];
    unsigned cdb_words, limit, piece_max, pieces;
    unsigned char *resp;
    unsigned long resp_len, resp_pos, piece_end;
    int media, changed;                 /* a disc in; changed since (UNIT ATTENTION owed) */
    u64 disc;                           /* its 2048-byte sectors */
    unsigned char sense, asc, ascq;
};

static struct {
    struct drive d[2];
    int sel, floating, intrq, nien;
    unsigned char ctl;
    u64 us;                             /* the clock: a microsecond a port access */
} ch;

static unsigned char pattern(u64 lba, unsigned i)
{
    u64 x = lba * 2654435761ULL + i * 40503ULL + 7;

    return (unsigned char)(x ^ (x >> 11) ^ (x >> 23));
}

static void sector_get(struct drive *d, u64 lba, unsigned char *out)
{
    int k;
    unsigned i;

    for (k = 0; k < d->nw; k++)
        if (d->wlba[k] == lba) {
            memcpy(out, d->wdata[k], 512);
            return;
        }
    for (i = 0; i < 512; i++)
        out[i] = pattern(lba, i);
}

static void sector_put(struct drive *d, u64 lba, const unsigned char *in)
{
    int k;

    for (k = 0; k < d->nw; k++)
        if (d->wlba[k] == lba) {
            memcpy(d->wdata[k], in, 512);
            return;
        }
    if (d->nw == MAXW) {
        printf("FAIL model: too many sectors written\n");
        fails++;
        return;
    }
    d->wlba[d->nw] = lba;
    d->wdata[d->nw] = malloc(512);
    memcpy(d->wdata[d->nw++], in, 512);
}

static void raise_irq(void)
{
    if (!ch.nien)
        ch.intrq = 1;
}

static void abort_cmd(struct drive *d)
{
    d->status = 0x51;
    d->err = 0x04;
    d->dir = 0;
    raise_irq();
}

static void put_id_string(unsigned short *w, const char *s, int words)
{
    int i;
    char b[80];

    memset(b, ' ', sizeof b);
    memcpy(b, s, strlen(s));
    for (i = 0; i < words; i++)
        w[i] = (unsigned short)((unsigned char)b[2 * i] << 8 | (unsigned char)b[2 * i + 1]);
}

static void identify_words(struct drive *d, unsigned short *w)
{
    memset(w, 0, 512);
    if (d->atapi) {
        w[0] = 0x85C0;
        put_id_string(w + 27, d->model, 20);
        return;
    }
    w[1] = (unsigned short)d->cyls;
    w[3] = (unsigned short)d->heads;
    w[6] = (unsigned short)d->spt;
    put_id_string(w + 10, "SERIAL123", 10);
    put_id_string(w + 27, d->model, 20);
    w[47] = (unsigned short)(0x8000 | d->max_mult);
    w[49] = d->lba ? 0x0200 : 0;
    w[53] = 1;
    w[59] = (unsigned short)(d->mult ? 0x100 | d->mult : 0);
    w[54] = (unsigned short)d->cyls;
    w[55] = (unsigned short)d->heads;
    w[56] = (unsigned short)d->spt;
    if (d->lba) {
        u64 n = d->sectors > 0x0FFFFFFFULL ? 0x0FFFFFFFULL : d->sectors;

        w[60] = (unsigned short)n;
        w[61] = (unsigned short)(n >> 16);
    }
    if (d->lba48) {
        w[83] = 0x4400;
        w[86] = 0x0400;
        w[100] = (unsigned short)d->sectors;
        w[101] = (unsigned short)(d->sectors >> 16);
        w[102] = (unsigned short)(d->sectors >> 32);
        w[103] = 0;
    }
}

/* The next block of a transfer made ready (data in: read from the sectors), or the command's end. */
static void next_block(struct drive *d)
{
    unsigned long n = d->mult && (d->last_cmd == 0xC4 || d->last_cmd == 0xC5 || d->last_cmd == 0x29 ||
                                  d->last_cmd == 0x39) ? d->mult : 1, i;

    if (!d->left) {
        d->dir = 0;
        d->status = 0x50;
        raise_irq();
        return;
    }
    if (n > d->left)
        n = d->left;
    d->words = (unsigned)n * 256;
    d->wpos = 0;
    d->busy = 3;
    if (d->dir == 1) {
        for (i = 0; i < n; i++) {
            if (d->at + i == d->bad_read) {
                d->status = 0x51;
                d->err = 0x40;          /* UNC */
                d->dir = 0;
                raise_irq();
                return;
            }
            sector_get(d, d->at + i, d->block + i * 512);
        }
        d->at += n;
    }
    d->left -= n;
    d->status = 0x58;
    raise_irq();
}

static int address(struct drive *d, int ext, u64 *lba, unsigned long *n)
{
    if (ext) {
        *lba = (u64)d->lba0[0] | (u64)d->lba1[0] << 8 | (u64)d->lba2[0] << 16 | (u64)d->lba0[1] << 24 |
               (u64)d->lba1[1] << 32 | (u64)d->lba2[1] << 40;
        *n = d->count[0] | (unsigned long)d->count[1] << 8;
        if (!*n)
            *n = 65536;
        d->ext_used++;
    } else if (d->dev & 0x40) {
        *lba = (u64)d->lba0[0] | (u64)d->lba1[0] << 8 | (u64)d->lba2[0] << 16 | (u64)(d->dev & 0x0F) << 24;
        *n = d->count[0] ? d->count[0] : 256;
    } else {
        unsigned cyl = d->lba1[0] | (unsigned)d->lba2[0] << 8, head = d->dev & 0x0F, sec = d->lba0[0];

        if (!sec || sec > d->spt || head >= d->heads || cyl >= d->cyls)
            return -1;
        *lba = ((u64)cyl * d->heads + head) * d->spt + sec - 1;
        *n = d->count[0] ? d->count[0] : 256;
    }
    if ((ext || (d->dev & 0x40)) && !d->lba)
        return -1;
    return *lba + *n <= d->sectors ? 0 : -1;
}

static void command(struct drive *d, unsigned cmd)
{
    unsigned long n;
    u64 lba;

    d->cmds++;
    d->last_cmd = cmd;
    d->err = 0;
    if (d->stuck) {
        d->status = 0x80;
        return;
    }
    switch (cmd) {
    case 0xA0:                                  /* PACKET */
        if (!d->atapi) {
            abort_cmd(d);
            return;
        }
        d->limit = d->lba1[0] | d->lba2[0] << 8;
        d->pkt = 1;
        d->cdb_words = 0;
        d->dir = 2;
        d->count[0] = 1;                        /* (CoD: the packet wanted) */
        d->status = 0x58;
        d->busy = 2;
        return;
    case 0xEC:
    case 0xA1:
        if ((cmd == 0xEC) != !d->atapi) {
            if (d->atapi) {                     /* ATAPI's answer to IDENTIFY: abort, the signature again */
                d->count[0] = 1, d->lba0[0] = 1, d->lba1[0] = 0x14, d->lba2[0] = 0xEB;
            }
            abort_cmd(d);
            return;
        }
        identify_words(d, (unsigned short *)d->block);
        d->words = 256;
        d->wpos = 0;
        d->dir = 1;
        d->left = 0;
        d->busy = 5;
        d->status = 0x58;
        raise_irq();
        return;
    case 0xC6:
        if (!d->count[0] || d->count[0] > d->max_mult || (d->count[0] & (d->count[0] - 1))) {
            abort_cmd(d);
            return;
        }
        d->mult = d->count[0];
        d->status = 0x50;
        raise_irq();
        return;
    case 0x20: case 0xC4: case 0x24: case 0x29:
    case 0x30: case 0xC5: case 0x34: case 0x39: {
        int ext = cmd == 0x24 || cmd == 0x29 || cmd == 0x34 || cmd == 0x39, wr = cmd >= 0x30 && cmd != 0xC4;

        if (d->atapi || ((cmd == 0xC4 || cmd == 0xC5 || cmd == 0x29 || cmd == 0x39) && !d->mult) ||
            (ext && !d->lba48)) {
            abort_cmd(d);
            return;
        }
        if (address(d, ext, &lba, &n)) {
            d->status = 0x51;
            d->err = 0x10;                      /* IDNF */
            raise_irq();
            return;
        }
        d->at = lba;
        d->left = n;
        d->dir = wr ? 2 : 1;
        next_block(d);
        if (wr)
            ch.intrq = 0;                       /* (data out: no interrupt before the first block) */
        return;
    }
    case 0x40:
    case 0x42:
        if (cmd == 0x42 && !d->lba48) {
            abort_cmd(d);
            return;
        }
        if (address(d, cmd == 0x42, &lba, &n)) {
            d->status = 0x51;
            d->err = 0x10;
            raise_irq();
            return;
        }
        d->busy = 10;
        d->status = 0x50;
        if (d->bad_read >= lba && d->bad_read < lba + n) {
            d->status = 0x51;
            d->err = 0x40;
            d->lba0[0] = (unsigned char)d->bad_read, d->lba1[0] = (unsigned char)(d->bad_read >> 8);
            d->lba2[0] = (unsigned char)(d->bad_read >> 16);
            d->dev = (unsigned char)((d->dev & 0xF0) | ((d->bad_read >> 24) & 0x0F));
        }
        raise_irq();
        return;
    case 0xE7:
    case 0xEA:
        if (cmd == 0xEA && !d->lba48) {
            abort_cmd(d);
            return;
        }
        d->busy = 20;
        d->status = 0x50;
        raise_irq();
        return;
    default:
        abort_cmd(d);
    }
}

/* ---- ATAPI: the packet's command run, its answer given in pieces */

static void pkt_status(struct drive *d)
{
    d->pkt = 0;
    d->dir = 0;
    d->count[0] = 3;                            /* (CoD and IO: the status phase) */
    d->busy = 2;
    raise_irq();
}

static void pkt_check(struct drive *d, unsigned key, unsigned asc, unsigned ascq)
{
    d->sense = (unsigned char)key, d->asc = (unsigned char)asc, d->ascq = (unsigned char)ascq;
    d->status = 0x51;
    d->err = (unsigned char)(key << 4 | 0x04);
    pkt_status(d);
}

static void pkt_piece(struct drive *d)
{
    unsigned long n = d->resp_len - d->resp_pos, lim = d->limit & ~1UL;

    if (!n) {
        d->status = 0x50;
        free(d->resp);
        d->resp = 0;
        pkt_status(d);
        return;
    }
    if (lim >= 2048)
        lim &= ~2047UL;                         /* (as drives do: whole sectors when the count allows) */
    if (d->piece_max && lim > d->piece_max)
        lim = d->piece_max;
    if (n > lim)
        n = lim;
    d->piece_end = d->resp_pos + n;
    d->lba1[0] = (unsigned char)n, d->lba2[0] = (unsigned char)(n >> 8);
    d->count[0] = 2;                            /* (IO, not CoD: data to the host) */
    d->status = 0x58;
    d->pkt = 2;
    d->dir = 1;
    d->busy = 3;
    d->pieces++;
    raise_irq();
}

static void pkt_answer(struct drive *d, const unsigned char *data, unsigned long n, unsigned long alloc)
{
    if (n > alloc)
        n = alloc;
    d->resp = malloc(n ? n : 1);
    memcpy(d->resp, data, n);
    d->resp_len = n, d->resp_pos = 0;
    d->sense = d->asc = d->ascq = 0;
    pkt_piece(d);
}

static unsigned long be32(const unsigned char *p) { return (unsigned long)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static void pkt_run(struct drive *d)
{
    unsigned char *c = d->cdb, out[32];
    unsigned long lba, n, i;

    if (c[0] != 0x03 && c[0] != 0x1B && d->media && d->changed) {   /* (REQUEST SENSE and START STOP: not held) */
        d->changed = 0;
        pkt_check(d, 6, 0x28, 0);
        return;
    }
    switch (c[0]) {
    case 0x00:                                  /* TEST UNIT READY */
        if (!d->media) {
            pkt_check(d, 2, 0x3A, 0);
            return;
        }
        d->status = 0x50;
        pkt_status(d);
        return;
    case 0x03:                                  /* REQUEST SENSE */
        memset(out, 0, 18);
        out[0] = 0x70, out[2] = d->sense, out[7] = 10, out[12] = d->asc, out[13] = d->ascq;
        pkt_answer(d, out, 18, c[4]);
        return;
    case 0x1B:                                  /* START STOP UNIT: 2 eject, 3 load */
        if ((c[4] & 3) == 2)
            d->media = 0;
        else if ((c[4] & 3) == 3 && !d->media)
            d->media = 1, d->changed = 1;
        d->status = 0x50;
        pkt_status(d);
        return;
    case 0x25:                                  /* READ CAPACITY */
        if (!d->media) {
            pkt_check(d, 2, 0x3A, 0);
            return;
        }
        n = (unsigned long)d->disc - 1;
        out[0] = (unsigned char)(n >> 24), out[1] = (unsigned char)(n >> 16), out[2] = (unsigned char)(n >> 8);
        out[3] = (unsigned char)n, out[4] = 0, out[5] = 0, out[6] = 0x08, out[7] = 0;
        pkt_answer(d, out, 8, 8);
        return;
    case 0x28:                                  /* READ(10) */
        if (!d->media) {
            pkt_check(d, 2, 0x3A, 0);
            return;
        }
        lba = be32(c + 2), n = (unsigned long)c[7] << 8 | c[8];
        if (lba + n > d->disc) {
            pkt_check(d, 5, 0x21, 0);
            return;
        }
        {
            unsigned char *b = malloc(n * 2048 + 1);
            for (i = 0; i < n * 2048; i++)
                b[i] = pattern(lba + i / 2048, (unsigned)(i % 2048));
            pkt_answer(d, b, n * 2048, n * 2048);
            free(b);
        }
        return;
    case 0x43:                                  /* READ TOC, format 0: one data track and the lead-out */
        if (!d->media) {
            pkt_check(d, 2, 0x3A, 0);
            return;
        }
        memset(out, 0, 20);
        out[1] = 18, out[2] = 1, out[3] = 1;
        out[5] = 0x14, out[6] = 1;              /* (track 1 at LBA 0) */
        out[13] = 0x14, out[14] = 0xAA;
        out[16] = (unsigned char)(d->disc >> 24), out[17] = (unsigned char)(d->disc >> 16);
        out[18] = (unsigned char)(d->disc >> 8), out[19] = (unsigned char)d->disc;
        pkt_answer(d, out, 20, (unsigned long)c[7] << 8 | c[8]);
        return;
    }
    pkt_check(d, 5, 0x20, 0);                   /* (an opcode it hasn't got) */
}

static struct drive *selected(void)
{
    return &ch.d[ch.sel];
}

/* The kernel's lock around a call (ata_io's hold: one thread's call at a time): every access to the ports
   must come while it is held, and each call must give it back. */
static int hold_depth, hold_calls, hold_outside;

static void m_hold(void *ctx, int on)
{
    (void)ctx;
    hold_depth += on ? 1 : -1;
    hold_calls += on != 0;
}

static unsigned char m_in8(void *ctx, unsigned port)
{
    struct drive *d = selected(), *o = &ch.d[!ch.sel];
    unsigned r;

    hold_outside += hold_depth != 1;

    (void)ctx;
    ch.us++;
    if (ch.floating || (!d->present && !o->present))
        return 0xFF;
    if (port == 0x3F6 || port == 0x1F7) {
        unsigned char s;

        if (!d->present)
            return 0x00;
        if (port == 0x1F7)
            ch.intrq = 0;
        if (d->stuck)
            return 0x80 | 0x50;
        if (d->busy) {
            d->busy--;
            return 0x80;
        }
        s = d->status;
        return s;
    }
    if (!d->present)
        d = o;                          /* (the other answers for an absent device 1) */
    r = port - 0x1F0;
    switch (r) {
    case 1: return d->err;
    case 2: return d->count[0];
    case 3: return d->lba0[0];
    case 4: return d->lba1[0];
    case 5: return d->lba2[0];
    case 6: return d->dev;
    }
    return 0xFF;
}

static void soft_reset(void)
{
    int i;

    for (i = 0; i < 2; i++) {
        struct drive *d = &ch.d[i];

        if (!d->present)
            continue;
        d->count[0] = 1, d->lba0[0] = 1;
        d->lba1[0] = d->atapi ? 0x14 : 0, d->lba2[0] = d->atapi ? 0xEB : 0;
        d->err = 1;
        d->status = d->atapi ? 0x00 : 0x50;
        d->dir = 0;
        d->busy = 50;
        d->mult = 0;
    }
}

static void m_out8(void *ctx, unsigned port, unsigned char v)
{
    int i;

    (void)ctx;
    hold_outside += hold_depth != 1;
    ch.us++;
    if (port == 0x3F6) {
        if ((ch.ctl & 0x04) && !(v & 0x04))
            soft_reset();
        ch.ctl = v;
        ch.nien = (v & 0x02) != 0;
        return;
    }
    if (port == 0x1F7) {
        if (selected()->present)
            command(selected(), v);
        return;
    }
    if (port == 0x1F6)
        ch.sel = (v >> 4) & 1;
    for (i = 0; i < 2; i++) {               /* (both devices latch the command block) */
        struct drive *d = &ch.d[i];

        switch (port - 0x1F0) {
        case 1: d->feat = v; break;
        case 2: d->count[1] = d->count[0], d->count[0] = v; break;
        case 3: d->lba0[1] = d->lba0[0], d->lba0[0] = v; break;
        case 4: d->lba1[1] = d->lba1[0], d->lba1[0] = v; break;
        case 5: d->lba2[1] = d->lba2[0], d->lba2[0] = v; break;
        case 6: d->dev = v; break;
        }
    }
}

static void m_in16s(void *ctx, unsigned port, unsigned short *buf, unsigned count)
{
    struct drive *d = selected();
    unsigned i;

    hold_outside += hold_depth != 1;

    (void)ctx;
    ch.us += count;
    if (d->atapi && d->pkt == 2 && port == 0x1F0 && !d->busy) {
        for (i = 0; i < count; i++) {
            if (d->resp_pos >= d->piece_end) {
                printf("FAIL model: ATAPI data read past the piece\n");
                fails++;
                return;
            }
            buf[i] = (unsigned short)(d->resp[d->resp_pos] | (d->resp_pos + 1 < d->piece_end ? d->resp[d->resp_pos + 1] << 8 : 0));
            d->resp_pos += d->resp_pos + 1 < d->piece_end ? 2 : 1;
        }
        if (d->resp_pos == d->piece_end)
            pkt_piece(d);
        return;
    }
    if (port != 0x1F0 || !d->present || d->dir != 1 || !(d->status & 0x08) || d->busy) {
        printf("FAIL model: data read with no data to give (dir %d status %02x)\n", d->dir, d->status);
        fails++;
        return;
    }
    for (i = 0; i < count; i++) {
        if (d->wpos >= d->words) {
            printf("FAIL model: read past the block\n");
            fails++;
            return;
        }
        buf[i] = (unsigned short)(d->block[2 * d->wpos] | d->block[2 * d->wpos + 1] << 8);
        d->wpos++;
    }
    if (d->wpos == d->words) {
        if (d->last_cmd == 0xEC || d->last_cmd == 0xA1) {
            d->dir = 0;
            d->status = 0x50;
        } else {
            next_block(d);
        }
    }
}

static void m_out16s(void *ctx, unsigned port, const unsigned short *buf, unsigned count)
{
    struct drive *d = selected();
    unsigned i;

    hold_outside += hold_depth != 1;

    (void)ctx;
    ch.us += count;
    if (d->atapi && d->pkt == 1 && port == 0x1F0 && !d->busy) {
        for (i = 0; i < count && d->cdb_words < 6; i++, d->cdb_words++) {
            d->cdb[2 * d->cdb_words] = (unsigned char)buf[i];
            d->cdb[2 * d->cdb_words + 1] = (unsigned char)(buf[i] >> 8);
        }
        if (i < count) {
            printf("FAIL model: more than the packet's 12 bytes\n");
            fails++;
        }
        if (d->cdb_words == 6) {
            d->busy = 4;
            pkt_run(d);
        }
        return;
    }
    if (port != 0x1F0 || !d->present || d->dir != 2 || !(d->status & 0x08) || d->busy) {
        printf("FAIL model: data written when none was asked for (dir %d status %02x)\n", d->dir, d->status);
        fails++;
        return;
    }
    for (i = 0; i < count; i++) {
        if (d->wpos >= d->words) {
            printf("FAIL model: write past the block\n");
            fails++;
            return;
        }
        d->block[2 * d->wpos] = (unsigned char)buf[i];
        d->block[2 * d->wpos + 1] = (unsigned char)(buf[i] >> 8);
        d->wpos++;
    }
    if (d->wpos == d->words) {
        unsigned k, n = d->words / 256;

        for (k = 0; k < n; k++) {
            if (d->at + k == d->bad_write) {
                d->status = 0x51 | 0x20;
                d->err = 0x10;
                d->dir = 0;
                d->busy = 2;
                raise_irq();
                return;
            }
            sector_put(d, d->at + k, d->block + k * 512);
        }
        d->at += n;
        next_block(d);
    }
}

static unsigned long m_ms(void *ctx)
{
    (void)ctx;
    ch.us += 1;
    return (unsigned long)(ch.us / 1000);
}

static const struct ata_io io = { m_in8, m_out8, m_in16s, m_out16s, m_ms, 0, m_hold };

static void setup(void)
{
    int i, k;

    for (i = 0; i < 2; i++)
        for (k = 0; k < ch.d[i].nw; k++)
            free(ch.d[i].wdata[k]);
    memset(&ch, 0, sizeof ch);
    ch.d[0].bad_read = ch.d[0].bad_write = ch.d[1].bad_read = ch.d[1].bad_write = ~0ULL;
}

static void ata_disk(struct drive *d, const char *model, u64 sectors, int lba, int lba48, unsigned mult)
{
    d->present = 1;
    d->lba = lba;
    d->lba48 = lba48;
    d->sectors = sectors;
    d->max_mult = mult;
    if (!lba) {
        d->heads = 16;
        d->spt = 63;
        d->cyls = (unsigned)(sectors / (16 * 63));
        d->sectors = (u64)d->cyls * 16 * 63;
    } else {
        d->heads = 16;
        d->spt = 63;
        d->cyls = sectors / (16 * 63) > 16383 ? 16383 : (unsigned)(sectors / (16 * 63));
    }
    strcpy(d->model, model);
}

static struct ata_chan chan;
static struct ata_dev dev[2];

static int probe(void)
{
    chan.io = &io;
    chan.base = 0x1F0;
    chan.ctl = 0x3F6;
    return ata_probe(&chan, dev, ATA_PROBE_OWN);
}

/* As under hosted GLOS: no reset, the BIOS's settings kept. */
static int probe_hosted(void)
{
    chan.io = &io;
    chan.base = 0x1F0;
    chan.ctl = 0x3F6;
    return ata_probe(&chan, dev, 0);
}

/* The channel as the BIOS left it: the interrupt allowed, none pending. */
static void left_clean(const char *what)
{
    CHECK(!ch.nien, "%s: nIEN left set", what);
    CHECK(!ch.intrq, "%s: an interrupt left pending", what);
}

/* ---- the tests */

static void cdb(unsigned char c[12], unsigned op, unsigned long lba, unsigned n)
{
    memset(c, 0, 12);
    c[0] = (unsigned char)op;
    c[2] = (unsigned char)(lba >> 24), c[3] = (unsigned char)(lba >> 16), c[4] = (unsigned char)(lba >> 8);
    c[5] = (unsigned char)lba, c[7] = (unsigned char)(n >> 8), c[8] = (unsigned char)n;
}

/* ATAPI packets (N4): a CD-ROM drive as device 1 beside a disk. */
static void t_atapi(void)
{
    struct drive *cd;
    unsigned char c[12], b[3 * 2048 + 64];
    unsigned long got, i;
    int r, ok;

    setup();
    ata_disk(&ch.d[0], "DISK", 100000, 1, 0, 16);
    cd = &ch.d[1];
    cd->present = 1, cd->atapi = 1, cd->media = 1, cd->changed = 1, cd->disc = 1000;
    strcpy(cd->model, "CD-ROM");
    CHECK(probe() == ATA_OK && dev[1].present && dev[1].atapi && !dev[0].atapi, "the CD-ROM drive found");
    cdb(c, 0x00, 0, 0);
    CHECK(ata_packet(&dev[0], c, b, 0, &got) == ATA_ENOTSUP, "a packet to a disk refused");
    r = ata_packet(&dev[1], c, b, 0, &got);
    CHECK(r == ATA_EDEVICE && dev[1].err >> 4 == 6, "the disc's change first: UNIT ATTENTION (r %d err %02x)", r,
          dev[1].err);
    memset(c, 0, 12), c[0] = 0x03, c[4] = 18;
    r = ata_packet(&dev[1], c, b, 18, &got);
    CHECK(r == ATA_OK && got == 18 && b[0] == 0x70 && b[2] == 6 && b[12] == 0x28, "REQUEST SENSE: %d %lu %02x/%02x", r,
          got, b[2], b[12]);
    cdb(c, 0x00, 0, 0);
    CHECK(ata_packet(&dev[1], c, b, 0, &got) == ATA_OK, "TEST UNIT READY: ready");
    cdb(c, 0x25, 0, 0);
    r = ata_packet(&dev[1], c, b, 8, &got);
    CHECK(r == ATA_OK && got == 8 && be32(b) == 999 && be32(b + 4) == 2048, "READ CAPACITY: %lu %lu", be32(b),
          be32(b + 4));
    cdb(c, 0x28, 5, 3);
    cd->pieces = 0;
    r = ata_packet(&dev[1], c, b, 3 * 2048, &got);
    for (ok = 1, i = 0; i < 3 * 2048; i++)
        ok &= b[i] == pattern(5 + i / 2048, (unsigned)(i % 2048));
    CHECK(r == ATA_OK && got == 3 * 2048 && ok && cd->pieces == 1, "READ(10) 3 sectors: %d %lu ok %d pieces %u", r, got,
          ok, cd->pieces);
    cd->piece_max = 2048, cd->pieces = 0;       /* (a drive that gives a sector at a time) */
    memset(b, 0, sizeof b);
    r = ata_packet(&dev[1], c, b, 3 * 2048, &got);
    for (ok = 1, i = 0; i < 3 * 2048; i++)
        ok &= b[i] == pattern(5 + i / 2048, (unsigned)(i % 2048));
    CHECK(r == ATA_OK && got == 3 * 2048 && ok && cd->pieces == 3, "... a sector a piece: %d %lu ok %d pieces %u", r,
          got, ok, cd->pieces);
    cd->piece_max = 0;
    cdb(c, 0x28, 1, 2);                         /* (more than the buffer: the rest read and dropped) */
    r = ata_packet(&dev[1], c, b, 2048, &got);
    for (ok = 1, i = 0; i < 2048; i++)
        ok &= b[i] == pattern(1, (unsigned)i);
    CHECK(r == ATA_OK && got == 2048 && ok && !cd->pkt, "an answer longer than asked for: %d %lu", r, got);
    cdb(c, 0x00, 0, 0);
    CHECK(ata_packet(&dev[1], c, b, 0, &got) == ATA_OK, "... the drive left done");
    memset(c, 0, 12), c[0] = 0x03, c[4] = 18;
    r = ata_packet(&dev[1], c, b, 17, &got);
    CHECK(r == ATA_OK && got == 17, "an odd length: %d %lu", r, got);
    cdb(c, 0x28, 999, 2);
    r = ata_packet(&dev[1], c, b, 2 * 2048, &got);
    CHECK(r == ATA_EDEVICE && dev[1].err >> 4 == 5, "READ past the disc's end: ILLEGAL REQUEST (%d %02x)", r, dev[1].err);
    memset(c, 0, 12), c[0] = 0x03, c[4] = 18;
    CHECK(ata_packet(&dev[1], c, b, 18, &got) == ATA_OK && b[12] == 0x21, "... its sense: LBA out of range (%02x)", b[12]);
    memset(c, 0, 12), c[0] = 0x43, c[8] = 20;
    r = ata_packet(&dev[1], c, b, 20, &got);
    CHECK(r == ATA_OK && got == 20 && b[2] == 1 && b[3] == 1 && b[14] == 0xAA && be32(b + 16) == 1000,
          "READ TOC: %d %lu", r, got);
    memset(c, 0, 12), c[0] = 0x43, c[8] = 12;
    r = ata_packet(&dev[1], c, b, 12, &got);
    CHECK(r == ATA_OK && got == 12, "READ TOC cut to its allocation: %d %lu", r, got);
    memset(c, 0, 12), c[0] = 0x1B, c[4] = 2;
    CHECK(ata_packet(&dev[1], c, b, 0, &got) == ATA_OK && !cd->media, "ejected");
    cdb(c, 0x00, 0, 0);
    r = ata_packet(&dev[1], c, b, 0, &got);
    CHECK(r == ATA_EDEVICE && dev[1].err >> 4 == 2, "no disc: NOT READY (%d %02x)", r, dev[1].err);
    memset(c, 0, 12), c[0] = 0xFF;
    r = ata_packet(&dev[1], c, b, 0, &got);
    CHECK(r == ATA_EDEVICE && dev[1].err >> 4 == 5, "an opcode it hasn't got: ILLEGAL REQUEST (%d %02x)", r, dev[1].err);
    left_clean("atapi");
    CHECK(hold_depth == 0, "ata_packet's hold given back");
    cd->stuck = 1;
    cdb(c, 0x00, 0, 0);
    CHECK(ata_packet(&dev[1], c, b, 0, &got) == ATA_ETIMEOUT, "a drive that stays busy: ATA_ETIMEOUT");
    cd->stuck = 0;
}

static void t_probe(void)
{
    int r;

    setup();
    ch.floating = 1;
    CHECK(probe() == ATA_ENODEV, "floating bus: not ATA_ENODEV");

    setup();
    ata_disk(&ch.d[0], "GLOS TEST DISK A", 1000000, 1, 0, 16);
    r = probe();
    CHECK(r == ATA_OK && dev[0].present && !dev[1].present, "master only: %d %d %d", r, dev[0].present,
          dev[1].present);
    CHECK(!strcmp(dev[0].model, "GLOS TEST DISK A"), "model \"%s\"", dev[0].model);
    CHECK(!strcmp(dev[0].serial, "SERIAL123"), "serial \"%s\"", dev[0].serial);
    CHECK(dev[0].lba && !dev[0].lba48 && dev[0].sectors_lo == 1000000 && dev[0].multiple == 16,
          "master: lba %d lba48 %d sectors %lu multiple %u", dev[0].lba, dev[0].lba48, dev[0].sectors_lo,
          dev[0].multiple);
    left_clean("probe");

    setup();
    ata_disk(&ch.d[0], "MASTER", 50000, 1, 0, 8);
    ata_disk(&ch.d[1], "SLAVE", 70000, 1, 0, 0);
    r = probe();
    CHECK(r == ATA_OK && dev[0].present && dev[1].present && !strcmp(dev[1].model, "SLAVE") &&
          dev[1].sectors_lo == 70000 && dev[1].multiple == 0 && dev[0].multiple == 8,
          "two disks: %d %d %d \"%s\" %lu mult %u/%u", r, dev[0].present, dev[1].present, dev[1].model,
          dev[1].sectors_lo, dev[0].multiple, dev[1].multiple);

    setup();
    ata_disk(&ch.d[0], "MASTER", 50000, 1, 0, 16);
    ch.d[1].present = ch.d[1].atapi = 1;
    strcpy(ch.d[1].model, "GLOS CD-ROM");
    r = probe();
    CHECK(r == ATA_OK && dev[0].present && dev[1].present && dev[1].atapi && !strcmp(dev[1].model, "GLOS CD-ROM"),
          "disk + CD: %d %d %d atapi %d \"%s\"", r, dev[0].present, dev[1].present, dev[1].atapi, dev[1].model);
    CHECK(ata_read(&dev[1], 0, 0, 1, malloc(512), 0) == ATA_ENOTSUP, "ATAPI read: not ATA_ENOTSUP");

    setup();
    ch.d[0].present = ch.d[0].atapi = 1;
    strcpy(ch.d[0].model, "CD ALONE");
    r = probe();
    CHECK(r == ATA_OK && dev[0].present && dev[0].atapi && !dev[1].present, "CD alone: %d %d %d", r,
          dev[0].present, dev[1].present);

    setup();
    ata_disk(&ch.d[1], "SLAVE ONLY", 9000, 1, 0, 16);
    r = probe();
    CHECK(r == ATA_OK && !dev[0].present && dev[1].present, "slave alone: %d %d %d", r, dev[0].present,
          dev[1].present);

    setup();
    ata_disk(&ch.d[0], "OLD CHS DISK", 16 * 63 * 600, 0, 0, 0);
    r = probe();
    CHECK(r == ATA_OK && dev[0].present && !dev[0].lba && dev[0].cyls == 600 && dev[0].heads == 16 &&
          dev[0].spt == 63 && dev[0].sectors_lo == 16UL * 63 * 600, "CHS disk: lba %d %u/%u/%u %lu", dev[0].lba,
          dev[0].cyls, dev[0].heads, dev[0].spt, dev[0].sectors_lo);

    setup();
    ata_disk(&ch.d[0], "BIG", 1ULL << 33, 1, 1, 16);
    r = probe();
    CHECK(r == ATA_OK && dev[0].lba48 && dev[0].sectors_lo == 0 && dev[0].sectors_hi == 2, "LBA48: %d %d %lu:%lu",
          r, dev[0].lba48, dev[0].sectors_hi, dev[0].sectors_lo);
}

/* n sectors at lba checked against the pattern (or `want`). */
static int same_as_pattern(const unsigned char *buf, u64 lba, unsigned long n)
{
    unsigned long s;
    unsigned i;

    for (s = 0; s < n; s++)
        for (i = 0; i < 512; i++)
            if (buf[s * 512 + i] != pattern(lba + s, i))
                return 0;
    return 1;
}

static void t_rw(const char *what, u64 sectors, int lba, int lba48, unsigned mult)
{
    static unsigned char buf[700 * 512], back[700 * 512];
    unsigned long done, i;
    unsigned seed = 12345;
    int r, k;

    setup();
    ata_disk(&ch.d[0], what, sectors, lba, lba48, mult);
    if (probe() != ATA_OK || !dev[0].present) {
        CHECK(0, "%s: probe", what);
        return;
    }
    for (k = 0; k < 60; k++) {
        unsigned long n, at;

        seed = seed * 1103515245 + 12345;
        n = 1 + (seed >> 8) % (k % 3 ? 40 : 600);
        seed = seed * 1103515245 + 12345;
        at = (seed >> 4) % (unsigned long)(ch.d[0].sectors - n);
        r = ata_read(&dev[0], at, 0, n, buf, &done);
        CHECK(r == ATA_OK && done == n, "%s: read %lu at %lu: %d, %lu done", what, n, at, r, done);
        if (!r && ch.d[0].nw == 0)
            CHECK(same_as_pattern(buf, at, n), "%s: read %lu at %lu: wrong data", what, n, at);
        for (i = 0; i < n * 512; i++)
            buf[i] = (unsigned char)(i * 7 + k);
        r = ata_write(&dev[0], at, 0, n, buf, &done);
        CHECK(r == ATA_OK && done == n, "%s: write %lu at %lu: %d, %lu done", what, n, at, r, done);
        r = ata_read(&dev[0], at, 0, n, back, &done);
        CHECK(r == ATA_OK && !memcmp(buf, back, n * 512), "%s: %lu at %lu not as written (%d)", what, n, at, r);
        left_clean(what);
    }
    CHECK(ata_read(&dev[0], (unsigned long)ch.d[0].sectors - 1, 0, 2, buf, &done) == ATA_ERANGE,
          "%s: past the end: not ATA_ERANGE", what);
    CHECK(ata_read(&dev[0], (unsigned long)ch.d[0].sectors - 1, 0, 1, buf, &done) == ATA_OK,
          "%s: the last sector", what);
    CHECK(ata_flush(&dev[0]) == ATA_OK && ch.d[0].last_cmd == (lba48 ? 0xEA : 0xE7), "%s: flush (%02x)", what,
          ch.d[0].last_cmd);
    left_clean(what);
}

static void t_lba48(void)
{
    static unsigned char buf[1000 * 512];
    unsigned long done;
    int r;

    setup();
    ata_disk(&ch.d[0], "BIG", (1ULL << 33) + 5000, 1, 1, 16);
    if (probe() != ATA_OK) {
        CHECK(0, "probe");
        return;
    }
    /* Below 2^28 and short: the 28-bit commands. */
    r = ata_read(&dev[0], 1000, 0, 100, buf, &done);
    CHECK(r == ATA_OK && ch.d[0].ext_used == 0 && same_as_pattern(buf, 1000, 100), "low read: %d ext %d", r,
          ch.d[0].ext_used);
    /* Across 2^28: 48-bit. */
    r = ata_read(&dev[0], 0x0FFFFF00UL, 0, 1000, buf, &done);
    CHECK(r == ATA_OK && done == 1000 && ch.d[0].ext_used > 0 && same_as_pattern(buf, 0x0FFFFF00ULL, 1000),
          "across 2^28: %d %lu ext %d", r, done, ch.d[0].ext_used);
    /* Past 2^32: the upper half. */
    r = ata_read(&dev[0], 0x100UL, 2, 300, buf, &done);
    CHECK(r == ATA_OK && done == 300 && same_as_pattern(buf, (2ULL << 32) + 0x100, 300), "past 2^32: %d %lu", r,
          done);
    /* Across 2^32 inside one call. */
    r = ata_read(&dev[0], 0xFFFFFFF0UL, 0, 40, buf, &done);
    CHECK(r == ATA_OK && same_as_pattern(buf, 0xFFFFFFF0ULL, 40), "across 2^32: %d", r);
    memset(buf, 0xA5, 512);
    r = ata_write(&dev[0], 0x200UL, 2, 1, buf, &done);
    CHECK(r == ATA_OK && ch.d[0].nw == 1 && ch.d[0].wlba[0] == (2ULL << 32) + 0x200, "write past 2^32: %d", r);
    CHECK(ata_read(&dev[0], 5000, 2, 1, buf, &done) == ATA_ERANGE, "past the end: not ATA_ERANGE");
    left_clean("lba48");
}

static void t_faults(void)
{
    static unsigned char buf[600 * 512];
    unsigned long done;
    u64 t0;
    int r;

    /* A sector that won't read, in the third block of 16: two blocks had. */
    setup();
    ata_disk(&ch.d[0], "BAD", 100000, 1, 0, 16);
    probe();
    ch.d[0].bad_read = 5040;
    r = ata_read(&dev[0], 5000, 0, 60, buf, &done);
    CHECK(r == ATA_EDEVICE && done == 32 && dev[0].err == 0x40, "bad read: %d done %lu err %02x", r, done,
          dev[0].err);
    left_clean("bad read");
    r = ata_read(&dev[0], 5000, 0, 30, buf, &done);
    CHECK(r == ATA_OK && done == 30, "after it: %d", r);

    /* A failing write: what came before it taken. */
    setup();
    ata_disk(&ch.d[0], "BADW", 100000, 1, 0, 0);
    probe();
    ch.d[0].bad_write = 2010;
    r = ata_write(&dev[0], 2000, 0, 20, buf, &done);
    CHECK(r == ATA_EDEVICE && done == 10, "bad write: %d done %lu", r, done);
    left_clean("bad write");

    /* A drive that stays busy: the time allowed, then ATA_ETIMEOUT. */
    setup();
    ata_disk(&ch.d[0], "STUCK", 100000, 1, 0, 16);
    probe();
    ch.d[0].stuck = 1;
    t0 = ch.us;
    r = ata_read(&dev[0], 0, 0, 1, buf, &done);
    CHECK(r == ATA_ETIMEOUT, "stuck: %d", r);
    CHECK(ch.us - t0 >= (ATA_T_CMD - 1) * 1000 && ch.us - t0 < ATA_T_CMD * 3000, "stuck: %llu us", ch.us - t0);
    CHECK(!ch.nien, "stuck: nIEN left set");

    /* Verify: no data moves; a bad sector's place told. */
    setup();
    ata_disk(&ch.d[0], "VERIFY", 100000, 1, 0, 16);
    probe();
    ch.d[0].cmds = 0;
    r = ata_verify(&dev[0], 1000, 0, 600, &done);
    CHECK(r == ATA_OK && done == 600 && ch.d[0].cmds == 3 && ch.d[0].last_cmd == 0x40, "verify: %d %lu cmds %d", r,
          done, ch.d[0].cmds);
    ch.d[0].bad_read = 1300;
    r = ata_verify(&dev[0], 1000, 0, 600, &done);
    CHECK(r == ATA_EDEVICE && done == 300 && dev[0].err == 0x40, "verify, bad at 1300: %d %lu %02x", r, done,
          dev[0].err);
    left_clean("verify");

    /* More than one command takes (28-bit: 256 at most): 256 + 256 + 88. */
    setup();
    ata_disk(&ch.d[0], "SPLIT", 100000, 1, 0, 16);
    probe();
    ch.d[0].cmds = 0;
    r = ata_read(&dev[0], 10, 0, 600, buf, &done);
    CHECK(r == ATA_OK && done == 600 && ch.d[0].cmds == 3 && same_as_pattern(buf, 10, 600),
          "600 sectors: %d done %lu cmds %d", r, done, ch.d[0].cmds);
}

/* Hosted: the drives as the BIOS left them (its multiple of 8, a CD on the slave), no reset, nothing changed. */
static void t_hosted(void)
{
    static unsigned char buf[100 * 512];
    unsigned long done;
    int r;

    setup();
    ata_disk(&ch.d[0], "BIOS DISK", 100000, 1, 0, 16);
    ch.d[0].mult = 8;                           /* (the BIOS's SET MULTIPLE) */
    ch.d[1].present = ch.d[1].atapi = 1;
    strcpy(ch.d[1].model, "BIOS CD");
    ch.d[0].busy = ch.d[1].busy = 0;
    ch.d[0].status = 0x50;
    r = probe_hosted();
    CHECK(r == ATA_OK && dev[0].present && !dev[0].atapi && dev[1].present && dev[1].atapi,
          "hosted: %d %d/%d %d/%d", r, dev[0].present, dev[0].atapi, dev[1].present, dev[1].atapi);
    CHECK(dev[0].multiple == 8 && ch.d[0].mult == 8, "hosted: multiple %u, the drive's %u", dev[0].multiple,
          ch.d[0].mult);
    CHECK(ch.d[0].busy == 0 && ch.d[0].err != 1, "hosted: the channel was reset");
    r = ata_read(&dev[0], 500, 0, 50, buf, &done);
    CHECK(r == ATA_OK && same_as_pattern(buf, 500, 50), "hosted read: %d", r);
    left_clean("hosted");

    setup();
    ata_disk(&ch.d[0], "NO MULTIPLE", 100000, 1, 0, 16);
    ch.d[0].status = 0x50;
    r = probe_hosted();
    CHECK(r == ATA_OK && dev[0].present && dev[0].multiple == 0 && ch.d[0].mult == 0, "hosted, single: %d %u %u", r,
          dev[0].multiple, ch.d[0].mult);
    r = ata_read(&dev[0], 500, 0, 50, buf, &done);
    CHECK(r == ATA_OK && same_as_pattern(buf, 500, 50), "hosted single read: %d", r);
}

int main(void)
{
    t_probe();
    t_hosted();
    t_atapi();
    t_rw("LBA28 MULTIPLE", 200000, 1, 0, 16);
    t_rw("LBA28 SINGLE", 200000, 1, 0, 0);
    t_rw("LBA28 MULTIPLE 2", 50000, 1, 0, 2);
    t_rw("CHS", 16UL * 63 * 300, 0, 0, 4);
    t_rw("LBA48", 300000, 1, 1, 16);
    t_lba48();
    t_faults();
    CHECK(hold_calls > 100 && !hold_depth && !hold_outside, "the lock: %d calls, depth %d at the end, %d accesses outside it",
          hold_calls, hold_depth, hold_outside);
    printf("ata_test: %d failures\n", fails);
    return fails != 0;
}
