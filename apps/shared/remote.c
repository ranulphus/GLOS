/* A folder tree on another computer over SFTP (remote.h): GLOS's SSH client (sdk/sshc) on a connection of GLOS's
 * (NET), logged in the way SSH.EXE logs in (apps/ssh/ssh.c is the same steps, a line at a time on a terminal):
 * the host's key against KNOWNHST.TXT (a changed one refused; a new one asked about), the person's Ed25519 key
 * first, then a password asked for up to three times; then the "sftp" subsystem. Every call waits: the network is
 * read, a tenth of a second at a time, until its answer has come. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glos/glos.h>
#include <glos/sftpc.h>
#include <glos/sshc.h>
#ifdef __DJGPP__
#include <glos/net.h>
#else
#include <arpa/inet.h>                          /* (the host's build: inet_aton alone) */
#endif

#include "remote.h"

static void *open_plain(const char *path, const char *mode) { return fopen(path, mode); }
static void *(*file_open)(const char *path, const char *mode) = open_plain;

void remote_files(void *(*open)(const char *path, const char *mode))
{
    file_open = open ? open : open_plain;
}

struct remote {
    glos_handle conn;
    struct sshc *c;
    struct sshc_chan *ch;
    struct sftpc *sf;
    char host[80], known[260], home[260], why[160];
    unsigned port;
    uint8_t host_pk[32];
    char fp[56];
    int host_new, changed, closed;
};

/* ---- the network */

static void net_send(void *io, const uint8_t *d, uint32_t n)
{
    struct remote *r = io;
    while (n && r->conn) {
        int k = glos_net_send(r->conn, d, n, 10000);
        if (k <= 0)
            break;
        d += k, n -= (uint32_t)k;
    }
}

static int net_read(struct remote *r, unsigned wait_ms)         /* what has come, into the client: -1 once it is gone */
{
    static uint8_t buf[8192];
    unsigned st;
    int k, first = 1;
    if (!r->conn || !r->c)
        return -1;
    for (;;) {
        k = glos_net_receive(r->conn, buf, sizeof buf, first ? wait_ms : 0, &st);
        first = 0;
        if (k < 0 || (k == 0 && st == GLOS_NETS_CLOSED))
            return -1;
        if (k == 0)
            return 0;
        if (sshc_input(r->c, buf, (uint32_t)k) != 0)
            return -1;
    }
}

static int pump(void *arg, unsigned ms)
{
    return net_read(arg, ms > 100 ? 100 : ms);
}

/* ---- the client's callbacks */

static int on_hostkey(void *io, const uint8_t pk[32], const char *fp)
{
    struct remote *r = io;
    int k = sshc_known(r->known, r->host, r->port, pk);
    memcpy(r->host_pk, pk, 32);
    snprintf(r->fp, sizeof r->fp, "%s", fp);
    if (k == -1) {
        r->changed = 1;
        return -1;
    }
    r->host_new = k != 1;
    return 0;                                   /* (a new one: asked about before the login) */
}

static void on_data(struct sshc_chan *ch, int stream, const uint8_t *d, uint32_t n)
{
    struct remote *r = sshc_io(sshc_chan_conn(ch));
    if (stream == 0 && r->sf)
        sftpc_input(r->sf, d, n);
}

static void on_closed(struct sshc_chan *ch)
{
    struct remote *r = sshc_io(sshc_chan_conn(ch));
    if (ch == r->ch)
        r->ch = NULL, r->closed = 1;
}

static const struct sshc_ops ops = { net_send, NULL, on_hostkey, NULL, on_data, NULL, NULL, on_closed };

/* Waits until the client is in another state than `while_in` (or dead), 20 seconds at most. */
static int settle(struct remote *r, int while_in)
{
    unsigned t0 = glos_ticks();
    while (sshc_state(r->c) == while_in && glos_ticks() - t0 < 20000)
        if (net_read(r, 100) != 0)
            return -1;
    return sshc_state(r->c) == while_in || sshc_state(r->c) == SSHC_DEAD ? -1 : 0;
}

