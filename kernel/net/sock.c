/* Apps' TCP connections (M10 step B; the API's NET, phase B step 11: kernel/app/api2net.c) and the ports they
 * listen on.
 *
 * A connection is a slot here, a pcb of lwIP's (the net thread's alone,
 * NO_SYS) and two rings of 16 KB between the app's thread and the net
 * thread, each written by one side and read by the other. lwIP's callbacks
 * put what arrives into the receive ring, and lwIP is told it has been taken
 * (tcp_recved) only once the app has read it, so the window is the room the
 * app has left: the ring is never offered more than it holds (TCP_WND, 8
 * segments, is less than a ring). What the app sends waits in the send ring
 * until the net thread's turn (sock_net_poll) writes it into TCP. An app's
 * thread waits on its own queue (a->wq), which the net thread wakes when its
 * connection moves on and app_kill() wakes for a kill.
 *
 * Closing hands the slot to the net thread: it sends what is left, then
 * closes (or resets at once), and frees the slot. An app's end closes its
 * connections the same way.
 *
 * A port listened on is a slot with lwIP's listening pcb and no rings. A connection that comes in gets a slot
 * and rings in the net thread (on_accept), as its app's but `pending` until the app's ACCEPT takes it; what
 * the other end sends meanwhile waits in its ring. At most `backlog` wait: more are refused by lwIP. Closing
 * the port resets those nobody took.
 *
 * A datagram port (API 2.2: UDP) is a slot with lwIP's UDP pcb and the same two rings, which hold whole
 * datagrams, each behind eight bytes that say its length and the other end's port and address. One that
 * comes in when 64 wait, or when the ring has no room for it, is dropped, as UDP allows; what the app sends
 * waits in the send ring for the net thread's turn, a datagram a pbuf.
 *
 * The calls here take their arguments as they are (sock_connect, sock_send, ...): the API's NET
 * (kernel/app/api2net.c) comes through them. */
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"

#include <string.h>

#include "kapi.h"
#include "../app/appint.h"
#include "member.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "nic.h"
#include "sched.h"
#include "sock.h"
#include "timer.h"
#include "vm.h"

#define NSOCK 32
#define PER_APP 16                              /* an API 2.0 app's (a server's connections count too) ... */
#define NETS_LISTENING 5                        /* (API 2.0's GLOS_NETS_LISTENING: a port listened on) */
#define RING 16384u                             /* (a power of two, and more than TCP_WND) */
#define DG_HDR 8u                               /* a datagram in a ring: its length, the port, the address */
#define DG_MAX GLOS_NET_DATAGRAM_MAX
#define DG_QUEUE GLOS_NET_DATAGRAM_QUEUE

static struct sock {
    u8 used;                                    /* the slot is taken, until the net thread has done with it */
    u8 gen;                                     /* the id's generation (a slot's old id is refused) */
    struct app *app;                            /* whose; 0 once it has let go (closed, or ended) */
    u32 flags;                                  /* GLOS_NETF_ */
    volatile u8 state;                          /* GLOS_NETS_ */
    volatile u8 why;                            /* FAILED: GLOS_E_NET_ */
    volatile u8 closing, reset;                 /* let go: closed once the output has gone, or reset now */
    volatile u8 tx_ev;                          /* an event owed when there is room again (a short send) */
    struct tcp_pcb *pcb;                        /* the net thread's; 0 once lwIP has freed it */
    ip_addr_t ip;
    u16 port;
    u8 *rx, *tx;
    volatile u32 rx_in, rx_out, tx_in, tx_out;  /* counts, never wrapped by hand: `in` the writer's, `out` the reader's */
    volatile u32 unacked;                       /* read by the app, lwIP not yet told */
    /* listening (API 2.0) */
    u8 hard;                                    /* its CLOSE resets it (2.0's RESET property) */
    u8 listen;                                  /* a port listened on: no rings, `lpcb` */
    u8 backlog;
    volatile u8 pending;                        /* a connection that came in, not yet taken by ACCEPT */
    struct tcp_pcb *lpcb;
    struct sock *parent;                        /* pending: the port it came in on */
    u32 seq;                                    /* ... and its turn */
    /* a datagram port (API 2.2) */
    u8 dgram;
    volatile u8 bcast;                          /* SENDTO may send to a broadcast address */
    struct udp_pcb *upcb;
    volatile u32 rx_n;                          /* datagrams in the receive ring */
    u32 tx_need;                                /* the room a SENDTO waits for */
    u32 dropped;                                /* those that came in and found no room */
} socks[NSOCK];
static u32 accept_seq;

static u32 id_of(const struct sock *s) { return (u32)s->gen << 8 | (u32)(s - socks + 1); }

static void barrier(void) { __asm__ volatile("" ::: "memory"); }

