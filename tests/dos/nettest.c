/* NETTEST (GLOS M10 step B): an app's TCP connections through GLOS's NET
 * and the sockets on it (glos/net.h), against an echo server on the host
 * (tests/loopa/jobs.py net-app). DJGPP, libglos.
 *
 *   NETTEST all HOST PORT CLOSED [NAME]
 *                     every case below, "NETTEST <case> ok|FAIL <detail>":
 *                     HOST:PORT the echo server (it closes a connection that
 *                     sends "bye\n"), CLOSED a port with nothing behind it,
 *                     NAME a name to look up on the internet (if there is one)
 *   NETTEST bsd HOST PORT CLOSED
 *                     the sockets' cases alone
 *   NETTEST hold HOST PORT N
 *                     N connections open, "NETTEST holding", then waits to be
 *                     killed
 *   NETTEST udp HOST PORT
 *                     datagram ports (API 2.2) and SOCK_DGRAM sockets against
 *                     a UDP echo server at HOST:PORT (one that is sent
 *                     "burst N SIZE" sends N numbered datagrams back)
 *   NETTEST udpraw HOST PORT
 *                     the datagram sockets in a program made an app by an INIT
 *                     call of its own, not glos_init (as DOS-GL and SDL do)
 *   NETTEST udpserve PORT SECONDS
 *                     a datagram port at PORT: what comes is sent back behind
 *                     "re:", and "announce" is broadcast to PORT+1 every
 *                     second; until "quit" comes or SECONDS have passed
 *   NETTEST udpfind PORT
 *                     another PC's udpserve found by broadcast: "who" to
 *                     everyone at PORT ("NETTEST find-reply ok from=IP"), its
 *                     announcement heard at PORT+1 ("NETTEST find-announce ok
 *                     from=IP"), 1400 bytes there and back ("find-long"),
 *                     then "quit" to it
 *
 * "NETTEST-DONE failures=N" at the end of all. */
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <glos/glos.h>
#include <glos/net.h>

static int failures;
static unsigned host;

static void report(const char *name, int ok, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void report(const char *name, int ok, const char *fmt, ...)
{
    va_list ap;
    printf("NETTEST %s %s ", name, ok ? "ok" : "FAIL");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
    failures += !ok;
}

/* The bytes a connection sends: xorshift from its seed. */
static void pattern(unsigned *x, unsigned char *buf, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        *x ^= *x << 13;
        *x ^= *x >> 17;
        *x ^= *x << 5;
        buf[i] = (unsigned char)*x;
    }
}

static unsigned addr_of(const char *s)
{
    struct in_addr a;
    return inet_aton(s, &a) ? (unsigned)a.s_addr : 0;
}

/* Sends `total` bytes of seed's pattern on each of n connections and checks what comes back, all at once. */
struct flow {
    glos_handle id;
    unsigned tx_seed, rx_seed;
    unsigned sent, got;
    int bad;
};

static int echo_all(struct flow *f, int n, unsigned total, unsigned limit_ms)
{
    static unsigned char out[4096], in[4096], want[4096];
    unsigned t0 = glos_ticks();
    int i, busy = 1;
    while (busy) {
        busy = 0;
        for (i = 0; i < n; i++) {
            unsigned st;
            int r;
            if (f[i].sent < total) {
                unsigned k = total - f[i].sent < sizeof out ? total - f[i].sent : sizeof out, x = f[i].tx_seed;
                pattern(&x, out, k);
                r = glos_net_send(f[i].id, out, k, 0);
                if (r < 0)
                    return -1;
                if (r > 0) {
                    unsigned y = f[i].tx_seed;     /* (the pattern on from what was taken) */
                    pattern(&y, out, (unsigned)r);
                    f[i].tx_seed = y;
                    f[i].sent += (unsigned)r;
                }
            }
            r = glos_net_receive(f[i].id, in, sizeof in, f[i].sent < total ? 0 : 20, &st);
            if (r < 0)
                return -1;
            if (r > 0) {
                pattern(&f[i].rx_seed, want, (unsigned)r);
                if (memcmp(in, want, (size_t)r))
                    f[i].bad = 1;
                f[i].got += (unsigned)r;
            }
            if (f[i].got < total)
                busy = 1;
        }
        if (glos_ticks() - t0 > limit_ms)
            return -2;
    }
    return 0;
}

