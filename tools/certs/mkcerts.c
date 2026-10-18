/* mkcerts (M10 step C): CERTS.DAT, GLOS's trust anchors, from certificates in
 * PEM (curl's cacert.pem: Mozilla's roots; a test CA added for Loop A), with
 * the decoder GLOS's TLS uses (sdk/tls/anchors.c, BearSSL). Then the file is
 * read back and compared with what was written.
 *
 *   mkcerts OUT.DAT FILE.PEM...
 *
 * Prints "mkcerts: N anchors (R RSA, E EC) in OUT.DAT". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bearssl.h"
#include <glos/tls.h>

static char *slurp(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    char *p;
    long size;
    if (!f || fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0
        || !(p = malloc((size_t)size + 1)) || fread(p, 1, (size_t)size, f) != (size_t)size) {
        if (f)
            fclose(f);
        return 0;
    }
    fclose(f);
    *n = (size_t)size;
    return p;
}

static int same(const br_x509_trust_anchor *a, const br_x509_trust_anchor *b)
{
    if (a->flags != b->flags || a->pkey.key_type != b->pkey.key_type || a->dn.len != b->dn.len
        || memcmp(a->dn.data, b->dn.data, a->dn.len))
        return 0;
    if (a->pkey.key_type == BR_KEYTYPE_RSA)
        return a->pkey.key.rsa.nlen == b->pkey.key.rsa.nlen && a->pkey.key.rsa.elen == b->pkey.key.rsa.elen
               && !memcmp(a->pkey.key.rsa.n, b->pkey.key.rsa.n, a->pkey.key.rsa.nlen)
               && !memcmp(a->pkey.key.rsa.e, b->pkey.key.rsa.e, a->pkey.key.rsa.elen);
    return a->pkey.key.ec.curve == b->pkey.key.ec.curve && a->pkey.key.ec.qlen == b->pkey.key.ec.qlen
           && !memcmp(a->pkey.key.ec.q, b->pkey.key.ec.q, a->pkey.key.ec.qlen);
}

int main(int argc, char **argv)
{
    struct tls_anchors a = { 0, 0 }, back = { 0, 0 };
    const br_x509_trust_anchor *t;
    size_t i, rsa = 0;
    int k;
    if (argc < 3) {
        fprintf(stderr, "usage: mkcerts OUT.DAT FILE.PEM...\n");
        return 2;
    }
    for (k = 2; k < argc; k++) {
        size_t n;
        char *pem = slurp(argv[k], &n);
        int got;
        if (!pem || (got = tls_anchors_from_pem(&a, pem, n)) <= 0) {
            fprintf(stderr, "mkcerts: no certificates read from %s\n", argv[k]);
            return 1;
        }
        free(pem);
    }
    if (tls_anchors_save(&a, argv[1]) != 0 || tls_anchors_load(&back, argv[1]) != 0 || back.n != a.n) {
        fprintf(stderr, "mkcerts: %s not written, or not read back\n", argv[1]);
        return 1;
    }
    t = a.ta;
    for (i = 0; i < a.n; i++) {
        if (!same(&t[i], &((const br_x509_trust_anchor *)back.ta)[i])) {
            fprintf(stderr, "mkcerts: anchor %zu read back differs\n", i);
            return 1;
        }
        rsa += t[i].pkey.key_type == BR_KEYTYPE_RSA;
    }
    printf("mkcerts: %zu anchors (%zu RSA, %zu EC) in %s\n", a.n, rsa, a.n - rsa, argv[1]);
    tls_anchors_free(&a);
    tls_anchors_free(&back);
    return 0;
}
