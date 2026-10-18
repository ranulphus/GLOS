/* GLOS's SSH client: the transport (RFC 4253), user authentication
 * (RFC 4252, RFC 4256's keyboard-interactive) and connection (RFC 4254)
 * layers, the client's side of kernel/ssh/ssh.c, with the same packet code
 * (chacha20-poly1305@openssh.com: K_1 encrypts the length, K_2 the rest,
 * block 0 of K_2 is the Poly1305 key) and wire encoding (kernel/ssh/sshbuf.c).
 * See glos/sshc.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_hash_sha256.h"
#include "crypto_onetimeauth_poly1305.h"
#include "crypto_scalarmult_curve25519.h"
#include "crypto_sign_ed25519.h"
#include "crypto_stream_chacha20.h"
#include "crypto_verify_16.h"
#include "randombytes.h"
#include "sshbuf.h"
#include <glos/sshc.h>

enum {
    MSG_DISCONNECT = 1, MSG_IGNORE = 2, MSG_UNIMPLEMENTED = 3, MSG_DEBUG = 4, MSG_SERVICE_REQUEST = 5,
    MSG_SERVICE_ACCEPT = 6, MSG_EXT_INFO = 7,
    MSG_KEXINIT = 20, MSG_NEWKEYS = 21, MSG_KEX_ECDH_INIT = 30, MSG_KEX_ECDH_REPLY = 31,
    MSG_USERAUTH_REQUEST = 50, MSG_USERAUTH_FAILURE = 51, MSG_USERAUTH_SUCCESS = 52, MSG_USERAUTH_BANNER = 53,
    MSG_USERAUTH_INFO_REQUEST = 60, MSG_USERAUTH_INFO_RESPONSE = 61,
    MSG_GLOBAL_REQUEST = 80, MSG_REQUEST_SUCCESS = 81, MSG_REQUEST_FAILURE = 82,
    MSG_CHANNEL_OPEN = 90, MSG_CHANNEL_OPEN_CONFIRMATION = 91, MSG_CHANNEL_OPEN_FAILURE = 92,
    MSG_CHANNEL_WINDOW_ADJUST = 93, MSG_CHANNEL_DATA = 94, MSG_CHANNEL_EXTENDED_DATA = 95, MSG_CHANNEL_EOF = 96,
    MSG_CHANNEL_CLOSE = 97, MSG_CHANNEL_REQUEST = 98, MSG_CHANNEL_SUCCESS = 99, MSG_CHANNEL_FAILURE = 100,
};

enum { DISC_PROTOCOL_ERROR = 2, DISC_KEX_FAILED = 3, DISC_HOST_KEY_NOT_VERIFIABLE = 9, DISC_BY_APPLICATION = 11 };

#define MAX_PACKET 35000
#define RX_CAP    (4 + MAX_PACKET + 16)
#define WORK_CAP  (64 + MAX_PACKET + 16)
#define TX_PAYLOAD 65
#define WINDOW    0x40000u                      /* what the server may send ahead on a channel */
#define MAXPKT    32768u
#define OUT_MAX   0x40000u
#define NCHAN     8
#define VERSION   "SSH-2.0-GLOS_1.0"

static const char *const our_kex[] = { "curve25519-sha256", "curve25519-sha256@libssh.org", NULL };
static const char *const our_hostkey[] = { "ssh-ed25519", NULL };
static const char *const our_cipher[] = { "chacha20-poly1305@openssh.com", NULL };
static const char *const our_comp[] = { "none", NULL };

struct sshc_chan {
    struct sshc *conn;
    uint32_t id, peer, peer_window, peer_maxpkt, our_window;
    uint8_t confirmed, eof_sent, eof_recv, close_sent, close_recv, held, eof_pending;
    int replies;                                /* requests sent with want-reply, not yet answered */
    int refused;
    char term[32], cmd[512], subsystem[32];
    unsigned cols, rows;
    uint32_t unconsumed;
    uint8_t *out;
    uint32_t out_len, out_cap;
    void *user;
};

struct sshc {
    const struct sshc_ops *ops;
    void *io;
    char version[64];
    int state, got_version, dead;
    uint8_t *rx, *rwork, *twork;
    uint32_t rx_len, rx_plen;
    int rx_enc, tx_enc;
    uint8_t rx_key[64], tx_key[64], next_rx_key[64];
    uint32_t rx_seq, tx_seq;
    int kex;                                    /* 0 none, 1 our KEXINIT sent, 2 ECDH_INIT sent, 3 our NEWKEYS sent */
    int first_kex_done, strict, ignore_next;
    uint8_t *ic, *is;
    uint32_t ic_len, is_len;
    char vs[256];
    uint32_t vs_len;
    uint8_t e[32], qc[32];                      /* our ECDH secret and public value */
    uint8_t session_id[32], host_pk[32];
    char why[160], methods[128];
    /* the login */
    char user[64], password[128];
    uint8_t sk[64], pk[32];
    int have_key, tried_none, tried_key, tried_password, tried_kbd;
    struct sshc_chan *chans[NCHAN];
};

static int handle(struct sshc *c, const uint8_t *p, uint32_t n);
static void flush(struct sshc_chan *ch);

static void say(struct sshc *c, const char *what)
{
    if (c->ops->log)
        c->ops->log(c->io, what);
}

/* ---- packets (as kernel/ssh/ssh.c) */

static void nonce_of(uint8_t n[8], uint32_t seq)
{
    memset(n, 0, 4);
    put_be32(n + 4, seq);
}

