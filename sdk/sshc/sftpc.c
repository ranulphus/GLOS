/* The SFTP client (glos/sftpc.h). Packets are uint32 length, byte type,
 * uint32 id, and the rest; answers are matched to requests by id. */
#include <stdlib.h>
#include <string.h>

#include "sshbuf.h"
#include <glos/sftpc.h>
#include <glos/sshc.h>

enum {
    FX_INIT = 1, FX_VERSION = 2, FX_OPEN = 3, FX_CLOSE = 4, FX_READ = 5, FX_WRITE = 6, FX_LSTAT = 7, FX_FSTAT = 8,
    FX_OPENDIR = 11, FX_READDIR = 12, FX_REMOVE = 13, FX_MKDIR = 14, FX_RMDIR = 15, FX_REALPATH = 16, FX_STAT = 17,
    FX_RENAME = 18,
    FX_STATUS = 101, FX_HANDLE = 102, FX_DATA = 103, FX_NAME = 104, FX_ATTRS = 105,
};
enum { FXF_READ = 1, FXF_WRITE = 2, FXF_CREAT = 8, FXF_TRUNC = 0x10 };
enum { FX_OK = 0, FX_EOF = 1, FX_NO_SUCH_FILE = 2, FX_PERMISSION_DENIED = 3, FX_FAILURE = 4 };
enum { A_SIZE = 1, A_UIDGID = 2, A_PERM = 4, A_TIME = 8, A_EXT = 0x80000000u };

#define CHUNK 32768u
#define INFLIGHT 8
#define WAIT_MS 30000u
#define RXMAX (CHUNK + 1024)

struct reply {
    int got;
    uint8_t type;
    uint8_t *p;                                 /* the packet after its id */
    uint32_t n;
};

struct sftpc {
    struct sshc_chan *ch;
    int (*pump)(void *arg, unsigned ms);
    void *arg;
    int version, dead;
    uint32_t next_id;
    uint8_t *rx;
    uint32_t rx_len;
    struct reply r[INFLIGHT + 2];
    uint32_t rid[INFLIGHT + 2];                 /* the ids waited for, in r[] */
    char why[128];
};

static void fail(struct sftpc *s, const char *why)
{
    if (!s->why[0])
        snprintf(s->why, sizeof s->why, "%s", why);
}

struct sftpc *sftpc_new(struct sshc_chan *ch, int (*pump)(void *arg, unsigned ms), void *arg)
{
    struct sftpc *s = calloc(1, sizeof *s);
    uint8_t m[9];
    if (!s || !(s->rx = malloc(RXMAX + 16))) {
        free(s);
        return NULL;
    }
    s->ch = ch;
    s->pump = pump;
    s->arg = arg;
    s->next_id = 1;
    put_be32(m, 5);
    m[4] = FX_INIT;
    put_be32(m + 5, 3);
    sshc_write(ch, m, 9);
    return s;
}

void sftpc_free(struct sftpc *s)
{
    int i;
    if (!s)
        return;
    for (i = 0; i < INFLIGHT + 2; i++)
        free(s->r[i].p);
    free(s->rx);
    free(s);
}

const char *sftpc_why(const struct sftpc *s) { return s->why; }

/* A whole packet: to the slot waiting for its id (VERSION has none). */
static void deliver(struct sftpc *s, const uint8_t *p, uint32_t n)
{
    uint32_t id;
    int i;
    if (p[0] == FX_VERSION) {
        s->version = n >= 5 ? (int)be32(p + 1) : 3;
        return;
    }
    if (n < 5)
        return;
    id = be32(p + 1);
    for (i = 0; i < INFLIGHT + 2; i++)
        if (s->rid[i] == id && !s->r[i].got) {
            free(s->r[i].p);
            s->r[i].p = malloc(n - 5 + 1);
            if (!s->r[i].p) {
                fail(s, "out of memory");
                return;
            }
            memcpy(s->r[i].p, p + 5, n - 5);
            s->r[i].n = n - 5;
            s->r[i].type = p[0];
            s->r[i].got = 1;
            return;
        }
}

