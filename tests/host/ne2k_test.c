/* Host test for the NE2000 driver's receive ring (kernel/drv/ne2k.c) against a small model of the card: a
 * DP8390's registers on two pages, its memory with the PROM and the ring, remote DMA through the data port.
 *
 * Frames put into the model's ring come out at net_rx whole, across the ring's end too. Then what a real card
 * now and then leaves and 86Box's never does: a header that names its own page as the next frame's, one that
 * names a page behind it, one whose next page does not fit its length, the card's own pointer outside its
 * ring, and a ring whose headers go round without ever meeting the card's pointer. Each is answered by the
 * ring begun again and a line in the log, the driver comes back (the first of them kept it going round for
 * ever: the 486 bench stood frozen, 2026-11-18 and 19), and frames after it are received as before. A frame
 * being sent when the ring is begun again does not leave the transmitter taken for ever. */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "nic.h"
#include "pci.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define BASE 0x300
#define RX_START 0x46
#define RX_STOP 0x80

/* ---- the card */
static u8 mem[65536];
static u8 cr, isr, imr, bnry, curr, pstart, pstop, par[6];
static u16 rsar, rbcr;
static int tx_holds;                            /* a frame sent stays "going" (no interrupt, the transmitter busy) */
static u32 sent;

u8 inb(u16 p)
{
    unsigned page = cr >> 6;
    if (p == 0x20 || p == 0x21 || p == 0xA1)
        return 0;
    if (p == 0xA0)                              /* the slave PIC's requests: line 10 while the card asks */
        return (isr & imr) ? 0x04 : 0;
    if (p < BASE || p >= BASE + 0x20)
        return 0xFF;
    p -= BASE;
    if (p == 0x1F)
        return 0;
    if (p == 0)
        return cr;
    if (page == 0)
        return p == 3 ? bnry : p == 7 ? isr : 0;
    if (page == 1)
        return p == 7 ? curr : p >= 1 && p <= 6 ? par[p - 1] : 0;
    return 0;
}

void outb(u16 p, u8 v)
{
    unsigned page = cr >> 6;
    if (p < BASE || p >= BASE + 0x20)
        return;
    p -= BASE;
    if (p == 0x1F) {
        isr |= 0x80;
        return;
    }
    if (p == 0) {
        cr = v;
        if (v & 0x04) {                         /* transmit */
            sent++;
            if (tx_holds)
                cr |= 0x04;
            else
                cr &= (u8)~0x04, isr |= 0x02;
        } else if (v & 0x01) {                  /* stopped: nothing is going any more */
            cr &= (u8)~0x04;
        }
        return;
    }
    if (page == 0) {
        switch (p) {
        case 1: pstart = v; break;
        case 2: pstop = v; break;
        case 3: bnry = v; break;
        case 7: isr &= (u8)~v; break;
        case 8: rsar = (u16)((rsar & 0xFF00) | v); break;
        case 9: rsar = (u16)((rsar & 0x00FF) | v << 8); break;
        case 10: rbcr = (u16)((rbcr & 0xFF00) | v); break;
        case 11: rbcr = (u16)((rbcr & 0x00FF) | v << 8); break;
        case 15: imr = v; break;
        }
    } else if (page == 1) {
        if (p == 7)
            curr = v;
        else if (p >= 1 && p <= 6)
            par[p - 1] = v;
    }
}

static void dma_step(void)
{
    rsar = (u16)(rsar + 2);
    if (rsar >= pstop << 8 && pstop)            /* (the ring's end: on at its start, as the card reads it) */
        rsar = (u16)(pstart << 8);
    rbcr = (u16)(rbcr >= 2 ? rbcr - 2 : 0);
    if (!rbcr)
        isr |= 0x40;
}

u16 inw(u16 p)
{
    u16 v;
    if (p != BASE + 0x10)
        return 0xFFFF;
    v = (u16)(mem[rsar] | mem[(u16)(rsar + 1)] << 8);
    dma_step();
    return v;
}