struct remote *remote_open(const char *host, unsigned port, const char *user, const char *dir,
                           const struct remote_ui *ui, void *ctx, int *err, char *why, unsigned room)
{
    struct remote *r = calloc(1, sizeof *r);
    uint8_t sk[64], pk[32];
    char key[260], password[128];
    struct in_addr ia;
    unsigned addr;
    int e, dummy, have_key, tries = 0;
    if (!err)
        err = &dummy;
    if (why && room)
        why[0] = 0;
    *err = REMOTE_E_MEMORY;
    if (!r)
        return NULL;
    snprintf(r->host, sizeof r->host, "%s", host);
    r->port = port ? port : 22;
    snprintf(r->known, sizeof r->known, "%s\\KNOWNHST.TXT", dir);
    snprintf(key, sizeof key, "%s\\ID_ED25", dir);
    have_key = sshc_key_read(key, sk, pk) == 0;
    *err = REMOTE_E_NAME;
    if (inet_aton(host, &ia)) {
        addr = (unsigned)ia.s_addr;
    } else if ((e = glos_net_resolve(host, 15000, &addr)) != 0) {
        if (why)
            snprintf(why, room, "%s", e == GLOS_E_NET_DOWN ? "there is no network" : "no computer of that name was found");
        goto bad;
    }
    *err = REMOTE_E_CONNECT;
    if (!(r->conn = glos_net_connect(addr, r->port, 15000, 0))) {
        e = glos_error();
        if (why)
            snprintf(why, room, "%s", e == GLOS_E_NET_REFUSED ? "it refused the connection (no SSH server there?)"
                     : e == GLOS_E_NET_TIMEOUT ? "it did not answer" : "there is no way to it from here");
        goto bad;
    }
    *err = REMOTE_E_MEMORY;
    if (!(r->c = sshc_new(&ops, r, NULL)))
        goto bad;
    *err = REMOTE_E_CONNECT;
    if (settle(r, SSHC_KEX) != 0 || sshc_state(r->c) != SSHC_AUTH) {
        *err = r->changed ? REMOTE_E_HOSTKEY : REMOTE_E_CONNECT;
        if (why) {
            if (r->changed)
                snprintf(why, room, "its key (%s) is not the one this PC knows it by: refused", r->fp);
            else
                snprintf(why, room, "%s", sshc_why(r->c)[0] ? sshc_why(r->c) : "it did not answer as an SSH server");
        }
        goto bad;
    }
    if (r->host_new) {
        *err = REMOTE_E_HOSTKEY;
        if (!ui || !ui->new_host || !ui->new_host(ctx, host, r->fp)) {
            if (why)
                snprintf(why, room, "its key was not accepted");
            goto bad;
        }
        mkdir(dir, 0777);
        sshc_known_add(r->known, r->host, r->port, r->host_pk);
    }
    *err = REMOTE_E_LOGIN;
    sshc_login(r->c, user, have_key ? sk : NULL, have_key ? pk : NULL, NULL);
    for (;;) {
        if (settle(r, SSHC_LOGGING_IN) != 0) {
            if (why)
                snprintf(why, room, "%s", sshc_why(r->c)[0] ? sshc_why(r->c) : "the connection ended");
            goto bad;
        }
        if (sshc_state(r->c) == SSHC_READY)
            break;
        {                                       /* refused: a password, if it takes one */
            const char *m = sshc_methods(r->c);
            if (!(strstr(m, "password") || strstr(m, "keyboard-interactive")) || tries >= 3 || !ui || !ui->password
                || !ui->password(ctx, user, host, tries > 0, password, sizeof password)) {
                if (why)
                    snprintf(why, room, "%s", tries >= 3 ? "the password was refused three times"
                             : sshc_why(r->c)[0] ? sshc_why(r->c) : "the login was refused");
                goto bad;
            }
            tries++;
            sshc_login(r->c, user, NULL, NULL, password);
            memset(password, 0, sizeof password);
        }
    }
    memset(sk, 0, sizeof sk);
    *err = REMOTE_E_FAILED;
    if (!(r->ch = sshc_session(r->c, NULL, 0, 0, NULL, "sftp")))
        goto bad;
    {
        unsigned t0 = glos_ticks();
        while (r->ch && sshc_chan_ok(r->ch) == 0 && glos_ticks() - t0 < 20000)
            if (net_read(r, 100) != 0)
                break;
    }
    if (!r->ch || sshc_chan_ok(r->ch) != 1 || !(r->sf = sftpc_new(r->ch, pump, r)) || sftpc_start(r->sf, 15000) != 0) {
        if (why)
            snprintf(why, room, "it has no file transfer (SFTP) for this login");
        goto bad;
    }
    if (sftpc_realpath(r->sf, ".", r->home, sizeof r->home) != 0 || r->home[0] != '/')
        strcpy(r->home, "/");
    *err = REMOTE_OK;
    return r;
bad:
    memset(sk, 0, sizeof sk);
    remote_close(r);
    return NULL;
}