static void case_echo(unsigned port)
{
    struct flow f = { 0, 0x12345678u, 0x12345678u, 0, 0, 0 };
    unsigned t0 = glos_ticks(), ms;
    int r;
    f.id = glos_net_connect(host, port, 5000, 0);
    if (!f.id) {
        report("echo-1m", 0, "connect: error %d", glos_error());
        return;
    }
    r = echo_all(&f, 1, 1u << 20, 120000);
    ms = glos_ticks() - t0;
    report("echo-1m", r == 0 && !f.bad && f.got == 1u << 20, "r=%d sent=%u got=%u bad=%d ms=%u kbps=%u", r, f.sent,
           f.got, f.bad, ms, ms ? (unsigned)(2ull * f.got * 1000 / 1024 / ms) : 0);
    glos_net_close(f.id, 0);
}

static void case_many(unsigned port)
{
    struct flow f[16];                          /* (an app has 16 at most) */
    glos_handle more;
    int i, r, err;
    memset(f, 0, sizeof f);
    for (i = 0; i < 16; i++) {
        f[i].tx_seed = f[i].rx_seed = 0x9E3779B9u * (unsigned)(i + 1);
        if (!(f[i].id = glos_net_connect(host, port, 5000, 0))) {
            report("many", 0, "connect %d: error %d", i, glos_error());
            while (i--)
                glos_net_close(f[i].id, 1);
            return;
        }
    }
    more = glos_net_connect(host, port, 5000, 0);
    err = glos_error();
    report("one-more-refused", !more && err == GLOS_E_MEMORY, "handle=%u error=%d", more, err);
    if (more)
        glos_net_close(more, 1);
    r = echo_all(f, 16, 32768, 120000);
    for (i = 0; i < 16 && r == 0; i++)
        if (f[i].bad || f[i].got != 32768)
            r = 10 + i;
    report("many", r == 0, "r=%d", r);
    for (i = 0; i < 16; i++)
        glos_net_close(f[i].id, 0);
}

static void case_closed(unsigned port)
{
    char buf[64];
    unsigned st = 0;
    glos_handle id = glos_net_connect(host, port, 5000, 0);
    int r, n = 0;
    if (!id) {
        report("far-close", 0, "connect: error %d", glos_error());
        return;
    }
    glos_net_send(id, "bye\n", 4, 1000);
    while ((r = glos_net_receive(id, buf, sizeof buf, 5000, &st)) > 0)
        n += r;
    report("far-close", r == 0 && st == GLOS_NETS_CLOSED && n == 4, "r=%d state=%u echoed=%d", r, st, n);
    r = glos_net_send(id, "x", 1, 0);           /* (half-closed: the app may still send) */
    glos_net_close(id, 0);
    r = glos_net_receive(id, buf, 1, 0, &st);
    report("closed-id", r == -GLOS_E_HANDLE, "r=%d", r);
}

static void case_events(unsigned port)
{
    union {
        struct glos_ev ev;
        struct glos_ev_net net;
        unsigned char room[GLOS_EV_MAX];
    } ev;
    char buf[16];
    unsigned st;
    glos_handle id = glos_net_connect(host, port, 0, GLOS_NETF_EVENTS);
    int opened = 0, data = 0, i, r;
    if (!id) {
        report("events", 0, "connect: error %d", glos_error());
        return;
    }
    for (i = 0; i < 50 && !opened; i++)
        if (glos_event(&ev, sizeof ev, 100, 0) > 0 && ev.ev.kind == GLOS_EV_NET && ev.ev.handle == id
            && ev.net.state == GLOS_NETS_OPEN)
            opened = 1;
    glos_net_send(id, "ping\n", 5, 1000);
    for (i = 0; i < 50 && !data; i++)
        if (glos_event(&ev, sizeof ev, 100, 0) > 0 && ev.ev.kind == GLOS_EV_NET && ev.ev.handle == id && ev.net.waiting > 0)
            data = (int)ev.net.waiting;
    r = glos_net_receive(id, buf, sizeof buf, 1000, &st);
    report("events", opened && data > 0 && r == 5 && !memcmp(buf, "ping\n", 5), "opened=%d data=%d r=%d", opened,
           data, r);
    glos_net_close(id, 0);
}

