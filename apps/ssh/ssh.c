/* ssh, sftp and ssh-keygen (M10 step D, PRD D61; programs since S2c, with cli.c
 * their terminal): sdk/sshc over a connection of GLOS's (NET); a session with a
 * pty passes its bytes to the terminal as they are; the questions (a new host
 * key, a password) are asked a line at a time.
 *
 *   ssh [-p PORT] [-i KEY] [-l USER] [-L [ADDRESS:]PORT:HOST:HOSTPORT]... [-g] [-N] [USER@]HOST [COMMAND]
 *                              HOST can be a saved session's name (SESSIONS.CFG beside the key, kept by the
 *                              Terminal's Sessions: apps/shared/sessions.h): its computer, port, user, key,
 *                              command, tunnels and keep-alive are taken, each unless the command line says
 *                              its own. -o ServerAliveInterval=SECONDS: a packet that says nothing after so
 *                              long a quiet, for a connection that something on the way would forget.
 *                              -sessionlog FILE (a session's `log`): what the session writes is added to
 *                              that file as text (apps/shared/sesslog.h).
 *                              -L: a tunnel: connections to PORT of this PC go through the session and come
 *                              out of the other computer to HOST:HOSTPORT (up to four -L's; six connections
 *                              through them at a time). The port is this PC's programs' alone (it is
 *                              listened on at 127.0.0.1: API 2.9's GLOS_NETF_LOCAL) unless -g opens the
 *                              tunnels to every computer that reaches this one, or ADDRESS says for one:
 *                              * (every computer) or 127.0.0.1 (this PC).
 *                              -N: no shell or command, the tunnels alone (Ctrl+C ends it)
 *   sftp [-P PORT] [-i KEY] [USER@]HOST
 *   ssh-keygen [-f FILE] [-C COMMENT]
 *
 * Keys and known hosts are in SSH\ beside the program (C:\GLOS\SSH; GLOSSSH in
 * the environment names another): ID_ED25 and ID_ED25.PUB, KNOWNHST.TXT. A
 * session's network is read whenever its connection has news (GLOS_EV_NET) or
 * the terminal's tick comes; sftp's requests wait for their answers there and
 * then. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glos/cp.h>
#include <glos/glos.h>
#ifdef __DJGPP__
#include <glos/net.h>
#else
#include <arpa/inet.h>                          /* (the host's build, for the goldens: inet_aton alone) */
#endif
#include <glos/sftpc.h>
#include <glos/sshc.h>

#include "cli.h"
#include "../shared/sessions.h"
#include "../shared/sesslog.h"

enum { S_OFF, S_CONNECTED, S_ASK_HOST, S_ASK_PASSWORD, S_LOGGING_IN, S_OPENING, S_RUNNING, S_SFTP };
#define NFWD 4                          /* -L's */
#define NPIPE 6                         /* connections through them at once (the client has eight channels) */

static struct {
    int stage;
    glos_handle conn;                   /* the GLOS connection */
    struct sshc *c;
    struct sshc_chan *ch;
    struct sftpc *sf;
    int sftp;                           /* sftp, not ssh */
    int screen;                         /* the xterm screen shows (a pty) */
    char host[256], user[64], cmd[512], keyfile[260], known[260];
    unsigned port;
    uint8_t host_pk[32];
    char fp[64];
    int host_new, host_ok;
    uint8_t sk[64], pk[32];
    int have_key, tries;
    int status;
    int accepted;                       /* (the channel's requests taken when it closed) */
    char sig[32];
    char rcwd[512];                     /* sftp's folder there */
    /* The tunnels (-L): each listens on a port of this PC; a connection to it is a "direct-tcpip" channel's. */
    struct {
        unsigned port, hport;
        char host[80];
        int open;                       /* 1: every computer may connect to it, 0: this PC alone, -1: as -g says */
        glos_handle listener;
    } fwd[NFWD];
    int nfwd, no_shell, gateway;
    unsigned keepalive, last_io;        /* a session's: seconds of quiet before a packet that says nothing */
    char title[24];                     /* a saved session's name: the terminal's title, not the host */
    char log[100];                      /* the session's log's file ("": none) */
    struct {
        glos_handle sock;               /* (0: free) */
        struct sshc_chan *ch;
        int sock_ended;
    } pipe[NPIPE];
} s;

static int log_screens = -1;                /* (SSHLOG=1 in the environment: the tests') */

