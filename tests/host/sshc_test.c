/* sdk/sshc against OpenSSH's sshd on the host (tests/host/sshc_test.sh starts it): the host key's fingerprint, a
 * login by key, a command's output, error and exit status, a pty's size, two channels at once, 1 MB each way, a
 * key refused, a host key refused; and a key written that OpenSSH reads (the script checks ssh-keygen -y).
 *
 *   sshc_test PORT USER KEY FINGERPRINT WORKDIR      (prints "sshc_test: N failures") */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "randombytes.h"
#include <glos/sftpc.h>
#include <glos/sshc.h>

void randombytes(void *p, long long n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(p, 1, (size_t)n, f) != (size_t)n)
        abort();
    fclose(f);
}

const char *randombytes_source(void) { return "urandom"; }

static int fails;

#define CHECK(c, ...) do { if (!(c)) { printf("sshc_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

struct io {
    int fd;
    char fp[64];
    int refuse_host;
};

struct out {
    char buf[2][4096];
    size_t n[2];
    unsigned long total[2];
    int status, eof, closed;
    char sig[32];
};

static void io_send(void *io, const uint8_t *d, uint32_t n)
{
    struct io *s = io;
    while (n) {
        ssize_t k = write(s->fd, d, n);
        if (k <= 0)
            return;
        d += k, n -= (uint32_t)k;
    }
}

static int io_hostkey(void *io, const uint8_t pk[32], const char *fp)
{
    struct io *s = io;
    (void)pk;
    snprintf(s->fp, sizeof s->fp, "%s", fp);
    return s->refuse_host ? -1 : 0;
}

static struct sftpc *sf;
static struct sshc_chan *sf_ch;

static void on_data(struct sshc_chan *ch, int stream, const uint8_t *d, uint32_t n)
{
    struct out *o = sshc_chan_user(ch);
    if (ch == sf_ch && sf && !stream) {
        sftpc_input(sf, d, n);
        return;
    }
    size_t k = n < sizeof o->buf[0] - 1 - o->n[stream] ? n : sizeof o->buf[0] - 1 - o->n[stream];
    memcpy(o->buf[stream] + o->n[stream], d, k);
    o->n[stream] += k;
    o->buf[stream][o->n[stream]] = 0;
    o->total[stream] += n;
}

static void on_eof(struct sshc_chan *ch) { ((struct out *)sshc_chan_user(ch))->eof = 1; }

static void on_chan_exit(struct sshc_chan *ch, int status, const char *sig)
{
    struct out *o = sshc_chan_user(ch);
    o->status = status;
    if (sig)
        snprintf(o->sig, sizeof o->sig, "%s", sig);
}

static void on_closed(struct sshc_chan *ch)
{
    struct out *o = sshc_chan_user(ch);
    if (o)
        o->closed = 1;
}

static const struct sshc_ops ops = { io_send, NULL, io_hostkey, NULL, on_data, on_eof, on_chan_exit, on_closed };

static int connect_to(unsigned port)
{
    struct sockaddr_in a;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons((unsigned short)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) {
        perror("connect");
        exit(2);
    }
    return fd;
}

/* Bytes from the server into the client, until done() or `ms` pass: 1 if done. */
static int pump(struct sshc *c, struct io *s, int (*done)(void *), void *arg, int ms)
{
    struct timespec t0, t;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (;;) {
        struct pollfd p = { s->fd, POLLIN, 0 };
        uint8_t buf[16384];
        long el;
        if (done(arg))
            return 1;
        clock_gettime(CLOCK_MONOTONIC, &t);
        el = (t.tv_sec - t0.tv_sec) * 1000 + (t.tv_nsec - t0.tv_nsec) / 1000000;
        if (el > ms)
            return 0;
        if (poll(&p, 1, 50) > 0) {
            ssize_t k = read(s->fd, buf, sizeof buf);
            if (k <= 0 || sshc_input(c, buf, (uint32_t)k) != 0)
                return done(arg);
        }
    }
}

static int st_auth(void *a) { return sshc_state(a) == SSHC_AUTH || sshc_state(a) == SSHC_DEAD; }
static int st_logged(void *a) { return sshc_state(a) != SSHC_LOGGING_IN; }
static int st_dead(void *a) { return sshc_state(a) == SSHC_DEAD; }
static int closed(void *a) { return ((struct out *)a)->closed; }
static int opened(void *a) { return sshc_chan_ok(a) != 0; }
static int drained(void *a) { return sshc_pending(a) <= 4 * 65536; }
struct two { struct out *a, *b; };
static int both_closed(void *a) { return ((struct two *)a)->a->closed && ((struct two *)a)->b->closed; }

/* sftpc's pump: the network once */
struct pumparg { struct sshc *c; struct io *s; };

static int never(void *a) { (void)a; return 0; }

static int sf_pump(void *arg, unsigned ms)
{
    struct pumparg *p = arg;
    pump(p->c, p->s, never, 0, (int)ms > 50 ? 50 : (int)ms);
    return sshc_state(p->c) == SSHC_DEAD ? -1 : 0;
}

