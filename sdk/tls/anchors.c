/* Trust anchors (include/glos/tls.h): CERTS.DAT read and written, and
 * certificates in PEM made into anchors with BearSSL's decoder. Built for
 * DJGPP (sdk/tls) and for the host (tools/certs/mkcerts.c, the host test). */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bearssl.h"
#include <glos/tls.h>

static const char MAGIC[8] = "GLOSCRT1";

static int grow(struct tls_anchors *a)
{
    br_x509_trust_anchor *t = realloc(a->ta, (a->n + 1) * sizeof *t);
    if (!t)
        return -1;
    a->ta = t;
    memset(&t[a->n], 0, sizeof *t);
    return 0;
}

static unsigned char *dup(const void *p, size_t n)
{
    unsigned char *q = malloc(n ? n : 1);
    if (q)
        memcpy(q, p, n);
    return q;
}

void tls_anchors_free(struct tls_anchors *a)
{
    br_x509_trust_anchor *t = a->ta;
    size_t i;
    for (i = 0; i < a->n; i++) {
        free(t[i].dn.data);
        if (t[i].pkey.key_type == BR_KEYTYPE_RSA) {
            free(t[i].pkey.key.rsa.n);
            free(t[i].pkey.key.rsa.e);
        } else {
            free(t[i].pkey.key.ec.q);
        }
    }
    free(a->ta);
    a->ta = 0;
    a->n = 0;
}

/* ---- PEM */

struct buf {
    unsigned char *p;
    size_t n, room;
};

static void append(void *ctx, const void *data, size_t len)
{
    struct buf *b = ctx;
    if (!b->p && b->room)
        return;                                 /* (out of memory before: given up) */
    if (b->n + len > b->room) {
        size_t room = (b->n + len) * 2 + 256;
        unsigned char *p = realloc(b->p, room);
        if (!p) {
            free(b->p);
            b->p = 0;
            b->room = 1;
            return;
        }
        b->p = p;
        b->room = room;
    }
    memcpy(b->p + b->n, data, len);
    b->n += len;
}

/* One certificate's DER made into an anchor at a->ta[a->n]: 0, or -1. */
static int anchor(struct tls_anchors *a, const unsigned char *der, size_t len)
{
    br_x509_decoder_context dc;
    struct buf dn = { 0, 0, 0 };
    br_x509_pkey *pk;
    br_x509_trust_anchor *t;
    br_x509_decoder_init(&dc, append, &dn);
    br_x509_decoder_push(&dc, der, len);
    pk = br_x509_decoder_get_pkey(&dc);
    if (!pk || !dn.p || grow(a) != 0) {
        free(dn.p);
        return -1;
    }
    t = &((br_x509_trust_anchor *)a->ta)[a->n];
    t->dn.data = dn.p;
    t->dn.len = dn.n;
    t->flags = br_x509_decoder_isCA(&dc) ? BR_X509_TA_CA : 0;
    t->pkey.key_type = pk->key_type;
    if (pk->key_type == BR_KEYTYPE_RSA) {
        t->pkey.key.rsa.n = dup(pk->key.rsa.n, pk->key.rsa.nlen);
        t->pkey.key.rsa.nlen = pk->key.rsa.nlen;
        t->pkey.key.rsa.e = dup(pk->key.rsa.e, pk->key.rsa.elen);
        t->pkey.key.rsa.elen = pk->key.rsa.elen;
    } else if (pk->key_type == BR_KEYTYPE_EC) {
        t->pkey.key.ec.curve = pk->key.ec.curve;
        t->pkey.key.ec.q = dup(pk->key.ec.q, pk->key.ec.qlen);
        t->pkey.key.ec.qlen = pk->key.ec.qlen;
    } else {
        free(dn.p);
        return -1;
    }
    a->n++;
    return 0;
}

int tls_anchors_from_pem(struct tls_anchors *a, const char *pem, size_t len)
{
    br_pem_decoder_context pc;
    struct buf der = { 0, 0, 0 };
    int in_cert = 0, count = 0;
    br_pem_decoder_init(&pc);
    while (len > 0) {
        size_t k = br_pem_decoder_push(&pc, pem, len);
        pem += k;
        len -= k;
        switch (br_pem_decoder_event(&pc)) {
        case BR_PEM_BEGIN_OBJ:
            in_cert = !strcmp(br_pem_decoder_name(&pc), "CERTIFICATE")
                      || !strcmp(br_pem_decoder_name(&pc), "X509 CERTIFICATE");
            der.n = 0;
            br_pem_decoder_setdest(&pc, in_cert ? append : 0, &der);
            break;
        case BR_PEM_END_OBJ:
            if (in_cert && der.p && anchor(a, der.p, der.n) == 0)
                count++;
            in_cert = 0;
            break;
        case BR_PEM_ERROR:
            free(der.p);
            return -1;
        }
    }
    free(der.p);
    return count;
}

