/* The system VM's virtual 8259A pair (supervisor.md §10). Pure state and no
 * I/O, so tests/host/vpic_test.c runs it on the host; the monitor maps the
 * physical PIC's mask onto it (vm_sync_mask). */
#ifndef K_VPIC_H
#define K_VPIC_H
#include "types.h"

struct pic8259 {
    u8 irr, isr, imr;
    u8 base;                    /* ICW2 */
    u8 init;                    /* 0 operational, else the ICW expected next (2, 3, 4) */
    u8 icw1, icw3, icw4;
    u8 read_isr;                /* OCW3: reads of the even port return ISR, not IRR */
    u8 poll;                    /* OCW3: the next even-port read is a poll */
    u8 smm;                     /* OCW3: special mask mode */
    u8 rotate_aeoi;             /* OCW2: rotate in automatic-EOI mode */
    u8 lowest;                  /* the lowest-priority level (7 after ICW1) */
};

struct vpic {
    struct pic8259 p[2];        /* master, slave (on the master's IR2) */
    u16 inflight;               /* lines a physical IRQ raised that are not yet EOIed virtually */
};

void vpic_reset(struct vpic *v, u8 master_base, u8 slave_base, u16 imr);
void vpic_raise(struct vpic *v, int irq);       /* an edge on irq (0-15) */
void vpic_raise_hw(struct vpic *v, int irq);    /* the same, from the physical PIC's line irq */
void vpic_write(struct vpic *v, u16 port, u8 val);      /* 20h, 21h, A0h, A1h */
u8 vpic_read(struct vpic *v, u16 port);
int vpic_pending(const struct vpic *v);         /* 1 if an INTA now would return a vector */
int vpic_ack(struct vpic *v);                   /* INTA: the vector, or -1 */
u16 vpic_imr(const struct vpic *v);             /* master in bits 0-7, slave in 8-15 */

#endif
