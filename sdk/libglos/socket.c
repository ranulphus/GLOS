/* Sockets on GLOS_FN_NET (include/glos/net.h; M10 step B). A socket is a
 * DOS file descriptor (the NUL device's, from __FSEXT_alloc_fd) with this
 * file's function behind it, so DJGPP's read, write, close, fcntl and select
 * reach GLOS's connection. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <io.h>
#include <unistd.h>
#include <sys/fsext.h>

#include <glos/api.h>
#include <glos/net.h>

#undef fcntl                            /* (DJGPP's, below) */

#define CONNECT_MS 60000u               /* a blocking connect's time, unless SO_SNDTIMEO gives one */

struct sk {
    int id;                             /* GLOS's connection, 0 before connect() */
    int nonblock;
    int err;                            /* SO_ERROR: a non-blocking connect's failure */
    int nagle;                          /* TCP_NODELAY set to 0 before connect() */
    int rd_shut;
    unsigned rcv_ms, snd_ms;            /* 0xFFFFFFFF: for ever */
    struct sockaddr_in peer;
};

int h_errno;

static int fail(int e)
{
    errno = e;
    return -1;
}

/* GLOS's error as an errno value. */
static int err_of(int g)
{
    switch (g) {
    case 0:
        return ENOSYS;
    case GLOS_E_NET_DOWN:
        return ENETDOWN;
    case GLOS_E_NET_TIMEOUT:
        return ETIMEDOUT;
    case GLOS_E_NET_REFUSED:
        return ECONNREFUSED;
    case GLOS_E_NET_RESET:
        return ECONNRESET;
    case GLOS_E_MEMORY:
        return ENOBUFS;
    case GLOS_E_NOT_APP:
        return ENOSYS;
    default:
        return EINVAL;
    }
}

static struct sk *sk_of(int fd);

static int sk_recv(struct sk *k, void *buf, size_t n, int dontwait)
{
    unsigned st;
    int r;
    if (!k->id)
        return fail(ENOTCONN);
    if (k->rd_shut)
        return 0;
    r = glos_net_recv((unsigned)k->id, buf, (unsigned)n, k->nonblock || dontwait ? 0 : k->rcv_ms, &st);
    if (r < 0)
        return fail(err_of(glos_error()));
    if (r == 0 && n && st != GLOS_NETS_CLOSED)
        return fail(st == GLOS_NETS_CONNECTING ? ENOTCONN : EAGAIN);   /* (nothing in the time given) */
    return r;
}

static int sk_send(struct sk *k, const void *buf, size_t n, int dontwait)
{
    int r;
    if (!k->id)
        return fail(ENOTCONN);
    r = glos_net_send((unsigned)k->id, buf, (unsigned)n, k->nonblock || dontwait ? 0 : k->snd_ms);
    if (r < 0)
        return fail(err_of(glos_error()) == EINVAL ? ENOTCONN : err_of(glos_error()));
    if (r == 0 && n)
        return fail(EAGAIN);
    return r;
}

static int ready(struct sk *k)
{
    unsigned avail, st, room;
    int r = 0;
    if (!k->id)
        return __FSEXT_ready_write | __FSEXT_ready_error;
    if (glos_net_state((unsigned)k->id, &avail, &st, &room) != 0)
        return __FSEXT_ready_read | __FSEXT_ready_write | __FSEXT_ready_error;
    if (avail || st == GLOS_NETS_CLOSED || st == GLOS_NETS_FAILED || k->rd_shut)
        r |= __FSEXT_ready_read;
    if (st != GLOS_NETS_CONNECTING && (room || st == GLOS_NETS_FAILED))
        r |= __FSEXT_ready_write;
    if (st == GLOS_NETS_FAILED)
        r |= __FSEXT_ready_error;
    return r;
}

static int handler(__FSEXT_Fnumber fn, int *rv, va_list ap)
{
    int fd = va_arg(ap, int);
    struct sk *k = __FSEXT_get_data(fd);
    if (!k)
        return 0;
    switch (fn) {
    case __FSEXT_read: {
        void *buf = va_arg(ap, void *);
        size_t n = va_arg(ap, size_t);
        *rv = sk_recv(k, buf, n, 0);
        return 1;
    }
    case __FSEXT_write: {
        const void *buf = va_arg(ap, const void *);
        size_t n = va_arg(ap, size_t);
        *rv = sk_send(k, buf, n, 0);
        return 1;
    }
    case __FSEXT_ready:
        *rv = ready(k);
        return 1;
    case __FSEXT_close:
        if (k->id)
            glos_net_close((unsigned)k->id, 0);
        free(k);
        __FSEXT_set_data(fd, 0);
        __FSEXT_set_function(fd, 0);
        *rv = _close(fd);
        return 1;
    case __FSEXT_lseek:
    case __FSEXT_llseek:
        *rv = fail(ESPIPE);
        return 1;
    case __FSEXT_dup:
    case __FSEXT_dup2:
        *rv = fail(EOPNOTSUPP);
        return 1;
    default:
        return 0;
    }
}

