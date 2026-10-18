/* Apps' TCP connections (M10 step B; GLOS_FN_NET in include/glos/api.h).
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
 * connections the same way. */
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include <string.h>

#include "glos/api.h"
#include "../app/appint.h"
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
#define PER_APP 8
#define RING 16384u                             /* (a power of two, and more than TCP_WND) */

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
} socks[NSOCK];

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

/* Its app told: woken, and with ev an event if it asked for them. From the net thread. */
static void notify(struct sock *s, int ev)
{
    u32 f = irq_save();
    struct app *a = s->app;
    if (a) {
        if (ev && (s->flags & GLOS_NETF_EVENTS)) {
            struct glos_event e;
            memset(&e, 0, sizeof e);
            e.size = sizeof e;
            e.type = GLOS_EV_NET;
            e.key = id_of(s);
            e.x = s->state;
            e.y = (int)(s->rx_in - s->rx_out);
            app_event(a, &e);
        }
        thread_wake(&a->wq);
    }
    irq_restore(f);
}

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

static void do_connect(void *arg)
{
    struct sock *s = arg;
    struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!pcb) {
        s->why = GLOS_E_MEMORY;
        s->state = GLOS_NETS_FAILED;
        return;
    }
    tcp_arg(pcb, s);
    tcp_recv(pcb, on_recv);
    tcp_sent(pcb, on_sent);
    tcp_err(pcb, on_err);
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
        if (!s->used || !s->rx)
            continue;
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
        if (a->kill || !ms || (until && (s32)(timer_ticks() - until) >= 0)) {
            irq_restore(f);
            return 0;
        }
        thread_wait(&a->wq, until ? until : timer_ticks() + 1024);     /* (for ever: a second at a time) */
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

static int net_fail(struct trapframe *tf, u32 e)
{
    tf->eax = e;
    tf->eflags |= FL_CF;
    return 0;
}

static int resolve(struct app *a, struct trapframe *tf)
{
    char name[128];
    u8 ip[4];
    u32 i, until;
    int k, r;
    for (i = 0; i < sizeof name; i++)
        if (user_rd((u16)tf->ds, tf->ecx + i, &name[i], 1) != 0)
            return net_fail(tf, GLOS_E_ARGUMENT);
        else if (!name[i])
            break;
    if (i == sizeof name || !i)
        return net_fail(tf, GLOS_E_ARGUMENT);
    if (!net_up())
        return net_fail(tf, GLOS_E_NET_DOWN);
    if ((k = net_lookup_start(name)) < 0)
        return net_fail(tf, GLOS_E_MEMORY);     /* (the lookups are all taken) */
    until = tf->edx == 0xFFFFFFFFu ? 0 : timer_ticks() + ticks_of(tf->edx);
    while ((r = net_lookup_poll(k, ip)) == 0 && !a->kill && (!until || (s32)(timer_ticks() - until) < 0)) {
        u32 f = irq_save();
        thread_wait(&a->wq, timer_ticks() + 32);        /* (the resolver wakes its own queue: looked at often) */
        irq_restore(f);
    }
    if (r == 0) {
        net_lookup_drop(k);
        return net_fail(tf, GLOS_E_NET_TIMEOUT);
    }
    if (r < 0)
        return net_fail(tf, GLOS_E_NET_NAME);
    tf->eax = (u32)ip[0] | (u32)ip[1] << 8 | (u32)ip[2] << 16 | (u32)ip[3] << 24;
    return 0;
}

