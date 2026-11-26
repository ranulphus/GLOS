/* EHCI's queue heads and qTDs (docs/v1/usb-storage.md, U4): pure, no I/O, host tested (tests/host/ehci_test.c).
 * From the EHCI specification 1.0 §3.5 (the qTD: next, alternate next, token: status, PID, CERR, C_Page, IOC, total
 * bytes, data toggle; five buffer pointers, the first with its offset, the rest 4 KB pages) and §3.6 (the QH's
 * endpoint characteristics and capabilities), and USB 2.0 §8.5.3 (a control transfer's stages and toggles). Every
 * qTD has CERR 3 (three tries for a transaction error); high speed only (no split transactions). */
#include "usb.h"

#define TOK_OUT     (0u << 8)
#define TOK_IN      (1u << 8)
#define TOK_SETUP   (2u << 8)
#define TOK_CERR3   (3u << 10)
#define TOK_IOC     (1u << 15)
#define TOK_DT      (1u << 31)

#define QH_HIGH     (2u << 12)          /* EPS: high speed */
#define QH_DTC      (1u << 14)          /* the toggle from each qTD */
#define QH_HEAD     (1u << 15)          /* H: the asynchronous list's head */
#define QH_MULT1    (1u << 30)

#define CHUNK       16384u              /* (a bulk qTD's most: 32 packets of 512, fits five pages from any offset) */

u32 ehci_qh_ep1(u32 addr, u32 ep, u32 maxp, int head)
{
    return (addr & 0x7F) | (ep & 0xF) << 8 | QH_HIGH | QH_DTC | (head ? QH_HEAD : 0) | (maxp & 0x7FF) << 16;
}

u32 ehci_qh_ep2(void) { return QH_MULT1; }

/* One qTD: len bytes from phys (len <= 20 KB less phys's offset in its page). */
static void qtd_set(struct ehci_qtd *q, u32 pid, u32 toggle, u32 phys, u32 len, int ioc, u32 next)
{
    u32 i, page = phys & ~0xFFFu;
    q->next = next;
    q->alt = EHCI_T;
    q->buf[0] = phys;
    for (i = 1; i < 5; i++)
        q->buf[i] = len ? page + i * 4096u : 0;
    q->token = (toggle ? TOK_DT : 0) | (len & 0x7FFF) << 16 | (ioc ? TOK_IOC : 0) | TOK_CERR3 | pid | EHCI_TOK_ACTIVE;
}

u32 ehci_control_qtds(struct ehci_qtd *q, u32 max, u32 q_phys, const u8 setup[8], u32 setup_phys, u32 data_phys)
{
    u32 len = setup[6] | (u32)setup[7] << 8, n = 0;
    int in = (setup[0] & 0x80) != 0;
    if (max < 3 || len > CHUNK)
        return 0;
    qtd_set(&q[n], TOK_SETUP, 0, setup_phys, 8, 0, q_phys + (n + 1) * (u32)sizeof *q);
    n++;
    if (len) {
        qtd_set(&q[n], in ? TOK_IN : TOK_OUT, 1, data_phys, len, 0, q_phys + (n + 1) * (u32)sizeof *q);
        q[n].alt = q[n].next;           /* (a data stage that comes in short goes on to the status stage all the same) */
        n++;
    }
    /* the status stage: the other way (IN when nothing came in), DATA1, zero bytes; the queue's end */
    qtd_set(&q[n], in && len ? TOK_OUT : TOK_IN, 1, 0, 0, 1, EHCI_T);
    return n + 1;
}

u32 ehci_bulk_qtds(struct ehci_qtd *q, u32 max, u32 q_phys, int in, u32 data_phys, u32 len, u32 maxp, u32 toggle)
{
    u32 n = 0, at = 0;
    if (!maxp)
        return 0;
    do {
        u32 now = len - at < CHUNK ? len - at : CHUNK, packets = now ? (now + maxp - 1) / maxp : 1;
        int last = at + now >= len;
        if (n == max)
            return 0;
        qtd_set(&q[n], in ? TOK_IN : TOK_OUT, toggle & 1, data_phys + at, now, last,
                last ? EHCI_T : q_phys + (n + 1) * (u32)sizeof *q);
        toggle ^= packets & 1;
        at += now;
        n++;
    } while (at < len);
    return n;
}

u32 ehci_qtd_moved(const struct ehci_qtd *q, u32 asked)
{
    u32 left = EHCI_TOK_LEFT(q->token);
    return left <= asked ? asked - left : 0;
}

u32 ehci_toggle_after(u32 toggle, u32 moved, u32 asked, u32 maxp)
{
    u32 packets;
    if (!maxp)
        return toggle & 1;
    packets = moved / maxp;
    if (moved % maxp || moved < asked || !asked)
        packets++;                      /* (a short or zero-length last packet counts: it was sent, and flipped it) */
    return (toggle ^ packets) & 1;
}