static struct sk *sk_of(int fd)
{
    return __FSEXT_get_function(fd) == handler ? __FSEXT_get_data(fd) : 0;
}

int glos_fcntl(int fd, int cmd, ...)
{
    struct sk *k = sk_of(fd);
    va_list ap;
    long arg;
    va_start(ap, cmd);
    arg = va_arg(ap, long);                     /* (an int or a pointer: one word either way) */
    va_end(ap);
    if (k && cmd == F_GETFL)
        return O_RDWR | (k->nonblock ? O_NONBLOCK : 0);
    if (k && cmd == F_SETFL) {
        k->nonblock = (arg & O_NONBLOCK) != 0;
        return 0;
    }
    return fcntl(fd, cmd, arg);
}

int glos_socket_id(int fd)
{
    struct sk *k = sk_of(fd);
    return k && k->id ? k->id : -1;
}

int socket(int domain, int type, int protocol)
{
    struct sk *k;
    int fd;
    if (domain != AF_INET)
        return fail(EAFNOSUPPORT);
    if (type != SOCK_STREAM || (protocol && protocol != IPPROTO_TCP))
        return fail(EPROTONOSUPPORT);
    if (!(glos_caps() & GLOS_CAP_NET) || !glos_id())
        return fail(glos_id() ? ENETDOWN : ENOSYS);
    if (!(k = calloc(1, sizeof *k)))
        return fail(ENOMEM);
    k->rcv_ms = k->snd_ms = 0xFFFFFFFFu;
    if ((fd = __FSEXT_alloc_fd(handler)) < 0) {
        free(k);
        return -1;
    }
    __FSEXT_set_data(fd, k);
    return fd;
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    struct sk *k = sk_of(fd);
    const struct sockaddr_in *in = (const struct sockaddr_in *)addr;
    unsigned ip, wait;
    int id;
    if (!k)
        return fail(ENOTSOCK);
    if (k->id)
        return fail(EISCONN);
    if (!addr || len < sizeof *in)
        return fail(EINVAL);
    if (in->sin_family != AF_INET)
        return fail(EAFNOSUPPORT);
    ip = (unsigned)in->sin_addr.s_addr;         /* (network order: as GLOS keeps an address) */
    wait = k->nonblock ? 0 : k->snd_ms != 0xFFFFFFFFu ? k->snd_ms : CONNECT_MS;
    id = glos_net_connect(ip, ntohs(in->sin_port), wait, k->nagle ? GLOS_NETF_NAGLE : 0);
    if (id < 0)
        return fail(err_of(glos_error()));
    k->id = id;
    k->peer = *in;
    if (k->nonblock) {
        unsigned st;
        glos_net_state((unsigned)id, 0, &st, 0);
        if (st == GLOS_NETS_CONNECTING)
            return fail(EINPROGRESS);
    }
    return 0;
}

ssize_t send(int fd, const void *buf, size_t n, int flags)
{
    struct sk *k = sk_of(fd);
    return k ? sk_send(k, buf, n, (flags & MSG_DONTWAIT) != 0) : fail(ENOTSOCK);
}

ssize_t recv(int fd, void *buf, size_t n, int flags)
{
    struct sk *k = sk_of(fd);
    if (!k)
        return fail(ENOTSOCK);
    if (flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL))
        return fail(EOPNOTSUPP);                /* (MSG_PEEK, MSG_WAITALL: not here) */
    return sk_recv(k, buf, n, (flags & MSG_DONTWAIT) != 0);
}

int shutdown(int fd, int how)
{
    struct sk *k = sk_of(fd);
    if (!k)
        return fail(ENOTSOCK);
    if (!k->id)
        return fail(ENOTCONN);
    if (how != SHUT_RD)
        return fail(EOPNOTSUPP);                /* (a half-close isn't in GLOS_FN_NET: close() instead) */
    k->rd_shut = 1;
    return 0;
}

static unsigned ms_of(const void *val, socklen_t len)
{
    const struct timeval *tv = val;
    unsigned long ms;
    if (len < sizeof *tv)
        return 0xFFFFFFFFu;
    ms = (unsigned long)tv->tv_sec * 1000 + (unsigned long)tv->tv_usec / 1000;
    return ms ? (unsigned)ms : 0xFFFFFFFFu;     /* (0: for ever, as BSD has it) */
}

