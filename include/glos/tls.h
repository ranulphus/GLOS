/* TLS for GLOS apps (M10 step C; sdk/tls, DJGPP): BearSSL 0.6's TLS 1.2
 * client over a GLOS_FN_NET connection, checking the server's certificate
 * against trust anchors (Mozilla's roots, as curl ships them, made into
 * C:\GLOS\CERTS.DAT on the build host by tools/certs).
 *
 * A certificate is checked against GLOS's clock only when the clock has been
 * set (SNTP or by hand: GLOS_FN_TIME); before that, tls_open() refuses with
 * "clock not set". Refusals have a reason in a word or two: "not trusted",
 * "expired", "wrong name", "clock not set", "timed out", "connection lost",
 * or "TLS error N" (BearSSL's code). */
#ifndef GLOS_TLS_H
#define GLOS_TLS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TLS_CERTS "C:\\GLOS\\CERTS.DAT"        /* the trust anchors (GLOSCERTS in the environment for others) */

struct tls_anchors {
    void *ta;                           /* br_x509_trust_anchor[n] */
    size_t n;
};

/* CERTS.DAT: "GLOSCRT1", the count (4 bytes), then each anchor: its flags (1: a CA), its key's type (BearSSL's
   BR_KEYTYPE_), its subject's DN (2 bytes of length, then DER), then RSA's n and e or EC's curve (1 byte) and point,
   each with 2 bytes of length. Little-endian. */
int tls_anchors_load(struct tls_anchors *a, const char *path);         /* 0, or -1 (errno) */
int tls_anchors_from_pem(struct tls_anchors *a, const char *pem, size_t len);  /* adds the certificates found:
                                           their count, or -1 */
int tls_anchors_save(const struct tls_anchors *a, const char *path);   /* 0, or -1 */
void tls_anchors_free(struct tls_anchors *a);

struct tls;
struct tls *tls_open(int conn, const char *host, const struct tls_anchors *a, unsigned timeout_ms, char *why,
                     size_t room);      /* the handshake done over GLOS connection `conn` (it is the TLS's then),
                                           host the name checked; 0 and why */
int tls_read(struct tls *t, void *buf, size_t n);     /* bytes; 0 at the end; -1 (tls_why) */
int tls_write(struct tls *t, const void *buf, size_t n);  /* all of them: n, or -1 */
const char *tls_why(const struct tls *t);
void tls_close(struct tls *t);          /* close_notify sent, the connection closed, t freed */
const char *tls_version(const struct tls *t);   /* "TLS1.2 suite=C02F": the version and cipher suite */

#ifdef __cplusplus
}
#endif
#endif
