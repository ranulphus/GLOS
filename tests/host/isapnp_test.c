/* isapnp_test: kernel/drv/isapnp.c (phase H) against a model of ISA Plug and Play cards on one bus, in the
 * specification's protocol: a card waits for the key (a wrong byte starts it over), sleeps, goes to Isolation at
 * Wake[0] if it has no number, answers the 72 pairs of reads at the READ_DATA port that was set (55h, AAh for a
 * 1; a card with a 0 that sees another's 1 drops out), takes a number, gives its resource data a byte at a time
 * and keeps its logical devices' registers. Cards: an ESS-like sound card with four logical devices (one of them
 * with dependent functions, one set up already as a BIOS would have), a network card, a third whose identifier
 * differs from the first's in its last serial bit. And faults: nothing on the bus; a card that answers on the
 * first READ_DATA port wrongly (as something else at that port would make it seem), so the next port is used.
 * "isapnp_test: 0 failures" when all held. */
#include <stdio.h>
#include <string.h>

#include "../../kernel/drv/isapnp.h"

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

enum { KEYWAIT, SLEEP, ISOLATION, CONFIG };

struct card {
    unsigned char rom[512];             /* the identifier's nine bytes, then the resource data */
    unsigned len;
    int state;
    unsigned lfsr, csn, pos, ldn;
    unsigned bit;                       /* the identifier's bit the isolation is at */
    int lost;                           /* dropped out of this round */
    unsigned char reg[8][0x100];        /* each logical device's registers */
};

static struct bus {
    struct card c[4];
    unsigned n, addr, rd_port;
    int second;                         /* the second read of a pair comes next */
    unsigned waited;
    unsigned bad_port;                  /* a port on which every read gives 55h (something else lives there) */
    unsigned key_writes;
} B;

static unsigned char csum(const unsigned char *id)
{
    unsigned v = 0x6A, i;
    for (i = 0; i < 64; i++) {
        unsigned bit = (id[i / 8] >> (i % 8)) & 1;
        v = (v >> 1) | (((v ^ (v >> 1) ^ bit) & 1) << 7);
    }
    return (unsigned char)v;
}

static void card_new(const unsigned char id8[8], const unsigned char *res, unsigned n)
{
    struct card *c = &B.c[B.n++];
    memset(c, 0, sizeof *c);
    memcpy(c->rom, id8, 8);
    c->rom[8] = csum(id8);
    memcpy(c->rom + 9, res, n);
    c->len = 9 + n;
    c->state = KEYWAIT;
    c->lfsr = 0x6A;
    {
        unsigned l;
        for (l = 0; l < 8; l++)
            c->reg[l][0x74] = c->reg[l][0x75] = 4;
    }
}

static void m_out(void *ctx, unsigned port, unsigned char v)
{
    unsigned i;
    (void)ctx;
    if (port == 0x279) {
        B.addr = v;
        for (i = 0; i < B.n; i++) {
            struct card *c = &B.c[i];
            if (c->state != KEYWAIT)
                continue;
            if (v == c->lfsr) {
                c->lfsr = (c->lfsr >> 1) | (((c->lfsr ^ (c->lfsr >> 1)) & 1) << 7);
                B.key_writes++;
                if (++c->pos == 32)
                    c->state = SLEEP, c->pos = 0;
            } else {
                c->lfsr = 0x6A, c->pos = 0;
            }
        }
        if (v == 0x01)                          /* serial isolation chosen: a round begins */
            for (i = 0; i < B.n; i++)
                B.c[i].bit = 0, B.c[i].lost = 0;
        B.second = 0;
        return;
    }
    if (port != 0xA79)
        return;
    for (i = 0; i < B.n; i++) {
        struct card *c = &B.c[i];
        if (c->state == KEYWAIT)
            continue;
        switch (B.addr) {
        case 0x00:
            if (c->state == ISOLATION)
                B.rd_port = ((unsigned)v << 2) | 3;
            break;
        case 0x02:
            if (v & 0x04)
                c->csn = 0;
            if (v & 0x02)
                c->state = KEYWAIT, c->lfsr = 0x6A, c->pos = 0;
            break;
        case 0x03:
            if (v == 0 && c->csn == 0)
                c->state = ISOLATION;
            else if (v != 0 && c->csn == v)
                c->state = CONFIG;
            else
                c->state = SLEEP;
            c->pos = 0, c->bit = 0, c->lost = 0;
            break;
        case 0x06:
            if (c->state == ISOLATION && !c->lost && c->bit == 72)
                c->csn = v, c->state = CONFIG, c->pos = 0;
            break;
        case 0x07:
            if (c->state == CONFIG)
                c->ldn = v & 7;
            break;
        default:
            if (c->state == CONFIG && B.addr >= 0x30)
                c->reg[c->ldn][B.addr] = v;
            break;
        }
    }
}

