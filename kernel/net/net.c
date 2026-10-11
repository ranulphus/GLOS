/* The net thread (urgent class, supervisor.md §7) and lwIP's glue (M3
 * item 3). The card's IRQ masks its line and wakes the thread, which
 * services the card (frames go straight into lwIP), unmasks, and runs
 * lwIP's timers; it sleeps until the next IRQ or timer. lwIP runs with
 * NO_SYS=1 in this thread alone, so nothing else may call it. DHCP brings
 * the interface up (GLOS-NET dhcp). The PC answers to NAME.local (mDNS:
 * lwIP's responder; the name is GLOS.CFG's [net] hostname, or "glos") and
 * offers its SSH server as a service (_ssh._tcp). Under /SELFTEST a TCP echo
 * service listens on port 7, and the responder is asked for its own name
 * with a query made up here (tests/loopa/jobs.py net: SLiRP carries no
 * multicast, so nothing outside can ask). */
#include "lwip/apps/mdns.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include <string.h>

#include "glos/bootinfo.h"
#include "io.h"
#include "kprintf.h"
#include "nic.h"
#include "sched.h"
#include "sshd.h"
#include "timer.h"
#include "vm.h"

static struct waitq netq;
static volatile u8 pending;
static int selftest;
static struct netif nif;
static u8 txbuf[ETH_MAX];
static const char *hostname = "glos";
static u32 bound_at;                            /* the tick DHCP gave an address at */
static u8 mdns_asked, mdns_answered;            /* /SELFTEST's question to the responder */
static u32 rng;

/* Another thread has work for this one (the ssh thread's output); from any
   context. */
static void net_kick(void)
{
    u32 f;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory");
    pending = 1;
    thread_wake(&netq);
    if (f & 0x200)
        sti();
}

void net_irq(struct trapframe *tf)
{
    (void)tf;
    vm_kmask(nic0->irq, 1);
    pending = 1;
    thread_wake(&netq);
}

/* ---- lwIP's port */

u32_t sys_now(void) { return (u32_t)(((u64)timer_ticks() * 1000) >> 10); }

void glos_lwip_diag(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kprintf("GLOS-LWIP ");
    kvprintf(fmt, ap);
    __builtin_va_end(ap);
}

void glos_lwip_assert(const char *msg, const char *file, int line)
{
    kprintf("GLOS-LWIP assert=\"%s\" at=%s:%d\n", msg, file, line);
    panic("lwip-assert", NULL);
}

/* DHCP transaction ids and TCP initial sequence numbers. Not for keys: the
   entropy pool (M3 item 4) is. */
unsigned int glos_net_rand(void)
{
    if (!rng)
        rng = 0x9E3779B9u ^ ((u32)nic0->mac[3] << 16 | (u32)nic0->mac[4] << 8 | nic0->mac[5]);
    rng ^= timer_ticks();
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

/* ---- the interface */

/* /SELFTEST: is this frame the responder's answer, our address for our name?
   (UDP from port 5353 with an A record whose data is the interface's address.) */
static void mdns_watch(const u8 *f, u32 len)
{
    u32 ip = ip4_addr_get_u32(netif_ip4_addr(&nif)), ihl, i;
    if (len < 14 + 20 + 8 + 12 || f[12] != 8 || f[13] != 0 || f[23] != 17)
        return;
    ihl = (f[14] & 15u) * 4;
    if (len < 14 + ihl + 8 + 12 || f[14 + ihl] != 0x14 || f[14 + ihl + 1] != 0xE9 || !(f[14 + ihl + 8 + 2] & 0x80))
        return;                                 /* (not from port 5353, or not a response) */
    for (i = 14 + ihl + 8 + 12; i + 14 <= len; i++)     /* type A, class IN (cache flush or not), TTL, 4 bytes */
        if (f[i] == 0 && f[i + 1] == 1 && (f[i + 2] & 0x7F) == 0 && f[i + 3] == 1 && f[i + 8] == 0 && f[i + 9] == 4
            && !memcmp(f + i + 10, &ip, 4)) {
            mdns_answered = 1;
            kprintf("GLOS-NET mdns answer=ok name=%s.local\n", hostname);
            return;
        }
}

static err_t link_out(struct netif *n, struct pbuf *p)
{
    u16 len = pbuf_copy_partial(p, txbuf, sizeof txbuf, 0);
    (void)n;
    if (mdns_asked && !mdns_answered)
        mdns_watch(txbuf, len);
    return nic0->send(nic0, txbuf, len) == 0 ? ERR_OK : ERR_IF;
}

static err_t nif_init(struct netif *n)
{
    n->name[0] = 'e';
    n->name[1] = 'n';
    n->output = etharp_output;
    n->linkoutput = link_out;
    n->mtu = 1500;
    n->hwaddr_len = 6;
    memcpy(n->hwaddr, nic0->mac, 6);
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP | NETIF_FLAG_IGMP;
    netif_set_hostname(n, hostname);
    return ERR_OK;
}

void net_rx(struct nic *c, const u8 *f, u32 len)
{
    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16)len, PBUF_POOL);
    if (!p) {
        c->rx_errors++;
        return;
    }
    pbuf_take(p, f, (u16)len);
    if (nif.input(p, &nif) != ERR_OK)
        pbuf_free(p);
}

