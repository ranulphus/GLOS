/* URLs for the Web Reader (phase F; docs/v1/apps/web-reader.md): taken apart, and a link in a page made whole
   against the page's own address. From RFC 3986. One code for the app and for the host's tests. */
#ifndef URL_H
#define URL_H

struct url_parts {
    char scheme[16];                    /* small letters; "" if it has none */
    char host[128];                     /* small letters; "" for none (a file's, a mailto's) */
    unsigned port;                      /* 0: not said (the scheme's own) */
    char path[512];                     /* as written (its escapes kept) */
    char query[512];                    /* after the "?", without it; has_query says whether there was one */
    char fragment[128];                 /* after the "#" */
    int has_query, has_host;
};

/* A URL taken apart. 0; -1: a part is longer than its room. */
int url_split(const char *url, struct url_parts *p);

/* A reference (a link's href: whole, or a part: "../x", "?q", "#top", "//host/x") made whole against base: RFC
   3986's rules, the dots of its path removed. Blanks round it go; a blank within it and anything else a URL
   cannot have is written as an escape (%20). 0; -1: it doesn't fit, or base has no scheme and ref is not whole. */
int url_resolve(const char *base, const char *ref, char *out, int room);

/* The port a URL is asked at: its own, or its scheme's (http 80, https 443, gemini 1965, gopher 70); 0: none. */
unsigned url_port(const struct url_parts *p);

/* Text as a query's value ("a b&c" -> "a+b%26c"): what a search typed into the address bar becomes. Cut to room. */
void url_escape(const char *text, char *out, int room);

#endif