static void send_packet(struct sshc *c, const uint8_t *payload, uint32_t len)
{
    uint8_t *w = c->twork, nonce[8], len4[4], ks[64];
    uint32_t pad, plen;
    if (c->dead || len > MAX_PACKET - 64)
        return;
    if (payload != w + TX_PAYLOAD)
        memmove(w + TX_PAYLOAD, payload, len);
    if (!c->tx_enc) {
        pad = 8 - ((5 + len) % 8);
        if (pad < 4)
            pad += 8;
        plen = 1 + len + pad;
        put_be32(w + 60, plen);
        w[64] = (uint8_t)pad;
        randombytes(w + TX_PAYLOAD + len, pad);
        c->ops->send(c->io, w + 60, 4 + plen);
    } else {
        pad = 8 - ((1 + len) % 8);
        if (pad < 4)
            pad += 8;
        plen = 1 + len + pad;
        nonce_of(nonce, c->tx_seq);
        memset(w, 0, 64);
        w[64] = (uint8_t)pad;
        randombytes(w + TX_PAYLOAD + len, pad);
        crypto_stream_chacha20_xor(w, w, 64 + plen, nonce, c->tx_key);
        put_be32(len4, plen);
        crypto_stream_chacha20(ks, 4, nonce, c->tx_key + 32);
        w[60] = len4[0] ^ ks[0];
        w[61] = len4[1] ^ ks[1];
        w[62] = len4[2] ^ ks[2];
        w[63] = len4[3] ^ ks[3];
        crypto_onetimeauth_poly1305(w + 64 + plen, w + 60, 4 + plen, w);
        c->ops->send(c->io, w + 60, 4 + plen + 16);
    }
    c->tx_seq++;
}

static void send_sb(struct sshc *c, struct sbuf *b)
{
    if (!b->err)
        send_packet(c, b->p, b->len);
}

static int die(struct sshc *c, uint32_t reason, const char *why)
{
    uint8_t m[256];
    struct sbuf b;
    if (!c->dead) {
        sb_init(&b, m, sizeof m);
        sb_u8(&b, MSG_DISCONNECT);
        sb_u32(&b, reason);
        sb_cstr(&b, why);
        sb_cstr(&b, "");
        send_sb(c, &b);
        if (!c->why[0])
            snprintf(c->why, sizeof c->why, "%s", why);
    }
    c->dead = 1;
    c->state = SSHC_DEAD;
    return -1;
}

/* ---- key exchange */

static void send_kexinit(struct sshc *c)
{
    uint8_t m[512], cookie[16];
    struct sbuf b;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_KEXINIT);
    randombytes(cookie, 16);
    sb_bytes(&b, cookie, 16);
    sb_cstr(&b, c->first_kex_done ? "curve25519-sha256,curve25519-sha256@libssh.org"
                                  : "curve25519-sha256,curve25519-sha256@libssh.org,kex-strict-c-v00@openssh.com");
    sb_cstr(&b, "ssh-ed25519");
    sb_cstr(&b, "chacha20-poly1305@openssh.com");
    sb_cstr(&b, "chacha20-poly1305@openssh.com");
    sb_cstr(&b, "hmac-sha2-256");                /* never used: the cipher authenticates */
    sb_cstr(&b, "hmac-sha2-256");
    sb_cstr(&b, "none");
    sb_cstr(&b, "none");
    sb_cstr(&b, "");
    sb_cstr(&b, "");
    sb_u8(&b, 0);
    sb_u32(&b, 0);
    if (b.err)
        return;
    free(c->ic);
    if (!(c->ic = malloc(b.len)))
        return;
    memcpy(c->ic, m, b.len);
    c->ic_len = b.len;
    send_packet(c, m, b.len);
    c->kex = 1;
}

static void send_ecdh_init(struct sshc *c)
{
    uint8_t m[64];
    struct sbuf b;
    randombytes(c->e, 32);
    crypto_scalarmult_curve25519_base(c->qc, c->e);
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_KEX_ECDH_INIT);
    sb_string(&b, c->qc, 32);
    send_sb(c, &b);
    c->kex = 2;
}

static int kexinit(struct sshc *c, const uint8_t *p, uint32_t n)
{
    struct sread r;
    const uint8_t *l[10];
    uint32_t ln[10];
    int i, follows;
    if (c->kex >= 2)
        return die(c, DISC_PROTOCOL_ERROR, "kexinit during a key exchange");
    sr_init(&r, p + 1, n - 1);
    sr_bytes(&r, 16);
    for (i = 0; i < 10; i++)
        l[i] = sr_string(&r, &ln[i]);
    follows = sr_u8(&r);
    sr_u32(&r);
    if (r.err)
        return die(c, DISC_PROTOCOL_ERROR, "a bad kexinit");
    if (!c->first_kex_done) {
        c->strict = namelist_has(l[0], ln[0], "kex-strict-s-v00@openssh.com");
        if (c->strict && c->rx_seq != 1)
            return die(c, DISC_PROTOCOL_ERROR, "strict kex: kexinit not first");
    }
    /* (the server's lists are the ones looked through, ours picked from: with one algorithm each, the result is
       the same as RFC 4253's rule) */
    if (!namelist_pick(l[0], ln[0], our_kex))
        return die(c, DISC_KEX_FAILED, "the server has no curve25519-sha256 (it needs OpenSSH 6.5 or later)");
    if (!namelist_pick(l[1], ln[1], our_hostkey))
        return die(c, DISC_KEX_FAILED, "the server has no ssh-ed25519 host key");
    if (!namelist_pick(l[2], ln[2], our_cipher) || !namelist_pick(l[3], ln[3], our_cipher))
        return die(c, DISC_KEX_FAILED, "the server has no chacha20-poly1305@openssh.com");
    if (!namelist_pick(l[6], ln[6], our_comp) || !namelist_pick(l[7], ln[7], our_comp))
        return die(c, DISC_KEX_FAILED, "the server wants compression");
    if (follows) {                              /* a guessed packet: ignored unless its guess was ours */
        uint32_t k = 0;
        while (k < ln[0] && l[0][k] != ',') k++;
        c->ignore_next = !((k == 17 && !memcmp(l[0], "curve25519-sha256", 17))
                           || (k == 28 && !memcmp(l[0], "curve25519-sha256@libssh.org", 28)));
    }
    free(c->is);
    if (!(c->is = malloc(n)))
        return die(c, DISC_BY_APPLICATION, "out of memory");
    memcpy(c->is, p, n);
    c->is_len = n;
    if (c->kex == 0)                            /* the server started a re-key */
        send_kexinit(c);
    send_ecdh_init(c);
    return 0;
}