static void status_cb(struct netif *n)
{
    char ip[16], gw[16], mask[16];
    if (ip4_addr_isany_val(*netif_ip4_addr(n)))
        return;
    ip4addr_ntoa_r(netif_ip4_addr(n), ip, sizeof ip);
    ip4addr_ntoa_r(netif_ip4_gw(n), gw, sizeof gw);
    ip4addr_ntoa_r(netif_ip4_netmask(n), mask, sizeof mask);
    kprintf("GLOS-NET dhcp ip=%s gw=%s mask=%s ticks=%u\n", ip, gw, mask, timer_ticks());
    bound_at = timer_ticks() | 1;
}

/* /SELFTEST: a question for NAME.local's address, as if from the gateway to
   mDNS's group, handed to the stack as a received frame. The answer is seen
   on its way out (mdns_watch). */
static void mdns_ask(void)
{
    static u8 f[14 + 20 + 8 + 12 + 40];
    static const u8 head[14 + 20] = { 0x01, 0x00, 0x5E, 0x00, 0x00, 0xFB, 0x52, 0x55, 0x0A, 0x00, 0x02, 0x02, 0x08, 0x00,
                                      0x45, 0x00, 0, 0, 0x12, 0x34, 0, 0, 0xFF, 17, 0, 0, 10, 0, 2, 2, 224, 0, 0, 251 };
    u32 n = 14 + 20 + 8 + 12, l = (u32)strlen(hostname), sum = 0, i, ip, udp;
    memcpy(f, head, sizeof head);
    f[n - 12 + 5] = 1;                          /* one question */
    f[n++] = (u8)l;
    memcpy(f + n, hostname, l);
    n += l;
    memcpy(f + n, "\005local\0\0\001\0\001", 11);     /* .local, type A, class IN */
    n += 11;
    ip = n - 14;
    udp = ip - 20;
    f[16] = (u8)(ip >> 8), f[17] = (u8)ip;
    f[34] = 0x14, f[35] = 0xE9, f[36] = 0x14, f[37] = 0xE9;     /* ports 5353; no UDP checksum */
    f[38] = (u8)(udp >> 8), f[39] = (u8)udp;
    for (i = 14; i < 34; i += 2)
        sum += ((u32)f[i] << 8) | f[i + 1];
    sum = (sum & 0xFFFF) + (sum >> 16);
    sum = ~((sum & 0xFFFF) + (sum >> 16));
    f[24] = (u8)(sum >> 8), f[25] = (u8)sum;
    mdns_asked = 1;
    net_rx(nic0, f, n);
}

/* ---- /SELFTEST: TCP echo on port 7; the connection closes after a line */

static err_t echo_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct pbuf *q;
    int line = 0;
    (void)arg;
    (void)err;
    if (!p) {
        tcp_close(pcb);
        return ERR_OK;
    }
    tcp_recved(pcb, p->tot_len);
    for (q = p; q; q = q->next) {
        tcp_write(pcb, q->payload, q->len, TCP_WRITE_FLAG_COPY);
        if (memchr(q->payload, '\n', q->len))
            line = 1;
    }
    kprintf("GLOS-NET echo bytes=%u\n", p->tot_len);
    pbuf_free(p);
    if (line)
        tcp_close(pcb);                         /* after what was written */
    else
        tcp_output(pcb);
    return ERR_OK;
}