void sftpc_input(struct sftpc *s, const uint8_t *data, uint32_t len)
{
    while (len && !s->dead) {
        uint32_t want = s->rx_len < 4 ? 4 - s->rx_len : 4 + be32(s->rx) - s->rx_len, k;
        if (s->rx_len >= 4 && be32(s->rx) > RXMAX) {
            s->dead = 1;
            fail(s, "a packet too big");
            return;
        }
        k = len < want ? len : want;
        memcpy(s->rx + s->rx_len, data, k);
        s->rx_len += k;
        data += k;
        len -= k;
        if (s->rx_len >= 4 && s->rx_len == 4 + be32(s->rx)) {
            if (be32(s->rx))
                deliver(s, s->rx + 4, be32(s->rx));
            s->rx_len = 0;
        }
    }
}

/* ---- requests */

static int slot_for(struct sftpc *s, uint32_t id)
{
    int i;
    for (i = 0; i < INFLIGHT + 2; i++)
        if (!s->rid[i]) {
            s->rid[i] = id;
            s->r[i].got = 0;
            return i;
        }
    return -1;
}

/* A request: type, then b's bytes (after the id). Its slot, or -1. */
static int request(struct sftpc *s, uint8_t type, const struct sbuf *b)
{
    uint8_t head[9];
    uint32_t id = s->next_id++;
    int k;
    if (s->dead || b->err || (k = slot_for(s, id)) < 0) {
        fail(s, b->err ? "a request too long" : "too many requests");
        return -1;
    }
    put_be32(head, 1 + 4 + b->len);
    head[4] = type;
    put_be32(head + 5, id);
    sshc_write(s->ch, head, 9);
    sshc_write(s->ch, b->p, b->len);
    return k;
}

/* Its answer: 0 (in s->r[k], the slot freed for the next), or -1. */
static int wait_for(struct sftpc *s, int k)
{
    unsigned waited = 0;
    while (!s->r[k].got && !s->dead && waited < WAIT_MS) {
        if (s->pump(s->arg, 100) != 0) {
            s->dead = 1;
            fail(s, "the connection is gone");
        }
        waited += 100;                          /* (an upper bound: the pump may return sooner) */
    }
    s->rid[k] = 0;
    if (!s->r[k].got) {
        fail(s, s->dead ? "the connection is gone" : "no answer from the server");
        return -1;
    }
    return 0;
}

int sftpc_start(struct sftpc *s, unsigned ms)
{
    unsigned waited = 0;
    while (!s->version && !s->dead && waited < ms) {
        if (s->pump(s->arg, 100) != 0)
            s->dead = 1;
        waited += 100;
    }
    if (!s->version)
        fail(s, "the server has no SFTP");
    return s->version ? 0 : -1;
}

/* A STATUS answer as 0, or -1 with its message. */
static int status_of(struct sftpc *s, struct reply *r, int eof_ok)
{
    struct sread rd;
    const uint8_t *msg;
    uint32_t code, len;
    if (r->type != FX_STATUS) {
        fail(s, "an unexpected answer");
        return -1;
    }
    sr_init(&rd, r->p, r->n);
    code = sr_u32(&rd);
    msg = sr_string(&rd, &len);
    if (code == FX_OK || (eof_ok && code == FX_EOF))
        return 0;
    if (!s->why[0]) {
        if (rd.err || !len)
            snprintf(s->why, sizeof s->why, "%s", code == FX_NO_SUCH_FILE ? "no such file"
                     : code == FX_PERMISSION_DENIED ? "permission denied" : code == FX_EOF ? "end of file" : "failed");
        else
            snprintf(s->why, sizeof s->why, "%.*s", (int)len, (const char *)msg);
    }
    return -1;
}

