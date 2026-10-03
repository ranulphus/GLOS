/* Known-answer tests for the SSH crypto from TinySSH (third_party/tinyssh;
 * M3 item 5): SHA-256 and SHA-512 (FIPS 180-4, "abc"), X25519 (RFC 7748
 * section 5.2 and 6.1), Ed25519 (RFC 8032 section 7.1, tests 1-3),
 * ChaCha20 (RFC 8439 section 2.4.2) and Poly1305 (RFC 8439 section 2.5.2).
 * The vectors were taken from the RFC texts by script, not typed.
 * TinySSH signs Ed25519 "hedged" (32 random bytes go into the nonce), so
 * its signatures differ from RFC 8032's deterministic ones: the RFC's are
 * verified instead, and GLOS's own must verify too. */
#include <stdio.h>
#include <string.h>
#include "crypto_dh_x25519.h"
#include "crypto_hash_sha256.h"
#include "crypto_hash_sha512.h"
#include "crypto_onetimeauth_poly1305.h"
#include "crypto_scalarmult_curve25519.h"
#include "crypto_sign_ed25519.h"
#include "crypto_stream_chacha20.h"
#include "randombytes.h"

/* Random bytes: what the test sets next (a keypair's seed), else 5Ah. */
static const unsigned char *next_random;
void randombytes(void *p, long long n)
{
    if (next_random) {
        memcpy(p, next_random, (size_t)n);
        next_random = NULL;
    } else {
        memset(p, 0x5A, (size_t)n);
    }
}
const char *randombytes_source(void) { return "test"; }

static int fails;
#define CHECK(c, what) do { if (!(c)) { printf("FAIL %s\n", what); fails++; } } while (0)

static unsigned unhex(unsigned char *out, const char *h)
{
    unsigned n = 0;
    for (; h[0] && h[1]; h += 2) {
        unsigned v;
        sscanf(h, "%2x", &v);
        out[n++] = (unsigned char)v;
    }
    return n;
}

static int same(const unsigned char *a, const char *hex)
{
    unsigned char b[256];
    unsigned n = unhex(b, hex);
    return memcmp(a, b, n) == 0;
}

static const char sha256_abc[] = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
static const char sha512_abc[] = "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f";
static const char x0_scalar[] = "a546e36bf0527c9d3b16154b82465edd62144c0ac1fc5a18506a2244ba449ac4";
static const char x0_u[] = "e6db6867583030db3594c1a424b15f7c726624ec26b3353b10a903a6d0ab1c4c";
static const char x0_out[] = "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552";
static const char x1_scalar[] = "4b66e9d4d1b4673c5ad22691957d6af5c11b6421e0ea01d42ca4169e7918ba0d";
static const char x1_u[] = "e5210f12786811d3f4b7959d0538ae2c31dbe7106fc03c3efc4cd549c715a493";
static const char x1_out[] = "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957";
static const char dh_a[] = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
static const char dh_A[] = "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a";
static const char dh_b[] = "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";
static const char dh_B[] = "de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f";
static const char dh_K[] = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";
static const char ed1_seed[] = "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60";
static const char ed1_pk[] = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
static const char ed1_msg[] = "";
static const char ed1_sig[] = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";
static const char ed2_seed[] = "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb";
static const char ed2_pk[] = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c";
static const char ed2_msg[] = "72";
static const char ed2_sig[] = "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00";
static const char ed3_seed[] = "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7";
static const char ed3_pk[] = "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025";
static const char ed3_msg[] = "af82";
static const char ed3_sig[] = "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a";
static const char cc_pt[] = "4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a204966204920636f756c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73637265656e20776f756c642062652069742e";
static const char cc_ct[] = "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab77937365af90bbf74a35be6b40b8eedf2785e42874d";
static const char poly_key[] = "85d6be7857556d337f4452fe42d506a80103808afb0db2fd4abff6af4149f51b";
static const char poly_msg[] = "43727970746f6772617068696320466f72756d2052657365617263682047726f7570";
static const char poly_tag[] = "a8061dc1305136c6c22b8baf0c0127a9";

