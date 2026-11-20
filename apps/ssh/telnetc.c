/* TELNET.EXE (phase F; the owner's choice of 2026-11-20: Telnet and raw TCP sessions, as PuTTY has them): a
 * Telnet client at a terminal, run by GLOS's shell as SSH.EXE is (cli.c is its terminal too). For what has no
 * SSH: old systems, bulletin boards, switches and their like. Nothing it carries is secret from the network.
 *
 *   telnet [-r] [-c PAGE] [-sessionlog FILE] HOST [PORT]
 *                              HOST: a computer's name or address, or a saved session of that kind
 *                              (SESSIONS.CFG beside SSH's key: apps/shared/sessions.h). PORT: 23 if none.
 *                              -r: raw, no Telnet among the bytes: a TCP connection and nothing else.
 *                              -c PAGE: the far side's characters are a DOS code page's (437 for most
 *                              bulletin boards): shown as they should be, and what is typed sent in it.
 *                              -sessionlog FILE: what the session writes is added to that file as text.
 *
 * Telnet's own words among the bytes are apps/shared/telnet.c's. Where the far side echoes what is typed (most
 * do), every key goes to it at once and Ctrl+C is its own; where it does not (and in a raw session), a line is
 * typed here, shown here, and sent at Enter, and Ctrl+C ends the session. Ctrl+] ends it at any time.
 *
 * SERIAL.EXE (the third of PuTTY's kinds the owner chose) is this file built again (CLI_WHICH 4): the connection
 * is a COM port's (API 2.10, glos_net_serial) in a computer's place, for what is at the end of a serial cable: a
 * router's or a switch's console, a modem, another PC's.
 *
 *   serial [-f none|rts|xon] [-c PAGE] [-sessionlog FILE] COMn [SPEED] [FRAME]
 *                              COMn: COM1 to COM4, or a saved session of that kind. SPEED: bits a second
 *                              (115200 if none). FRAME: the bits, the parity (N, E, O) and the stop bits, as
 *                              8N1 (which it is if none). -f: flow control, by the RTS and CTS wires or by
 *                              XON and XOFF. Every key goes down the line at once, and nothing is shown that
 *                              the far side does not send back. Ctrl+] ends the session. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/cp.h>
#include <glos/glos.h>
#include <glos/net.h>

#include "cli.h"
#include "../shared/sessions.h"
#include "../shared/sesslog.h"
#include "../shared/telnet.h"

static struct {
    int on;                             /* a connection is there */
    glos_handle conn;
    struct telnet t;
    int raw;                            /* -r: no Telnet among the bytes */
    int serial;                         /* the connection is a COM port's (SERIAL.EXE): every key at once */
    int page;                           /* the far side's code page (0: its bytes as they are, UTF-8) */
    int lines;                          /* a line at a time, shown here: the far side does not echo */
    char host[64], title[24], log[100];
    unsigned port;                      /* (a serial line's speed) */
    char frame[4];                      /* a serial line's: "8N1" */
    int flow;                           /* ... and its flow control (-1: none said) */
    unsigned u;                         /* a typed character being put together from its UTF-8 bytes ... */
    int more;                           /* ... and how many of them are still to come */
} s;

static void finish(int code)
{
    if (s.conn)
        glos_net_close(s.conn, 0);
    sesslog_close();
    s.conn = 0;
    s.on = 0;
    term_title("");
    shell_resume(code);
}

/* ---- to the network */

static void net_send(void *ctx, const unsigned char *d, int n)
{
    (void)ctx;
    while (n > 0 && s.conn) {
        int k = glos_net_send(s.conn, d, (unsigned)n, 10000);
        if (k <= 0)
            break;
        d += k, n -= k;
    }
}

static void to_far(const unsigned char *d, int n)       /* bytes for the far side, as it has its characters */
{
    if (s.raw)
        net_send(NULL, d, n);
    else
        telnet_output(&s.t, d, n);
}

/* What was typed (UTF-8, as the terminal sends it): to the far side, in its code page if it has one (a
   character the page hasn't got: a question mark). */
static void typed_out(const unsigned char *d, int n)
{
    unsigned char out[128];
    int i, k = 0;
    if (!s.page) {
        to_far(d, n);
        return;
    }
    for (i = 0; i < n; i++) {
        unsigned char c = d[i];
        if (s.more && (c & 0xC0) == 0x80) {
            s.u = s.u << 6 | (c & 0x3F);
            if (--s.more)
                continue;
        } else if (c >= 0xF0) {
            s.u = c & 7, s.more = 3;
            continue;
        } else if (c >= 0xE0) {
            s.u = c & 15, s.more = 2;
            continue;
        } else if (c >= 0xC0) {
            s.u = c & 31, s.more = 1;
            continue;
        } else {
            s.u = c, s.more = 0;
        }
        if (s.u < 0x80) {
            out[k++] = (unsigned char)s.u;
        } else {
            int b = cp_byte(s.page, s.u);
            out[k++] = b < 0 ? '?' : (unsigned char)b;
        }
        if (k == (int)sizeof out) {
            to_far(out, k);
            k = 0;
        }
    }
    if (k)
        to_far(out, k);
}