static unsigned char m_in(void *ctx, unsigned port)
{
    unsigned i, v = 0xFF;
    (void)ctx;
    if (B.bad_port && port == B.bad_port)
        return 0x55;
    if (!B.rd_port || port != B.rd_port)
        return 0xFF;
    if (B.addr == 0x01) {                       /* a read of an isolation pair */
        int one = 0;
        for (i = 0; i < B.n; i++) {
            struct card *c = &B.c[i];
            if (c->state == ISOLATION && !c->lost && c->bit < 72 && ((c->rom[c->bit / 8] >> (c->bit % 8)) & 1))
                one = 1;
        }
        v = one ? (B.second ? 0xAA : 0x55) : 0xFF;
        if (B.second)
            for (i = 0; i < B.n; i++) {
                struct card *c = &B.c[i];
                if (c->state != ISOLATION || c->lost || c->bit >= 72)
                    continue;
                if (one && !((c->rom[c->bit / 8] >> (c->bit % 8)) & 1))
                    c->lost = 1;                /* (another's 1 seen: out of this round) */
                else
                    c->bit++;
            }
        B.second = !B.second;
        return (unsigned char)v;
    }
    for (i = 0; i < B.n; i++) {
        struct card *c = &B.c[i];
        if (c->state != CONFIG)
            continue;
        if (B.addr == 0x04)
            v = c->pos < c->len ? c->rom[c->pos++] : 0xFF;
        else if (B.addr == 0x05)
            v = 1;
        else if (B.addr == 0x06)
            v = c->csn;
        else if (B.addr >= 0x30)
            v = c->reg[c->ldn][B.addr];
    }
    return (unsigned char)v;
}

static void m_wait(void *ctx, unsigned us)
{
    (void)ctx;
    B.waited += us;
}

static const struct pnp_io io = { m_in, m_out, m_wait };

/* ---- resource data */

#define ID4(a, b, c, n) (unsigned char)((((a) - '@') << 2) | (((b) - '@') >> 3)), \
                        (unsigned char)(((((b) - '@') & 7) << 5) | ((c) - '@')), \
                        (unsigned char)((n) >> 8), (unsigned char)((n) & 0xFF)

static const unsigned char sound_res[] = {
    0x0A, 0x10, 0x01,                                   /* PnP version 1.0 */
    0x82, 0x05, 0x00, 'S', 'o', 'u', 'n', 'd',          /* its name (a large item) */
    0x15, ID4('E', 'S', 'S', 0x1868), 0x00,             /* logical device 0: the audio */
    0x1C, ID4('P', 'N', 'P', 0xB02F),                   /*   compatible */
    0x31, 0x00,                                         /*   the best set: */
    0x47, 0x01, 0x20, 0x02, 0x20, 0x02, 0x01, 0x10,     /*     I/O 220h, 16 */
    0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x01, 0x04,     /*     I/O 388h, 4 */
    0x47, 0x01, 0x30, 0x03, 0x30, 0x03, 0x01, 0x02,     /*     I/O 330h, 2 */
    0x22, 0x20, 0x00,                                   /*     IRQ 5 */
    0x2A, 0x02, 0x00,                                   /*     DMA 1 */
    0x2A, 0x01, 0x00,                                   /*     DMA 0 */
    0x30,                                               /*   another: */
    0x47, 0x01, 0x40, 0x02, 0x80, 0x02, 0x20, 0x10,     /*     I/O 240h-280h */
    0x47, 0x01, 0x88, 0x03, 0x88, 0x03, 0x01, 0x04,
    0x47, 0x01, 0x00, 0x03, 0x30, 0x03, 0x10, 0x02,
    0x22, 0xA0, 0x0E,                                   /*     IRQ 5, 7, 9, 10, 11 */
    0x2A, 0x0B, 0x00,
    0x2A, 0x0B, 0x00,
    0x38,                                               /*   end of the sets */
    0x15, ID4('E', 'S', 'S', 0x0001), 0x00,             /* logical device 1: the game port */
    0x4B, 0x01, 0x02, 0x01,                             /*   fixed I/O 201h, 1 */
    0x15, ID4('E', 'S', 'S', 0x0002), 0x00,             /* logical device 2: set up already (below) */
    0x47, 0x01, 0x00, 0x01, 0xF8, 0x03, 0x08, 0x08,
    0x22, 0x00, 0x9C,                                   /*   IRQ 10, 11, 12, 15 */
    0x15, ID4('E', 'S', 'S', 0x0003), 0x00,             /* logical device 3: nothing it can have */
    0x79, 0x00,                                         /* the end */
};

