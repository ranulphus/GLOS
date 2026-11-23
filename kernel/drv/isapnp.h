/* ISA Plug and Play (kernel/drv/isapnp.c): the cards found, numbered and read, and a logical device set up. Plain C
   over port callbacks: the kernel's side is pnpbus.c, the host's test tests/host/isapnp_test.c. */
#ifndef K_ISAPNP_H
#define K_ISAPNP_H

struct pnp_io {
    unsigned char (*in8)(void *ctx, unsigned port);
    void (*out8)(void *ctx, unsigned port, unsigned char v);
    void (*wait_us)(void *ctx, unsigned us);
};

/* An ID as its four bytes come (the first lowest): three letters packed in two bytes, then four hex digits. */
#define PNP_ID(a, b, c, n) ((unsigned long)((((a) - '@') << 2) | (((b) - '@') >> 3)) \
                            | (unsigned long)(((((b) - '@') & 7) << 5) | ((c) - '@')) << 8 \
                            | (unsigned long)(((n) >> 8) & 0xFF) << 16 | (unsigned long)((n) & 0xFF) << 24)
#define PNP_VENDOR(id) ((id) & 0xFFFFul)        /* its three letters */

#define PNP_DEVS 24

struct pnp_dev {
    unsigned char csn, ldn, active;
    unsigned long card;                 /* the card's ID */
    unsigned long id, compat;           /* the logical device's, and its first compatible ID's (0: none) */
    /* as it is set: I/O bases (0: none), interrupt lines (0: none), DMA channels (4: none) */
    unsigned short io[4];
    unsigned char irq[2], dma[2];
    /* what it may be given: its independent resources and those of its first (preferred) dependent function, in
       the order of its registers */
    unsigned char n_io, n_irq, n_dma;
    struct {
        unsigned short min, max;
        unsigned char align, len;
    } io_opt[4];
    unsigned short irq_opt[2];          /* a bit a line */
    unsigned char dma_opt[2];           /* a bit a channel */
};

struct pnp {
    const struct pnp_io *io;
    void *ctx;
    unsigned rd;                        /* the READ_DATA port in use (0: none found to work) */
    unsigned ncards, ndevs;
    struct pnp_dev dev[PNP_DEVS];
};

/* Every card isolated, given a number and read; no logical device is changed. The number of cards. */
int pnp_scan(struct pnp *p);
/* A logical device given these resources (as struct pnp_dev's io, irq and dma) and turned on: 0. */
int pnp_set(struct pnp *p, struct pnp_dev *d, const unsigned short io[4], const unsigned char irq[2],
            const unsigned char dma[2]);
void pnp_id_text(unsigned long id, char out[8]);        /* "ESS1868" */

#endif