static int simple(struct sftpc *s, uint8_t type, const char *a, const char *b2, int attrs)
{
    uint8_t m[1100];
    struct sbuf b;
    int k;
    s->why[0] = 0;
    sb_init(&b, m, sizeof m);
    sb_cstr(&b, a);
    if (b2)
        sb_cstr(&b, b2);
    if (attrs)
        sb_u32(&b, 0);
    if ((k = request(s, type, &b)) < 0 || wait_for(s, k) != 0)
        return -1;
    return status_of(s, &s->r[k], 0);
}

int sftpc_remove(struct sftpc *s, const char *path) { return simple(s, FX_REMOVE, path, NULL, 0); }
int sftpc_mkdir(struct sftpc *s, const char *path) { return simple(s, FX_MKDIR, path, NULL, 1); }
int sftpc_rmdir(struct sftpc *s, const char *path) { return simple(s, FX_RMDIR, path, NULL, 0); }
int sftpc_rename(struct sftpc *s, const char *from, const char *to) { return simple(s, FX_RENAME, from, to, 0); }

static void read_attrs(struct sread *r, struct sftpc_attrs *a)
{
    uint32_t flags = sr_u32(r), n, i, len;
    memset(a, 0, sizeof *a);
    if (flags & A_SIZE) {
        uint32_t hi = sr_u32(r), lo = sr_u32(r);
        a->size = (uint64_t)hi << 32 | lo;
        a->has_size = 1;
    }
    if (flags & A_UIDGID)
        sr_u32(r), sr_u32(r);
    if (flags & A_PERM)
        a->perm = sr_u32(r), a->has_perm = 1;
    if (flags & A_TIME)
        sr_u32(r), a->mtime = sr_u32(r), a->has_time = 1;
    if (flags & A_EXT)
        for (n = sr_u32(r), i = 0; i < n && !r->err; i++)
            sr_string(r, &len), sr_string(r, &len);
}

int sftpc_stat(struct sftpc *s, const char *path, struct sftpc_attrs *a)
{
    uint8_t m[1100];
    struct sbuf b;
    struct sread r;
    int k;
    s->why[0] = 0;
    sb_init(&b, m, sizeof m);
    sb_cstr(&b, path);
    if ((k = request(s, FX_STAT, &b)) < 0 || wait_for(s, k) != 0)
        return -1;
    if (s->r[k].type != FX_ATTRS)
        return status_of(s, &s->r[k], 0) == 0 ? -1 : -1;
    sr_init(&r, s->r[k].p, s->r[k].n);
    read_attrs(&r, a);
    return r.err ? -1 : 0;
}

int sftpc_realpath(struct sftpc *s, const char *path, char *out, size_t room)
{
    uint8_t m[1100];
    struct sbuf b;
    struct sread r;
    const uint8_t *name;
    uint32_t len;
    int k;
    s->why[0] = 0;
    sb_init(&b, m, sizeof m);
    sb_cstr(&b, path);
    if ((k = request(s, FX_REALPATH, &b)) < 0 || wait_for(s, k) != 0)
        return -1;
    if (s->r[k].type != FX_NAME)
        return status_of(s, &s->r[k], 0), -1;
    sr_init(&r, s->r[k].p, s->r[k].n);
    if (sr_u32(&r) < 1 || !(name = sr_string(&r, &len)) || r.err || len >= room) {
        fail(s, "a bad answer");
        return -1;
    }
    memcpy(out, name, len);
    out[len] = 0;
    return 0;
}

/* A handle from OPEN or OPENDIR into h (its length back), or -1. */
static int get_handle(struct sftpc *s, int k, uint8_t *h, uint32_t *hlen)
{
    struct sread r;
    const uint8_t *p;
    if (s->r[k].type != FX_HANDLE)
        return status_of(s, &s->r[k], 0), -1;
    sr_init(&r, s->r[k].p, s->r[k].n);
    p = sr_string(&r, hlen);
    if (r.err || *hlen > 256) {
        fail(s, "a bad handle");
        return -1;
    }
    memcpy(h, p, *hlen);
    return 0;
}

