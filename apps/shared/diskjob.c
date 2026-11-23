/* Disk Tools' long jobs (diskjob.h). Sectors go a run at a time (32, GLOS_FS_SECTORS' most; or what the caller
 * says: a floppy's track, so that each call is one turn of the disk); a run that fails is gone through again a
 * sector at a time, each tried three times. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diskjob.h"

#define RUN 32

static unsigned run_of(unsigned run) { return run >= 1 && run <= RUN ? run : RUN; }

static void bad_sector(struct diskjob_report *r, unsigned long lba)
{
    if (!r->bad)
        r->first_bad = lba;
    if (r->bad < 16)
        r->bad_list[r->bad] = lba;
    r->bad++;
}

/* A run read: whole, or sector by sector with what can't be read as zeros (counted). */
static void read_run(diskjob_io io, void *ctx, unsigned long lba, unsigned char *buf, unsigned count, struct diskjob_report *r)
{
    unsigned i, tries;
    if (io(ctx, lba, buf, count, 0) == 0)
        return;
    for (i = 0; i < count; i++) {
        for (tries = 0; tries < 3 && io(ctx, lba + i, buf + i * 512, 1, 0) != 0; tries++)
            ;
        if (tries == 3) {
            memset(buf + i * 512, 0, 512);
            bad_sector(r, lba + i);
        }
    }
}

int diskjob_to_file(diskjob_io io, void *ctx, unsigned long total, unsigned run, FILE *out, diskjob_tick tick, void *arg,
                    struct diskjob_report *r)
{
    static unsigned char buf[RUN * 512];        /* (a job at a time: the worker's) */
    unsigned long lba;
    memset(r, 0, sizeof *r);
    run = run_of(run);
    for (lba = 0; lba < total; lba += run) {
        unsigned count = total - lba < run ? (unsigned)(total - lba) : run;
        if (tick && tick(arg, lba, total)) {
            r->stopped = 1;
            return -1;
        }
        read_run(io, ctx, lba, buf, count, r);
        if (fwrite(buf, 512, count, out) != count) {
            snprintf(r->why, sizeof r->why, "the file could not be written (is its disk full?)");
            return -1;
        }
        r->done = lba + count;
    }
    if (tick)
        tick(arg, total, total);
    return 0;
}

int diskjob_from_file(diskjob_io io, void *ctx, unsigned long total, unsigned run, FILE *in, int verify, diskjob_tick tick,
                      void *arg, struct diskjob_report *r)
{
    static unsigned char buf[RUN * 512], back[RUN * 512];
    unsigned long lba, pass, size, unwritten = 0;
    unsigned i, tries;
    memset(r, 0, sizeof *r);
    run = run_of(run);
    /* (in sectors: total * 512 would not fit 32 bits for a disk of 4 GB) */
    if (fseek(in, 0, SEEK_END) != 0 || (size = (unsigned long)ftell(in)) % 512 != 0 || size / 512 != total) {
        snprintf(r->why, sizeof r->why, "the image is not the disk's size");
        return -1;
    }
    for (pass = 0; pass < (verify ? 2u : 1u); pass++) {         /* written; then read back and held against the file */
        if (pass)
            unwritten = r->bad;
        rewind(in);
        for (lba = 0; lba < total; lba += run) {
            unsigned count = total - lba < run ? (unsigned)(total - lba) : run;
            unsigned long at = verify ? (pass * total + lba) / 2 : lba;
            if (tick && tick(arg, at, total)) {
                r->stopped = 1;
                return -1;
            }
            if (fread(buf, 512, count, in) != count) {
                snprintf(r->why, sizeof r->why, "the image could not be read");
                return -1;
            }
            if (pass == 0) {
                if (io(ctx, lba, buf, count, 1) != 0)
                    for (i = 0; i < count; i++) {
                        for (tries = 0; tries < 3 && io(ctx, lba + i, buf + i * 512, 1, 1) != 0; tries++)
                            ;
                        if (tries == 3)
                            bad_sector(r, lba + i);
                    }
            } else {
                struct diskjob_report unread;
                memset(&unread, 0, sizeof unread);
                read_run(io, ctx, lba, back, count, &unread);
                for (i = 0; i < count; i++)
                    if (memcmp(buf + i * 512, back + i * 512, 512)) {
                        /* (one the writing counted is not counted twice: as far as the list knows them) */
                        unsigned k, known = unwritten < 16 ? (unsigned)unwritten : 16;
                        for (k = 0; k < known && r->bad_list[k] != lba + i; k++)
                            ;
                        if (k == known)
                            bad_sector(r, lba + i);
                    }
            }
            r->done = lba + count;
        }
    }
    if (tick)
        tick(arg, total, total);
    if (r->bad)
        snprintf(r->why, sizeof r->why, "sectors could not be written as the image has them");
    return r->bad ? -1 : 0;
}