static void derive(uint8_t out[64], const uint8_t *kmp, uint32_t kmp_len, const uint8_t h[32], char letter,
                   const uint8_t sid[32])
{
    uint8_t buf[48 + 32 + 1 + 32];
    memcpy(buf, kmp, kmp_len);
    memcpy(buf + kmp_len, h, 32);
    buf[kmp_len + 32] = (uint8_t)letter;
    memcpy(buf + kmp_len + 33, sid, 32);
    crypto_hash_sha256(out, buf, kmp_len + 65);
    memcpy(buf + kmp_len + 32, out, 32);
    crypto_hash_sha256(out + 32, buf, kmp_len + 64);
    memset(buf, 0, sizeof buf);
}

void sshc_fingerprint(char out[52], const uint8_t pk[32])
{
    uint8_t blob[51], h[32];
    struct sbuf b;
    sb_init(&b, blob, sizeof blob);
    sb_cstr(&b, "ssh-ed25519");
    sb_string(&b, pk, 32);
    crypto_hash_sha256(h, blob, b.len);
    memcpy(out, "SHA256:", 7);
    b64_encode(out + 7, 45, h, 32);
    out[7 + 43] = 0;                            /* (as ssh-keygen -l: no '=') */
}

static int ecdh_reply(struct sshc *c, const uint8_t *p, uint32_t n)
{
    struct sread r, kb;
    struct sbuf hb, kmb;
    const uint8_t *ks, *qs, *sig, *t, *pk;
    uint32_t kslen, qslen, siglen, tlen, pklen, kmp_len;
    uint8_t k[32], h[32], kmp[40], *hbuf, sm[96 + 32], key[64], m[1];
    unsigned long long mlen;
    char fp[52];
    int i, zero = 0;
    if (c->kex != 2)
        return die(c, DISC_PROTOCOL_ERROR, "an unexpected ecdh reply");
    sr_init(&r, p + 1, n - 1);
    ks = sr_string(&r, &kslen);
    qs = sr_string(&r, &qslen);
    sig = sr_string(&r, &siglen);
    if (r.err || qslen != 32)
        return die(c, DISC_KEX_FAILED, "a bad ecdh reply");
    sr_init(&kb, ks, kslen);
    t = sr_string(&kb, &tlen);
    pk = sr_string(&kb, &pklen);
    if (kb.err || pklen != 32 || !sr_streq(t, tlen, "ssh-ed25519"))
        return die(c, DISC_KEX_FAILED, "the host key isn't ssh-ed25519");
    crypto_scalarmult_curve25519(k, c->e, qs);
    for (i = 0; i < 32; i++)
        zero |= k[i];
    if (!zero)
        return die(c, DISC_KEX_FAILED, "a bad shared secret");
    sb_init(&kmb, kmp, sizeof kmp);
    sb_mpint(&kmb, k, 32);
    kmp_len = kmb.len;
    if (!(hbuf = malloc(1024 + c->ic_len + c->is_len)))
        return die(c, DISC_BY_APPLICATION, "out of memory");
    sb_init(&hb, hbuf, 1024 + c->ic_len + c->is_len);
    sb_cstr(&hb, c->version);
    sb_string(&hb, c->vs, c->vs_len);
    sb_string(&hb, c->ic, c->ic_len);
    sb_string(&hb, c->is, c->is_len);
    sb_string(&hb, ks, kslen);
    sb_string(&hb, c->qc, 32);
    sb_string(&hb, qs, 32);
    sb_bytes(&hb, kmp, kmp_len);
    crypto_hash_sha256(h, hbuf, hb.len);
    free(hbuf);
    if (hb.err)
        return die(c, DISC_BY_APPLICATION, "a key exchange too big");
    /* the signature: string "ssh-ed25519", string 64 bytes; over H */
    sr_init(&kb, sig, siglen);
    t = sr_string(&kb, &tlen);
    if (kb.err || !sr_streq(t, tlen, "ssh-ed25519") || !(t = sr_string(&kb, &tlen)) || tlen != 64)
        return die(c, DISC_KEX_FAILED, "a bad host signature");
    memcpy(sm, t, 64);
    memcpy(sm + 64, h, 32);
    if (crypto_sign_ed25519_open(sm, &mlen, sm, 96, pk) != 0)
        return die(c, DISC_KEX_FAILED, "the host's signature is wrong");
    if (!c->first_kex_done) {
        memcpy(c->session_id, h, 32);
        memcpy(c->host_pk, pk, 32);
        sshc_fingerprint(fp, pk);
        if (c->ops->hostkey && c->ops->hostkey(c->io, pk, fp) != 0)
            return die(c, DISC_HOST_KEY_NOT_VERIFIABLE, "host key refused");
    } else if (memcmp(c->host_pk, pk, 32)) {
        return die(c, DISC_HOST_KEY_NOT_VERIFIABLE, "the host key changed in a re-key");
    }
    m[0] = MSG_NEWKEYS;
    send_packet(c, m, 1);
    derive(key, kmp, kmp_len, h, 'C', c->session_id);  /* client to server */
    memcpy(c->tx_key, key, 64);
    c->tx_enc = 1;
    if (c->strict)
        c->tx_seq = 0;
    derive(c->next_rx_key, kmp, kmp_len, h, 'D', c->session_id);
    c->kex = 3;
    memset(c->e, 0, sizeof c->e);
    memset(k, 0, sizeof k);
    memset(kmp, 0, sizeof kmp);
    memset(key, 0, sizeof key);
    return 0;
}