static const unsigned char net_res[] = {
    0x0A, 0x10, 0x00,
    0x15, ID4('R', 'T', 'L', 0x8019), 0x00,
    0x1C, ID4('P', 'N', 'P', 0x80D6),
    0x47, 0x00, 0x20, 0x02, 0x80, 0x03, 0x20, 0x20,     /* I/O 220h-380h in steps of 20h, 32 */
    0x23, 0x38, 0x9E, 0x01,                             /* IRQ 3, 4, 5, 9, 10, 11, 12, 15 (with its flags byte) */
    0x79, 0x00,
};

static void bus_new(void)
{
    memset(&B, 0, sizeof B);
}

static const struct pnp_dev *by_id(const struct pnp *p, unsigned long id, unsigned nth)
{
    unsigned i;
    for (i = 0; i < p->ndevs; i++)
        if (p->dev[i].id == id && !nth--)
            return &p->dev[i];
    return 0;
}

static void t_none(void)
{
    struct pnp p;
    memset(&p, 0, sizeof p);
    p.io = &io;
    bus_new();
    CHECK(pnp_scan(&p) == 0 && p.ndevs == 0, "an empty bus: %u cards %u devices", p.ncards, p.ndevs);
}

static void t_cards(void)
{
    static const unsigned char id_a[8] = { ID4('E', 'S', 'S', 0x1868), 0x11, 0x22, 0x33, 0x44 };
    static const unsigned char id_b[8] = { ID4('R', 'T', 'L', 0x8019), 0x01, 0x00, 0x00, 0x00 };
    static const unsigned char id_c[8] = { ID4('E', 'S', 'S', 0x1868), 0x11, 0x22, 0x33, 0xC4 };  /* a's, its last bit other */
    struct pnp p;
    const struct pnp_dev *d;
    struct pnp_dev *m;
    char t[8];
    unsigned i, csn_a = 0;
    memset(&p, 0, sizeof p);
    p.io = &io;
    bus_new();
    card_new(id_a, sound_res, sizeof sound_res);
    card_new(id_b, net_res, sizeof net_res);
    card_new(id_c, sound_res, sizeof sound_res);
    /* (a BIOS has set card a's logical device 2 up: 2F8h, IRQ 11) */
    B.c[0].reg[2][0x30] = 1, B.c[0].reg[2][0x60] = 0x02, B.c[0].reg[2][0x61] = 0xF8, B.c[0].reg[2][0x70] = 11;
    CHECK(pnp_scan(&p) == 3, "three cards: %u", p.ncards);
    CHECK(p.ndevs == 9, "nine logical devices: %u", p.ndevs);
    CHECK(B.c[0].csn && B.c[1].csn && B.c[2].csn && B.c[0].csn != B.c[1].csn && B.c[1].csn != B.c[2].csn
          && B.c[0].csn != B.c[2].csn, "each its own number: %u %u %u", B.c[0].csn, B.c[1].csn, B.c[2].csn);
    for (i = 0; i < 3; i++)
        CHECK(B.c[i].state == KEYWAIT, "card %u left waiting for the key: state %d", i, B.c[i].state);
    CHECK(B.waited >= 3 * (1000 + 72 * 250), "the isolation's waits kept: %u us", B.waited);
    pnp_id_text(PNP_ID('E', 'S', 'S', 0x1868), t);
    CHECK(!strcmp(t, "ESS1868"), "an ID's text: %s", t);
    CHECK((d = by_id(&p, PNP_ID('R', 'T', 'L', 0x8019), 0)) != 0, "the network card's device");
    if (d) {
        CHECK(d->compat == PNP_ID('P', 'N', 'P', 0x80D6), "its compatible ID");
        CHECK(d->n_io == 1 && d->io_opt[0].min == 0x220 && d->io_opt[0].max == 0x380 && d->io_opt[0].align == 0x20
              && d->io_opt[0].len == 0x20, "its I/O range: %x-%x", d->io_opt[0].min, d->io_opt[0].max);
        CHECK(d->n_irq == 1 && d->irq_opt[0] == 0x9E38, "its interrupts (a three-byte item): %x", d->irq_opt[0]);
        CHECK(d->n_dma == 0 && !d->active && d->dma[0] == 4, "no DMA; not set up");
    }
    for (i = 0; i < p.ndevs; i++)
        if (p.dev[i].id == PNP_ID('E', 'S', 'S', 0x1868) && p.dev[i].csn == B.c[0].csn)
            csn_a = i + 1;
    CHECK(csn_a, "card a's audio device");
    d = csn_a ? &p.dev[csn_a - 1] : 0;
    if (d) {
        CHECK(d->ldn == 0 && d->card == PNP_ID('E', 'S', 'S', 0x1868), "its place and its card");
        CHECK(d->n_io == 3 && d->io_opt[0].min == 0x220 && d->io_opt[1].min == 0x388 && d->io_opt[2].min == 0x330,
              "the first set's I/O alone: %u (%x %x %x)", d->n_io, d->io_opt[0].min, d->io_opt[1].min, d->io_opt[2].min);
        CHECK(d->n_irq == 1 && d->irq_opt[0] == 0x0020, "the first set's interrupt: %u %x", d->n_irq, d->irq_opt[0]);
        CHECK(d->n_dma == 2 && d->dma_opt[0] == 0x02 && d->dma_opt[1] == 0x01, "its two DMA channels");
        CHECK(d->compat == PNP_ID('P', 'N', 'P', 0xB02F), "its compatible ID");
        CHECK(!d->active, "not set up");
    }
    for (i = 0; i < p.ndevs; i++) {
        d = &p.dev[i];
        if (d->csn != B.c[0].csn)
            continue;
        if (d->ldn == 1)
            CHECK(d->id == PNP_ID('E', 'S', 'S', 0x0001) && d->n_io == 1 && d->io_opt[0].min == 0x201
                  && d->io_opt[0].max == 0x201 && d->io_opt[0].len == 1, "the game port's fixed I/O");
        if (d->ldn == 2)
            CHECK(d->active && d->io[0] == 0x2F8 && d->irq[0] == 11 && d->irq_opt[0] == 0x9C00,
                  "the one a BIOS set up, as it is: active=%u io=%x irq=%u", d->active, d->io[0], d->irq[0]);
        if (d->ldn == 3)
            CHECK(!d->n_io && !d->n_irq && !d->n_dma, "one with nothing");
    }
    /* card c's audio device set up: only its registers change */
    m = 0;
    for (i = 0; i < p.ndevs; i++)
        if (p.dev[i].id == PNP_ID('E', 'S', 'S', 0x1868) && p.dev[i].csn == B.c[2].csn)
            m = &p.dev[i];
    CHECK(m != 0, "card c's audio device");
    if (m) {
        static const unsigned short sio[4] = { 0x220, 0x388, 0x330, 0 };
        static const unsigned char sirq[2] = { 5, 0 }, sdma[2] = { 1, 0 };
        CHECK(pnp_set(&p, m, sio, sirq, sdma) == 0, "set up");
        CHECK(B.c[2].reg[0][0x30] == 1 && B.c[2].reg[0][0x60] == 0x02 && B.c[2].reg[0][0x61] == 0x20
              && B.c[2].reg[0][0x64] == 0x03 && B.c[2].reg[0][0x65] == 0x30 && B.c[2].reg[0][0x70] == 5
              && B.c[2].reg[0][0x71] == 2 && B.c[2].reg[0][0x74] == 1 && B.c[2].reg[0][0x75] == 0,
              "its registers");
        CHECK(m->active && m->io[0] == 0x220 && m->irq[0] == 5 && m->dma[0] == 1, "and as read back");
        CHECK(B.c[0].reg[0][0x30] == 0 && B.c[0].reg[0][0x60] == 0, "card a's audio device untouched");
        CHECK(B.c[0].reg[2][0x30] == 1 && B.c[0].reg[2][0x61] == 0xF8, "... and the one the BIOS set up");
        CHECK(B.c[2].state == KEYWAIT, "left waiting for the key");
    }
}

static void t_port(void)
{
    static const unsigned char id_b[8] = { ID4('R', 'T', 'L', 0x8019), 0x09, 0x00, 0x00, 0x00 };
    struct pnp p;
    memset(&p, 0, sizeof p);
    p.io = &io;
    bus_new();
    card_new(id_b, net_res, sizeof net_res);
    B.bad_port = 0x20B;                         /* something else answers there: every read 55h */
    CHECK(pnp_scan(&p) == 1 && p.ndevs == 1, "found on another READ_DATA port: %u cards", p.ncards);
    CHECK(p.rd != 0x20B && p.rd == B.rd_port, "the port used: %x (the cards': %x)", p.rd, B.rd_port);
}

int main(void)
{
    t_none();
    t_cards();
    t_port();
    printf("isapnp_test: %d failures\n", fails);
    return fails != 0;
}
