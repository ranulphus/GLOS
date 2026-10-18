/* HTTP/1.1 GETs (include/glos/http.h; M10 step C). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/api.h>
#include <glos/http.h>
#include <glos/net.h>
#include <glos/tls.h>

#define REDIRECTS 5

struct url {
    int https;
    char host[256];
    unsigned port;
    char path[1024];
};

static int parse(const char *s, struct url *u)
{
    const char *h, *e, *p;
    size_t n;
    if (!strncmp(s, "https://", 8))
        u->https = 1, h = s + 8;
    else if (!strncmp(s, "http://", 7))
        u->https = 0, h = s + 7;
    else
        return -1;
    for (e = h; *e && *e != '/' && *e != ':' && *e != '?' && *e != '#'; e++) ;
    n = (size_t)(e - h);
    if (!n || n >= sizeof u->host)
        return -1;
    memcpy(u->host, h, n);
    u->host[n] = 0;
    u->port = u->https ? 443 : 80;
    if (*e == ':') {
        char *end;
        unsigned long v = strtoul(e + 1, &end, 10);
        if (end == e + 1 || !v || v > 65535)
            return -1;
        u->port = (unsigned)v;
        e = end;
    }
    p = *e == '/' || *e == '?' ? e : "/";
    for (n = 0; *p && *p != '#' && n < sizeof u->path - 1; p++)
        u->path[n++] = *p;
    if (*p && *p != '#')
        return -1;
    u->path[n] = 0;
    if (u->path[0] == '?') {                    /* (http://host?q: the path is "/?q") */
        memmove(u->path + 1, u->path, n + 1);
        u->path[0] = '/';
    }
    return 0;
}

/* A connection: the TLS's or the bare one's. */
struct conn {
    int id;
    struct tls *tls;
    unsigned timeout_ms;
    unsigned char buf[4096];
    size_t at, n;
    char why[128];
};

static int raw_read(struct conn *c, void *buf, size_t n)
{
    unsigned st;
    int r;
    if (c->tls) {
        r = tls_read(c->tls, buf, n);
        if (r < 0)
            snprintf(c->why, sizeof c->why, "%s", tls_why(c->tls));
        return r;
    }
    r = glos_net_recv((unsigned)c->id, buf, (unsigned)n, c->timeout_ms, &st);
    if (r < 0) {
        snprintf(c->why, sizeof c->why, "connection lost");
        return -1;
    }
    if (r == 0 && st != GLOS_NETS_CLOSED) {
        snprintf(c->why, sizeof c->why, "timed out");
        return -1;
    }
    return r;
}

static int fill(struct conn *c)
{
    int r;
    if (c->at < c->n)
        return 1;
    r = raw_read(c, c->buf, sizeof c->buf);
    if (r <= 0)
        return r;
    c->at = 0;
    c->n = (size_t)r;
    return 1;
}

/* A line (CR LF or LF, the end taken off): its length, or -1 (or the end before one). */
static int line(struct conn *c, char *out, size_t room)
{
    size_t n = 0;
    for (;;) {
        int r = fill(c);
        char ch;
        if (r <= 0) {
            if (!r)
                snprintf(c->why, sizeof c->why, "closed in the headers");
            return -1;
        }
        ch = (char)c->buf[c->at++];
        if (ch == '\n')
            break;
        if (n < room - 1)
            out[n++] = ch;
    }
    if (n && out[n - 1] == '\r')
        n--;
    out[n] = 0;
    return (int)n;
}

static int send_all(struct conn *c, const char *s, size_t n)
{
    if (c->tls)
        return tls_write(c->tls, s, n) == (int)n ? 0 : -1;
    while (n) {
        int r = glos_net_send((unsigned)c->id, s, (unsigned)n, c->timeout_ms);
        if (r <= 0)
            return -1;
        s += r, n -= (size_t)r;
    }
    return 0;
}

