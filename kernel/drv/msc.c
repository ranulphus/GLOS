/* USB mass storage, Bulk-Only Transport and the SCSI commands it carries (docs/v1/usb-storage.md, phase G's U1).
 *
 * Sources (clean room): USB Mass Storage Class, Bulk-Only Transport rev 1.0 (the CBW and the CSW, the reset
 * recovery, the thirteen cases of host and device expectations, reduced here to: a stall in the data stage is
 * cleared and the status read; a status that won't come, a phase error or a CSW that isn't valid ends in the reset
 * recovery); SPC-2 and SBC-2 (INQUIRY, TEST UNIT READY, REQUEST SENSE, READ CAPACITY(10), READ(10), WRITE(10)).
 *
 * A command: its CBW (31 bytes: "USBC", a tag, the data's length, the direction, LUN 0, the command block) on the
 * OUT pipe; its data, if any, on the pipe of its direction; its CSW (13 bytes: "USBS", the same tag, the residue,
 * the status) on the IN pipe. Status 1, the command failed: REQUEST SENSE says why (a unit attention when a stick
 * is first used, a medium not present in an empty card reader). The controller is reached only through msc_ops.
 *     GLOS-MSC ready vendor=... product=... blocks=N size=512 removable=1
 *     GLOS-MSC refused why=...                     (msc_start's -1: the controller's driver logs it with where) */
#include <string.h>
#include "msc.h"

#define CBW_SIG 0x43425355u             /* "USBC" */
#define CSW_SIG 0x53425355u             /* "USBS" */
#define RUN     128u                    /* blocks a READ(10) or WRITE(10) at most: 64 KB */

