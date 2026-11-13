/* The cable link (slip.h; docs/cable.md): IP between this PC and one other over a serial cable, framed as
 * RFC 1055 says (SLIP). An interface of lwIP's beside the network card's, or the only one on a PC with no
 * card GLOS drives: SSH, SFTP, VNC and the apps' sockets go over it as over Ethernet.
 *
 * Point to point, with fixed addresses by role: the server's 192.168.250.1, the client's 192.168.250.2 (the
 * roles say no more than that). SLIP has no checks of its own: a damaged packet fails IP's or TCP's checksum
 * and is sent again. Nothing is forwarded between the cable and the card. */
#include <string.h>

#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"

#include "glos/bootinfo.h"
#include "kprintf.h"
#include "nic.h"
#include "slip.h"
#include "uart.h"

#define END     0xC0
#define ESC     0xDB
#define ESC_END 0xDC
#define ESC_ESC 0xDD
#define MTU     576                             /* (a packet's 50 ms on the line at 115,200 baud) */

static struct netif sl;
static u8 on, added;
static u32 role, com;
static u8 frame[1600];
static u32 flen;
static u8 esc, bad;
static u32 rx_packets, tx_packets, rx_bad, tx_dropped;

static err_t slip_out(struct netif *n, struct pbuf *p, const ip4_addr_t *to)
{
    u8 b[128];
    struct pbuf *q;
    u32 need = 2, k = 0, i;
    (void)n;
    (void)to;
    for (q = p; q; q = q->next)
        for (i = 0; i < q->len; i++) {
            u8 c = ((const u8 *)q->payload)[i];
            need += c == END || c == ESC ? 2 : 1;
        }
    if (!on || uart_room() < need) {            /* (the line is behind: TCP sends it again later) */
        tx_dropped++;
        return ERR_MEM;
    }
    b[k++] = END;                               /* (what the line picked up since the last packet ends there) */
    for (q = p; q; q = q->next)
        for (i = 0; i < q->len; i++) {
            u8 c = ((const u8 *)q->payload)[i];
            if (k > sizeof b - 3) {
                uart_write(b, k);
                k = 0;
            }
            if (c == END)
                b[k++] = ESC, b[k++] = ESC_END;
            else if (c == ESC)
                b[k++] = ESC, b[k++] = ESC_ESC;
            else
                b[k++] = c;
        }
    b[k++] = END;
    uart_write(b, k);
    tx_packets++;
    return ERR_OK;
}

static void deliver(void)
{
    struct pbuf *p;
    if (flen < 20 || (frame[0] >> 4) != 4 || !(p = pbuf_alloc(PBUF_RAW, (u16_t)flen, PBUF_POOL))) {
        rx_bad++;                               /* (noise, a log's line from the other end, or no memory) */
        return;
    }
    pbuf_take(p, frame, (u16_t)flen);
    rx_packets++;
    if (sl.input(p, &sl) != ERR_OK)
        pbuf_free(p);
}

void slip_turn(void)
{
    u8 b[256];
    u32 n, i;
    if (!on)
        return;
    while ((n = uart_read(b, sizeof b)) != 0)
        for (i = 0; i < n; i++) {
            u8 c = b[i];
            if (c == END) {
                if (flen && !bad && !esc)
                    deliver();
                else if (flen)
                    rx_bad++;
                flen = 0, esc = bad = 0;
                continue;
            }
            if (esc) {
                esc = 0;
                if (c == ESC_END)
                    c = END;
                else if (c == ESC_ESC)
                    c = ESC;
                else
                    bad = 1;
            } else if (c == ESC) {
                esc = 1;
                continue;
            }
            if (flen < sizeof frame)
                frame[flen++] = c;
            else
                bad = 1;
        }
}

static err_t sl_init(struct netif *n)
{
    n->name[0] = 's', n->name[1] = 'l';
    n->output = slip_out;
    n->mtu = MTU;
    n->flags = NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

int slip_start(u32 r, u32 c, u32 baud, int alone)
{
    ip4_addr_t ip, mask, peer;
    u16 base;
    u32 irq, bd;
    int fifo, e;
    struct uart_counts counts;
    if (on)
        return -2;
    if (r != BI_CABLE_SERVER && r != BI_CABLE_CLIENT)
        return -1;
    if ((e = uart_open(c, baud ? baud : 115200, END, net_kick)) != 0) {
        kprintf("GLOS-NET cable error=%s com=%u\n", e == -1 ? "no-port" : e == -2 ? "in-use" : "speed", c);
        return e;
    }
    role = r, com = c;
    flen = 0, esc = bad = 0;
    rx_packets = tx_packets = rx_bad = tx_dropped = 0;
    IP4_ADDR(&ip, 192, 168, 250, r == BI_CABLE_SERVER ? 1 : 2);
    IP4_ADDR(&peer, 192, 168, 250, r == BI_CABLE_SERVER ? 2 : 1);
    IP4_ADDR(&mask, 255, 255, 255, 252);
    if (!added) {
        netif_add(&sl, &ip, &mask, &peer, NULL, sl_init, ip_input);
        added = 1;
    } else {
        netif_set_addr(&sl, &ip, &mask, &peer);
    }
    if (alone)
        netif_set_default(&sl);
    on = 1;
    netif_set_up(&sl);
    uart_info(&base, &irq, &bd, &fifo, &counts);
    kprintf("GLOS-NET cable role=%s com=%u io=%03x irq=%u baud=%u fifo=%u ip=192.168.250.%u peer=192.168.250.%u\n",
            r == BI_CABLE_SERVER ? "server" : "client", c, base, irq, bd, fifo, r == BI_CABLE_SERVER ? 1 : 2,
            r == BI_CABLE_SERVER ? 2 : 1);
    return 0;
}

void slip_stop(void)
{
    if (!on)
        return;
    netif_set_down(&sl);
    on = 0;
    uart_close();
    kprintf("GLOS-NET cable off\n");
}

void slip_leave(void) { uart_close(); }

int slip_up(void) { return on; }

int slip_address(char ip[16])
{
    if (!on)
        return 0;
    ip4addr_ntoa_r(netif_ip4_addr(&sl), ip, 16);
    return 1;
}

u32 slip_stat(char *buf, u32 max)
{
    struct uart_counts c;
    u16 base;
    u32 irq, baud;
    int fifo;
    if (!on || uart_info(&base, &irq, &baud, &fifo, &c) != 0)
        return 0;
    return (u32)ksnprintf(buf, max, "cable=%s com=%u io=%03x irq=%u baud=%u fifo=%u ip=192.168.250.%u peer=192.168.250.%u\n"
                          "cable rx=%u tx=%u rx_bad=%u tx_dropped=%u bytes_rx=%u bytes_tx=%u overruns=%u line_errors=%u\n",
                          role == BI_CABLE_SERVER ? "server" : "client", com, base, irq, baud, fifo,
                          role == BI_CABLE_SERVER ? 1 : 2, role == BI_CABLE_SERVER ? 2 : 1, rx_packets, tx_packets, rx_bad,
                          tx_dropped, c.rx, c.tx, c.overruns, c.errors + c.dropped);
}
