/* The virtual 8259A pair (supervisor.md §10): ICW1-4, OCW1-3, IRR/ISR reads,
 * poll, specific and non-specific EOI, rotation, automatic EOI, special mask
 * mode and special fully nested mode, with the slave on the master's IR2 as
 * on every AT. Requests are edges: an IRR bit stays set until acknowledged,
 * and a second EOI with nothing in service does nothing (SDL and DJGPP's
 * INT 75h handler send two). */
#include "vpic.h"

static int rank(const struct pic8259 *c, int level) { return (level - c->lowest - 1) & 7; }

/* The highest-priority level in bits, or -1. */
static int highest(const struct pic8259 *c, u8 bits)
{
    int i;
    for (i = 1; i <= 8; i++) {
        int l = (c->lowest + i) & 7;
        if (bits & (1u << l))
            return l;
    }
    return -1;
}

/* The level chip c would raise INT for now (cascade: the slave's request on
   the master's IR2), or -1. */
static int request(const struct pic8259 *c, u8 cascade, int is_master)
{
    u8 req = (u8)((c->irr | cascade) & ~c->imr), isr = c->isr;
    int r, s;
    if (c->smm)
        req &= (u8)~isr;
    r = highest(c, req);
    if (r < 0 || c->smm)
        return r;
    if (is_master && r == 2 && (c->icw4 & 0x10))
        isr &= (u8)~0x04;                       /* special fully nested: the slave arbitrates */
    s = highest(c, isr);
    return (s >= 0 && rank(c, s) <= rank(c, r)) ? -1 : r;
}

static int slave_request(const struct vpic *v) { return request(&v->p[1], 0, 0); }

static int master_request(const struct vpic *v)
{
    return request(&v->p[0], slave_request(v) >= 0 ? 0x04 : 0, 1);
}

static void eoi(struct vpic *v, int chip, int level)
{
    v->p[chip].isr &= (u8)~(1u << level);
    v->inflight &= (u16)~(1u << (chip * 8 + level));
}

/* An acknowledge (INTA or poll) of level on chip. */
static void take(struct vpic *v, int chip, int level)
{
    struct pic8259 *c = &v->p[chip];
    c->irr &= (u8)~(1u << level);
    if ((c->icw4 & 0x02) || v->auto_eoi) {     /* automatic EOI */
        if (c->rotate_aeoi && (c->icw4 & 0x02))
            c->lowest = (u8)level;
        v->inflight &= (u16)~(1u << (chip * 8 + level));
    } else {
        c->isr |= (u8)(1u << level);
    }
}

void vpic_reset(struct vpic *v, u8 master_base, u8 slave_base, u16 imr)
{
    int i;
    for (i = 0; i < 2; i++) {
        struct pic8259 *c = &v->p[i];
        c->irr = c->isr = 0;
        c->imr = (u8)(imr >> (8 * i));
        c->base = (u8)((i ? slave_base : master_base) & 0xF8);
        c->init = 0;
        c->icw1 = 0x11;
        c->icw3 = i ? 0x02 : 0x04;
        c->icw4 = 0x01;
        c->read_isr = c->poll = c->smm = c->rotate_aeoi = 0;
        c->lowest = 7;
    }
    v->inflight = 0;
}

void vpic_raise(struct vpic *v, int irq)
{
    if (irq == 2)
        irq = 9;                                /* the AT's IRQ 2 is the slave's IR1 */
    v->p[irq >> 3].irr |= (u8)(1u << (irq & 7));
}

void vpic_raise_hw(struct vpic *v, int irq)
{
    vpic_raise(v, irq);
    v->inflight |= (u16)(1u << irq);
}

void vpic_write(struct vpic *v, u16 port, u8 val)
{
    int chip = (port & 0x80) ? 1 : 0, level;
    struct pic8259 *c = &v->p[chip];

    if (port & 1) {
        switch (c->init) {
        case 2:
            c->base = val & 0xF8;
            c->init = (c->icw1 & 0x02) ? ((c->icw1 & 0x01) ? 4 : 0) : 3;
            break;
        case 3:
            c->icw3 = val;
            c->init = (c->icw1 & 0x01) ? 4 : 0;
            break;
        case 4:
            c->icw4 = val;
            c->init = 0;
            break;
        default:
            c->imr = val;                       /* OCW1 */
        }
        return;
    }
    if (val & 0x10) {                           /* ICW1 */
        c->icw1 = val;
        c->init = 2;
        c->irr = c->isr = c->imr = 0;
        c->read_isr = c->poll = c->smm = c->rotate_aeoi = 0;
        c->lowest = 7;
        if (!(val & 0x01))
            c->icw4 = 0;
        v->inflight &= (u16)~(0xFFu << (chip * 8));
        return;
    }
    if (val & 0x08) {                           /* OCW3 */
        if (val & 0x04)
            c->poll = 1;
        if (val & 0x02)
            c->read_isr = val & 0x01;
        if (val & 0x40)
            c->smm = (val >> 5) & 1;
        return;
    }
    level = val & 7;                            /* OCW2 */
    switch (val >> 5) {
    case 0: c->rotate_aeoi = 0; break;
    case 4: c->rotate_aeoi = 1; break;
    case 1:                                     /* non-specific EOI */
    case 5:                                     /* ... and rotate */
        level = highest(c, c->isr);
        if (level >= 0) {
            eoi(v, chip, level);
            if ((val >> 5) == 5)
                c->lowest = (u8)level;
        }
        break;
    case 3: eoi(v, chip, level); break;         /* specific EOI */
    case 7: eoi(v, chip, level); c->lowest = (u8)level; break;
    case 6: c->lowest = (u8)level; break;       /* set priority */
    default: break;                             /* 2: no operation */
    }
}

u8 vpic_read(struct vpic *v, u16 port)
{
    int chip = (port & 0x80) ? 1 : 0, r;
    struct pic8259 *c = &v->p[chip];
    if (port & 1)
        return c->imr;
    if (c->poll) {
        c->poll = 0;
        r = chip ? slave_request(v) : master_request(v);
        if (r < 0)
            return 0;
        take(v, chip, r);
        return (u8)(0x80 | r);
    }
    if (c->read_isr)
        return c->isr;
    return (u8)(c->irr | (!chip && slave_request(v) >= 0 ? 0x04 : 0));
}

int vpic_pending(const struct vpic *v) { return master_request(v) >= 0; }

int vpic_ack(struct vpic *v)
{
    int s = slave_request(v), m = master_request(v);
    if (m < 0)
        return -1;
    if (m == 2 && s >= 0) {
        take(v, 0, 2);
        take(v, 1, s);
        return v->p[1].base + s;
    }
    take(v, 0, m);
    return v->p[0].base + m;
}

u16 vpic_imr(const struct vpic *v) { return (u16)(v->p[0].imr | (v->p[1].imr << 8)); }