static int open_conn(struct http_get *g, const struct url *u, struct conn *c)
{
    unsigned addr;
    const char *at = g->resolve ? strchr(g->resolve, ':') : 0;
    struct in_addr ia;
    memset(c, 0, sizeof *c);
    c->timeout_ms = g->timeout_ms ? g->timeout_ms : 30000;
    if (at && (size_t)(at - g->resolve) == strlen(u->host) && !strncmp(g->resolve, u->host, strlen(u->host))
        && inet_aton(at + 1, &ia)) {
        addr = (unsigned)ia.s_addr;
    } else if (inet_aton(u->host, &ia)) {
        addr = (unsigned)ia.s_addr;
    } else if (glos_net_resolve(u->host, c->timeout_ms, &addr) != 0) {
        snprintf(g->why, sizeof g->why, glos_error() == GLOS_E_NET_NAME ? "no such name: %s"
                                        : glos_error() == GLOS_E_NET_DOWN ? "no network (%s)" : "name not found in time: %s",
                 u->host);
        return -1;
    }
    if ((c->id = glos_net_connect(addr, u->port, c->timeout_ms, 0)) < 0) {
        int e = glos_error();
        snprintf(g->why, sizeof g->why, "%s:%u: %s", u->host, u->port,
                 e == GLOS_E_NET_REFUSED ? "connection refused" : e == GLOS_E_NET_TIMEOUT ? "connection timed out"
                 : e == GLOS_E_NET_DOWN ? "no network" : "connection failed");
        return -1;
    }
    if (u->https) {
        char why[64];
        if (!g->anchors) {
            glos_net_close((unsigned)c->id, 1);
            snprintf(g->why, sizeof g->why, "no trust anchors");
            return -1;
        }
        if (!(c->tls = tls_open(c->id, u->host, g->anchors, c->timeout_ms, why, sizeof why))) {
            snprintf(g->why, sizeof g->why, "%s: %s", u->host, why);
            return -1;
        }
        snprintf(g->tls, sizeof g->tls, "%s", tls_version(c->tls));
    }
    return 0;
}

static void close_conn(struct conn *c)
{
    if (c->tls)
        tls_close(c->tls);
    else if (c->id > 0)
        glos_net_close((unsigned)c->id, 0);
    c->tls = 0;
    c->id = 0;
}

/* The body's bytes, n of them (or to the end if `to_end`): 0, or -1. */
static int body(struct http_get *g, struct conn *c, unsigned long n, int to_end, unsigned long total)
{
    while (to_end || n) {
        size_t k;
        int r = fill(c);
        if (r < 0) {
            snprintf(g->why, sizeof g->why, "%s", c->why);
            return -1;
        }
        if (!r) {
            if (to_end)
                return 0;
            snprintf(g->why, sizeof g->why, "closed with %lu bytes still to come", n);
            return -1;
        }
        k = c->n - c->at;
        if (!to_end && k > n)
            k = (size_t)n;
        if (g->body && g->body(g->arg, c->buf + c->at, (unsigned)k) != 0) {
            snprintf(g->why, sizeof g->why, "stopped");
            return -1;
        }
        c->at += k;
        g->length += k;
        if (!to_end)
            n -= k;
        if (g->progress)
            g->progress(g->arg, g->length, total);
    }
    return 0;
}

static int chunked(struct http_get *g, struct conn *c)
{
    char l[128];
    for (;;) {
        char *end;
        unsigned long n;
        if (line(c, l, sizeof l) < 0) {
            snprintf(g->why, sizeof g->why, "%s", c->why);
            return -1;
        }
        n = strtoul(l, &end, 16);
        if (end == l) {
            snprintf(g->why, sizeof g->why, "a bad chunk");
            return -1;
        }
        if (!n)
            break;
        if (body(g, c, n, 0, 0) != 0 || line(c, l, sizeof l) != 0) {
            if (!g->why[0])
                snprintf(g->why, sizeof g->why, "a bad chunk's end");
            return -1;
        }
    }
    while (line(c, l, sizeof l) > 0) ;          /* (the trailer) */
    return 0;
}