static int close_handle(struct sftpc *s, const uint8_t *h, uint32_t hlen)
{
    uint8_t m[300];
    struct sbuf b;
    int k;
    sb_init(&b, m, sizeof m);
    sb_string(&b, h, hlen);
    if ((k = request(s, FX_CLOSE, &b)) < 0 || wait_for(s, k) != 0)
        return -1;
    return status_of(s, &s->r[k], 0);
}

int sftpc_ls(struct sftpc *s, const char *path, int (*f)(void *arg, const char *name, const char *longname,
                                                          const struct sftpc_attrs *a), void *arg)
{
    uint8_t m[1100], h[256];
    uint32_t hlen;
    struct sbuf b;
    int k, rc = 0;
    s->why[0] = 0;
    sb_init(&b, m, sizeof m);
    sb_cstr(&b, path);
    if ((k = request(s, FX_OPENDIR, &b)) < 0 || wait_for(s, k) != 0 || get_handle(s, k, h, &hlen) != 0)
        return -1;
    for (;;) {
        struct sread r;
        uint32_t n, i;
        sb_init(&b, m, sizeof m);
        sb_string(&b, h, hlen);
        if ((k = request(s, FX_READDIR, &b)) < 0 || wait_for(s, k) != 0) {
            rc = -1;
            break;
        }
        if (s->r[k].type != FX_NAME) {
            rc = status_of(s, &s->r[k], 1);
            break;
        }
        sr_init(&r, s->r[k].p, s->r[k].n);
        n = sr_u32(&r);
        for (i = 0; i < n && !r.err; i++) {
            char name[256], longname[512];
            struct sftpc_attrs a;
            uint32_t nl, ll;
            const uint8_t *np = sr_string(&r, &nl), *lp = sr_string(&r, &ll);
            read_attrs(&r, &a);
            if (r.err)
                break;
            snprintf(name, sizeof name, "%.*s", (int)nl, (const char *)np);
            snprintf(longname, sizeof longname, "%.*s", (int)ll, (const char *)lp);
            if (f(arg, name, longname, &a) != 0) {
                close_handle(s, h, hlen);
                return 0;
            }
        }
    }
    if (close_handle(s, h, hlen) != 0)
        rc = -1;
    return rc;
}

static int open_file(struct sftpc *s, const char *path, uint32_t flags, uint8_t *h, uint32_t *hlen)
{
    uint8_t m[1100];
    struct sbuf b;
    int k;
    sb_init(&b, m, sizeof m);
    sb_cstr(&b, path);
    sb_u32(&b, flags);
    if (flags & FXF_CREAT) {
        sb_u32(&b, A_PERM);
        sb_u32(&b, 0644);
    } else {
        sb_u32(&b, 0);
    }
    if ((k = request(s, FX_OPEN, &b)) < 0 || wait_for(s, k) != 0)
        return -1;
    return get_handle(s, k, h, hlen);
}

static void rw_head(struct sbuf *b, uint8_t *m, uint32_t cap, const uint8_t *h, uint32_t hlen, uint64_t off)
{
    sb_init(b, m, cap);
    sb_string(b, h, hlen);
    sb_u32(b, (uint32_t)(off >> 32));
    sb_u32(b, (uint32_t)off);
}