static int count_entry(void *arg, const char *name, const char *longname, const struct sftpc_attrs *a)
{
    (void)longname;
    if (!strcmp(name, "up.bin") && a->has_size && a->size == 1048576)
        ++*(int *)arg;
    return 0;
}

static struct sshc *open_conn(struct io *s, unsigned port)
{
    struct sshc *c;
    s->fd = connect_to(port);
    c = sshc_new(&ops, s, NULL);
    pump(c, s, st_auth, c, 10000);
    return c;
}

static void close_conn(struct sshc *c, struct io *s)
{
    sshc_free(c);
    close(s->fd);
}

static struct sshc_chan *run(struct sshc *c, struct out *o, const char *term, const char *cmd)
{
    struct sshc_chan *ch;
    memset(o, 0, sizeof *o);
    o->status = -100;
    ch = sshc_session(c, term, 80, 24, cmd, NULL);
    if (ch)
        sshc_chan_set_user(ch, o);
    return ch;
}

int main(int argc, char **argv)
{
    struct io s;
    struct sshc *c;
    struct out o, o2;
    struct sshc_chan *ch;
    uint8_t sk[64], pk[32], sk2[64], pk2[32];
    char path[512];
    unsigned port;
    if (argc < 6) {
        fprintf(stderr, "usage: sshc_test PORT USER KEY FINGERPRINT WORKDIR\n");
        return 2;
    }
    port = (unsigned)atoi(argv[1]);

    /* a key made, written, read back (the script checks that OpenSSH reads it too) */
    CHECK(sshc_keygen(sk2, pk2) == 0, "keygen");
    snprintf(path, sizeof path, "%s/made.key", argv[5]);
    CHECK(sshc_key_write(path, sk2, pk2, "glos@test") == 0, "key write");
    CHECK(sshc_key_read(path, sk, pk) == 0 && !memcmp(sk, sk2, 64) && !memcmp(pk, pk2, 32), "key read back");
    CHECK(sshc_key_read(argv[3], sk, pk) == 0, "OpenSSH's key %s read", argv[3]);

    /* the host key and a login by key */
    memset(&s, 0, sizeof s);
    c = open_conn(&s, port);
    CHECK(sshc_state(c) == SSHC_AUTH, "ready for a login: state %d (%s)", sshc_state(c), sshc_why(c));
    CHECK(!strcmp(s.fp, argv[4]), "fingerprint %s, not %s", s.fp, argv[4]);
    sshc_login(c, argv[2], sk, pk, NULL);
    pump(c, &s, st_logged, c, 10000);
    CHECK(sshc_state(c) == SSHC_READY, "login by key: state %d (%s)", sshc_state(c), sshc_why(c));

    /* a command: stdout, stderr, the exit status */
    ch = run(c, &o, NULL, "echo hello; echo trouble >&2; exit 3");
    pump(c, &s, closed, &o, 10000);
    CHECK(o.closed && !strcmp(o.buf[0], "hello\n") && !strcmp(o.buf[1], "trouble\n") && o.status == 3,
          "exec: closed %d out %s err %s status %d", o.closed, o.buf[0], o.buf[1], o.status);

    /* a pty of 80 by 24 */
    ch = run(c, &o, "xterm", "stty size; echo $TERM");
    pump(c, &s, closed, &o, 10000);
    CHECK(strstr(o.buf[0], "24 80") && strstr(o.buf[0], "xterm"), "pty: %s", o.buf[0]);

    /* two at once */
    {
        struct two t = { &o, &o2 };
        run(c, &o, NULL, "sleep 1; echo one");
        run(c, &o2, NULL, "echo two");
        pump(c, &s, both_closed, &t, 10000);
        CHECK(!strcmp(o.buf[0], "one\n") && !strcmp(o2.buf[0], "two\n"), "two channels: %s / %s", o.buf[0], o2.buf[0]);
    }

    /* 1 MB down, 1 MB up */
    ch = run(c, &o, NULL, "head -c 1048576 /dev/zero");
    pump(c, &s, closed, &o, 30000);
    CHECK(o.total[0] == 1048576 && o.status == 0, "1 MB down: %lu", o.total[0]);
    ch = run(c, &o, NULL, "wc -c");
    {
        static uint8_t mb[65536];
        int i;
        pump(c, &s, opened, ch, 10000);
        for (i = 0; i < 16; i++) {
            sshc_write(ch, mb, sizeof mb);
            while (sshc_pending(ch) > 4 * sizeof mb)
                pump(c, &s, drained, ch, 1000);
        }
        sshc_eof(ch);
        pump(c, &s, closed, &o, 30000);
        CHECK(atoi(o.buf[0]) == 1048576, "1 MB up: wc says %s", o.buf[0]);
    }
    /* SFTP: 1 MB put and got back, ls, stat, rename, mkdir, rmdir, remove */
    {
        static uint8_t data[1048576], back[1048576];
        struct pumparg pa = { c, &s };
        struct sftpc_attrs a;
        char where[512], rp[512];
        FILE *f;
        int i, seen = 0;
        for (i = 0; i < (int)sizeof data; i++)
            data[i] = (uint8_t)(i * 2654435761u >> 13);
        snprintf(path, sizeof path, "%s/local.bin", argv[5]);
        f = fopen(path, "wb");
        fwrite(data, 1, sizeof data, f);
        fclose(f);
        memset(&o, 0, sizeof o);
        sf_ch = sshc_session(c, NULL, 0, 0, NULL, "sftp");
        sshc_chan_set_user(sf_ch, &o);
        sf = sftpc_new(sf_ch, sf_pump, &pa);
        CHECK(sftpc_start(sf, 10000) == 0, "sftp start: %s", sftpc_why(sf));
        CHECK(sftpc_realpath(sf, argv[5], rp, sizeof rp) == 0, "realpath: %s", sftpc_why(sf));
        snprintf(where, sizeof where, "%s/up.bin", rp);
        f = fopen(path, "rb");
        CHECK(sftpc_put(sf, f, where, NULL, NULL) == 0, "put: %s", sftpc_why(sf));
        fclose(f);
        CHECK(sftpc_stat(sf, where, &a) == 0 && a.has_size && a.size == sizeof data, "stat: %s size %llu",
              sftpc_why(sf), (unsigned long long)a.size);
        CHECK(sftpc_ls(sf, rp, count_entry, &seen) == 0 && seen == 1, "ls: %s seen %d", sftpc_why(sf), seen);
        snprintf(path, sizeof path, "%s/got.bin", argv[5]);
        f = fopen(path, "wb");
        CHECK(sftpc_get(sf, where, f, NULL, NULL) == 0, "get: %s", sftpc_why(sf));
        fclose(f);
        f = fopen(path, "rb");
        CHECK(fread(back, 1, sizeof back, f) == sizeof back && fgetc(f) == EOF && !memcmp(back, data, sizeof data),
              "got back the same bytes");
        fclose(f);
        snprintf(path, sizeof path, "%s/sub", rp);
        CHECK(sftpc_mkdir(sf, path) == 0 && sftpc_stat(sf, path, &a) == 0 && (a.perm & 0170000) == 040000,
              "mkdir: %s", sftpc_why(sf));
        CHECK(sftpc_rmdir(sf, path) == 0, "rmdir: %s", sftpc_why(sf));
        snprintf(path, sizeof path, "%s/moved.bin", rp);
        CHECK(sftpc_rename(sf, where, path) == 0 && sftpc_stat(sf, where, &a) != 0, "rename: %s", sftpc_why(sf));
        CHECK(sftpc_remove(sf, path) == 0 && sftpc_stat(sf, path, &a) != 0, "remove: %s", sftpc_why(sf));
        CHECK(sftpc_get(sf, "/no/such/file", stdout, NULL, NULL) != 0 && strstr(sftpc_why(sf), "No such file"),
              "a missing file: %s", sftpc_why(sf));
        sftpc_free(sf);
        sf = 0;
        sshc_close(sf_ch);
        sf_ch = 0;
    }
    close_conn(c, &s);

    /* a key the server doesn't know: refused, with the server's methods */
    c = open_conn(&s, port);
    sshc_login(c, argv[2], sk2, pk2, NULL);
    pump(c, &s, st_logged, c, 10000);
    CHECK(sshc_state(c) == SSHC_AUTH && strstr(sshc_why(c), "denied") && strstr(sshc_methods(c), "publickey"),
          "unknown key: state %d why %s methods %s", sshc_state(c), sshc_why(c), sshc_methods(c));
    close_conn(c, &s);

    /* the host key refused */
    memset(&s, 0, sizeof s);
    s.refuse_host = 1;
    s.fd = connect_to(port);
    c = sshc_new(&ops, &s, NULL);
    pump(c, &s, st_dead, c, 10000);
    CHECK(sshc_state(c) == SSHC_DEAD && strstr(sshc_why(c), "host key refused"), "host key refused: %s",
          sshc_why(c));
    close_conn(c, &s);

    /* known hosts */
    snprintf(path, sizeof path, "%s/KNOWNHST.TXT", argv[5]);
    remove(path);
    CHECK(sshc_known(path, "example.org", 22, pk2) == -2, "no file");
    CHECK(sshc_known_add(path, "example.org", 22, pk2) == 0 && sshc_known_add(path, "example.org", 2222, pk) == 0,
          "known add");
    CHECK(sshc_known(path, "example.org", 22, pk2) == 1, "known");
    CHECK(sshc_known(path, "EXAMPLE.org", 22, pk2) == 1, "known, the case aside");
    CHECK(sshc_known(path, "example.org", 22, pk) == -1, "changed");
    CHECK(sshc_known(path, "example.org", 2222, pk) == 1, "known on another port");
    CHECK(sshc_known(path, "example.net", 22, pk) == 0, "unknown");

    printf("sshc_test: %d failures\n", fails);
    return fails != 0;
}