/* `ref`, the Location of a redirect from `base`, as a URL in out. */
static int resolve_ref(const struct url *base, const char *ref, char *out, size_t room)
{
    if (!strncmp(ref, "http://", 7) || !strncmp(ref, "https://", 8))
        return snprintf(out, room, "%s", ref) < (int)room ? 0 : -1;
    if (ref[0] == '/' && ref[1] == '/')
        return snprintf(out, room, "%s:%s", base->https ? "https" : "http", ref) < (int)room ? 0 : -1;
    if (ref[0] == '/')
        return snprintf(out, room, "%s://%s:%u%s", base->https ? "https" : "http", base->host, base->port, ref)
               < (int)room ? 0 : -1;
    {
        const char *slash = strrchr(base->path, '/');
        int dir = slash ? (int)(slash - base->path) + 1 : 1;
        return snprintf(out, room, "%s://%s:%u%.*s%s", base->https ? "https" : "http", base->host, base->port, dir,
                        base->path, ref) < (int)room ? 0 : -1;
    }
}

int http_get(struct http_get *g)
{
    static struct conn c;
    char url[512], req[1500], l[1024], location[512], port[8];
    struct url u;
    int hops, was_https = 0;
    snprintf(url, sizeof url, "%s", g->url);
    g->why[0] = g->tls[0] = 0;
    g->status = 0;
    for (hops = 0; hops <= REDIRECTS; hops++) {
        unsigned long length = 0;
        int have_length = 0, is_chunked = 0, r;
        if (parse(url, &u) != 0) {
            snprintf(g->why, sizeof g->why, "not an http: or https: URL: %s", url);
            return -1;
        }
        if (was_https && !u.https) {
            snprintf(g->why, sizeof g->why, "redirected from https to http: %s", url);
            return -1;
        }
        was_https |= u.https;
        if (open_conn(g, &u, &c) != 0)
            return -1;
        if ((u.https && u.port == 443) || (!u.https && u.port == 80))
            port[0] = 0;
        else
            snprintf(port, sizeof port, ":%u", u.port);
        snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s%s\r\nUser-Agent: GLOS-FETCH/1.0\r\n"
                 "Accept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", u.path, u.host, port);
        if (send_all(&c, req, strlen(req)) != 0) {
            snprintf(g->why, sizeof g->why, "%s", c.tls ? tls_why(c.tls) : "connection lost");
            close_conn(&c);
            return -1;
        }
        if (line(&c, l, sizeof l) < 0 || strncmp(l, "HTTP/1.", 7) || strlen(l) < 12) {
            snprintf(g->why, sizeof g->why, "%s", c.why[0] ? c.why : "not an HTTP response");
            close_conn(&c);
            return -1;
        }
        g->status = atoi(l + 9);
        location[0] = 0;
        while ((r = line(&c, l, sizeof l)) > 0) {
            char *v = strchr(l, ':');
            if (!v)
                continue;
            *v++ = 0;
            while (*v == ' ' || *v == '\t')
                v++;
            if (!strcasecmp(l, "Content-Length"))
                length = strtoul(v, 0, 10), have_length = 1;
            else if (!strcasecmp(l, "Transfer-Encoding") && strstr(v, "chunked"))
                is_chunked = 1;
            else if (!strcasecmp(l, "Location"))
                snprintf(location, sizeof location, "%s", v);
        }
        if (r < 0) {
            snprintf(g->why, sizeof g->why, "%s", c.why);
            close_conn(&c);
            return -1;
        }
        if ((g->status == 301 || g->status == 302 || g->status == 303 || g->status == 307 || g->status == 308)
            && location[0]) {
            close_conn(&c);
            if (resolve_ref(&u, location, url, sizeof url) != 0) {
                snprintf(g->why, sizeof g->why, "a redirect too long");
                return -1;
            }
            continue;
        }
        if (g->status < 200 || g->status > 299) {
            close_conn(&c);
            snprintf(g->why, sizeof g->why, "HTTP %d", g->status);
            return -1;
        }
        snprintf(g->url_got, sizeof g->url_got, "%s", url);
        g->length = 0;
        r = is_chunked ? chunked(g, &c) : body(g, &c, length, !have_length, have_length ? length : 0);
        close_conn(&c);
        return r;
    }
    snprintf(g->why, sizeof g->why, "more than %d redirects", REDIRECTS);
    return -1;
}