void remote_close(struct remote *r)
{
    if (!r)
        return;
    if (r->sf)
        sftpc_free(r->sf);
    if (r->c)
        sshc_free(r->c);
    if (r->conn)
        glos_net_close(r->conn, 0);
    free(r);
}

const char *remote_home(const struct remote *r) { return r->home; }
const char *remote_why(const struct remote *r) { return r->why; }

static int done(struct remote *r, int k)        /* an sftpc call's answer as ours */
{
    if (k == 0)
        return REMOTE_OK;
    snprintf(r->why, sizeof r->why, "%s", r->sf ? sftpc_why(r->sf) : "");
    return r->closed || !r->ch || sshc_state(r->c) == SSHC_DEAD ? REMOTE_E_GONE : REMOTE_E_FAILED;
}

struct lister {
    int (*each)(void *ctx, const struct remote_entry *e);
    void *ctx;
};

static int list_one(void *arg, const char *name, const char *longname, const struct sftpc_attrs *a)
{
    struct lister *l = arg;
    struct remote_entry e;
    (void)longname;
    if (!strcmp(name, ".") || !strcmp(name, ".."))
        return 0;
    e.name = name;
    e.dir = a->has_perm && (a->perm & 0170000) == 0040000;
    e.size = a->has_size ? a->size : 0;
    e.mtime = a->has_time ? a->mtime : 0;
    return l->each(l->ctx, &e);
}

int remote_list(struct remote *r, const char *dir, int (*each)(void *ctx, const struct remote_entry *e), void *ctx)
{
    struct lister l = { each, ctx };
    return done(r, sftpc_ls(r->sf, dir, list_one, &l));
}

int remote_stat(struct remote *r, const char *path, struct remote_entry *e)
{
    struct sftpc_attrs a;
    int k = sftpc_stat(r->sf, path, &a);
    memset(e, 0, sizeof *e);
    if (k == 0) {
        e->dir = a.has_perm && (a.perm & 0170000) == 0040000;
        e->size = a.has_size ? a.size : 0;
        e->mtime = a.has_time ? a.mtime : 0;
    }
    return done(r, k);
}

struct moving {
    void (*progress)(void *ctx, unsigned long long done, unsigned long long total);
    void *ctx;
};

static void moved(void *arg, uint64_t n, uint64_t total)
{
    struct moving *m = arg;
    if (m->progress)
        m->progress(m->ctx, n, total);
}

int remote_get(struct remote *r, const char *path, const char *local,
               void (*progress)(void *ctx, unsigned long long done, unsigned long long total), void *ctx)
{
    struct moving m = { progress, ctx };
    FILE *f = file_open(local, "wb");
    int k;
    if (!f)
        return REMOTE_E_LOCAL;
    k = sftpc_get(r->sf, path, f, moved, &m);
    if (fclose(f) != 0 && k == 0)
        return REMOTE_E_LOCAL;
    return done(r, k);
}

int remote_put(struct remote *r, const char *local, const char *path,
               void (*progress)(void *ctx, unsigned long long done, unsigned long long total), void *ctx)
{
    struct moving m = { progress, ctx };
    FILE *f = file_open(local, "rb");
    int k;
    if (!f)
        return REMOTE_E_LOCAL;
    k = sftpc_put(r->sf, f, path, moved, &m);
    fclose(f);
    return done(r, k);
}

int remote_mkdir(struct remote *r, const char *path) { return done(r, sftpc_mkdir(r->sf, path)); }
int remote_rename(struct remote *r, const char *from, const char *to) { return done(r, sftpc_rename(r->sf, from, to)); }

int remote_remove(struct remote *r, const char *path, int dir)
{
    return done(r, dir ? sftpc_rmdir(r->sf, path) : sftpc_remove(r->sf, path));
}

const char *remote_error(int err)
{
    switch (err) {
    case REMOTE_OK: return "no error";
    case REMOTE_E_NAME: return "no computer of that name was found";
    case REMOTE_E_CONNECT: return "the computer did not answer";
    case REMOTE_E_HOSTKEY: return "the computer's key was not accepted";
    case REMOTE_E_LOGIN: return "the login was refused";
    case REMOTE_E_GONE: return "the connection ended";
    case REMOTE_E_MEMORY: return "not enough memory";
    case REMOTE_E_LOCAL: return "the file here couldn't be read or written";
    default: return "the other computer refused";
    }
}
