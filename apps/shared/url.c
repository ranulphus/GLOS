/* URLs (url.h), from RFC 3986: scheme ":" ["//" host [":" port]] path ["?" query] ["#" fragment]. */
#include <stdio.h>
#include <string.h>

#include "url.h"

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* The scheme's length if s begins with one ("http:" 4), else 0: a letter, then letters, digits, + - . */
static unsigned scheme_len(const char *s)
{
    unsigned n = 0;
    if (!((lower((unsigned char)s[0]) >= 'a' && lower((unsigned char)s[0]) <= 'z')))
        return 0;
    while ((lower((unsigned char)s[n]) >= 'a' && lower((unsigned char)s[n]) <= 'z') || (s[n] >= '0' && s[n] <= '9') || s[n] == '+' || s[n] == '-'
           || s[n] == '.')
        n++;
    return s[n] == ':' ? n : 0;
}

static int take(char *to, unsigned room, const char *from, size_t n, int small)
{
    size_t i;
    if (n >= room)
        return -1;
    for (i = 0; i < n; i++)
        to[i] = (char)(small ? lower((unsigned char)from[i]) : from[i]);
    to[n] = 0;
    return 0;
}

int url_split(const char *url, struct url_parts *p)
{
    unsigned n = scheme_len(url);
    const char *s = url, *end;
    memset(p, 0, sizeof *p);
    if (n) {
        if (take(p->scheme, sizeof p->scheme, s, n, 1) != 0)
            return -1;
        s += n + 1;
    }
    if (s[0] == '/' && s[1] == '/') {           /* an authority: [user@]host[:port] */
        const char *at, *colon, *host;
        s += 2;
        end = s + strcspn(s, "/?#");
        host = s;
        for (at = s; at < end; at++)
            if (*at == '@')
                host = at + 1;
        colon = NULL;
        for (at = host; at < end; at++) {
            if (*at == ']')                     /* ([an IPv6 address]: its colons are no port's) */
                colon = NULL;
            else if (*at == ':')
                colon = at;
        }
        if (colon) {
            unsigned long port = 0;
            for (at = colon + 1; at < end && *at >= '0' && *at <= '9' && port < 100000; at++)
                port = port * 10 + (unsigned long)(*at - '0');
            p->port = at == end && port < 65536 ? (unsigned)port : 0;
        }
        if (take(p->host, sizeof p->host, host, (size_t)((colon ? colon : end) - host), 1) != 0)
            return -1;
        p->has_host = 1;
        s = end;
    }
    end = s + strcspn(s, "?#");
    if (take(p->path, sizeof p->path, s, (size_t)(end - s), 0) != 0)
        return -1;
    s = end;
    if (*s == '?') {
        end = s + 1 + strcspn(s + 1, "#");
        if (take(p->query, sizeof p->query, s + 1, (size_t)(end - s - 1), 0) != 0)
            return -1;
        p->has_query = 1;
        s = end;
    }
    if (*s == '#' && take(p->fragment, sizeof p->fragment, s + 1, strlen(s + 1), 0) != 0)
        return -1;
    return 0;
}

unsigned url_port(const struct url_parts *p)
{
    if (p->port)
        return p->port;
    return !strcmp(p->scheme, "http") ? 80 : !strcmp(p->scheme, "https") ? 443 : !strcmp(p->scheme, "gemini") ? 1965
           : !strcmp(p->scheme, "gopher") ? 70 : 0;
}

/* A path's "." and ".." parts taken out, where it lies (RFC 3986 5.2.4). */
static void undot(char *path)
{
    char *in = path, *out = path;
    while (*in) {
        if (!strncmp(in, "../", 3)) {
            in += 3;
        } else if (!strncmp(in, "./", 2)) {
            in += 2;
        } else if (!strncmp(in, "/./", 3)) {
            in += 2;
        } else if (!strcmp(in, "/.")) {
            in[1] = 0;                          /* ("/." at the end: "/") */
        } else if (!strncmp(in, "/../", 4) || !strcmp(in, "/..")) {
            if (in[3])
                in += 3;
            else
                in[1] = 0;                      /* ("/.." at the end: "/") */
            while (out > path && *--out != '/')         /* the part before it goes */
                ;
        } else if (!strcmp(in, ".") || !strcmp(in, "..")) {
            in += strlen(in);
        } else {                                /* a part moved as it is: up to, not with, the next slash */
            do
                *out++ = *in++;
            while (*in && *in != '/');
        }
    }
    *out = 0;
}

