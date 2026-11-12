/* API 2.0's NET (include/glos/api.h; docs/v1/phase-b.md step 11): TCP connections out, ports listened on and
 * the connections that come in on them, names resolved, the network's state. Over kernel/net/sock.c, which
 * 1.x's GLOS_FN_NET shares, and kernel/net/net.c.
 *
 * A connection (or a port) is a handle whose object is its number in sock.c's table. These calls are made
 * without the app's lock (api2.c): they wait, and the app's other threads go on. What they touch they look up
 * again after a wait (sock.c finds the connection by its number each time). */
#include <string.h>

#include "api2int.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "nic.h"
#include "sock.h"
#include "vm.h"

static u32 conn_of(struct app *a, glos_handle h)        /* its number, or 0 */
{
    u32 f = irq_save();
    void *obj = handle_obj(&a->v2->h, h, GLOS_HK_CONNECTION);
    irq_restore(f);
    return (u32)obj;
}

static glos_handle handle_for(struct app *a, u32 id)
{
    u32 f = irq_save();
    glos_handle h = handle_new(&a->v2->h, GLOS_HK_CONNECTION, (void *)id);
    irq_restore(f);
    return h;
}

static void handle_drop(struct app *a, glos_handle h)
{
    u32 f = irq_save();
    handle_close(&a->v2->h, h);
    irq_restore(f);
}

void api2_net_close(struct app *a, void *obj) { sock_close(a, (u32)obj, -1); }

/* GLOS_EV_NET for the connection with that number, if the app has a handle for it. (One that moves on before
   its call has made the handle has no event from here: the call posts its state itself.) */
void api2_net_event(struct app *a, u32 id, u32 state, u32 waiting, u32 room)
{
    struct glos_ev_net e;
    u32 i = 0, h;
    void *obj;
    memset(&e, 0, sizeof e);
    while (handle_next(&a->v2->h, &i, &h, &obj) == 0)
        if (GLOS_HANDLE_KIND(h) == GLOS_HK_CONNECTION && (u32)obj == id)
            e.ev.handle = h;
    if (!e.ev.handle)
        return;
    e.ev.size = sizeof e;
    e.ev.kind = GLOS_EV_NET;
    e.state = state, e.waiting = waiting, e.room = room;
    a2_post(a, &e.ev, 1);
}

static void changed_tell(struct app *a, void *arg)
{
    struct glos_ev ev;
    (void)arg;
    if (!a->v2)
        return;
    memset(&ev, 0, sizeof ev);
    ev.size = sizeof ev;
    ev.kind = GLOS_EV_NETWORK;
    a2_post(a, &ev, 1);
}

void api2_net_changed(void) { app_each(changed_tell, 0); }

/* ---- the calls */

