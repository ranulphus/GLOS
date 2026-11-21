/* Locked PDFs opened, with no password or with one (pdfcrypt.h): the standard security handler, from ISO 32000-1 7.6 (and
 * for its two newest kinds, Adobe's Supplement to it and ISO 32000-2).
 *
 * The "Encrypt" dictionary says the kind: V (how the file is locked: 1 and 2 are RC4, 4 names "crypt filters"
 * for strings and for streams, each RC4 or AES of 128 bits, 5 is AES of 256 bits), R (how the key is made: 2 to
 * 6), the key's Length, O and U (what the owner's and the user's passwords are checked against), P (what the
 * user may do: a part of the key).
 *
 * R 2 to 4: the file's key is the MD5 of the password padded to 32 bytes with a fixed string, O, P and the
 * file's ID (hashed 50 times more from R 3); the password is right if U is that padding encrypted with the key
 * (R 2), or the MD5 of padding and ID encrypted twenty times over (R 3, 4). Each object's strings and streams
 * have a key of their own: the MD5 of the file's key and the object's number and generation.
 *
 * R 5 and 6: U holds a hash of the password and a salt, and a second salt; the file's key is in UE, encrypted
 * with the hash of the password and that second salt. R 5's hash is SHA-256; R 6's goes round 64 times and
 * more through AES and SHA-256, -384 or -512, as the bytes fall. Every object is locked with the file's key.
 *
 * AES is in chained blocks: 16 bytes that start the chain come first, and the last block says how many bytes
 * of it are padding. Whatever the file says, nothing here reads or writes beyond what it was given. */
#include <stdlib.h>
#include <string.h>

#include "../../third_party/bearssl/inc/bearssl_block.h"
#include "../../third_party/bearssl/inc/bearssl_hash.h"
#include "pdfcrypt.h"

enum { NONE, RC4, AES128, AES256 };

struct pdf_crypt {
    unsigned char key[32];
    unsigned nkey;
    int strings, streams;               /* how each is locked */
    int meta;
};

static const unsigned char padding[32] = { 0x28, 0xBF, 0x4E, 0x5E, 0x4E, 0x75, 0x8A, 0x41, 0x64, 0x00, 0x4E, 0x56, 0xFF, 0xFA, 0x01, 0x08,
                                           0x2E, 0x2E, 0x00, 0xB6, 0xD0, 0x68, 0x3E, 0x80, 0x2F, 0x0C, 0xA9, 0xFE, 0x64, 0x53, 0x69, 0x7A };

static void rc4(const unsigned char *key, unsigned nkey, unsigned char *b, unsigned long n)
{
    unsigned char s[256], t;
    unsigned i, j;
    unsigned long k;
    for (i = 0; i < 256; i++)
        s[i] = (unsigned char)i;
    for (i = j = 0; i < 256; i++) {
        j = (j + s[i] + key[i % nkey]) & 255;
        t = s[i], s[i] = s[j], s[j] = t;
    }
    for (i = j = 0, k = 0; k < n; k++) {
        i = (i + 1) & 255, j = (j + s[i]) & 255;
        t = s[i], s[i] = s[j], s[j] = t;
        b[k] ^= s[(s[i] + s[j]) & 255];
    }
}

/* R 6's hash of a password and a salt (ISO 32000-2, algorithm 2.B), and for the owner's password the file's U
   after them: SHA-256 of them, then rounds: the password, the hash so far (and U), 64 times over, encrypted with
   AES (its key and the chain's start from the hash so far); the next hash is SHA-256, -384 or -512 of that, as
   its first 16 bytes' sum falls; until 64 rounds are done and the last byte encrypted is small enough. R 5's is
   the SHA-256 alone. */
