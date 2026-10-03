/* GLOS's SSH layer (kernel/ssh) on host sockets, for OpenSSH's client
 * (tests/host/sshd_test.sh; milestones-m0-m4.md M3 item 6). One process,
 * poll(), any number of connections. Commands, as exec requests:
 *   echo ARGS     ARGS and a newline
 *   exit N        exit status N
 *   err TEXT      TEXT on stderr, exit status 3
 *   cat           stdin back until EOF
 *   big N         N bytes (a pattern), produced as the client's window allows
 *   sleep MS      "slept" after MS milliseconds
 * The sftp subsystem is refused until M3 item 9.
 * Usage: sshd PORT HOSTKEY AUTHKEYS (prints "ready" when listening). */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "randombytes.h"
#include "ssh.h"
#include "sshbuf.h"

#define MAXCLIENTS 32

struct client {
    int fd;
    struct ssh_conn *conn;
    uint8_t *out;
    size_t out_len, out_cap;
};

struct job {                                    /* a channel's command (ssh_chan_user) */
    int kind;                                   /* 1 cat, 2 big, 3 sleep */
    unsigned long long left;
    long long due_ms;
    struct ssh_chan *ch;
};

static struct client clients[MAXCLIENTS];
static struct job *jobs[MAXCLIENTS * SSH_MAX_CHANNELS];

void randombytes(void *p, long long n)
{
    static int fd = -1;
    if (fd < 0)
        fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, p, (size_t)n) != n)
        abort();
}

const char *randombytes_source(void) { return "urandom"; }

static long long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void *x_alloc(size_t n) { return calloc(1, n); }
static void x_free(void *p) { free(p); }

static void x_send(void *io, const uint8_t *d, uint32_t n)
{
    struct client *cl = io;
    if (cl->out_len + n > cl->out_cap) {
        cl->out_cap = (cl->out_len + n) * 2;
        cl->out = realloc(cl->out, cl->out_cap);
    }
    memcpy(cl->out + cl->out_len, d, n);
    cl->out_len += n;
}

static void x_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "sshd: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

static void add_job(struct job *j)
{
    size_t i;
    for (i = 0; i < sizeof jobs / sizeof jobs[0]; i++)
        if (!jobs[i]) {
            jobs[i] = j;
            return;
        }
}

static void drop_job(struct job *j)
{
    size_t i;
    for (i = 0; i < sizeof jobs / sizeof jobs[0]; i++)
        if (jobs[i] == j)
            jobs[i] = NULL;
    free(j);
}

static int x_exec(struct ssh_chan *ch, const char *cmd)
{
    struct job *j;
    x_log("exec %s", cmd);
    if (!strncmp(cmd, "echo ", 5) || !strcmp(cmd, "echo")) {
        const char *a = cmd[4] ? cmd + 5 : "";
        ssh_chan_write(ch, 0, (const uint8_t *)a, (uint32_t)strlen(a));
        ssh_chan_write(ch, 0, (const uint8_t *)"\n", 1);
        ssh_chan_exit(ch, 0);
        return 0;
    }
    if (!strncmp(cmd, "exit ", 5)) {
        ssh_chan_exit(ch, (uint32_t)atoi(cmd + 5));
        return 0;
    }
    if (!strncmp(cmd, "err ", 4)) {
        ssh_chan_write(ch, 1, (const uint8_t *)cmd + 4, (uint32_t)strlen(cmd + 4));
        ssh_chan_write(ch, 1, (const uint8_t *)"\n", 1);
        ssh_chan_exit(ch, 3);
        return 0;
    }
    j = calloc(1, sizeof *j);
    j->ch = ch;
    if (!strcmp(cmd, "cat")) {
        j->kind = 1;
    } else if (!strncmp(cmd, "big ", 4)) {
        j->kind = 2;
        j->left = strtoull(cmd + 4, NULL, 10);
    } else if (!strncmp(cmd, "sleep ", 6)) {
        j->kind = 3;
        j->due_ms = now_ms() + atoll(cmd + 6);
    } else {
        free(j);
        ssh_chan_write(ch, 1, (const uint8_t *)"unknown command\n", 16);
        ssh_chan_exit(ch, 127);
        return 0;
    }
    ssh_chan_set_user(ch, j);
    add_job(j);
    return 0;
}

static int x_subsystem(struct ssh_chan *ch, const char *name)
{
    (void)ch;
    x_log("subsystem %s refused", name);
    return -1;
}

static void x_data(struct ssh_chan *ch, const uint8_t *d, uint32_t n)
{
    struct job *j = ssh_chan_user(ch);
    if (j && j->kind == 1)
        ssh_chan_write(ch, 0, d, n);
}

static void x_eof(struct ssh_chan *ch)
{
    struct job *j = ssh_chan_user(ch);
    if (j && j->kind == 1) {
        ssh_chan_exit(ch, 0);
        drop_job(j);
        ssh_chan_set_user(ch, NULL);
    }
}

static void x_gone(struct ssh_chan *ch)
{
    struct job *j = ssh_chan_user(ch);
    if (j)
        drop_job(j);
}

