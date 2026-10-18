/* TLS 1.2 client connections (include/glos/tls.h; M10 step C): BearSSL's
 * engine run over a GLOS_FN_NET connection. The engine's loop is the one its
 * br_sslio has, here so that the handshake can be finished in tls_open() and
 * its failure told there; reads and writes wait `timeout_ms` for the network
 * at most. The certificate is checked against GLOS_FN_TIME's clock, and not
 * at all before that clock has been set. The engine is seeded with 32 bytes
 * of GLOS_FN_RANDOM. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bearssl.h"
#include <glos/api.h>
#include <glos/tls.h>

struct tls {
    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    unsigned char buf[BR_SSL_BUFSIZE_BIDI];
    int conn;
    unsigned timeout_ms;
    int lost;                           /* the network failed (or timed out): the engine isn't run again */
    int timed_out;
    int eof;                            /* the far end closed TCP (with or without close_notify) */
    char why[64];
};

/* ---- the network under it */

static int low_read(struct tls *t, unsigned char *buf, size_t len)
{
    unsigned st;
    int r = glos_net_recv((unsigned)t->conn, buf, (unsigned)len, t->timeout_ms, &st);
    if (r > 0)
        return r;
    if (r == 0 && st == GLOS_NETS_CLOSED) {
        t->eof = 1;
        return -1;
    }
    if (r == 0)
        t->timed_out = 1;
    t->lost = 1;
    return -1;
}

static int low_write(struct tls *t, const unsigned char *buf, size_t len)
{
    int r = glos_net_send((unsigned)t->conn, buf, (unsigned)len, t->timeout_ms);
    if (r > 0)
        return r;
    if (r == 0)
        t->timed_out = 1;
    t->lost = 1;
    return -1;
}

/* The engine run until it can do `target` (BR_SSL_SENDAPP, BR_SSL_RECVAPP or both): 0, or -1. */
static int run_until(struct tls *t, unsigned target)
{
    br_ssl_engine_context *e = &t->sc.eng;
    for (;;) {
        unsigned st = br_ssl_engine_current_state(e);
        unsigned char *buf;
        size_t len;
        int k;
        if ((st & BR_SSL_CLOSED) || t->lost || t->eof)
            return -1;
        if (st & BR_SSL_SENDREC) {              /* (records to send go first) */
            buf = br_ssl_engine_sendrec_buf(e, &len);
            if ((k = low_write(t, buf, len)) < 0)
                return -1;
            br_ssl_engine_sendrec_ack(e, (size_t)k);
            continue;
        }
        if (st & target)
            return 0;
        if (st & BR_SSL_RECVAPP)                /* (writing, with data to be read first: not here) */
            return -1;
        if (st & BR_SSL_RECVREC) {
            buf = br_ssl_engine_recvrec_buf(e, &len);
            if ((k = low_read(t, buf, len)) < 0)
                return -1;
            br_ssl_engine_recvrec_ack(e, (size_t)k);
            continue;
        }
        br_ssl_engine_flush(e, 0);
    }
}

static void reason(struct tls *t)
{
    int e = br_ssl_engine_last_error(&t->sc.eng);
    const char *w;
    switch (e) {
    case BR_ERR_OK:                             /* (the engine's fine: the network under it isn't) */
        w = t->timed_out ? "timed out" : t->eof ? "closed" : "connection lost";
        break;
    case BR_ERR_X509_NOT_TRUSTED:
        w = "not trusted";
        break;
    case BR_ERR_X509_EXPIRED:
        w = "expired";
        break;
    case BR_ERR_X509_BAD_SERVER_NAME:
        w = "wrong name";
        break;
    case BR_ERR_IO:
        w = t->timed_out ? "timed out" : t->eof ? "closed during the handshake" : "connection lost";
        break;
    default:
        snprintf(t->why, sizeof t->why, "TLS error %d", e);
        return;
    }
    snprintf(t->why, sizeof t->why, "%s", w);
}

/* ---- the connection */