/* The app's connection with that id, or 0. */
static struct sock *find(struct app *a, u32 id)
{
    u32 i = (id & 0xFF) - 1;
    if (i >= NSOCK || !socks[i].used || socks[i].app != a || socks[i].gen != (u8)(id >> 8))
        return 0;
    return &socks[i];
}

static u32 waiting_on(const struct sock *l)    /* (interrupts off) connections waiting for ACCEPT on a port */
{
    u32 i, n = 0;
    for (i = 0; i < NSOCK; i++)
        n += socks[i].used && socks[i].pending && socks[i].parent == l;
    return n;
}

/* (interrupts off) What waits to be read, and the room to send: bytes; a port: connections, and none; a datagram
   port: datagrams, and the longest SENDTO would take now. */
static u32 waiting_of(const struct sock *s)
{
    return s->listen ? waiting_on(s) : s->dgram ? s->rx_n : s->rx_in - s->rx_out;
}

static u32 room_of(const struct sock *s)
{
    u32 room = RING - (s->tx_in - s->tx_out);
    if (s->listen)
        return 0;
    if (!s->dgram)
        return room;
    return room < DG_HDR ? 0 : room - DG_HDR > DG_MAX ? DG_MAX : room - DG_HDR;
}

/* Its app told: woken, and with ev an event if it asked for them. From the net thread. */
static void notify(struct sock *s, int ev)
{
    u32 f = irq_save();
    struct app *a = s->app;
    if (a) {
        if (ev && (s->flags & GLOS_NETF_EVENTS) && a->v2 && !a->session)
            api2_net_event(a, id_of(s), s->state, waiting_of(s), room_of(s));
        if (a->session)
            vm_kick();                          /* (a full-screen program's: it waits on the VM's thread) */
        else
            thread_wake(&a->wq);
    }
    irq_restore(f);
}

/* Bytes into a ring at a count, and out of one: round its end. */
static void ring_put(u8 *ring, u32 at, const void *src, u32 n)
{
    u32 first;
    at %= RING;
    first = RING - at < n ? RING - at : n;
    memcpy(ring + at, src, first);
    memcpy(ring, (const u8 *)src + first, n - first);
}

static void ring_get(const u8 *ring, u32 at, void *dst, u32 n)
{
    u32 first;
    at %= RING;
    first = RING - at < n ? RING - at : n;
    memcpy(dst, ring + at, first);
    memcpy((u8 *)dst + first, ring, n - first);
}

struct dg_hdr { u16 len, port; u32 addr; };     /* (DG_HDR bytes) */

/* ---- the net thread's side */

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    struct sock *s = arg;
    u32 n, at, first;
    int was_empty;
    (void)pcb;
    (void)err;
    if (!p) {                                   /* the other end has closed its side */
        if (s->state == GLOS_NETS_OPEN)
            s->state = GLOS_NETS_CLOSED;
        notify(s, 1);
        return ERR_OK;
    }
    n = p->tot_len;
    if (RING - (s->rx_in - s->rx_out) < n)
        return ERR_MEM;                         /* (not when the window is the room: lwIP offers it again later) */
    at = s->rx_in % RING;
    first = RING - at < n ? RING - at : n;
    pbuf_copy_partial(p, s->rx + at, (u16_t)first, 0);
    if (first < n)
        pbuf_copy_partial(p, s->rx, (u16_t)(n - first), (u16_t)first);
    pbuf_free(p);
    was_empty = s->rx_in == s->rx_out;
    barrier();
    s->rx_in += n;
    notify(s, was_empty);                       /* (an event for the first bytes only, until the app has read) */
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    struct sock *s = arg;
    (void)pcb;
    (void)len;
    if (s->tx_ev && RING - (s->tx_in - s->tx_out) >= RING / 4) {
        s->tx_ev = 0;
        notify(s, 1);
    } else {
        notify(s, 0);                           /* (a send waiting for room) */
    }
    return ERR_OK;
}

static void on_err(void *arg, err_t err)
{
    struct sock *s = arg;
    s->pcb = 0;                                 /* lwIP has freed it */
    if (s->state == GLOS_NETS_CONNECTING)
        s->why = err == ERR_RST ? GLOS_E_NET_REFUSED : GLOS_E_NET_TIMEOUT;
    else
        s->why = GLOS_E_NET_RESET;
    s->state = GLOS_NETS_FAILED;
    notify(s, 1);
}

static err_t on_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    struct sock *s = arg;
    (void)pcb;
    (void)err;
    s->state = GLOS_NETS_OPEN;
    notify(s, 1);
    return ERR_OK;
}

static void hook(struct sock *s, struct tcp_pcb *pcb);

static void do_connect(void *arg)
{
    struct sock *s = arg;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        s->why = GLOS_E_MEMORY;
        s->state = GLOS_NETS_FAILED;
        return;
    }
    hook(s, pcb);
    if (!(s->flags & GLOS_NETF_NAGLE))
        tcp_nagle_disable(pcb);
    s->pcb = pcb;
    if (tcp_connect(pcb, &s->ip, s->port, on_connected) != ERR_OK) {
        tcp_arg(pcb, NULL);
        tcp_abort(pcb);
        s->pcb = 0;
        s->why = GLOS_E_NET_REFUSED;
        s->state = GLOS_NETS_FAILED;
    }
}

