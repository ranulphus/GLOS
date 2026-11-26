/* Host test for USB mass storage's Bulk-Only Transport and SCSI commands (kernel/drv/msc.c): a fake device behind
 * the same calls a controller's driver gives (a CBW on OUT, the data, a CSW on IN; a RAM disk of 64 blocks), and
 * faults put in: its first TEST UNIT READY a unit attention; a stall in a READ's data stage (a short read, retried);
 * a phase error; a CSW with the wrong tag; a device that never answers; a card reader with no medium; 2 KB blocks.
 * Each fault's recovery (the class reset and both halts cleared) counted. */
#include <stdio.h>
#include <string.h>
#include "msc.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static struct {
    enum { S_CBW, S_DATA, S_CSW } state;
    u8 cdb[16];
    u32 tag, len, status;
    int in, halted[2], ua_pending, dead, no_medium, block2048;
    int stall_data, phase_error, wrong_tag, resets;
    u8 sense[3];
    u8 disk[64 * 512];
} dev;

static void sense(u8 key, u8 asc) { dev.sense[0] = key, dev.sense[1] = asc, dev.sense[2] = 0; }
static u32 rd32(const u8 *p) { return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

/* The command's data (IN) or where it goes (OUT): into buf, *n bytes; its status set. */
static void execute(u8 *buf, u32 len, u32 *n, int out_data)
{
    u32 lba = be32(dev.cdb + 2), count = (u32)dev.cdb[7] << 8 | dev.cdb[8];
    *n = 0;
    dev.status = 0;
    switch (dev.cdb[0]) {
    case 0x12:                                  /* INQUIRY */
        memset(buf, 0, len);
        buf[1] = 0x80;
        memcpy(buf + 8, "GLOSTEST", 8);
        memcpy(buf + 16, "Fake stick      ", 16);
        memcpy(buf + 32, "1.00", 4);
        *n = len < 36 ? len : 36;
        break;
    case 0x00:                                  /* TEST UNIT READY */
        if (dev.no_medium)
            sense(2, 0x3A), dev.status = 1;
        else if (dev.ua_pending)
            dev.ua_pending = 0, sense(6, 0x28), dev.status = 1;
        break;
    case 0x03:                                  /* REQUEST SENSE */
        memset(buf, 0, len);
        buf[0] = 0x70, buf[2] = dev.sense[0], buf[7] = 10, buf[12] = dev.sense[1], buf[13] = dev.sense[2];
        *n = len < 18 ? len : 18;
        break;
    case 0x25:                                  /* READ CAPACITY(10) */
        buf[0] = 0, buf[1] = 0, buf[2] = 0, buf[3] = dev.block2048 ? 15 : 63;
        buf[4] = 0, buf[5] = 0, buf[6] = dev.block2048 ? 8 : 2, buf[7] = 0;
        *n = 8;
        break;
    case 0x28:                                  /* READ(10) */
        if (lba + count > 64) {
            sense(5, 0x21), dev.status = 1;
            break;
        }
        memcpy(buf, dev.disk + lba * 512, count * 512);
        *n = count * 512;
        break;
    case 0x2A:                                  /* WRITE(10) */
        if (out_data)
            memcpy(dev.disk + lba * 512, buf, count * 512);
        *n = count * 512;
        break;
    default:
        sense(5, 0x20), dev.status = 1;
    }
}

static int bulk(void *ctx, int in, void *buf, u32 len, u32 *got)
{
    u8 *b = buf;
    (void)ctx;
    *got = 0;
    if (dev.dead)
        return -1;
    if (dev.halted[in])
        return MSC_STALL;
    if (dev.state == S_CBW) {
        if (in || len != 31 || rd32(b) != 0x43425355u)
            return dev.halted[0] = 1, MSC_STALL;
        dev.tag = rd32(b + 4), dev.len = rd32(b + 8), dev.in = (b[12] & 0x80) != 0;
        memcpy(dev.cdb, b + 15, 16);
        *got = 31;
        dev.state = dev.len ? S_DATA : S_CSW;
        if (!dev.len) {
            u32 n;
            execute(0, 0, &n, 0);
        }
        return 0;
    }
    if (dev.state == S_DATA) {
        u32 n;
        if (in != dev.in)
            return -1;
        if (in && dev.cdb[0] == 0x28 && dev.stall_data) {   /* half of it, then a stall */
            dev.stall_data--;
            execute(b, len, &n, 0);
            *got = len / 2;
            dev.halted[1] = 1;
            dev.state = S_CSW;
            return MSC_STALL;
        }
        execute(b, len, &n, !in);
        *got = n;
        dev.state = S_CSW;
        return 0;
    }
    if (!in || len != 13)
        return -1;
    memset(b, 0, 13);
    b[0] = 'U', b[1] = 'S', b[2] = 'B', b[3] = 'S';
    {
        u32 t = dev.tag + (dev.wrong_tag ? 1 : 0);
        b[4] = (u8)t, b[5] = (u8)(t >> 8), b[6] = (u8)(t >> 16), b[7] = (u8)(t >> 24);
    }
    if (dev.wrong_tag)
        dev.wrong_tag--;
    b[12] = (u8)(dev.phase_error ? 2 : dev.status);
    if (dev.phase_error)
        dev.phase_error--;
    *got = 13;
    dev.state = S_CBW;
    return 0;
}

static int clear_halt(void *ctx, int in) { (void)ctx; dev.halted[in] = 0; return 0; }
static int reset(void *ctx) { (void)ctx; dev.resets++; dev.state = S_CBW; return 0; }
static void nap(void *ctx, u32 ms) { (void)ctx, (void)ms; }

static const struct msc_ops ops = { bulk, clear_halt, reset, nap };

static void fresh(struct msc *m)
{
    u32 i;
    memset(&dev.state, 0, sizeof dev - sizeof dev.disk);
    for (i = 0; i < sizeof dev.disk; i++)
        dev.disk[i] = (u8)(i * 7 + i / 512);
    memset(m, 0, sizeof *m);
    m->ops = &ops;
}

int main(void)
{
    struct msc m;
    const char *why = "";
    u8 buf[8 * 512], w[2 * 512];
    u32 i;

    fresh(&m);                                  /* a good stick, its first command a unit attention */
    dev.ua_pending = 1;
    CHECK(msc_start(&m, &why) == 0);
    CHECK(m.blocks == 64 && m.block_size == 512 && m.removable);
    CHECK(!strcmp(m.vendor, "GLOSTEST") && !strcmp(m.product, "Fake stick") && !strcmp(m.rev, "1.00"));
    CHECK(m.failures == 1 && m.sense_key == 6 && m.resets == 0);

    CHECK(msc_read(&m, 3, buf, 8) == 0 && !memcmp(buf, dev.disk + 3 * 512, sizeof buf));
    for (i = 0; i < sizeof w; i++)
        w[i] = (u8)(0xA5 ^ i);
    CHECK(msc_write(&m, 10, w, 2) == 0 && !memcmp(dev.disk + 10 * 512, w, sizeof w));
    CHECK(msc_read(&m, 63, buf, 2) == -1);      /* (past its end: refused before the device is asked) */

    dev.stall_data = 1;                         /* a stall in the data stage: a short read, done again */
    CHECK(msc_read(&m, 0, buf, 4) == 0 && !memcmp(buf, dev.disk, 4 * 512) && dev.resets == 0);

    dev.phase_error = 1;                        /* a phase error: the reset recovery, then again */
    CHECK(msc_read(&m, 1, buf, 1) == 0 && dev.resets == 1 && m.resets == 1);

    dev.wrong_tag = 1;                          /* a CSW with another tag: not valid, the recovery, again */
    CHECK(msc_read(&m, 2, buf, 1) == 0 && dev.resets == 2);

    {                                           /* a command it doesn't know: failed, its sense read */
        static const u8 odd[6] = { 0x5A };
        CHECK(msc_command(&m, odd, 6, 0, 0, 0, 0) == 1 && m.sense_key == 5 && m.asc == 0x20);
    }

    fresh(&m);                                  /* a device that never answers */
    dev.dead = 1;
    CHECK(msc_start(&m, &why) == -1 && !strcmp(why, "no-inquiry"));

    fresh(&m);                                  /* a card reader with no card */
    dev.no_medium = 1;
    CHECK(msc_start(&m, &why) == -1 && !strcmp(why, "no-medium"));

    fresh(&m);                                  /* 2 KB blocks: refused */
    dev.block2048 = 1;
    CHECK(msc_start(&m, &why) == -1 && !strcmp(why, "not-512-byte-blocks"));

    printf("msc_test: %d failures\n", fails);
    return fails != 0;
}