static err_t echo_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    (void)err;
    tcp_recv(pcb, echo_recv);
    return ERR_OK;
}

static void echo_start(void)
{
    struct tcp_pcb *pcb = tcp_new();
    if (!pcb || tcp_bind(pcb, IP_ADDR_ANY, 7) != ERR_OK || !(pcb = tcp_listen(pcb)))
        kprintf("GLOS-NET error=echo-listen\n");
    else
        tcp_accept(pcb, echo_accept);
}

/* ---- the thread */

static void net_thread(void *arg)
{
    u32 ms, deadline;
    (void)arg;
    lwip_init();
    netif_add(&nif, NULL, NULL, NULL, NULL, nif_init, ethernet_input);
    netif_set_default(&nif);
    netif_set_status_callback(&nif, status_cb);
    netif_set_up(&nif);
    dhcp_start(&nif);
    mdns_resp_init();
    if (mdns_resp_add_netif(&nif, hostname) != ERR_OK
        || mdns_resp_add_service(&nif, hostname, "_ssh", DNSSD_PROTO_TCP, 22, NULL, NULL) < 0)
        kprintf("GLOS-NET error=mdns\n");
    else
        kprintf("GLOS-NET mdns name=%s.local\n", hostname);
    if (selftest)
        echo_start();
    sshd_net_start(net_kick);
    for (;;) {
        ms = sys_timeouts_sleeptime();
        if (ms > 60000)
            ms = 60000;
        deadline = timer_ticks() + (ms * 1024 + 999) / 1000 + 1;
        cli();
        if (!pending)
            thread_wait(&netq, deadline);
        pending = 0;
        sti();
        nic0->service(nic0);
        cli();
        vm_kmask(nic0->irq, 0);
        sti();
        sys_check_timeouts();
        sshd_net_poll();
        if (selftest && bound_at && !mdns_asked && timer_ticks() - bound_at > 3 * 1024)
            mdns_ask();                         /* (the responder has probed for its name by then) */
    }
}

void net_report(void)
{
    char ip[16];
    if (!nic0)
        return;
    ip4addr_ntoa_r(netif_ip4_addr(&nif), ip, sizeof ip);
    kprintf("GLOS-NET card=%s ip=%s rx=%u tx=%u rx_errors=%u overruns=%u\n", nic0->name, ip, nic0->rx_frames,
            nic0->tx_frames, nic0->rx_errors, nic0->overruns);
}

/* glos net (lines) and glos stat (a JSON object, or null). */
u32 net_stat(char *buf, u32 max, int json)
{
    char ip[16], gw[16], mask[16];
    if (!nic0)
        return (u32)ksnprintf(buf, max, json ? "null" : "no network card\n");
    ip4addr_ntoa_r(netif_ip4_addr(&nif), ip, sizeof ip);
    ip4addr_ntoa_r(netif_ip4_gw(&nif), gw, sizeof gw);
    ip4addr_ntoa_r(netif_ip4_netmask(&nif), mask, sizeof mask);
    if (json)
        return (u32)ksnprintf(buf, max, "{\"card\":\"%s\",\"ip\":\"%s\",\"rx\":%u,\"tx\":%u,\"rx_errors\":%u,\"overruns\":%u}",
                              nic0->name, ip, nic0->rx_frames, nic0->tx_frames, nic0->rx_errors, nic0->overruns);
    return (u32)ksnprintf(buf, max, "card=%s io=%04x irq=%u mac=%02x:%02x:%02x:%02x:%02x:%02x\n"
                          "ip=%s mask=%s gw=%s\nrx=%u tx=%u rx_errors=%u overruns=%u\n", nic0->name, nic0->base,
                          nic0->irq, nic0->mac[0], nic0->mac[1], nic0->mac[2], nic0->mac[3], nic0->mac[4], nic0->mac[5],
                          ip, mask, gw, nic0->rx_frames, nic0->tx_frames, nic0->rx_errors, nic0->overruns);
}

void net_start(int st)
{
    selftest = st;
    if (vm.bi->hostname[0])
        hostname = vm.bi->hostname;
    thread_create("net", PRIO_URGENT, net_thread, NULL);
}