static void case_bsd(const char *hosts, unsigned port, unsigned closed)
{
    struct addrinfo hints, *ai = 0;
    char svc[8], buf[32];
    struct timeval tv;
    fd_set rd, wr;
    int fd, r, n = 0, e;
    socklen_t len;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    sprintf(svc, "%u", port);
    r = getaddrinfo(hosts, svc, &hints, &ai);
    if (r != 0) {
        report("bsd", 0, "getaddrinfo: %s", gai_strerror(r));
        return;
    }
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    r = fd >= 0 ? connect(fd, ai->ai_addr, ai->ai_addrlen) : -1;
    freeaddrinfo(ai);
    if (r != 0) {
        report("bsd", 0, "socket/connect: %s", glos_net_strerror(errno));
        return;
    }
    r = write(fd, "hello\n", 6);
    FD_ZERO(&rd);
    FD_SET(fd, &rd);
    tv.tv_sec = 5, tv.tv_usec = 0;
    if (r == 6 && select(fd + 1, &rd, 0, 0, &tv) == 1)
        n = read(fd, buf, sizeof buf);
    report("bsd", r == 6 && n == 6 && !memcmp(buf, "hello\n", 6), "write=%d read=%d", r, n);
    close(fd);
    r = (int)glos_socket_handle(fd);
    report("bsd-close", r == 0, "handle=%d", r);

    /* a connection refused, as errno */
    {
        struct sockaddr_in in;
        memset(&in, 0, sizeof in);
        in.sin_family = AF_INET;
        in.sin_port = htons((unsigned short)closed);
        in.sin_addr.s_addr = (in_addr_t)host;
        fd = socket(AF_INET, SOCK_STREAM, 0);
        r = connect(fd, (struct sockaddr *)&in, sizeof in);
        e = errno;
        close(fd);
        report("bsd-refused", r == -1 && e == ECONNREFUSED, "r=%d errno=%d", r, e);

        /* non-blocking: EINPROGRESS, then select says it can be written to */
        in.sin_port = htons((unsigned short)port);
        fd = socket(AF_INET, SOCK_STREAM, 0);
        fcntl(fd, F_SETFL, O_NONBLOCK);
        r = connect(fd, (struct sockaddr *)&in, sizeof in);
        e = errno;
        FD_ZERO(&wr);
        FD_SET(fd, &wr);
        tv.tv_sec = 5, tv.tv_usec = 0;
        n = select(fd + 1, 0, &wr, 0, &tv);
        len = sizeof r;
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &e, &len);
        report("bsd-nonblock", n == 1 && e == 0, "connect=%d select=%d so_error=%d", r, n, e);
        r = recv(fd, buf, sizeof buf, 0);
        e = errno;
        report("bsd-eagain", r == -1 && e == EAGAIN, "recv=%d errno=%d", r, e);
        close(fd);
    }
    r = gethostbyname("no-such-name.invalid") == 0 && h_errno == HOST_NOT_FOUND;
    report("bsd-noname", r, "h_errno=%d", h_errno);
}

/* ---- datagram ports (API 2.2) */

static int dg_echo(glos_handle h, unsigned port, unsigned n, unsigned seed)
{
    static unsigned char out[GLOS_NET_DATAGRAM_MAX], in[GLOS_NET_DATAGRAM_MAX + 8];
    unsigned from = 0, fport = 0, len = 0;
    int r;
    pattern(&seed, out, n);
    if (glos_net_sendto(h, out, n, 1000, host, port) != 0)
        return -100;
    r = glos_net_recvfrom(h, in, sizeof in, 3000, &from, &fport, &len);
    return r == (int)n && len == n && from == host && fport == port && !memcmp(in, out, n) ? 0 : r < 0 ? r : -200;
}

