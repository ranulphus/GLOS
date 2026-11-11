/* The secrets store's arithmetic and format (kernel/include/secstore.h; API 2.0's SECRETS; docs/v1/phase-b.md
 * step 13). TinySSH's SHA-256, ChaCha20 and Poly1305 (third_party/tinyssh), which hash and encrypt a whole
 * buffer at a time: HMAC (RFC 2104) and PBKDF2 (RFC 8018) are built on that here, for short messages. */
#include <string.h>

#include "crypto_hash_sha256.h"
#include "crypto_onetimeauth_poly1305.h"
#include "crypto_stream_chacha20.h"
#include "crypto_verify_16.h"

#include "secstore.h"

void hmac_sha256(const u8 *key, u32 klen, const u8 *msg, u32 mlen, u8 out[32])
{
    u8 k[64], buf[64 + 192], inner[32];
    u32 i;
    memset(k, 0, sizeof k);
    if (klen > 64)
        crypto_hash_sha256(k, key, klen);       /* (a key longer than a block: its hash) */
    else
        memcpy(k, key, klen);
    if (mlen > 192)
        mlen = 192;
    for (i = 0; i < 64; i++)
        buf[i] = k[i] ^ 0x36;
    memcpy(buf + 64, msg, mlen);
    crypto_hash_sha256(inner, buf, 64 + mlen);
    for (i = 0; i < 64; i++)
        buf[i] = k[i] ^ 0x5C;
    memcpy(buf + 64, inner, 32);
    crypto_hash_sha256(out, buf, 64 + 32);
    memset(k, 0, sizeof k);
    memset(buf, 0, sizeof buf);
}

/* One block of PBKDF2's output (32 bytes: all a ChaCha20 key needs). */
void pbkdf2_sha256(const u8 *pw, u32 pwlen, const u8 *salt, u32 saltlen, u32 iters, u8 out[32])
{
    u8 u[32], first[32 + 4];
    u32 i, k;
    if (saltlen > 32)
        saltlen = 32;
    memcpy(first, salt, saltlen);
    first[saltlen] = 0, first[saltlen + 1] = 0, first[saltlen + 2] = 0, first[saltlen + 3] = 1;
    hmac_sha256(pw, pwlen, first, saltlen + 4, u);
    memcpy(out, u, 32);
    for (i = 1; i < iters; i++) {
        hmac_sha256(pw, pwlen, u, 32, u);
        for (k = 0; k < 32; k++)
            out[k] ^= u[k];
    }
    memset(u, 0, sizeof u);
}

/* ---- the records */

static u32 rec_len(const u8 *r) { return 5u + r[1] + r[2] + (u32)(r[3] | r[4] << 8); }

static const u8 *find(const struct secstore *s, int kind, const char *owner, const char *name)
{
    u32 at = 0, ol = (u32)strlen(owner), nl = (u32)strlen(name);
    while (at + 5 <= s->len && at + rec_len(s->b + at) <= s->len) {
        const u8 *r = s->b + at;
        if (r[0] == kind && r[1] == ol && r[2] == nl && !memcmp(r + 5, owner, ol) && !memcmp(r + 5 + ol, name, nl))
            return r;
        at += rec_len(r);
    }
    return 0;
}

int ss_remove(struct secstore *s, int kind, const char *owner, const char *name)
{
    u8 *r = (u8 *)find(s, kind, owner, name);
    u32 n;
    if (!r)
        return -1;
    n = rec_len(r);
    memmove(r, r + n, s->len - (u32)(r - s->b) - n);
    s->len -= n;
    memset(s->b + s->len, 0, n);                /* (nothing of it left behind) */
    return 0;
}

int ss_put(struct secstore *s, int kind, const char *owner, const char *name, const u8 *value, u32 vlen)
{
    u32 ol = (u32)strlen(owner), nl = (u32)strlen(name), need = 5 + ol + nl + vlen, old = 0;
    const u8 *was = find(s, kind, owner, name);
    u8 *r;
    if (!ol || ol > SS_NAME_MAX || !nl || nl > SS_NAME_MAX || vlen > SS_VALUE_MAX)
        return -2;
    if (was)
        old = rec_len(was);
    if (s->len - old + need > SS_ROOM)
        return -1;
    if (was)
        ss_remove(s, kind, owner, name);
    r = s->b + s->len;
    r[0] = (u8)kind, r[1] = (u8)ol, r[2] = (u8)nl, r[3] = (u8)vlen, r[4] = (u8)(vlen >> 8);
    memcpy(r + 5, owner, ol);
    memcpy(r + 5 + ol, name, nl);
    if (vlen)
        memcpy(r + 5 + ol + nl, value, vlen);
    s->len += need;
    return 0;
}