static int newkeys(struct sshc *c)
{
    int i, first = !c->first_kex_done;
    if (c->kex != 3)
        return die(c, DISC_PROTOCOL_ERROR, "an unexpected newkeys");
    memcpy(c->rx_key, c->next_rx_key, 64);
    memset(c->next_rx_key, 0, 64);
    c->rx_enc = 1;
    if (c->strict)
        c->rx_seq = 0;
    c->first_kex_done = 1;
    c->kex = 0;
    free(c->ic);
    free(c->is);
    c->ic = c->is = NULL;
    if (first) {
        uint8_t m[32];
        struct sbuf b;
        say(c, c->strict ? "kex done strict=1" : "kex done strict=0");
        sb_init(&b, m, sizeof m);
        sb_u8(&b, MSG_SERVICE_REQUEST);
        sb_cstr(&b, "ssh-userauth");
        send_sb(c, &b);
    }
    for (i = 0; i < NCHAN; i++)
        if (c->chans[i])
            flush(c->chans[i]);
    return 0;
}

/* ---- the login */

static void auth_next(struct sshc *c);

static void request_head(struct sbuf *b, struct sshc *c, const char *method)
{
    sb_u8(b, MSG_USERAUTH_REQUEST);
    sb_cstr(b, c->user);
    sb_cstr(b, "ssh-connection");
    sb_cstr(b, method);
}

static void auth_key(struct sshc *c)
{
    uint8_t blob[51], data[512], sm[64 + 512], m[700];
    unsigned long long smlen;
    struct sbuf b, bb;
    sb_init(&bb, blob, sizeof blob);
    sb_cstr(&bb, "ssh-ed25519");
    sb_string(&bb, c->pk, 32);
    sb_init(&b, data, sizeof data);             /* what is signed (RFC 4252 7) */
    sb_string(&b, c->session_id, 32);
    request_head(&b, c, "publickey");
    sb_u8(&b, 1);
    sb_cstr(&b, "ssh-ed25519");
    sb_string(&b, blob, bb.len);
    if (b.err)
        return;
    crypto_sign_ed25519(sm, &smlen, data, b.len, c->sk);
    sb_init(&b, m, sizeof m);
    request_head(&b, c, "publickey");
    sb_u8(&b, 1);
    sb_cstr(&b, "ssh-ed25519");
    sb_string(&b, blob, bb.len);
    sb_u32(&b, 4 + 11 + 4 + 64);
    sb_cstr(&b, "ssh-ed25519");
    sb_string(&b, sm, 64);
    send_sb(c, &b);
    memset(sm, 0, sizeof sm);
}

static int offered(const struct sshc *c, const char *method)
{
    return !c->methods[0] || namelist_has((const uint8_t *)c->methods, (uint32_t)strlen(c->methods), method);
}

/* The next way in, after a refusal (or at the start): none left, SSHC_AUTH again. */
static void auth_next(struct sshc *c)
{
    uint8_t m[400];
    struct sbuf b;
    if (!c->have_key && !c->password[0] && !c->methods[0] && !c->tried_none) {
        c->tried_none = 1;                      /* (nothing to offer yet: "none", for the server's list) */
        sb_init(&b, m, sizeof m);
        request_head(&b, c, "none");
        send_sb(c, &b);
        return;
    }
    if (c->have_key && !c->tried_key && offered(c, "publickey")) {
        c->tried_key = 1;
        auth_key(c);
        return;
    }
    if (c->password[0] && !c->tried_password && offered(c, "password")) {
        c->tried_password = 1;
        sb_init(&b, m, sizeof m);
        request_head(&b, c, "password");
        sb_u8(&b, 0);
        sb_cstr(&b, c->password);
        send_sb(c, &b);
        memset(m, 0, sizeof m);
        return;
    }
    if (c->password[0] && !c->tried_kbd && offered(c, "keyboard-interactive")) {
        c->tried_kbd = 1;
        sb_init(&b, m, sizeof m);
        request_head(&b, c, "keyboard-interactive");
        sb_cstr(&b, "");
        sb_cstr(&b, "");
        send_sb(c, &b);
        return;
    }
    memset(c->password, 0, sizeof c->password);
    snprintf(c->why, sizeof c->why, "permission denied (%s)", c->methods[0] ? c->methods : "no methods");
    c->state = SSHC_AUTH;
}

int sshc_login(struct sshc *c, const char *user, const uint8_t sk[64], const uint8_t pk[32], const char *password)
{
    if (c->state != SSHC_AUTH)
        return -1;
    snprintf(c->user, sizeof c->user, "%s", user);
    snprintf(c->password, sizeof c->password, "%s", password ? password : "");
    c->have_key = sk && pk;
    if (c->have_key) {
        memcpy(c->sk, sk, 64);
        memcpy(c->pk, pk, 32);
    }
    c->tried_none = c->tried_key = c->tried_password = c->tried_kbd = 0;
    c->methods[0] = c->why[0] = 0;
    c->state = SSHC_LOGGING_IN;
    auth_next(c);
    return 0;
}

