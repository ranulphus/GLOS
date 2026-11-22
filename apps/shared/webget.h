/* A page fetched for the Web Reader (phase F; docs/v1/apps/web-reader.md): on a thread of its own (a name looked
   up, TLS's handshake, the bytes: seconds on a 486), into memory, with the window's thread asking how far it is.
   apps/shared/webget.c is GLOS's (sdk/http, sdk/tls, THREAD); webget_none.c the host's, where there is no
   network. */
#ifndef WEBGET_H
#define WEBGET_H

struct webget;

/* An http: or https: URL begun to be fetched. certs: the trust anchors' file (CERTS.DAT's path); limit: the most
   bytes kept (a larger body is cut there). NULL: no memory, no thread. */
struct webget *webget_start(const char *url, const char *certs, unsigned long limit);
/* (phase F) The same by POST: a form's body (len bytes, a form's kind: name=value&...), copied. */
struct webget *webget_post(const char *url, const char *certs, unsigned long limit, const char *body, unsigned long len);

/* How it is: 0 on its way (*got bytes so far of *total, 0 if not known), 1 here, -1 failed (webget_why). */
int webget_state(struct webget *g, unsigned long *got, unsigned long *total);
const char *webget_bytes(struct webget *g, unsigned long *len);        /* once here: the body (cut at the limit) */
const char *webget_type(struct webget *g);      /* ... what it is, as its sender says ("text/html; charset=utf-8") */
const char *webget_url(struct webget *g);       /* ... the URL it came from, after redirects */
const char *webget_why(struct webget *g);       /* once failed: why, in a few words */
void webget_free(struct webget *g);             /* one on its way is given up (it ends by itself; nothing waits) */

#endif
