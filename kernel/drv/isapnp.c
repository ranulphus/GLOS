/* ISA Plug and Play (GLOS v1, phase H tier 2, 2026-11-23; docs/v1/hardware-audit.md): the cards of 1994-1999 with no
 * jumpers (sound cards by ESS, OPTi, Creative, Crystal, Yamaha; network cards; modems), which wait, turned off,
 * for software to find them and give them their ports, interrupt and DMA channel. A PnP BIOS or a maker's DOS
 * program often has; where none did, GLOS does it here.
 *
 * Written from Intel's and Microsoft's "Plug and Play ISA Specification", version 1.0a, May 5, 1994 (the copy at
 * osdever.net/documents/PNP-ISA-v1.0a.pdf, sha256 21bd7b02...2b1f):
 *   - three ports: ADDRESS (279h, written), WRITE_DATA (A79h, written), READ_DATA (read; software's choice in
 *     203h-3FFh, its low two bits 11);
 *   - the initiation key: two zeros, then the 32 values of an 8-bit shift register from 6Ah (its input bit 0 XOR
 *     bit 1) written to ADDRESS: every card listens (the Sleep state);
 *   - Wake[0] (register 03h) takes the cards with no number to Isolation; the READ_DATA port is set there (00h,
 *     its bits 9:2); with ADDRESS at 01h, 72 pairs of reads give the serial identifier a bit at a time, lowest
 *     first: 55h then AAh is a 1, anything else a 0 (a card with a 0 that sees another's 1 drops out), a
 *     millisecond before the first pair and 250 microseconds after each; the last eight bits are a checksum of
 *     the first 64 (the same shift register, its input also XOR the bit);
 *   - the one card left takes a number (06h, the CSN) and is in Config; Wake[n] returns to it later;
 *   - its resource data are read a byte at a time (04h, when 05h's bit 0 says one is there): the nine bytes of
 *     its identifier, then tagged items to an end tag: a logical device's ID, compatible IDs, the interrupts,
 *     DMA channels and I/O ranges it may have (some inside "dependent functions": alternatives, the first the
 *     best);
 *   - a logical device (07h chooses it): 30h bit 0 on; its I/O bases at 60h, 62h ... (high byte first), its
 *     interrupts at 70h and 72h (71h, 73h: 2, edge and active high), its DMA channels at 74h and 75h (4: none);
 *   - 02h: 04h takes every card's number away, 02h sends them back to waiting for the key.
 * A scan changes no logical device: one that a BIOS has set up stays as it is and is reported as it is. */
#include "isapnp.h"

#define ADDRESS    0x279
#define WRITE_DATA 0xA79

static void out(struct pnp *p, unsigned port, unsigned v) { p->io->out8(p->ctx, port, (unsigned char)v); }
static void wait(struct pnp *p, unsigned us) { p->io->wait_us(p->ctx, us); }

static void wr(struct pnp *p, unsigned reg, unsigned v)
{
    out(p, ADDRESS, reg);
    out(p, WRITE_DATA, v);
}

static unsigned rd(struct pnp *p, unsigned reg)
{
    out(p, ADDRESS, reg);
    return p->io->in8(p->ctx, p->rd);
}

static void key(struct pnp *p)
{
    unsigned v = 0x6A, i;
    out(p, ADDRESS, 0);
    out(p, ADDRESS, 0);
    for (i = 0; i < 32; i++) {
        out(p, ADDRESS, v);
        v = (v >> 1) | (((v ^ (v >> 1)) & 1) << 7);
    }
}

/* One card isolated from those with no number: 1, its identifier in id; 0 if none answered; -1 if what came is
   no identifier (two things on the READ_DATA port: another port is tried). */
static int isolate(struct pnp *p, unsigned char id[9])
{
    unsigned i, sum = 0x6A, any = 0;
    wr(p, 0x03, 0);
    wr(p, 0x00, p->rd >> 2);
    wait(p, 1000);
    out(p, ADDRESS, 0x01);
    wait(p, 1000);
    for (i = 0; i < 9; i++)
        id[i] = 0;
    for (i = 0; i < 72; i++) {
        unsigned a = p->io->in8(p->ctx, p->rd), b = p->io->in8(p->ctx, p->rd), bit = a == 0x55 && b == 0xAA;
        wait(p, 250);
        any |= bit;
        if (i < 64)
            sum = (sum >> 1) | (((sum ^ (sum >> 1) ^ bit) & 1) << 7);
        id[i / 8] |= (unsigned char)(bit << (i % 8));
    }
    if (!any)
        return 0;
    return sum == id[8] ? 1 : -1;
}

