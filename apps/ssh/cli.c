/* SSH.EXE, SFTP.EXE and SSHKEY.EXE (docs/v1/shell.md, step S2c): the SSH client, its file transfer and its
 * key maker as programs of their own, run by GLOS's shell at any terminal: the Terminal app's, or an SSH
 * login's (a hop from one PC to the next).
 *
 * The client itself is ssh.c (the Terminal app's built-in one until S2c), and this file is the terminal it
 * speaks to: the one the program was started at, by API 2.1's GLOS_INIT_TERMINAL and SYS's TERMINAL. A session's bytes pass both ways as they are (the terminal
 * at the far end of the chain draws them); the questions (a new host key, a password, sftp's commands) are
 * read a line at a time here. With no terminal (ssh pc "SSH host command", a batch file) what it says goes
 * to its standard output, and a question can't be asked: it ends.
 *
 * Built three times: CLI_WHICH 0 ssh, 1 sftp, 2 ssh-keygen. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>

#include "cli.h"

static int have_term, gone, resumed, exit_code, hidden, asking, esc;
static unsigned cols = 80, rows = 24;
static char line[512], home[260];
static int nline;

/* ---- what the client asks of its terminal (term.h) */

static void put(const char *d, int n)
{
    int k;
    if (!have_term) {
        fwrite(d, 1, (size_t)n, stdout);
        fflush(stdout);
        return;
    }
    while (n > 0 && !gone) {
        if ((k = glos_terminal_write(d, (unsigned)n)) < 0) {
            gone = 1;                           /* (the terminal is nobody's any more) */
            return;
        }
        d += k, n -= k;
        if (n)                                  /* (its reader is behind: a moment) */
            glos_sleep(10);
    }
}

void term_raw(const char *d, int n) { put(d, n); }

void term_write(const char *s, int n)           /* (a line feed alone gets its carriage return) */
{
    int i, from = 0;
    if (n < 0)
        n = (int)strlen(s);
    for (i = 0; i < n; i++)
        if (s[i] == '\n' && (!i || s[i - 1] != '\r')) {
            put(s + from, i - from);
            put("\r\n", 2);
            from = i + 1;
        }
    put(s + from, n - from);
}

void term_print(const char *s) { term_write(s, -1); }

void term_printf(const char *fmt, ...)
{
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    term_write(b, -1);
}

void term_attr(int a)
{
    if (have_term)
        put(a == T_ERROR ? "\033[31m" : "\033[0m", a == T_ERROR ? 5 : 4);
}

void term_title(const char *t)
{
    char b[160];
    if (have_term && *t)
        put(b, snprintf(b, sizeof b, "\033]0;%.120s\007", t));
}

void term_prompt_done(void) { asking = 1, nline = 0; }
void term_input_hidden(int on) { hidden = on; }
void term_size(int *c, int *r) { *c = (int)cols, *r = (int)rows; }
const char *term_home(void) { return home; }
void shell_resume(int code) { resumed = 1, exit_code = code; }

/* ---- what was typed */

static void typed(const unsigned char *b, int n)
{
    int i;
    if (ssh_screen()) {                         /* a session at this terminal: its, as they are */
        ssh_raw((const char *)b, n);
        return;
    }
    for (i = 0; i < n && !resumed; i++) {
        unsigned char c = b[i];
        if (esc) {                              /* (a key's sequence: nothing here) */
            if (esc == 1)
                esc = c == '[' || c == 'O' ? 2 : 0;
            else if (c >= 0x40)
                esc = 0;
            continue;
        }
        if (c == 3) {                           /* Ctrl+C */
            put("^C\r\n", 4);
            asking = 0;
            ssh_break();
        } else if (c == 27) {
            esc = 1;
        } else if (!asking || !ssh_asking()) {
            continue;                           /* (nothing is asked: what is typed is nobody's) */
        } else if (c == '\r' || c == '\n') {
            line[nline] = 0;
            asking = 0;
            put("\r\n", 2);
            ssh_answer(line);                   /* (it may ask again) */
            memset(line, 0, sizeof line);
        } else if (c == 4 && !nline) {          /* Ctrl+D at an empty line: sftp's end */
            asking = 0;
            put("\r\n", 2);
            ssh_answer("exit");
        } else if (c == 8 || c == 127) {
            if (nline) {
                nline--;
                if (!hidden)
                    put("\b \b", 3);
            }
        } else if (c >= 32 && nline < (int)sizeof line - 1) {
            line[nline++] = (char)c;
            if (!hidden)
                put((const char *)&c, 1);
        }
    }
}

int main(int argc, char **argv)
{
    static const char *const names[] = { "SSH", "SFTP", "SSH key maker" };
    unsigned char ev[GLOS_EV_MAX], b[256];
    unsigned c2, r2;
    char *p;
    int r, k;
    snprintf(home, sizeof home, "%s", argv[0]);
    for (p = home; *p; p++)
        if (*p == '/')
            *p = '\\';
    if ((p = strrchr(home, '\\')) != 0)
        *p = 0;
    else
        strcpy(home, "C:\\GLOS");
    if (glos_init_as(names[CLI_WHICH], "", "", GLOS_INIT_TERMINAL) != 0) {
        printf("%s runs under GLOS.\n", names[CLI_WHICH]);
        return 2;
    }
    have_term = glos_terminal_size(&cols, &rows) == 0;
    r = CLI_WHICH == 2 ? keygen_command(argc, argv) : CLI_WHICH == 1 ? sftp_command(argc, argv) : ssh_command(argc, argv);
    if (r != -2)
        return r;
    while (!resumed) {
        glos_event(ev, sizeof ev, 50, (1u << GLOS_AREA_NET) | (1u << GLOS_AREA_SYS));
        if (have_term && !gone) {
            if (glos_terminal_size(&c2, &r2) == 0 && (c2 != cols || r2 != rows)) {
                cols = c2, rows = r2;
                ssh_resize((int)cols, (int)rows);
            }
            while ((k = glos_terminal_read(b, sizeof b)) > 0)
                typed(b, k);
            if (k < 0)
                gone = 1;
        }
        if (resumed)
            break;
        if (gone || (asking && !have_term)) {   /* nobody to show it to, or to answer */
            if (!have_term)
                term_print("no terminal to answer at\n");
            asking = 0;
            ssh_break();
            if (!resumed)
                break;
        }
        ssh_poll();
    }
    return resumed ? exit_code : 255;
}
