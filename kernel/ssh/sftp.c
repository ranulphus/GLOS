/* The SFTP server (PRD §7.3; M3 item 9): protocol version 3
 * (draft-ietf-secsh-filexfer-02), which is what OpenSSH's sftp and scp
 * speak, on the DOS server (kernel/dos/dos.c). GLOS's own implementation:
 * DOS has no permissions, links or owners and 8.3 names only, so the
 * protocol's DOS side is small.
 *
 * Paths: "/C/TEST/FILE.TXT" is C:\TEST\FILE.TXT; a relative path starts at
 * the DOS current directory when the session began; "." and ".." are
 * resolved here. A name that isn't 8.3 is refused (DOS would truncate it
 * silently). Attributes: size, permissions (directories 0755, files 0644,
 * read-only files 0444) and times (DOS's local time, given as if UTC);
 * SETSTAT is accepted and ignored.
 *
 * Each session handles one request at a time: the ssh thread parses it,
 * posts its DOS calls and returns to its other work; sftp_service() takes
 * the answer when it has come. Input is consumed (and the client's window
 * reopened) as requests finish, and no new request starts while more than
 * 128 KB of output waits for the client. */
#include <string.h>

#include "dos.h"
#include "kprintf.h"
#include "mm.h"
#include "sftp.h"
#include "ssh.h"

#define IN_CAP     (0x40000u + 0x10000u)        /* a window, and one packet more */
#define MAX_PACKET 0x10000u
#define NHANDLES   8
#define PATH_AT    0                            /* transfer buffer: the path, */
#define PATH2_AT   128                          /* a second path (RENAME), */
#define DTA_AT     256                          /* the find DTA, */
#define DATA_AT    512                          /* and data */

enum {
    FXP_INIT = 1, FXP_VERSION, FXP_OPEN, FXP_CLOSE, FXP_READ, FXP_WRITE, FXP_LSTAT, FXP_FSTAT, FXP_SETSTAT,
    FXP_FSETSTAT, FXP_OPENDIR, FXP_READDIR, FXP_REMOVE, FXP_MKDIR, FXP_RMDIR, FXP_REALPATH, FXP_STAT,
    FXP_RENAME, FXP_READLINK, FXP_SYMLINK,
    FXP_STATUS = 101, FXP_HANDLE, FXP_DATA, FXP_NAME, FXP_ATTRS
};
enum { FX_OK, FX_EOF, FX_NO_SUCH_FILE, FX_PERMISSION_DENIED, FX_FAILURE, FX_BAD_MESSAGE, FX_NO_CONNECTION,
       FX_CONNECTION_LOST, FX_OP_UNSUPPORTED };
#define ATTR_SIZE  0x01
#define ATTR_PERM  0x04
#define ATTR_TIME  0x08
#define PF_READ   0x01
#define PF_WRITE  0x02
#define PF_APPEND 0x04
#define PF_CREAT  0x08
#define PF_TRUNC  0x10
#define PF_EXCL   0x20

struct handle {
    u8 used, dir, append, first;                /* first: OPENDIR found an entry READDIR hasn't sent */
    u16 dos;                                    /* a file's DOS handle */
    u8 dta[43];                                 /* a directory's find state */
    u32 gen;
};

struct sftp {
    struct ssh_chan *ch;
    u8 *in;
    u32 in_off, in_len;                         /* waiting input: in[in_off .. in_off + in_len) */
    u32 plen;                                   /* the request being served: its length, 0 if none */
    u8 type;
    u32 id;
    u8 busy;                                    /* a DOS request is out */
    u8 phase;                                   /* a request's step */
    u8 inited, gone, homing, eof, ended;                    /* homing: asking DOS for the current directory */
    u32 pflags, hidx;
    u64 off;                                    /* READ, WRITE */
    u32 len, done;
    const u8 *wdata;
    struct dos_req req;
    struct handle h[NHANDLES];
    u32 gen;
    char home[80];                              /* "/C/DIR", for relative paths */
    char path[80], path2[80];                   /* normalised, "/C/..." */
    char dos1[80], dos2[80];                    /* as DOS paths */
    u8 dta[43];
    u8 out[64];
};

