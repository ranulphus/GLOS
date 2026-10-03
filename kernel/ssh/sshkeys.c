/* Key files (PRD §7.1, D29): OpenSSH's private key format, unencrypted
 * ed25519 only (`ssh-keygen -t ed25519 -N ""`), and authorized_keys lines
 * of type ssh-ed25519 (lines with options, or of other types, are
 * skipped). The format: PROTOCOL.key in OpenSSH's sources. */
#include <string.h>
#include "sshbuf.h"

static int b64v(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/* Base64, skipping white space; stops at '=' or anything else. */
uint32_t b64_decode(uint8_t *out, uint32_t cap, const char *in, uint32_t n)
{
    uint32_t acc = 0, bits = 0, len = 0, i;
    for (i = 0; i < n; i++) {
        int v;
        if (in[i] == ' ' || in[i] == '\r' || in[i] == '\n' || in[i] == '\t')
            continue;
        if ((v = b64v(in[i])) < 0)
            break;
        acc = acc << 6 | (uint32_t)v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (len < cap)
                out[len] = (uint8_t)(acc >> bits);
            len++;
        }
    }
    return len <= cap ? len : 0;
}

uint32_t b64_encode(char *out, uint32_t cap, const uint8_t *in, uint32_t n)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t i, o = 0;
    for (i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16 | (i + 1 < n ? (uint32_t)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        if (o + 4 >= cap)
            return 0;
        out[o++] = t[v >> 18];
        out[o++] = t[(v >> 12) & 63];
        out[o++] = i + 1 < n ? t[(v >> 6) & 63] : '=';
        out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    out[o] = 0;
    return o;
}

static const char *find(const char *text, uint32_t n, const char *s)
{
    uint32_t k = (uint32_t)strlen(s), i;
    for (i = 0; i + k <= n; i++)
        if (memcmp(text + i, s, k) == 0)
            return text + i;
    return NULL;
}

int ssh_parse_privkey(const char *text, uint32_t n, uint8_t sk[64], uint8_t pk[32])
{
    static uint8_t blob[1024];
    const char *a = find(text, n, "-----BEGIN OPENSSH PRIVATE KEY-----"), *b;
    struct sread r, s;
    const uint8_t *p, *q;
    uint32_t len, plen, qlen;

    if (!a || !(b = find(a, n - (uint32_t)(a - text), "-----END OPENSSH PRIVATE KEY-----")))
        return -1;
    a += 35;
    len = b64_decode(blob, sizeof blob, a, (uint32_t)(b - a));
    sr_init(&r, blob, len);
    p = sr_bytes(&r, 15);
    if (!p || memcmp(p, "openssh-key-v1", 15) != 0)
        return -1;
    p = sr_string(&r, &plen);                   /* cipher, kdf: none, none */
    if (!sr_streq(p, plen, "none"))
        return -2;                              /* encrypted: not supported */
    p = sr_string(&r, &plen);
    if (!sr_streq(p, plen, "none"))
        return -2;
    sr_string(&r, &plen);                       /* kdf options */
    if (sr_u32(&r) != 1)                        /* one key */
        return -1;
    sr_string(&r, &plen);                       /* its public blob */
    p = sr_string(&r, &plen);                   /* the private section */
    if (r.err)
        return -1;
    sr_init(&s, p, plen);
    if (sr_u32(&s) != sr_u32(&s))               /* check words */
        return -1;
    p = sr_string(&s, &plen);
    if (!sr_streq(p, plen, "ssh-ed25519"))
        return -3;                              /* another key type */
    p = sr_string(&s, &plen);
    q = sr_string(&s, &qlen);
    if (s.err || plen != 32 || qlen != 64 || memcmp(q + 32, p, 32) != 0)
        return -1;
    memcpy(pk, p, 32);
    memcpy(sk, q, 64);                          /* seed and public key, as TinySSH wants */
    memset(blob, 0, sizeof blob);
    return 0;
}

int ssh_parse_authkeys(const char *text, uint32_t n, uint8_t (*pks)[32], int max)
{
    uint8_t blob[128];
    uint32_t i = 0, end, len;
    int count = 0;

    while (i < n && count < max) {
        for (end = i; end < n && text[end] != '\n'; end++) ;
        if (end - i > 12 && memcmp(text + i, "ssh-ed25519 ", 12) == 0) {
            uint32_t k = i + 12, e;
            struct sread r;
            const uint8_t *p;
            uint32_t plen;
            for (e = k; e < end && text[e] != ' ' && text[e] != '\r'; e++) ;
            len = b64_decode(blob, sizeof blob, text + k, e - k);
            sr_init(&r, blob, len);
            p = sr_string(&r, &plen);
            if (sr_streq(p, plen, "ssh-ed25519")) {
                p = sr_string(&r, &plen);
                if (!r.err && plen == 32)
                    memcpy(pks[count++], p, 32);
            }
        }
        i = end + 1;
    }
    return count;
}
