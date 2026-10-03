/* SSH wire encoding (RFC 4251 section 5): a writer into a fixed buffer and
 * a reader over received bytes. Both fail safe: the first overrun sets
 * `err` and every later call does nothing (the caller checks err once).
 * Portable: kernel/ssh builds into the kernel and into tests/host/sshd. */
#ifndef GLOS_SSHBUF_H
#define GLOS_SSHBUF_H
#include <stddef.h>
#include <stdint.h>

struct sbuf {
    uint8_t *p;
    uint32_t len, cap;
    int err;
};

struct sread {
    const uint8_t *p;
    uint32_t len, pos;
    int err;
};

void sb_init(struct sbuf *b, uint8_t *p, uint32_t cap);
void sb_u8(struct sbuf *b, uint8_t v);
void sb_u32(struct sbuf *b, uint32_t v);
void sb_bytes(struct sbuf *b, const void *p, uint32_t n);
void sb_string(struct sbuf *b, const void *p, uint32_t n);
void sb_cstr(struct sbuf *b, const char *s);
void sb_mpint(struct sbuf *b, const uint8_t *be, uint32_t n);   /* an unsigned big-endian number */

void sr_init(struct sread *r, const void *p, uint32_t len);
uint8_t sr_u8(struct sread *r);
uint32_t sr_u32(struct sread *r);
const uint8_t *sr_string(struct sread *r, uint32_t *n);
const uint8_t *sr_bytes(struct sread *r, uint32_t n);
int sr_streq(const uint8_t *p, uint32_t n, const char *s);

/* Name-list (comma-separated) helpers. */
int namelist_has(const uint8_t *list, uint32_t n, const char *name);
const char *namelist_pick(const uint8_t *client, uint32_t n, const char *const *ours);  /* first client name we have */

static inline uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static inline void put_be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* Keys (sshkeys.c): OpenSSH's unencrypted ed25519 private key file and
   authorized_keys lines. */
int ssh_parse_privkey(const char *text, uint32_t n, uint8_t sk[64], uint8_t pk[32]);
int ssh_parse_authkeys(const char *text, uint32_t n, uint8_t (*pks)[32], int max);     /* how many */
uint32_t b64_decode(uint8_t *out, uint32_t cap, const char *in, uint32_t n);
uint32_t b64_encode(char *out, uint32_t cap, const uint8_t *in, uint32_t n);

#endif