static int res_byte(struct pnp *p)
{
    unsigned n;
    for (n = 0; n < 2000; n++) {
        if (rd(p, 0x05) & 1)
            return (int)rd(p, 0x04);
        wait(p, 100);
    }
    return -1;
}

static unsigned long id_of(const unsigned char *b)
{
    return (unsigned long)b[0] | (unsigned long)b[1] << 8 | (unsigned long)b[2] << 16 | (unsigned long)b[3] << 24;
}

/* The card in Config (its number csn): its resource data read, its logical devices added with what they may
   have and how they are set now. */
static void read_card(struct pnp *p, unsigned csn)
{
    unsigned char b[9];
    struct pnp_dev *d = 0;
    unsigned long card;
    unsigned i, ldn = 0, dep = 0, first = p->ndevs, guard;
    int c;
    wr(p, 0x03, csn);                           /* (its resource data from their start) */
    for (i = 0; i < 9; i++) {
        if ((c = res_byte(p)) < 0)
            return;
        b[i] = (unsigned char)c;
    }
    card = id_of(b);
    for (guard = 0; guard < 4096; guard++) {
        unsigned tag, len, name;
        if ((c = res_byte(p)) < 0)
            break;
        tag = (unsigned)c;
        if (tag & 0x80) {                       /* a large item: a name, memory, the maker's own: passed over */
            int lo = res_byte(p), hi = res_byte(p);
            if (lo < 0 || hi < 0)
                break;
            for (len = (unsigned)lo | (unsigned)hi << 8; len; len--)
                if (res_byte(p) < 0)
                    break;
            continue;
        }
        name = (tag >> 3) & 0x0F, len = tag & 7;
        for (i = 0; i < len && i < sizeof b; i++) {
            if ((c = res_byte(p)) < 0)
                break;
            b[i] = (unsigned char)c;
        }
        if (name == 0x0F)                       /* the end */
            break;
        if (name == 0x02 && len >= 4) {         /* a logical device */
            d = 0, dep = 0;
            if (p->ndevs < PNP_DEVS) {
                d = &p->dev[p->ndevs++];
                for (i = 0; i < sizeof *d; i++)
                    ((unsigned char *)d)[i] = 0;
                d->csn = (unsigned char)csn, d->ldn = (unsigned char)ldn;
                d->card = card, d->id = id_of(b);
                d->dma[0] = d->dma[1] = 4;
            }
            ldn++;
            continue;
        }
        if (name == 0x06) {                     /* a dependent function begins: the first is taken, the rest not */
            dep++;
            continue;
        }
        if (name == 0x07) {
            dep = 0x100;                        /* (what follows belongs to all of them again) */
            continue;
        }
        if (!d || (dep != 0 && dep != 1 && dep != 0x100))
            continue;
        if (name == 0x03 && len >= 4 && !d->compat)
            d->compat = id_of(b);
        else if (name == 0x04 && len >= 2 && d->n_irq < 2)
            d->irq_opt[d->n_irq++] = (unsigned short)(b[0] | b[1] << 8);
        else if (name == 0x05 && len >= 1 && d->n_dma < 2)
            d->dma_opt[d->n_dma++] = b[0];
        else if (name == 0x08 && len >= 7 && d->n_io < 4) {
            d->io_opt[d->n_io].min = (unsigned short)(b[1] | b[2] << 8);
            d->io_opt[d->n_io].max = (unsigned short)(b[3] | b[4] << 8);
            d->io_opt[d->n_io].align = b[5], d->io_opt[d->n_io].len = b[6];
            d->n_io++;
        } else if (name == 0x09 && len >= 3 && d->n_io < 4) {
            d->io_opt[d->n_io].min = d->io_opt[d->n_io].max = (unsigned short)(b[0] | (b[1] & 3) << 8);
            d->io_opt[d->n_io].align = 1, d->io_opt[d->n_io].len = b[2];
            d->n_io++;
        }
    }
    for (i = first; i < p->ndevs; i++) {        /* each as it is set now */
        unsigned k;
        d = &p->dev[i];
        wr(p, 0x07, d->ldn);
        d->active = (unsigned char)(rd(p, 0x30) & 1);
        for (k = 0; k < 4; k++)
            d->io[k] = (unsigned short)(rd(p, 0x60 + 2 * k) << 8 | rd(p, 0x61 + 2 * k));
        for (k = 0; k < 2; k++) {
            d->irq[k] = (unsigned char)(rd(p, 0x70 + 2 * k) & 0x0F);
            d->dma[k] = (unsigned char)(rd(p, 0x74 + k) & 7);
        }
    }
}

