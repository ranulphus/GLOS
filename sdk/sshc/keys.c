/* The SSH client's keys and known hosts (glos/sshc.h): an Ed25519 key made
 * and written as OpenSSH writes one (PROTOCOL.key, unencrypted: ssh-keygen -t
 * ed25519 -N ""), read back with the kernel's parser (kernel/ssh/sshkeys.c),
 * and KNOWNHST.TXT in OpenSSH's known_hosts format. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto_sign_ed25519.h"
#include "randombytes.h"
#include "sshbuf.h"
#include <glos/sshc.h>

int sshc_keygen(uint8_t sk[64], uint8_t pk[32])
{
    return crypto_sign_ed25519_keypair(pk, sk) == 0 ? 0 : -1;
}

static uint32_t pub_blob(uint8_t blob[51], const uint8_t pk[32])
{
    struct sbuf b;
    sb_init(&b, blob, 51);
    sb_cstr(&b, "ssh-ed25519");
    sb_string(&b, pk, 32);
    return b.len;
}

int sshc_key_write(const char *path, const uint8_t sk[64], const uint8_t pk[32], const char *comment)
{
    uint8_t blob[51], priv[512], all[1024], check[4];
    char text[1500], pubpath[300];
    struct sbuf p, a;
    uint32_t i, n, blen = pub_blob(blob, pk);
    FILE *f;
    randombytes(check, 4);
    sb_init(&p, priv, sizeof priv);
    sb_bytes(&p, check, 4);
    sb_bytes(&p, check, 4);
    sb_cstr(&p, "ssh-ed25519");
    sb_string(&p, pk, 32);
    sb_string(&p, sk, 64);
    sb_cstr(&p, comment);
    for (i = 1; p.len % 8; i++)                 /* (padding 1, 2, 3 ... to the cipher's block, 8 for "none") */
        sb_u8(&p, (uint8_t)i);
    sb_init(&a, all, sizeof all);
    sb_bytes(&a, "openssh-key-v1", 15);
    sb_cstr(&a, "none");
    sb_cstr(&a, "none");
    sb_cstr(&a, "");
    sb_u32(&a, 1);
    sb_string(&a, blob, blen);
    sb_string(&a, priv, p.len);
    memset(priv, 0, sizeof priv);
    if (p.err || a.err)
        return -1;
    n = (uint32_t)sprintf(text, "-----BEGIN OPENSSH PRIVATE KEY-----\n");
    {
        char line[1400];
        uint32_t k = b64_encode(line, sizeof line, all, a.len), at;
        for (at = 0; at < k; at += 70)
            n += (uint32_t)sprintf(text + n, "%.*s\n", (int)(k - at < 70 ? k - at : 70), line + at);
        memset(line, 0, sizeof line);
    }
    n += (uint32_t)sprintf(text + n, "-----END OPENSSH PRIVATE KEY-----\n");
    memset(all, 0, sizeof all);
    if (!(f = fopen(path, "wb")) || fwrite(text, 1, n, f) != n || fclose(f) != 0) {
        memset(text, 0, sizeof text);
        return -1;
    }
    memset(text, 0, sizeof text);
    snprintf(pubpath, sizeof pubpath, "%s", path);
    {
        char *dot = strrchr(pubpath, '.'), *slash = strrchr(pubpath, '\\');
        if (dot && (!slash || dot > slash))
            *dot = 0;
        strncat(pubpath, ".PUB", sizeof pubpath - strlen(pubpath) - 1);
    }
    {
        char b64[80];
        b64_encode(b64, sizeof b64, blob, blen);
        if (!(f = fopen(pubpath, "w")) || fprintf(f, "ssh-ed25519 %s %s\n", b64, comment) < 0 || fclose(f) != 0)
            return -1;
    }
    return 0;
}

int sshc_key_read(const char *path, uint8_t sk[64], uint8_t pk[32])
{
    FILE *f = fopen(path, "rb");
    char text[4096];
    size_t n;
    int r;
    if (!f)
        return -1;
    n = fread(text, 1, sizeof text, f);
    fclose(f);
    r = ssh_parse_privkey(text, (uint32_t)n, sk, pk);
    memset(text, 0, sizeof text);
    return r == 0 ? 0 : -1;
}

/* ---- known hosts */

static void host_field(char *out, size_t room, const char *host, unsigned port)
{
    if (port == 22)
        snprintf(out, room, "%s", host);
    else
        snprintf(out, room, "[%s]:%u", host, port);
}

/* One of a line's comma-separated names is `name` (case aside). */
static int names_have(const char *names, size_t n, const char *name)
{
    size_t k = strlen(name);
    while (n) {
        size_t i = 0;
        while (i < n && names[i] != ',')
            i++;
        if (i == k && !strncasecmp(names, name, k))
            return 1;
        names += i < n ? i + 1 : i;
        n -= i < n ? i + 1 : i;
    }
    return 0;
}

int sshc_known(const char *path, const char *host, unsigned port, const uint8_t pk[32])
{
    FILE *f = fopen(path, "r");
    char line[1024], name[300];
    int found = 0;
    if (!f)
        return -2;
    host_field(name, sizeof name, host, port);
    while (fgets(line, sizeof line, f)) {
        char *p = line, *names, *type, *key;
        size_t nlen;
        uint8_t blob[64];
        uint32_t blen;
        while (isspace((unsigned char)*p))
            p++;
        if (!*p || *p == '#' || *p == '@' || *p == '|')     /* (markers and hashed names: not read) */
            continue;
        names = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        nlen = (size_t)(p - names);
        if (!names_have(names, nlen, name))
            continue;
        while (isspace((unsigned char)*p))
            p++;
        type = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (p - type != 11 || strncmp(type, "ssh-ed25519", 11))
            continue;                           /* (another type for this host: not ours to judge) */
        while (isspace((unsigned char)*p))
            p++;
        key = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        blen = b64_decode(blob, sizeof blob, key, (uint32_t)(p - key));
        if (blen == 51 && !memcmp(blob + 19, pk, 32)) {
            fclose(f);
            return 1;
        }
        found = 1;
    }
    fclose(f);
    return found ? -1 : 0;
}

int sshc_known_add(const char *path, const char *host, unsigned port, const uint8_t pk[32])
{
    uint8_t blob[51];
    char b64[80], name[300];
    FILE *f = fopen(path, "a");
    if (!f)
        return -1;
    host_field(name, sizeof name, host, port);
    b64_encode(b64, sizeof b64, blob, pub_blob(blob, pk));
    if (fprintf(f, "%s ssh-ed25519 %s\n", name, b64) < 0) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}