/* ---- CERTS.DAT */

static int put16(FILE *f, size_t v) { return putc((int)(v & 255), f) == EOF || putc((int)(v >> 8), f) == EOF ? -1 : 0; }

static int put_bytes(FILE *f, const unsigned char *p, size_t n)
{
    return n > 65535 || put16(f, n) != 0 || fwrite(p, 1, n, f) != n ? -1 : 0;
}

int tls_anchors_save(const struct tls_anchors *a, const char *path)
{
    const br_x509_trust_anchor *t = a->ta;
    FILE *f = fopen(path, "wb");
    size_t i;
    int bad = 0;
    unsigned char n[4] = { (unsigned char)a->n, (unsigned char)(a->n >> 8), (unsigned char)(a->n >> 16),
                           (unsigned char)(a->n >> 24) };
    if (!f)
        return -1;
    bad |= fwrite(MAGIC, 1, 8, f) != 8 || fwrite(n, 1, 4, f) != 4;
    for (i = 0; i < a->n && !bad; i++) {
        bad |= putc(t[i].flags & BR_X509_TA_CA ? 1 : 0, f) == EOF || putc(t[i].pkey.key_type, f) == EOF;
        bad |= put_bytes(f, t[i].dn.data, t[i].dn.len);
        if (t[i].pkey.key_type == BR_KEYTYPE_RSA) {
            bad |= put_bytes(f, t[i].pkey.key.rsa.n, t[i].pkey.key.rsa.nlen);
            bad |= put_bytes(f, t[i].pkey.key.rsa.e, t[i].pkey.key.rsa.elen);
        } else {
            bad |= putc(t[i].pkey.key.ec.curve, f) == EOF;
            bad |= put_bytes(f, t[i].pkey.key.ec.q, t[i].pkey.key.ec.qlen);
        }
    }
    bad |= fclose(f) != 0;
    return bad ? -1 : 0;
}

struct rd {
    const unsigned char *p;
    size_t n;
    int bad;
};

static unsigned get8(struct rd *r)
{
    if (r->n < 1) {
        r->bad = 1;
        return 0;
    }
    r->n--;
    return *r->p++;
}

static unsigned char *get_bytes(struct rd *r, size_t *len)
{
    size_t n = get8(r);
    n |= (size_t)get8(r) << 8;
    if (r->bad || r->n < n) {
        r->bad = 1;
        return 0;
    }
    *len = n;
    r->p += n;
    r->n -= n;
    return dup(r->p - n, n);
}

int tls_anchors_load(struct tls_anchors *a, const char *path)
{
    FILE *f = fopen(path, "rb");
    unsigned char *data;
    long size;
    struct rd r;
    unsigned long count, i;
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 12 || fseek(f, 0, SEEK_SET) != 0
        || !(data = malloc((size_t)size))) {
        fclose(f);
        errno = EINVAL;
        return -1;
    }
    if (fread(data, 1, (size_t)size, f) != (size_t)size || memcmp(data, MAGIC, 8)) {
        free(data);
        fclose(f);
        errno = EINVAL;
        return -1;
    }
    fclose(f);
    r.p = data + 12;
    r.n = (size_t)size - 12;
    r.bad = 0;
    count = data[8] | (unsigned long)data[9] << 8 | (unsigned long)data[10] << 16 | (unsigned long)data[11] << 24;
    for (i = 0; i < count && !r.bad; i++) {
        br_x509_trust_anchor *t;
        if (grow(a) != 0)
            break;
        t = &((br_x509_trust_anchor *)a->ta)[a->n];
        t->flags = get8(&r) ? BR_X509_TA_CA : 0;
        t->pkey.key_type = (unsigned char)get8(&r);
        t->dn.data = get_bytes(&r, &t->dn.len);
        if (t->pkey.key_type == BR_KEYTYPE_RSA) {
            t->pkey.key.rsa.n = get_bytes(&r, &t->pkey.key.rsa.nlen);
            t->pkey.key.rsa.e = get_bytes(&r, &t->pkey.key.rsa.elen);
        } else if (t->pkey.key_type == BR_KEYTYPE_EC) {
            t->pkey.key.ec.curve = (int)get8(&r);
            t->pkey.key.ec.q = get_bytes(&r, &t->pkey.key.ec.qlen);
        } else {
            r.bad = 1;
        }
        a->n++;                                 /* (freed with the rest if it was bad) */
    }
    free(data);
    if (r.bad || i != count) {
        tls_anchors_free(a);
        errno = EINVAL;
        return -1;
    }
    return 0;
}