int diskjob_check(diskjob_io io, void *ctx, unsigned long total, unsigned run, diskjob_tick tick, void *arg, struct diskjob_report *r)
{
    static unsigned char buf[RUN * 512];
    unsigned long lba;
    memset(r, 0, sizeof *r);
    run = run_of(run);
    for (lba = 0; lba < total; lba += run) {
        unsigned count = total - lba < run ? (unsigned)(total - lba) : run;
        if (tick && tick(arg, lba, total)) {
            r->stopped = 1;
            return -1;
        }
        read_run(io, ctx, lba, buf, count, r);
        r->done = lba + count;
    }
    if (tick)
        tick(arg, total, total);
    return r->bad ? -1 : 0;
}

/* ---- a floppy's full format (2026-11-23) */

int diskjob_format(diskjob_track fmt, void *ctx, unsigned cyls, unsigned heads, unsigned spt, diskjob_tick tick, void *arg,
                   struct diskjob_report *r, int *err)
{
    unsigned long total = (unsigned long)cyls * heads * spt;
    unsigned c, h, s;
    memset(r, 0, sizeof *r);
    *err = 0;
    for (c = 0; c < cyls; c++)
        for (h = 0; h < heads; h++) {
            unsigned long first = ((unsigned long)c * heads + h) * spt;
            unsigned long long bad = 0;
            int e;
            if (tick && tick(arg, first, total)) {
                r->stopped = 1;
                return -1;
            }
            if ((e = fmt(ctx, c, h, &bad)) != 0) {
                *err = e;
                snprintf(r->why, sizeof r->why, "error %02Xh at cylinder %u, side %u", (unsigned)e & 0xFF, c, h);
                return -1;
            }
            for (s = 0; s < spt && s < 64; s++)
                if (bad >> s & 1) {
                    if (r->bad < sizeof r->bad_list / sizeof r->bad_list[0])
                        r->bad_list[r->bad] = first + s;
                    if (!r->bad++)
                        r->first_bad = first + s;
                }
            r->done = first + spt;
        }
    if (tick)
        tick(arg, total, total);
    return 0;
}

static unsigned le16(const unsigned char *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

int diskjob_mark_bad(diskjob_io io, void *ctx, const unsigned long *bad, unsigned nbad)
{
    unsigned char boot[512], *fat;
    unsigned long total, data, clusters, cl;
    unsigned spc, reserved, nfats, fatsz, rootsecs, i, k;
    int fat12, rc = 0;
    if (!nbad)
        return 0;
    if (io(ctx, 0, boot, 1, 0) != 0 || le16(boot + 11) != 512 || !(spc = boot[13]) || !(reserved = le16(boot + 14))
        || !(nfats = boot[16]) || !(fatsz = le16(boot + 22)))
        return -2;
    total = le16(boot + 19) ? le16(boot + 19)
                            : (unsigned long)le16(boot + 32) | (unsigned long)le16(boot + 34) << 16;
    rootsecs = (le16(boot + 17) * 32 + 511) / 512;
    data = reserved + (unsigned long)nfats * fatsz + rootsecs;
    if (total <= data)
        return -2;
    clusters = (total - data) / spc;
    if (clusters >= 65525)
        return -2;                      /* (FAT32: not a floppy's) */
    fat12 = clusters < 4085;
    for (i = 0; i < nbad; i++)
        if (bad[i] < data)
            return -1;
    if (!(fat = malloc((size_t)fatsz * 512)))
        return -2;
    if (io(ctx, reserved, fat, fatsz, 0) != 0) {
        free(fat);
        return -2;
    }
    for (i = 0; i < nbad; i++) {
        cl = 2 + (bad[i] - data) / spc;
        if (cl >= clusters + 2)
            continue;                   /* (past the last cluster: no file is ever there) */
        if (fat12) {
            unsigned long o = cl * 3 / 2;
            if (cl & 1)
                fat[o] = (unsigned char)((fat[o] & 0x0F) | 0x70), fat[o + 1] = 0xFF;
            else
                fat[o] = 0xF7, fat[o + 1] = (unsigned char)((fat[o + 1] & 0xF0) | 0x0F);
        } else {
            fat[cl * 2] = 0xF7, fat[cl * 2 + 1] = 0xFF;
        }
    }
    for (k = 0; k < nfats && rc == 0; k++)
        if (io(ctx, reserved + (unsigned long)k * fatsz, fat, fatsz, 1) != 0)
            rc = -2;
    free(fat);
    return rc;
}