static void case_udp(unsigned port)
{
    union {
        struct glos_ev ev;
        struct glos_ev_net net;
        unsigned char room[GLOS_EV_MAX];
    } ev;
    static unsigned char buf[GLOS_NET_DATAGRAM_MAX + 1];
    struct glos_version v;
    unsigned from, fport, len, t0, ms, bcast = host | 0xFF000000u;       /* (the host's /24) */
    int st = 0, waiting = -1, room = 0, own = 0, r, r2, r3, i, n, last, ordered;
    glos_handle h, h2, h3;
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    glos_version(&v);
    report("dg-version", v.major == 2 && v.minor >= 2 && (v.functions[GLOS_AREA_NET][0] & 0x380) == 0x380,
           "version=%u.%u net=%x", v.major, v.minor, v.functions[GLOS_AREA_NET][0]);
    h = glos_net_datagram(0, 0);
    if (!h) {
        report("dg-open", 0, "error %d", glos_error());
        return;
    }
    glos_get(h, GLOS_PROP_NET_STATE, &st);
    glos_get(h, GLOS_PROP_NET_WAITING, &waiting);
    glos_get(h, GLOS_PROP_NET_ROOM, &room);
    glos_get(h, GLOS_PROP_NET_PORT, &own);
    report("dg-open", st == GLOS_NETS_DATAGRAM && waiting == 0 && room == GLOS_NET_DATAGRAM_MAX && own > 0 && own < 65536,
           "state=%d waiting=%d room=%d port=%d", st, waiting, room, own);
    r = dg_echo(h, port, 1, 11);
    r2 = dg_echo(h, port, 512, 12);
    r3 = dg_echo(h, port, GLOS_NET_DATAGRAM_MAX, 13);
    report("dg-echo", !r && !r2 && !r3, "1: %d, 512: %d, %d: %d", r, r2, GLOS_NET_DATAGRAM_MAX, r3);
    r = glos_net_sendto(h, buf, GLOS_NET_DATAGRAM_MAX + 1, 0, host, port);
    r2 = glos_net_sendto(h, buf, 10, 0, host, 0);
    r3 = glos_net_send(h, buf, 10, 0);                  /* (a connection's call) */
    report("dg-refusals", r == -GLOS_E_ARGUMENT && r2 == -GLOS_E_ARGUMENT && r3 == -GLOS_E_ARGUMENT,
           "too long: %d, port 0: %d, SEND: %d", r, r2, r3);

    /* one cut short: what the room takes, its whole length, and the rest gone */
    memset(buf, 'x', 300);
    glos_net_sendto(h, buf, 300, 1000, host, port);
    len = 0;
    r = glos_net_recvfrom(h, buf, 100, 3000, &from, &fport, &len);
    r2 = glos_net_recvfrom(h, buf, 100, 300, 0, 0, 0);
    report("dg-truncated", r == 100 && len == 300 && r2 == -GLOS_E_NET_TIMEOUT, "copied=%d length=%u next=%d", r, len, r2);

    t0 = glos_ticks();
    r = glos_net_recvfrom(h, buf, sizeof buf, 500, 0, 0, 0);
    ms = glos_ticks() - t0;
    r2 = glos_net_recvfrom(h, buf, sizeof buf, 0, 0, 0, 0);
    report("dg-timeout", r == -GLOS_E_NET_TIMEOUT && ms >= 450 && ms < 1500 && r2 == -GLOS_E_NET_TIMEOUT,
           "r=%d ms=%u at once: %d", r, ms, r2);

    /* more than the queue holds: 64 wait, in the order they came, and the rest were dropped */
    glos_net_sendto(h, "burst 80 200", 12, 1000, host, port);
    glos_sleep(1500);
    glos_get(h, GLOS_PROP_NET_WAITING, &waiting);
    for (n = 0, last = -1, ordered = 1; n < 100; n++) {
        if ((r = glos_net_recvfrom(h, buf, sizeof buf, 0, 0, 0, &len)) < 0)
            break;
        buf[4] = 0;
        i = atoi((char *)buf);
        ordered &= r == 200 && i > last;
        last = i;
    }
    report("dg-queue", waiting == GLOS_NET_DATAGRAM_QUEUE && n == GLOS_NET_DATAGRAM_QUEUE && ordered && last == 63,
           "waiting=%d read=%d ordered=%d last=%d", waiting, n, ordered, last);

    /* a broadcast address: refused until the port may */
    r = glos_net_sendto(h, "hello", 5, 0, 0xFFFFFFFFu, port);
    r2 = glos_net_sendto(h, "hello", 5, 0, bcast, port);
    glos_set(h, GLOS_PROP_NET_BROADCAST, 1);
    r3 = glos_net_sendto(h, "hello", 5, 0, 0xFFFFFFFFu, port);
    i = glos_net_sendto(h, "hello", 5, 0, bcast, port);
    report("dg-broadcast", r == -GLOS_E_REFUSED && r2 == -GLOS_E_REFUSED && r3 == 0 && i == 0,
           "without: %d %d, with: %d %d", r, r2, r3, i);
    glos_sleep(300);
    while (glos_net_recvfrom(h, buf, sizeof buf, 0, 0, 0, 0) >= 0)      /* (an echo of those, if the server heard) */
        ;
    glos_close(h);

    /* a port of the app's choosing; taken twice only when both say so */
    h = glos_net_datagram(47001, GLOS_NETF_REUSE);
    own = 0;
    if (h)
        glos_get(h, GLOS_PROP_NET_PORT, &own);
    h2 = glos_net_datagram(47001, GLOS_NETF_REUSE);
    h3 = glos_net_datagram(47001, 0);
    r = glos_error();
    report("dg-port", h && own == 47001 && h2 && !h3 && r == GLOS_E_NET_REFUSED, "port=%d second=%d third=%d error=%d", own,
           h2 != 0, h3 != 0, r);
    if (h2)
        glos_close(h2);                                 /* (of two on a port, the later one has what comes in) */
    r = h ? dg_echo(h, port, 64, 21) : -1;              /* (the server answers to the port it heard from) */
    report("dg-port-echo", r == 0, "r=%d", r);
    if (h)
        glos_close(h);

    /* events: one when a datagram comes in */
    h = glos_net_datagram(0, GLOS_NETF_EVENTS);
    glos_net_sendto(h, "ping", 4, 1000, host, port);
    for (i = 0, n = 0; i < 50 && !n; i++)
        if (glos_event(&ev, sizeof ev, 100, 0) > 0 && ev.ev.kind == GLOS_EV_NET && ev.ev.handle == h
            && ev.net.state == GLOS_NETS_DATAGRAM)
            n = (int)ev.net.waiting;
    r = glos_net_recvfrom(h, buf, sizeof buf, 1000, 0, 0, 0);
    report("dg-events", n == 1 && r == 4 && !memcmp(buf, "ping", 4), "waiting=%d r=%d", n, r);
    glos_close(h);
}

