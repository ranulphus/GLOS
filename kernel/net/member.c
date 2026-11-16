/* A cable's far PC as a member of this PC's network (docs/v1/cable-network.md, step N1; member.h).
 *
 * This PC has a network card on a network and a cable link in the server role. For the PC at the cable's far
 * end it does four things, all in the net thread:
 *
 *   - its request for an address (DHCP, which it makes over the cable as it would over a card) is passed on
 *     to the card's network as a broadcast with the "answer by broadcast" flag set, and the answers, which
 *     name it by its hardware address, are passed back down the cable. So the network's own DHCP server gives
 *     it an address, and this PC, reading the answer as it passes, knows that address and how long it holds;
 *   - it answers on the Ethernet for that address (proxy ARP), so that the network's packets for the far PC
 *     come to this PC's card;
 *   - it forwards the far PC's packets and no others: one from the cable out through the card, one for the
 *     far PC's address down the cable, each with its time-to-live counted down (forward(), below. Not lwIP's
 *     ip4_forward: lwIP 2.2.0's clears the IP, UDP and TCP checksums of what it forwards whenever checksums
 *     are made in software, as GLOS's are, and every packet it passed was thrown away at the other end);
 *     this PC's own packets for the far PC are routed down the cable (member_route);
 *   - broadcasts and multicasts (the .local names' lookups, the announcements LAN programs find each other
 *     by) are copied both ways, to a serial cable only as fast as it can take them.
 *
 * The far PC is then one more PC of the network: an address of its own, reached by it from anywhere there.
 * It is not hidden behind this PC's address (no NAT), and no Ethernet frame is bridged: only IP crosses.
 * [net] cable_share = off keeps the cable to the two PCs. A chain (a third PC behind the second) is not done. */
#include <string.h>

#include "lwip/etharp.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/ip4.h"
#include "lwip/ip4_frag.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/prot/ip4.h"

#include "kcfg.h"
#include "kprintf.h"
#include "member.h"
#include "nic.h"
#include "timer.h"

#define MEMBERS  4
#define COPY_BPS 2048u                  /* broadcasts and multicasts copied to the cable: bytes a second at most
                                           (a serial cable carries 11 KB a second in all) */

struct member {
    u8 used, bound;
    u8 mac[6];                          /* the hardware address its DHCP messages give */
    ip4_addr_t ip;                      /* its address, once the server has acknowledged it */
    u32 until;                          /* the tick its lease ends at */
    u32 asked;                          /* the tick of its last request (one never answered is forgotten) */
    u32 heard;                          /* the tick anything last came from it */
    u32 probed;                         /* the tick it was last asked whether it is there */
};
#define QUIET   (30 * 1024u)            /* silent this long, a member is asked for an echo, and again each ... */
#define GONE    (100 * 1024u)           /* ... and silent this long it has gone: its cable is out, or it is off */
#define PROBE_ID 0x474Cu

static struct member mem[MEMBERS];
static struct netif *card, *cable;
static u8 frame[1600];
static u32 copy_tick, copy_left;        /* the cable's budget for copies, a second at a time */
static u32 relayed, answered, copied_out, copied_in, copy_dropped, forwarded, too_long, probes, cable_answers;

static int sharing(void)
{
    char v[8];
    if (!card || !cable || !nic0 || !netif_is_up(cable))
        return 0;
    v[0] = 0;
    kcfg_get("net", "cable_share", v, sizeof v);
    return strcmp(v, "off") != 0;
}

void member_links(struct netif *c, struct netif *k)
{
    card = c;
    if (cable != k)
        memset(mem, 0, sizeof mem);     /* (another cable, or none: its far PC has gone) */
    cable = k;
}

static struct member *by_mac(const u8 *mac, int make)
{
    u32 i;
    struct member *spare = 0;
    for (i = 0; i < MEMBERS; i++) {
        if (mem[i].used && !memcmp(mem[i].mac, mac, 6))
            return &mem[i];
        if (!mem[i].used && !spare)
            spare = &mem[i];
    }
    if (!make || !spare)
        return 0;
    memset(spare, 0, sizeof *spare);
    spare->used = 1;
    memcpy(spare->mac, mac, 6);
    return spare;
}