int sftpc_get(struct sftpc *s, const char *remote, FILE *local, void (*progress)(void *arg, uint64_t done, uint64_t
              total), void *arg)
{
    struct sftpc_attrs a;
    uint8_t h[256], m[300];
    uint32_t hlen;
    uint64_t next = 0, done = 0, total, at[INFLIGHT];
    int slot[INFLIGHT], used[INFLIGHT], i, eof = 0, rc = 0, drop = 0;
    s->why[0] = 0;
    total = sftpc_stat(s, remote, &a) == 0 && a.has_size ? a.size : 0;
    s->why[0] = 0;
    if (open_file(s, remote, FXF_READ, h, &hlen) != 0)
        return -1;
    memset(used, 0, sizeof used);
    for (;;) {
        int j = -1;
        for (i = 0; i < INFLIGHT && !eof && !rc && !drop; i++)     /* reads in flight, at the offsets that follow */
            if (!used[i]) {
                struct sbuf b;
                rw_head(&b, m, sizeof m, h, hlen, next);
                sb_u32(&b, CHUNK);
                if ((slot[i] = request(s, FX_READ, &b)) < 0) {
                    rc = -1;
                    break;
                }
                at[i] = next;
                next += CHUNK;
                used[i] = 1;
            }
        for (i = 0; i < INFLIGHT; i++)          /* the oldest answered first */
            if (used[i] && (j < 0 || at[i] < at[j]))
                j = i;
        if (j < 0)
            break;
        used[j] = 0;
        if (wait_for(s, slot[j]) != 0) {
            rc = -1;
            continue;
        }
        if (rc || drop)
            ;                                   /* (draining, or reads past a short one: dropped) */
        else if (s->r[slot[j]].type == FX_STATUS) {
            if (status_of(s, &s->r[slot[j]], 1) != 0)
                rc = -1;
            eof = 1;
        } else if (s->r[slot[j]].type == FX_DATA) {
            struct sread r;
            const uint8_t *d;
            uint32_t len;
            sr_init(&r, s->r[slot[j]].p, s->r[slot[j]].n);
            d = sr_string(&r, &len);
            if (r.err || at[j] != done || len > CHUNK) {
                fail(s, "a bad read");
                rc = -1;
            } else if (len && fwrite(d, 1, len, local) != len) {
                fail(s, "can't write the file here");
                rc = -1;
            } else {
                done += len;
                if (progress)
                    progress(arg, done, total);
                if (len < CHUNK)
                    drop = 1;                   /* (a short read: the rest from `done`, once these are back) */
            }
        }
        if (drop) {
            int busy = 0;
            for (i = 0; i < INFLIGHT; i++)
                busy |= used[i];
            if (!busy) {
                drop = 0;
                next = done;
            }
        }
    }
    if (close_handle(s, h, hlen) != 0 && rc == 0)
        rc = -1;
    return rc;
}

int sftpc_put(struct sftpc *s, FILE *local, const char *remote, void (*progress)(void *arg, uint64_t done, uint64_t
              total), void *arg)
{
    static uint8_t m[CHUNK + 300], data[CHUNK];
    uint8_t h[256];
    uint32_t hlen;
    uint64_t off = 0, total = 0, acked = 0;
    int slot[INFLIGHT], used[INFLIGHT], i, rc = 0, end = 0;
    long size;
    s->why[0] = 0;
    if (fseek(local, 0, SEEK_END) == 0 && (size = ftell(local)) >= 0)
        total = (uint64_t)size;
    fseek(local, 0, SEEK_SET);
    if (open_file(s, remote, FXF_WRITE | FXF_CREAT | FXF_TRUNC, h, &hlen) != 0)
        return -1;
    memset(used, 0, sizeof used);
    for (i = 0;; i = (i + 1) % INFLIGHT) {
        size_t n;
        struct sbuf b;
        if (used[i]) {                          /* the oldest write's answer first */
            if (wait_for(s, slot[i]) != 0 || status_of(s, &s->r[slot[i]], 0) != 0) {
                rc = -1;
                break;
            }
            used[i] = 0;
            acked += CHUNK;
            if (progress)
                progress(arg, acked < off ? acked : off, total);
        }
        if (end) {
            int j, busy = 0;
            for (j = 0; j < INFLIGHT; j++)
                busy |= used[j];
            if (!busy)
                break;
            continue;
        }
        n = fread(data, 1, CHUNK, local);
        if (!n) {
            end = 1;
            continue;
        }
        rw_head(&b, m, sizeof m, h, hlen, off);
        sb_string(&b, data, (uint32_t)n);
        if ((slot[i] = request(s, FX_WRITE, &b)) < 0) {
            rc = -1;
            break;
        }
        used[i] = 1;
        off += n;
    }
    if (close_handle(s, h, hlen) != 0 && rc == 0)
        rc = -1;
    if (rc == 0 && progress)
        progress(arg, off, total);
    return rc;
}