/* ---- from the network */

static void far_text(void *ctx, const unsigned char *d, int n) /* the far side's text: to the terminal and the log */
{
    char out[768];
    int i, k = 0;
    (void)ctx;
    for (i = 0; i < n; i++) {
        unsigned u = d[i];
        if (s.page && u >= 0x80)
            u = cp_char(s.page, d[i]);
        if (!s.page || u < 0x80) {              /* (no page: its bytes as they are) */
            out[k++] = (char)d[i];
        } else if (u < 0x800) {
            out[k++] = (char)(0xC0 | u >> 6), out[k++] = (char)(0x80 | (u & 0x3F));
        } else {
            out[k++] = (char)(0xE0 | u >> 12), out[k++] = (char)(0x80 | (u >> 6 & 0x3F)), out[k++] = (char)(0x80 | (u & 0x3F));
        }
        if (k > (int)sizeof out - 4) {
            term_raw(out, k);
            sesslog_write(out, k);
            k = 0;
        }
    }
    if (k) {
        term_raw(out, k);
        sesslog_write(out, k);
    }
}

static void mode_now(void)              /* a line at a time, or every key: as the far side echoes or not */
{
    int lines = !s.serial && (s.raw || !telnet_echoes(&s.t));
    if (lines && !s.lines)
        term_prompt_done();             /* (the line typed next is the session's) */
    s.lines = lines;
}

void ssh_poll(void)
{
    unsigned char buf[2048];
    unsigned st = 0;
    int k;
    if (!s.on)
        return;
    for (;;) {
        k = glos_net_receive(s.conn, buf, sizeof buf, 0, &st);
        if (k > 0) {
            if (s.raw)
                far_text(NULL, buf, k);
            else
                telnet_input(&s.t, buf, k);
            mode_now();
            continue;
        }
        if (k < 0 || st == GLOS_NETS_CLOSED || st == GLOS_NETS_FAILED) {
            term_printf("\r\nConnection to %s closed%s.\r\n", s.host, k < 0 || st == GLOS_NETS_FAILED ? " (it broke)" : "");
            finish(k < 0 || st == GLOS_NETS_FAILED ? 255 : 0);
        }
        return;
    }
}

/* ---- what the terminal asks (cli.h) */

int ssh_screen(void) { return s.on && !s.lines; }
int ssh_asking(void) { return s.on && s.lines; }

void ssh_answer(const char *line)       /* a line typed (a line at a time): sent with its end */
{
    if (!s.on)
        return;
    typed_out((const unsigned char *)line, (int)strlen(line));
    to_far((const unsigned char *)"\r\n", 2);
    if (s.lines)
        term_prompt_done();
}

void ssh_raw(const char *d, int n)      /* keys as they are typed (the far side echoes): Ctrl+] ends the session */
{
    int i;
    if (!s.on)
        return;
    for (i = 0; i < n; i++)
        if (d[i] == 0x1D) {
            typed_out((const unsigned char *)d, i);
            term_printf("\r\nConnection to %s closed.\r\n", s.host);
            finish(0);
            return;
        }
    typed_out((const unsigned char *)d, n);
}

void ssh_resize(int cols, int rows)
{
    if (s.on && !s.raw)
        telnet_size(&s.t, (unsigned)cols, (unsigned)rows);
}

void ssh_break(void)                    /* Ctrl+C while a line is typed here, or the terminal gone: its end */
{
    if (!s.on)
        return;
    term_printf("Connection to %s closed.\r\n", s.host);
    finish(0);
}

/* ---- the command */

/* HOST as a saved session's name: its computer, port, code page and log, where the command line said nothing.
   0, or -1 (a session of another kind: said). */
