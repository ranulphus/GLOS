/* GLOS's SSH server (PRD §7, §8.3; M3 item 6): the protocol alone, driven
 * by bytes in and bytes out, so the same code runs in the kernel (the ssh
 * thread, kernel/ssh/sshd.c) and on the host (tests/host/sshd.c, against
 * OpenSSH). Each connection is used from one thread at a time.
 *
 * curve25519-sha256 (RFC 8731) with strict key exchange (OpenSSH's
 * kex-strict, against CVE-2023-48795), ssh-ed25519 host keys and user keys
 * (RFC 8709), chacha20-poly1305@openssh.com; re-keying whenever the client
 * asks; "session" channels with exec and subsystem requests, many per
 * connection, with window flow control (RFC 4254). Public keys only (D29),
 * for the one user "glos". */
#ifndef GLOS_SSH_H
#define GLOS_SSH_H
#include <stddef.h>
#include <stdint.h>

#define SSH_MAX_PACKET   35000                  /* RFC 4253 6.1: the least a server must accept */
#define SSH_MAX_AUTHKEYS 16
#define SSH_MAX_CHANNELS 8

struct ssh_conn;
struct ssh_chan;

/* What the platform provides. */
struct ssh_ops {
    void *(*alloc)(size_t n);                   /* zeroed */
    void (*free)(void *p);
    void (*send)(void *io, const uint8_t *data, uint32_t len);  /* to the TCP connection, in order */
    void (*log)(const char *fmt, ...);
    /* A channel request: start it and return 0, or -1 to refuse. Output goes
       through ssh_chan_write(), the end through ssh_chan_exit(). */
    int (*exec)(struct ssh_chan *ch, const char *cmd);
    int (*subsystem)(struct ssh_chan *ch, const char *name);
    void (*chan_data)(struct ssh_chan *ch, const uint8_t *data, uint32_t len);   /* the client's stdin */
    void (*chan_eof)(struct ssh_chan *ch);
    void (*chan_gone)(struct ssh_chan *ch);     /* closed both ways: drop any reference to ch */
};

struct ssh_server {
    const struct ssh_ops *ops;
    uint8_t host_sk[64], host_pk[32];
    uint8_t authkeys[SSH_MAX_AUTHKEYS][32];
    int n_authkeys;
    const char *version;                        /* "SSH-2.0-..." without CR LF */
};

/* A new connection: sends the version line and KEXINIT through ops->send. */
struct ssh_conn *ssh_conn_new(const struct ssh_server *srv, void *io);
/* Bytes from the client. 0, or -1: the connection is over (close it, then
   ssh_conn_free); a DISCONNECT has been sent where there was a reason to. */
int ssh_conn_input(struct ssh_conn *c, const uint8_t *data, uint32_t len);
void ssh_conn_free(struct ssh_conn *c);         /* every channel's chan_gone first */
int ssh_conn_authenticated(const struct ssh_conn *c);

/* Channels. user is the platform's own pointer. */
void *ssh_chan_user(const struct ssh_chan *ch);
void ssh_chan_set_user(struct ssh_chan *ch, void *user);
/* Output to the client: stream 0 is stdout, 1 stderr. Buffered while the
   client's window is shut; returns how much was buffered so far. */
uint32_t ssh_chan_write(struct ssh_chan *ch, int stream, const uint8_t *data, uint32_t len);
/* The command ended: exit-status, EOF and CLOSE once the output has gone. */
void ssh_chan_exit(struct ssh_chan *ch, uint32_t status);
uint32_t ssh_chan_pending(const struct ssh_chan *ch);       /* output bytes not yet sent */
/* Input at the platform's pace: after ssh_chan_hold(), the client's window
   reopens only as ssh_chan_consumed() reports input used, so what the
   platform buffers never exceeds one window (256 KB). */
void ssh_chan_hold(struct ssh_chan *ch);
void ssh_chan_consumed(struct ssh_chan *ch, uint32_t n);

#endif