static void hook(struct sock *s, struct tcp_pcb *pcb)
{
    tcp_arg(pcb, s);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
}

/* A connection has come in on a port listened on: a slot and rings for it, its app's once ACCEPT takes it.
   Refused (lwIP ends it) when as many wait as the port's backlog, or there is no slot or memory. */
static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    struct sock *l = arg, *s = 0;
    u32 i, f;
    u8 *rx, *tx;
    if (err != ERR_OK || !pcb || !l)
        return ERR_VAL;
    f = irq_save();
    for (i = 0; i < NSOCK && !s; i++)
        if (!socks[i].used)
            s = &socks[i];
    if (s && !l->closing && l->app && waiting_on(l) < l->backlog)
        s->used = 1;                            /* (the poll leaves it be until it has its rings) */
    else
        s = 0;
    irq_restore(f);
    if (!s)
        return ERR_MEM;
    rx = kmalloc(RING);
    tx = kmalloc(RING);
    if (!rx || !tx) {
        kfree(rx);
        kfree(tx);
        s->used = 0;
        return ERR_MEM;
    }
    s->gen++;
    s->flags = 0;
    s->state = GLOS_NETS_OPEN;
    s->why = 0;
    s->closing = s->reset = s->tx_ev = s->listen = s->hard = s->dgram = 0;
    s->lpcb = 0;
    s->rx_in = s->rx_out = s->tx_in = s->tx_out = s->unacked = 0;
    ip_addr_copy(s->ip, pcb->remote_ip);
    s->port = pcb->remote_port;
    s->pcb = pcb;
    s->parent = l;
    s->seq = ++accept_seq;
    s->pending = 1;
    s->app = l->app;
    s->tx = tx;
    barrier();
    s->rx = rx;
    hook(s, pcb);
    tcp_nagle_disable(pcb);
    notify(l, 1);
    return ERR_OK;
}

static void do_listen(void *arg)
{
    struct sock *s = arg;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4), *l;
    err_t err = ERR_OK;
    if (!pcb) {
        s->why = GLOS_E_MEMORY;
        s->state = GLOS_NETS_FAILED;
        return;
    }
    /* (The address may be used again: connections that came in on the port before, still winding down, don't
       keep an app from listening on it anew. A port something listens on is refused all the same: GLOS's own
       at the bind, another app's at the listen.) */
    ip_set_option(pcb, SOF_REUSEADDR);
    if (tcp_bind(pcb, IP_ANY_TYPE, s->port) != ERR_OK) {
        tcp_close(pcb);
        s->why = GLOS_E_NET_REFUSED;
        s->state = GLOS_NETS_FAILED;
        return;
    }
    if (!(l = tcp_listen_with_backlog_and_err(pcb, s->backlog, &err))) {
        tcp_close(pcb);
        s->why = err == ERR_USE ? GLOS_E_NET_REFUSED : GLOS_E_MEMORY;
        s->state = GLOS_NETS_FAILED;
        return;
    }
    s->port = l->local_port;                    /* (port 0: the one lwIP chose) */
    tcp_arg(l, s);
    tcp_accept(l, on_accept);
    s->lpcb = l;
    s->state = NETS_LISTENING;
}

/* ---- datagram ports (the net thread's side) */

/* A datagram has come in for a port: into its ring behind its header, or dropped when 64 wait or there is no
   room. */
static void on_udp(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    struct sock *s = arg;
    struct dg_hdr h;
    u32 n = p->tot_len, at, first, f;
    int was_empty;
    (void)pcb;
    if (!s || !s->rx || s->closing || n > DG_MAX || s->rx_n >= DG_QUEUE
        || RING - (s->rx_in - s->rx_out) < DG_HDR + n) {
        if (s)
            s->dropped++;
        pbuf_free(p);
        return;
    }
    h.len = (u16)n, h.port = port, h.addr = ip4_addr_get_u32(ip_2_ip4(addr));
    ring_put(s->rx, s->rx_in, &h, DG_HDR);
    at = (s->rx_in + DG_HDR) % RING;
    first = RING - at < n ? RING - at : n;
    pbuf_copy_partial(p, s->rx + at, (u16_t)first, 0);
    if (first < n)
        pbuf_copy_partial(p, s->rx, (u16_t)(n - first), (u16_t)first);
    pbuf_free(p);
    f = irq_save();
    was_empty = s->rx_n == 0;
    s->rx_in += DG_HDR + n;
    s->rx_n++;
    irq_restore(f);
    notify(s, was_empty);                       /* (an event for the first only, until the app has read) */
}