static int userauth_reply(struct sshc *c, const uint8_t *p, uint32_t n)
{
    struct sread r;
    const uint8_t *s;
    uint32_t len;
    sr_init(&r, p + 1, n - 1);
    switch (p[0]) {
    case MSG_USERAUTH_SUCCESS:
        c->state = SSHC_READY;
        memset(c->password, 0, sizeof c->password);
        memset(c->sk, 0, sizeof c->sk);
        say(c, "login ok");
        return 0;
    case MSG_USERAUTH_FAILURE:
        s = sr_string(&r, &len);
        if (!r.err) {
            if (len >= sizeof c->methods)
                len = sizeof c->methods - 1;
            memcpy(c->methods, s, len);
            c->methods[len] = 0;
        }
        if (c->state == SSHC_LOGGING_IN)
            auth_next(c);
        return 0;
    case MSG_USERAUTH_BANNER:
        s = sr_string(&r, &len);
        if (!r.err && c->ops->banner) {
            char *t = malloc(len + 1);
            if (t) {
                memcpy(t, s, len);
                t[len] = 0;
                c->ops->banner(c->io, t);
                free(t);
            }
        }
        return 0;
    case MSG_USERAUTH_INFO_REQUEST: {           /* keyboard-interactive: every prompt that hides the answer gets the
                                                   password, the rest an empty line */
        uint8_t m[400];
        struct sbuf b;
        uint32_t i, prompts;
        sr_string(&r, &len);
        sr_string(&r, &len);
        sr_string(&r, &len);
        prompts = sr_u32(&r);
        if (r.err || prompts > 16)
            return die(c, DISC_PROTOCOL_ERROR, "a bad keyboard-interactive request");
        sb_init(&b, m, sizeof m);
        sb_u8(&b, MSG_USERAUTH_INFO_RESPONSE);
        sb_u32(&b, prompts);
        for (i = 0; i < prompts; i++) {
            sr_string(&r, &len);
            sb_cstr(&b, sr_u8(&r) ? "" : c->password);
        }
        send_sb(c, &b);
        memset(m, 0, sizeof m);
        return 0;
    }
    }
    return 0;
}

/* ---- channels */

static struct sshc_chan *chan_of(struct sshc *c, struct sread *r)
{
    uint32_t id = sr_u32(r);
    return !r->err && id < NCHAN && c->chans[id] ? c->chans[id] : NULL;
}

static void chan_msg(struct sshc_chan *ch, uint8_t type)
{
    uint8_t m[8];
    struct sbuf b;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, type);
    sb_u32(&b, ch->peer);
    send_sb(ch->conn, &b);
}

static void chan_free(struct sshc_chan *ch)
{
    struct sshc *c = ch->conn;
    c->chans[ch->id] = NULL;
    if (c->ops->chan_closed)
        c->ops->chan_closed(ch);
    free(ch->out);
    free(ch);
}

static void flush(struct sshc_chan *ch)
{
    struct sshc *c = ch->conn;
    uint8_t *m = c->twork + TX_PAYLOAD;
    struct sbuf b;
    if (c->kex || c->dead || ch->close_sent || !ch->confirmed)
        return;
    while (ch->out_len && ch->peer_window) {
        uint32_t k = ch->out_len;
        if (k > ch->peer_window) k = ch->peer_window;
        if (k > ch->peer_maxpkt) k = ch->peer_maxpkt;
        if (k > MAXPKT) k = MAXPKT;
        sb_init(&b, m, MAXPKT + 32);
        sb_u8(&b, MSG_CHANNEL_DATA);
        sb_u32(&b, ch->peer);
        sb_string(&b, ch->out, k);
        send_sb(c, &b);
        memmove(ch->out, ch->out + k, ch->out_len - k);
        ch->out_len -= k;
        ch->peer_window -= k;
    }
    if (ch->eof_pending && !ch->out_len && !ch->eof_sent) {
        chan_msg(ch, MSG_CHANNEL_EOF);
        ch->eof_sent = 1;
    }
}

static void chan_request(struct sshc_chan *ch, const char *type, int reply, const struct sbuf *extra)
{
    uint8_t m[700];
    struct sbuf b;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_CHANNEL_REQUEST);
    sb_u32(&b, ch->peer);
    sb_cstr(&b, type);
    sb_u8(&b, (uint8_t)reply);
    if (extra)
        sb_bytes(&b, extra->p, extra->len);
    send_sb(ch->conn, &b);
    if (reply)
        ch->replies++;
}

/* The open confirmed: the pty, then the shell, command or subsystem. */
static void chan_start(struct sshc_chan *ch)
{
    uint8_t m[600];
    struct sbuf b;
    if (ch->term[0]) {
        sb_init(&b, m, sizeof m);
        sb_cstr(&b, ch->term);
        sb_u32(&b, ch->cols);
        sb_u32(&b, ch->rows);
        sb_u32(&b, 0);
        sb_u32(&b, 0);
        sb_string(&b, "", 1);                   /* the modes: TTY_OP_END alone */
        chan_request(ch, "pty-req", 1, &b);
    }
    sb_init(&b, m, sizeof m);
    if (ch->subsystem[0]) {
        sb_cstr(&b, ch->subsystem);
        chan_request(ch, "subsystem", 1, &b);
    } else if (ch->cmd[0]) {
        sb_cstr(&b, ch->cmd);
        chan_request(ch, "exec", 1, &b);
    } else {
        chan_request(ch, "shell", 1, 0);
    }
    flush(ch);
}

