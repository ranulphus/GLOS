/* GLOS's SSH server: the transport (RFC 4253), user authentication
 * (RFC 4252) and connection (RFC 4254) layers. See ssh.h.
 *
 * Packets: before the first NEWKEYS, plain RFC 4253 packets; after it,
 * chacha20-poly1305@openssh.com (OpenSSH's PROTOCOL.chacha20poly1305):
 * K_1 (bytes 32-63 of the key) encrypts the 4-byte length, K_2 (bytes 0-31)
 * the rest from block 1, block 0 of K_2 is the Poly1305 key, and the tag
 * covers the encrypted length and payload. As in TinySSH, one ChaCha20 pass
 * over 64 zero bytes followed by the packet gives the Poly1305 key and the
 * payload at once. */
#include <string.h>

#include "crypto_hash_sha256.h"
#include "crypto_onetimeauth_poly1305.h"
#include "crypto_scalarmult_curve25519.h"
#include "crypto_sign_ed25519.h"
#include "crypto_stream_chacha20.h"
#include "crypto_verify_16.h"
#include "randombytes.h"
#include "ssh.h"
#include "sshbuf.h"

enum {
    MSG_DISCONNECT = 1, MSG_IGNORE = 2, MSG_UNIMPLEMENTED = 3, MSG_DEBUG = 4, MSG_SERVICE_REQUEST = 5,
    MSG_SERVICE_ACCEPT = 6,
    MSG_KEXINIT = 20, MSG_NEWKEYS = 21, MSG_KEX_ECDH_INIT = 30, MSG_KEX_ECDH_REPLY = 31,
    MSG_USERAUTH_REQUEST = 50, MSG_USERAUTH_FAILURE = 51, MSG_USERAUTH_SUCCESS = 52, MSG_USERAUTH_PK_OK = 60,
    MSG_GLOBAL_REQUEST = 80, MSG_REQUEST_FAILURE = 82,
    MSG_CHANNEL_OPEN = 90, MSG_CHANNEL_OPEN_CONFIRMATION = 91, MSG_CHANNEL_OPEN_FAILURE = 92,
    MSG_CHANNEL_WINDOW_ADJUST = 93, MSG_CHANNEL_DATA = 94, MSG_CHANNEL_EXTENDED_DATA = 95, MSG_CHANNEL_EOF = 96,
    MSG_CHANNEL_CLOSE = 97, MSG_CHANNEL_REQUEST = 98, MSG_CHANNEL_SUCCESS = 99, MSG_CHANNEL_FAILURE = 100,
};

enum {
    DISC_PROTOCOL_ERROR = 2, DISC_KEX_FAILED = 3, DISC_MAC_ERROR = 5, DISC_SERVICE_NOT_AVAILABLE = 7,
    DISC_NO_MORE_AUTH = 14,
};

#define RX_CAP    (4 + SSH_MAX_PACKET + 16)
#define WORK_CAP  (64 + SSH_MAX_PACKET + 16)
#define WINDOW    0x40000u                      /* what a client may send ahead on a channel */
#define MAXPKT    32768u                        /* the largest data packet we take, or send */
#define OUT_MAX   0x40000u                      /* output buffered per stream */
#define USER      "glos"

static const char *const our_kex[] = { "curve25519-sha256", "curve25519-sha256@libssh.org", NULL };
static const char *const our_hostkey[] = { "ssh-ed25519", NULL };
static const char *const our_cipher[] = { "chacha20-poly1305@openssh.com", NULL };
static const char *const our_comp[] = { "none", NULL };

struct ssh_chan {
    struct ssh_conn *conn;
    uint32_t id, peer, peer_window, peer_maxpkt, our_window;
    uint8_t started, eof_recv, close_sent, close_recv, exit_pending;
    uint8_t held;                               /* the window reopens only as the platform consumes */
    uint32_t unconsumed;                        /* held: input delivered and not yet consumed */
    uint32_t exit_status;
    uint8_t *out[2];
    uint32_t out_len[2], out_cap[2];
    void *user;
};

struct ssh_conn {
    const struct ssh_server *srv;
    const struct ssh_ops *ops;
    void *io;
    int got_version, authed, service_ok, auth_fails, dead;
    uint8_t *rx, *rwork, *twork;
    uint32_t rx_len, rx_plen;                   /* rx_plen: the decrypted length of the packet arriving */
    int rx_enc, tx_enc;
    uint8_t rx_key[64], tx_key[64], next_rx_key[64];   /* K_2 then K_1 */
    uint32_t rx_seq, tx_seq;
    int kex;                                    /* 0 none, 1 our KEXINIT sent, 2 both, 3 our NEWKEYS sent */
    int first_kex_done, strict, ignore_next;
    uint8_t *ic, *is;
    uint32_t ic_len, is_len;
    char vc[256];
    uint32_t vc_len;
    uint8_t session_id[32];
    struct ssh_chan *chans[SSH_MAX_CHANNELS];
};