static struct member *by_ip(u32 ip)
{
    u32 i;
    for (i = 0; i < MEMBERS; i++)
        if (mem[i].used && mem[i].bound && ip4_addr_get_u32(&mem[i].ip) == ip)
            return &mem[i];
    return 0;
}

static int any_bound(void)
{
    u32 i;
    for (i = 0; i < MEMBERS; i++)
        if (mem[i].used && mem[i].bound)
            return 1;
    return 0;
}

/* An ARP frame from this PC's card: an answer (to `to`, or to everyone when to is 0) that ip is at our address. */
static void arp_say(u32 ip, const u8 *to_mac, u32 to_ip)
{
    u8 *f = frame;
    memset(f, 0, 60);
    if (to_mac)
        memcpy(f, to_mac, 6);
    else
        memset(f, 0xFF, 6);
    memcpy(f + 6, nic0->mac, 6);
    f[12] = 0x08, f[13] = 0x06;
    f[15] = 1, f[16] = 0x08, f[18] = 6, f[19] = 4;
    f[21] = to_mac ? 2 : 1;                     /* a reply; unasked, a request for its own address (gratuitous) */
    memcpy(f + 22, nic0->mac, 6);
    memcpy(f + 28, &ip, 4);
    if (to_mac)
        memcpy(f + 32, to_mac, 6);
    memcpy(f + 38, to_mac ? &to_ip : &ip, 4);
    nic0->send(nic0, f, 60);
}

/* An IP packet out of the card as it is, to everyone or to its multicast group's Ethernet address. */
static void card_send_ip(const u8 *ip, u32 len)
{
    u8 *f = frame;
    if (len > sizeof frame - 14)
        return;
    if ((ip[16] & 0xF0) == 0xE0) {              /* 224.0.0.0/4: 01:00:5E and the group's low 23 bits */
        f[0] = 0x01, f[1] = 0x00, f[2] = 0x5E, f[3] = ip[17] & 0x7F, f[4] = ip[18], f[5] = ip[19];
    } else {
        memset(f, 0xFF, 6);
    }
    memcpy(f + 6, nic0->mac, 6);
    f[12] = 0x08, f[13] = 0x00;
    memcpy(f + 14, ip, len);
    nic0->send(nic0, f, len + 14 < 60 ? 60 : len + 14);
}

/* An IP packet down the cable as it is. */
static void cable_send_ip(const u8 *ip, u32 len)
{
    struct pbuf *p;
    ip4_addr_t to;
    if (len > cable->mtu || !(p = pbuf_alloc(PBUF_LINK, (u16_t)len, PBUF_POOL)))    /* (room in front for an
                                                   Ethernet header: a parallel cable's interface has ARP) */
        return;
    pbuf_take(p, ip, (u16_t)len);
    memcpy(&to, ip + 16, 4);
    cable->output(cable, p, &to);
    pbuf_free(p);
}

static int copy_budget(u32 len)
{
    u32 now = timer_ticks();
    if (now - copy_tick >= 1024) {
        copy_tick = now;
        copy_left = COPY_BPS;
    }
    if (len > copy_left) {
        copy_dropped++;
        return 0;
    }
    copy_left -= len;
    return 1;
}

/* A DHCP message's type (option 53) and lease (option 51, seconds; 0 if it has none). bootp: the BOOTP message,
   n bytes. */
static int dhcp_type(const u8 *bootp, u32 n, u32 *lease)
{
    u32 i = 240;
    int type = 0;
    *lease = 0;
    if (n < 240 || bootp[236] != 0x63 || bootp[237] != 0x82 || bootp[238] != 0x53 || bootp[239] != 0x63)
        return 0;
    while (i < n && bootp[i] != 0xFF) {
        u32 len;
        if (bootp[i] == 0) {
            i++;
            continue;
        }
        if (i + 1 >= n)
            break;
        len = bootp[i + 1];
        if (i + 2 + len > n)
            break;
        if (bootp[i] == 53 && len == 1)
            type = bootp[i + 2];
        if (bootp[i] == 51 && len == 4)
            *lease = (u32)bootp[i + 2] << 24 | (u32)bootp[i + 3] << 16 | (u32)bootp[i + 4] << 8 | bootp[i + 5];
        i += 2 + len;
    }
    return type;
}