static void do_dgram(void *arg)
{
    struct sock *s = arg;
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        s->why = GLOS_E_MEMORY;
        s->state = GLOS_NETS_FAILED;
        return;
    }
    if (s->flags & GLOS_NETF_REUSE)
        ip_set_option(pcb, SOF_REUSEADDR);
    if (udp_bind(pcb, IP_ANY_TYPE, s->port) != ERR_OK) {
        udp_remove(pcb);
        s->why = GLOS_E_NET_REFUSED;            /* (the port is taken) */
        s->state = GLOS_NETS_FAILED;
        return;
    }
    s->port = pcb->local_port;                  /* (port 0: the one lwIP chose) */
    udp_recv(pcb, on_udp, s);
    s->upcb = pcb;
    s->state = GLOS_NETS_DATAGRAM;
}

/* A datagram port's turn: what the app queued goes out, a pbuf a datagram (one there is no memory for is
   dropped: UDP may); closed once that is done. */
/* The datagram at the front of a port's send ring as a pbuf, or 0 (no memory). */
static struct pbuf *dg_pbuf(const struct sock *s, u32 len)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);
    u32 at = (s->tx_out + DG_HDR) % RING, first = RING - at < len ? RING - at : len;
    if (!p)
        return 0;
    if (first)
        pbuf_take(p, s->tx + at, (u16_t)first);
    if (first < len)
        pbuf_take_at(p, s->tx, (u16_t)(len - first), (u16_t)first);
    return p;
}

static void dgram_poll(struct sock *s)
{
    int sent = 0;
    while (s->tx_in != s->tx_out && s->upcb && !s->reset) {
        struct dg_hdr h;
        struct pbuf *p;
        struct netif *too;
        ip_addr_t to, from;
        u32 src = 0;
        ring_get(s->tx, s->tx_out, &h, DG_HDR);
        ip_addr_set_ip4_u32(&to, h.addr);
        if ((p = dg_pbuf(s, h.len)) != 0) {
            udp_sendto(s->upcb, p, &to, h.port);        /* (no route, no room in the card: lost, as UDP allows) */
            pbuf_free(p);
        }
        if ((too = member_own_broadcast(h.addr, h.len, &src)) != 0 && (p = dg_pbuf(s, h.len)) != 0) {
            ip_addr_set_ip4_u32(&from, src);            /* (a broadcast: a copy down the cable, for the PC there
                                                           that is a member of the card's network: member.c) */
            udp_sendto_if_src(s->upcb, p, &to, h.port, too, &from);
            pbuf_free(p);
        }
        barrier();
        s->tx_out += DG_HDR + h.len;
        sent = 1;
    }
    if (sent) {
        int ev = s->tx_ev && room_of(s) >= s->tx_need;
        if (ev)
            s->tx_ev = 0;
        notify(s, ev);                          /* (a SENDTO waiting for room; an event after one that found none) */
    }
    if (s->closing) {
        if (s->upcb) {
            udp_recv(s->upcb, NULL, NULL);
            udp_remove(s->upcb);
            s->upcb = 0;
        }
        kfree(s->rx);
        kfree(s->tx);
        s->rx = s->tx = 0;
        s->dgram = 0;
        barrier();
        s->used = 0;
    }
}

static void do_nagle(void *arg)
{
    struct sock *s = arg;
    if (s->pcb)
        tcp_nagle_enable(s->pcb);
}

static void unhook(struct tcp_pcb *pcb)
{
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
}

void sock_net_poll(void)
{
    int i;
    for (i = 0; i < NSOCK; i++) {
        struct sock *s = &socks[i];
        u32 f, k;
        if (s->used && s->listen) {             /* a port: closed when its app lets go, those nobody took reset */
            int j, any = 0;
            if (!s->closing)
                continue;
            if (s->lpcb) {
                tcp_arg(s->lpcb, NULL);
                tcp_accept(s->lpcb, NULL);
                tcp_close(s->lpcb);
                s->lpcb = 0;
            }
            f = irq_save();
            for (j = 0; j < NSOCK; j++)
                if (socks[j].used && socks[j].pending && socks[j].parent == s) {
                    socks[j].app = 0;
                    socks[j].parent = 0;
                    socks[j].pending = 0;
                    socks[j].reset = socks[j].closing = 1;
                    any = 1;
                }
            s->listen = 0;
            barrier();
            s->used = 0;
            irq_restore(f);
            if (any)
                net_kick();                     /* (another turn: those before this slot) */
            continue;
        }
        if (!s->used || !s->rx)
            continue;
        if (s->dgram) {
            dgram_poll(s);
            continue;
        }
        f = irq_save();
        k = s->unacked;
        s->unacked = 0;
        irq_restore(f);
        if (k && s->pcb)
            tcp_recved(s->pcb, (u16_t)k);       /* (at most a ring: less than 64 KB) */
        if (s->pcb && !s->reset && (s->state == GLOS_NETS_OPEN || s->state == GLOS_NETS_CLOSED)) {
            int wrote = 0;
            while (s->tx_in != s->tx_out && tcp_sndbuf(s->pcb) > 0) {
                u32 n = s->tx_in - s->tx_out, at = s->tx_out % RING, room = tcp_sndbuf(s->pcb);
                if (n > RING - at)
                    n = RING - at;
                if (n > room)
                    n = room;
                if (tcp_write(s->pcb, s->tx + at, (u16_t)n, TCP_WRITE_FLAG_COPY) != ERR_OK)
                    break;
                barrier();
                s->tx_out += n;
                wrote = 1;
            }
            if (wrote)
                tcp_output(s->pcb);
        }
        if (s->closing && (s->reset || !s->pcb || s->tx_in == s->tx_out
                           || (s->state != GLOS_NETS_OPEN && s->state != GLOS_NETS_CLOSED))) {
            if (s->pcb) {
                unhook(s->pcb);
                if (s->reset || tcp_close(s->pcb) != ERR_OK)
                    tcp_abort(s->pcb);
                s->pcb = 0;
            }
            kfree(s->rx);
            kfree(s->tx);
            s->rx = s->tx = 0;
            barrier();
            s->used = 0;
        }
    }
}