int setsockopt(int fd, int level, int name, const void *val, socklen_t len)
{
    struct sk *k = sk_of(fd);
    if (!k)
        return fail(ENOTSOCK);
    if (level == SOL_SOCKET && name == SO_RCVTIMEO)
        k->rcv_ms = ms_of(val, len);
    else if (level == SOL_SOCKET && name == SO_SNDTIMEO)
        k->snd_ms = ms_of(val, len);
    else if (level == SOL_SOCKET && (name == SO_KEEPALIVE || name == SO_REUSEADDR))
        ;
    else if (level == IPPROTO_TCP && name == TCP_NODELAY && len >= sizeof(int))
        k->nagle = !*(const int *)val;          /* (before connect(): GLOS sets it with the connection) */
    else
        return fail(EINVAL);
    return 0;
}

int getsockopt(int fd, int level, int name, void *val, socklen_t *len)
{
    struct sk *k = sk_of(fd);
    int v;
    if (!k)
        return fail(ENOTSOCK);
    if (!val || !len || *len < sizeof(int))
        return fail(EINVAL);
    if (level == SOL_SOCKET && name == SO_TYPE) {
        v = SOCK_STREAM;
    } else if (level == SOL_SOCKET && name == SO_ERROR) {
        unsigned st;
        v = k->err;
        if (!v && k->id && glos_net_state((unsigned)k->id, 0, &st, 0) == 0 && st == GLOS_NETS_FAILED) {
            char c;
            if (glos_net_recv((unsigned)k->id, &c, 0, 0, 0) < 0)  /* (the reason, from an empty read) */
                v = err_of(glos_error());
        }
        k->err = 0;
    } else {
        return fail(EINVAL);
    }
    *(int *)val = v;
    *len = sizeof(int);
    return 0;
}

int getpeername(int fd, struct sockaddr *addr, socklen_t *len)
{
    struct sk *k = sk_of(fd);
    if (!k)
        return fail(ENOTSOCK);
    if (!k->id)
        return fail(ENOTCONN);
    if (!addr || !len)
        return fail(EINVAL);
    memcpy(addr, &k->peer, *len < sizeof k->peer ? *len : sizeof k->peer);
    *len = sizeof k->peer;
    return 0;
}

/* ---- addresses */

int inet_aton(const char *s, struct in_addr *a)
{
    unsigned long part[4];
    int n = 0;
    for (;;) {
        char *e;
        if (*s < '0' || *s > '9')
            return 0;
        part[n] = strtoul(s, &e, 10);
        if (part[n] > 255 || e == s)
            return 0;
        s = e;
        n++;
        if (!*s)
            break;
        if (*s != '.' || n == 4)
            return 0;
        s++;
    }
    if (n != 4)
        return 0;
    if (a)
        a->s_addr = htonl(part[0] << 24 | part[1] << 16 | part[2] << 8 | part[3]);
    return 1;
}

in_addr_t inet_addr(const char *s)
{
    struct in_addr a;
    return inet_aton(s, &a) ? a.s_addr : INADDR_NONE;
}

char *inet_ntoa(struct in_addr a)
{
    static char buf[INET_ADDRSTRLEN];
    unsigned long v = ntohl(a.s_addr);
    sprintf(buf, "%lu.%lu.%lu.%lu", v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255);
    return buf;
}

int inet_pton(int af, const char *s, void *dst)
{
    if (af != AF_INET)
        return fail(EAFNOSUPPORT);
    return inet_aton(s, dst);
}

const char *inet_ntop(int af, const void *src, char *dst, socklen_t size)
{
    const char *t;
    if (af != AF_INET) {
        errno = EAFNOSUPPORT;
        return 0;
    }
    t = inet_ntoa(*(const struct in_addr *)src);
    if (strlen(t) >= size) {
        errno = ENOBUFS;
        return 0;
    }
    return strcpy(dst, t);
}

/* ---- names */

#define LOOKUP_MS 10000u

static int lookup(const char *name, in_addr_t *a)
{
    struct in_addr ia;
    unsigned ip;
    if (inet_aton(name, &ia)) {
        *a = ia.s_addr;
        return 0;
    }
    if (glos_net_resolve(name, LOOKUP_MS, &ip) != 0)
        return glos_error();
    *a = (in_addr_t)ip;
    return 0;
}