/* The IPv4 header's length if ip..ip+len is a whole UDP packet for port `dport`, else 0. */
static u32 udp_for(const u8 *ip, u32 len, u16 dport)
{
    u32 ihl = (ip[0] & 15u) * 4, total = (u32)ip[2] << 8 | ip[3];
    if (len < 28 || (ip[0] >> 4) != 4 || ihl < 20 || total > len || total < ihl + 8 || ip[9] != 17
        || (((u32)ip[6] << 8 | ip[7]) & 0x3FFF))    /* (a fragment: left to the stack) */
        return 0;
    return ((u32)ip[ihl + 2] << 8 | ip[ihl + 3]) == dport ? ihl : 0;
}

/* ---- from a cable that carries Ethernet frames (the parallel link: N3) */

/* A frame that has come by the cable, before lwIP has it. On such a cable the far PC, which has the network's
   address and mask on its interface, asks by ARP for every address of the network it wants: this PC answers
   for all of them (but the far PC's own and this PC's on the cable, which lwIP answers), so that everything
   the far PC sends comes here, to be forwarded. */
void member_cable_frame(const u8 *f, u32 len)
{
    u32 asker, target;
    struct pbuf *p;
    u8 *r;
    if (len < 42 || !cable || !(cable->flags & NETIF_FLAG_ETHARP) || f[12] != 0x08 || f[13] != 0x06 || f[21] != 1
        || !sharing())
        return;
    memcpy(&asker, f + 28, 4);
    memcpy(&target, f + 38, 4);
    if (!by_ip(asker) || target == asker || target == ip4_addr_get_u32(netif_ip4_addr(cable)))
        return;
    if (!(p = pbuf_alloc(PBUF_RAW, 60, PBUF_POOL)))
        return;
    r = (u8 *)p->payload;
    memset(r, 0, 60);
    memcpy(r, f + 6, 6);
    memcpy(r + 6, cable->hwaddr, 6);
    r[12] = 0x08, r[13] = 0x06;
    r[15] = 1, r[16] = 0x08, r[18] = 6, r[19] = 4, r[21] = 2;
    memcpy(r + 22, cable->hwaddr, 6);
    memcpy(r + 28, &target, 4);
    memcpy(r + 32, f + 22, 6);
    memcpy(r + 38, &asker, 4);
    cable->linkoutput(cable, p);
    pbuf_free(p);
    cable_answers++;
}

/* A broadcast of this PC's own (an app's datagram to everyone, or to the card's network's broadcast address) goes
   out of the card as lwIP routes it: the cable's interface if a copy is due there too, for the members, within
   the cable's means; and the card's address, which the copy says it is from (the members answer to it). */
struct netif *member_own_broadcast(u32 dest, u32 len, u32 *src)
{
    if (!card || !cable || !sharing() || !any_bound() || !netif_is_up(cable))
        return 0;
    if (dest != 0xFFFFFFFFu && !ip4_addr_isbroadcast_u32(dest, card))
        return 0;
    if (!copy_budget(len + 28))
        return 0;
    *src = ip4_addr_get_u32(netif_ip4_addr(card));
    copied_in++;
    return cable;
}

/* A packet of this PC's own for a member goes down its cable (member_route), and lwIP would give it the cable
   interface's address as its source: the pair's private one, which no other PC of the network knows this PC
   by. The cable's interface if dest is a member, and the card's address for the packet to come from: this PC is
   one PC on the network, whichever way a packet leaves it. (Echoes of the Void's two-PC test, 2026-11-16: the
   far PC found the near one's game "at 192.168.250.1".) */
struct netif *member_source(u32 dest, u32 *src)
{
    if (!card || !cable || !sharing() || !by_ip(dest))
        return 0;
    *src = ip4_addr_get_u32(netif_ip4_addr(card));
    return cable;
}