static void le32(u8 *p, u32 v) { p[0] = (u8)v, p[1] = (u8)(v >> 8), p[2] = (u8)(v >> 16), p[3] = (u8)(v >> 24); }
static u32 rle32(const u8 *p) { return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u32 rbe32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

/* The reset recovery: the class's reset, then both pipes' halts cleared. */
static void recover(struct msc *m)
{
    m->resets++;
    m->ops->reset(m->ctx);
    m->ops->clear_halt(m->ctx, 1);
    m->ops->clear_halt(m->ctx, 0);
}

/* The CSW: 0 with *status, or -1 (none, or not valid: the recovery done). A stall at the first try is cleared and
   the CSW asked for again, once. */
static int csw(struct msc *m, u32 tag, u32 *status)
{
    u8 b[13];
    u32 got = 0;
    int r = m->ops->bulk(m->ctx, 1, b, sizeof b, &got);
    if (r == MSC_STALL) {
        m->ops->clear_halt(m->ctx, 1);
        r = m->ops->bulk(m->ctx, 1, b, sizeof b, &got);
    }
    if (r != 0 || got != 13 || rle32(b) != CSW_SIG || rle32(b + 4) != tag || b[12] > 2) {
        recover(m);
        return -1;
    }
    *status = b[12];
    return 0;
}

static int transport(struct msc *m, const u8 *cdb, u32 cdb_len, int in, void *buf, u32 len, u32 *got)
{
    u8 cbw[31];
    u32 tag = ++m->tag, moved = 0, status = 0, sent = 0;
    int r;
    if (cdb_len < 1 || cdb_len > 16)
        return -1;
    memset(cbw, 0, sizeof cbw);
    le32(cbw, CBW_SIG);
    le32(cbw + 4, tag);
    le32(cbw + 8, len);
    cbw[12] = in ? 0x80 : 0;
    cbw[14] = (u8)cdb_len;
    memcpy(cbw + 15, cdb, cdb_len);
    m->commands++;
    if (m->ops->bulk(m->ctx, 0, cbw, sizeof cbw, &sent) != 0 || sent != sizeof cbw) {
        recover(m);
        return -1;
    }
    if (len) {
        r = m->ops->bulk(m->ctx, in, buf, len, &moved);
        if (r == MSC_STALL)
            m->ops->clear_halt(m->ctx, in);     /* (the device ended the data early: its status says how it went) */
        else if (r != 0) {
            recover(m);
            return -1;
        }
    }
    if (csw(m, tag, &status) != 0)
        return -1;
    if (got)
        *got = moved;
    if (status == 2) {                          /* a phase error: the device is lost; the recovery */
        recover(m);
        return -1;
    }
    return status ? 1 : 0;
}

int msc_command(struct msc *m, const u8 *cdb, u32 cdb_len, int in, void *buf, u32 len, u32 *got)
{
    int r = transport(m, cdb, cdb_len, in, buf, len, got);
    if (r == 1) {                               /* failed: what the device says of it */
        static const u8 sense[6] = { 0x03, 0, 0, 0, 18, 0 };
        u8 s[18];
        u32 n = 0;
        m->failures++;
        m->sense_key = 0, m->asc = 0, m->ascq = 0;
        memset(s, 0, sizeof s);
        if (transport(m, sense, sizeof sense, 1, s, sizeof s, &n) == 0 && n >= 14)
            m->sense_key = s[2] & 0x0F, m->asc = s[12], m->ascq = s[13];
    }
    return r;
}

static void trim(char *out, const u8 *in, u32 n)
{
    u32 i;
    for (i = 0; i < n; i++)
        out[i] = (char)(in[i] >= 0x20 && in[i] < 0x7F ? in[i] : ' ');
    while (n && out[n - 1] == ' ')
        n--;
    out[n] = 0;
}

int msc_start(struct msc *m, const char **why)
{
    static const u8 inquiry[6] = { 0x12, 0, 0, 0, 36, 0 }, tur[6] = { 0 };
    static const u8 capacity[10] = { 0x25 };
    u8 b[36];
    u32 got = 0, waited = 0;
    int r = -1, tries;
    for (tries = 0; tries < 3 && r != 0; tries++) {
        memset(b, 0, sizeof b);
        r = msc_command(m, inquiry, sizeof inquiry, 1, b, sizeof b, &got);
    }
    if (r != 0 || got < 36) {
        *why = "no-inquiry";
        return -1;
    }
    if ((b[0] & 0x1F) != 0) {                   /* (0: a direct-access block device) */
        *why = "not-a-disk";
        return -1;
    }
    m->removable = (b[1] & 0x80) != 0;
    trim(m->vendor, b + 8, 8);
    trim(m->product, b + 16, 16);
    trim(m->rev, b + 32, 4);
    for (;;) {                                  /* ready: a unit attention (its first command) or not ready yet waited out */
        r = msc_command(m, tur, sizeof tur, 0, 0, 0, 0);
        if (r == 0)
            break;
        if (r < 0 || waited >= 5000 || (m->sense_key == 2 && m->asc == 0x3A)) {
            *why = r < 0 ? "transport" : m->sense_key == 2 && m->asc == 0x3A ? "no-medium" : "not-ready";
            return -1;
        }
        if (m->sense_key != 6) {                /* (a unit attention is answered at once; anything else, a pause) */
            m->ops->sleep(m->ctx, 100);
            waited += 100;
        }
    }
    for (tries = 0, r = -1; tries < 2 && r != 0; tries++) {
        memset(b, 0, 8);
        r = msc_command(m, capacity, sizeof capacity, 1, b, 8, &got);
    }
    if (r != 0 || got < 8) {
        *why = "no-capacity";
        return -1;
    }
    m->blocks = rbe32(b) + 1;
    m->block_size = rbe32(b + 4);
    if (m->block_size != 512) {
        *why = "not-512-byte-blocks";
        return -1;
    }
    return 0;
}

static int rw(struct msc *m, u8 op, u32 lba, void *buf, u32 count)
{
    while (count) {
        u32 n = count < RUN ? count : RUN, got = 0;
        u8 cdb[10];
        int r, tries;
        memset(cdb, 0, sizeof cdb);
        cdb[0] = op;
        cdb[2] = (u8)(lba >> 24), cdb[3] = (u8)(lba >> 16), cdb[4] = (u8)(lba >> 8), cdb[5] = (u8)lba;
        cdb[7] = (u8)(n >> 8), cdb[8] = (u8)n;
        if (lba >= m->blocks || n > m->blocks - lba)
            return -1;
        for (tries = 0, r = -1; tries < 3 && (r != 0 || got != n * 512); tries++) {
            got = 0;
            r = msc_command(m, cdb, sizeof cdb, op == 0x28, buf, n * 512, &got);
            if (r == 1 && m->sense_key != 6)    /* (refused for a reason other than a unit attention: no retry) */
                return -1;
        }
        if (r != 0 || got != n * 512)
            return -1;
        lba += n, count -= n;
        buf = (u8 *)buf + n * 512;
    }
    return 0;
}

int msc_read(struct msc *m, u32 lba, void *buf, u32 count) { return rw(m, 0x28, lba, buf, count); }

int msc_write(struct msc *m, u32 lba, const void *buf, u32 count) { return rw(m, 0x2A, lba, (void *)buf, count); }