static const struct ssh_ops ops = { x_alloc, x_free, x_send, x_log, x_exec, x_subsystem, x_data, x_eof, x_gone };

/* The jobs that produce output over time. */
static void run_jobs(void)
{
    static uint8_t pattern[8192];
    size_t i;
    if (!pattern[1])
        for (i = 0; i < sizeof pattern; i++)
            pattern[i] = (uint8_t)("0123456789abcdef\n"[i % 17]);
    for (i = 0; i < sizeof jobs / sizeof jobs[0]; i++) {
        struct job *j = jobs[i];
        if (!j)
            continue;
        if (j->kind == 2) {
            while (j->left && ssh_chan_pending(j->ch) < 65536) {
                uint32_t k = j->left > sizeof pattern ? sizeof pattern : (uint32_t)j->left;
                k = ssh_chan_write(j->ch, 0, pattern, k);
                if (!k)
                    break;
                j->left -= k;
            }
            if (!j->left) {
                ssh_chan_exit(j->ch, 0);
                ssh_chan_set_user(j->ch, NULL);
                drop_job(j);
            }
        } else if (j->kind == 3 && now_ms() >= j->due_ms) {
            ssh_chan_write(j->ch, 0, (const uint8_t *)"slept\n", 6);
            ssh_chan_exit(j->ch, 0);
            ssh_chan_set_user(j->ch, NULL);
            drop_job(j);
        }
    }
}

static char *slurp(const char *path, uint32_t *n)
{
    FILE *f = fopen(path, "rb");
    char *b = calloc(1, 65536);
    if (!f) {
        perror(path);
        exit(2);
    }
    *n = (uint32_t)fread(b, 1, 65535, f);
    fclose(f);
    return b;
}

static void close_client(struct client *cl)
{
    ssh_conn_free(cl->conn);
    close(cl->fd);
    free(cl->out);
    memset(cl, 0, sizeof *cl);
    cl->fd = -1;
}

int main(int argc, char **argv)
{
    static struct ssh_server srv;
    struct sockaddr_in sa;
    struct pollfd pfd[MAXCLIENTS + 1];
    uint32_t n;
    char *t;
    int ls, one = 1, i;

    if (argc != 4) {
        fprintf(stderr, "usage: sshd PORT HOSTKEY AUTHKEYS\n");
        return 2;
    }
    t = slurp(argv[2], &n);
    if (ssh_parse_privkey(t, n, srv.host_sk, srv.host_pk) != 0) {
        fprintf(stderr, "sshd: bad host key %s\n", argv[2]);
        return 2;
    }
    free(t);
    t = slurp(argv[3], &n);
    srv.n_authkeys = ssh_parse_authkeys(t, n, srv.authkeys, SSH_MAX_AUTHKEYS);
    free(t);
    srv.ops = &ops;
    srv.version = "SSH-2.0-GLOS_hosttest";
    for (i = 0; i < MAXCLIENTS; i++)
        clients[i].fd = -1;

    ls = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((uint16_t)atoi(argv[1]));
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(ls, (struct sockaddr *)&sa, sizeof sa) != 0 || listen(ls, 16) != 0) {
        perror("bind");
        return 2;
    }
    printf("ready authkeys=%d\n", srv.n_authkeys);
    fflush(stdout);

    for (;;) {
        int np = 0;
        pfd[np].fd = ls;
        pfd[np++].events = POLLIN;
        for (i = 0; i < MAXCLIENTS; i++) {
            pfd[np].fd = clients[i].fd;
            pfd[np++].events = clients[i].fd < 0 ? 0 : (short)(POLLIN | (clients[i].out_len ? POLLOUT : 0));
        }
        poll(pfd, (nfds_t)np, 20);
        if (pfd[0].revents & POLLIN) {
            int fd = accept(ls, NULL, NULL);
            for (i = 0; i < MAXCLIENTS && clients[i].fd >= 0; i++) ;
            if (fd >= 0 && i < MAXCLIENTS) {
                setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
                clients[i].fd = fd;
                clients[i].conn = ssh_conn_new(&srv, &clients[i]);
            } else if (fd >= 0) {
                close(fd);
            }
        }
        for (i = 0; i < MAXCLIENTS; i++) {
            struct client *cl = &clients[i];
            short re = pfd[i + 1].revents;
            if (cl->fd < 0)
                continue;
            if (re & POLLIN) {
                uint8_t buf[65536];
                ssize_t k = read(cl->fd, buf, sizeof buf);
                if (k <= 0 || ssh_conn_input(cl->conn, buf, (uint32_t)k) != 0) {
                    if (cl->out_len)            /* a last DISCONNECT */
                        (void)!write(cl->fd, cl->out, cl->out_len);
                    close_client(cl);
                    continue;
                }
            }
            if ((re & POLLOUT) && cl->out_len) {
                ssize_t k = write(cl->fd, cl->out, cl->out_len);
                if (k > 0) {
                    memmove(cl->out, cl->out + k, cl->out_len - (size_t)k);
                    cl->out_len -= (size_t)k;
                }
            }
        }
        run_jobs();
    }
}