int member_cable_mac(u32 ip, u8 mac[6])
{
    struct member *m = by_ip(ip);
    if (m) {
        memcpy(mac, m->mac, 6);
        return 1;
    }
    if (card && cable && any_bound() && ip == (ip4_addr_get_u32(netif_ip4_addr(card)) | ~ip4_addr_get_u32(netif_ip4_netmask(card)))) {
        memset(mac, 0xFF, 6);                   /* (the card's network's broadcast address) */
        return 1;
    }
    return 0;
}

/* ---- from the card */

void member_card_frame(const u8 *f, u32 len)
{
    const u8 *ip = f + 14;
    u32 n = len - 14, ihl, total;
    if (len < 42 || !sharing() || !memcmp(f + 6, nic0->mac, 6))
        return;
    if (f[12] == 0x08 && f[13] == 0x06) {       /* ARP: who has a member's address? We do */
        u32 target;
        memcpy(&target, f + 38, 4);
        if (f[21] == 1 && by_ip(target)) {
            u32 asker;
            memcpy(&asker, f + 28, 4);
            if (asker != target) {              /* (not the member's own probe for its address, come round) */
                arp_say(target, f + 22, asker);
                answered++;
            }
        }
        return;
    }
    if (f[12] != 0x08 || f[13] != 0x00 || n < 20)
        return;
    total = (u32)ip[2] << 8 | ip[3];
    if ((ihl = udp_for(ip, n, 68)) != 0) {      /* DHCP to a client: is it one of the cable's? */
        const u8 *b = ip + ihl + 8;
        u32 bn = total - ihl - 8, lease;
        struct member *m;
        int type;
        if (bn < 240 || b[0] != 2 || !(m = by_mac(b + 28, 0)))
            return;
        type = dhcp_type(b, bn, &lease);
        cable_send_ip(ip, total);
        if (type == 5) {                        /* ACK: the address is its */
            ip4_addr_t was = m->ip;
            int had = m->bound;
            memcpy(&m->ip, b + 16, 4);
            m->bound = !ip4_addr_isany_val(m->ip);
            m->until = timer_ticks() + (lease && lease < 0x3FFFFF ? lease : 0x3FFFFF) * 1024u;
            m->heard = timer_ticks();
            if (m->bound && (!had || !ip4_addr_eq(&was, &m->ip))) {
                char a[16];
                ip4addr_ntoa_r(&m->ip, a, sizeof a);
                kprintf("GLOS-NET member ip=%s mac=%02x:%02x:%02x:%02x:%02x:%02x lease=%u\n", a, m->mac[0], m->mac[1],
                        m->mac[2], m->mac[3], m->mac[4], m->mac[5], lease);
                arp_say(ip4_addr_get_u32(&m->ip), 0, 0);        /* (the network's PCs learn where it is now) */
            }
        } else if (type == 6 && m->bound) {     /* NAK: no longer */
            m->bound = 0;
            kprintf("GLOS-NET member gone mac=%02x:%02x:%02x:%02x:%02x:%02x why=nak\n", m->mac[0], m->mac[1], m->mac[2],
                    m->mac[3], m->mac[4], m->mac[5]);
        }
        return;
    }
    if (!any_bound() || total > n)
        return;
    if (((ip[16] & 0xF0) == 0xE0 || (f[0] & 1)) && ip[9] == 17 && !udp_for(ip, n, 67)) {
        /* a broadcast or a multicast (UDP alone: names, announcements): a copy down the cable, within its means */
        if (total <= cable->mtu && copy_budget(total)) {
            cable_send_ip(ip, total);
            copied_in++;
        }
    }
}

/* ---- forwarding */

/* p, an IP packet with its header first, sent on through `out` towards its destination: its time-to-live one
   less (and the header's checksum with it), cut into fragments if it is too long for the cable and may be cut.
   The packet is the caller's still. */