static void case_udp_bsd(unsigned port)
{
    struct sockaddr_in to, from, own;
    static char buf[GLOS_NET_DATAGRAM_MAX + 1];
    struct timeval tv;
    fd_set rd;
    socklen_t len;
    int fd, fd2, r, r2, r3, e, e2, v = 0, one = 1;
    memset(&to, 0, sizeof to);
    to.sin_family = AF_INET;
    to.sin_port = htons((unsigned short)port);
    to.sin_addr.s_addr = (in_addr_t)host;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        report("dgbsd-socket", 0, "%s", glos_net_strerror(errno));
        return;
    }
    len = sizeof v;
    getsockopt(fd, SOL_SOCKET, SO_TYPE, &v, &len);
    memset(&own, 0, sizeof own);
    own.sin_family = AF_INET;
    own.sin_port = htons(47002);
    own.sin_addr.s_addr = htonl(INADDR_ANY);
    r = bind(fd, (struct sockaddr *)&own, sizeof own);
    memset(&own, 0, sizeof own);
    len = sizeof own;
    r2 = getsockname(fd, (struct sockaddr *)&own, &len);
    fd2 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    r3 = bind(fd2, (struct sockaddr *)&own, sizeof own);        /* (the same port: in use) */
    e = errno;
    report("dgbsd-bind", v == SOCK_DGRAM && r == 0 && r2 == 0 && ntohs(own.sin_port) == 47002 && r3 == -1 && e == EADDRINUSE,
           "type=%d bind=%d port=%u second=%d errno=%d", v, r, ntohs(own.sin_port), r3, e);
    close(fd2);

    r = (int)sendto(fd, "hello", 5, 0, (struct sockaddr *)&to, sizeof to);
    FD_ZERO(&rd);
    FD_SET(fd, &rd);
    tv.tv_sec = 5, tv.tv_usec = 0;
    r2 = select(fd + 1, &rd, 0, 0, &tv);
    memset(&from, 0, sizeof from);
    len = sizeof from;
    r3 = (int)recvfrom(fd, buf, sizeof buf, 0, (struct sockaddr *)&from, &len);
    report("dgbsd-echo", r == 5 && r2 == 1 && r3 == 5 && !memcmp(buf, "hello", 5) && from.sin_addr.s_addr == (in_addr_t)host
           && ntohs(from.sin_port) == port, "sendto=%d select=%d recvfrom=%d from=%s:%u", r, r2, r3,
           inet_ntoa(from.sin_addr), ntohs(from.sin_port));

    memset(buf, 'y', 300);
    sendto(fd, buf, 300, 0, (struct sockaddr *)&to, sizeof to);
    r = (int)recvfrom(fd, buf, 100, MSG_TRUNC, 0, 0);          /* (its real length, as Linux gives it) */
    sendto(fd, buf, 300, 0, (struct sockaddr *)&to, sizeof to);
    r2 = (int)recvfrom(fd, buf, 100, 0, 0, 0);
    report("dgbsd-trunc", r == 300 && r2 == 100, "with MSG_TRUNC: %d, without: %d", r, r2);

    fcntl(fd, F_SETFL, O_NONBLOCK);
    r = (int)recvfrom(fd, buf, sizeof buf, 0, 0, 0);
    e = errno;
    fcntl(fd, F_SETFL, 0);
    r2 = (int)recvfrom(fd, buf, sizeof buf, MSG_DONTWAIT, 0, 0);
    e2 = errno;
    report("dgbsd-eagain", r == -1 && e == EAGAIN && r2 == -1 && e2 == EAGAIN, "nonblock: %d errno=%d, dontwait: %d errno=%d",
           r, e, r2, e2);

    r = (int)sendto(fd, buf, GLOS_NET_DATAGRAM_MAX + 1, 0, (struct sockaddr *)&to, sizeof to);
    e = errno;
    r2 = (int)send(fd, "x", 1, 0);                              /* (no peer set) */
    e2 = errno;
    report("dgbsd-errors", r == -1 && e == EMSGSIZE && r2 == -1 && e2 == EDESTADDRREQ, "too long: %d errno=%d, no peer: %d errno=%d",
           r, e, r2, e2);

    to.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    r = (int)sendto(fd, "hello", 5, 0, (struct sockaddr *)&to, sizeof to);
    e = errno;
    r2 = setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof one);
    r3 = (int)sendto(fd, "hello", 5, 0, (struct sockaddr *)&to, sizeof to);
    report("dgbsd-broadcast", r == -1 && e == EACCES && r2 == 0 && r3 == 5, "without: %d errno=%d, with: %d", r, e, r3);
    to.sin_addr.s_addr = (in_addr_t)host;
    close(fd);

    /* connect(): send() and recv() with the peer it set; a socket never bound has a port of GLOS's choosing */
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    r = connect(fd, (struct sockaddr *)&to, sizeof to);
    r2 = (int)send(fd, "again", 5, 0);
    tv.tv_sec = 5, tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    r3 = (int)recv(fd, buf, sizeof buf, 0);
    len = sizeof own;
    memset(&own, 0, sizeof own);
    getsockname(fd, (struct sockaddr *)&own, &len);
    report("dgbsd-connect", r == 0 && r2 == 5 && r3 == 5 && !memcmp(buf, "again", 5) && ntohs(own.sin_port) != 0,
           "connect=%d send=%d recv=%d port=%u", r, r2, r3, ntohs(own.sin_port));
    close(fd);
}