static int handle(struct ssh_conn *c, const uint8_t *p, uint32_t n);
static void flush(struct ssh_chan *ch);
static void window_adjust(struct ssh_chan *ch);

/* ---- packets out */

static void nonce_of(uint8_t n[8], uint32_t seq)
{
    memset(n, 0, 4);
    put_be32(n + 4, seq);
}

/* Every packet is put together in twork: [60 bytes][length][padlen][payload]
   [padding][tag], so the payload is always at TX_PAYLOAD and a caller may
   build it there directly (flush). */
#define TX_PAYLOAD 65

static void send_packet(struct ssh_conn *c, const uint8_t *payload, uint32_t len)
{
    uint8_t *w = c->twork, nonce[8], len4[4], ks[64];
    uint32_t pad, plen;

    if (c->dead || len > SSH_MAX_PACKET - 64)
        return;
    if (payload != w + TX_PAYLOAD)
        memmove(w + TX_PAYLOAD, payload, len);
    if (!c->tx_enc) {                           /* (4 + 1 + payload + pad) % 8 == 0 */
        pad = 8 - ((5 + len) % 8);
        if (pad < 4)
            pad += 8;
        plen = 1 + len + pad;
        put_be32(w + 60, plen);
        w[64] = (uint8_t)pad;
        randombytes(w + TX_PAYLOAD + len, pad);
        c->ops->send(c->io, w + 60, 4 + plen);
    } else {                                    /* (1 + payload + pad) % 8 == 0 */
        pad = 8 - ((1 + len) % 8);
        if (pad < 4)
            pad += 8;
        plen = 1 + len + pad;
        nonce_of(nonce, c->tx_seq);
        memset(w, 0, 64);
        w[64] = (uint8_t)pad;
        randombytes(w + TX_PAYLOAD + len, pad);
        crypto_stream_chacha20_xor(w, w, 64 + plen, nonce, c->tx_key);      /* w[0..31]: the Poly1305 key */
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

static void send_sb(struct ssh_conn *c, struct sbuf *b)
{
    if (!b->err)
        send_packet(c, b->p, b->len);
}

static void disconnect(struct ssh_conn *c, uint32_t reason, const char *why)
{
    uint8_t m[128];
    struct sbuf b;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_DISCONNECT);
    sb_u32(&b, reason);
    sb_cstr(&b, why);
    sb_cstr(&b, "");
    send_sb(c, &b);
    c->ops->log("disconnect reason=%u why=%s", reason, why);
    c->dead = 1;
}

/* ---- key exchange */

static void send_kexinit(struct ssh_conn *c)
{
    uint8_t m[512], cookie[16];
    struct sbuf b;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_KEXINIT);
    randombytes(cookie, 16);
    sb_bytes(&b, cookie, 16);
    sb_cstr(&b, "curve25519-sha256,curve25519-sha256@libssh.org,kex-strict-s-v00@openssh.com");
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
    c->ops->free(c->is);
    c->is = c->ops->alloc(b.len);
    if (!c->is)
        return;
    memcpy(c->is, m, b.len);
    c->is_len = b.len;
    send_packet(c, m, b.len);
    c->kex = 1;
}

static int kexinit(struct ssh_conn *c, const uint8_t *p, uint32_t n)
{
    struct sread r;
    const uint8_t *l[10];
    uint32_t ln[10];
    int i, follows;

    if (c->kex >= 2)
        return disconnect(c, DISC_PROTOCOL_ERROR, "kexinit again"), -1;
    sr_init(&r, p + 1, n - 1);
    sr_bytes(&r, 16);
    for (i = 0; i < 10; i++)
        l[i] = sr_string(&r, &ln[i]);
    follows = sr_u8(&r);
    sr_u32(&r);
    if (r.err)
        return disconnect(c, DISC_PROTOCOL_ERROR, "bad kexinit"), -1;
    if (!c->first_kex_done) {
        c->strict = namelist_has(l[0], ln[0], "kex-strict-c-v00@openssh.com");
        if (c->strict && c->rx_seq != 1)        /* strict: KEXINIT must be the client's first packet */
            return disconnect(c, DISC_PROTOCOL_ERROR, "strict kex: kexinit not first"), -1;
    }
    if (!namelist_pick(l[0], ln[0], our_kex) || !namelist_pick(l[1], ln[1], our_hostkey)
        || !namelist_pick(l[2], ln[2], our_cipher) || !namelist_pick(l[3], ln[3], our_cipher)
        || !namelist_pick(l[6], ln[6], our_comp) || !namelist_pick(l[7], ln[7], our_comp))
        return disconnect(c, DISC_KEX_FAILED, "no common algorithms"), -1;
    if (follows) {                              /* a guessed first packet: wrong unless its first choices are ours */
        uint32_t k = 0;
        while (k < ln[0] && l[0][k] != ',') k++;
        c->ignore_next = !((k == 17 && !memcmp(l[0], "curve25519-sha256", 17))
                           || (k == 28 && !memcmp(l[0], "curve25519-sha256@libssh.org", 28)));
    }
    c->ops->free(c->ic);
    c->ic = c->ops->alloc(n);
    if (!c->ic)
        return -1;
    memcpy(c->ic, p, n);
    c->ic_len = n;
    if (c->kex == 0)                            /* the client started a re-key */
        send_kexinit(c);
    c->kex = 2;
    return 0;
}