/* -log: where a session has got to, on stdout as it happens ("SSH| ...": the suite waits for them). */
static void milestone(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void milestone(const char *fmt, ...)
{
    va_list ap;
    if (log_screens < 0)
        log_screens = getenv("SSHLOG") != NULL;
    if (!log_screens)
        return;
    printf("SSH| ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- where things are */

static void ssh_dir(char *out, size_t room)
{
    const char *e = getenv("GLOSSSH");
    if (e && *e) {
        snprintf(out, room, "%s", e);
        return;
    }
    snprintf(out, room, "%s", term_home());
    strncat(out, "\\SSH", room - strlen(out) - 1);
}

static void say(const char *s1) { term_print(s1); }

static void err(const char *s1)
{
    term_attr(T_ERROR);
    term_print(s1);
    term_attr(T_NORMAL);
}

/* ---- the network */

static void net_send(void *io, const uint8_t *d, uint32_t n)
{
    (void)io;
    s.last_io = glos_ticks();
    while (n && s.conn) {
        int k = glos_net_send(s.conn, d, n, 10000);
        if (k <= 0)
            break;
        d += k, n -= (uint32_t)k;
    }
}

/* What has come: into the client. -1 once the connection is gone. */
static int net_read(unsigned wait_ms)
{
    uint8_t buf[8192];
    unsigned st;
    int k, first = 1;
    if (!s.conn || !s.c)
        return -1;
    for (;;) {
        k = glos_net_receive(s.conn, buf, sizeof buf, first ? wait_ms : 0, &st);
        first = 0;
        if (k < 0 || (k == 0 && st == GLOS_NETS_CLOSED))
            return -1;
        if (k == 0)
            return 0;
        s.last_io = glos_ticks();
        if (sshc_input(s.c, buf, (uint32_t)k) != 0)
            return -1;
    }
}

static int pump(void *arg, unsigned ms)
{
    (void)arg;
    return net_read(ms > 100 ? 100 : ms);
}

/* ---- the client's callbacks */

static int on_hostkey(void *io, const uint8_t pk[32], const char *fp)
{
    int k;
    (void)io;
    memcpy(s.host_pk, pk, 32);
    snprintf(s.fp, sizeof s.fp, "%s", fp);
    k = sshc_known(s.known, s.host, s.port, pk);
    if (k == -1) {
        err("@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\r\n"
            "@    WARNING: REMOTE HOST IDENTIFICATION HAS CHANGED!     @\r\n"
            "@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\r\n");
        term_printf("The ED25519 key %s sent is %s,\r\nnot the one in %s. Refused.\r\n", s.host, fp, s.known);
        milestone("host-key-changed");
        return -1;
    }
    s.host_new = k != 1;
    s.host_ok = k == 1;
    return 0;                                   /* (a new one: asked about before the login) */
}

static void on_banner(void *io, const char *text)
{
    (void)io;
    term_write(text, -1);
}

static int pipe_of(struct sshc_chan *ch)        /* the tunnel's connection this channel carries, or -1 */
{
    int i;
    for (i = 0; i < NPIPE; i++)
        if (s.pipe[i].sock && s.pipe[i].ch == ch)
            return i;
    return -1;
}

static void pipe_end(int i)
{
    if (s.pipe[i].sock)
        glos_net_close(s.pipe[i].sock, 0);
    s.pipe[i].sock = 0, s.pipe[i].ch = 0, s.pipe[i].sock_ended = 0;
}

static void on_data(struct sshc_chan *ch, int stream, const uint8_t *d, uint32_t n)
{
    int i = pipe_of(ch);
    if (i >= 0) {                               /* a tunnel's: to the connection on this PC */
        while (n) {
            int k = glos_net_send(s.pipe[i].sock, d, n, 10000);
            if (k <= 0) {
                sshc_close(ch);
                break;
            }
            d += k, n -= (uint32_t)k;
        }
        return;
    }
    if (ch != s.ch)
        return;
    if (s.sf && !stream) {
        sftpc_input(s.sf, d, n);
        return;
    }
    sesslog_write(d, (int)n);                   /* (the session's log, if it has one) */
    if (s.screen) {                             /* (a pty: the terminal has the bytes as they are) */
        term_raw((const char *)d, (int)n);
        return;
    }
    if (stream)
        term_attr(T_ERROR);
    term_write((const char *)d, (int)n);
    term_attr(T_NORMAL);
}

static void on_exit_status(struct sshc_chan *ch, int status, const char *sig)
{
    (void)ch;
    s.status = status;
    if (sig)
        snprintf(s.sig, sizeof s.sig, "%s", sig);
}

static void on_eof(struct sshc_chan *ch)       /* the far side has no more: a tunnel's connection here is closed */
{
    int i = pipe_of(ch);
    if (i >= 0) {
        glos_net_close(s.pipe[i].sock, 0);      /* (what was sent is sent first) */
        s.pipe[i].sock = 0;
        sshc_close(ch);
    }
}

static void on_closed(struct sshc_chan *ch)
{
    int i;
    for (i = 0; i < NPIPE; i++)
        if (s.pipe[i].ch == ch) {
            pipe_end(i);
            milestone("tunnel-closed");
        }
    if (ch == s.ch) {
        s.accepted = sshc_chan_ok(ch) == 1;     /* (a quick command may open, run and close between two polls) */
        s.ch = 0;
    }
}

static const struct sshc_ops ops = { net_send, NULL, on_hostkey, on_banner, on_data, on_eof, on_exit_status, on_closed };

/* ---- the tunnels (-L) */

static void tunnels_listen(void)                /* logged in: each -L's port of this PC listened on */
{
    struct glos_version v;
    int i, own;
    memset(&v, 0, sizeof v);
    v.size = sizeof v;
    glos_version(&v);
    own = v.major > 2 || (v.major == 2 && v.minor >= 9);       /* (a GLOS before 2.9 listens for everyone) */
    for (i = 0; i < s.nfwd; i++) {
        int open = s.fwd[i].open < 0 ? s.gateway : s.fwd[i].open;
        if (!open && !own) {
            term_printf("ssh: port %u: this GLOS has no ports for this PC alone (-g opens a tunnel to every computer)\r\n",
                        s.fwd[i].port);
            continue;
        }
        s.fwd[i].listener = glos_net_listen(s.fwd[i].port, 4, GLOS_NETF_EVENTS | (open ? 0 : GLOS_NETF_LOCAL));
        if (!s.fwd[i].listener)
            term_printf("ssh: port %u of this PC can't be listened on\r\n", s.fwd[i].port);
        else
            milestone("tunnel %u %s:%u %s", s.fwd[i].port, s.fwd[i].host, s.fwd[i].hport, open ? "open" : "own");
    }
}

static void tunnels_poll(void)                  /* new connections taken; what each has to send, sent */
{
    static uint8_t buf[4096];
    unsigned st, addr, port;
    int i, k, f;
    if (!s.c || sshc_state(s.c) != SSHC_READY)
        return;
    for (f = 0; f < s.nfwd; f++) {
        glos_handle h;
        while (s.fwd[f].listener && (h = glos_net_accept(s.fwd[f].listener, 0, GLOS_NETF_EVENTS, &addr, &port)) != 0) {
            for (i = 0; i < NPIPE && (s.pipe[i].sock || s.pipe[i].ch); i++)
                ;
            if (i == NPIPE || !(s.pipe[i].ch = sshc_direct(s.c, s.fwd[f].host, s.fwd[f].hport, "127.0.0.1", port))) {
                glos_net_close(h, 1);           /* (no room for one more: refused) */
                continue;
            }
            s.pipe[i].sock = h, s.pipe[i].sock_ended = 0;
            milestone("tunnel-open %u", s.fwd[f].port);
        }
    }
    for (i = 0; i < NPIPE; i++) {
        if (!s.pipe[i].sock || !s.pipe[i].ch || s.pipe[i].sock_ended)
            continue;
        while (sshc_pending(s.pipe[i].ch) < 32768) {    /* (no further ahead of the far side than that) */
            k = glos_net_receive(s.pipe[i].sock, buf, sizeof buf, 0, &st);
            if (k > 0) {
                sshc_write(s.pipe[i].ch, buf, (uint32_t)k);
                continue;
            }
            if (k < 0 || st == GLOS_NETS_FAILED) {      /* this PC's side broke off: so does the far one */
                s.pipe[i].sock_ended = 1;
                sshc_close(s.pipe[i].ch);
            } else if (st == GLOS_NETS_CLOSED) {        /* it has no more to send: said to the far side */
                s.pipe[i].sock_ended = 1;
                sshc_eof(s.pipe[i].ch);
            }
            break;
        }
    }
}

static void tunnels_end(void)
{
    int i;
    for (i = 0; i < s.nfwd; i++)
        if (s.fwd[i].listener)
            glos_net_close(s.fwd[i].listener, 1);
    for (i = 0; i < NPIPE; i++)
        pipe_end(i);
}

/* ---- the session's stages */

static void finish(int code)
{
    if (s.screen) {
        s.screen = 0;
    }
    tunnels_end();
    sesslog_close();
    if (s.sf)
        sftpc_free(s.sf);
    if (s.c)
        sshc_free(s.c);
    if (s.conn)
        glos_net_close(s.conn, 0);
    memset(s.sk, 0, sizeof s.sk);
    s.sf = 0;
    s.c = 0;
    s.ch = 0;
    s.conn = 0;
    s.stage = S_OFF;
    term_title("");
    milestone("end code=%d", code);
    shell_resume(code);
}

static void ask_password(void)
{
    char p[300];
    snprintf(p, sizeof p, "%s@%s's password: ", s.user, s.host);
    s.stage = S_ASK_PASSWORD;
    say(p);
    term_input_hidden(1);
    term_prompt_done();
    milestone("ask-password");
}

static void open_channel(void)
{
    int cols, rows;
    term_size(&cols, &rows);
    s.screen = !s.sftp && !s.cmd[0];
    s.ch = s.sftp ? sshc_session(s.c, NULL, 0, 0, NULL, "sftp")
                  : sshc_session(s.c, s.screen ? "xterm" : NULL, (unsigned)cols, (unsigned)rows, s.cmd[0] ? s.cmd : NULL,
                                 NULL);
    s.stage = S_OPENING;
    s.status = -1;
    s.accepted = 0;
    s.sig[0] = 0;
    if (!s.ch) {
        err("ssh: no channel\r\n");
        finish(255);
    }
}

static void sftp_prompt(void)
{
    say("sftp> ");
    term_prompt_done();
    milestone("sftp-ready");
}

/* The client has moved on: the next stage. */
static void step(void)
{
    switch (s.stage) {
    case S_CONNECTED:
        if (sshc_state(s.c) == SSHC_DEAD) {
            term_printf("ssh: %s: %s\r\n", s.host, sshc_why(s.c));
            finish(255);
        } else if (sshc_state(s.c) == SSHC_AUTH) {
            if (s.host_new) {
                term_printf("The authenticity of host '%s' can't be established.\r\nED25519 key fingerprint is %s.\r\n",
                            s.host, s.fp);
                say("Are you sure you want to continue connecting (yes/no)? ");
                s.stage = S_ASK_HOST;
                term_prompt_done();
                milestone("ask-host %s", s.fp);
            } else {
                s.stage = S_LOGGING_IN;
                sshc_login(s.c, s.user, s.have_key ? s.sk : NULL, s.have_key ? s.pk : NULL, NULL);
            }
        }
        break;
    case S_LOGGING_IN:
        if (sshc_state(s.c) == SSHC_READY) {
            milestone("logged-in");
            tunnels_listen();
            if (s.no_shell) {                   /* -N: the tunnels alone, until Ctrl+C */
                term_printf("The tunnels are open. Ctrl+C ends them.\r\n");
                s.stage = S_RUNNING;
            } else {
                open_channel();
            }
        } else if (sshc_state(s.c) == SSHC_AUTH) {      /* refused */
            const char *m = sshc_methods(s.c);
            if ((strstr(m, "password") || strstr(m, "keyboard-interactive")) && s.tries < 3) {
                s.tries++;
                ask_password();
            } else {
                term_printf("%s@%s: %s.\r\n", s.user, s.host, sshc_why(s.c));
                finish(255);
            }
        } else if (sshc_state(s.c) == SSHC_DEAD) {
            term_printf("ssh: %s: %s\r\n", s.host, sshc_why(s.c));
            finish(255);
        }
        break;
    case S_OPENING:
        if (!s.ch && s.accepted && !s.sftp) {   /* (opened, run and closed already: its end, as S_RUNNING's) */
            s.stage = S_RUNNING;
            step();
        } else if (!s.ch || sshc_chan_ok(s.ch) < 0) {
            term_printf("ssh: %s: %s refused\r\n", s.host, s.sftp ? "the sftp subsystem" : "the session");
            finish(255);
        } else if (sshc_chan_ok(s.ch) == 1) {
            if (s.sftp) {
                s.sf = sftpc_new(s.ch, pump, NULL);
                if (!s.sf || sftpc_start(s.sf, 15000) != 0 || sftpc_realpath(s.sf, ".", s.rcwd, sizeof s.rcwd) != 0) {
                    term_printf("sftp: %s\r\n", s.sf ? sftpc_why(s.sf) : "out of memory");
                    finish(255);
                    return;
                }
                term_printf("Connected to %s.\r\n", s.host);
                s.stage = S_SFTP;
                sftp_prompt();
            } else {
                s.stage = S_RUNNING;
            }
        }
        break;
    case S_RUNNING:
        if (!s.ch && !s.no_shell) {
            int code = s.status >= 0 ? s.status : 255;
            if (s.screen) {
                        s.screen = 0;
                    }
            if (!s.cmd[0])
                term_printf("Connection to %s closed.\r\n", s.host);
            if (s.sig[0])
                term_printf("ssh: the command ended by the signal %s\r\n", s.sig);
            finish(code);
        }
        break;
    }
}

void ssh_poll(void)
{
    if (s.stage == S_OFF)
        return;
    if (net_read(0) < 0) {                      /* the connection is gone (or broken) */
        if (s.stage == S_RUNNING && !s.ch) {    /* (after the session's own end: as that) */
            step();
            return;
        }
        if (s.screen) {
                s.screen = 0;
            }
        term_printf("%s: %s: %s\r\n", s.sftp ? "sftp" : "ssh", s.host,
                    s.c && sshc_why(s.c)[0] ? sshc_why(s.c) : "the connection was closed");
        finish(255);
        return;
    }
    tunnels_poll();
    if (s.keepalive && s.c && sshc_state(s.c) == SSHC_READY && glos_ticks() - s.last_io >= s.keepalive * 1000u) {
        sshc_keepalive(s.c);                    /* (which is sent: last_io is now) */
        milestone("keepalive");
    }
    if (s.stage != S_ASK_HOST && s.stage != S_ASK_PASSWORD && s.stage != S_SFTP)
        step();
}

/* ---- starting */

static int parse_target(const char *t, char *user, size_t uroom, char *host, size_t hroom)
{
    const char *at = strrchr(t, '@');
    if (at) {
        snprintf(user, uroom, "%.*s", (int)(at - t), t);
        t = at + 1;
    }
    snprintf(host, hroom, "%s", t);
    return *host ? 0 : -1;
}

static int connect_to(void)
{
    unsigned addr;
    struct in_addr ia;
    int e;
    if (inet_aton(s.host, &ia)) {
        addr = (unsigned)ia.s_addr;
    } else if ((e = glos_net_resolve(s.host, 15000, &addr)) != 0) {
        term_printf("ssh: Could not resolve hostname %s: %s\r\n", s.host,
                    e == GLOS_E_NET_DOWN ? "no network" : "Name or service not known");
        return -1;
    }
    if (!(s.conn = glos_net_connect(addr, s.port, 15000, GLOS_NETF_EVENTS))) {
        e = glos_error();
        term_printf("ssh: connect to host %s port %u: %s\r\n", s.host, s.port, e == GLOS_E_NET_REFUSED
                    ? "Connection refused" : e == GLOS_E_NET_TIMEOUT ? "Connection timed out" : "No route to host");
        s.conn = 0;
        return -1;
    }
    return 0;
}

static int add_tunnel(const char *spec, int len)        /* one -L's words: 0, or -1 (not a tunnel, or one too many) */
{
    if (s.nfwd == NFWD || session_tunnel(spec, len, &s.fwd[s.nfwd].open, &s.fwd[s.nfwd].port, s.fwd[s.nfwd].host,
                                         sizeof s.fwd[0].host, &s.fwd[s.nfwd].hport) != 0)
        return -1;
    s.nfwd++;
    return 0;
}

/* HOST as a saved session's name: what the session says, where the command line said nothing. 0, or -1 (a
   session of another kind: said). */
static int by_session(const char *dir, char *key, size_t kroom, int port_said, int user_said, int sftp)
{
    static struct session all[SESSIONS_MAX];
    char path[300];
    const struct session *k;
    int n, i;
    sessions_path(path, sizeof path, dir);
    n = sessions_load(path, all, SESSIONS_MAX);
    if ((i = sessions_find(all, n, s.host)) < 0)
        return 0;
    k = &all[i];
    if (k->kind != SESSION_SSH) {
        term_printf("%s: %s is a %s session: telnet %s\r\n", sftp ? "sftp" : "ssh", k->name,
                    k->kind == SESSION_RAW ? "raw" : "Telnet", k->name);
        return -1;
    }
    milestone("session %s", k->name);
    snprintf(s.title, sizeof s.title, "%s", k->name);
    snprintf(s.host, sizeof s.host, "%s", k->host);
    if (!port_said && k->port)
        s.port = (unsigned)k->port;
    if (!user_said && k->user[0])
        snprintf(s.user, sizeof s.user, "%s", k->user);
    if (!key[0] && k->key[0])
        snprintf(key, kroom, "%s", k->key);
    if (!s.keepalive)
        s.keepalive = (unsigned)k->keepalive;
    if (sftp)
        return 0;
    if (!s.cmd[0])
        snprintf(s.cmd, sizeof s.cmd, "%s", k->command);
    if (!s.log[0])
        snprintf(s.log, sizeof s.log, "%s", k->log);
    {
        const char *p = k->tunnels, *e;         /* its tunnels, after the command line's own */
        while (*p) {
            while (*p == ' ')
                p++;
            for (e = p; *e && *e != ' '; e++)
                ;
            if (e > p)
                add_tunnel(p, (int)(e - p));
            p = e;
        }
    }
    return 0;
}

static int start(int argc, char **argv, int sftp)
{
    char dir[260], key[260] = "";
    int i, port_said = 0, user_said = 0;
    if (s.stage != S_OFF) {
        err("ssh: a session is open already\r\n");
        return 1;
    }
    memset(&s, 0, sizeof s);
    s.sftp = sftp;
    s.port = 22;
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if ((!strcmp(argv[i], "-p") || !strcmp(argv[i], "-P")) && i + 1 < argc)
            s.port = (unsigned)atoi(argv[++i]), port_said = 1;
        else if (!strcmp(argv[i], "-i") && i + 1 < argc)
            snprintf(key, sizeof key, "%s", argv[++i]);
        else if (!strcmp(argv[i], "-l") && i + 1 < argc)
            snprintf(s.user, sizeof s.user, "%s", argv[++i]), user_said = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc && !strncmp(argv[i + 1], "ServerAliveInterval=", 20)
                 && atoi(argv[i + 1] + 20) >= 0 && atoi(argv[i + 1] + 20) <= 3600)
            s.keepalive = (unsigned)atoi(argv[++i] + 20);       /* (as OpenSSH calls it: seconds; 0: none) */
        else if (!sftp && !strcmp(argv[i], "-sessionlog") && i + 1 < argc)
            snprintf(s.log, sizeof s.log, "%s", argv[++i]);
        else if (!sftp && !strcmp(argv[i], "-N"))
            s.no_shell = 1;
        else if (!sftp && !strcmp(argv[i], "-g"))
            s.gateway = 1;
        else if (!sftp && !strcmp(argv[i], "-L") && i + 1 < argc) {     /* [address:]port:host:hostport */
            if (add_tunnel(argv[++i], -1) != 0) {
                term_printf("ssh: -L wants [ADDRESS:]PORT:HOST:HOSTPORT, ADDRESS * or 127.0.0.1 (at most %d of them)\r\n", NFWD);
                return 1;
            }
        }
        else {
            term_printf("%s: unknown option %s\r\n", sftp ? "sftp" : "ssh", argv[i]);
            return 1;
        }
    }
    if (i >= argc || parse_target(argv[i], s.user, sizeof s.user, s.host, sizeof s.host) != 0 || !s.port) {
        say(sftp ? "usage: sftp [-P port] [-i key] [user@]host\r\n"
                 : "usage: ssh [-p port] [-i key] [-l user] [-L [address:]port:host:hostport] [-g] [-N] [user@]host [command]\r\n");
        return 1;
    }
    for (i++; !sftp && i < argc; i++) {         /* the command: the words that follow */
        if (s.cmd[0])
            strncat(s.cmd, " ", sizeof s.cmd - strlen(s.cmd) - 1);
        strncat(s.cmd, argv[i], sizeof s.cmd - strlen(s.cmd) - 1);
    }
    ssh_dir(dir, sizeof dir);
    if (by_session(dir, key, sizeof key, port_said, user_said || s.user[0], sftp) != 0)
        return 1;
    if (!s.user[0]) {
        const char *u = getenv("USER");
        snprintf(s.user, sizeof s.user, "%s", u && *u ? u : "glos");
    }
    snprintf(s.known, sizeof s.known, "%s\\KNOWNHST.TXT", dir);
    if (!key[0])
        snprintf(key, sizeof key, "%s\\ID_ED25", dir);
    s.have_key = sshc_key_read(key, s.sk, s.pk) == 0;
    if (connect_to() != 0)
        return 255;
    s.c = sshc_new(&ops, 0, NULL);
    if (!s.c) {
        glos_net_close(s.conn, 1);
        s.conn = 0;
        err("ssh: out of memory\r\n");
        return 255;
    }
    s.stage = S_CONNECTED;
    term_title(s.title[0] ? s.title : s.host);
    if (s.log[0]) {                             /* the session's log: begun now, or said to be none */
        char what[120];
        snprintf(what, sizeof what, "ssh %s%s%s", s.title[0] ? "" : s.user, s.title[0] ? "" : "@", s.title[0] ? s.title : s.host);
        if (sesslog_open(s.log, what) != 0)
            term_printf("ssh: the log %s can't be written: no log is kept\r\n", s.log);
    }
    return -2;                                  /* (it goes on: shell_resume() at its end) */
}

int ssh_command(int argc, char **argv) { return start(argc, argv, 0); }
int sftp_command(int argc, char **argv) { return start(argc, argv, 1); }

int ssh_screen(void) { return s.screen; }
int ssh_asking(void) { return s.stage == S_ASK_HOST || s.stage == S_ASK_PASSWORD || s.stage == S_SFTP; }

/* ---- answers (lines typed while it asks) */

static void sftp_line(const char *line);

void ssh_answer(const char *line)
{
    if (s.stage == S_ASK_HOST) {
        if (!strcmp(line, "yes")) {
            char dir[260];
            ssh_dir(dir, sizeof dir);
            mkdir(dir, 0777);
            if (sshc_known_add(s.known, s.host, s.port, s.host_pk) == 0)
                term_printf("Warning: Permanently added '%s' (ED25519) to the list of known hosts.\r\n", s.host);
            else
                term_printf("Warning: couldn't add '%s' to %s.\r\n", s.host, s.known);
            s.stage = S_LOGGING_IN;
            sshc_login(s.c, s.user, s.have_key ? s.sk : NULL, s.have_key ? s.pk : NULL, NULL);
        } else if (!strcmp(line, "no")) {
            term_printf("Host key verification failed.\r\n");
            finish(255);
        } else {
            say("Please type 'yes' or 'no': ");
            term_prompt_done();
        }
        return;
    }
    if (s.stage == S_ASK_PASSWORD) {
        term_input_hidden(0);
        s.stage = S_LOGGING_IN;
        sshc_login(s.c, s.user, NULL, NULL, line);  /* (the key has been tried) */
        return;
    }
    if (s.stage == S_SFTP)
        sftp_line(line);
}

/* ---- keys and the window, while the screen shows */

/* What was typed at the terminal, as it sent it: the session's. */
void ssh_raw(const char *d, int n)
{
    if (s.stage == S_RUNNING && s.ch)
        sshc_write(s.ch, (const uint8_t *)d, (uint32_t)n);
}

void ssh_resize(int cols, int rows)
{
    if (s.screen && s.ch)
        sshc_resize(s.ch, (unsigned)cols, (unsigned)rows);
}

void ssh_break(void)
{
    if (s.no_shell && s.stage == S_RUNNING) {   /* -N: the tunnels end */
        term_printf("Connection to %s closed.\r\n", s.host);
        finish(0);
    } else if (s.ch && s.stage == S_RUNNING)
        sshc_signal(s.ch, "INT");
    else if (s.stage != S_OFF && s.stage != S_SFTP) {
        err("^Break\r\n");
        finish(255);
    }
}

/* ---- sftp's commands */

static void remote_path(const char *p, char *out, size_t room)
{
    if (p[0] == '/')
        snprintf(out, room, "%s", p);
    else if (!strcmp(s.rcwd, "/"))
        snprintf(out, room, "/%s", p);
    else
        snprintf(out, room, "%s/%s", s.rcwd, p);
}

static const char *base_of(const char *p)
{
    const char *a = strrchr(p, '/'), *b = strrchr(p, '\\'), *c = strrchr(p, ':');
    const char *m = a > b ? a : b;
    m = m > c ? m : c;
    return m ? m + 1 : p;
}

static int list_one(void *arg, const char *name, const char *longname, const struct sftpc_attrs *a)
{
    (void)a;
    if (strcmp(name, ".") && strcmp(name, ".."))
        term_printf("%s\r\n", *(int *)arg ? longname : name);
    return 0;
}

static void progress(void *arg, uint64_t done, uint64_t total)
{
    (void)arg;
    (void)done;
    (void)total;
}

/* A file's name on this PC as it was typed (UTF-8, as the terminal sends it) in DOS's own code page, which is
   what DOS knows it by. */
static const char *here(const char *typed, char *out, size_t room)
{
    if (cp_from_utf8(cp_active(), typed, out, (int)room) < 0)
        snprintf(out, room, "%s", typed);
    return out;
}

static void sftp_line(const char *line)
{
    char copy[600], *argv[8], path[600], local[300], dos[300];
    int argc = 0;
    char *p;
    snprintf(copy, sizeof copy, "%s", line);
    for (p = strtok(copy, " \t"); p && argc < 8; p = strtok(NULL, " \t"))
        argv[argc++] = p;
    if (!argc) {
        sftp_prompt();
        return;
    }
    if (!strcmp(argv[0], "exit") || !strcmp(argv[0], "quit") || !strcmp(argv[0], "bye")) {
        finish(0);
        return;
    }
    if (!strcmp(argv[0], "help") || !strcmp(argv[0], "?")) {
        say("ls [-l] [path]     cd path     pwd     lcd path     lpwd\r\n"
            "get remote [local] put local [remote]\r\n"
            "rm path   mkdir path   rmdir path   rename old new   exit\r\n");
    } else if (!strcmp(argv[0], "pwd")) {
        term_printf("Remote working directory: %s\r\n", s.rcwd);
    } else if (!strcmp(argv[0], "lpwd") || !strcmp(argv[0], "lcd")) {
        if (argc > 1 && chdir(here(argv[1], dos, sizeof dos)) != 0)
            term_printf("Couldn't change local directory to %s\r\n", argv[1]);
        else if (getcwd(dos, sizeof dos) && cp_to_utf8(cp_active(), dos, local, sizeof local) >= 0)
            term_printf("Local working directory: %s\r\n", local);
    } else if (!strcmp(argv[0], "cd") && argc > 1) {
        struct sftpc_attrs a;
        char real[512];
        remote_path(argv[1], path, sizeof path);
        if (sftpc_realpath(s.sf, path, real, sizeof real) != 0 || sftpc_stat(s.sf, real, &a) != 0
            || (a.has_perm && (a.perm & 0170000) != 040000))
            term_printf("Couldn't change directory: %s\r\n", sftpc_why(s.sf)[0] ? sftpc_why(s.sf) : "not a directory");
        else
            snprintf(s.rcwd, sizeof s.rcwd, "%s", real);
    } else if (!strcmp(argv[0], "ls")) {
        int l = argc > 1 && !strcmp(argv[1], "-l");
        remote_path(argc > 1 + l ? argv[1 + l] : ".", path, sizeof path);
        if (sftpc_ls(s.sf, path, list_one, &l) != 0)
            term_printf("Couldn't list %s: %s\r\n", path, sftpc_why(s.sf));
    } else if (!strcmp(argv[0], "get") && argc > 1) {
        FILE *f;
        remote_path(argv[1], path, sizeof path);
        snprintf(local, sizeof local, "%s", argc > 2 ? argv[2] : base_of(argv[1]));
        if (!(f = fopen(here(local, dos, sizeof dos), "wb"))) {
            term_printf("Couldn't open %s here\r\n", local);
        } else {
            unsigned t0 = glos_ticks();
            int r;
            term_printf("Fetching %s to %s\r\n", path, local);
            r = sftpc_get(s.sf, path, f, progress, NULL);
            fclose(f);
            if (r != 0) {
                term_printf("Couldn't get %s: %s\r\n", path, sftpc_why(s.sf));
                remove(dos);
            } else {
                struct stat st;
                stat(dos, &st);
                term_printf("%s: %ld bytes in %u ms\r\n", local, (long)st.st_size, glos_ticks() - t0);
            }
        }
    } else if (!strcmp(argv[0], "put") && argc > 1) {
        FILE *f = fopen(here(argv[1], dos, sizeof dos), "rb");
        remote_path(argc > 2 ? argv[2] : base_of(argv[1]), path, sizeof path);
        if (!f) {
            term_printf("Couldn't open %s here\r\n", argv[1]);
        } else {
            unsigned t0 = glos_ticks();
            term_printf("Uploading %s to %s\r\n", argv[1], path);
            if (sftpc_put(s.sf, f, path, progress, NULL) != 0)
                term_printf("Couldn't put %s: %s\r\n", path, sftpc_why(s.sf));
            else
                term_printf("%s: done in %u ms\r\n", path, glos_ticks() - t0);
            fclose(f);
        }
    } else if (!strcmp(argv[0], "rm") && argc > 1) {
        remote_path(argv[1], path, sizeof path);
        if (sftpc_remove(s.sf, path) != 0)
            term_printf("Couldn't delete %s: %s\r\n", path, sftpc_why(s.sf));
    } else if (!strcmp(argv[0], "mkdir") && argc > 1) {
        remote_path(argv[1], path, sizeof path);
        if (sftpc_mkdir(s.sf, path) != 0)
            term_printf("Couldn't make %s: %s\r\n", path, sftpc_why(s.sf));
    } else if (!strcmp(argv[0], "rmdir") && argc > 1) {
        remote_path(argv[1], path, sizeof path);
        if (sftpc_rmdir(s.sf, path) != 0)
            term_printf("Couldn't remove %s: %s\r\n", path, sftpc_why(s.sf));
    } else if (!strcmp(argv[0], "rename") && argc > 2) {
        char to[600];
        remote_path(argv[1], path, sizeof path);
        remote_path(argv[2], to, sizeof to);
        if (sftpc_rename(s.sf, path, to) != 0)
            term_printf("Couldn't rename %s: %s\r\n", path, sftpc_why(s.sf));
    } else {
        term_printf("Invalid command: %s (help lists them)\r\n", argv[0]);
    }
    if (s.stage == S_SFTP) {
        if (s.c && sshc_state(s.c) == SSHC_DEAD) {
            term_printf("sftp: the connection is gone: %s\r\n", sshc_why(s.c));
            finish(255);
        } else {
            sftp_prompt();
        }
    }
}

/* ---- ssh-keygen */

int keygen_command(int argc, char **argv)
{
    char dir[260], file[260] = "", comment[64] = "glos", fp[64], pub[300];
    uint8_t sk[64], pk[32];
    FILE *f;
    int i;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc)
            snprintf(file, sizeof file, "%s", argv[++i]);
        else if (!strcmp(argv[i], "-C") && i + 1 < argc)
            snprintf(comment, sizeof comment, "%s", argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc && !strcmp(argv[i + 1], "ed25519"))
            i++;
        else {
            say("usage: ssh-keygen [-t ed25519] [-f file] [-C comment]\r\n");
            return 1;
        }
    }
    if (!file[0]) {
        ssh_dir(dir, sizeof dir);
        mkdir(dir, 0777);
        snprintf(file, sizeof file, "%s\\ID_ED25", dir);
    }
    if ((f = fopen(file, "rb")) != NULL) {
        fclose(f);
        term_printf("%s already exists: not overwritten (DEL it first).\r\n", file);
        return 1;
    }
    say("Generating public/private ed25519 key pair.\r\n");
    if (sshc_keygen(sk, pk) != 0 || sshc_key_write(file, sk, pk, comment) != 0) {
        memset(sk, 0, sizeof sk);
        term_printf("Couldn't write %s\r\n", file);
        return 1;
    }
    memset(sk, 0, sizeof sk);
    sshc_fingerprint(fp, pk);
    term_printf("Your identification has been saved in %s\r\nYour public key has been saved in %s.PUB\r\n"
                "The key fingerprint is:\r\n%s %s\r\n", file, file, fp, comment);
    snprintf(pub, sizeof pub, "%s.PUB", file);
    if ((f = fopen(pub, "r")) != NULL) {        /* (the line for a server's authorized_keys) */
        char line[200];
        if (fgets(line, sizeof line, f))
            term_printf("%s", line);
        fclose(f);
        say("\r");
    }
    return 0;
}