/* ---- the app's side */

/* Milliseconds as ticks (api.c's). */
static u32 ticks_of(u32 ms)
{
    if (ms > 0x03FFFFFFu)                       /* (18 hours: the sum below in 32 bits) */
        ms = 0x03FFFFFFu;
    return ms + (ms * 24 + 999) / 1000;
}

/* Where a call waits, and whether its program is being ended: an app's thread on the app's queue; a full-screen
   program's calls (api2sess.c) on the VM's thread, on the VM's queue, which the net thread's news kicks. */
static struct waitq *wq_of(struct app *a) { return a->session ? &vm.waitq : &a->wq; }
static int killed(const struct app *a) { return a->session ? vm.kill_req != 0 : a->kill != 0; }

/* Waits until ok(s), the time is up, or the app is killed: 1 if ok(s). */
static int wait_for(struct app *a, struct sock *s, int (*ok)(const struct sock *), u32 ms)
{
    u32 until = ms == 0xFFFFFFFFu ? 0 : timer_ticks() + ticks_of(ms);
    for (;;) {
        u32 f = irq_save();
        if (ok(s)) {
            irq_restore(f);
            return 1;
        }
        if (killed(a) || !ms || (until && (s32)(timer_ticks() - until) >= 0)) {
            irq_restore(f);
            return 0;
        }
        thread_wait(wq_of(a), until ? until : timer_ticks() + 1024);    /* (for ever: a second at a time) */
        irq_restore(f);
    }
}

static int connected(const struct sock *s) { return s->state != GLOS_NETS_CONNECTING; }
static int readable(const struct sock *s)
{
    return s->rx_in != s->rx_out || (s->state != GLOS_NETS_OPEN && s->state != GLOS_NETS_CONNECTING);
}
static int writable(const struct sock *s) { return s->tx_in - s->tx_out < RING || s->state == GLOS_NETS_FAILED; }

/* The slot handed to the net thread to finish. */
static void let_go(struct sock *s, int reset)
{
    u32 f = irq_save();
    s->app = 0;
    s->reset = (u8)reset;
    barrier();
    s->closing = 1;
    irq_restore(f);
    net_kick();
}

int sock_resolve(struct app *a, const char *name, u32 ms, u32 *addr)
{
    u8 ip[4];
    u32 until;
    int k, r;
    if (!net_up())
        return GLOS_E_NET_DOWN;
    if ((k = net_lookup_start(name)) < 0)
        return GLOS_E_MEMORY;                   /* (the lookups are all taken) */
    until = ms == 0xFFFFFFFFu ? 0 : timer_ticks() + ticks_of(ms);
    while ((r = net_lookup_poll(k, ip)) == 0 && !killed(a) && (!until || (s32)(timer_ticks() - until) < 0)) {
        u32 f = irq_save();
        thread_wait(wq_of(a), timer_ticks() + 32);      /* (the resolver wakes its own queue: looked at often) */
        irq_restore(f);
    }
    if (r == 0) {
        net_lookup_drop(k);
        return GLOS_E_NET_TIMEOUT;
    }
    if (r < 0)
        return GLOS_E_NET_NAME;
    *addr = (u32)ip[0] | (u32)ip[1] << 8 | (u32)ip[2] << 16 | (u32)ip[3] << 24;
    return 0;
}

/* A free slot taken for a (the net thread's poll leaves it be until it has its rings, or is a port), or 0:
   16 an app, 32 in all. */
static struct sock *slot(struct app *a)
{
    struct sock *s = 0;
    u32 i, mine = 0, f = irq_save();
    for (i = 0; i < NSOCK; i++) {
        if (socks[i].used && socks[i].app == a)
            mine++;
        else if (!socks[i].used && !s)
            s = &socks[i];
    }
    if (s && mine < PER_APP)
        s->used = 1;
    else
        s = 0;
    irq_restore(f);
    return s;
}

