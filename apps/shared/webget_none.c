/* A page fetched (webget.h) where there is no network: the host's stand-in for the toolkit's tests, in which
   the Web Reader reads pages from files. Every fetch fails at once. */
#include <stdlib.h>

#include "webget.h"

struct webget {
    int unused;
};

struct webget *webget_start(const char *url, const char *certs, unsigned long limit)
{
    (void)url, (void)certs, (void)limit;
    return calloc(1, sizeof(struct webget));
}

struct webget *webget_post(const char *url, const char *certs, unsigned long limit, const char *body, unsigned long len)
{
    (void)url, (void)certs, (void)limit, (void)body, (void)len;
    return calloc(1, sizeof(struct webget));
}

int webget_state(struct webget *g, unsigned long *got, unsigned long *total)
{
    (void)g;
    if (got)
        *got = 0;
    if (total)
        *total = 0;
    return -1;
}

const char *webget_bytes(struct webget *g, unsigned long *len) { (void)g; *len = 0; return ""; }
const char *webget_type(struct webget *g) { (void)g; return ""; }
const char *webget_url(struct webget *g) { (void)g; return ""; }
const char *webget_why(struct webget *g) { (void)g; return "no network"; }
void webget_free(struct webget *g) { free(g); }
