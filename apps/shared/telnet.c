/* Telnet's own part of a connection (telnet.h). Plain C. */
#include <stdio.h>
#include <string.h>

#include "telnet.h"

enum { IAC = 255, DONT = 254, DO = 253, WONT = 252, WILL = 251, SB = 250, SE = 240 };
enum { O_BINARY = 0, O_ECHO = 1, O_SGA = 3, O_TTYPE = 24, O_NAWS = 31 };
enum { S_TEXT, S_IAC, S_OPT, S_SUB, S_SUB_IAC };

static int has(const unsigned char *set, unsigned o) { return set[o >> 3] >> (o & 7) & 1; }

static void put(unsigned char *set, unsigned o, int on)
{
    if (on)
        set[o >> 3] |= (unsigned char)(1u << (o & 7));
    else
        set[o >> 3] &= (unsigned char)~(1u << (o & 7));
}

static void say(struct telnet *t, unsigned cmd, unsigned opt)
{
    unsigned char m[3];
    m[0] = IAC, m[1] = (unsigned char)cmd, m[2] = (unsigned char)opt;
    t->send(t->ctx, m, 3);
}

static void size_out(struct telnet *t)          /* IAC SB NAWS cols rows IAC SE, a 255 among them doubled */
{
    unsigned char m[16], v[4];
    int n = 0, i;
    v[0] = (unsigned char)(t->cols >> 8), v[1] = (unsigned char)t->cols;
    v[2] = (unsigned char)(t->rows >> 8), v[3] = (unsigned char)t->rows;
    m[n++] = IAC, m[n++] = SB, m[n++] = O_NAWS;
    for (i = 0; i < 4; i++) {
        m[n++] = v[i];
        if (v[i] == IAC)
            m[n++] = IAC;
    }
    m[n++] = IAC, m[n++] = SE;
    t->send(t->ctx, m, n);
    t->size_told = 1;
}

void telnet_init(struct telnet *t, void (*send)(void *, const unsigned char *, int),
                 void (*data)(void *, const unsigned char *, int), void *ctx, const char *ttype, unsigned cols,
                 unsigned rows)
{
    memset(t, 0, sizeof *t);
    t->send = send, t->data = data, t->ctx = ctx;
    snprintf(t->ttype, sizeof t->ttype, "%s", ttype && *ttype ? ttype : "XTERM");
    t->cols = cols, t->rows = rows;
}

void telnet_offer(struct telnet *t)
{
    put(t->us, O_TTYPE, 1);
    say(t, WILL, O_TTYPE);
    put(t->us, O_NAWS, 1);
    say(t, WILL, O_NAWS);
    put(t->them, O_SGA, 1);
    say(t, DO, O_SGA);
}

/* The far side says what it will do, or asks what this side should: answered where it changes anything. */
static void option(struct telnet *t, unsigned cmd, unsigned o)
{
    switch (cmd) {
    case WILL:
        if (o == O_ECHO || o == O_SGA || o == O_BINARY) {
            if (!has(t->them, o)) {
                put(t->them, o, 1);
                say(t, DO, o);
            }
        } else {
            say(t, DONT, o);
        }
        break;
    case WONT:
        if (has(t->them, o)) {
            put(t->them, o, 0);
            say(t, DONT, o);
        }
        break;
    case DO:
        if (o == O_TTYPE || o == O_NAWS || o == O_SGA || o == O_BINARY) {
            if (!has(t->us, o)) {
                put(t->us, o, 1);
                say(t, WILL, o);
            }
            if (o == O_NAWS && !t->size_told)
                size_out(t);
        } else {
            say(t, WONT, o);
        }
        break;
    case DONT:
        if (has(t->us, o)) {
            put(t->us, o, 0);
            say(t, WONT, o);
            if (o == O_NAWS)
                t->size_told = 0;
        }
        break;
    }
}

static void subnegotiation(struct telnet *t)    /* TERMINAL-TYPE SEND: what the terminal is called */
{
    if (t->nsub >= 2 && t->sub[0] == O_TTYPE && t->sub[1] == 1 && has(t->us, O_TTYPE)) {
        unsigned char m[8 + sizeof t->ttype];
        int n = 0, k = (int)strlen(t->ttype);
        m[n++] = IAC, m[n++] = SB, m[n++] = O_TTYPE, m[n++] = 0;
        memcpy(m + n, t->ttype, (size_t)k);
        n += k;
        m[n++] = IAC, m[n++] = SE;
        t->send(t->ctx, m, n);
    }
}

void telnet_input(struct telnet *t, const unsigned char *in, int n)
{
    unsigned char out[256];
    int i, k = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = in[i];
        switch (t->st) {
        case S_IAC:
            if (c == IAC) {                     /* a 255 that is text */
                out[k++] = c;
                t->cr = 0;
                t->st = S_TEXT;
            } else if (c == WILL || c == WONT || c == DO || c == DONT) {
                t->cmd = c;
                t->st = S_OPT;
            } else if (c == SB) {
                t->nsub = 0;
                t->st = S_SUB;
            } else {                            /* (NOP, GA, AYT and their like: nothing of this side's) */
                t->st = S_TEXT;
            }
            break;
        case S_OPT:
            if (k) {                            /* (the text before it first: in its order) */
                t->data(t->ctx, out, k);
                k = 0;
            }
            option(t, t->cmd, c);
            t->st = S_TEXT;
            break;
        case S_SUB:
            if (c == IAC)
                t->st = S_SUB_IAC;
            else if (t->nsub < (int)sizeof t->sub)
                t->sub[t->nsub++] = c;
            break;
        case S_SUB_IAC:
            if (c == SE) {
                subnegotiation(t);
                t->st = S_TEXT;
            } else {                            /* (a doubled 255 within it; anything else: it goes on) */
                if (c == IAC && t->nsub < (int)sizeof t->sub)
                    t->sub[t->nsub++] = c;
                t->st = S_SUB;
            }
            break;
        default:
            if (c == IAC) {
                t->st = S_IAC;
            } else if (c == 0 && t->cr && !has(t->them, O_BINARY)) {
                t->cr = 0;                      /* (a carriage return's NUL: no text) */
            } else {
                t->cr = c == '\r';
                out[k++] = c;
            }
        }
        if (k == (int)sizeof out) {
            t->data(t->ctx, out, k);
            k = 0;
        }
    }
    if (k)
        t->data(t->ctx, out, k);
}

void telnet_output(struct telnet *t, const unsigned char *d, int n)
{
    unsigned char out[256];
    int i, k = 0;
    for (i = 0; i < n; i++) {
        out[k++] = d[i];
        if (d[i] == IAC)
            out[k++] = IAC;
        else if (d[i] == '\r' && !has(t->us, O_BINARY) && !(i + 1 < n && d[i + 1] == '\n'))
            out[k++] = 0;                       /* (a carriage return alone: Telnet's, with its NUL; with a line
                                                   feed it is a line's end as it stands) */
        if (k >= (int)sizeof out - 2) {
            t->send(t->ctx, out, k);
            k = 0;
        }
    }
    if (k)
        t->send(t->ctx, out, k);
}

void telnet_size(struct telnet *t, unsigned cols, unsigned rows)
{
    if (cols == t->cols && rows == t->rows)
        return;
    t->cols = cols, t->rows = rows;
    if (has(t->us, O_NAWS) && t->size_told)
        size_out(t);
}

int telnet_echoes(const struct telnet *t) { return has(t->them, O_ECHO); }
