/* Disk Tools' long jobs (diskjob.h). Sectors go a run at a time (32, GLOS_FS_SECTORS' most; or what the caller
 * says: a floppy's track, so that each call is one turn of the disk); a run that fails is gone through again a
 * sector at a time, each tried three times. */
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
