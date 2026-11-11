/* The secrets store's arithmetic and format (kernel/core/secstore.c): the key derivation's known answers (RFC
   4231's HMAC-SHA-256; PBKDF2 against Python's hashlib), the records, the file sealed and opened. */
#include <stdio.h>
#include <string.h>

#include "randombytes.h"
#include "secstore.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("secstore_test: line %d: %s\n", __LINE__, #c); fails++; } } while (0)

void randombytes(void *out, long long len) { memset(out, 0x5A, (size_t)len); }  /* (TinySSH's sources want one) */

static int is(const u8 *got, const char *hex)
{
    char s[65];
    int i;
    for (i = 0; i < 32; i++)
        sprintf(s + 2 * i, "%02x", got[i]);
    return !strcmp(s, hex);
}

static struct secstore s, t;
static u8 file[SS_FILE_MAX], big[SS_VALUE_MAX];

int main(void)
{
    static const char *longpw = "a master password of some length, longer than a block of the hash's: sixty-four bytes";
    struct ss_head h, h2;
    u8 out[32], key[32], key2[32], v[64];
    u32 n = 0, len, i;

    memset(out, 0x0B, 20);
    hmac_sha256(out, 20, (const u8 *)"Hi There", 8, key);
    CHECK(is(key, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
    hmac_sha256((const u8 *)"Jefe", 4, (const u8 *)"what do ya want for nothing?", 28, key);
    CHECK(is(key, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));
    pbkdf2_sha256((const u8 *)"passwd", 6, (const u8 *)"salt", 4, 1, key);
    CHECK(is(key, "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc"));
    pbkdf2_sha256((const u8 *)"Password", 8, (const u8 *)"NaCl", 4, 1000, key);
    CHECK(is(key, "c27dad0abae39af4ebb9965719d584e8b4eb2ee69e1fc9f8f4784d1ca68696e2"));
    pbkdf2_sha256((const u8 *)longpw, (u32)strlen(longpw), (const u8 *)"0123456789abcdef", 16, 3, key);
    CHECK(is(key, "6d3bc31e4510d19cc7e4b0c38c7e42652025b487cae460b6c0f4c11fe2e3ea87"));

    /* Records: put, got, put again in the same place, another owner's of the same name apart, removed. */
    CHECK(ss_put(&s, SS_SECRET, "TERM.EXE", "host1", (const u8 *)"hunter2", 7) == 0 && ss_count(&s, SS_SECRET) == 1);
    CHECK(ss_put(&s, SS_SECRET, "CHAT.EXE", "host1", (const u8 *)"other", 5) == 0);
    CHECK(ss_put(&s, SS_SECRET, "*", "wifi", (const u8 *)"shared", 6) == 0 && ss_put(&s, SS_GRANT, "CHAT.EXE", "wifi", 0, 0) == 0);
    CHECK(ss_get(&s, SS_SECRET, "TERM.EXE", "host1", v, sizeof v, &n) == 0 && n == 7 && !memcmp(v, "hunter2", 7));
    CHECK(ss_get(&s, SS_SECRET, "CHAT.EXE", "host1", v, sizeof v, &n) == 0 && n == 5 && !memcmp(v, "other", 5));
    CHECK(ss_get(&s, SS_SECRET, "TERM.EXE", "host", v, sizeof v, &n) == -1 && ss_get(&s, SS_SECRET, "TERM.EX", "host1", v, 8, &n) == -1);
    CHECK(ss_get(&s, SS_GRANT, "CHAT.EXE", "wifi", 0, 0, &n) == 0 && n == 0 && ss_get(&s, SS_GRANT, "TERM.EXE", "wifi", 0, 0, 0) == -1);
    CHECK(ss_get(&s, SS_SECRET, "CHAT.EXE", "wifi", 0, 0, 0) == -1);            /* (a grant is no secret) */
    len = s.len;
    CHECK(ss_put(&s, SS_SECRET, "TERM.EXE", "host1", (const u8 *)"longer than it was", 18) == 0 && s.len == len + 11);
    CHECK(ss_get(&s, SS_SECRET, "TERM.EXE", "host1", v, 4, &n) == 0 && n == 18 && !memcmp(v, "long", 4));    /* (cut to the room) */
    CHECK(ss_count(&s, SS_SECRET) == 3 && ss_count(&s, SS_GRANT) == 1);
    CHECK(ss_remove(&s, SS_SECRET, "CHAT.EXE", "host1") == 0 && ss_remove(&s, SS_SECRET, "CHAT.EXE", "host1") == -1);
    CHECK(ss_count(&s, SS_SECRET) == 2 && ss_get(&s, SS_SECRET, "*", "wifi", v, sizeof v, &n) == 0 && n == 6);
    CHECK(ss_put(&s, SS_SECRET, "", "x", v, 1) == -2 && ss_put(&s, SS_SECRET, "A.EXE", "", v, 1) == -2);
    CHECK(ss_put(&s, SS_SECRET, "A.EXE", "x", big, SS_VALUE_MAX + 1) == -2 && ss_put(&s, SS_SECRET, "A.EXE", "x", big, 0) == 0);
    CHECK(ss_put(&s, SS_SECRET, "A.EXE", "0123456789012345678901234567890123456789012345678901234567890123", v, 1) == -2);
    memset(big, 0xC3, sizeof big);
    len = s.len;
    for (i = 0; i < 40 && ss_put(&s, SS_SECRET, "BIG.EXE", (char[]){ (char)('a' + i), 0 }, big, SS_VALUE_MAX) == 0; i++)
        ;
    CHECK(i == 15 && s.len <= SS_ROOM && ss_put(&s, SS_SECRET, "BIG.EXE", "z", big, SS_VALUE_MAX) == -1);       /* full */
    CHECK(ss_put(&s, SS_SECRET, "BIG.EXE", "a", big, 10) == 0 && ss_get(&s, SS_SECRET, "TERM.EXE", "host1", v, sizeof v, &n) == 0);

    /* The file: sealed and opened with its key; another key, or a byte changed anywhere, is refused, and nothing
       of it is given; the header alone says how its key is made. */
    memset(&h, 0, sizeof h);
    h.flags = SS_F_PASSWORD, h.iters = 4096;
    memset(h.salt, 0x11, 16), memset(h.nonce, 0x22, 8);
    memset(key, 0x33, 32), memset(key2, 0x33, 32);
    key2[31] ^= 1;
    len = ss_seal(&s, &h, key, file);
    CHECK(len == SS_HEAD + s.len + SS_TAG && !memcmp(file, "GLOSSEC1", 8) && memcmp(file + SS_HEAD, s.b, s.len) != 0);
    CHECK(ss_head(file, len, &h2) == 0 && h2.flags == SS_F_PASSWORD && h2.iters == 4096 && h2.length == s.len
          && !memcmp(h2.salt, h.salt, 16) && !memcmp(h2.nonce, h.nonce, 8));
    CHECK(ss_open(file, len, key, &t) == 0 && t.len == s.len && !memcmp(t.b, s.b, s.len));
    memset(&t, 0x77, sizeof t);
    CHECK(ss_open(file, len, key2, &t) == -2 && t.b[0] == 0x77);
    for (i = 0; i < len; i += 37) {
        file[i] ^= 0x40;
        CHECK(ss_open(file, len, key, &t) != 0);
        file[i] ^= 0x40;
    }
    CHECK(ss_open(file, len - 1, key, &t) == -1 && ss_open(file, 10, key, &t) == -1 && ss_head((const u8 *)"GLOSSEC2", 8, &h2) == -1);
    memset(&t, 0, sizeof t);                            /* an empty store's file */
    len = ss_seal(&t, &h, key, file);
    CHECK(len == SS_HEAD + SS_TAG && ss_open(file, len, key, &s) == 0 && s.len == 0 && ss_count(&s, SS_SECRET) == 0);

    if (fails)
        return 1;
    printf("secstore_test: ok\n");
    return 0;
}