/* HASH(K || H || letter || session_id), extended to 64 bytes (RFC 4253 7.2). */
static void derive(uint8_t out[64], const uint8_t *kmp, uint32_t kmp_len, const uint8_t h[32], char letter,
                   const uint8_t sid[32])
{
    uint8_t buf[48 + 32 + 1 + 32];
    memcpy(buf, kmp, kmp_len);
    memcpy(buf + kmp_len, h, 32);
    buf[kmp_len + 32] = (uint8_t)letter;
    memcpy(buf + kmp_len + 33, sid, 32);
    crypto_hash_sha256(out, buf, kmp_len + 65);
    memcpy(buf + kmp_len + 32, out, 32);        /* K || H || K1 */
    crypto_hash_sha256(out + 32, buf, kmp_len + 64);
    memset(buf, 0, sizeof buf);
}

static int ecdh_init(struct ssh_conn *c, const uint8_t *p, uint32_t n)
{
    struct sread r;
    struct sbuf b, hb;
    const uint8_t *qc;
    uint32_t qlen, kmp_len;
    uint8_t e[32], qs[32], k[32], h[32], kmp[40], blob[51], sm[96], *hbuf, m[256], key[64];
    unsigned long long smlen;
    int i, zero = 0;

    if (c->kex != 2)
        return disconnect(c, DISC_PROTOCOL_ERROR, "unexpected ecdh"), -1;
    sr_init(&r, p + 1, n - 1);
    qc = sr_string(&r, &qlen);
    if (r.err || qlen != 32)
        return disconnect(c, DISC_KEX_FAILED, "bad client key"), -1;
    randombytes(e, 32);
    crypto_scalarmult_curve25519_base(qs, e);
    crypto_scalarmult_curve25519(k, e, qc);
    for (i = 0; i < 32; i++)
        zero |= k[i];
    if (!zero)
        return disconnect(c, DISC_KEX_FAILED, "bad shared secret"), -1;

    sb_init(&b, blob, sizeof blob);             /* K_S: the host key */
    sb_cstr(&b, "ssh-ed25519");
    sb_string(&b, c->srv->host_pk, 32);
    sb_init(&hb, kmp, sizeof kmp);              /* K, as mpint (RFC 8731 3.1) */
    sb_mpint(&hb, k, 32);
    kmp_len = hb.len;

    hbuf = c->ops->alloc(1024 + c->ic_len + c->is_len);
    if (!hbuf)
        return -1;
    sb_init(&hb, hbuf, 1024 + c->ic_len + c->is_len);
    sb_string(&hb, c->vc, c->vc_len);
    sb_cstr(&hb, c->srv->version);
    sb_string(&hb, c->ic, c->ic_len);
    sb_string(&hb, c->is, c->is_len);
    sb_string(&hb, blob, b.len);
    sb_string(&hb, qc, 32);
    sb_string(&hb, qs, 32);
    sb_bytes(&hb, kmp, kmp_len);
    crypto_hash_sha256(h, hbuf, hb.len);
    c->ops->free(hbuf);
    if (hb.err)
        return -1;
    if (!c->first_kex_done)
        memcpy(c->session_id, h, 32);

    crypto_sign_ed25519(sm, &smlen, h, 32, c->srv->host_sk);
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_KEX_ECDH_REPLY);
    sb_u32(&b, 51);                             /* K_S */
    sb_cstr(&b, "ssh-ed25519");
    sb_string(&b, c->srv->host_pk, 32);
    sb_string(&b, qs, 32);
    sb_u32(&b, 4 + 11 + 4 + 64);                /* the signature */
    sb_cstr(&b, "ssh-ed25519");
    sb_string(&b, sm, 64);
    send_sb(c, &b);
    m[0] = MSG_NEWKEYS;
    send_packet(c, m, 1);

    derive(key, kmp, kmp_len, h, 'D', c->session_id);  /* server to client */
    memcpy(c->tx_key, key, 64);
    c->tx_enc = 1;
    if (c->strict)
        c->tx_seq = 0;
    derive(c->next_rx_key, kmp, kmp_len, h, 'C', c->session_id);
    c->kex = 3;
    memset(e, 0, sizeof e);
    memset(k, 0, sizeof k);
    memset(kmp, 0, sizeof kmp);
    memset(key, 0, sizeof key);
    return 0;
}