static int port_of(const char *service, unsigned *port)
{
    static const struct { const char *name; unsigned port; } known[] = {
        { "ftp", 21 }, { "ssh", 22 }, { "telnet", 23 }, { "smtp", 25 }, { "domain", 53 }, { "http", 80 },
        { "pop3", 110 }, { "ntp", 123 }, { "imap", 143 }, { "https", 443 }, { "imaps", 993 }, { 0, 0 } };
    char *e;
    int i;
    unsigned long v;
    if (!service) {
        *port = 0;
        return 0;
    }
    v = strtoul(service, &e, 10);
    if (*service && !*e && v <= 65535) {
        *port = (unsigned)v;
        return 0;
    }
    for (i = 0; known[i].name; i++)
        if (!strcmp(known[i].name, service)) {
            *port = known[i].port;
            return 0;
        }
    return -1;
}

int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res)
{
    struct addrinfo *ai;
    struct sockaddr_in *in;
    in_addr_t a = htonl(INADDR_LOOPBACK);
    unsigned port;
    int flags = hints ? hints->ai_flags : 0, g;
    if (!node && !service)
        return EAI_NONAME;
    if (hints && hints->ai_family != AF_UNSPEC && hints->ai_family != AF_INET)
        return EAI_FAMILY;
    if (hints && hints->ai_socktype && hints->ai_socktype != SOCK_STREAM)
        return EAI_SOCKTYPE;
    if (port_of(service, &port) != 0 || ((flags & AI_NUMERICSERV) && service && !*service))
        return EAI_SERVICE;
    if (!node) {
        a = flags & AI_PASSIVE ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    } else if (flags & AI_NUMERICHOST) {
        struct in_addr ia;
        if (!inet_aton(node, &ia))
            return EAI_NONAME;
        a = ia.s_addr;
    } else if ((g = lookup(node, &a)) != 0) {
        return g == GLOS_E_NET_NAME ? EAI_NONAME : g == GLOS_E_NET_TIMEOUT ? EAI_AGAIN : EAI_FAIL;
    }
    if (!(ai = calloc(1, sizeof *ai + sizeof *in)))
        return EAI_MEMORY;
    in = (struct sockaddr_in *)(ai + 1);
    in->sin_family = AF_INET;
    in->sin_port = htons((unsigned short)port);
    in->sin_addr.s_addr = a;
    ai->ai_family = AF_INET;
    ai->ai_socktype = SOCK_STREAM;
    ai->ai_protocol = IPPROTO_TCP;
    ai->ai_addrlen = sizeof *in;
    ai->ai_addr = (struct sockaddr *)in;
    if ((flags & AI_CANONNAME) && node)
        ai->ai_canonname = strdup(node);
    *res = ai;
    return 0;
}

void freeaddrinfo(struct addrinfo *res)
{
    while (res) {
        struct addrinfo *next = res->ai_next;
        free(res->ai_canonname);
        free(res);
        res = next;
    }
}

const char *gai_strerror(int e)
{
    switch (e) {
    case 0:
        return "Success";
    case EAI_AGAIN:
        return "No answer from the name server";
    case EAI_FAIL:
        return "The network is down";
    case EAI_FAMILY:
        return "Address family not supported";
    case EAI_MEMORY:
        return "Out of memory";
    case EAI_NONAME:
        return "No such name";
    case EAI_SERVICE:
        return "Unknown service";
    case EAI_SOCKTYPE:
        return "Socket type not supported";
    default:
        return "Unknown error";
    }
}

struct hostent *gethostbyname(const char *name)
{
    static struct hostent h;
    static char hname[128];
    static in_addr_t addr;
    static char *list[2];
    int g;
    if ((g = lookup(name, &addr)) != 0) {
        h_errno = g == GLOS_E_NET_NAME ? HOST_NOT_FOUND : g == GLOS_E_NET_TIMEOUT ? TRY_AGAIN : NO_RECOVERY;
        return 0;
    }
    strncpy(hname, name, sizeof hname - 1);
    list[0] = (char *)&addr;
    list[1] = 0;
    h.h_name = hname;
    h.h_aliases = list + 1;
    h.h_addrtype = AF_INET;
    h.h_length = 4;
    h.h_addr_list = list;
    return &h;
}

const char *glos_net_strerror(int e)
{
    switch (e) {
    case ENETDOWN:
        return "Network is down";
    case ETIMEDOUT:
        return "Connection timed out";
    case ECONNREFUSED:
        return "Connection refused";
    case ECONNRESET:
        return "Connection reset by peer";
    case ENOTCONN:
        return "Socket is not connected";
    case EINPROGRESS:
        return "Operation now in progress";
    case EISCONN:
        return "Socket is already connected";
    case ENOTSOCK:
        return "Not a socket";
    case EAFNOSUPPORT:
        return "Address family not supported";
    case EPROTONOSUPPORT:
        return "Protocol not supported";
    case EOPNOTSUPP:
        return "Operation not supported";
    case ENOBUFS:
        return "No buffer space available";
    default:
        return strerror(e);
    }
}
