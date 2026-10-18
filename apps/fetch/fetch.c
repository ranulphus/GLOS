/* FETCH (M10 step C): a file over HTTPS or HTTP, for the terminal and SSH.
 *
 *   FETCH [--certs FILE] [--resolve NAME:ADDRESS] [--quiet] URL [FILE]
 *
 * FILE defaults to the URL's last part (8.3, as DOS wants it); "-" writes
 * to stdout. Certificates are checked against C:\GLOS\CERTS.DAT (GLOSCERTS
 * names another, as does --certs), and not before GLOS's clock has been set.
 * Progress goes to stderr; at the end, a line:
 *
 *   FETCH done bytes=N sha256=HEX file=FILE status=200 tls=...
 *   FETCH failed: WHY
 *
 * Exit status 0, or 1 (failed), 2 (usage). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bearssl.h"
#include <glos/api.h>
#include <glos/http.h>
#include <glos/tls.h>

struct out {
    FILE *f;
    br_sha256_context sha;
    int quiet;
    unsigned last;
    int failed;
};

static int put(void *arg, const void *data, unsigned n)
{
    struct out *o = arg;
    br_sha256_update(&o->sha, data, n);
    if (fwrite(data, 1, n, o->f) != n) {
        o->failed = 1;
        return -1;
    }
    return 0;
}

static void progress(void *arg, unsigned long got, unsigned long total)
{
    struct out *o = arg;
    unsigned now = glos_ticks();
    if (o->quiet || now - o->last < 500)
        return;
    o->last = now;
    if (total)
        fprintf(stderr, "\r%lu of %lu bytes (%lu%%)   ", got, total, got * 100 / total);
    else
        fprintf(stderr, "\r%lu bytes   ", got);
}

/* The URL's last part, made a DOS name: up to 8 characters, a dot, 3. */
static void name_of(const char *url, char *out)
{
    const char *p = strchr(url, '?'), *s, *dot;
    char base[256];
    size_t n, i, k = 0;
    n = p ? (size_t)(p - url) : strlen(url);
    snprintf(base, sizeof base, "%.*s", (int)n, url);
    while ((n = strlen(base)) && base[n - 1] == '/')
        base[n - 1] = 0;
    s = strrchr(base, '/');
    s = s ? s + 1 : base;
    if (!*s || strstr(base, "://") + 3 >= s)
        s = "INDEX.HTM";
    dot = strrchr(s, '.');
    for (i = 0; s[i] && (!dot || s + i < dot) && k < 8; i++)
        if (isalnum((unsigned char)s[i]) || s[i] == '-' || s[i] == '_')
            out[k++] = (char)toupper((unsigned char)s[i]);
    if (!k)
        out[k++] = 'F';
    if (dot && dot[1]) {
        out[k++] = '.';
        for (i = 1; dot[i] && i <= 3; i++)
            if (isalnum((unsigned char)dot[i]))
                out[k++] = (char)toupper((unsigned char)dot[i]);
    }
    out[k] = 0;
}

int main(int argc, char **argv)
{
    struct http_get g;
    struct tls_anchors a = { 0, 0 };
    struct out o;
    const char *certs = getenv("GLOSCERTS"), *resolve = 0, *url = 0, *file = 0;
    char name[16], hex[65];
    unsigned char sum[32];
    int i, r, need_certs;
    memset(&o, 0, sizeof o);
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--certs") && i + 1 < argc)
            certs = argv[++i];
        else if (!strcmp(argv[i], "--resolve") && i + 1 < argc)
            resolve = argv[++i];
        else if (!strcmp(argv[i], "--quiet"))
            o.quiet = 1;
        else if (!url)
            url = argv[i];
        else if (!file)
            file = argv[i];
        else
            url = 0, i = argc;
    }
    if (!url) {
        fprintf(stderr, "usage: FETCH [--certs FILE] [--resolve NAME:ADDRESS] [--quiet] URL [FILE]\n");
        return 2;
    }
    if (glos_init() != 0) {
        printf("FETCH failed: %s\n", glos_version() ? "GLOS refused it as an app" : "this needs GLOS");
        return 1;
    }
    need_certs = !strncmp(url, "https://", 8);
    if (need_certs && tls_anchors_load(&a, certs ? certs : TLS_CERTS) != 0) {
        printf("FETCH failed: no trust anchors (%s)\n", certs ? certs : TLS_CERTS);
        return 1;
    }
    if (!file) {
        name_of(url, name);
        file = name;
    }
    o.f = strcmp(file, "-") ? fopen(file, "wb") : stdout;
    if (!o.f) {
        printf("FETCH failed: can't write %s\n", file);
        return 1;
    }
    br_sha256_init(&o.sha);
    memset(&g, 0, sizeof g);
    g.url = url;
    g.anchors = need_certs ? &a : 0;
    g.resolve = resolve;
    g.body = put;
    g.progress = progress;
    g.arg = &o;
    r = http_get(&g);
    if (o.f != stdout && fclose(o.f) != 0)
        o.failed = 1;
    if (!o.quiet)
        fprintf(stderr, "\r                                        \r");
    if (r != 0 || o.failed) {
        if (o.f != stdout)
            remove(file);
        printf("FETCH failed: %s\n", o.failed ? "the file couldn't be written" : g.why);
        return 1;
    }
    br_sha256_out(&o.sha, sum);
    for (i = 0; i < 32; i++)
        sprintf(hex + 2 * i, "%02x", sum[i]);
    fprintf(o.f == stdout ? stderr : stdout, "FETCH done bytes=%lu sha256=%s file=%s status=%d%s%s\n", g.length, hex,
            file, g.status, g.tls[0] ? " tls=" : "", g.tls);    /* (after the body on stdout: on stderr) */
    tls_anchors_free(&a);
    return 0;
}