static int newkeys(struct ssh_conn *c)
{
    int i;
    if (c->kex != 3)
        return disconnect(c, DISC_PROTOCOL_ERROR, "unexpected newkeys"), -1;
    memcpy(c->rx_key, c->next_rx_key, 64);
    memset(c->next_rx_key, 0, 64);
    c->rx_enc = 1;
    if (c->strict)
        c->rx_seq = 0;
    if (!c->first_kex_done)
        c->ops->log("kex done strict=%u", c->strict);
    c->first_kex_done = 1;
    c->kex = 0;
    c->ops->free(c->ic);
    c->ops->free(c->is);
    c->ic = c->is = NULL;
    for (i = 0; i < SSH_MAX_CHANNELS; i++)      /* output and window held during the key exchange */
        if (c->chans[i]) {
            flush(c->chans[i]);
            if (c->chans[i])
                window_adjust(c->chans[i]);
        }
    return 0;
}

/* ---- user authentication */

static void fingerprint(char out[48], const uint8_t pk[32])
{
    uint8_t blob[51], h[32];
    struct sbuf b;
    sb_init(&b, blob, sizeof blob);
    sb_cstr(&b, "ssh-ed25519");
    sb_string(&b, pk, 32);
    crypto_hash_sha256(h, blob, b.len);
    b64_encode(out, 48, h, 32);
    out[43] = 0;                                /* as ssh-keygen -l: no '=' */
}

static void auth_failure(struct ssh_conn *c)
{
    uint8_t m[32];
    struct sbuf b;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_USERAUTH_FAILURE);
    sb_cstr(&b, "publickey");
    sb_u8(&b, 0);
    send_sb(c, &b);
}

static int userauth(struct ssh_conn *c, const uint8_t *p, uint32_t n)
{
    struct sread r, kb;
    const uint8_t *user, *service, *method, *algo, *blob, *sigblob, *t, *pk = NULL;
    uint32_t ulen, slen, mlen, alen, blen, sglen, tlen;
    uint8_t has_sig, signed_data[512], m[128];
    unsigned long long outlen;
    struct sbuf b;
    char fp[48];
    int i;

    if (c->authed)
        return 0;
    sr_init(&r, p + 1, n - 1);
    user = sr_string(&r, &ulen);
    service = sr_string(&r, &slen);
    method = sr_string(&r, &mlen);
    if (r.err)
        return disconnect(c, DISC_PROTOCOL_ERROR, "bad userauth"), -1;
    if (++c->auth_fails > 20)
        return disconnect(c, DISC_NO_MORE_AUTH, "too many attempts"), -1;
    if (!sr_streq(user, ulen, USER) || !sr_streq(service, slen, "ssh-connection")
        || !sr_streq(method, mlen, "publickey")) {
        auth_failure(c);
        return 0;
    }
    has_sig = sr_u8(&r);
    algo = sr_string(&r, &alen);
    blob = sr_string(&r, &blen);
    sr_init(&kb, blob, blen);
    t = sr_string(&kb, &tlen);
    if (!r.err && sr_streq(algo, alen, "ssh-ed25519") && sr_streq(t, tlen, "ssh-ed25519")) {
        t = sr_string(&kb, &tlen);
        if (!kb.err && tlen == 32)
            for (i = 0; i < c->srv->n_authkeys; i++)
                if (!memcmp(c->srv->authkeys[i], t, 32))
                    pk = t;
    }
    if (!pk) {
        auth_failure(c);
        return 0;
    }
    if (!has_sig) {                             /* would this key do? */
        sb_init(&b, m, sizeof m);
        sb_u8(&b, MSG_USERAUTH_PK_OK);
        sb_string(&b, algo, alen);
        sb_string(&b, blob, blen);
        send_sb(c, &b);
        return 0;
    }
    sigblob = sr_string(&r, &sglen);
    sr_init(&kb, sigblob, sglen);
    t = sr_string(&kb, &tlen);
    if (r.err || !sr_streq(t, tlen, "ssh-ed25519") || !(t = sr_string(&kb, &tlen)) || tlen != 64) {
        auth_failure(c);
        return 0;
    }
    memcpy(signed_data, t, 64);                 /* signature, then what was signed */
    sb_init(&b, signed_data + 64, sizeof signed_data - 64);
    sb_string(&b, c->session_id, 32);
    sb_u8(&b, MSG_USERAUTH_REQUEST);
    sb_string(&b, user, ulen);
    sb_string(&b, service, slen);
    sb_cstr(&b, "publickey");
    sb_u8(&b, 1);
    sb_string(&b, algo, alen);
    sb_string(&b, blob, blen);
    fingerprint(fp, pk);
    if (b.err || crypto_sign_ed25519_open(signed_data, &outlen, signed_data, 64 + b.len, pk) != 0) {
        c->ops->log("auth fail key=SHA256:%s", fp);
        auth_failure(c);
        return 0;
    }
    c->authed = 1;
    c->ops->log("auth ok user=glos key=SHA256:%s", fp);
    m[0] = MSG_USERAUTH_SUCCESS;
    send_packet(c, m, 1);
    return 0;
}