int sock_connect(struct app *a, u32 addr, u32 port, u32 ms, u32 flags, u32 *id)
{
    struct sock *s;
    u8 *rx, *tx;
    if (!net_up())
        return GLOS_E_NET_DOWN;
    if (!port || port > 65535)
        return GLOS_E_ARGUMENT;
    if (!(s = slot(a)))
        return GLOS_E_MEMORY;
    rx = kmalloc(RING);
    tx = kmalloc(RING);
    if (!rx || !tx) {
        kfree(rx);
        kfree(tx);
        s->used = 0;
        return GLOS_E_MEMORY;
    }
    s->gen++;
    s->flags = flags & (GLOS_NETF_EVENTS | GLOS_NETF_NAGLE);
    s->state = GLOS_NETS_CONNECTING;
    s->why = 0;
    s->closing = s->reset = s->tx_ev = s->listen = s->pending = s->hard = s->dgram = 0;
    s->pcb = s->lpcb = 0;
    s->parent = 0;
    s->rx_in = s->rx_out = s->tx_in = s->tx_out = s->unacked = 0;
    IP_ADDR4(&s->ip, addr & 0xFF, (addr >> 8) & 0xFF, (addr >> 16) & 0xFF, addr >> 24);
    s->port = (u16)port;
    s->app = a;
    s->tx = tx;
    barrier();
    s->rx = rx;
    if (net_run(do_connect, s, 1) != 0) {
        let_go(s, 1);
        return GLOS_E_NET_DOWN;
    }
    if (ms && !wait_for(a, s, connected, ms)) {
        let_go(s, 1);                           /* (given up: lwIP's own tries would take minutes) */
        return GLOS_E_NET_TIMEOUT;
    }
    if (s->state == GLOS_NETS_FAILED) {
        u32 why = s->why;
        let_go(s, 1);
        return (int)why;
    }
    *id = id_of(s);
    return 0;
}

int sock_send(struct app *a, u32 id, u16 ds, u32 off, u32 n, u32 ms, u32 *taken)
{
    struct sock *s = find(a, id);
    u32 done = 0;
    *taken = 0;
    if (!s || s->listen || s->dgram)
        return GLOS_E_ARGUMENT;
    for (;;) {
        u32 room, at, k;
        if (s->state == GLOS_NETS_FAILED) {
            *taken = done;
            return done ? 0 : s->why;
        }
        if (s->state == GLOS_NETS_CONNECTING)
            return GLOS_E_ARGUMENT;
        room = RING - (s->tx_in - s->tx_out);
        while (done < n && room) {
            at = s->tx_in % RING;
            k = n - done;
            if (k > room)
                k = room;
            if (k > RING - at)
                k = RING - at;
            if (user_rd(ds, off + done, s->tx + at, k) != 0)
                return GLOS_E_ARGUMENT;
            barrier();
            s->tx_in += k;
            done += k;
            room -= k;
        }
        net_kick();
        if (done == n || !wait_for(a, s, writable, ms))
            break;
    }
    if (done < n)
        s->tx_ev = 1;                           /* (an event when there is room again) */
    *taken = done;
    return 0;
}

int sock_recv(struct app *a, u32 id, u16 ds, u32 off, u32 room, u32 ms, u32 *got, u32 *state)
{
    struct sock *s = find(a, id);
    u32 n = 0, f;
    *got = 0;
    if (!s || s->listen || s->dgram)
        return GLOS_E_ARGUMENT;
    *state = s->state;
    if (s->state == GLOS_NETS_CONNECTING && !ms)
        return 0;
    wait_for(a, s, readable, ms);
    while (n < room && s->rx_in != s->rx_out) {
        u32 at = s->rx_out % RING, k = s->rx_in - s->rx_out;
        if (k > RING - at)
            k = RING - at;
        if (k > room - n)
            k = room - n;
        if (user_wr(ds, off + n, s->rx + at, k) != 0)
            return GLOS_E_ARGUMENT;
        barrier();
        f = irq_save();
        s->rx_out += k;
        s->unacked += k;
        irq_restore(f);
        n += k;
    }
    if (n)
        net_kick();                             /* (the window opens again) */
    if (!n && s->state == GLOS_NETS_FAILED)
        return s->why;
    *got = n;
    *state = s->state;
    return 0;
}

int sock_close(struct app *a, u32 id, int reset)
{
    struct sock *s = find(a, id);
    if (!s)
        return GLOS_E_ARGUMENT;
    let_go(s, reset < 0 ? s->hard : reset);
    return 0;
}

/* Whether its CLOSE resets it: read (v -1) or set. -1: no such connection; -2: a port. */
int sock_hard(struct app *a, u32 id, int v)
{
    struct sock *s = find(a, id);
    if (!s)
        return -1;
    if (s->listen)
        return -2;
    if (v >= 0)
        s->hard = (u8)(v != 0);
    return s->hard;
}

