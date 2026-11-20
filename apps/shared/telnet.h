/* Telnet's own part of a connection (phase F; the owner's choice of 2026-11-20: Telnet sessions, as PuTTY has
   them): the commands and options that travel among the text (RFC 854, 855), taken out of what comes in and
   put into what goes out. Bytes in, bytes out: no network of its own, so the host's test drives it too
   (tests/host/telnet_test.c).

   What it agrees to: the far side's ECHO (857), SUPPRESS-GO-AHEAD (858) and BINARY (856) both ways, its own
   TERMINAL-TYPE (1091) and window size (NAWS, 1073). Everything else is refused, once. */
#ifndef TELNET_H
#define TELNET_H

struct telnet {
    void (*send)(void *ctx, const unsigned char *d, int n);     /* bytes for the network */
    void (*data)(void *ctx, const unsigned char *d, int n);     /* the far side's text, for the terminal */
    void *ctx;
    char ttype[16];                     /* what the terminal is called: "XTERM", "ANSI" */
    unsigned cols, rows;
    /* its own: */
    int st;
    unsigned char cmd;                  /* WILL, WONT, DO or DONT, whose option comes next */
    unsigned char sub[40];              /* a subnegotiation's bytes (the option first) */
    int nsub;
    unsigned char us[32], them[32];     /* a bit an option: what this end does, what the far end does */
    int size_told;                      /* the window's size has been sent (NAWS agreed) */
    int cr;                             /* a carriage return came last (a NUL after it is no text) */
};

void telnet_init(struct telnet *t, void (*send)(void *, const unsigned char *, int),
                 void (*data)(void *, const unsigned char *, int), void *ctx, const char *ttype, unsigned cols,
                 unsigned rows);
void telnet_offer(struct telnet *t);    /* said first, to a Telnet server (port 23): its type and size are on offer,
                                           and it would have go-aheads left out */
void telnet_input(struct telnet *t, const unsigned char *in, int n);   /* what came from the network */
void telnet_output(struct telnet *t, const unsigned char *d, int n);   /* what was typed: to the network, as Telnet
                                                                         carries it (a 255 doubled; a carriage
                                                                         return with its NUL, unless binary) */
void telnet_size(struct telnet *t, unsigned cols, unsigned rows);      /* the window's new size: told, if agreed */
int telnet_echoes(const struct telnet *t);      /* 1: the far side echoes what is typed (none of it here) */

#endif