static int resolve(struct app *a, struct trapframe *tf)
{
    struct glos_net_resolve q;
    char name[128];
    u32 size, i;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    for (i = 0; i < sizeof name; i++)
        if (user_rd((u16)tf->ds, q.name + i, &name[i], 1) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        else if (!name[i])
            break;
    if (i == sizeof name || !i)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if ((r = sock_resolve(a, name, q.ms, &q.address)) != 0)
        return a2_fail(tf, (u32)r);
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int connect(struct app *a, struct trapframe *tf)
{
    struct glos_net_connect q;
    u32 size, id = 0, waiting = 0, room = 0;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if ((r = sock_connect(a, q.address, q.port, q.ms, q.flags, &id)) != 0)
        return a2_fail(tf, (u32)r);
    q.handle = handle_for(a, id);
    sock_state(a, id, &waiting, &q.state, &room);
    if (!q.handle || a2_wr(tf, &q, sizeof q, size) != 0) {
        if (q.handle)
            handle_drop(a, q.handle);
        sock_close(a, id, 1);
        return a2_fail(tf, q.handle ? GLOS_E_ARGUMENT : GLOS_E_MEMORY);
    }
    if ((q.flags & GLOS_NETF_EVENTS) && !q.ms && q.state != GLOS_NETS_CONNECTING) {     /* (it moved on before
                                                   its handle was made: the event the net thread couldn't post) */
        u32 f = irq_save();
        api2_net_event(a, id, q.state, waiting, room);
        irq_restore(f);
    }
    return a2_ok(tf, 0);
}

static int io(struct app *a, struct trapframe *tf, int send)
{
    struct glos_net_io q;
    u32 size, id;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = conn_of(a, q.handle)))
        return a2_fail(tf, GLOS_E_HANDLE);
    q.done = 0;
    if (send) {
        u32 waiting, room;
        r = sock_send(a, id, (u16)tf->ds, q.buffer, q.count, q.ms, &q.done);
        if (sock_state(a, id, &waiting, &q.state, &room) != 0)
            q.state = GLOS_NETS_FAILED;
    } else {
        r = sock_recv(a, id, (u16)tf->ds, q.buffer, q.count, q.ms, &q.done, &q.state);
    }
    if (a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return r ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
}

static int listen(struct app *a, struct trapframe *tf)
{
    struct glos_net_listen q;
    u32 size, id = 0;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.backlog < 1 || q.backlog > 8)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if ((r = sock_listen(a, q.port, q.backlog, q.flags, &id)) != 0)
        return a2_fail(tf, (u32)r);
    q.handle = handle_for(a, id);
    if (!q.handle || a2_wr(tf, &q, sizeof q, size) != 0) {
        if (q.handle)
            handle_drop(a, q.handle);
        sock_close(a, id, 1);
        return a2_fail(tf, q.handle ? GLOS_E_ARGUMENT : GLOS_E_MEMORY);
    }
    return a2_ok(tf, 0);
}

static int accept(struct app *a, struct trapframe *tf)
{
    struct glos_net_accept q;
    u32 size, id, conn = 0;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = conn_of(a, q.listener)))
        return a2_fail(tf, GLOS_E_HANDLE);
    if ((r = sock_accept(a, id, q.ms, q.flags, &conn, &q.address, &q.port)) != 0)
        return a2_fail(tf, (u32)r);
    q.handle = handle_for(a, conn);
    if (!q.handle || a2_wr(tf, &q, sizeof q, size) != 0) {
        if (q.handle)
            handle_drop(a, q.handle);
        sock_close(a, conn, 1);
        return a2_fail(tf, q.handle ? GLOS_E_ARGUMENT : GLOS_E_MEMORY);
    }
    if (q.flags & GLOS_NETF_EVENTS) {           /* (what came in before it was accepted: told now) */
        u32 waiting = 0, state = 0, room = 0, f;
        if (sock_state(a, conn, &waiting, &state, &room) == 0 && (waiting || state != GLOS_NETS_OPEN)) {
            f = irq_save();
            api2_net_event(a, conn, state, waiting, room);
            irq_restore(f);
        }
    }
    return a2_ok(tf, 0);
}

