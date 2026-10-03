/* The network card GLOS owns (supervisor.md §10; PRD §4.4). One card in M3:
 * an NE2000 (ISA, or the RTL8029 on PCI). Its ports read FFh for the
 * system VM, its PCI function disappears from the VM's view, and its IRQ is
 * the kernel's. All register access happens in the net thread; the IRQ only
 * masks the line and wakes it. */
#ifndef K_NIC_H
#define K_NIC_H
#include "types.h"

struct trapframe;

#define ETH_MAX 1514

struct nic {
    const char *name;
    u8 mac[6];
    u16 base;
    u8 irq;
    int (*send)(struct nic *n, const void *frame, u32 len);    /* 0, or -1 if the card is busy or broken */
    void (*service)(struct nic *n);     /* the net thread: interrupts acknowledged, frames to net_rx() */
    u32 rx_frames, tx_frames, rx_errors, overruns;
};

extern struct nic *nic0;

int ne2k_probe(void);                   /* finds, refuses or claims an NE2000; sets nic0 */
void net_start(int selftest);           /* the net thread, once a card is claimed */
void net_rx(struct nic *n, const u8 *frame, u32 len);
void net_irq(struct trapframe *tf);     /* the card's IRQ handler */
void net_report(void);                  /* GLOS-NET counters, at leave */

#endif
