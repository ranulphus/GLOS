/* A page fetched (webget.h), on GLOS: a thread runs GLOS's HTTP client (sdk/http: redirects, TLS, bodies by
 * length, in chunks or to the close) and keeps what comes. A lock is round what the two threads share: how far
 * it is, and whether it is still wanted. One that is given up while on its way ends at the next bytes that
 * come (or its time out), and frees itself. (A program with this is linked with gloswrap.opt.) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/http.h>
#include <glos/tls.h>

#include "webget.h"

struct webget {
    char url[1024], certs[260];
    char *post;                         /* a POST's body (malloc'd), or 0 */
    unsigned long post_len;
    unsigned long limit;
    glos_handle thread, lock;
    int state;                          /* 0 on its way, 1 here, -1 failed */
    int unwanted;                       /* given up: the thread frees it */
    char *bytes;
    unsigned long n, room, got, total;
    char type[96], url_got[512], why[128];
};

static int body(void *arg, const void *data, unsigned n)
{
    struct webget *g = arg;
    int stop;
    glos_take(g->lock, 0xFFFFFFFFu);
    if (g->n + n > g->room && g->room < g->limit) {
        unsigned long room = g->room ? g->room * 2 : 16384;
        char *b;
        while (room < g->n + n && room < g->limit)
            room *= 2;
        room = room > g->limit ? g->limit : room;
        if ((b = realloc(g->bytes, room + 1)) != NULL)
            g->bytes = b, g->room = room;
    }
    if (g->n < g->room) {
        unsigned take = g->room - g->n < n ? (unsigned)(g->room - g->n) : n;
        memcpy(g->bytes + g->n, data, take);
        g->n += take;
    }
    g->got += n;
    stop = g->unwanted || g->n >= g->limit;     /* (given up, or as much as is kept: no more is asked for) */
    glos_give(g->lock);
    return stop;
}

static void progress(void *arg, unsigned long got, unsigned long total)
{
    struct webget *g = arg;
    (void)got;
    glos_take(g->lock, 0xFFFFFFFFu);
    g->total = total;
    glos_give(g->lock);
}

static void run(void *arg)
{
    struct webget *g = arg;
    struct tls_anchors anchors = { 0, 0 };
    struct http_get h;
    int r, have = 0, unwanted;
    memset(&h, 0, sizeof h);
    h.url = g->url, h.timeout_ms = 30000, h.body = body, h.progress = progress, h.arg = g;
    h.agent = "GLOS-Web-Reader/1.0", h.accept = "text/html, text/plain;q=0.9, image/*;q=0.8, */*;q=0.5";
    h.post = g->post, h.post_len = g->post_len;
    if (!strncmp(g->url, "https:", 6)) {
        have = tls_anchors_load(&anchors, g->certs) == 0;
        h.anchors = &anchors;
    }
    if (!strncmp(g->url, "https:", 6) && !have) {
        r = -1;
        snprintf(h.why, sizeof h.why, "no trust anchors (CERTS.DAT)");
    } else {
        r = http_get(&h);
    }
    if (have)
        tls_anchors_free(&anchors);
    glos_take(g->lock, 0xFFFFFFFFu);
    if (r != 0 && g->n >= g->limit && g->limit)        /* (cut at the limit: what came is the page) */
        r = 0;
    snprintf(g->type, sizeof g->type, "%s", h.type);
    snprintf(g->url_got, sizeof g->url_got, "%s", h.url_got[0] ? h.url_got : g->url);
    snprintf(g->why, sizeof g->why, "%s", h.why[0] ? h.why : "failed");
    if (g->bytes)
        g->bytes[g->n] = 0;
    g->state = r == 0 ? 1 : -1;
    unwanted = g->unwanted;
    glos_give(g->lock);
    if (unwanted) {                             /* (nobody waits for it) */
        glos_close(g->lock);
        free(g->bytes);
        free(g->post);
        free(g);
    }
}

static struct webget *start(const char *url, const char *certs, unsigned long limit, const char *post, unsigned long len)
{
    struct webget *g = calloc(1, sizeof *g);
    if (!g || strlen(url) >= sizeof g->url || (post && !(g->post = malloc(len + 1)))) {
        free(g);
        return NULL;
    }
    if (post)
        memcpy(g->post, post, len), g->post[len] = 0, g->post_len = len;
    strcpy(g->url, url);
    snprintf(g->certs, sizeof g->certs, "%s", certs ? certs : "");
    g->limit = limit;
    if (!(g->lock = glos_lock()) || !(g->thread = glos_thread(run, g, 128 * 1024, 0))) {
        if (g->lock)
            glos_close(g->lock);
        free(g->post);
        free(g);
        return NULL;
    }
    glos_detach(g->thread);
    return g;
}

struct webget *webget_start(const char *url, const char *certs, unsigned long limit)
{
    return start(url, certs, limit, NULL, 0);
}

struct webget *webget_post(const char *url, const char *certs, unsigned long limit, const char *body, unsigned long len)
{
    return start(url, certs, limit, body ? body : "", body ? len : 0);
}

int webget_state(struct webget *g, unsigned long *got, unsigned long *total)
{
    int state;
    glos_take(g->lock, 0xFFFFFFFFu);
    state = g->state;
    if (got)
        *got = g->got;
    if (total)
        *total = g->total;
    glos_give(g->lock);
    return state;
}

const char *webget_bytes(struct webget *g, unsigned long *len)
{
    *len = g->n;
    return g->bytes ? g->bytes : "";
}

const char *webget_type(struct webget *g) { return g->type; }
const char *webget_url(struct webget *g) { return g->url_got; }
const char *webget_why(struct webget *g) { return g->why; }

void webget_free(struct webget *g)
{
    int over;
    if (!g)
        return;
    glos_take(g->lock, 0xFFFFFFFFu);
    over = g->state != 0;
    g->unwanted = 1;
    glos_give(g->lock);
    if (over) {
        glos_close(g->lock);
        free(g->bytes);
        free(g->post);
        free(g);
    }
}