static void forward(struct pbuf *p, struct netif *out)
{
    struct ip_hdr *h = (struct ip_hdr *)p->payload;
    ip4_addr_t to;
    if (IPH_TTL(h) <= 1)
        return;                                 /* (its last hop was here) */
    IPH_TTL_SET(h, (u8_t)(IPH_TTL(h) - 1));
    if (IPH_CHKSUM(h) >= PP_HTONS(0xFFFFu - 0x100))     /* (one's complement: the top byte one more, the carry back in) */
        IPH_CHKSUM_SET(h, (u16_t)(IPH_CHKSUM(h) + PP_HTONS(0x100) + 1));
    else
        IPH_CHKSUM_SET(h, (u16_t)(IPH_CHKSUM(h) + PP_HTONS(0x100)));
    ip4_addr_copy(to, h->dest);
    if (out->mtu && p->tot_len > out->mtu) {
        if (!(IPH_OFFSET(h) & PP_NTOHS(IP_DF))) {
            ip4_frag(p, out, &to);
        } else {                                /* (it may not be cut: its sender is told, and sends smaller. TCP to
                                                   the far PC never comes to this, by the segment size it asks for) */
            too_long++;
            icmp_dest_unreach(p, ICMP_DUR_FRAG);
        }
        return;
    }
    out->output(out, p, &to);
    forwarded++;
}

/* ---- lwIP's hook on every IPv4 packet that comes in, by whichever interface: 1 if it is taken here (and freed) */

int member_ip_hook(struct pbuf *p, struct netif *inp)
{
    static u8 pkt[1600];
    const struct ip_hdr *h = (const struct ip_hdr *)p->payload;
    u32 len, ihl, dst, src;
    if (!cable || !card || (inp != cable && inp != card) || p->len < 20 || IPH_V(h) != 4 || !sharing())
        return 0;
    dst = ip4_addr_get_u32(&h->dest), src = ip4_addr_get_u32(&h->src);
    if (inp == card) {                          /* from the network: a member's packet goes down its cable */
        if (!by_ip(dst))
            return 0;
        forward(p, cable);
        pbuf_free(p);
        return 1;
    }
    if (p->tot_len > sizeof pkt)
        return 0;
    {
        struct member *from = by_ip(src);
        if (from)
            from->heard = timer_ticks();        /* (it is there) */
    }
    if (!ip4_addr_ismulticast(&h->dest) && dst != 0xFFFFFFFFu && !ip4_addr_isbroadcast_u32(dst, card)) {
        struct netif *n;                        /* from the cable, to one address: this PC's own are lwIP's; a
                                                   member's packet for anywhere else goes out of the card */
        const u8 *b = (const u8 *)p->payload;
        u32 hl = (b[0] & 15u) * 4;
        if (!by_ip(src))
            return 0;
        if (b[9] == 1 && p->len >= hl + 8 && b[hl] == 0 && b[hl + 4] == (PROBE_ID >> 8) && b[hl + 5] == (PROBE_ID & 0xFF)) {
            pbuf_free(p);                       /* (its answer to our asking whether it is there) */
            return 1;
        }
        for (n = netif_list; n; n = n->next)
            if (ip4_addr_get_u32(netif_ip4_addr(n)) == dst)
                return 0;
        forward(p, card);
        pbuf_free(p);
        return 1;
    }
    len = pbuf_copy_partial(p, pkt, sizeof pkt, 0);
    if ((ihl = udp_for(pkt, len, 67)) != 0 && dst == 0xFFFFFFFFu) {     /* its DHCP request, to everyone */
        u8 *b = pkt + ihl + 8;
        u32 total = (u32)pkt[2] << 8 | pkt[3];
        struct member *m;
        if (total - ihl - 8 < 240 || b[0] != 1 || b[1] != 1 || b[2] != 6 || !(m = by_mac(b + 28, 1)))
            return 0;
        m->asked = timer_ticks();
        b[10] |= 0x80;                          /* answer by broadcast: the answer is then ours to see and pass on */
        pkt[ihl + 6] = pkt[ihl + 7] = 0;        /* (the UDP checksum, which the flag has spoiled: none, as IPv4 allows) */
        card_send_ip(pkt, total);
        relayed++;
        pbuf_free(p);
        return 1;
    }
    if (pkt[9] == 17 && any_bound()) {          /* its broadcast (to everyone, or to the network's own broadcast
                                                   address) or multicast: a copy to the network; ours too */
        u32 total = (u32)pkt[2] << 8 | pkt[3];
        if (total <= len) {
            card_send_ip(pkt, total);
            copied_out++;
        }
    }
    return 0;
}

