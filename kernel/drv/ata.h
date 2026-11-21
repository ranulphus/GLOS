/* ATA disks by programmed I/O (docs/v1/native-dos.md, N1c): the protocol alone, over a channel's ports given as
 * callbacks, so that tests/host/ata_test.c runs it against a model of a drive. kernel/drv/ide.c finds the
 * channels (ISA, PCI compatibility or native mode) and gives it the real ports and a clock.
 *
 * Polled, with the channel's interrupt held off (nIEN) for the length of each command and the status read at
 * its end (which clears a pending one), so the BIOS beside GLOS finds the channel as it left it. LBA28, LBA48
 * past 2^28 sectors, CHS for drives without LBA; READ/WRITE MULTIPLE where the drive has them; FLUSH CACHE.
 * ATAPI devices are found and named, their packets come with N4. */
#ifndef K_ATA_H
#define K_ATA_H

struct ata_io {
    unsigned char (*in8)(void *ctx, unsigned port);
    void (*out8)(void *ctx, unsigned port, unsigned char v);
    void (*in16s)(void *ctx, unsigned port, unsigned short *buf, unsigned count);
    void (*out16s)(void *ctx, unsigned port, const unsigned short *buf, unsigned count);
    unsigned long (*ms)(void *ctx);     /* a millisecond clock that runs with interrupts off (wraps) */
    void *ctx;
};

enum {
    ATA_OK = 0,
    ATA_ENODEV = -1,                    /* nothing there */
    ATA_ETIMEOUT = -2,                  /* busy, or no data asked for, past the time allowed */
    ATA_EDEVICE = -3,                   /* the drive's error (ERR or DF); its error register in ata_dev.err */
    ATA_ERANGE = -4,                    /* past the drive's end, or a count it can't take */
    ATA_ENOTSUP = -5                    /* an ATAPI device (N4), or 48-bit addressing it hasn't got */
};

struct ata_chan {
    const struct ata_io *io;
    unsigned base, ctl;                 /* command block (1F0h, 170h, or a native BAR) and control (3F6h, 376h) */
    int selected;                       /* the device last selected (-1: unknown) */
};

struct ata_dev {
    struct ata_chan *ch;
    int slave;
    int present, atapi;
    int lba, lba48;
    unsigned multiple;                  /* sectors a DRQ block with READ/WRITE MULTIPLE; 0: single sectors */
    unsigned long sectors_lo, sectors_hi;   /* addressable sectors */
    unsigned cyls, heads, spt;          /* the current CHS translation (for drives without LBA) */
    char model[41], serial[21];
    unsigned char err, status;          /* the last command's error and status registers */
    unsigned short id[256];             /* IDENTIFY (PACKET) DEVICE's words */
};

/* The channel's devices found, IDENTIFY's words read: 0 with d[0..1].present telling which; ATA_ENODEV if the
   channel isn't there (a floating bus). ATA_PROBE_OWN when GLOS alone drives the channel (native): a soft reset
   and the signatures first, and its own SET MULTIPLE. Without it (hosted: the BIOS drives these disks too) the
   devices are asked as they are (a reset would put back their power-on settings, under the BIOS's feet: its
   multiple, its PIO timing, an old drive's CHS translation), and the multiple the BIOS set is the one used. */
#define ATA_PROBE_OWN 1
int ata_probe(struct ata_chan *ch, struct ata_dev d[2], int flags);

/* n sectors at lba (hi: its upper 32 bits) to or from buf (n * 256 words), in as many commands as the drive
   needs (256 sectors a command in 28-bit and CHS, 65,536 in 48-bit). ATA_OK, or an error with the sectors done
   in *done (done may be 0). */
int ata_read(struct ata_dev *d, unsigned long lba_lo, unsigned long lba_hi, unsigned long n, void *buf,
             unsigned long *done);
int ata_write(struct ata_dev *d, unsigned long lba_lo, unsigned long lba_hi, unsigned long n, const void *buf,
              unsigned long *done);
/* The drive's write cache written out (FLUSH CACHE, or its EXT form). */
int ata_flush(struct ata_dev *d);

/* Time allowed (ms): a drive spinning up after a reset; a command to start moving data; between blocks. */
#define ATA_T_RESET 31000UL
#define ATA_T_CMD   10000UL
#define ATA_T_BLOCK 10000UL

#endif