struct tls *tls_open(int conn, const char *host, const struct tls_anchors *a, unsigned timeout_ms, char *why,
                     size_t room)
{
    struct tls *t;
    unsigned char seed[32];
    unsigned long utc;
    int how = glos_time(&utc, 0);
    if (how < 0 || how == GLOS_TIME_RTC) {
        snprintf(why, room, "clock not set");
        glos_net_close((unsigned)conn, 1);
        return 0;
    }
    if (!(t = calloc(1, sizeof *t)) || glos_random(seed, sizeof seed) != 0) {
        free(t);
        snprintf(why, room, "%s", t ? "no randomness" : "out of memory");
        glos_net_close((unsigned)conn, 1);
        return 0;
    }
    t->conn = conn;
    t->timeout_ms = timeout_ms;
    br_ssl_client_init_full(&t->sc, &t->xc, a->ta, a->n);
    br_x509_minimal_set_time(&t->xc, (unsigned)(utc / 86400) + 719528u, (unsigned)(utc % 86400));
    br_ssl_engine_set_buffer(&t->sc.eng, t->buf, sizeof t->buf, 1);
    br_ssl_engine_inject_entropy(&t->sc.eng, seed, sizeof seed);
    memset(seed, 0, sizeof seed);
    if (!br_ssl_client_reset(&t->sc, host, 0) || run_until(t, BR_SSL_SENDAPP) != 0) {
        reason(t);
        snprintf(why, room, "%s", t->why);
        glos_net_close((unsigned)conn, 1);
        free(t);
        return 0;
    }
    return t;
}

int tls_read(struct tls *t, void *buf, size_t n)
{
    unsigned char *p;
    size_t len;
    if (!n)
        return 0;
    if (run_until(t, BR_SSL_RECVAPP) != 0) {
        /* The end: a close_notify, or TCP closed without one (the caller's framing, a length or chunks, is what says
           whether all came) */
        if (br_ssl_engine_last_error(&t->sc.eng) == BR_ERR_OK || (t->eof && !t->lost))
            return 0;
        reason(t);
        return -1;
    }
    p = br_ssl_engine_recvapp_buf(&t->sc.eng, &len);
    if (len > n)
        len = n;
    memcpy(buf, p, len);
    br_ssl_engine_recvapp_ack(&t->sc.eng, len);
    return (int)len;
}

int tls_write(struct tls *t, const void *buf, size_t n)
{
    const unsigned char *s = buf;
    size_t done = 0;
    while (done < n) {
        unsigned char *p;
        size_t len;
        if (run_until(t, BR_SSL_SENDAPP) != 0) {
            reason(t);
            return -1;
        }
        p = br_ssl_engine_sendapp_buf(&t->sc.eng, &len);
        if (len > n - done)
            len = n - done;
        memcpy(p, s + done, len);
        br_ssl_engine_sendapp_ack(&t->sc.eng, len);
        done += len;
    }
    br_ssl_engine_flush(&t->sc.eng, 0);
    if (run_until(t, BR_SSL_SENDAPP | BR_SSL_RECVAPP) != 0 && br_ssl_engine_current_state(&t->sc.eng) & BR_SSL_CLOSED) {
        reason(t);
        return -1;
    }
    return (int)n;
}

const char *tls_why(const struct tls *t) { return t->why; }

const char *tls_version(const struct tls *t)
{
    static char s[64];
    br_ssl_session_parameters sp;
    unsigned v = br_ssl_engine_get_version(&t->sc.eng);
    br_ssl_engine_get_session_parameters(&t->sc.eng, &sp);
    snprintf(s, sizeof s, "TLS1.%u suite=%04x", v >= 0x0301 ? v - 0x0301 : 0, sp.cipher_suite);  /* (0303h: 1.2) */
    return s;
}

void tls_close(struct tls *t)
{
    if (!t)
        return;
    if (!t->lost && !(br_ssl_engine_current_state(&t->sc.eng) & BR_SSL_CLOSED)) {
        unsigned saved = t->timeout_ms;
        br_ssl_engine_close(&t->sc.eng);
        t->timeout_ms = 2000;                   /* (the close_notify sent; the answer not waited for long) */
        run_until(t, 0);
        t->timeout_ms = saved;
    }
    glos_net_close((unsigned)t->conn, 0);
    free(t);
}
