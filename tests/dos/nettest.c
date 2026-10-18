/* NETTEST (GLOS M10 step B): an app's TCP connections through GLOS_FN_NET
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
 *
 * "NETTEST-DONE failures=N" at the end of all. */
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <glos/api.h>
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
    int id;
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
                r = glos_net_send((unsigned)f[i].id, out, k, 0);
                if (r < 0)
                    return -1;
                if (r > 0) {
                    unsigned y = f[i].tx_seed;     /* (the pattern on from what was taken) */
                    pattern(&y, out, (unsigned)r);
                    f[i].tx_seed = y;
                    f[i].sent += (unsigned)r;
                }
            }
            r = glos_net_recv((unsigned)f[i].id, in, sizeof in, f[i].sent < total ? 0 : 20, &st);
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
    if (f.id < 0) {
        report("echo-1m", 0, "connect: error %d", glos_error());
        return;
    }
    r = echo_all(&f, 1, 1u << 20, 120000);
    ms = glos_ticks() - t0;
    report("echo-1m", r == 0 && !f.bad && f.got == 1u << 20, "r=%d sent=%u got=%u bad=%d ms=%u kbps=%u", r, f.sent,
           f.got, f.bad, ms, ms ? (unsigned)(2ull * f.got * 1000 / 1024 / ms) : 0);
    glos_net_close((unsigned)f.id, 0);
}

static void case_many(unsigned port)
{
    struct flow f[8];
    int i, r, ninth, err;
    memset(f, 0, sizeof f);
    for (i = 0; i < 8; i++) {
        f[i].tx_seed = f[i].rx_seed = 0x9E3779B9u * (unsigned)(i + 1);
        if ((f[i].id = glos_net_connect(host, port, 5000, 0)) < 0) {
            report("many", 0, "connect %d: error %d", i, glos_error());
            while (i--)
                glos_net_close((unsigned)f[i].id, 1);
            return;
        }
    }
    ninth = glos_net_connect(host, port, 5000, 0);
    err = glos_error();
    report("ninth-refused", ninth < 0 && err == GLOS_E_MEMORY, "r=%d error=%d", ninth, err);
    if (ninth >= 0)
        glos_net_close((unsigned)ninth, 1);
    r = echo_all(f, 8, 65536, 120000);
    for (i = 0; i < 8 && r == 0; i++)
        if (f[i].bad || f[i].got != 65536)
            r = 10 + i;
    report("many", r == 0, "r=%d", r);
    for (i = 0; i < 8; i++)
        glos_net_close((unsigned)f[i].id, 0);
}

static void case_closed(unsigned port)
{
    char buf[64];
    unsigned st = 0;
    int id = glos_net_connect(host, port, 5000, 0), r, n = 0;
    if (id < 0) {
        report("far-close", 0, "connect: error %d", glos_error());
        return;
    }
    glos_net_send((unsigned)id, "bye\n", 4, 1000);
    while ((r = glos_net_recv((unsigned)id, buf, sizeof buf, 5000, &st)) > 0)
        n += r;
    report("far-close", r == 0 && st == GLOS_NETS_CLOSED && n == 4, "r=%d state=%u echoed=%d", r, st, n);
    r = glos_net_send((unsigned)id, "x", 1, 0);    /* (half-closed: the app may still send) */
    glos_net_close((unsigned)id, 0);
    r = glos_net_recv((unsigned)id, buf, 1, 0, &st);
    report("closed-id", r < 0 && glos_error() == GLOS_E_ARGUMENT, "r=%d error=%d", r, glos_error());
}

