/* ssh, sftp and ssh-keygen in the terminal (M10 step D, PRD D61): sdk/sshc
 * over a GLOS_FN_NET connection, the xterm screen (vt.c) for a session with a
 * pty, the shell's own line for the questions (a new host key, a password).
 *
 *   ssh [-p PORT] [-i KEY] [-l USER] [USER@]HOST [COMMAND]
 *   sftp [-P PORT] [-i KEY] [USER@]HOST
 *   ssh-keygen [-f FILE] [-C COMMENT]
 *
 * Keys and known hosts are in SSH\ beside TERM.EXE (C:\GLOS\SSH; GLOSSSH in
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

#include <glos/api.h>
#include <glos/gui.h>
#ifdef __DJGPP__
#include <glos/net.h>
#else
#include <arpa/inet.h>                          /* (the host's build, for the goldens: inet_aton alone) */
#endif
#include <glos/sftpc.h>
#include <glos/sshc.h>

#include "term.h"
#include "vt.h"

enum { S_OFF, S_CONNECTED, S_ASK_HOST, S_ASK_PASSWORD, S_LOGGING_IN, S_OPENING, S_RUNNING, S_SFTP };

static struct vt vt;
static struct {
    int stage;
    int conn;                           /* the GLOS connection */
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
    unsigned changed_at;                /* (the log's screen dumps) */
    int dump_due;
    char rcwd[512];                     /* sftp's folder there */
} s;

static int log_screens;