static int udp(int argc, char **argv)
{
    if (argc < 4)
        return 2;
    host = addr_of(argv[2]);
    case_udp((unsigned)atoi(argv[3]));
    case_udp_bsd((unsigned)atoi(argv[3]));
    printf("NETTEST-UDP-DONE failures=%d\n", failures);
    return failures != 0;
}

static const char *ip_text(unsigned a)
{
    struct in_addr ia;
    ia.s_addr = (in_addr_t)a;
    return inet_ntoa(ia);
}

/* The network's interfaces as text, "kind:name:address:flags" each, commas between: NET's STATE. */
static int ifs_text(char *out, unsigned room)
{
    struct glos_net_if nif[4];
    unsigned count = 0, i, k = 0;
    out[0] = 0;
    if (glos_net_state(nif, sizeof nif, &count) != 0)
        return -1;
    for (i = 0; i < count && k + 48 < room; i++)
        k += (unsigned)sprintf(out + k, "%s%u:%s:%s:%u", i ? "," : "", nif[i].kind, nif[i].card, ip_text(nif[i].address),
                               nif[i].flags & 3);
    return (int)count;
}

static int udpserve(int argc, char **argv)
{
    static char buf[GLOS_NET_DATAGRAM_MAX], out[GLOS_NET_DATAGRAM_MAX], hello[128];
    unsigned port, secs, t0, last = 0, from, fport, len, heard = 0;
    glos_handle h;
    int r;
    if (argc < 4)
        return 2;
    port = (unsigned)atoi(argv[2]);
    secs = (unsigned)atoi(argv[3]);
    if (!(h = glos_net_datagram(port, GLOS_NETF_BROADCAST))) {
        printf("NETTEST udpserve FAIL error %d\n", glos_error());
        return 1;
    }
    printf("NETTEST serving %u\n", port);
    fflush(stdout);
    for (t0 = glos_ticks(); glos_ticks() - t0 < secs * 1000;) {
        if (glos_ticks() - last >= 1000) {           /* (with this PC's interfaces, for the one that hears it to show) */
            last = glos_ticks();
            strcpy(hello, "announce ");
            ifs_text(hello + 9, sizeof hello - 9);
            glos_net_sendto(h, hello, strlen(hello), 0, 0xFFFFFFFFu, port + 1);
        }
        if ((r = glos_net_recvfrom(h, buf, sizeof buf - 3, 200, &from, &fport, &len)) < 0)
            continue;
        heard++;
        if (r == 4 && !memcmp(buf, "quit", 4))
            break;
        memcpy(out, "re:", 3);
        memcpy(out + 3, buf, (unsigned)r);
        glos_net_sendto(h, out, (unsigned)r + 3, 500, from, fport);
    }
    printf("NETTEST served %u\n", heard);
    glos_close(h);
    return 0;
}

