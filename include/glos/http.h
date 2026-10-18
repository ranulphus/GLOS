/* HTTP/1.1 GETs for GLOS apps (M10 step C; sdk/http, DJGPP), over TLS
 * (glos/tls.h) for https: URLs. Redirects followed (5 at most, https to http
 * refused), bodies by length, chunked or to the close. One connection a
 * request ("Connection: close"); no compression asked for. */
#ifndef GLOS_HTTP_H
#define GLOS_HTTP_H

#ifdef __cplusplus
extern "C" {
#endif

struct tls_anchors;

struct http_get {
    /* in */
    const char *url;                    /* http://HOST[:PORT]/PATH or https://... */
    const struct tls_anchors *anchors;  /* for https */
    const char *resolve;                /* "NAME:ADDRESS": that name taken to be at that address (curl's --resolve:
                                           a test server with a name its certificate has), or 0 */
    unsigned timeout_ms;                /* for each wait on the network (0: 30 s) */
    int (*body)(void *arg, const void *data, unsigned n);       /* the body as it comes: 0 to go on */
    void (*progress)(void *arg, unsigned long got, unsigned long total);  /* (total 0: not known) */
    void *arg;
    /* out */
    int status;                         /* the last response's (200 ...) */
    unsigned long length;               /* the body's bytes */
    char url_got[512];                  /* the URL the body came from (after redirects) */
    char tls[64];                       /* tls_version()'s, for https */
    char why[128];                      /* why it failed */
};

int http_get(struct http_get *g);       /* 0: a 2xx response's body given; -1 and why ("HTTP 404" for another status) */

#ifdef __cplusplus
}
#endif
#endif