static int connect(struct app *a, struct trapframe *tf)
{
    struct sock *s = 0;
    u32 i, mine = 0, f;
    u8 *rx, *tx;
    if (!net_up())
        return net_fail(tf, GLOS_E_NET_DOWN);
    if (!tf->edx || tf->edx > 65535)
        return net_fail(tf, GLOS_E_ARGUMENT);
    f = irq_save();
    for (i = 0; i < NSOCK; i++) {
        if (socks[i].used && socks[i].app == a)
            mine++;
        else if (!socks[i].used && !s)
            s = &socks[i];
    }
    if (s && mine < PER_APP)
        s->used = 1;                            /* (the net thread's poll leaves it be until it has its rings) */
    irq_restore(f);
    if (!s || mine >= PER_APP)
        return net_fail(tf, GLOS_E_MEMORY);     /* (8 an app, 32 in all) */
    rx = kmalloc(RING);
    tx = kmalloc(RING);
    if (!rx || !tx) {
        kfree(rx);
        kfree(tx);
        s->used = 0;
        return net_fail(tf, GLOS_E_MEMORY);
    }
    s->gen++;
    s->flags = tf->edi & (GLOS_NETF_EVENTS | GLOS_NETF_NAGLE);
    s->state = GLOS_NETS_CONNECTING;
    s->why = 0;
    s->closing = s->reset = s->tx_ev = 0;
    s->pcb = 0;
    s->rx_in = s->rx_out = s->tx_in = s->tx_out = s->unacked = 0;
    IP_ADDR4(&s->ip, tf->ecx & 0xFF, (tf->ecx >> 8) & 0xFF, (tf->ecx >> 16) & 0xFF, tf->ecx >> 24);
    s->port = (u16)tf->edx;
    s->app = a;
    s->tx = tx;
    barrier();
    s->rx = rx;
    if (net_run(do_connect, s, 1) != 0) {
        let_go(s, 1);
        return net_fail(tf, GLOS_E_NET_DOWN);
    }
    if (tf->esi && !wait_for(a, s, connected, tf->esi)) {
        let_go(s, 1);                           /* (given up: lwIP's own tries would take minutes) */
        return net_fail(tf, GLOS_E_NET_TIMEOUT);
    }
    if (s->state == GLOS_NETS_FAILED) {
        u32 why = s->why;
        let_go(s, 1);
        return net_fail(tf, why);
    }
    tf->eax = id_of(s);
    return 0;
}

static int send(struct app *a, struct sock *s, struct trapframe *tf)
{
    u32 done = 0, n = tf->esi;
    for (;;) {
        u32 room, at, k;
        if (s->state == GLOS_NETS_FAILED)
            return done ? (tf->eax = done, 0) : net_fail(tf, s->why);
        if (s->state == GLOS_NETS_CONNECTING)
            return net_fail(tf, GLOS_E_ARGUMENT);
        room = RING - (s->tx_in - s->tx_out);
        while (done < n && room) {
            at = s->tx_in % RING;
            k = n - done;
            if (k > room)
                k = room;
            if (k > RING - at)
                k = RING - at;
            if (user_rd((u16)tf->ds, tf->edx + done, s->tx + at, k) != 0)
                return net_fail(tf, GLOS_E_ARGUMENT);
            barrier();
            s->tx_in += k;
            done += k;
            room -= k;
        }
        net_kick();
        if (done == n || !wait_for(a, s, writable, tf->edi))
            break;
    }
    if (done < n)
        s->tx_ev = 1;                           /* (an event when there is room again) */
    tf->eax = done;
    return 0;
}

static int recv(struct app *a, struct sock *s, struct trapframe *tf)
{
    u32 n = 0, f;
    if (s->state == GLOS_NETS_CONNECTING && !tf->edi) {
        tf->eax = 0;
        tf->ebx = s->state;
        return 0;
    }
    wait_for(a, s, readable, tf->edi);
    while (n < tf->esi && s->rx_in != s->rx_out) {
        u32 at = s->rx_out % RING, k = s->rx_in - s->rx_out;
        if (k > RING - at)
            k = RING - at;
        if (k > tf->esi - n)
            k = tf->esi - n;
        if (user_wr((u16)tf->ds, tf->edx + n, s->rx + at, k) != 0)
            return net_fail(tf, GLOS_E_ARGUMENT);
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
        return net_fail(tf, s->why);
    tf->eax = n;
    tf->ebx = s->state;
    return 0;
}

int sock_api(struct app *a, struct trapframe *tf)
{
    struct sock *s;
    if (tf->ebx == GLOS_NET_RESOLVE)
        return resolve(a, tf);
    if (tf->ebx == GLOS_NET_CONNECT)
        return connect(a, tf);
    if (tf->ebx > GLOS_NET_STATE || !(s = find(a, tf->ecx)))
        return net_fail(tf, GLOS_E_ARGUMENT);
    switch (tf->ebx) {
    case GLOS_NET_SEND:
        return send(a, s, tf);
    case GLOS_NET_RECV:
        return recv(a, s, tf);
    case GLOS_NET_CLOSE:
        let_go(s, tf->edx != 0);
        tf->eax = 0;
        return 0;
    default:
        tf->eax = s->rx_in - s->rx_out;
        tf->ebx = s->state;
        tf->ecx = RING - (s->tx_in - s->tx_out);
        return 0;
    }
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