struct sshc_chan *sshc_session(struct sshc *c, const char *term, unsigned cols, unsigned rows, const char *cmd,
                               const char *subsystem)
{
    struct sshc_chan *ch;
    uint8_t m[64];
    struct sbuf b;
    uint32_t i;
    if (c->state != SSHC_READY)
        return NULL;
    for (i = 0; i < NCHAN && c->chans[i]; i++) ;
    if (i == NCHAN || !(ch = calloc(1, sizeof *ch)))
        return NULL;
    ch->conn = c;
    ch->id = i;
    ch->our_window = WINDOW;
    snprintf(ch->term, sizeof ch->term, "%s", term ? term : "");
    snprintf(ch->cmd, sizeof ch->cmd, "%s", cmd ? cmd : "");
    snprintf(ch->subsystem, sizeof ch->subsystem, "%s", subsystem ? subsystem : "");
    ch->cols = cols;
    ch->rows = rows;
    c->chans[i] = ch;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_CHANNEL_OPEN);
    sb_cstr(&b, "session");
    sb_u32(&b, i);
    sb_u32(&b, WINDOW);
    sb_u32(&b, MAXPKT);
    send_sb(c, &b);
    return ch;
}

int sshc_chan_ok(const struct sshc_chan *ch)
{
    if (ch->refused)
        return -1;
    return ch->confirmed && !ch->replies ? 1 : 0;
}

uint32_t sshc_write(struct sshc_chan *ch, const uint8_t *data, uint32_t len)
{
    if (ch->close_sent || ch->eof_pending)
        return ch->out_len;
    if (ch->out_len + len > ch->out_cap) {
        uint32_t cap = ch->out_cap ? ch->out_cap : 4096;
        uint8_t *nb;
        while (cap < ch->out_len + len && cap < OUT_MAX)
            cap *= 2;
        if ((nb = realloc(ch->out, cap)) != NULL) {
            ch->out = nb;
            ch->out_cap = cap;
        }
    }
    if (len > ch->out_cap - ch->out_len)
        len = ch->out_cap - ch->out_len;
    memcpy(ch->out + ch->out_len, data, len);
    ch->out_len += len;
    flush(ch);
    return ch->out_len;
}

void sshc_eof(struct sshc_chan *ch)
{
    ch->eof_pending = 1;
    flush(ch);
}

void sshc_close(struct sshc_chan *ch)
{
    if (ch->close_sent)
        return;
    ch->close_sent = 1;                         /* (before its open is confirmed: the CLOSE goes with the answer) */
    if (ch->confirmed && !ch->conn->dead)
        chan_msg(ch, MSG_CHANNEL_CLOSE);
    if (ch->close_recv || ch->conn->dead)
        chan_free(ch);
}

void sshc_resize(struct sshc_chan *ch, unsigned cols, unsigned rows)
{
    uint8_t m[32];
    struct sbuf b;
    ch->cols = cols;
    ch->rows = rows;
    if (!ch->confirmed || ch->close_sent)
        return;
    sb_init(&b, m, sizeof m);
    sb_u32(&b, cols);
    sb_u32(&b, rows);
    sb_u32(&b, 0);
    sb_u32(&b, 0);
    chan_request(ch, "window-change", 0, &b);
}

void sshc_signal(struct sshc_chan *ch, const char *name)
{
    uint8_t m[32];
    struct sbuf b;
    if (!ch->confirmed || ch->close_sent)
        return;
    sb_init(&b, m, sizeof m);
    sb_cstr(&b, name);
    chan_request(ch, "signal", 0, &b);
}

static void window_adjust(struct sshc_chan *ch)
{
    uint8_t m[16];
    struct sbuf b;
    uint32_t want = WINDOW - (ch->held ? ch->unconsumed : 0);
    if (ch->close_sent || ch->conn->kex || ch->our_window >= want / 2 || want <= ch->our_window)
        return;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_CHANNEL_WINDOW_ADJUST);
    sb_u32(&b, ch->peer);
    sb_u32(&b, want - ch->our_window);
    send_sb(ch->conn, &b);
    ch->our_window = want;
}

void sshc_hold(struct sshc_chan *ch) { ch->held = 1; }

void sshc_consumed(struct sshc_chan *ch, uint32_t n)
{
    ch->unconsumed = n > ch->unconsumed ? 0 : ch->unconsumed - n;
    window_adjust(ch);
}

uint32_t sshc_pending(const struct sshc_chan *ch) { return ch->out_len; }
void *sshc_chan_user(const struct sshc_chan *ch) { return ch->user; }
void sshc_chan_set_user(struct sshc_chan *ch, void *user) { ch->user = user; }
struct sshc *sshc_chan_conn(const struct sshc_chan *ch) { return ch->conn; }
void *sshc_io(const struct sshc *c) { return c->io; }