/* ---- channels */

static struct ssh_chan *chan_of(struct ssh_conn *c, struct sread *r)
{
    uint32_t id = sr_u32(r);
    return !r->err && id < SSH_MAX_CHANNELS && c->chans[id] && !c->chans[id]->close_recv ? c->chans[id] : NULL;
}

static void chan_free(struct ssh_chan *ch)
{
    struct ssh_conn *c = ch->conn;
    c->ops->chan_gone(ch);
    c->chans[ch->id] = NULL;
    c->ops->free(ch->out[0]);
    c->ops->free(ch->out[1]);
    c->ops->free(ch);
}

static void chan_msg(struct ssh_chan *ch, uint8_t type)
{
    uint8_t m[8];
    struct sbuf b;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, type);
    sb_u32(&b, ch->peer);
    send_sb(ch->conn, &b);
}

static void flush(struct ssh_chan *ch)
{
    struct ssh_conn *c = ch->conn;
    uint8_t *m = c->twork + TX_PAYLOAD;         /* built where send_packet wants it */
    struct sbuf b;
    int s;

    if (c->kex || c->dead || ch->close_sent)
        return;
    for (s = 0; s < 2; s++)
        while (ch->out_len[s] && ch->peer_window) {
            uint32_t k = ch->out_len[s];
            if (k > ch->peer_window) k = ch->peer_window;
            if (k > ch->peer_maxpkt) k = ch->peer_maxpkt;
            if (k > MAXPKT) k = MAXPKT;
            sb_init(&b, m, MAXPKT + 32);
            sb_u8(&b, s ? MSG_CHANNEL_EXTENDED_DATA : MSG_CHANNEL_DATA);
            sb_u32(&b, ch->peer);
            if (s)
                sb_u32(&b, 1);                  /* SSH_EXTENDED_DATA_STDERR */
            sb_string(&b, ch->out[s], k);
            send_sb(c, &b);
            memmove(ch->out[s], ch->out[s] + k, ch->out_len[s] - k);
            ch->out_len[s] -= k;
            ch->peer_window -= k;
        }
    if (ch->exit_pending && !ch->out_len[0] && !ch->out_len[1]) {
        uint8_t x[64];
        sb_init(&b, x, sizeof x);
        sb_u8(&b, MSG_CHANNEL_REQUEST);
        sb_u32(&b, ch->peer);
        sb_cstr(&b, "exit-status");
        sb_u8(&b, 0);
        sb_u32(&b, ch->exit_status);
        send_sb(c, &b);
        chan_msg(ch, MSG_CHANNEL_EOF);
        chan_msg(ch, MSG_CHANNEL_CLOSE);
        ch->close_sent = 1;
        if (ch->close_recv)
            chan_free(ch);
    }
}

uint32_t ssh_chan_write(struct ssh_chan *ch, int stream, const uint8_t *data, uint32_t len)
{
    struct ssh_conn *c = ch->conn;
    uint32_t room;
    if (ch->close_sent || ch->exit_pending || stream < 0 || stream > 1)
        return 0;
    if (ch->out_len[stream] + len > ch->out_cap[stream] && ch->out_cap[stream] < OUT_MAX) {
        uint32_t cap = ch->out_cap[stream] ? ch->out_cap[stream] : 4096;
        uint8_t *nb;
        while (cap < ch->out_len[stream] + len && cap < OUT_MAX)
            cap *= 2;
        if (cap > OUT_MAX)
            cap = OUT_MAX;
        if ((nb = c->ops->alloc(cap)) != NULL) {
            memcpy(nb, ch->out[stream], ch->out_len[stream]);
            c->ops->free(ch->out[stream]);
            ch->out[stream] = nb;
            ch->out_cap[stream] = cap;
        }
    }
    room = ch->out_cap[stream] - ch->out_len[stream];
    if (len > room)
        len = room;
    memcpy(ch->out[stream] + ch->out_len[stream], data, len);
    ch->out_len[stream] += len;
    flush(ch);
    return len;
}

void ssh_chan_exit(struct ssh_chan *ch, uint32_t status)
{
    if (ch->exit_pending || ch->close_sent)
        return;
    ch->exit_pending = 1;
    ch->exit_status = status;
    flush(ch);
}

uint32_t ssh_chan_pending(const struct ssh_chan *ch) { return ch->out_len[0] + ch->out_len[1]; }
void *ssh_chan_user(const struct ssh_chan *ch) { return ch->user; }
void ssh_chan_set_user(struct ssh_chan *ch, void *user) { ch->user = user; }

