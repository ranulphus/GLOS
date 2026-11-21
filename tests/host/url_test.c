/* Host test for the Web Reader's URLs (apps/shared/url.c): URLs taken apart; references made whole against a
 * base: RFC 3986's own examples (its section 5.4, normal and abnormal), and what pages really have (blanks,
 * hosts in capitals, ports, no path, other schemes); a search's text escaped; whatever is given: no harm. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/url.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

static unsigned long seed = 20261121ul;
static unsigned rnd(void)
{
    seed = seed * 1103515245ul + 12345ul;
    return (unsigned)((seed >> 8) & 0xFFFFFFul);
}

static void whole(const char *base, const char *ref, const char *want)
{
    char out[600];
    int r = url_resolve(base, ref, out, sizeof out);
    CHECK(r == 0 && !strcmp(out, want), "\"%s\" against %s: %s, not %s", ref, base, r == 0 ? out : "(refused)", want);
}

int main(void)
{
    /* RFC 3986, 5.4: references against http://a/b/c/d;p?q */
    static const char *const rfc[][2] = {
        { "g:h", "g:h" }, { "g", "http://a/b/c/g" }, { "./g", "http://a/b/c/g" }, { "g/", "http://a/b/c/g/" }, { "/g", "http://a/g" },
        { "//g", "http://g/" }, { "?y", "http://a/b/c/d;p?y" }, { "g?y", "http://a/b/c/g?y" }, { "#s", "http://a/b/c/d;p?q#s" },
        { "g#s", "http://a/b/c/g#s" }, { "g?y#s", "http://a/b/c/g?y#s" }, { ";x", "http://a/b/c/;x" }, { "g;x", "http://a/b/c/g;x" },
        { "g;x?y#s", "http://a/b/c/g;x?y#s" }, { "", "http://a/b/c/d;p?q" }, { ".", "http://a/b/c/" }, { "./", "http://a/b/c/" },
        { "..", "http://a/b/" }, { "../", "http://a/b/" }, { "../g", "http://a/b/g" }, { "../..", "http://a/" }, { "../../", "http://a/" },
        { "../../g", "http://a/g" },
        /* ... and the abnormal ones */
        { "../../../g", "http://a/g" }, { "../../../../g", "http://a/g" }, { "/./g", "http://a/g" }, { "/../g", "http://a/g" },
        { "g.", "http://a/b/c/g." }, { ".g", "http://a/b/c/.g" }, { "g..", "http://a/b/c/g.." }, { "..g", "http://a/b/c/..g" },
        { "./../g", "http://a/b/g" }, { "./g/.", "http://a/b/c/g/" }, { "g/./h", "http://a/b/c/g/h" }, { "g/../h", "http://a/b/c/h" },
        { "g;x=1/./y", "http://a/b/c/g;x=1/y" }, { "g;x=1/../y", "http://a/b/c/y" }, { "g?y/./x", "http://a/b/c/g?y/./x" },
        { "g?y/../x", "http://a/b/c/g?y/../x" }, { "g#s/./x", "http://a/b/c/g#s/./x" }, { "g#s/../x", "http://a/b/c/g#s/../x" },
        { "http:g", "http:g" } };
    struct url_parts p;
    char out[600], big[3000];
    unsigned i, k;

    for (i = 0; i < sizeof rfc / sizeof rfc[0]; i++)
        whole("http://a/b/c/d;p?q", rfc[i][0], rfc[i][1]);
    /* (the RFC has "//g" as http://g: a host with no path is asked for "/" here, which is the same page) */

    /* what pages have */
    whole("https://Example.ORG:8443/docs/index.html?x=1#top", "page two.html", "https://example.org:8443/docs/page%20two.html");
    whole("https://example.org", "about", "https://example.org/about");
    whole("https://example.org", "", "https://example.org/");
    whole("https://example.org/a/b", "  \n /c?q=a b\t ", "https://example.org/c?q=a%20b");
    whole("https://example.org/a/b", "HTTP://Other.Example/X/../Y", "http://other.example/Y");
    whole("https://example.org/a/b", "mailto:someone@example.org", "mailto:someone@example.org");
    whole("https://example.org/a/b", "//cdn.example.org/lib.js", "https://cdn.example.org/lib.js");
    whole("gemini://example.org/dir/", "page.gmi", "gemini://example.org/dir/page.gmi");
    whole("file:///C:/WEB/INDEX.HTM", "PICS/A.PNG", "file:///C:/WEB/PICS/A.PNG");
    whole("https://example.org/a/b", "caf\xC3\xA9.html#caf\xC3\xA9", "https://example.org/a/caf%C3%A9.html#caf%C3%A9");
    whole("https://example.org/a/b", "x\"y<z>.html", "https://example.org/a/x%22y%3Cz%3E.html");
    CHECK(url_resolve("no scheme here", "x", out, sizeof out) == -1 && url_resolve("http://a/", "x", out, 8) == -1,
          "refused: a base with no scheme, a result with no room");

    /* taken apart */
    CHECK(url_split("HTTPS://User:Pw@Example.org:8443/a/b.html?x=1&y=2#frag", &p) == 0 && !strcmp(p.scheme, "https") && !strcmp(p.host, "example.org")
          && p.port == 8443 && !strcmp(p.path, "/a/b.html") && !strcmp(p.query, "x=1&y=2") && p.has_query && !strcmp(p.fragment, "frag")
          && p.has_host && url_port(&p) == 8443, "a URL with everything");
    CHECK(url_split("http://example.org", &p) == 0 && !p.path[0] && !p.port && url_port(&p) == 80 && !p.has_query, "no path, no port");
    CHECK(url_split("https://[2001:db8::1]:444/x", &p) == 0 && !strcmp(p.host, "[2001:db8::1]") && p.port == 444, "an address in brackets");
    CHECK(url_split("https://[2001:db8::1]/x", &p) == 0 && !strcmp(p.host, "[2001:db8::1]") && !p.port && url_port(&p) == 443, "... with no port");
    CHECK(url_split("gemini://example.org/", &p) == 0 && url_port(&p) == 1965 && url_split("gopher://example.org/1/x", &p) == 0
          && url_port(&p) == 70 && url_split("mailto:a@b", &p) == 0 && !p.has_host && !strcmp(p.path, "a@b") && url_port(&p) == 0, "other schemes");
    CHECK(url_split("/only/a/path?q", &p) == 0 && !p.scheme[0] && !p.has_host && !strcmp(p.path, "/only/a/path") && !strcmp(p.query, "q"),
          "no scheme");
    CHECK(url_split("http://example.org:99999/", &p) == 0 && p.port == 0 && url_split("http://example.org:8x/", &p) == 0 && p.port == 0,
          "a port that is none");
    memset(big, 'a', sizeof big - 1), big[sizeof big - 1] = 0;
    memcpy(big, "http://h/", 9);
    CHECK(url_split(big, &p) == -1 && url_resolve("http://a/", big + 9, out, sizeof out) == -1, "parts too long are refused");

    /* a search's words */
    url_escape("a b&c=d/e?\xC3\xA9~", out, sizeof out);
    CHECK(!strcmp(out, "a+b%26c%3Dd%2Fe%3F%C3%A9~"), "escaped: %s", out);
    url_escape("long long long", out, 8);
    CHECK(strlen(out) < 8, "... cut to its room");

    /* whatever is given */
    for (i = 0; i < 20000; i++) {
        char base[80], ref[80];
        static const char bits[] = "http://a.b/c/d?e#f:@[]%20 .././/\\\"<>\t\n";
        unsigned bl = rnd() % 60, rl = rnd() % 70;
        for (k = 0; k < bl; k++)
            base[k] = rnd() % 4 ? bits[rnd() % (sizeof bits - 1)] : (char)(1 + rnd() % 255);
        base[bl] = 0;
        for (k = 0; k < rl; k++)
            ref[k] = rnd() % 4 ? bits[rnd() % (sizeof bits - 1)] : (char)(1 + rnd() % 255);
        ref[rl] = 0;
        if (i & 1)
            memcpy(base, "http://a/b/", bl < 11 ? bl : 11);
        if (url_resolve(base, ref, out, (int)(1 + rnd() % sizeof out)) == 0)
            url_split(out, &p);
        url_split(ref, &p);
    }
    printf("url_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