int main(void)
{
    unsigned char a[1024], b[1024], k[64], s[64], buf[1024];
    unsigned long long n;
    unsigned i;

    crypto_hash_sha256(a, (const unsigned char *)"abc", 3);
    CHECK(same(a, sha256_abc), "sha256 abc");
    crypto_hash_sha512(a, (const unsigned char *)"abc", 3);
    CHECK(same(a, sha512_abc), "sha512 abc");

    unhex(s, x0_scalar); unhex(b, x0_u);
    crypto_scalarmult_curve25519(a, s, b);
    CHECK(same(a, x0_out), "x25519 rfc7748 5.2 #1");
    unhex(s, x1_scalar); unhex(b, x1_u);
    crypto_scalarmult_curve25519(a, s, b);
    CHECK(same(a, x1_out), "x25519 rfc7748 5.2 #2");
    unhex(s, dh_a);
    crypto_scalarmult_curve25519_base(a, s);
    CHECK(same(a, dh_A), "x25519 alice public");
    unhex(k, dh_b);
    crypto_scalarmult_curve25519_base(b, k);
    CHECK(same(b, dh_B), "x25519 bob public");
    crypto_scalarmult_curve25519(buf, s, b);
    CHECK(same(buf, dh_K), "x25519 shared (alice)");
    crypto_scalarmult_curve25519(buf, k, a);
    CHECK(same(buf, dh_K), "x25519 shared (bob)");
    crypto_dh_x25519(buf, b, s);
    CHECK(same(buf, dh_K), "crypto_dh_x25519");

    {
        const char *seeds[] = { ed1_seed, ed2_seed, ed3_seed }, *pks[] = { ed1_pk, ed2_pk, ed3_pk };
        const char *msgs[] = { ed1_msg, ed2_msg, ed3_msg }, *sigs[] = { ed1_sig, ed2_sig, ed3_sig };
        for (i = 0; i < 3; i++) {
            unsigned char seed[32], sk[64], pk[32], m[16], sm[128], out[128];
            unsigned long long mlen = unhex(m, msgs[i]), smlen, outlen;
            char what[64];
            unhex(seed, seeds[i]);
            next_random = seed;                         /* the keypair from the RFC's secret key */
            crypto_sign_ed25519_keypair(pk, sk);
            sprintf(what, "ed25519 rfc8032 test %u public key", i + 1);
            CHECK(same(pk, pks[i]), what);
            unhex(sm, sigs[i]);                         /* the RFC's signature verifies */
            memcpy(sm + 64, m, mlen);
            sprintf(what, "ed25519 rfc8032 test %u verify", i + 1);
            CHECK(crypto_sign_ed25519_open(out, &outlen, sm, 64 + mlen, pk) == 0 && outlen == mlen, what);
            crypto_sign_ed25519(sm, &smlen, m, mlen, sk);   /* and so does ours */
            sprintf(what, "ed25519 rfc8032 test %u own signature", i + 1);
            CHECK(smlen == 64 + mlen && crypto_sign_ed25519_open(out, &outlen, sm, smlen, pk) == 0, what);
            sm[5] ^= 1;
            sprintf(what, "ed25519 rfc8032 test %u forged", i + 1);
            CHECK(crypto_sign_ed25519_open(out, &outlen, sm, smlen, pk) != 0, what);
        }
    }

    /* RFC 8439's nonce is 00..00 4a 00..00 with counter 1; TinySSH's ChaCha20
       (the SSH variant: 64-bit counter from 0, 64-bit nonce) gives the same
       block from its second 64 bytes with nonce 00 00 00 4a 00 00 00 00. */
    {
        static const unsigned char key[32] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
                                               16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
        static const unsigned char nonce[8] = { 0, 0, 0, 0x4a, 0, 0, 0, 0 };
        unsigned len;
        memset(buf, 0, 64);
        len = unhex(buf + 64, cc_pt);
        crypto_stream_chacha20_xor(a, buf, 64 + len, nonce, key);
        CHECK(same(a + 64, cc_ct), "chacha20 rfc8439 2.4.2");
    }

    unhex(k, poly_key);
    n = unhex(buf, poly_msg);
    crypto_onetimeauth_poly1305(a, buf, n, k);
    CHECK(same(a, poly_tag), "poly1305 rfc8439 2.5.2");
    CHECK(crypto_onetimeauth_poly1305_verify(a, buf, n, k) == 0, "poly1305 verify");
    buf[0] ^= 1;
    CHECK(crypto_onetimeauth_poly1305_verify(a, buf, n, k) != 0, "poly1305 forged");

    printf("crypto_kat: %d failures\n", fails);
    return fails != 0;
}