void outw(u16 p, u16 v)
{
    if (p != BASE + 0x10)
        return;
    mem[rsar] = (u8)v, mem[(u16)(rsar + 1)] = (u8)(v >> 8);
    dma_step();
}

/* ---- the kernel around the driver */
static struct nic *card;
static char logged[4096];
static u32 got, got_len[64];
static u8 got_first[64], got_last[64];

void kprintf(const char *fmt, ...)
{
    va_list ap;
    size_t n = strlen(logged);
    va_start(ap, fmt);
    vsnprintf(logged + n, sizeof logged - n, fmt, ap);
    va_end(ap);
}

struct pci_dev *pci_find(u16 vendor, u16 device, int nth) { (void)vendor, (void)device, (void)nth; return 0; }
u32 pci_read32(u32 bus, u32 dev, u32 fn, u32 reg) { (void)bus, (void)dev, (void)fn, (void)reg; return 0; }
void pci_write32(u32 bus, u32 dev, u32 fn, u32 reg, u32 v) { (void)bus, (void)dev, (void)fn, (void)reg, (void)v; }
int nic_pci(struct pci_dev *d, const char *name) { (void)d, (void)name; return -1; }
void nic_claimed(struct nic *n, struct pci_dev *d) { (void)d; card = n; }
void vdev_hide(u16 base, u16 len) { (void)base, (void)len; }

void net_rx(struct nic *n, const u8 *frame, u32 len)
{
    (void)n;
    if (got < 64)
        got_len[got] = len, got_first[got] = frame[0], got_last[got] = frame[len - 1];
    got++;
}

/* ---- the test */

/* A frame of `len` bytes into the ring at the card's pointer, as the card writes it: the header (received
   intact, the next frame's page, the length with the header's four bytes), the bytes round the ring's end;
   the pointer moves on. `named`: the next page the header says, or 0 for the right one. Its first page. */
static unsigned put(u32 len, u8 fill, unsigned named)
{
    unsigned page = curr, next = page + ((len + 4 + 4 + 255) >> 8), at, i;
    if (next >= RX_STOP)
        next -= RX_STOP - RX_START;
    at = page << 8;
    mem[at] = 0x01, mem[at + 1] = (u8)(named ? named : next), mem[at + 2] = (u8)(len + 4), mem[at + 3] = (u8)((len + 4) >> 8);
    for (i = 0; i < len; i++) {
        unsigned a = at + 4 + i;
        if (a >= RX_STOP << 8)
            a -= (RX_STOP - RX_START) << 8;
        mem[a] = (u8)(i == 0 || i == len - 1 ? fill : 0x55);
    }
    curr = (u8)next;
    isr |= 0x01;
    return page;
}

static void turn(void)
{
    got = 0;
    logged[0] = 0;
    card->service(card);
}

static void hung(int sig)
{
    static const char said[] = "FAIL ne2k_test: the driver did not come back (its receive loop went round for ever)\n";
    (void)sig;
    (void)!write(1, said, sizeof said - 1);
    _exit(1);
}

