/* GLOS's SSH client (M10 step D, PRD D61; sdk/sshc): the protocol alone,
 * driven by bytes in and bytes out like the kernel's server (kernel/ssh/ssh.h),
 * so that the same code runs in the terminal (DJGPP) and on the host against
 * OpenSSH (tests/host/sshc_test.c).
 *
 * The algorithms are OpenSSH's defaults since 6.5, as the server's:
 * curve25519-sha256 with strict key exchange, ssh-ed25519 host keys,
 * chacha20-poly1305@openssh.com. A server without them is refused with the
 * reason. Re-keying when the server asks. Logins by an Ed25519 key, a
 * password, or keyboard-interactive answered with the password. "session"
 * channels with a pty, a shell, a command or a subsystem (sftp), several on a
 * connection, with window flow control. No forwarding yet. */
#ifndef GLOS_SSHC_H
#define GLOS_SSHC_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sshc;
struct sshc_chan;

struct sshc_ops {
    void (*send)(void *io, const uint8_t *data, uint32_t len);  /* to the TCP connection, in order */
    void (*log)(void *io, const char *what);                   /* (may be NULL) */
    /* The server's host key, at the first key exchange: 0 to go on, -1 to refuse it. fingerprint is
       "SHA256:..." as ssh-keygen -l shows it. A re-key must bring the same key. */
    int (*hostkey)(void *io, const uint8_t pk[32], const char *fingerprint);
    void (*banner)(void *io, const char *text);                 /* USERAUTH_BANNER (may be NULL) */
    /* A channel's events (any may be NULL). stream 0 stdout, 1 stderr. */
    void (*chan_data)(struct sshc_chan *ch, int stream, const uint8_t *data, uint32_t len);
    void (*chan_eof)(struct sshc_chan *ch);
    void (*chan_exit)(struct sshc_chan *ch, int status, const char *signal);   /* status -1 with a signal */
    void (*chan_closed)(struct sshc_chan *ch);                  /* gone: no more use of ch */
};

enum {
    SSHC_KEX,                           /* the first key exchange is under way */
    SSHC_AUTH,                          /* ready for a login (sshc_login) */
    SSHC_LOGGING_IN,
    SSHC_READY,                         /* logged in: channels */
    SSHC_DEAD                           /* over: sshc_why() */
};

struct sshc *sshc_new(const struct sshc_ops *ops, void *io, const char *version);  /* sends the version line and
                                           KEXINIT; version "SSH-2.0-..." or NULL for GLOS's */
int sshc_input(struct sshc *c, const uint8_t *data, uint32_t len);     /* bytes from the server: 0, or -1 (over) */
int sshc_state(const struct sshc *c);
const char *sshc_why(const struct sshc *c);     /* why it is over, or a login's failure */
void sshc_free(struct sshc *c);

/* A login (SSHC_AUTH): the key first if sk is given, then the password if the server will take one (password or
   keyboard-interactive), then failure. SSHC_READY once in; SSHC_AUTH again with sshc_why() if refused, and
   sshc_methods() the server's list. */
int sshc_login(struct sshc *c, const char *user, const uint8_t sk[64], const uint8_t pk[32], const char *password);
const char *sshc_methods(const struct sshc *c);

/* Channels (SSHC_READY). A "session" channel opened; then, in order, a pty (term NULL: none), and a shell
   (cmd NULL), a command, or a subsystem (subsystem set). Its requests are answered in order: sshc_chan_ok()
   says 1 once the last has been taken, -1 refused. */
struct sshc_chan *sshc_session(struct sshc *c, const char *term, unsigned cols, unsigned rows, const char *cmd,
                               const char *subsystem);
int sshc_chan_ok(const struct sshc_chan *ch);
uint32_t sshc_write(struct sshc_chan *ch, const uint8_t *data, uint32_t len);  /* buffered past the server's window:
                                           the bytes buffered and not yet sent */
void sshc_eof(struct sshc_chan *ch);    /* no more input from us */
void sshc_close(struct sshc_chan *ch);
void sshc_resize(struct sshc_chan *ch, unsigned cols, unsigned rows);
void sshc_signal(struct sshc_chan *ch, const char *name);      /* "INT" ... */
void sshc_consumed(struct sshc_chan *ch, uint32_t n);  /* with sshc_hold(): the window reopens as input is used */
void sshc_hold(struct sshc_chan *ch);
uint32_t sshc_pending(const struct sshc_chan *ch);     /* our bytes waiting for the server's window */
void *sshc_chan_user(const struct sshc_chan *ch);
void sshc_chan_set_user(struct sshc_chan *ch, void *user);
struct sshc *sshc_chan_conn(const struct sshc_chan *ch);
void *sshc_io(const struct sshc *c);

/* Keys and known hosts (sdk/sshc/keys.c). */
int sshc_keygen(uint8_t sk[64], uint8_t pk[32]);        /* (randombytes) */
int sshc_key_write(const char *path, const uint8_t sk[64], const uint8_t pk[32], const char *comment);
                                        /* PATH (OpenSSH's unencrypted private key) and PATH.PUB ("ssh-ed25519 ..."):
                                           0, or -1 */
int sshc_key_read(const char *path, uint8_t sk[64], uint8_t pk[32]);  /* 0, or -1 */
void sshc_fingerprint(char out[52], const uint8_t pk[32]);     /* "SHA256:..." */
/* KNOWNHST.TXT, OpenSSH's known_hosts format ("host ssh-ed25519 KEY", [host]:port for another port; hashed
   names aren't read): 1 known and the same, 0 unknown, -1 known with another key (a refusal), -2 no file. */
int sshc_known(const char *path, const char *host, unsigned port, const uint8_t pk[32]);
int sshc_known_add(const char *path, const char *host, unsigned port, const uint8_t pk[32]);

#ifdef __cplusplus
}
#endif
#endif