/* -log: where a session has got to, on stdout as it happens ("SSH| ...": the suite waits for them). */
static void milestone(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void milestone(const char *fmt, ...)
{
    va_list ap;
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
    while (n && s.conn > 0) {
        int k = glos_net_send((unsigned)s.conn, d, n, 10000);
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
    if (s.conn <= 0 || !s.c)
        return -1;
    for (;;) {
        k = glos_net_recv((unsigned)s.conn, buf, sizeof buf, first ? wait_ms : 0, &st);
        first = 0;
        if (k < 0 || (k == 0 && st == GLOS_NETS_CLOSED))
            return -1;
        if (k == 0)
            return 0;
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

static void on_data(struct sshc_chan *ch, int stream, const uint8_t *d, uint32_t n)
{
    if (ch != s.ch)
        return;
    if (s.sf && !stream) {
        sftpc_input(s.sf, d, n);
        return;
    }
    if (s.screen) {
        vt_feed(&vt, d, (int)n);
        if (vt.reply_n) {
            sshc_write(s.ch, (const uint8_t *)vt.reply, (uint32_t)vt.reply_n);
            vt.reply_n = 0;
        }
        if (vt.title_set) {
            vt.title_set = 0;
            term_title(vt.title);
        }
        s.changed_at = glos_ticks();
        s.dump_due = 1;
        term_screen_dirty();
    } else {
        if (stream)
            term_attr(T_ERROR);
        term_write((const char *)d, (int)n);
        term_attr(T_NORMAL);
    }
}

static void on_exit_status(struct sshc_chan *ch, int status, const char *sig)
{
    (void)ch;
    s.status = status;
    if (sig)
        snprintf(s.sig, sizeof s.sig, "%s", sig);
}

static void on_closed(struct sshc_chan *ch)
{
    if (ch == s.ch) {
        s.accepted = sshc_chan_ok(ch) == 1;     /* (a quick command may open, run and close between two polls) */
        s.ch = 0;
    }
}

static const struct sshc_ops ops = { net_send, NULL, on_hostkey, on_banner, on_data, NULL, on_exit_status, on_closed };

/* ---- the session's stages */

static void dump_screen(void)
{
    static char text[VT_COLS_MAX * VT_ROWS_MAX * 3 + VT_ROWS_MAX + 1];
    char *p, *nl;
    if (!log_screens)
        return;
    vt_dump(&vt, text, sizeof text);
    printf("VT-SCREEN %dx%d\n", vt.cols, vt.rows);
    for (p = text; (nl = strchr(p, '\n')) != NULL; p = nl + 1)
        printf("VT| %.*s\n", (int)(nl - p), p);
    printf("VT-END\n");
    fflush(stdout);
}

static void finish(int code)
{
    if (s.screen) {
        dump_screen();
        s.screen = 0;
        term_screen_off();
    }
    if (s.sf)
        sftpc_free(s.sf);
    if (s.c)
        sshc_free(s.c);
    if (s.conn > 0)
        glos_net_close((unsigned)s.conn, 0);
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
    if (s.screen) {
        vt_init(&vt, cols, rows);
        term_screen_on(&vt);
    }
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
            open_channel();
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
        if (!s.ch) {
            int code = s.status >= 0 ? s.status : 255;
            if (s.screen) {
                dump_screen();
                s.screen = 0;
                term_screen_off();
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
            dump_screen();
            s.screen = 0;
            term_screen_off();
        }
        term_printf("%s: %s: %s\r\n", s.sftp ? "sftp" : "ssh", s.host,
                    s.c && sshc_why(s.c)[0] ? sshc_why(s.c) : "the connection was closed");
        finish(255);
        return;
    }
    if (s.stage != S_ASK_HOST && s.stage != S_ASK_PASSWORD && s.stage != S_SFTP)
        step();
    if (s.dump_due && glos_ticks() - s.changed_at > 1500) {
        s.dump_due = 0;
        dump_screen();
    }
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
    if (inet_aton(s.host, &ia)) {
        addr = (unsigned)ia.s_addr;
    } else if (glos_net_resolve(s.host, 15000, &addr) != 0) {
        term_printf("ssh: Could not resolve hostname %s: %s\r\n", s.host,
                    glos_error() == GLOS_E_NET_DOWN ? "no network" : "Name or service not known");
        return -1;
    }
    if ((s.conn = glos_net_connect(addr, s.port, 15000, GLOS_NETF_EVENTS)) < 0) {
        int e = glos_error();
        term_printf("ssh: connect to host %s port %u: %s\r\n", s.host, s.port, e == GLOS_E_NET_REFUSED
                    ? "Connection refused" : e == GLOS_E_NET_TIMEOUT ? "Connection timed out" : "No route to host");
        s.conn = 0;
        return -1;
    }
    return 0;
}

static int start(int argc, char **argv, int sftp)
{
    char dir[260], key[260] = "";
    int i;
    if (s.stage != S_OFF) {
        err("ssh: a session is open already\r\n");
        return 1;
    }
    memset(&s, 0, sizeof s);
    s.sftp = sftp;
    s.port = 22;
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if ((!strcmp(argv[i], "-p") || !strcmp(argv[i], "-P")) && i + 1 < argc)
            s.port = (unsigned)atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i") && i + 1 < argc)
            snprintf(key, sizeof key, "%s", argv[++i]);
        else if (!strcmp(argv[i], "-l") && i + 1 < argc)
            snprintf(s.user, sizeof s.user, "%s", argv[++i]);
        else {
            term_printf("%s: unknown option %s\r\n", sftp ? "sftp" : "ssh", argv[i]);
            return 1;
        }
    }
    if (i >= argc || parse_target(argv[i], s.user, sizeof s.user, s.host, sizeof s.host) != 0 || !s.port) {
        say(sftp ? "usage: sftp [-P port] [-i key] [user@]host\r\n"
                 : "usage: ssh [-p port] [-i key] [-l user] [user@]host [command]\r\n");
        return 1;
    }
    for (i++; !sftp && i < argc; i++) {         /* the command: the words that follow */
        if (s.cmd[0])
            strncat(s.cmd, " ", sizeof s.cmd - strlen(s.cmd) - 1);
        strncat(s.cmd, argv[i], sizeof s.cmd - strlen(s.cmd) - 1);
    }
    if (!s.user[0]) {
        const char *u = getenv("USER");
        snprintf(s.user, sizeof s.user, "%s", u && *u ? u : "glos");
    }
    ssh_dir(dir, sizeof dir);
    snprintf(s.known, sizeof s.known, "%s\\KNOWNHST.TXT", dir);
    if (!key[0])
        snprintf(key, sizeof key, "%s\\ID_ED25", dir);
    s.have_key = sshc_key_read(key, s.sk, s.pk) == 0;
    if (connect_to() != 0)
        return 255;
    s.c = sshc_new(&ops, 0, NULL);
    if (!s.c) {
        glos_net_close((unsigned)s.conn, 1);
        s.conn = 0;
        err("ssh: out of memory\r\n");
        return 255;
    }
    s.stage = S_CONNECTED;
    term_title(s.host);
    return -2;                                  /* (it goes on: shell_resume() at its end) */
}

int ssh_command(int argc, char **argv) { return start(argc, argv, 0); }
int sftp_command(int argc, char **argv) { return start(argc, argv, 1); }

int ssh_active(void) { return s.stage != S_OFF; }
int ssh_screen(void) { return s.screen; }
int ssh_asking(void) { return s.stage == S_ASK_HOST || s.stage == S_ASK_PASSWORD || s.stage == S_SFTP; }
void ssh_log_screens(int on) { log_screens = on; }

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

void ssh_key(unsigned key, unsigned ch, unsigned mods)
{
    char out[32];
    int n;
    if (s.stage != S_RUNNING || !s.ch)
        return;
    if ((n = vt_key(&vt, key, ch, mods, out, sizeof out)) > 0)
        sshc_write(s.ch, (const uint8_t *)out, (uint32_t)n);
}

void ssh_paste(const char *text, int n)
{
    if (s.stage != S_RUNNING || !s.ch)
        return;
    if (vt.bracketed)
        sshc_write(s.ch, (const uint8_t *)"\033[200~", 6);
    sshc_write(s.ch, (const uint8_t *)text, (uint32_t)n);
    if (vt.bracketed)
        sshc_write(s.ch, (const uint8_t *)"\033[201~", 6);
}

void ssh_resize(int cols, int rows)
{
    if (!s.screen)
        return;
    vt_resize(&vt, cols, rows);
    if (s.ch)
        sshc_resize(s.ch, (unsigned)cols, (unsigned)rows);
}

void ssh_break(void)
{
    if (s.ch && s.stage == S_RUNNING)
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

static void sftp_line(const char *line)
{
    char copy[600], *argv[8], path[600], local[300];
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
        if (argc > 1 && chdir(argv[1]) != 0)
            term_printf("Couldn't change local directory to %s\r\n", argv[1]);
        else if (getcwd(local, sizeof local))
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
        if (!(f = fopen(local, "wb"))) {
            term_printf("Couldn't open %s here\r\n", local);
        } else {
            unsigned t0 = glos_ticks();
            int r;
            term_printf("Fetching %s to %s\r\n", path, local);
            r = sftpc_get(s.sf, path, f, progress, NULL);
            fclose(f);
            if (r != 0) {
                term_printf("Couldn't get %s: %s\r\n", path, sftpc_why(s.sf));
                remove(local);
            } else {
                struct stat st;
                stat(local, &st);
                term_printf("%s: %ld bytes in %u ms\r\n", local, (long)st.st_size, glos_ticks() - t0);
            }
        }
    } else if (!strcmp(argv[0], "put") && argc > 1) {
        FILE *f = fopen(argv[1], "rb");
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