int main(void)
{
    static const u8 mac[6] = { 0x00, 0x00, 0xC0, 0x12, 0x34, 0x56 };
    u8 frame[100];
    unsigned i, page;

    signal(SIGALRM, hung);
    alarm(20);
    for (i = 0; i < 6; i++)
        mem[2 * i] = mem[2 * i + 1] = mac[i];   /* the PROM, each byte twice as a word-wide card has it */

    /* Found, set up, its interrupt line made out. */
    CHECK(ne2k_probe() == 0 && card);
    if (!card) {
        printf("ne2k_test: no card\n");
        return 1;
    }
    CHECK(!memcmp(card->mac, mac, 6) && card->irq == 10 && card->base == BASE);
    CHECK(bnry == RX_START && curr == RX_START + 1 && pstart == RX_START && pstop == RX_STOP);

    /* Three frames, whole. */
    put(60, 0xA1, 0);
    put(300, 0xA2, 0);
    put(1514, 0xA3, 0);
    turn();
    CHECK(got == 3 && got_len[0] == 60 && got_len[1] == 300 && got_len[2] == 1514);
    CHECK(got_first[0] == 0xA1 && got_last[0] == 0xA1 && got_first[2] == 0xA3 && got_last[2] == 0xA3);
    CHECK(card->rx_frames == 3 && card->rx_errors == 0 && !logged[0]);

    /* Round the ring's end: frames until the pointer has gone past it, each received, the last across it. */
    for (i = 0; i < 12; i++) {
        put(1514, (u8)(0xB0 + i), 0);
        turn();
        CHECK(got == 1 && got_len[0] == 1514 && got_first[0] == 0xB0 + i && got_last[0] == 0xB0 + i);
    }
    CHECK(card->rx_errors == 0 && !logged[0]);

    /* A header that names its own page: not believed, the ring begun again, the driver back. */
    page = curr;
    put(200, 0xC1, page);
    turn();
    CHECK(got == 0 && card->rx_errors == 1 && strstr(logged, "GLOS-NET ne2000 ring restart why=header"));
    CHECK(bnry == RX_START && curr == RX_START + 1);
    put(64, 0xC2, 0);                           /* ... and frames come again */
    turn();
    CHECK(got == 1 && got_len[0] == 64 && got_first[0] == 0xC2);

    /* One that names a page behind it, in the ring. */
    put(100, 0xC3, 0);
    page = curr;
    put(100, 0xC4, RX_START + 1);
    turn();
    CHECK(got == 1 && got_first[0] == 0xC3 && card->rx_errors == 2 && strstr(logged, "why=header"));
    CHECK(bnry == RX_START && curr == RX_START + 1);

    /* One whose next page is pages past where its length ends. */
    page = curr;
    put(100, 0xC5, page + 5);
    turn();
    CHECK(got == 0 && card->rx_errors == 3 && strstr(logged, "why=header"));

    /* The card's own pointer outside its ring. */
    put(100, 0xC6, 0);
    curr = 0x20;
    turn();
    CHECK(card->rx_errors == 4 && strstr(logged, "why=pointer") && curr == RX_START + 1);

    /* Headers that go round the ring, each right in itself, and never meet the card's pointer (it stands in
       the middle of one of the "frames"): after four rings full the driver gives up and begins again. */
    for (page = RX_START; page < RX_STOP; page += 2) {
        unsigned next = page + 2 >= RX_STOP ? RX_START : page + 2;
        mem[page << 8] = 0x01, mem[(page << 8) + 1] = (u8)next, mem[(page << 8) + 2] = 0x40, mem[(page << 8) + 3] = 0x01;
    }
    bnry = RX_STOP - 1;                         /* (the next frame to take: the ring's first page) */
    curr = RX_START + 1;                        /* (an odd page: the chain of even ones never comes to it) */
    isr |= 0x01;
    turn();
    CHECK(got == 4 * (RX_STOP - RX_START) && strstr(logged, "why=endless") && card->rx_errors == 5);
    CHECK(bnry == RX_START && curr == RX_START + 1);
    put(64, 0xC7, 0);
    turn();
    CHECK(got == 1 && got_first[0] == 0xC7 && !logged[0]);

    /* A frame going out when the ring is begun again (the card is stopped under it, and its interrupt never
       comes): the next frame is sent, not refused for ever. */
    memset(frame, 0x11, sizeof frame);
    tx_holds = 1;
    sent = 0;
    CHECK(card->send(card, frame, 100) == 0 && sent == 1);
    page = curr;
    put(200, 0xC8, page);                       /* (a bad header: the ring begun again, the card stopped) */
    turn();
    tx_holds = 0;
    CHECK(card->send(card, frame, 100) == 0 && sent == 2);
    /* ... and one whose interrupt was lost, the transmitter idle: the same. */
    isr &= (u8)~0x02;
    CHECK(card->send(card, frame, 100) == 0 && sent == 3);

    printf("ne2k_test: %d failures\n", fails);
    return fails != 0;
}