static int channel(struct sshc *c, const uint8_t *p, uint32_t n)
{
    struct sread r;
    struct sshc_chan *ch;
    const uint8_t *s;
    uint32_t len;
    sr_init(&r, p + 1, n - 1);
    if (!(ch = chan_of(c, &r)))
        return 0;                               /* (a channel already gone) */
    switch (p[0]) {
    case MSG_CHANNEL_OPEN_CONFIRMATION:
        ch->peer = sr_u32(&r);
        ch->peer_window = sr_u32(&r);
        ch->peer_maxpkt = sr_u32(&r);
        if (r.err)
            return die(c, DISC_PROTOCOL_ERROR, "a bad channel confirmation");
        ch->confirmed = 1;
        if (ch->close_sent) {                   /* (closed before it was open) */
            chan_msg(ch, MSG_CHANNEL_CLOSE);
            return 0;
        }
        chan_start(ch);
        return 0;
    case MSG_CHANNEL_OPEN_FAILURE:             /* (chan_closed tells its user) */
        ch->refused = 1;
        chan_free(ch);
        return 0;
    case MSG_CHANNEL_WINDOW_ADJUST:
        ch->peer_window += sr_u32(&r);
        flush(ch);
        return 0;
    case MSG_CHANNEL_DATA:
    case MSG_CHANNEL_EXTENDED_DATA: {
        int stream = 0;
        if (p[0] == MSG_CHANNEL_EXTENDED_DATA)
            stream = sr_u32(&r) == 1 ? 1 : 0;
        s = sr_string(&r, &len);
        if (r.err || len > ch->our_window)
            return die(c, DISC_PROTOCOL_ERROR, "data past the window");
        ch->our_window -= len;
        if (ch->held)
            ch->unconsumed += len;
        if (c->ops->chan_data && !ch->close_sent)
            c->ops->chan_data(ch, stream, s, len);
        if (c->chans[ch->id] == ch)             /* (still there) */
            window_adjust(ch);
        return 0;
    }
    case MSG_CHANNEL_EOF:
        ch->eof_recv = 1;
        if (c->ops->chan_eof)
            c->ops->chan_eof(ch);
        return 0;
    case MSG_CHANNEL_CLOSE:
        ch->close_recv = 1;
        if (!ch->close_sent) {
            chan_msg(ch, MSG_CHANNEL_CLOSE);
            ch->close_sent = 1;
        }
        chan_free(ch);
        return 0;
    case MSG_CHANNEL_SUCCESS:
    case MSG_CHANNEL_FAILURE:
        if (ch->replies > 0)
            ch->replies--;
        if (p[0] == MSG_CHANNEL_FAILURE)
            ch->refused = 1;
        return 0;
    case MSG_CHANNEL_REQUEST: {
        const uint8_t *type = sr_string(&r, &len);
        uint32_t tlen = len;
        int reply = sr_u8(&r);
        if (r.err)
            return die(c, DISC_PROTOCOL_ERROR, "a bad channel request");
        if (sr_streq(type, tlen, "exit-status")) {
            uint32_t st = sr_u32(&r);
            if (c->ops->chan_exit)
                c->ops->chan_exit(ch, (int)st, NULL);
        } else if (sr_streq(type, tlen, "exit-signal")) {
            char sig[32];
            s = sr_string(&r, &len);
            if (len >= sizeof sig)
                len = sizeof sig - 1;
            memcpy(sig, s ? (const char *)s : "", s ? len : 0);
            sig[s ? len : 0] = 0;
            if (c->ops->chan_exit)
                c->ops->chan_exit(ch, -1, sig);
        } else if (reply) {
            chan_msg(ch, MSG_CHANNEL_FAILURE);  /* (keepalive@openssh.com and the like) */
        }
        return 0;
    }
    }
    return 0;
}

/* ---- dispatch */

static int handle(struct sshc *c, const uint8_t *p, uint32_t n)
{
    uint8_t m[16];
    struct sbuf b;
    if (!n)
        return die(c, DISC_PROTOCOL_ERROR, "an empty packet");
    if (c->ignore_next) {
        c->ignore_next = 0;
        return 0;
    }
    if (c->strict && !c->first_kex_done && p[0] != MSG_KEXINIT && p[0] != MSG_KEX_ECDH_REPLY && p[0] != MSG_NEWKEYS
        && p[0] != MSG_DISCONNECT)
        return die(c, DISC_PROTOCOL_ERROR, "strict kex: an unexpected packet");
    switch (p[0]) {
    case MSG_DISCONNECT: {
        struct sread r;
        const uint8_t *s;
        uint32_t len;
        sr_init(&r, p + 1, n - 1);
        sr_u32(&r);
        s = sr_string(&r, &len);
        if (!c->why[0])
            snprintf(c->why, sizeof c->why, "the server closed: %.*s", r.err ? 0 : (int)len, s ? (const char *)s : "");
        c->dead = 1;
        c->state = SSHC_DEAD;
        return -1;
    }
    case MSG_IGNORE:
    case MSG_DEBUG:
    case MSG_UNIMPLEMENTED:
    case MSG_EXT_INFO:
        return 0;
    case MSG_KEXINIT:
        return kexinit(c, p, n);
    case MSG_KEX_ECDH_REPLY:
        return ecdh_reply(c, p, n);
    case MSG_NEWKEYS:
        return newkeys(c);
    case MSG_SERVICE_ACCEPT:
        if (c->state == SSHC_KEX)
            c->state = SSHC_AUTH;
        return 0;
    case MSG_USERAUTH_SUCCESS:
    case MSG_USERAUTH_FAILURE:
    case MSG_USERAUTH_BANNER:
    case MSG_USERAUTH_INFO_REQUEST:
        return userauth_reply(c, p, n);
    case MSG_GLOBAL_REQUEST: {                  /* (hostkeys-00@openssh.com ...: none taken) */
        struct sread r;
        uint32_t len;
        sr_init(&r, p + 1, n - 1);
        sr_string(&r, &len);
        if (sr_u8(&r) && !r.err) {
            m[0] = MSG_REQUEST_FAILURE;
            send_packet(c, m, 1);
        }
        return 0;
    }
    case MSG_REQUEST_SUCCESS:
    case MSG_REQUEST_FAILURE:
        return 0;
    }
    if (p[0] >= MSG_CHANNEL_OPEN_CONFIRMATION && p[0] <= MSG_CHANNEL_FAILURE)
        return channel(c, p, n);
    sb_init(&b, m, sizeof m);                   /* anything else: UNIMPLEMENTED */
    sb_u8(&b, MSG_UNIMPLEMENTED);
    sb_u32(&b, c->rx_seq - 1);
    send_sb(c, &b);
    return 0;
}