static int hash56(long r, const unsigned char *pw, unsigned npw, const unsigned char *salt, const unsigned char *u48,
                  unsigned char out[32])
{
    unsigned char k[64], iv[16], *e;
    unsigned nk = 32, nu = u48 ? 48 : 0, round, one, i, sum;
    br_sha256_context s2;
    br_sha384_context s3;
    br_sha512_context s5;
    br_aes_big_cbcenc_keys aes;
    br_sha256_init(&s2);
    br_sha256_update(&s2, pw, npw);
    br_sha256_update(&s2, salt, 8);
    if (nu)
        br_sha256_update(&s2, u48, nu);
    br_sha256_out(&s2, k);
    if (r == 6) {
        if (!(e = malloc(64ul * (npw + 64 + nu))))
            return -1;
        for (round = 0; round < 1000; round++) {
            one = npw + nk + nu;
            for (i = 0; i < 64; i++) {
                memcpy(e + (unsigned long)i * one, pw, npw);
                memcpy(e + (unsigned long)i * one + npw, k, nk);
                if (nu)
                    memcpy(e + (unsigned long)i * one + npw + nk, u48, nu);
            }
            br_aes_big_cbcenc_init(&aes, k, 16);
            memcpy(iv, k + 16, 16);
            br_aes_big_cbcenc_run(&aes, iv, e, 64ul * one);
            for (i = 0, sum = 0; i < 16; i++)
                sum += e[i];
            if (sum % 3 == 0) {
                br_sha256_init(&s2);
                br_sha256_update(&s2, e, 64ul * one);
                br_sha256_out(&s2, k);
                nk = 32;
            } else if (sum % 3 == 1) {
                br_sha384_init(&s3);
                br_sha384_update(&s3, e, 64ul * one);
                br_sha384_out(&s3, k);
                nk = 48;
            } else {
                br_sha512_init(&s5);
                br_sha512_update(&s5, e, 64ul * one);
                br_sha512_out(&s5, k);
                nk = 64;
            }
            if (round + 1 >= 64 && e[64ul * one - 1] + 32u <= round + 1)
                break;
        }
        free(e);
    }
    memcpy(out, k, 32);
    return 0;
}

static int method(struct pdf *d, struct pdf_obj *enc, const char *which)       /* the crypt filter StmF or StrF names: how */
{
    const char *name = pdf_name(pdf_get(d, enc, which)), *how;
    struct pdf_obj *filter = pdf_get(d, pdf_get(d, enc, "CF"), name);
    if (!name[0] || !strcmp(name, "Identity"))
        return NONE;
    how = pdf_name(pdf_get(d, filter, "CFM"));
    return !strcmp(how, "V2") ? RC4 : !strcmp(how, "AESV2") ? AES128 : !strcmp(how, "AESV3") ? AES256 : !strcmp(how, "None") ? NONE : -1;
}

struct lock {                           /* what the file's Encrypt dictionary says */
    long r;
    const unsigned char *o, *u, *oe, *ue, *id;
    unsigned idn;
    unsigned char p[4];
};

/* R 2 to 4: the file's key from a password as the format pads it (32 bytes), into c; 1 if it is the user's (the
   file's U is what that key makes). */
static int user_key(struct pdf_crypt *c, const struct lock *l, const unsigned char pw[32])
{
    unsigned char k[16], t[32], x[16];
    br_md5_context md;
    unsigned i, j;
    br_md5_init(&md);
    br_md5_update(&md, pw, 32);
    br_md5_update(&md, l->o, 32);
    br_md5_update(&md, l->p, 4);
    br_md5_update(&md, l->id, l->idn);
    if (l->r >= 4 && !c->meta)
        br_md5_update(&md, "\xFF\xFF\xFF\xFF", 4);
    br_md5_out(&md, k);
    for (i = 0; l->r >= 3 && i < 50; i++) {
        br_md5_init(&md);
        br_md5_update(&md, k, c->nkey);
        br_md5_out(&md, k);
    }
    memcpy(c->key, k, c->nkey);
    if (l->r == 2) {                            /* U: the padding, encrypted */
        memcpy(t, padding, 32);
        rc4(c->key, c->nkey, t, 32);
        return !memcmp(t, l->u, 32);
    }
    br_md5_init(&md);                           /* U's first half: the hash of padding and ID, encrypted twenty times */
    br_md5_update(&md, padding, 32);
    br_md5_update(&md, l->id, l->idn);
    br_md5_out(&md, t);
    for (i = 0; i < 20; i++) {
        for (j = 0; j < c->nkey; j++)
            x[j] = (unsigned char)(c->key[j] ^ i);
        rc4(x, c->nkey, t, 16);
    }
    return !memcmp(t, l->u, 16);
}

/* R 2 to 4, the owner's password: O is the user's (padded), locked with a key made from the owner's. Undone
   with the password given, it is the user's password if that was the owner's: tried as such. */