static void case_events(unsigned port)
{
    struct glos_event ev;
    char buf[16];
    unsigned st;
    int id = glos_net_connect(host, port, 0, GLOS_NETF_EVENTS), opened = 0, data = 0, i, r;
    if (id < 0) {
        report("events", 0, "connect: error %d", glos_error());
        return;
    }
    for (i = 0; i < 50 && !opened; i++) {
        ev.size = sizeof ev;
        if (glos_event(&ev, 100) && ev.type == GLOS_EV_NET && ev.key == (unsigned)id && ev.x == GLOS_NETS_OPEN)
            opened = 1;
    }
    glos_net_send((unsigned)id, "ping\n", 5, 1000);
    for (i = 0; i < 50 && !data; i++) {
        ev.size = sizeof ev;
        if (glos_event(&ev, 100) && ev.type == GLOS_EV_NET && ev.key == (unsigned)id && ev.y > 0)
            data = ev.y;
    }
    r = glos_net_recv((unsigned)id, buf, sizeof buf, 1000, &st);
    report("events", opened && data > 0 && r == 5 && !memcmp(buf, "ping\n", 5), "opened=%d data=%d r=%d", opened,
           data, r);
    glos_net_close((unsigned)id, 0);
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
    r = glos_socket_id(fd);
    report("bsd-close", r == -1, "id=%d", r);

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

static int all(int argc, char **argv)
{
    unsigned port, closed, a, t0, ms;
    unsigned char r1[64], r2[64], big[300];
    int r, i, zero = 1;
    if (argc < 5)
        return 2;
    host = addr_of(argv[2]);
    port = (unsigned)atoi(argv[3]);
    closed = (unsigned)atoi(argv[4]);
    report("version", glos_version() >= 0x10003u && (glos_caps() & GLOS_CAP_NET), "version=%x caps=%x",
           glos_version(), glos_caps());
    r = glos_random(r1, sizeof r1) == 0 && glos_random(r2, sizeof r2) == 0 && glos_random(big, sizeof big) == 0;
    for (i = 0; i < 64; i++)
        zero &= !r1[i];
    report("random", r && !zero && memcmp(r1, r2, sizeof r1), "r=%d zero=%d", r, zero);

    {
        unsigned long utc;
        int off, how = glos_time(&utc, &off);
        printf("NETTEST-TIME how=%d utc=%lu offset=%d\n", how, utc, off);  /* (the host compares it with its own) */
        report("time", how >= 0, "how=%d", how);
    }
    r = glos_net_resolve(argv[2], 1000, &a);
    report("resolve-dotted", r == 0 && a == host, "r=%d addr=%08x", r, a);
    r = glos_net_resolve("no-such-name.invalid", 20000, &a);
    report("resolve-unknown", r < 0 && glos_error() == GLOS_E_NET_NAME, "r=%d error=%d", r, glos_error());
    if (argc > 5) {
        r = glos_net_resolve(argv[5], 20000, &a);
        report("resolve-internet", r == 0 && a, "%s: r=%d error=%d addr=%08x", argv[5], r, glos_error(), a);
    }
    r = glos_net_connect(host, closed, 5000, 0);
    report("refused", r < 0 && glos_error() == GLOS_E_NET_REFUSED, "r=%d error=%d", r, glos_error());
    t0 = glos_ticks();
    r = glos_net_connect(addr_of("10.255.255.1"), 80, 2000, 0);     /* (an address nobody answers for) */
    ms = glos_ticks() - t0;
    report("timeout", r < 0 && glos_error() == GLOS_E_NET_TIMEOUT && ms >= 1900 && ms < 4000, "r=%d error=%d ms=%u",
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
        if (glos_net_connect(host, (unsigned)atoi(argv[3]), 5000, 0) < 0) {
            printf("NETTEST hold FAIL connect %d: error %d\n", i, glos_error());
            return 1;
        }
    printf("NETTEST holding %d\n", n);
    fflush(stdout);
    for (;;)
        glos_sleep(1000);
}

int main(int argc, char **argv)
{
    if (glos_init() != 0) {
        printf("NETTEST no GLOS (error %d)\n", glos_error());
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
    printf("usage: NETTEST all HOST PORT CLOSED [NAME] | bsd HOST PORT CLOSED | hold HOST PORT N\n");
    return 2;
}