int sock_state(struct app *a, u32 id, u32 *waiting, u32 *state, u32 *room)
{
    struct sock *s = find(a, id);
    u32 f;
    if (!s)
        return GLOS_E_ARGUMENT;
    f = irq_save();
    *waiting = waiting_of(s);
    *state = s->state;
    *room = room_of(s);
    irq_restore(f);
    return 0;
}

/* The other end's address and port (a port listened on and a datagram port: 0 and its own port). */
int sock_peer(struct app *a, u32 id, u32 *addr, u32 *port)
{
    struct sock *s = find(a, id);
    if (!s)
        return GLOS_E_ARGUMENT;
    *addr = s->listen || s->dgram ? 0 : ip4_addr_get_u32(ip_2_ip4(&s->ip));
    *port = s->port;
    return 0;
}

/* ---- datagram ports (the app's side) */

int sock_dgram(struct app *a, u32 port, u32 flags, u32 *id)
{
    struct sock *s;
    u8 *rx, *tx;
    if (!net_up())
        return GLOS_E_NET_DOWN;
    if (port > 65535)
        return GLOS_E_ARGUMENT;
    if (!(s = slot(a)))
        return GLOS_E_MEMORY;
    rx = kmalloc(RING);
    tx = kmalloc(RING);
    if (!rx || !tx) {
        kfree(rx);
        kfree(tx);
        s->used = 0;
        return GLOS_E_MEMORY;
    }
    s->gen++;
    s->flags = flags & (GLOS_NETF_EVENTS | GLOS_NETF_BROADCAST | GLOS_NETF_REUSE);
    s->state = GLOS_NETS_CONNECTING;
    s->why = 0;
    s->closing = s->reset = s->tx_ev = s->listen = s->pending = s->hard = 0;
    s->bcast = (flags & GLOS_NETF_BROADCAST) != 0;
    s->pcb = s->lpcb = 0;
    s->upcb = 0;
    s->parent = 0;
    s->rx_in = s->rx_out = s->tx_in = s->tx_out = s->unacked = 0;
    s->rx_n = s->tx_need = s->dropped = 0;
    ip_addr_set_zero_ip4(&s->ip);
    s->port = (u16)port;
    s->app = a;
    s->dgram = 1;
    s->tx = tx;
    barrier();
    s->rx = rx;
    if (net_run(do_dgram, s, 1) != 0 || s->state != GLOS_NETS_DATAGRAM) {
        int why = s->state == GLOS_NETS_FAILED ? s->why : GLOS_E_NET_DOWN;
        let_go(s, 1);
        return why;
    }
    *id = id_of(s);
    return 0;
}

/* Whether SENDTO may send to a broadcast address: read (v -1) or set. -1: no such datagram port. */
int sock_bcast(struct app *a, u32 id, int v)
{
    struct sock *s = find(a, id);
    if (!s || !s->dgram)
        return -1;
    if (v >= 0)
        s->bcast = (u8)(v != 0);
    return s->bcast;
}

static int is_broadcast(u32 addr)               /* everyone, or an interface's network's broadcast address */
{
    struct netif *n;
    u32 f;
    int yes = addr == 0xFFFFFFFFu;
    f = irq_save();
    for (n = netif_list; n && !yes; n = n->next)
        yes = netif_is_up(n) && !ip4_addr_isany_val(*netif_ip4_addr(n)) && ip4_addr_isbroadcast_u32(addr, n);
    irq_restore(f);
    return yes;
}

static int dg_room(const struct sock *s)
{
    return RING - (s->tx_in - s->tx_out) >= DG_HDR + s->tx_need || s->state != GLOS_NETS_DATAGRAM;
}

int sock_sendto(struct app *a, u32 id, u16 ds, u32 off, u32 n, u32 ms, u32 addr, u32 port)
{
    struct sock *s = find(a, id);
    struct dg_hdr h;
    u32 at, first;
    if (!s || !s->dgram || n > DG_MAX || !port || port > 65535 || !addr)
        return GLOS_E_ARGUMENT;
    if (!s->bcast && is_broadcast(addr))
        return GLOS_E_REFUSED;                  /* (not without its BROADCAST) */
    if (!net_up())
        return GLOS_E_NET_DOWN;
    s->tx_need = n;
    if (!dg_room(s) && !wait_for(a, s, dg_room, ms)) {
        s->tx_ev = 1;                           /* (an event when there is room for it) */
        return GLOS_E_NET_TIMEOUT;
    }
    if (s->state != GLOS_NETS_DATAGRAM)
        return GLOS_E_ARGUMENT;
    at = (s->tx_in + DG_HDR) % RING;
    first = RING - at < n ? RING - at : n;
    if ((first && user_rd(ds, off, s->tx + at, first) != 0)
        || (n > first && user_rd(ds, off + first, s->tx, n - first) != 0))
        return GLOS_E_ARGUMENT;
    h.len = (u16)n, h.port = (u16)port, h.addr = addr;
    ring_put(s->tx, s->tx_in, &h, DG_HDR);
    barrier();
    s->tx_in += DG_HDR + n;
    net_kick();
    return 0;
}