static int owner_key(struct pdf_crypt *c, const struct lock *l, const unsigned char pw[32])
{
    unsigned char k[16], t[32], x[16];
    br_md5_context md;
    unsigned i, j;
    br_md5_init(&md);
    br_md5_update(&md, pw, 32);
    br_md5_out(&md, k);
    for (i = 0; l->r >= 3 && i < 50; i++) {
        br_md5_init(&md);
        br_md5_update(&md, k, 16);
        br_md5_out(&md, k);
    }
    memcpy(t, l->o, 32);
    if (l->r == 2) {
        rc4(k, c->nkey, t, 32);
    } else {
        for (i = 20; i-- > 0; ) {
            for (j = 0; j < c->nkey; j++)
                x[j] = (unsigned char)(k[j] ^ i);
            rc4(x, c->nkey, t, 32);
        }
    }
    return user_key(c, l, t);
}

/* R 5 and 6: the password's hash with the first salt of U (or of O, and U after it, for the owner's) is that
   string's first 32 bytes if the password is right; the file's key is then UE (or OE), undone with the hash by
   the second salt. */
static int key56(struct pdf_crypt *c, const struct lock *l, const unsigned char *pw, unsigned npw, int owner)
{
    const unsigned char *s = owner ? l->o : l->u, *u48 = owner ? l->u : NULL;
    unsigned char h[32], iv[16];
    br_aes_big_cbcdec_keys aes;
    if (hash56(l->r, pw, npw, s + 32, u48, h) != 0 || memcmp(h, s, 32) || hash56(l->r, pw, npw, s + 40, u48, h) != 0)
        return 0;
    memcpy(c->key, owner ? l->oe : l->ue, 32);
    memset(iv, 0, sizeof iv);
    br_aes_big_cbcdec_init(&aes, h, 32);
    br_aes_big_cbcdec_run(&aes, iv, c->key, 32);
    return 1;
}

struct pdf_crypt *pdf_crypt_open(struct pdf *d, struct pdf_obj *enc, const unsigned char *id, unsigned idn, const char *password)
{
    long v = pdf_int(pdf_get(d, enc, "V"), 0), bits = pdf_int(pdf_get(d, enc, "Length"), 40);
    double pv = pdf_num(pdf_get(d, enc, "P"), 0);
    unsigned long p = pv >= 2147483648.0 && pv < 4294967296.0 ? (unsigned long)pv : pv > -2147483649.0 && pv < 2147483648.0
                      ? (unsigned long)(long)pv : 0;
    unsigned on = 0, un = 0, oen = 0, uen = 0, npw = password ? (unsigned)strlen(password) : 0, n = 0, i;
    struct pdf_obj *meta = pdf_get(d, enc, "EncryptMetadata");
    struct pdf_crypt *c;
    struct lock l;
    unsigned char pw[32];
    int ok = 0;
    if (!pdf_name_is(pdf_get(d, enc, "Filter"), "Standard") || !(c = calloc(1, sizeof *c)))
        return NULL;
    l.r = pdf_int(pdf_get(d, enc, "R"), 0);
    l.o = (const unsigned char *)pdf_string(pdf_get(d, enc, "O"), &on), l.u = (const unsigned char *)pdf_string(pdf_get(d, enc, "U"), &un);
    l.oe = (const unsigned char *)pdf_string(pdf_get(d, enc, "OE"), &oen);
    l.ue = (const unsigned char *)pdf_string(pdf_get(d, enc, "UE"), &uen);
    l.id = id, l.idn = idn;
    l.p[0] = (unsigned char)p, l.p[1] = (unsigned char)(p >> 8), l.p[2] = (unsigned char)(p >> 16), l.p[3] = (unsigned char)(p >> 24);
    c->meta = !(pdf_is(meta, PDF_BOOL) && meta->u.num == 0);
    if (v == 1 || v == 2) {
        c->strings = c->streams = RC4;
        c->nkey = v == 1 ? 5 : bits >= 40 && bits <= 128 ? (unsigned)(bits / 8) : 0;
    } else if (v == 4 || v == 5) {
        c->strings = method(d, enc, "StrF"), c->streams = method(d, enc, "StmF");
        /* (AES of 128 bits has a key of 16 bytes whatever Length says, and it often says nothing) */
        c->nkey = v == 5 ? 32 : c->strings == AES128 || c->streams == AES128 ? 16 : bits >= 40 && bits <= 128 ? (unsigned)(bits / 8) : 16;
    }
    if (!c->nkey || c->strings < 0 || c->streams < 0)
        goto no;
    if (l.r >= 2 && l.r <= 4 && v != 5 && on >= 32 && un >= 32) {
        if (l.r == 2)
            c->nkey = 5;
        if (c->nkey < 5 || c->nkey > 16)
            goto no;
        /* the password as these kinds take it: its first 32 bytes in the format's own characters (Latin-1's, near
           enough: what UTF-8 has beyond them is left out), padded with the fixed string */
        for (i = 0; i < npw && n < 32; ) {
            unsigned long ch = (unsigned char)password[i++];
            int more = ch >= 0xF0 ? 3 : ch >= 0xE0 ? 2 : ch >= 0xC0 ? 1 : 0;
            ch &= more == 3 ? 7 : more == 2 ? 15 : more == 1 ? 31 : 255;
            for (; more && i < npw && ((unsigned char)password[i] & 0xC0) == 0x80; more--)
                ch = ch << 6 | ((unsigned char)password[i++] & 63);
            if (ch < 256)
                pw[n++] = (unsigned char)ch;
        }
        memcpy(pw + n, padding, 32 - n);
        ok = user_key(c, &l, pw) || owner_key(c, &l, pw);
    } else if ((l.r == 5 || l.r == 6) && v == 5 && un >= 48 && uen >= 32) {
        npw = npw > 127 ? 127 : npw;            /* (UTF-8, as it is typed here; 127 bytes of it) */
        ok = key56(c, &l, (const unsigned char *)(password ? password : ""), npw, 0)
             || (on >= 48 && oen >= 32 && key56(c, &l, (const unsigned char *)(password ? password : ""), npw, 1));
        c->nkey = 32;
    }
    if (ok)
        return c;
no:
    free(c);
    return NULL;
}