int pnp_scan(struct pnp *p)
{
    /* READ_DATA ports to try: clear of the game port (200h-207h), the usual sound, MIDI and printer ports */
    static const unsigned short ports[] = { 0x20B, 0x213, 0x23B, 0x3C3 };
    unsigned char id[9];
    unsigned t, csn;
    int r = 0;
    p->ncards = p->ndevs = 0;
    for (t = 0; t < sizeof ports / sizeof ports[0]; t++) {
        p->rd = ports[t];
        key(p);
        wr(p, 0x02, 0x04);                      /* every card's number taken away */
        wait(p, 2000);
        for (csn = 1; csn <= 32; csn++) {
            if ((r = isolate(p, id)) != 1)
                break;
            wr(p, 0x06, csn);
            wait(p, 250);
            read_card(p, csn);
            p->ncards++;
        }
        wr(p, 0x02, 0x02);                      /* back to waiting for the key */
        if (p->ncards)                          /* (none: the next port is tried too. Something else that answers
                                                   on this one hides the cards' bits, and looks like no card) */
            break;
    }
    (void)r;
    if (!p->ncards)
        p->rd = 0;
    return (int)p->ncards;
}

int pnp_set(struct pnp *p, struct pnp_dev *d, const unsigned short io[4], const unsigned char irq[2],
            const unsigned char dma[2])
{
    unsigned k;
    if (!p->rd || !d->csn)
        return -1;
    key(p);
    wr(p, 0x03, d->csn);
    wr(p, 0x07, d->ldn);
    wr(p, 0x30, 0);
    for (k = 0; k < 4; k++) {
        wr(p, 0x60 + 2 * k, io[k] >> 8);
        wr(p, 0x61 + 2 * k, io[k] & 0xFF);
    }
    for (k = 0; k < 2; k++) {
        wr(p, 0x70 + 2 * k, irq[k] & 0x0F);
        wr(p, 0x71 + 2 * k, 0x02);              /* an edge, active high: the ISA bus's */
        wr(p, 0x74 + k, dma[k] > 7 ? 4 : dma[k]);
    }
    wr(p, 0x30, 1);
    d->active = (unsigned char)(rd(p, 0x30) & 1);
    for (k = 0; k < 4; k++)
        d->io[k] = (unsigned short)(rd(p, 0x60 + 2 * k) << 8 | rd(p, 0x61 + 2 * k));
    for (k = 0; k < 2; k++) {
        d->irq[k] = (unsigned char)(rd(p, 0x70 + 2 * k) & 0x0F);
        d->dma[k] = (unsigned char)(rd(p, 0x74 + k) & 7);
    }
    wr(p, 0x02, 0x02);
    return d->active && d->io[0] == io[0] ? 0 : -1;
}

void pnp_id_text(unsigned long id, char out[8])
{
    static const char hex[] = "0123456789ABCDEF";
    unsigned b0 = (unsigned)(id & 0xFF), b1 = (unsigned)(id >> 8 & 0xFF), b2 = (unsigned)(id >> 16 & 0xFF),
             b3 = (unsigned)(id >> 24 & 0xFF);
    out[0] = (char)('@' + ((b0 >> 2) & 0x1F));
    out[1] = (char)('@' + (((b0 & 3) << 3) | (b1 >> 5)));
    out[2] = (char)('@' + (b1 & 0x1F));
    out[3] = hex[b2 >> 4], out[4] = hex[b2 & 15], out[5] = hex[b3 >> 4], out[6] = hex[b3 & 15];
    out[7] = 0;
}