static int by_session(int port_said, int page_said)
{
    static struct session all[SESSIONS_MAX];
    char dir[260], path[300];
    const char *e = getenv("GLOSSSH");
    const struct session *k;
    int n, i;
    if (e && *e)
        snprintf(dir, sizeof dir, "%s", e);
    else
        snprintf(dir, sizeof dir, "%s\\SSH", term_home());
    sessions_path(path, sizeof path, dir);
    n = sessions_load(path, all, SESSIONS_MAX);
    if ((i = sessions_find(all, n, s.host)) < 0)
        return 0;
    k = &all[i];
    if ((k->kind == SESSION_SERIAL) != (CLI_WHICH == 4) || k->kind == SESSION_SSH) {    /* another program's */
        term_printf("%s: %s is %s session: %s %s\r\n", CLI_WHICH == 4 ? "serial" : "telnet", k->name,
                    k->kind == SESSION_SSH ? "an SSH" : k->kind == SESSION_SERIAL ? "a serial" : "a Telnet",
                    k->kind == SESSION_SSH ? "ssh" : k->kind == SESSION_SERIAL ? "serial" : "telnet", k->name);
        return -1;
    }
    snprintf(s.title, sizeof s.title, "%s", k->name);
    snprintf(s.host, sizeof s.host, "%s", k->host);
    s.raw = s.raw || k->kind == SESSION_RAW;
    if (k->kind == SESSION_SERIAL) {            /* (port_said: a speed was; the frame and the flow go with it) */
        if (!port_said) {
            s.port = (unsigned)(k->speed ? k->speed : 115200);
            snprintf(s.frame, sizeof s.frame, "%s", k->frame);
        }
        if (s.flow < 0)
            s.flow = k->flow;
    } else if (!port_said && k->port)
        s.port = (unsigned)k->port;
    if (!page_said)
        s.page = k->charset;
    if (!s.log[0])
        snprintf(s.log, sizeof s.log, "%s", k->log);
    return 0;
}

int telnet_command(int argc, char **argv)
{
    struct in_addr ia;
    unsigned addr;
    int i, e, cols = 80, rows = 24, port_said = 0, page_said = 0;
    if (s.on) {
        term_print("telnet: a session is open already\r\n");
        return 1;
    }
    memset(&s, 0, sizeof s);
    s.port = 23;
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-r"))
            s.raw = 1;
        else if (!strcmp(argv[i], "-c") && i + 1 < argc && atoi(argv[i + 1]) > 0 && cp_table(atoi(argv[i + 1])))
            s.page = atoi(argv[++i]), page_said = 1;
        else if (!strcmp(argv[i], "-sessionlog") && i + 1 < argc)
            snprintf(s.log, sizeof s.log, "%s", argv[++i]);
        else {
            term_printf(!strcmp(argv[i], "-c") ? "telnet: -c wants a code page GLOS has (437, 850, ...)\r\n"
                                               : "telnet: unknown option %s\r\n", argv[i]);
            return 1;
        }
    }
    if (i >= argc || !argv[i][0] || strlen(argv[i]) >= sizeof s.host) {
        term_print("usage: telnet [-r] [-c page] [-sessionlog file] host [port]\r\n");
        return 1;
    }
    snprintf(s.host, sizeof s.host, "%s", argv[i++]);
    if (i < argc) {
        s.port = (unsigned)atoi(argv[i]);
        port_said = 1;
        if (!s.port || s.port > 65535 || i + 1 < argc) {
            term_print("usage: telnet [-r] [-c page] [-sessionlog file] host [port]\r\n");
            return 1;
        }
    }
    if (by_session(port_said, page_said) != 0)
        return 1;
    if (inet_aton(s.host, &ia)) {
        addr = (unsigned)ia.s_addr;
    } else if ((e = glos_net_resolve(s.host, 15000, &addr)) != 0) {
        term_printf("telnet: Could not resolve hostname %s: %s\r\n", s.host,
                    e == GLOS_E_NET_DOWN ? "no network" : "Name or service not known");
        return 255;
    }
    term_printf("Trying %s...\r\n", s.host);
    if (!(s.conn = glos_net_connect(addr, s.port, 15000, GLOS_NETF_EVENTS))) {
        e = glos_error();
        term_printf("telnet: connect to host %s port %u: %s\r\n", s.host, s.port, e == GLOS_E_NET_REFUSED
                    ? "Connection refused" : e == GLOS_E_NET_TIMEOUT ? "Connection timed out" : "No route to host");
        s.conn = 0;
        return 255;
    }
    term_printf("Connected to %s. Ctrl+] ends the session.\r\n", s.host);
    term_size(&cols, &rows);
    /* (A bulletin board's pictures are for a terminal called ANSI; everything else has xterm's name.) */
    telnet_init(&s.t, net_send, far_text, NULL, s.page ? "ANSI" : "XTERM", (unsigned)cols, (unsigned)rows);
    s.on = 1;
    term_title(s.title[0] ? s.title : s.host);
    if (s.log[0]) {
        char what[120];
        snprintf(what, sizeof what, "telnet %s", s.title[0] ? s.title : s.host);
        if (sesslog_open(s.log, what) != 0)
            term_printf("telnet: the log %s can't be written: no log is kept\r\n", s.log);
    }
    if (!s.raw && s.port == 23)                 /* (a Telnet server: this side says what it has first) */
        telnet_offer(&s.t);
    mode_now();
    return -2;                                  /* (it goes on: shell_resume() at its end) */
}