static int chan_open(struct ssh_conn *c, const uint8_t *p, uint32_t n)
{
    struct sread r;
    struct sbuf b;
    const uint8_t *type;
    uint32_t tlen, sender, window, maxpkt, i;
    uint8_t m[64];
    struct ssh_chan *ch;

    sr_init(&r, p + 1, n - 1);
    type = sr_string(&r, &tlen);
    sender = sr_u32(&r);
    window = sr_u32(&r);
    maxpkt = sr_u32(&r);
    if (r.err)
        return disconnect(c, DISC_PROTOCOL_ERROR, "bad channel open"), -1;
    for (i = 0; i < SSH_MAX_CHANNELS && c->chans[i]; i++) ;
    sb_init(&b, m, sizeof m);
    if (!sr_streq(type, tlen, "session") || i == SSH_MAX_CHANNELS || !(ch = c->ops->alloc(sizeof *ch))) {
        sb_u8(&b, MSG_CHANNEL_OPEN_FAILURE);
        sb_u32(&b, sender);
        sb_u32(&b, sr_streq(type, tlen, "session") ? 4 : 3);   /* resource shortage / unknown type */
        sb_cstr(&b, sr_streq(type, tlen, "session") ? "too many channels" : "unknown channel type");
        sb_cstr(&b, "");
        send_sb(c, &b);
        return 0;
    }
    ch->conn = c;
    ch->id = i;
    ch->peer = sender;
    ch->peer_window = window;
    ch->peer_maxpkt = maxpkt ? maxpkt : 1;
    ch->our_window = WINDOW;
    c->chans[i] = ch;
    sb_u8(&b, MSG_CHANNEL_OPEN_CONFIRMATION);
    sb_u32(&b, sender);
    sb_u32(&b, i);
    sb_u32(&b, WINDOW);
    sb_u32(&b, MAXPKT);
    send_sb(c, &b);
    return 0;
}

static int chan_request(struct ssh_conn *c, const uint8_t *p, uint32_t n)
{
    struct sread r;
    struct ssh_chan *ch;
    const uint8_t *type, *arg;
    uint32_t tlen, alen;
    uint8_t want, m[8];
    int ok = -1;
    char cmd[1024];

    sr_init(&r, p + 1, n - 1);
    ch = chan_of(c, &r);
    type = sr_string(&r, &tlen);
    want = sr_u8(&r);
    if (r.err || !ch)
        return 0;
    if ((sr_streq(type, tlen, "exec") || sr_streq(type, tlen, "subsystem")) && !ch->started) {
        arg = sr_string(&r, &alen);
        if (!r.err && alen < sizeof cmd) {
            memcpy(cmd, arg, alen);
            cmd[alen] = 0;
            ch->started = 1;
            ok = sr_streq(type, tlen, "exec") ? c->ops->exec(ch, cmd) : c->ops->subsystem(ch, cmd);
            if (ok != 0)
                ch->started = 0;
        }
    }
    if (want) {
        m[0] = ok == 0 ? MSG_CHANNEL_SUCCESS : MSG_CHANNEL_FAILURE;
        put_be32(m + 1, ch->peer);
        send_packet(c, m, 5);
    }
    return 0;
}

/* Reopen the client's window once half of it is in use, counting input the
   platform still holds. */
static void window_adjust(struct ssh_chan *ch)
{
    uint32_t open = WINDOW - ch->our_window - ch->unconsumed;
    uint8_t m[16];
    struct sbuf b;
    if (ch->conn->kex || ch->conn->dead || ch->close_sent || ch->our_window + ch->unconsumed > WINDOW
        || open < WINDOW / 2)
        return;
    sb_init(&b, m, sizeof m);
    sb_u8(&b, MSG_CHANNEL_WINDOW_ADJUST);
    sb_u32(&b, ch->peer);
    sb_u32(&b, open);
    send_sb(ch->conn, &b);
    ch->our_window += open;
}

void ssh_chan_hold(struct ssh_chan *ch) { ch->held = 1; }

void ssh_chan_consumed(struct ssh_chan *ch, uint32_t n)
{
    ch->unconsumed -= n < ch->unconsumed ? n : ch->unconsumed;
    window_adjust(ch);
}

static int chan_data(struct ssh_conn *c, const uint8_t *p, uint32_t n, int ext)
{
    struct sread r;
    struct ssh_chan *ch;
    const uint8_t *d;
    uint32_t len;

    sr_init(&r, p + 1, n - 1);
    ch = chan_of(c, &r);
    if (ext)
        sr_u32(&r);
    d = sr_string(&r, &len);
    if (r.err || !ch)
        return 0;
    if (len > ch->our_window)
        return disconnect(c, DISC_PROTOCOL_ERROR, "window exceeded"), -1;
    ch->our_window -= len;
    if (!ext && !ch->eof_recv && len) {
        if (ch->held)
            ch->unconsumed += len;
        c->ops->chan_data(ch, d, len);
    }
    window_adjust(ch);
    return 0;
}