/* ---- packets */

static u32 rd32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void wr32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }

struct rd { const u8 *p; u32 n; int err; };

static u32 get32(struct rd *r)
{
    u32 v;
    if (r->n < 4) { r->err = 1; return 0; }
    v = rd32(r->p);
    r->p += 4;
    r->n -= 4;
    return v;
}

static u64 get64(struct rd *r) { u64 hi = get32(r); return hi << 32 | get32(r); }

static const u8 *getstr(struct rd *r, u32 *len)
{
    const u8 *s;
    *len = get32(r);
    if (r->err || *len > r->n) { r->err = 1; *len = 0; return (const u8 *)""; }
    s = r->p;
    r->p += *len;
    r->n -= *len;
    return s;
}

/* Replies go straight into the channel: a header, then the body. */
static void reply(struct sftp *s, u8 type, const u8 *body, u32 n, const u8 *data, u32 dn)
{
    u8 hdr[9];
    wr32(hdr, 1 + 4 + n + dn);
    hdr[4] = type;
    wr32(hdr + 5, s->id);
    ssh_chan_write(s->ch, 0, hdr, 9);
    if (n)
        ssh_chan_write(s->ch, 0, body, n);
    if (dn)
        ssh_chan_write(s->ch, 0, data, dn);
}

static void status(struct sftp *s, u32 code, const char *msg)
{
    u8 b[128];
    u32 n = (u32)strlen(msg);
    if (n > 100)
        n = 100;
    wr32(b, code);
    wr32(b + 4, n);
    memcpy(b + 8, msg, n);
    wr32(b + 8 + n, 0);                         /* language tag: empty */
    reply(s, FXP_STATUS, b, 12 + n, 0, 0);
}

static u32 dos_status(u16 err)
{
    switch (err) {
    case 2: case 3: case 0x12: return FX_NO_SUCH_FILE;
    case 5: case 0x13: case 0x20: case 0x21: return FX_PERMISSION_DENIED;
    default: return FX_FAILURE;
    }
}

/* ---- names and attributes */