static int safe(int c)                  /* a character a URL may have as it is */
{
    return c > 0x20 && c < 0x7F && c != '"' && c != '<' && c != '>' && c != '\\' && c != '^' && c != '`' && c != '{' && c != '|' && c != '}';
}

int url_resolve(const char *base, const char *ref, char *out, int room)
{
    struct url_parts b, r, t;
    char clean[1400], path[1100];
    size_t n = 0, len;
    int k;
    while (*ref == ' ' || *ref == '\t' || *ref == '\n' || *ref == '\r')
        ref++;
    len = strlen(ref);
    while (len && (ref[len - 1] == ' ' || ref[len - 1] == '\t' || ref[len - 1] == '\n' || ref[len - 1] == '\r'))
        len--;
    for (; len; len--, ref++) {                 /* what a URL cannot have: as escapes; ends of lines within it: gone */
        unsigned char c = (unsigned char)*ref;
        if (c == '\n' || c == '\r' || c == '\t')
            continue;
        if (n + 4 >= sizeof clean)
            return -1;
        if (safe(c))
            clean[n++] = (char)c;
        else
            n += (size_t)sprintf(clean + n, "%%%02X", c);
    }
    clean[n] = 0;
    if (url_split(base, &b) != 0 || url_split(clean, &r) != 0)
        return -1;
    memset(&t, 0, sizeof t);
    if (r.scheme[0]) {                          /* whole already */
        t = r;
        undot(t.path);
    } else {
        if (!b.scheme[0])
            return -1;
        memcpy(t.scheme, b.scheme, sizeof t.scheme);
        if (r.has_host) {
            memcpy(t.host, r.host, sizeof t.host), t.port = r.port, t.has_host = 1;
            memcpy(t.path, r.path, sizeof t.path);
            undot(t.path);
            memcpy(t.query, r.query, sizeof t.query), t.has_query = r.has_query;
        } else {
            memcpy(t.host, b.host, sizeof t.host), t.port = b.port, t.has_host = b.has_host;
            if (!r.path[0]) {
                memcpy(t.path, b.path, sizeof t.path);
                if (r.has_query)
                    memcpy(t.query, r.query, sizeof t.query), t.has_query = 1;
                else
                    memcpy(t.query, b.query, sizeof t.query), t.has_query = b.has_query;
            } else {
                if (r.path[0] == '/') {
                    snprintf(path, sizeof path, "%s", r.path);
                } else if (b.has_host && !b.path[0]) {  /* (merged: the base's path up to its last slash, then the ref's) */
                    snprintf(path, sizeof path, "/%s", r.path);
                } else {
                    const char *slash = strrchr(b.path, '/');
                    snprintf(path, sizeof path, "%.*s%s", slash ? (int)(slash - b.path + 1) : 0, b.path, r.path);
                }
                undot(path);
                if (strlen(path) >= sizeof t.path)
                    return -1;
                strcpy(t.path, path);
                memcpy(t.query, r.query, sizeof t.query), t.has_query = r.has_query;
            }
        }
    }
    memcpy(t.fragment, r.fragment, sizeof t.fragment);
    k = snprintf(out, (size_t)room, "%s:%s%s", t.scheme, t.has_host ? "//" : "", t.host);
    if (k > 0 && k < room && t.port)
        k += snprintf(out + k, (size_t)(room - k), ":%u", t.port);
    if (k > 0 && k < room)                      /* (a host with no path is asked for "/") */
        k += snprintf(out + k, (size_t)(room - k), "%s%s%s%s%s", t.has_host && !t.path[0] && (!strcmp(t.scheme, "http")
                      || !strcmp(t.scheme, "https")) ? "/" : t.path, t.has_query ? "?" : "", t.query, t.fragment[0] ? "#" : "", t.fragment);
    return k > 0 && k < room ? 0 : -1;
}

void url_escape(const char *text, char *out, int room)
{
    int n = 0;
    for (; *text && n + 4 < room; text++) {
        unsigned char c = (unsigned char)*text;
        if ((c >= '0' && c <= '9') || (lower(c) >= 'a' && lower(c) <= 'z') || c == '-' || c == '_' || c == '.' || c == '~')
            out[n++] = (char)c;
        else if (c == ' ')
            out[n++] = '+';
        else
            n += sprintf(out + n, "%%%02X", c);
    }
    if (room > 0)
        out[n] = 0;
}