void pdf_crypt_close(struct pdf_crypt *c) { free(c); }
int pdf_crypt_meta(const struct pdf_crypt *c) { return c->meta; }

static unsigned long undo(const struct pdf_crypt *c, int how, unsigned num, unsigned gen, unsigned char *b, unsigned long n)
{
    unsigned char key[32], iv[16];
    unsigned nk = 32;
    br_aes_big_cbcdec_keys aes;
    if (how == NONE || !n)
        return n;
    if (how == AES256) {
        memcpy(key, c->key, 32);
    } else {                                    /* the object's own key */
        unsigned char tail[5] = { (unsigned char)num, (unsigned char)(num >> 8), (unsigned char)(num >> 16), (unsigned char)gen,
                                  (unsigned char)(gen >> 8) };
        br_md5_context md;
        br_md5_init(&md);
        br_md5_update(&md, c->key, c->nkey);
        br_md5_update(&md, tail, 5);
        if (how == AES128)
            br_md5_update(&md, "sAlT", 4);
        br_md5_out(&md, key);
        nk = c->nkey + 5 > 16 ? 16 : c->nkey + 5;
    }
    if (how == RC4) {
        rc4(key, nk, b, n);
        return n;
    }
    n &= ~15ul;                                 /* (whole blocks: the chain's start, and one at least after it) */
    if (n < 32)
        return 0;
    memcpy(iv, b, 16);
    br_aes_big_cbcdec_init(&aes, key, nk);
    br_aes_big_cbcdec_run(&aes, iv, b + 16, n - 16);
    memmove(b, b + 16, n - 16);
    n -= 16;
    if (b[n - 1] >= 1 && b[n - 1] <= 16)        /* the padding: so many bytes, each saying so */
        n -= b[n - 1];
    return n;
}

unsigned long pdf_crypt_string(const struct pdf_crypt *c, unsigned num, unsigned gen, unsigned char *b, unsigned long n)
{
    return undo(c, c->strings, num, gen, b, n);
}

unsigned long pdf_crypt_stream(const struct pdf_crypt *c, unsigned num, unsigned gen, unsigned char *b, unsigned long n)
{
    return undo(c, c->streams, num, gen, b, n);
}
