/* The net thread (urgent class, supervisor.md §7) and lwIP's glue (M3
 * item 3). The card's IRQ masks its line and wakes the thread, which
 * services the card (frames go straight into lwIP), unmasks, and runs
 * lwIP's timers; it sleeps until the next IRQ or timer. lwIP runs with
 * NO_SYS=1 in this thread alone, so nothing else may call it. DHCP brings
 * the interface up (GLOS-NET dhcp). Under /SELFTEST a TCP echo service
 * listens on port 7 (tests/loopa/jobs.py net). */
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/tcp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include <string.h>

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

static err_t link_out(struct netif *n, struct pbuf *p)
{
    u16 len = pbuf_copy_partial(p, txbuf, sizeof txbuf, 0);
    (void)n;
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
    n->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
    netif_set_hostname(n, "glos");
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

void net_start(int st)
{
    selftest = st;
    thread_create("net", PRIO_URGENT, net_thread, NULL);
}