/* ---- dispatch */

static int handle(struct ssh_conn *c, const uint8_t *p, uint32_t n)
{
    struct sread r;
    struct ssh_chan *ch;
    uint8_t type = p[0], m[64];
    struct sbuf b;

    if (c->ignore_next) {                       /* the client's wrong guess (kexinit first_kex_packet_follows) */
        c->ignore_next = 0;
        return 0;
    }
    if (c->kex && type != MSG_KEXINIT && type != MSG_KEX_ECDH_INIT && type != MSG_NEWKEYS
        && type != MSG_DISCONNECT) {
        /* During a key exchange only its own messages; in the first one
           with strict kex, nothing else at all (CVE-2023-48795). */
        if (c->strict && !c->first_kex_done)
            return disconnect(c, DISC_PROTOCOL_ERROR, "strict kex: unexpected message"), -1;
        if (type != MSG_IGNORE && type != MSG_DEBUG && type != MSG_UNIMPLEMENTED)
            return disconnect(c, DISC_PROTOCOL_ERROR, "message during key exchange"), -1;
        return 0;
    }
    switch (type) {
    case MSG_DISCONNECT:
        c->ops->log("client disconnected");
        c->dead = 1;
        return -1;
    case MSG_IGNORE:
    case MSG_DEBUG:
    case MSG_UNIMPLEMENTED:
        return 0;
    case MSG_KEXINIT:
        return kexinit(c, p, n);
    case MSG_KEX_ECDH_INIT:
        return ecdh_init(c, p, n);
    case MSG_NEWKEYS:
        return newkeys(c);
    }
    if (!c->first_kex_done)
        return disconnect(c, DISC_PROTOCOL_ERROR, "message before keys"), -1;
    if (type == MSG_SERVICE_REQUEST) {
        const uint8_t *s;
        uint32_t slen;
        sr_init(&r, p + 1, n - 1);
        s = sr_string(&r, &slen);
        if (!sr_streq(s, slen, "ssh-userauth"))
            return disconnect(c, DISC_SERVICE_NOT_AVAILABLE, "no such service"), -1;
        c->service_ok = 1;
        sb_init(&b, m, sizeof m);
        sb_u8(&b, MSG_SERVICE_ACCEPT);
        sb_cstr(&b, "ssh-userauth");
        send_sb(c, &b);
        return 0;
    }
    if (type == MSG_USERAUTH_REQUEST && c->service_ok)
        return userauth(c, p, n);
    if (!c->authed)
        return disconnect(c, DISC_PROTOCOL_ERROR, "not authenticated"), -1;
    switch (type) {
    case MSG_GLOBAL_REQUEST: {
        const uint8_t *name;
        uint32_t nlen;
        sr_init(&r, p + 1, n - 1);
        name = sr_string(&r, &nlen);
        (void)name;
        if (!r.err && sr_u8(&r)) {
            m[0] = MSG_REQUEST_FAILURE;
            send_packet(c, m, 1);
        }
        return 0;
    }
    case MSG_CHANNEL_OPEN:
        return chan_open(c, p, n);
    case MSG_CHANNEL_REQUEST:
        return chan_request(c, p, n);
    case MSG_CHANNEL_DATA:
        return chan_data(c, p, n, 0);
    case MSG_CHANNEL_EXTENDED_DATA:
        return chan_data(c, p, n, 1);
    case MSG_CHANNEL_WINDOW_ADJUST:
        sr_init(&r, p + 1, n - 1);
        ch = chan_of(c, &r);
        if (ch) {
            uint32_t add = sr_u32(&r);
            ch->peer_window = ch->peer_window + add < ch->peer_window ? 0xFFFFFFFFu : ch->peer_window + add;
            flush(ch);
        }
        return 0;
    case MSG_CHANNEL_EOF:
        sr_init(&r, p + 1, n - 1);
        ch = chan_of(c, &r);
        if (ch && !ch->eof_recv) {
            ch->eof_recv = 1;
            c->ops->chan_eof(ch);
        }
        return 0;
    case MSG_CHANNEL_CLOSE:
        sr_init(&r, p + 1, n - 1);
        ch = chan_of(c, &r);
        if (ch) {
            ch->close_recv = 1;
            if (!ch->close_sent) {
                chan_msg(ch, MSG_CHANNEL_CLOSE);
                ch->close_sent = 1;
            }
            chan_free(ch);
        }
        return 0;
    case MSG_CHANNEL_SUCCESS:
    case MSG_CHANNEL_FAILURE:
        return 0;
    }
    sb_init(&b, m, sizeof m);                   /* anything else */
    sb_u8(&b, MSG_UNIMPLEMENTED);
    sb_u32(&b, c->rx_seq - 1);
    send_sb(c, &b);
    return 0;
}

/* ---- input */