/* ---- SERIAL.EXE's command: a COM port in a computer's place */

int serial_command(int argc, char **argv)
{
    unsigned bits = 8, parity = 0, stop = 1;
    int i, e, com, page_said = 0, speed_said = 0;
    if (s.on) {
        term_print("serial: a session is open already\r\n");
        return 1;
    }
    memset(&s, 0, sizeof s);
    s.serial = s.raw = 1;
    s.port = 115200;
    s.flow = -1;
    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc && (!strcmp(argv[i + 1], "none") || !strcmp(argv[i + 1], "rts")
                                                         || !strcmp(argv[i + 1], "xon")))
            s.flow = argv[i + 1][0] == 'r' ? 1 : argv[i + 1][0] == 'x' ? 2 : 0, i++;
        else if (!strcmp(argv[i], "-c") && i + 1 < argc && atoi(argv[i + 1]) > 0 && cp_table(atoi(argv[i + 1])))
            s.page = atoi(argv[++i]), page_said = 1;
        else if (!strcmp(argv[i], "-sessionlog") && i + 1 < argc)
            snprintf(s.log, sizeof s.log, "%s", argv[++i]);
        else {
            term_printf(!strcmp(argv[i], "-c") ? "serial: -c wants a code page GLOS has (437, 850, ...)\r\n"
                        : !strcmp(argv[i], "-f") ? "serial: -f wants none, rts or xon\r\n" : "serial: unknown option %s\r\n",
                        argv[i]);
            return 1;
        }
    }
    if (i >= argc || !argv[i][0] || strlen(argv[i]) >= sizeof s.host || argc - i > 3) {
        term_print("usage: serial [-f none|rts|xon] [-c page] [-sessionlog file] COMn [speed] [frame]\r\n");
        return 1;
    }
    snprintf(s.host, sizeof s.host, "%s", argv[i++]);
    if (i < argc) {
        s.port = (unsigned)atoi(argv[i++]);
        speed_said = 1;
    }
    if (i < argc)
        snprintf(s.frame, sizeof s.frame, "%.3s", strlen(argv[i]) == 3 ? argv[i] : "?");
    if (by_session(speed_said, page_said) != 0)
        return 1;
    if (!(com = session_com(s.host))) {
        term_printf("serial: %s is no port (COM1 to COM4) and no saved session\r\n", s.host);
        return 1;
    }
    if (session_frame(s.frame, &bits, &parity, &stop) != 0 || s.port < 50 || s.port > 115200) {
        term_print("serial: a speed is 50 to 115200, a frame its bits (5 to 8), parity (N, E, O) and stop bits (1, 2): 8N1\r\n");
        return 1;
    }
    if (s.flow < 0)
        s.flow = 0;
    if (!(s.conn = glos_net_serial((unsigned)com, s.port, bits, parity, stop, (unsigned)s.flow, GLOS_NETF_EVENTS))) {
        e = glos_error();
        term_printf("serial: COM%d: %s\r\n", com,
                    e == GLOS_E_NOT_FOUND ? "this PC has no such port"
                    : e == GLOS_E_REFUSED ? "a serial session is open already (one at a time), or the cable link has that port"
                    : e == GLOS_E_ARGUMENT ? "its chip can't have that speed or frame"
                    : e == GLOS_E_FUNCTION ? "this GLOS has no serial lines for programs (API 2.10)" : "it can't be opened");
        s.conn = 0;
        return 255;
    }
    snprintf(s.host, sizeof s.host, "COM%d", com);
    term_printf("Connected to COM%d at %u %u%c%u%s. Ctrl+] ends the session.\r\n", com, s.port, bits, "NOE"[parity], stop,
                s.flow == 1 ? ", RTS/CTS" : s.flow == 2 ? ", XON/XOFF" : "");
    s.on = 1;
    term_title(s.title[0] ? s.title : s.host);
    if (s.log[0]) {
        char what[120];
        snprintf(what, sizeof what, "serial %s", s.title[0] ? s.title : s.host);
        if (sesslog_open(s.log, what) != 0)
            term_printf("serial: the log %s can't be written: no log is kept\r\n", s.log);
    }
    mode_now();
    return -2;                                  /* (it goes on: shell_resume() at its end) */
}
