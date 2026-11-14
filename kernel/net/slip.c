/* The cable link (slip.h; docs/cable.md): IP between this PC and one other over a serial cable, framed as
 * RFC 1055 says (SLIP). An interface of lwIP's beside the network card's, or the only one on a PC with no
 * card GLOS drives: SSH, SFTP, VNC and the apps' sockets go over it as over Ethernet.
 *
 * Point to point, with fixed addresses by role: the server's 192.168.250.1, the client's 192.168.250.2 (the
 * roles say no more than that). SLIP has no checks of its own: a damaged packet fails IP's or TCP's checksum
 * and is sent again.
 *
 * As a client with no card of its own, the PC also asks for an address over the cable (DHCP, as over a card):
 * a GLOS PC at the far end with a network card passes the request to its network (kernel/net/member.c), and
 * the cable's interface then has an address of that network, with its gateway and name servers. Until an
 * answer comes, and whenever the far end has no network to offer, the fixed pair stands. [net] cable_join =
 * off: never asked. */
#include <string.h>

#include "lwip/dhcp.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"

#include "glos/bootinfo.h"
#include "kcfg.h"
#include "kprintf.h"
#include "nic.h"
#include "vm.h"
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
static u8 asking, asked, member;        /* a client alone: DHCP runs on the cable; it has an address by it */
static ip4_addr_t fixed_ip, fixed_mask, fixed_peer;

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
    if (flen < 20 || (frame[0] >> 4) != 4 || !(p = pbuf_alloc(PBUF_LINK, (u16_t)flen, PBUF_POOL))) {   /* (with room
                                                   in front for an Ethernet header: a member's packet is
                                                   forwarded out of the card as it is, member.c) */
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
    if (role == BI_CABLE_CLIENT && !nic0 && !asked && kcfg_settled()) {
        char v[8];                              /* (once GLOS.CFG is read: is the far end's network to be joined?) */
        v[0] = 0;
        kcfg_get("net", "cable_join", v, sizeof v);
        asked = 1;
        if (strcmp(v, "off") && dhcp_start(&sl) == ERR_OK)
            asking = 1;
    }
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

/* The interface's address has changed. DHCP's (this PC a member of the far end's network): said, and the rest
   of GLOS told. None (a lease that ran out, with nobody to renew it): the fixed pair again. */
static void sl_status(struct netif *n)
{
    static u8 busy;
    char ip[16], gw[16], mask[16];
    if (busy || !asking)
        return;
    if (ip4_addr_isany_val(*netif_ip4_addr(n))) {
        busy = 1;
        netif_set_addr(n, &fixed_ip, &fixed_mask, &fixed_peer);
        busy = 0;
        if (member) {
            member = 0;
            kprintf("GLOS-NET cable member=no\n");
            net_cable_member(n, 0);
        }
        return;
    }
    if (ip4_addr_eq(netif_ip4_addr(n), &fixed_ip))
        return;
    member = 1;
    ip4addr_ntoa_r(netif_ip4_addr(n), ip, sizeof ip);
    ip4addr_ntoa_r(netif_ip4_gw(n), gw, sizeof gw);
    ip4addr_ntoa_r(netif_ip4_netmask(n), mask, sizeof mask);
    kprintf("GLOS-NET cable member ip=%s gw=%s mask=%s\n", ip, gw, mask);
    net_cable_member(n, 1);
}

static err_t sl_init(struct netif *n)
{
    const struct bootinfo *bi = vm.bi;
    u32 h = 2166136261u, i;
    n->name[0] = 's', n->name[1] = 'l';
    n->output = slip_out;
    n->mtu = MTU;
    n->flags = NETIF_FLAG_LINK_UP | NETIF_FLAG_BROADCAST | NETIF_FLAG_IGMP;
    /* A hardware address of its own, for DHCP to know the PC by (a serial port has none): made of the PC's host
       key and name, so the same at every start; the bit that says "made up here" set. */
    for (i = 0; i < bi->hostkey_len; i++)
        h = (h ^ ((const u8 *)bi->hostkey)[i]) * 16777619u;
    for (i = 0; bi->hostname[i]; i++)
        h = (h ^ (u8)bi->hostname[i]) * 16777619u;
    n->hwaddr_len = 6;
    n->hwaddr[0] = 0x02, n->hwaddr[1] = 0x47, n->hwaddr[2] = 0x4C;
    n->hwaddr[3] = (u8)(h >> 16), n->hwaddr[4] = (u8)(h >> 8), n->hwaddr[5] = (u8)h;
    netif_set_hostname(n, bi->hostname[0] ? bi->hostname : "glos");
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
    fixed_ip = ip, fixed_mask = mask, fixed_peer = peer;
    asking = asked = member = 0;
    if (!added) {
        netif_add(&sl, &ip, &mask, &peer, NULL, sl_init, ip_input);
        netif_set_status_callback(&sl, sl_status);
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
    if (asking) {
        asking = 0;                             /* (before DHCP lets go: its doing is not ours to answer now) */
        dhcp_release_and_stop(&sl);
        if (member)
            net_cable_member(&sl, 0);
        member = 0;
    }
    netif_set_down(&sl);
    on = 0;
    uart_close();
    kprintf("GLOS-NET cable off\n");
}

void slip_leave(void) { uart_close(); }

int slip_up(void) { return on; }

struct netif *slip_netif(int server) { return on && (!server || role == BI_CABLE_SERVER) ? &sl : 0; }

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
    char ip[16], gw[16];
    u16 base;
    u32 irq, baud;
    int fifo;
    if (!on || uart_info(&base, &irq, &baud, &fifo, &c) != 0)
        return 0;
    ip4addr_ntoa_r(netif_ip4_addr(&sl), ip, sizeof ip);
    ip4addr_ntoa_r(netif_ip4_gw(&sl), gw, sizeof gw);
    return (u32)ksnprintf(buf, max, "cable=%s com=%u io=%03x irq=%u baud=%u fifo=%u ip=%s peer=%s%s\n"
                          "cable rx=%u tx=%u rx_bad=%u tx_dropped=%u bytes_rx=%u bytes_tx=%u overruns=%u line_errors=%u\n",
                          role == BI_CABLE_SERVER ? "server" : "client", com, base, irq, baud, fifo, ip, gw,
                          member ? " member=yes" : "", rx_packets, tx_packets, rx_bad, tx_dropped, c.rx, c.tx,
                          c.overruns, c.errors + c.dropped);
}