static int version_line(struct sshc *c)
{
    uint32_t i, end;
    for (i = 0; i < c->rx_len && c->rx[i] != '\n'; i++) ;
    if (i == c->rx_len)
        return c->rx_len > 255 ? die(c, DISC_PROTOCOL_ERROR, "no version line") : 0;
    end = i && c->rx[i - 1] == '\r' ? i - 1 : i;
    if (end >= 4 && !memcmp(c->rx, "SSH-", 4)) {
        if (!(end >= 8 && !memcmp(c->rx, "SSH-2.0-", 8)) && !(end >= 9 && !memcmp(c->rx, "SSH-1.99-", 9)))
            return die(c, DISC_PROTOCOL_ERROR, "not SSH 2");
        memcpy(c->vs, c->rx, end);
        c->vs_len = end;
        c->got_version = 1;
    }
    memmove(c->rx, c->rx + i + 1, c->rx_len - i - 1);
    c->rx_len -= i + 1;
    return 1;
}

static int packet(struct sshc *c)
{
    uint8_t nonce[8], ks[4], tag[16], *pay;
    uint32_t plen, total, padlen, len;
    int rc;
    if (!c->rx_enc) {
        if (c->rx_len < 5)
            return 0;
        plen = be32(c->rx);
        if (plen < 5 || plen > MAX_PACKET)
            return die(c, DISC_PROTOCOL_ERROR, "a bad packet length");
        total = 4 + plen;
        if (c->rx_len < total)
            return 0;
        padlen = c->rx[4];
        pay = c->rx + 5;
    } else {
        if (c->rx_len < 4)
            return 0;
        nonce_of(nonce, c->rx_seq);
        if (!c->rx_plen) {
            crypto_stream_chacha20(ks, 4, nonce, c->rx_key + 32);
            plen = (uint32_t)(c->rx[0] ^ ks[0]) << 24 | (uint32_t)(c->rx[1] ^ ks[1]) << 16
                   | (uint32_t)(c->rx[2] ^ ks[2]) << 8 | (uint32_t)(c->rx[3] ^ ks[3]);
            if (plen < 8 || plen > MAX_PACKET || plen % 8)
                return die(c, DISC_PROTOCOL_ERROR, "a bad packet length");
            c->rx_plen = plen;
        }
        plen = c->rx_plen;
        total = 4 + plen + 16;
        if (c->rx_len < total)
            return 0;
        memset(c->rwork, 0, 64);
        memcpy(c->rwork + 64, c->rx + 4, plen);
        crypto_stream_chacha20_xor(c->rwork, c->rwork, 64 + plen, nonce, c->rx_key);
        crypto_onetimeauth_poly1305(tag, c->rx, 4 + plen, c->rwork);
        if (crypto_verify_16(tag, c->rx + 4 + plen) != 0)
            return die(c, DISC_PROTOCOL_ERROR, "a bad MAC");
        c->rx_plen = 0;
        padlen = c->rwork[64];
        pay = c->rwork + 65;
    }
    if (padlen < 4 || padlen + 1 >= plen)
        return die(c, DISC_PROTOCOL_ERROR, "bad padding");
    len = plen - padlen - 1;
    c->rx_seq++;
    if (!c->rx_enc) {
        memcpy(c->rwork + 65, pay, len);
        pay = c->rwork + 65;
    }
    memmove(c->rx, c->rx + total, c->rx_len - total);
    c->rx_len -= total;
    rc = handle(c, pay, len);
    return rc < 0 ? -1 : 1;
}

int sshc_input(struct sshc *c, const uint8_t *data, uint32_t len)
{
    int rc;
    if (c->dead)
        return -1;
    while (len) {
        uint32_t k = RX_CAP - c->rx_len;
        if (!k)
            return die(c, DISC_PROTOCOL_ERROR, "input overflow");
        if (k > len)
            k = len;
        memcpy(c->rx + c->rx_len, data, k);
        c->rx_len += k;
        data += k;
        len -= k;
        for (;;) {
            rc = c->got_version ? packet(c) : version_line(c);
            if (rc < 0 || c->dead) {
                c->dead = 1;
                c->state = SSHC_DEAD;
                return -1;
            }
            if (rc == 0)
                break;
        }
    }
    return 0;
}

struct sshc *sshc_new(const struct sshc_ops *ops, void *io, const char *version)
{
    struct sshc *c = calloc(1, sizeof *c);
    char line[80];
    size_t n;
    if (!c)
        return NULL;
    c->ops = ops;
    c->io = io;
    snprintf(c->version, sizeof c->version, "%s", version ? version : VERSION);
    c->rx = malloc(RX_CAP);
    c->rwork = malloc(WORK_CAP);
    c->twork = malloc(WORK_CAP);
    if (!c->rx || !c->rwork || !c->twork) {
        sshc_free(c);
        return NULL;
    }
    c->state = SSHC_KEX;
    n = (size_t)snprintf(line, sizeof line, "%s\r\n", c->version);
    ops->send(io, (const uint8_t *)line, (uint32_t)n);
    send_kexinit(c);
    return c;
}

int sshc_state(const struct sshc *c) { return c->state; }
const char *sshc_why(const struct sshc *c) { return c->why; }
const char *sshc_methods(const struct sshc *c) { return c->methods; }

void sshc_free(struct sshc *c)
{
    int i;
    if (!c)
        return;
    for (i = 0; i < NCHAN; i++)
        if (c->chans[i])
            chan_free(c->chans[i]);
    if (c->rx) memset(c->rx, 0, RX_CAP);
    if (c->rwork) memset(c->rwork, 0, WORK_CAP);
    if (c->twork) memset(c->twork, 0, WORK_CAP);
    memset(c->rx_key, 0, sizeof c->rx_key);
    memset(c->tx_key, 0, sizeof c->tx_key);
    memset(c->next_rx_key, 0, sizeof c->next_rx_key);
    memset(c->sk, 0, sizeof c->sk);
    memset(c->password, 0, sizeof c->password);
    free(c->rx);
    free(c->rwork);
    free(c->twork);
    free(c->ic);
    free(c->is);
    free(c);
}
