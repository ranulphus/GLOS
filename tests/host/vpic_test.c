/* Host test for the virtual 8259A pair (kernel/vm/vpic.c), as scripts of the
 * port sequences real code sends: the BIOS's initialisation, DOS's timer
 * and keyboard handlers, nesting, a slave IRQ with its two EOIs, SDL's
 * double EOI, a poll, special mask mode, automatic EOI and rotation. */
#include <stdio.h>
#include "vpic.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* What a BIOS POST sends: ICW1-4 to both chips, then the masks. */
static void post(struct vpic *v)
{
    vpic_write(v, 0x20, 0x11); vpic_write(v, 0x21, 0x08); vpic_write(v, 0x21, 0x04); vpic_write(v, 0x21, 0x01);
    vpic_write(v, 0xA0, 0x11); vpic_write(v, 0xA1, 0x70); vpic_write(v, 0xA1, 0x02); vpic_write(v, 0xA1, 0x01);
    vpic_write(v, 0x21, 0xB8);                  /* IRQ 0, 1, 2, 6 enabled */
    vpic_write(v, 0xA1, 0x8F);                  /* IRQ 12, 13, 14 enabled */
}

int main(void)
{
    struct vpic v;

    vpic_reset(&v, 0x08, 0x70, 0xFFFF);
    CHECK(vpic_imr(&v) == 0xFFFF);
    post(&v);
    CHECK(vpic_imr(&v) == 0x8FB8);
    CHECK(!vpic_pending(&v));

    /* The timer: INTA, then a non-specific EOI from INT 08h. */
    vpic_raise_hw(&v, 0);
    CHECK(v.inflight == 0x0001);
    CHECK(vpic_pending(&v));
    CHECK(vpic_ack(&v) == 0x08);
    CHECK(!vpic_pending(&v));
    vpic_write(&v, 0x20, 0x0B);                 /* OCW3: read ISR */
    CHECK(vpic_read(&v, 0x20) == 0x01);
    vpic_write(&v, 0x20, 0x20);
    CHECK(vpic_read(&v, 0x20) == 0x00);
    CHECK(v.inflight == 0);
    vpic_write(&v, 0x20, 0x0A);                 /* back to IRR */

    /* Masked lines stay requested until unmasked (IRR latches). */
    vpic_raise(&v, 3);
    CHECK(!vpic_pending(&v));
    CHECK(vpic_read(&v, 0x20) == 0x08);
    vpic_write(&v, 0x21, 0xB0);
    CHECK(vpic_ack(&v) == 0x0B);
    vpic_write(&v, 0x20, 0x20);
    vpic_write(&v, 0x21, 0xB8);

    /* Nesting: the keyboard in service blocks the keyboard and lower levels,
       not the timer. */
    vpic_raise(&v, 1);
    CHECK(vpic_ack(&v) == 0x09);
    vpic_raise(&v, 6);
    CHECK(!vpic_pending(&v));
    vpic_raise(&v, 0);
    CHECK(vpic_ack(&v) == 0x08);
    vpic_write(&v, 0x20, 0x20);                 /* ends the timer's ... */
    vpic_write(&v, 0x20, 0x0B);
    CHECK(vpic_read(&v, 0x20) == 0x02);         /* ... the keyboard's still in service */
    vpic_write(&v, 0x20, 0x0A);
    CHECK(!vpic_pending(&v));
    vpic_write(&v, 0x20, 0x20);
    CHECK(vpic_ack(&v) == 0x0E);                /* now IRQ 6 */
    vpic_write(&v, 0x20, 0x66);                 /* specific EOI, level 6 */
    CHECK(v.p[0].isr == 0);

    /* A slave IRQ (12) and the two EOIs; then SDL's double EOI does nothing. */
    vpic_raise_hw(&v, 12);
    CHECK(vpic_read(&v, 0x20) == 0x04);         /* the cascade shows in the master's IRR */
    CHECK(vpic_ack(&v) == 0x74);
    CHECK(v.p[0].isr == 0x04 && v.p[1].isr == 0x10);
    CHECK(v.inflight == 0x1000);
    vpic_write(&v, 0xA0, 0x20);
    CHECK(v.inflight == 0);
    vpic_write(&v, 0x20, 0x20);
    vpic_write(&v, 0x20, 0x20);
    vpic_write(&v, 0xA0, 0x20);
    CHECK(v.p[0].isr == 0 && v.p[1].isr == 0);

    /* The slave's levels are blocked by the master's IR2 in service, and a
       master level above it gets through. */
    vpic_raise(&v, 13);
    CHECK(vpic_ack(&v) == 0x75);
    vpic_raise(&v, 14);
    CHECK(!vpic_pending(&v));
    vpic_raise(&v, 1);
    CHECK(vpic_ack(&v) == 0x09);
    vpic_write(&v, 0x20, 0x20);                 /* the keyboard */
    vpic_write(&v, 0xA0, 0x20);                 /* IRQ 13 */
    vpic_write(&v, 0x20, 0x20);                 /* the cascade */
    CHECK(vpic_ack(&v) == 0x76);
    vpic_write(&v, 0xA0, 0x20);
    vpic_write(&v, 0x20, 0x20);

    /* Poll: the level with bit 7 set, taken as by an INTA. */
    vpic_raise(&v, 1);
    vpic_write(&v, 0x20, 0x0C);
    CHECK(vpic_read(&v, 0x20) == 0x81);
    CHECK(v.p[0].isr == 0x02);
    vpic_write(&v, 0x20, 0x0C);
    CHECK(vpic_read(&v, 0x20) == 0x00);         /* nothing more */
    vpic_write(&v, 0x20, 0x20);

    /* Special mask mode: a level in service masked off lets lower ones in. */
    vpic_raise(&v, 0);
    CHECK(vpic_ack(&v) == 0x08);
    vpic_write(&v, 0x20, 0x68);                 /* OCW3: set SMM */
    vpic_write(&v, 0x21, 0xB9);                 /* mask IRQ 0 */
    vpic_raise(&v, 6);
    CHECK(vpic_ack(&v) == 0x0E);
    vpic_write(&v, 0x20, 0x66);
    vpic_write(&v, 0x20, 0x60);                 /* specific EOI, level 0 */
    vpic_write(&v, 0x20, 0x48);                 /* reset SMM */
    vpic_write(&v, 0x21, 0xB8);
    CHECK(v.p[0].isr == 0);

    /* Rotation: after a rotating EOI of level 1, level 2's the highest. */
    vpic_raise(&v, 1);
    CHECK(vpic_ack(&v) == 0x09);
    vpic_write(&v, 0x20, 0xA0);                 /* rotate on non-specific EOI */
    CHECK(v.p[0].lowest == 1);
    vpic_raise(&v, 0);
    vpic_raise(&v, 6);
    CHECK(vpic_ack(&v) == 0x0E);                /* 6 beats 0 now */
    vpic_write(&v, 0x20, 0xC7);                 /* set priority: 0 highest again */
    vpic_write(&v, 0x20, 0x66);
    CHECK(vpic_ack(&v) == 0x08);
    vpic_write(&v, 0x20, 0x20);

    /* Automatic EOI (ICW4 bit 1): nothing stays in service. */
    vpic_write(&v, 0x20, 0x11); vpic_write(&v, 0x21, 0x50); vpic_write(&v, 0x21, 0x04); vpic_write(&v, 0x21, 0x03);
    CHECK(vpic_imr(&v) == 0x8F00);              /* ICW1 clears the mask */
    vpic_raise_hw(&v, 0);
    CHECK(vpic_ack(&v) == 0x50);
    CHECK(v.p[0].isr == 0 && v.inflight == 0);

    /* ICW1 without ICW4 (bit 0 clear) and single mode (bit 1): ICW2 only. */
    vpic_write(&v, 0xA0, 0x12);
    vpic_write(&v, 0xA1, 0x78);
    CHECK(v.p[1].init == 0 && v.p[1].base == 0x78);
    vpic_write(&v, 0xA1, 0x00);
    CHECK(vpic_read(&v, 0xA1) == 0x00);         /* OCW1 again */

    /* Re-initialising a chip forgets its lines in flight. */
    post(&v);
    vpic_raise_hw(&v, 5);
    vpic_raise_hw(&v, 9);
    CHECK(v.inflight == 0x0220);
    vpic_write(&v, 0xA0, 0x11);
    CHECK(v.inflight == 0x0020);

    printf("vpic_test: %d failures\n", fails);
    return fails != 0;
}