static int dg_waiting(const struct sock *s) { return s->rx_n != 0 || s->state != GLOS_NETS_DATAGRAM; }

/* The next datagram: `got` of its bytes copied (what the room takes), its whole length, and who sent it. */
int sock_recvfrom(struct app *a, u32 id, u16 ds, u32 off, u32 room, u32 ms, u32 *got, u32 *len, u32 *addr, u32 *port)
{
    struct sock *s = find(a, id);
    struct dg_hdr h;
    u32 at, first, k, f;
    *got = *len = *addr = *port = 0;
    if (!s || !s->dgram)
        return GLOS_E_ARGUMENT;
    if (!wait_for(a, s, dg_waiting, ms) || !s->rx_n)
        return GLOS_E_NET_TIMEOUT;
    ring_get(s->rx, s->rx_out, &h, DG_HDR);
    k = h.len < room ? h.len : room;
    at = (s->rx_out + DG_HDR) % RING;
    first = RING - at < k ? RING - at : k;
    if ((first && user_wr(ds, off, s->rx + at, first) != 0)
        || (k > first && user_wr(ds, off + first, s->rx, k - first) != 0))
        return GLOS_E_ARGUMENT;                 /* (it waits still) */
    barrier();
    f = irq_save();
    s->rx_out += DG_HDR + h.len;
    s->rx_n--;
    irq_restore(f);
    *got = k, *len = h.len, *addr = h.addr, *port = h.port;
    return 0;
}

int sock_listen(struct app *a, u32 port, u32 backlog, u32 flags, u32 *id)
{
    struct sock *s;
    if (!net_up())
        return GLOS_E_NET_DOWN;
    if (port > 65535)
        return GLOS_E_ARGUMENT;
    if (!(s = slot(a)))
        return GLOS_E_MEMORY;
    s->gen++;
    s->flags = flags & GLOS_NETF_EVENTS;
    s->state = GLOS_NETS_CONNECTING;
    s->why = 0;
    s->closing = s->reset = s->tx_ev = s->pending = s->hard = s->dgram = 0;
    s->pcb = s->lpcb = 0;
    s->parent = 0;
    s->rx = s->tx = 0;
    s->rx_in = s->rx_out = s->tx_in = s->tx_out = s->unacked = 0;
    s->port = (u16)port;
    s->backlog = (u8)(backlog < 1 ? 1 : backlog > 8 ? 8 : backlog);
    s->app = a;
    barrier();
    s->listen = 1;
    if (net_run(do_listen, s, 1) != 0 || s->state != NETS_LISTENING) {
        int why = s->state == GLOS_NETS_FAILED ? s->why : GLOS_E_NET_DOWN;
        let_go(s, 1);
        return why;
    }
    *id = id_of(s);
    return 0;
}

static int has_waiting(const struct sock *l) { return waiting_on(l) != 0 || l->state != NETS_LISTENING; }

int sock_accept(struct app *a, u32 id, u32 ms, u32 flags, u32 *conn, u32 *addr, u32 *port)
{
    struct sock *l = find(a, id), *s = 0;
    u32 i, f;
    if (!l || !l->listen)
        return GLOS_E_ARGUMENT;
    wait_for(a, l, has_waiting, ms);
    f = irq_save();
    for (i = 0; i < NSOCK; i++)                 /* the one that came first */
        if (socks[i].used && socks[i].pending && socks[i].parent == l && (!s || (s32)(socks[i].seq - s->seq) < 0))
            s = &socks[i];
    if (s) {
        s->pending = 0;
        s->parent = 0;
        s->flags = flags & (GLOS_NETF_EVENTS | GLOS_NETF_NAGLE);
    }
    irq_restore(f);
    if (!s)
        return GLOS_E_NET_TIMEOUT;              /* (none came in the time) */
    if (flags & GLOS_NETF_NAGLE)
        net_run(do_nagle, s, 1);
    *conn = id_of(s);
    *addr = ip4_addr_get_u32(ip_2_ip4(&s->ip));
    *port = s->port;
    return 0;
}

void sock_app_gone(struct app *a)
{
    int i, any = 0;
    u32 f = irq_save();
    for (i = 0; i < NSOCK; i++)
        if (socks[i].used && socks[i].app == a) {
            socks[i].app = 0;
            socks[i].closing = 1;
            any = 1;
        }
    irq_restore(f);
    if (any)
        net_kick();
}

u32 sock_count(void)
{
    u32 n = 0;
    int i;
    for (i = 0; i < NSOCK; i++)
        n += socks[i].used;
    return n;
}