static int udpfind(int argc, char **argv)
{
    char buf[160];
    unsigned port, from = 0, fport = 0, server = 0;
    glos_handle ask, hear;
    int r = -1, i;
    if (argc < 3)
        return 2;
    port = (unsigned)atoi(argv[2]);
    ask = glos_net_datagram(0, GLOS_NETF_BROADCAST);
    hear = glos_net_datagram(port + 1, 0);
    if (!ask || !hear) {
        printf("NETTEST udpfind FAIL error %d\n", glos_error());
        return 1;
    }
    for (i = 0; i < 5 && r < 0; i++) {                  /* (asked again: a broadcast may be lost) */
        glos_net_sendto(ask, "who", 3, 0, 0xFFFFFFFFu, port);
        r = glos_net_recvfrom(ask, buf, sizeof buf, 1500, &from, &fport, 0);
    }
    report("find-reply", r == 6 && !memcmp(buf, "re:who", 6) && fport == port, "from=%s r=%d asked=%d", ip_text(from), r, i);
    server = from;
    if (server) {                                       /* a long one, there and back: more than a cable's packet
                                                           holds, so cut into fragments and put together again */
        static unsigned char big[1400], back[1403 + 8];
        unsigned seed = 77, len = 0;
        pattern(&seed, big, sizeof big);
        glos_net_sendto(ask, big, sizeof big, 1000, server, port);
        r = glos_net_recvfrom(ask, back, sizeof back, 8000, 0, 0, &len);
        report("find-long", r == 1403 && len == 1403 && !memcmp(back, "re:", 3) && !memcmp(back + 3, big, sizeof big),
               "r=%d length=%u", r, len);
    }
    r = glos_net_recvfrom(hear, buf, sizeof buf - 1, 5000, &from, &fport, 0);
    buf[r > 0 ? r : 0] = 0;
    report("find-announce", r >= 9 && !memcmp(buf, "announce ", 9), "from=%s r=%d ifs=%s", ip_text(from), r,
           r >= 9 ? buf + 9 : "");
    r = ifs_text(buf, sizeof buf);
    report("find-ifs", r > 0, "n=%d ifs=%s", r, buf);
    if (server)
        glos_net_sendto(ask, "quit", 4, 500, server, port);
    glos_sleep(500);                                    /* (sent before the port goes) */
    glos_close(ask);
    glos_close(hear);
    return failures != 0;
}