static int state(struct trapframe *tf)
{
    struct glos_sys_stat q;
    struct glos_net_if nif;
    struct net_if k;
    u32 size, give;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    q.length = q.count = 0;
    if (net_if_info(&k) == 0) {                 /* (no card: an empty list) */
        memset(&nif, 0, sizeof nif);
        nif.size = sizeof nif;
        nif.kind = GLOS_NETIF_ETHERNET;
        nif.flags = (k.up ? GLOS_NETIF_UP : 0) | (k.up && k.dhcp ? GLOS_NETIF_DHCP : 0);
        memcpy(nif.mac, k.mac, 6);
        nif.address = k.ip, nif.mask = k.mask, nif.gateway = k.gw;
        nif.dns[0] = k.dns[0], nif.dns[1] = k.dns[1];
        nif.lease = k.lease, nif.lease_left = k.lease_left;
        nif.rx_frames = k.rx, nif.tx_frames = k.tx, nif.rx_errors = k.rx_errors, nif.overruns = k.overruns;
        nif.connections = sock_count();
        strncpy(nif.card, k.card, sizeof nif.card - 1);
        strncpy(nif.host, k.host, sizeof nif.host - 1);
        q.length = sizeof nif;
        give = q.room >= sizeof nif ? sizeof nif : 0;
        q.count = give ? 1 : 0;
        if (give && user_wr((u16)tf->ds, q.buffer, &nif, give) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    if (a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return q.count * sizeof nif < q.length ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

int api2_net(struct app *a, struct trapframe *tf, u32 n)
{
    switch (GLOS_FN(GLOS_AREA_NET, n)) {
    case GLOS_NET_RESOLVE:
        return resolve(a, tf);
    case GLOS_NET_CONNECT:
        return connect(a, tf);
    case GLOS_NET_SEND:
        return io(a, tf, 1);
    case GLOS_NET_RECEIVE:
        return io(a, tf, 0);
    case GLOS_NET_LISTEN:
        return listen(a, tf);
    case GLOS_NET_ACCEPT:
        return accept(a, tf);
    case GLOS_NET_STATE:
        return state(tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}

/* ---- a connection's properties */

static int p_get(struct app *a, void *obj, s32 *v, int what)
{
    u32 x[3], addr, port;
    if (sock_state(a, (u32)obj, &x[1], &x[0], &x[2]) != 0 || sock_peer(a, (u32)obj, &addr, &port) != 0)
        return GLOS_E_HANDLE;
    if (what >= 2 && what != 4 && x[0] == GLOS_NETS_LISTENING)  /* (a port has no room, no other end, no reset) */
        return GLOS_E_PROPERTY;
    *v = what < 3 ? (s32)x[what] : what == 3 ? (s32)addr : (s32)port;
    return 0;
}

#define GETTER(name, what) \
    static int name(struct app *a, void *obj, s32 *v, char *text, u32 *len) \
    { (void)text, (void)len; return p_get(a, obj, v, what); }
GETTER(state_get, 0) GETTER(waiting_get, 1) GETTER(room_get, 2) GETTER(addr_get, 3) GETTER(port_get, 4)

static int reset_get(struct app *a, void *obj, s32 *v, char *text, u32 *len)
{
    int r = sock_hard(a, (u32)obj, -1);
    (void)text, (void)len;
    if (r < 0)
        return r == -2 ? GLOS_E_PROPERTY : GLOS_E_HANDLE;
    *v = r;
    return 0;
}

static int reset_set(struct app *a, void *obj, s32 v, const char *text, u32 len)
{
    int r;
    (void)text, (void)len;
    if (v != 0 && v != 1)
        return GLOS_E_ARGUMENT;
    r = sock_hard(a, (u32)obj, v);
    return r >= 0 ? 0 : r == -2 ? GLOS_E_PROPERTY : GLOS_E_HANDLE;
}

static int no_set(struct app *a, void *obj, s32 v, const char *text, u32 len)
{
    (void)a, (void)obj, (void)v, (void)text, (void)len;
    return GLOS_E_PROPERTY;
}

const struct propdef api2_net_props[] = {
    { GLOS_PROP_NET_STATE, P_NUM | P_RO, state_get, no_set },
    { GLOS_PROP_NET_WAITING, P_NUM | P_RO, waiting_get, no_set },
    { GLOS_PROP_NET_ROOM, P_NUM | P_RO, room_get, no_set },
    { GLOS_PROP_NET_ADDRESS, P_NUM | P_RO, addr_get, no_set },
    { GLOS_PROP_NET_PORT, P_NUM | P_RO, port_get, no_set },
    { GLOS_PROP_NET_RESET, P_NUM, reset_get, reset_set },
};
const u32 api2_net_nprops = ARRAY_SIZE(api2_net_props);
