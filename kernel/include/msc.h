/* USB mass storage: Bulk-Only Transport and its SCSI commands, with no host controller in it (kernel/drv/msc.c;
 * docs/v1/usb-storage.md). A controller's driver gives a device's two bulk pipes as calls; host tested against a
 * fake device (tests/host/msc_test.c). */
#ifndef K_MSC_H
#define K_MSC_H
#include "types.h"

#define MSC_STALL (-2)                  /* bulk(): the endpoint answered STALL (it is halted) */

struct msc_ops {
    /* A bulk transfer on the IN (in 1) or OUT pipe of len bytes: 0 (got: the bytes moved, fewer if the device sent
       a short packet), MSC_STALL, or -1 (another error, or no answer in time). */
    int (*bulk)(void *ctx, int in, void *buf, u32 len, u32 *got);
    int (*clear_halt)(void *ctx, int in);       /* CLEAR_FEATURE(ENDPOINT_HALT), the pipe's toggle back to DATA0 */
    int (*reset)(void *ctx);                    /* the class's Bulk-Only Mass Storage Reset (FFh) */
    void (*sleep)(void *ctx, u32 ms);           /* a pause (a unit not ready yet) */
};

struct msc {
    const struct msc_ops *ops;
    void *ctx;
    u32 tag;
    u32 blocks, block_size;             /* READ CAPACITY's: last LBA + 1, and the block's bytes */
    char vendor[9], product[17], rev[5];        /* INQUIRY's, trailing blanks off */
    u8 removable;
    u8 sense_key, asc, ascq;            /* the last REQUEST SENSE's */
    u32 commands, failures, resets;
};

/* The device found out: INQUIRY, TEST UNIT READY until it is ready (a unit attention or not ready yet waited out, up
   to about five seconds), READ CAPACITY. 0, or -1 with why (a phrase, for the log). */
int msc_start(struct msc *m, const char **why);
/* 512-byte blocks: 0, or -1 (after the transport's recovery, or a command the device refused). */
int msc_read(struct msc *m, u32 lba, void *buf, u32 count);
int msc_write(struct msc *m, u32 lba, const void *buf, u32 count);
/* A SCSI command: 0 passed, 1 failed (its sense read into m), -1 the transport failed (the reset recovery done). */
int msc_command(struct msc *m, const u8 *cdb, u32 cdb_len, int in, void *buf, u32 len, u32 *got);

#endif