static int all(int argc, char **argv)
{
    unsigned port, closed, a, t0, ms;
    unsigned char r1[64], r2[64], big[256];        /* (the most a call gives) */
    int r, i, zero = 1;
    if (argc < 5)
        return 2;
    host = addr_of(argv[2]);
    port = (unsigned)atoi(argv[3]);
    closed = (unsigned)atoi(argv[4]);
    {
        struct glos_version v;
        memset(&v, 0, sizeof v);
        v.size = sizeof v;
        r = glos_version(&v);
        report("version", r == 0 && v.major == 2 && (v.machine & GLOS_MACHINE_NET), "version=%u.%u machine=%x", v.major,
               v.minor, v.machine);
    }
    r = glos_random(r1, sizeof r1) == 0 && glos_random(r2, sizeof r2) == 0 && glos_random(big, sizeof big) == 0;
    for (i = 0; i < 64; i++)
        zero &= !r1[i];
    report("random", r && !zero && memcmp(r1, r2, sizeof r1), "r=%d zero=%d", r, zero);

    {
        struct glos_time t = { sizeof t, 0, 0, 0 };
        r = glos_time(&t);
        printf("NETTEST-TIME how=%u utc=%u offset=%d\n", t.how, t.utc, t.offset);  /* (the host compares it with its own) */
        report("time", r == 0, "r=%d how=%u", r, t.how);
    }
    r = glos_net_resolve(argv[2], 1000, &a);
    report("resolve-dotted", r == 0 && a == host, "r=%d addr=%08x", r, a);
    r = glos_net_resolve("no-such-name.invalid", 20000, &a);
    report("resolve-unknown", r == GLOS_E_NET_NAME, "r=%d", r);
    if (argc > 5) {
        r = glos_net_resolve(argv[5], 20000, &a);
        report("resolve-internet", r == 0 && a, "%s: r=%d addr=%08x", argv[5], r, a);
    }
    r = (int)glos_net_connect(host, closed, 5000, 0);
    report("refused", !r && glos_error() == GLOS_E_NET_REFUSED, "handle=%d error=%d", r, glos_error());
    t0 = glos_ticks();
    r = (int)glos_net_connect(addr_of("10.255.255.1"), 80, 2000, 0);        /* (an address nobody answers for) */
    ms = glos_ticks() - t0;
    report("timeout", !r && glos_error() == GLOS_E_NET_TIMEOUT && ms >= 1900 && ms < 4000, "handle=%d error=%d ms=%u",
           r, glos_error(), ms);
    case_echo(port);
    case_many(port);
    case_closed(port);
    case_events(port);
    case_bsd(argv[2], port, closed);
    printf("NETTEST-DONE failures=%d\n", failures);
    return failures != 0;
}

static int hold(int argc, char **argv)
{
    int i, n;
    if (argc < 5)
        return 2;
    host = addr_of(argv[2]);
    n = atoi(argv[4]);
    for (i = 0; i < n; i++)
        if (!glos_net_connect(host, (unsigned)atoi(argv[3]), 5000, 0)) {
            printf("NETTEST hold FAIL connect %d: error %d\n", i, glos_error());
            return 1;
        }
    printf("NETTEST holding %d\n", n);
    fflush(stdout);
    for (;;)
        glos_sleep(1000);
}

/* The sockets in a program that a library made an app with an INIT call of its own, as DOS-GL's and SDL's GLOS
   back ends do: none before it is one, then all of the datagram sockets' cases. */
static int udpraw(int argc, char **argv)
{
    struct glos_init in;
    int fd, e, r;
    if (argc < 4)
        return 2;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    e = errno;
    report("raw-before", fd == -1 && e == ENOSYS && !glos_is_app(), "socket=%d errno=%d", fd, e);
    memset(&in, 0, sizeof in);
    in.size = sizeof in;
    in.major = GLOS_MAJOR, in.minor = GLOS_MINOR;
    strcpy(in.name, "NETTEST RAW");
    r = glos_call(GLOS_CORE_INIT, &in);
    report("raw-init", r == 0 && !glos_app() && glos_is_app(), "r=%d app=%u is_app=%d", r, glos_app(), glos_is_app());
    host = addr_of(argv[2]);
    case_udp_bsd((unsigned)atoi(argv[3]));
    printf("NETTEST-RAW-DONE failures=%d\n", failures);
    return failures != 0;
}

int main(int argc, char **argv)
{
    int r;
    if (argc > 1 && !strcmp(argv[1], "udpraw"))
        return udpraw(argc, argv);
    r = glos_init("NETTEST", "", "");
    if (r != 0) {
        printf("NETTEST no GLOS (error %d)\n", r);
        return 3;
    }
    if (argc > 1 && !strcmp(argv[1], "all"))
        return all(argc, argv);
    if (argc > 4 && !strcmp(argv[1], "bsd")) {
        host = addr_of(argv[2]);
        case_bsd(argv[2], (unsigned)atoi(argv[3]), (unsigned)atoi(argv[4]));
        printf("NETTEST-DONE failures=%d\n", failures);
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "hold"))
        return hold(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "udp"))
        return udp(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "udpserve"))
        return udpserve(argc, argv);
    if (argc > 1 && !strcmp(argv[1], "udpfind"))
        return udpfind(argc, argv);
    printf("usage: NETTEST all HOST PORT CLOSED [NAME] | bsd HOST PORT CLOSED | hold HOST PORT N | udp HOST PORT\n");
    return 2;
}