static int version_line(struct ssh_conn *c)
{
    uint32_t i, end;
    for (i = 0; i < c->rx_len && c->rx[i] != '\n'; i++) ;
    if (i == c->rx_len)
        return c->rx_len > 255 ? -1 : 0;        /* not yet */
    end = i && c->rx[i - 1] == '\r' ? i - 1 : i;
    if (end >= 4 && !memcmp(c->rx, "SSH-", 4)) {
        if (!(end >= 8 && !memcmp(c->rx, "SSH-2.0-", 8)) && !(end >= 9 && !memcmp(c->rx, "SSH-1.99-", 9)))
            return -1;
        char v[64];
        memcpy(c->vc, c->rx, end);
        c->vc_len = end;
        c->got_version = 1;
        memcpy(v, c->vc, end > 60 ? 60 : end);
        v[end > 60 ? 60 : end] = 0;
        c->ops->log("client version=%s", v);
    }
    memmove(c->rx, c->rx + i + 1, c->rx_len - i - 1);   /* other lines before it are ignored */
    c->rx_len -= i + 1;
    return 1;
}

/* One whole packet from rx, if there is one: 1, 0 not yet, -1 broken. */
static int packet(struct ssh_conn *c)
{
    uint8_t nonce[8], ks[4], tag[16], *pay;
    uint32_t plen, total, padlen, len;
    int rc;

    if (!c->rx_enc) {
        if (c->rx_len < 5)
            return 0;
        plen = be32(c->rx);
        if (plen < 5 || plen > SSH_MAX_PACKET)
            return disconnect(c, DISC_PROTOCOL_ERROR, "bad packet length"), -1;
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
            if (plen < 8 || plen > SSH_MAX_PACKET || plen % 8)
                return disconnect(c, DISC_PROTOCOL_ERROR, "bad packet length"), -1;
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
            return disconnect(c, DISC_MAC_ERROR, "bad mac"), -1;
        c->rx_plen = 0;
        padlen = c->rwork[64];
        pay = c->rwork + 65;
    }
    if (padlen < 4 || padlen + 1 >= plen)
        return disconnect(c, DISC_PROTOCOL_ERROR, "bad padding"), -1;
    len = plen - padlen - 1;
    c->rx_seq++;
    if (!c->rx_enc) {                           /* the payload lives in rx, which is about to move */
        memcpy(c->rwork + 65, pay, len);
        pay = c->rwork + 65;
    }
    memmove(c->rx, c->rx + total, c->rx_len - total);
    c->rx_len -= total;
    rc = handle(c, pay, len);                   /* replies are built in twork, never rwork */
    return rc < 0 ? -1 : 1;
}

int ssh_conn_input(struct ssh_conn *c, const uint8_t *data, uint32_t len)
{
    int rc;
    if (c->dead)
        return -1;
    while (len) {
        uint32_t k = RX_CAP - c->rx_len;
        if (!k)
            return disconnect(c, DISC_PROTOCOL_ERROR, "input overflow"), -1;
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
                return -1;
            }
            if (rc == 0)
                break;
        }
    }
    return 0;
}

struct ssh_conn *ssh_conn_new(const struct ssh_server *srv, void *io)
{
    const struct ssh_ops *ops = srv->ops;
    struct ssh_conn *c = ops->alloc(sizeof *c);
    char line[64];
    uint32_t n;
    if (!c)
        return NULL;
    c->srv = srv;
    c->ops = ops;
    c->io = io;
    c->rx = ops->alloc(RX_CAP);
    c->rwork = ops->alloc(WORK_CAP);
    c->twork = ops->alloc(WORK_CAP);
    if (!c->rx || !c->rwork || !c->twork) {
        ssh_conn_free(c);
        return NULL;
    }
    n = (uint32_t)strlen(srv->version);
    memcpy(line, srv->version, n);
    line[n++] = '\r';
    line[n++] = '\n';
    ops->send(io, (const uint8_t *)line, n);
    send_kexinit(c);
    return c;
}

void ssh_conn_free(struct ssh_conn *c)
{
    const struct ssh_ops *ops;
    int i;
    if (!c)
        return;
    ops = c->ops;
    for (i = 0; i < SSH_MAX_CHANNELS; i++)
        if (c->chans[i])
            chan_free(c->chans[i]);
    if (c->rx) memset(c->rx, 0, RX_CAP);
    if (c->rwork) memset(c->rwork, 0, WORK_CAP);
    if (c->twork) memset(c->twork, 0, WORK_CAP);
    memset(c->rx_key, 0, sizeof c->rx_key);
    memset(c->tx_key, 0, sizeof c->tx_key);
    memset(c->next_rx_key, 0, sizeof c->next_rx_key);
    ops->free(c->rx);
    ops->free(c->rwork);
    ops->free(c->twork);
    ops->free(c->ic);
    ops->free(c->is);
    ops->free(c);
}

int ssh_conn_authenticated(const struct ssh_conn *c) { return c->authed; }
