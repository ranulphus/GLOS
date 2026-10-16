/* The network card GLOS owns (supervisor.md §10; PRD §4.4, §8.1). One card:
 * an RTL8139 or an Intel 8255x on PCI (M5), else an NE2000 (the RTL8029 on
 * PCI, or an ISA card). Its ports read FFh for the system VM, its PCI
 * function disappears from the VM's view, and its IRQ is the kernel's. All
 * register access happens in the net thread; the IRQ only masks the line
 * and wakes it. */
#ifndef K_NIC_H
#define K_NIC_H
#include "types.h"

struct trapframe;
struct pci_dev;

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

int nic_probe(void);                    /* finds, refuses or claims a card (kernel/drv/nic.c); sets nic0 */
int nic_pci(struct pci_dev *d, const char *name);       /* a PCI card may be taken: 0, or -1 with the refusal said */
void nic_claimed(struct nic *n, struct pci_dev *d);     /* its IRQ and PCI function are the kernel's; the card line */
int ne2k_probe(void);                   /* each: 0 once it has claimed its card */
int rtl8139_probe(void);
int e100_probe(void);
void net_start(int selftest);           /* the net thread, once a card is claimed */
void net_rx(struct nic *n, const u8 *frame, u32 len);
void net_irq(struct trapframe *tf);     /* the card's IRQ handler */
void net_report(void);                  /* GLOS-NET counters, at leave */
int net_counts(u8 ip[4], u32 *rx, u32 *tx, char *card, u32 room);     /* GLOS_FN_STAT's (M9) */
u32 net_stat(char *buf, u32 max, int json);     /* glos net's lines, or glos stat's JSON object */

#endif