static char up(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

/* An 8.3 name component of DOS's characters. */
static int name83(const char *c, u32 n)
{
    u32 i, base = 0, ext = 0, dot = 0;
    for (i = 0; i < n; i++) {
        char ch = c[i];
        if (ch == '.') {
            if (dot++ || !base)
                return 0;
        } else if (ch <= ' ' || ch == '"' || ch == '*' || ch == '+' || ch == ',' || ch == '/' || ch == ':'
                   || ch == ';' || ch == '<' || ch == '=' || ch == '>' || ch == '?' || ch == '[' || ch == '\\'
                   || ch == ']' || ch == '|' || (u8)ch >= 0x80) {
            return 0;
        } else if (dot) {
            ext++;
        } else {
            base++;
        }
    }
    return base >= 1 && base <= 8 && ext <= 3 && !(dot && !ext);
}

/* p (relative to home unless it starts with '/') resolved into out as
   "/X/A/B" ("/" alone, or "/X" for a drive's root); 0, or -1 if a
   component isn't 8.3 or the result is too long. */
static int normalise(const char *home, const u8 *p, u32 n, char *out)
{
    char buf[160];
    u32 len = 0, i = 0, st;
    if (n && p[0] == '/') {
        out[0] = 0;
    } else {
        strcpy(out, home);
    }
    len = (u32)strlen(out);
    if (n > sizeof buf - 1)
        return -1;
    memcpy(buf, p, n);
    buf[n] = 0;
    while (i < n) {
        while (i < n && buf[i] == '/')
            i++;
        st = i;
        while (i < n && buf[i] != '/')
            i++;
        if (i == st || (i - st == 1 && buf[st] == '.'))
            continue;
        if (i - st == 2 && buf[st] == '.' && buf[st + 1] == '.') {
            while (len && out[len - 1] != '/')
                len--;
            if (len)
                len--;
            out[len] = 0;
            continue;
        }
        if (len == 0) {                         /* the first component: a drive */
            if (i - st != 1 || up(buf[st]) < 'A' || up(buf[st]) > 'Z')
                return -1;
        } else if (!name83(buf + st, i - st)) {
            return -1;
        }
        if (len + 1 + (i - st) > 66)
            return -1;
        out[len++] = '/';
        for (; st < i; st++)
            out[len++] = up(buf[st]);
        out[len] = 0;
    }
    if (!len)
        strcpy(out, "/");
    return 0;
}

/* "/X/A/B" as "X:\A\B"; "/X" as "X:\". */
static void dos_path(const char *p, char *out)
{
    u32 i, n = 0;
    out[n++] = p[1];
    out[n++] = ':';
    if (!p[2])
        out[n++] = '\\';
    for (i = 2; p[i]; i++)
        out[n++] = p[i] == '/' ? '\\' : p[i];
    out[n] = 0;
}

static u32 unix_time(u16 date, u16 time)
{
    u32 y = 1980 + (date >> 9), m = (date >> 5) & 15, d = date & 31, era, yoe, doy, doe;
    if (m < 1 || m > 12 || d < 1)
        return 315532800u;                      /* 1980-01-01 */
    y -= m <= 2;                                /* days from civil (H. Hinnant) */
    era = y / 400;
    yoe = y - era * 400;
    doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return ((era * 146097 + doe - 719468) * 86400u) + (time >> 11) * 3600u + ((time >> 5) & 63) * 60u
           + (time & 31) * 2u;
}

/* ATTRS for a DTA entry, or a directory with no DTA (a drive's root). */
static u32 attrs(u8 *b, const u8 *dta)
{
    u32 perm, t = 315532800u, size = 0;
    if (dta) {
        u8 a = dta[0x15];
        size = (u32)dta[0x1A] | (u32)dta[0x1B] << 8 | (u32)dta[0x1C] << 16 | (u32)dta[0x1D] << 24;
        t = unix_time((u16)(dta[0x18] | dta[0x19] << 8), (u16)(dta[0x16] | dta[0x17] << 8));
        perm = (a & 0x10) ? 0040755 : (a & 0x01) ? 0100444 : 0100644;
        if (a & 0x10)
            size = 0;
    } else {
        perm = 0040755;
    }
    wr32(b, ATTR_SIZE | ATTR_PERM | ATTR_TIME);
    wr32(b + 4, 0);
    wr32(b + 8, size);
    wr32(b + 12, perm);
    wr32(b + 16, t);
    wr32(b + 20, t);
    return 24;
}

/* A NAME reply with one entry. */
static void name1(struct sftp *s, const char *name, const u8 *dta, int dir)
{
    static const char *const mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    u8 b[256];
    char ln[96];
    u32 n = (u32)strlen(name), k, size = 0, m = 1, d = 1, hh = 0, mm = 0;
    if (dta) {
        u16 date = (u16)(dta[0x18] | dta[0x19] << 8), time = (u16)(dta[0x16] | dta[0x17] << 8);
        size = (dta[0x15] & 0x10) ? 0 : (u32)dta[0x1A] | (u32)dta[0x1B] << 8 | (u32)dta[0x1C] << 16
                                         | (u32)dta[0x1D] << 24;
        m = (date >> 5) & 15;
        d = date & 31;
        hh = time >> 11;
        mm = (time >> 5) & 63;
        dir = (dta[0x15] & 0x10) != 0;
    }
    k = (u32)ksnprintf(ln, sizeof ln, "%crw%cr--r--    1 0        0        %10u %s %02u %02u:%02u %s",
                       dir ? 'd' : '-', dir ? 'x' : '-', size, mon[(m - 1) % 12], d, hh, mm, name);
    if (k > sizeof ln - 1)
        k = sizeof ln - 1;
    wr32(b, 1);
    wr32(b + 4, n);
    memcpy(b + 8, name, n);
    wr32(b + 8 + n, k);
    memcpy(b + 12 + n, ln, k);
    n = 12 + n + k;
    n += attrs(b + n, dta);
    reply(s, FXP_NAME, b, n, 0, 0);
}

/* ---- handles */

static void handle_reply(struct sftp *s, u32 i)
{
    u8 b[12];
    wr32(b, 8);
    wr32(b + 4, i);
    wr32(b + 8, s->h[i].gen);
    reply(s, FXP_HANDLE, b, 12, 0, 0);
}

static int new_handle(struct sftp *s)
{
    u32 i;
    for (i = 0; i < NHANDLES; i++)
        if (!s->h[i].used) {
            memset(&s->h[i], 0, sizeof s->h[i]);
            s->h[i].used = 1;
            s->h[i].gen = ++s->gen;
            return (int)i;
        }
    return -1;
}

static int find_handle(struct sftp *s, struct rd *r)
{
    u32 n, i, g;
    const u8 *p = getstr(r, &n);
    if (r->err || n != 8)
        return -1;
    i = rd32(p);
    g = rd32(p + 4);
    return i < NHANDLES && s->h[i].used && s->h[i].gen == g ? (int)i : -1;
}

/* ---- DOS requests */

static void call(struct dos_req *q, u32 k, u16 ax, u16 bx, u16 cx, u16 dx)
{
    memset(&q->r[k], 0, sizeof q->r[k]);
    q->r[k].ax = ax;
    q->r[k].bx = bx;
    q->r[k].cx = cx;
    q->r[k].dx = dx;
    q->r[k].ds = q->r[k].es = DOS_XFER;
    if (k + 1 > q->n)
        q->n = k + 1;
}

static void post(struct sftp *s)
{
    if (dos_post(&s->req) != 0) {
        s->req.state = DR_DONE;
        s->req.error = 1;
    }
    s->busy = 1;
}

/* A request that sends a path (and a second one) to DOS. */
static void begin(struct sftp *s, const char *dos1, const char *dos2)
{
    static u8 paths[256];
    memset(&s->req, 0, sizeof s->req);
    memset(paths, 0, sizeof paths);
    if (dos1)
        strcpy((char *)paths + PATH_AT, dos1);
    if (dos2)
        strcpy((char *)paths + PATH2_AT, dos2);
    s->req.in = paths;
    s->req.in_len = dos1 || dos2 ? sizeof paths : 0;
}

/* Find (first: the path in the buffer) into the DTA, which comes back. */
static void find(struct sftp *s, int first, const u8 *dta)
{
    if (!first) {                               /* findnext continues from its own DTA */
        memset(&s->req, 0, sizeof s->req);
        memcpy(s->dta, dta, 43);
        s->req.in = s->dta;
        s->req.in_len = 43;
        s->req.in_at = DTA_AT;
    }
    call(&s->req, 0, 0x1A00, 0, 0, DTA_AT);
    if (first)
        call(&s->req, 1, 0x4E00, 0, 0x37, PATH_AT);
    else
        call(&s->req, 1, 0x4F00, 0, 0, 0);
    s->req.out = s->dta;
    s->req.out_len = 43;
    s->req.out_at = DTA_AT;
}

/* ---- requests */

/* WRITE's next piece, as much as the transfer buffer holds: seek, write. */
static void write_chunk(struct sftp *s)
{
    struct handle *h = &s->h[s->hidx];
    u32 k = s->len - s->done, room = dos_xfer_size() - DATA_AT;
    if (k > room)
        k = room;
    memset(&s->req, 0, sizeof s->req);
    s->req.in = s->wdata + s->done;
    s->req.in_len = k;
    s->req.in_at = DATA_AT;
    if (h->append)
        call(&s->req, 0, 0x4202, h->dos, 0, 0);
    else
        call(&s->req, 0, 0x4200, h->dos, (u16)((s->off + s->done) >> 16), (u16)(s->off + s->done));
    call(&s->req, 1, 0x4000, h->dos, (u16)k, DATA_AT);
    post(s);
}

/* Parse a new request; 1 when it has been answered, 0 when it waits for DOS. */
static int start(struct sftp *s, const u8 *p, u32 n)
{
    struct rd r = { p, n, 0 };
    const u8 *a, *b;
    u32 an, bn;
    int i;

    s->type = *r.p++;
    r.n--;
    if (s->type == FXP_INIT) {
        u8 v[4];
        u32 ver = get32(&r);
        (void)ver;
        wr32(v, 3);
        {                                       /* VERSION has no request id */
            u8 hdr[5];
            wr32(hdr, 5);
            hdr[4] = FXP_VERSION;
            ssh_chan_write(s->ch, 0, hdr, 5);
            ssh_chan_write(s->ch, 0, v, 4);
        }
        s->inited = 1;
        return 1;
    }
    s->id = get32(&r);
    if (r.err)
        return status(s, FX_BAD_MESSAGE, "short request"), 1;
    s->phase = 0;
    switch (s->type) {
    case FXP_REALPATH:
        a = getstr(&r, &an);
        if (r.err || normalise(s->home, a, an, s->path) != 0)
            return status(s, FX_NO_SUCH_FILE, "not a DOS path (8.3 names only)"), 1;
        name1(s, s->path, 0, 1);
        return 1;
    case FXP_STAT:
    case FXP_LSTAT:
    case FXP_OPENDIR:
    case FXP_REMOVE:
    case FXP_MKDIR:
    case FXP_RMDIR:
    case FXP_OPEN:
        a = getstr(&r, &an);
        if (r.err || normalise(s->home, a, an, s->path) != 0)
            return status(s, FX_NO_SUCH_FILE, "not a DOS path (8.3 names only)"), 1;
        if (s->path[1] == 0 || ((s->type == FXP_STAT || s->type == FXP_LSTAT) && s->path[2] == 0)) {
            u8 at[32];
            if (s->type == FXP_STAT || s->type == FXP_LSTAT)
                return reply(s, FXP_ATTRS, at, attrs(at, 0), 0, 0), 1;
            if (s->path[1] == 0)
                return status(s, FX_PERMISSION_DENIED, "/ holds the drives: use /C"), 1;
        }
        dos_path(s->path, s->dos1);
        break;
    case FXP_RENAME:
        a = getstr(&r, &an);
        b = getstr(&r, &bn);
        if (r.err || normalise(s->home, a, an, s->path) != 0 || normalise(s->home, b, bn, s->path2) != 0
            || !s->path[1] || !s->path[2] || !s->path2[1] || !s->path2[2])
            return status(s, FX_NO_SUCH_FILE, "not a DOS path (8.3 names only)"), 1;
        dos_path(s->path, s->dos1);
        dos_path(s->path2, s->dos2);
        begin(s, s->dos1, s->dos2);
        call(&s->req, 0, 0x5600, 0, 0, PATH_AT);
        s->req.r[0].di = PATH2_AT;
        post(s);
        return 0;
    case FXP_CLOSE:
    case FXP_READ:
    case FXP_WRITE:
    case FXP_FSTAT:
    case FXP_READDIR:
    case FXP_FSETSTAT:
        if ((i = find_handle(s, &r)) < 0)
            return status(s, FX_FAILURE, "no such handle"), 1;
        s->hidx = (u32)i;
        break;
    case FXP_SETSTAT:
        return status(s, FX_OK, ""), 1;         /* DOS has nothing to set that we keep */
    default:
        return status(s, FX_OP_UNSUPPORTED, "not on DOS"), 1;
    }

    switch (s->type) {
    case FXP_STAT:
    case FXP_LSTAT:
    case FXP_OPENDIR:
        begin(s, s->dos1, 0);
        if (s->type == FXP_OPENDIR) {           /* the pattern: the directory's *.* */
            u32 k = (u32)strlen(s->dos1);
            strcpy((char *)s->req.in + PATH_AT + k, s->dos1[k - 1] == '\\' ? "*.*" : "\\*.*");
        }
        find(s, 1, 0);
        post(s);
        return 0;
    case FXP_REMOVE:
    case FXP_MKDIR:
    case FXP_RMDIR:
        begin(s, s->dos1, 0);
        call(&s->req, 0, s->type == FXP_REMOVE ? 0x4100 : s->type == FXP_MKDIR ? 0x3900 : 0x3A00, 0, 0, PATH_AT);
        post(s);
        return 0;
    case FXP_OPEN:
        s->pflags = get32(&r);
        if (r.err)
            return status(s, FX_BAD_MESSAGE, "short open"), 1;
        begin(s, s->dos1, 0);
        if ((s->pflags & PF_WRITE) && (s->pflags & PF_CREAT) && (s->pflags & PF_EXCL))
            call(&s->req, 0, 0x5B00, 0, 0, PATH_AT);
        else if ((s->pflags & PF_WRITE) && (s->pflags & PF_CREAT) && (s->pflags & PF_TRUNC))
            call(&s->req, 0, 0x3C00, 0, 0, PATH_AT);
        else
            call(&s->req, 0, (s->pflags & PF_WRITE) ? ((s->pflags & PF_READ) ? 0x3D02 : 0x3D01) : 0x3D00, 0, 0,
                 PATH_AT);
        post(s);
        return 0;
    case FXP_CLOSE:
        if (s->h[s->hidx].dir) {
            s->h[s->hidx].used = 0;
            return status(s, FX_OK, ""), 1;
        }
        memset(&s->req, 0, sizeof s->req);
        call(&s->req, 0, 0x3E00, s->h[s->hidx].dos, 0, 0);
        post(s);
        return 0;
    case FXP_FSETSTAT:
        return status(s, FX_OK, ""), 1;
    case FXP_FSTAT:
        if (s->h[s->hidx].dir)
            return status(s, FX_FAILURE, "a directory"), 1;
        memset(&s->req, 0, sizeof s->req);
        call(&s->req, 0, 0x4202, s->h[s->hidx].dos, 0, 0);     /* the size: seek to the end */
        call(&s->req, 1, 0x5700, s->h[s->hidx].dos, 0, 0);     /* the date and time */
        post(s);
        return 0;
    case FXP_READ:
        s->off = get64(&r);
        s->len = get32(&r);
        if (r.err || s->h[s->hidx].dir)
            return status(s, FX_FAILURE, "bad read"), 1;
        if (s->off >> 32)
            return status(s, FX_EOF, ""), 1;
        if (s->len > dos_xfer_size() - DATA_AT)
            s->len = dos_xfer_size() - DATA_AT;
        if (s->len > 32768)
            s->len = 32768;
        memset(&s->req, 0, sizeof s->req);
        call(&s->req, 0, 0x4200, s->h[s->hidx].dos, (u16)(s->off >> 16), (u16)s->off);
        call(&s->req, 1, 0x3F00, s->h[s->hidx].dos, (u16)s->len, DATA_AT);
        s->req.out = kmalloc(s->len ? s->len : 1);
        if (!s->req.out)
            return status(s, FX_FAILURE, "no memory"), 1;
        s->req.out_len = s->len;
        s->req.out_at = DATA_AT;
        post(s);
        return 0;
    case FXP_WRITE:
        s->off = get64(&r);
        s->wdata = getstr(&r, &s->len);
        if (r.err || s->h[s->hidx].dir)
            return status(s, FX_FAILURE, "bad write"), 1;
        if (s->off >> 32)
            return status(s, FX_FAILURE, "past 4 GB"), 1;
        s->done = 0;
        write_chunk(s);
        return 0;
    case FXP_READDIR: {
            struct handle *h = &s->h[s->hidx];
            if (!h->dir)
                return status(s, FX_FAILURE, "not a directory"), 1;
            if (h->first) {
                h->first = 0;
                name1(s, (const char *)h->dta + 0x1E, h->dta, 0);
                return 1;
            }
            find(s, 0, h->dta);
            post(s);
            return 0;
        }
    }
    return status(s, FX_FAILURE, "unexpected"), 1;
}

/* The DOS request has come back: answer, or go on with the next step. 1
   when the request is answered. */
static int resume(struct sftp *s)
{
    struct dos_req *q = &s->req;
    struct handle *h = &s->h[s->hidx];
    int i;

    s->busy = 0;
    if (s->type == FXP_READ) {
        u8 *data = q->out;
        u32 got = q->error ? 0 : q->r[1].ax;
        if (q->error)
            status(s, dos_status(q->error), "read failed");
        else if (!got)
            status(s, FX_EOF, "");
        else {
            u8 b[4];
            wr32(b, got);
            reply(s, FXP_DATA, b, 4, data, got);
        }
        kfree(data);
        return 1;
    }
    if (q->error) {
        if (s->type == FXP_OPEN && q->error == 2 && (s->pflags & PF_CREAT) && s->phase == 0) {
            s->phase = 1;                       /* not there: create it */
            begin(s, s->dos1, 0);
            call(&s->req, 0, 0x3C00, 0, 0, PATH_AT);
            post(s);
            return 0;
        }
        if (s->type == FXP_OPENDIR && q->error == 0x12 && q->ran == 2) {
            i = new_handle(s);                  /* an empty directory */
            if (i < 0)
                return status(s, FX_FAILURE, "too many handles"), 1;
            s->h[i].dir = 1;
            s->h[i].first = 0;
            memcpy(s->h[i].dta, s->dta, 43);
            return handle_reply(s, (u32)i), 1;
        }
        if (s->type == FXP_READDIR && (q->error == 0x12 || q->error == 2))
            return status(s, FX_EOF, ""), 1;
        return status(s, dos_status(q->error), "DOS refused it"), 1;
    }
    switch (s->type) {
    case FXP_STAT:
    case FXP_LSTAT: {
            u8 at[32];
            reply(s, FXP_ATTRS, at, attrs(at, s->dta), 0, 0);
            return 1;
        }
    case FXP_OPENDIR:
        i = new_handle(s);
        if (i < 0)
            return status(s, FX_FAILURE, "too many handles"), 1;
        s->h[i].dir = 1;
        s->h[i].first = 1;
        memcpy(s->h[i].dta, s->dta, 43);
        return handle_reply(s, (u32)i), 1;
    case FXP_READDIR:
        memcpy(h->dta, s->dta, 43);
        name1(s, (const char *)h->dta + 0x1E, h->dta, 0);
        return 1;
    case FXP_OPEN:
        i = new_handle(s);
        if (i < 0) {                            /* no handle to give: close it again */
            memset(&s->req, 0, sizeof s->req);
            call(&s->req, 0, 0x3E00, q->r[0].ax, 0, 0);
            post(s);
            s->type = FXP_CLOSE;
            s->phase = 9;
            return 0;
        }
        s->h[i].dos = q->r[0].ax;
        s->h[i].append = (s->pflags & PF_APPEND) != 0;
        return handle_reply(s, (u32)i), 1;
    case FXP_CLOSE:
        if (s->phase == 9)
            return status(s, FX_FAILURE, "too many handles"), 1;
        h->used = 0;
        return status(s, FX_OK, ""), 1;
    case FXP_FSTAT: {
            u8 dta[43], at[32];
            memset(dta, 0, sizeof dta);
            dta[0x1A] = (u8)q->r[0].ax;
            dta[0x1B] = (u8)(q->r[0].ax >> 8);
            dta[0x1C] = (u8)q->r[0].dx;
            dta[0x1D] = (u8)(q->r[0].dx >> 8);
            dta[0x16] = (u8)q->r[1].cx;
            dta[0x17] = (u8)(q->r[1].cx >> 8);
            dta[0x18] = (u8)q->r[1].dx;
            dta[0x19] = (u8)(q->r[1].dx >> 8);
            reply(s, FXP_ATTRS, at, attrs(at, dta), 0, 0);
            return 1;
        }
    case FXP_WRITE: {
            u32 k = s->len - s->done, room = dos_xfer_size() - DATA_AT;
            if (k > room)
                k = room;
            if (q->r[1].ax != k)
                return status(s, FX_FAILURE, "disk full"), 1;
            s->done += k;
            if (s->done < s->len) {
                write_chunk(s);
                return 0;
            }
            return status(s, FX_OK, ""), 1;
        }
    default:                                    /* REMOVE, MKDIR, RMDIR, RENAME */
        return status(s, FX_OK, ""), 1;
    }
}

/* ---- sessions */

struct sftp *sftp_open(struct ssh_chan *ch)
{
    struct sftp *s = kmalloc(sizeof *s);
    if (!s)
        return 0;
    memset(s, 0, sizeof *s);
    s->in = kmalloc(IN_CAP);
    if (!s->in) {
        kfree(s);
        return 0;
    }
    s->ch = ch;
    strcpy(s->home, "/C");
    s->homing = 1;
    ssh_chan_hold(ch);
    dos_hold();
    return s;
}

/* The session's home: DOS's current drive and directory (19h, 47h). */
static void home_ask(struct sftp *s)
{
    memset(&s->req, 0, sizeof s->req);
    call(&s->req, 0, 0x1900, 0, 0, 0);
    call(&s->req, 1, 0x4700, 0, 0, 0);
    s->req.r[1].si = PATH_AT;
    s->req.out = (u8 *)s->dos1;
    s->req.out_len = 64;
    s->req.out_at = PATH_AT;
    post(s);
}

static void home_set(struct sftp *s)
{
    u32 n = 0, i;
    s->homing = 0;
    if (s->req.error)
        return;                                 /* "/C" then */
    s->home[n++] = '/';
    s->home[n++] = (char)('A' + (s->req.r[0].ax & 0xFF));
    s->dos1[63] = 0;
    for (i = 0; s->dos1[i] && n < sizeof s->home - 2; i++) {
        if (i == 0)
            s->home[n++] = '/';
        s->home[n++] = s->dos1[i] == '\\' ? '/' : up(s->dos1[i]);
    }
    s->home[n] = 0;
}

void sftp_input(struct sftp *s, const u8 *d, u32 n)
{
    if (s->in_off + s->in_len + n > IN_CAP) {   /* make room at the front */
        memcpy(s->in, s->in + s->in_off, s->in_len);
        s->in_off = 0;
    }
    if (s->in_len + n > IN_CAP)
        n = IN_CAP - s->in_len;                 /* can't happen: the window bounds it */
    memcpy(s->in + s->in_off + s->in_len, d, n);
    s->in_len += n;
}

/* Done with the request at the front: its bytes go, and the window opens. */
static void consumed(struct sftp *s)
{
    u32 k = 4 + s->plen;
    s->in_off += k;
    s->in_len -= k;
    if (!s->in_len)
        s->in_off = 0;
    s->plen = 0;
    ssh_chan_consumed(s->ch, k);
}

/* 1 while the session wants its DOS request finished before it may go. */
int sftp_service(struct sftp *s)
{
    u32 i;
    if (s->busy) {
        if (s->req.state != DR_DONE)
            return 1;
        if (s->homing) {
            s->busy = 0;
            home_set(s);
        } else if (s->gone) {                   /* nobody to answer */
            if (s->type == FXP_READ && s->phase != 8)
                kfree(s->req.out);
            s->busy = 0;
        } else if (resume(s)) {
            consumed(s);
        }
    }
    if (s->gone && !s->busy) {                  /* close what the client left open, then go */
        for (i = 0; i < NHANDLES; i++)
            if (s->h[i].used && !s->h[i].dir) {
                s->h[i].used = 0;
                memset(&s->req, 0, sizeof s->req);
                call(&s->req, 0, 0x3E00, s->h[i].dos, 0, 0);
                s->type = FXP_CLOSE;
                s->phase = 8;
                post(s);
                return 1;
            }
        return -1;
    }
    if (s->homing && !s->busy) {
        home_ask(s);
        return 1;
    }
    while (!s->busy && !s->gone && ssh_chan_pending(s->ch) < 0x20000) {
        u32 len;
        if (s->in_len < 4)
            break;
        len = rd32(s->in + s->in_off);
        if (len < 1 || len > MAX_PACKET) {
            status(s, FX_BAD_MESSAGE, "bad length");
            ssh_chan_exit(s->ch, 1);
            s->in_len = 0;
            break;
        }
        if (s->in_len < 4 + len)
            break;
        s->plen = len;
        if (start(s, s->in + s->in_off + 4, len))
            consumed(s);
    }
    if (s->eof && !s->busy && !s->ended && s->in_len < 4) {     /* all answered: the end */
        s->ended = 1;
        ssh_chan_exit(s->ch, 0);
    }
    return s->busy;
}

void sftp_eof(struct sftp *s) { s->eof = 1; }

/* The channel has gone: s may be freed once sftp_service returns 0, after
   the files the client left open are closed. */
void sftp_close(struct sftp *s) { s->gone = 1; }

void sftp_free(struct sftp *s)
{
    dos_release();
    kfree(s->in);
    kfree(s);
}