/* ---- routing (lwIP's hook): this PC's own packets for a member go down its cable */

struct netif *member_route(const void *src, const void *dest)
{
    (void)src;
    return cable && netif_is_up(cable) && by_ip(ip4_addr_get_u32((const ip4_addr_t *)dest)) ? cable : 0;
}

/* ---- now and then */

void member_turn(void)
{
    u32 i, now = timer_ticks();
    for (i = 0; i < MEMBERS; i++) {
        struct member *m = &mem[i];
        if (!m->used)
            continue;
        if (m->bound && ((s32)(now - m->until) > 0 || now - m->heard > GONE)) {
            char a[16];
            ip4addr_ntoa_r(&m->ip, a, sizeof a);
            kprintf("GLOS-NET member gone ip=%s why=%s\n", a, (s32)(now - m->until) > 0 ? "lease" : "silent");
            m->used = m->bound = 0;             /* (nothing answers for its address from here: no packet for it
                                                   is taken in and lost) */
        } else if (m->bound && now - m->heard > QUIET && now - m->probed > QUIET && cable && netif_is_up(cable)) {
            u8 e[28];                           /* an echo request down the cable: is it there? */
            u32 sum = 0, k, us = card ? ip4_addr_get_u32(netif_ip4_addr(card)) : 0, it = ip4_addr_get_u32(&m->ip);
            m->probed = now;
            memset(e, 0, sizeof e);
            e[0] = 0x45, e[3] = 28, e[8] = 64, e[9] = 1;
            memcpy(e + 12, &us, 4);
            memcpy(e + 16, &it, 4);
            for (k = 0; k < 20; k += 2)
                sum += (u32)e[k] << 8 | e[k + 1];
            sum = (sum & 0xFFFF) + (sum >> 16), sum = ~((sum & 0xFFFF) + (sum >> 16));
            e[10] = (u8)(sum >> 8), e[11] = (u8)sum;
            e[20] = 8, e[24] = PROBE_ID >> 8, e[25] = PROBE_ID & 0xFF, e[26] = (u8)(now >> 18), e[27] = (u8)(now >> 10);
            for (sum = 0, k = 20; k < 28; k += 2)
                sum += (u32)e[k] << 8 | e[k + 1];
            sum = (sum & 0xFFFF) + (sum >> 16), sum = ~((sum & 0xFFFF) + (sum >> 16));
            e[22] = (u8)(sum >> 8), e[23] = (u8)sum;
            cable_send_ip(e, 28);
            probes++;
        } else if (!m->bound && now - m->asked > 120 * 1024u) {
            m->used = 0;                        /* (asked, and never acknowledged) */
        }
    }
}

u32 member_stat(char *buf, u32 max)
{
    u32 i, k = 0;
    for (i = 0; i < MEMBERS && k + 96 < max; i++)
        if (mem[i].used && mem[i].bound) {
            char a[16];
            ip4addr_ntoa_r(&mem[i].ip, a, sizeof a);
            k += (u32)ksnprintf(buf + k, max - k, "member ip=%s mac=%02x:%02x:%02x:%02x:%02x:%02x lease_s=%u\n", a,
                                mem[i].mac[0], mem[i].mac[1], mem[i].mac[2], mem[i].mac[3], mem[i].mac[4], mem[i].mac[5],
                                (u32)((s32)(mem[i].until - timer_ticks()) > 0 ? (mem[i].until - timer_ticks()) >> 10 : 0));
        }
    if (card && cable && k + 96 < max)
        k += (u32)ksnprintf(buf + k, max - k, "share=%s relayed=%u arp_answers=%u cable_arp_answers=%u forwarded=%u too_long=%u "
                            "copied_out=%u copied_in=%u copy_dropped=%u probes=%u\n", sharing() ? "on" : "off", relayed,
                            answered, cable_answers, forwarded, too_long, copied_out, copied_in, copy_dropped, probes);
    return k;
}