int ss_get(const struct secstore *s, int kind, const char *owner, const char *name, u8 *out, u32 room, u32 *vlen)
{
    const u8 *r = find(s, kind, owner, name);
    u32 n;
    if (!r)
        return -1;
    n = (u32)(r[3] | r[4] << 8);
    if (vlen)
        *vlen = n;
    if (out && room)
        memcpy(out, r + 5 + r[1] + r[2], n < room ? n : room);
    return 0;
}

u32 ss_count(const struct secstore *s, int kind)
{
    u32 at = 0, n = 0;
    while (at + 5 <= s->len && at + rec_len(s->b + at) <= s->len) {
        n += s->b[at] == kind;
        at += rec_len(s->b + at);
    }
    return n;
}

/* ---- the file */

static void put32(u8 *p, u32 v) { p[0] = (u8)v, p[1] = (u8)(v >> 8), p[2] = (u8)(v >> 16), p[3] = (u8)(v >> 24); }
static u32 get32(const u8 *p) { return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24; }

int ss_head(const u8 *file, u32 len, struct ss_head *h)
{
    if (len < SS_HEAD + SS_TAG || memcmp(file, "GLOSSEC1", 8) != 0)
        return -1;
    h->flags = get32(file + 8);
    h->iters = get32(file + 12);
    memcpy(h->salt, file + 16, 16);
    memcpy(h->nonce, file + 32, 8);
    h->length = get32(file + 40);
    return h->length <= SS_ROOM && len == SS_HEAD + h->length + SS_TAG ? 0 : -1;
}

/* The stream for a key and a nonce over the records: XORed in place from its byte 64; its first 32 bytes are the
   tag's key (out). */
static void stream(u8 *text, u32 len, const u8 nonce[8], const u8 key[32], u8 mac_key[32])
{
    static u8 work[64 + SS_ROOM];               /* (one store, one writer: secrets.c holds its lock) */
    memset(work, 0, 64);
    memcpy(work + 64, text, len);
    crypto_stream_chacha20_xor(work, work, 64 + len, nonce, key);
    memcpy(mac_key, work, 32);
    memcpy(text, work + 64, len);
    memset(work, 0, 64 + len);
}

u32 ss_seal(const struct secstore *s, const struct ss_head *h, const u8 key[32], u8 *file)
{
    u8 mac_key[32];
    memcpy(file, "GLOSSEC1", 8);
    put32(file + 8, h->flags);
    put32(file + 12, h->iters);
    memcpy(file + 16, h->salt, 16);
    memcpy(file + 32, h->nonce, 8);
    put32(file + 40, s->len);
    memcpy(file + SS_HEAD, s->b, s->len);
    stream(file + SS_HEAD, s->len, h->nonce, key, mac_key);
    crypto_onetimeauth_poly1305(file + SS_HEAD + s->len, file, SS_HEAD + s->len, mac_key);
    memset(mac_key, 0, sizeof mac_key);
    return SS_HEAD + s->len + SS_TAG;
}

int ss_open(const u8 *file, u32 len, const u8 key[32], struct secstore *s)
{
    struct ss_head h;
    u8 mac_key[32], tag[16], none[1] = { 0 };
    if (ss_head(file, len, &h) != 0)
        return -1;
    stream(none, 0, h.nonce, key, mac_key);     /* (the tag first: nothing is decrypted that doesn't pass) */
    crypto_onetimeauth_poly1305(tag, file, SS_HEAD + h.length, mac_key);
    if (crypto_verify_16(tag, file + SS_HEAD + h.length) != 0) {
        memset(mac_key, 0, sizeof mac_key);
        return -2;
    }
    memset(s, 0, sizeof *s);
    memcpy(s->b, file + SS_HEAD, h.length);
    stream(s->b, h.length, h.nonce, key, mac_key);
    s->len = h.length;
    memset(mac_key, 0, sizeof mac_key);
    return 0;
}
