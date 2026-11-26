/* Host test for EHCI's queue heads and qTDs (kernel/drv/ehcitd.c): the QH's words; a control transfer's stages
 * (PIDs, toggles, the status stage's direction, IOC on the last alone, the links); a bulk transfer cut in 16 KB
 * qTDs from a buffer not on a page's start (pages, lengths, each qTD's starting toggle); the bytes moved; the
 * toggle after short, whole and zero-length transfers. */
#include <stdio.h>
#include <string.h>
#include "usb.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define PID(t)   (((t) >> 8) & 3)
#define DT(t)    ((t) >> 31)
#define LEN(t)   EHCI_TOK_LEFT(t)
#define IOC(t)   (((t) >> 15) & 1)
#define CERR(t)  (((t) >> 10) & 3)

int main(void)
{
    struct ehci_qtd q[8];
    u32 base = 0x123000, n, i;
    u8 get_desc[8] = { 0x80, 6, 0, 1, 0, 0, 18, 0 };            /* GET_DESCRIPTOR(device), 18 bytes in */
    u8 set_addr[8] = { 0x00, 5, 7, 0, 0, 0, 0, 0 };             /* SET_ADDRESS(7): no data */
    u8 set_out[8] = { 0x21, 9, 0, 2, 0, 0, 4, 0 };              /* a class request with 4 bytes out */

    CHECK(ehci_qh_ep1(5, 2, 512, 0) == (5u | 2u << 8 | 2u << 12 | 1u << 14 | 512u << 16));
    CHECK(ehci_qh_ep1(0, 0, 64, 1) == (2u << 12 | 1u << 14 | 1u << 15 | 64u << 16));
    CHECK(ehci_qh_ep2() == 1u << 30);

    memset(q, 0, sizeof q);                     /* control IN: setup, data in, status out */
    n = ehci_control_qtds(q, 8, base, get_desc, 0x200000, 0x201000);
    CHECK(n == 3);
    CHECK(PID(q[0].token) == 2 && DT(q[0].token) == 0 && LEN(q[0].token) == 8 && q[0].buf[0] == 0x200000);
    CHECK(PID(q[1].token) == 1 && DT(q[1].token) == 1 && LEN(q[1].token) == 18 && q[1].buf[0] == 0x201000);
    CHECK(PID(q[2].token) == 0 && DT(q[2].token) == 1 && LEN(q[2].token) == 0);
    CHECK(!IOC(q[0].token) && !IOC(q[1].token) && IOC(q[2].token));
    CHECK(q[0].next == base + 32 && q[1].next == base + 64 && q[2].next == EHCI_T);
    for (i = 0; i < 3; i++)
        CHECK((q[i].token & EHCI_TOK_ACTIVE) && CERR(q[i].token) == 3);
    CHECK(q[0].alt == EHCI_T && q[1].alt == base + 64 && q[2].alt == EHCI_T);  /* (a short data stage: to status) */

    n = ehci_control_qtds(q, 8, base, set_addr, 0x200000, 0x201000);    /* no data: status IN */
    CHECK(n == 2 && PID(q[1].token) == 1 && DT(q[1].token) == 1 && IOC(q[1].token) && q[1].next == EHCI_T);
    n = ehci_control_qtds(q, 8, base, set_out, 0x200000, 0x201000);     /* data out: status IN */
    CHECK(n == 3 && PID(q[1].token) == 0 && LEN(q[1].token) == 4 && PID(q[2].token) == 1);
    CHECK(ehci_control_qtds(q, 2, base, get_desc, 0, 0) == 0);           /* (no room) */

    /* bulk IN of 40960 bytes from 200h into a page, 512-byte packets, starting from DATA1 */
    n = ehci_bulk_qtds(q, 8, base, 1, 0x300200, 40960, 512, 1);
    CHECK(n == 3);
    CHECK(LEN(q[0].token) == 16384 && LEN(q[1].token) == 16384 && LEN(q[2].token) == 8192);
    CHECK(q[0].buf[0] == 0x300200 && q[0].buf[1] == 0x301000 && q[0].buf[4] == 0x304000);
    CHECK(q[1].buf[0] == 0x304200 && q[1].buf[1] == 0x305000);
    CHECK(q[2].buf[0] == 0x308200);
    /* 32 packets a full qTD: an even number, so each starts from DATA1 again */
    CHECK(DT(q[0].token) == 1 && DT(q[1].token) == 1 && DT(q[2].token) == 1);
    CHECK(PID(q[0].token) == 1 && !IOC(q[0].token) && !IOC(q[1].token) && IOC(q[2].token));
    CHECK(q[0].next == base + 32 && q[1].next == base + 64 && q[2].next == EHCI_T);

    /* bulk OUT of 31 bytes (a CBW), 64-byte packets (as if full speed's), DATA0 */
    n = ehci_bulk_qtds(q, 8, base, 0, 0x300000, 31, 64, 0);
    CHECK(n == 1 && PID(q[0].token) == 0 && DT(q[0].token) == 0 && LEN(q[0].token) == 31 && IOC(q[0].token));
    /* an odd number of packets in a qTD starts the next one from the other toggle: 16384 bytes at 1000 a packet
       are 17 (the toggles' arithmetic alone: a real high-speed bulk endpoint's packets are 512) */
    n = ehci_bulk_qtds(q, 8, base, 0, 0x300000, 20000, 1000, 0);
    CHECK(n == 2 && DT(q[0].token) == 0 && DT(q[1].token) == 1);
    CHECK(ehci_bulk_qtds(q, 1, base, 1, 0x300000, 40960, 512, 0) == 0);  /* (no room) */
    n = ehci_bulk_qtds(q, 8, base, 1, 0x300000, 0, 512, 0);             /* a zero-length one: one qTD */
    CHECK(n == 1 && LEN(q[0].token) == 0);

    /* what a qTD moved, as the controller leaves its token */
    q[0].token = (13u << 16) | 0;               /* (13 left of 31) */
    CHECK(ehci_qtd_moved(&q[0], 31) == 18);
    q[0].token = 0;
    CHECK(ehci_qtd_moved(&q[0], 31) == 31);

    /* the toggle after: whole packets, a short last one, a zero-length one ending a short transfer, none asked */
    CHECK(ehci_toggle_after(0, 1024, 1024, 512) == 0);  /* 2 packets */
    CHECK(ehci_toggle_after(0, 13, 13, 512) == 1);      /* 1 (short) */
    CHECK(ehci_toggle_after(1, 31, 31, 512) == 0);      /* a CBW: 1 */
    CHECK(ehci_toggle_after(0, 512, 4096, 512) == 0);   /* 512 then a zero-length packet: 2 */
    CHECK(ehci_toggle_after(0, 600, 4096, 512) == 0);   /* 512 then 88: 2 */
    CHECK(ehci_toggle_after(1, 0, 0, 512) == 0);        /* a zero-length transfer: 1 */

    printf("ehci_test: %d failures\n", fails);
    return fails != 0;
}
