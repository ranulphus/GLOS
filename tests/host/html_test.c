/* Host test for the Web Reader's HTML (apps/shared/html.c): pages written out here, each read into a line of
 * text a block (its kind and depth, its runs with their styles, links and colours, a row's cells, pictures,
 * anchors, fields) and held against what it should be: text and its blanks, headings, styles, links made whole,
 * lists in lists, quotes, preformatted text, rules, line breaks, tables, definitions, character references of
 * every kind, what is not shown (scripts, styles, hidden elements, comments), pictures, a base address, colours,
 * forms' fields, tags out of order and not known, pages in Latin and in UTF-8; and whatever bytes: no harm. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/html.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

static char got[8192];
static unsigned ngot;
static int quiet;                       /* (the damaged pages: nothing is noted) */

static void note(const char *fmt, const char *a, const char *b)
{
    if (!quiet && ngot + strlen(a) + strlen(b) + 64 < sizeof got)
        ngot += (unsigned)sprintf(got + ngot, fmt, a, b);
}

static void s_block(void *c, int kind, int depth)
{
    static const char *const names[] = { "P", "H1:", "H2:", "H3:", "B", "N", "Q", "C", "R", "T:" };
    char t[24];
    (void)c;
    if (kind >= HTML_HEAD1 && kind <= HTML_HEAD3)
        snprintf(t, sizeof t, "%s%s", ngot ? "\n" : "", names[kind]);
    else if (kind == HTML_RULE || kind == HTML_ROW)
        snprintf(t, sizeof t, "%s%s", ngot ? "\n" : "", names[kind]);
    else
        snprintf(t, sizeof t, "%s%s%d:", ngot ? "\n" : "", kind >= 0 && kind < 10 ? names[kind] : "?", depth);
    note("%s%s", t, "");
}

static void s_text(void *c, const char *text, int n, unsigned style, const char *link)
{
    char t[1200], st[8];
    int i, k = 0;
    (void)c;
    for (i = 0; i < n && k < 1100; i++)
        if (text[i] == '\n')
            t[k++] = '\\', t[k++] = 'n';
        else
            t[k++] = text[i];
    t[k] = 0;
    snprintf(st, sizeof st, "%s%s%s%s", style & HTML_BOLD ? "b" : "", style & HTML_ITALIC ? "i" : "", style & HTML_MONO ? "m" : "",
             style & HTML_STRIKE ? "s" : "");
    if (link)
        note("<%s|", link, "");
    if (st[0])
        note("{%s:%s}", st, t);
    else
        note("%s%s", t, "");
    if (link)
        note(">%s", "", "");
}

static void s_cell(void *c) { (void)c; note(" | %s%s", "", ""); }

static void s_colour(void *c, long rgb)
{
    char t[32];
    (void)c;
    if (rgb < 0)
        snprintf(t, sizeof t, "(-)");
    else
        snprintf(t, sizeof t, "(%06lx)", rgb & 0xFFFFFFl);
    note("%s%s", t, "");
}

static void s_image(void *c, const char *src, const char *alt, int w, int h, const char *link)
{
    char t[700];
    (void)c;
    snprintf(t, sizeof t, "%sIMG(%s,%s,%d,%d,%s)", ngot ? "\n" : "", src, alt, w, h, link ? link : "");
    note("%s%s", t, "");
}

static void s_title(void *c, const char *title) { (void)c; note("%sTITLE:%s", ngot ? "\n" : "", title); }
static void s_anchor(void *c, const char *name) { (void)c; note("@%s%s", name, " "); }

static void s_field(void *c, const char *kind, const char *name, const char *value)
{
    char t[300];
    (void)c;
    snprintf(t, sizeof t, "FIELD(%s,%s,%s)", kind, name, value);
    note("%s%s", t, "");
}

static const struct html_sink sink = { s_block, s_text, s_cell, s_colour, s_image, s_title, s_anchor, s_field, NULL };

static const char *read_n(const char *page, unsigned long n, const char *charset)
{
    ngot = 0, got[0] = 0;
    html_read(page, n, charset, "http://h/d/p.html", &sink);
    return got;
}

static const char *read(const char *page) { return read_n(page, (unsigned long)strlen(page), ""); }

static void is(const char *what, const char *page, const char *want)
{
    const char *g = read(page);
    CHECK(!strcmp(g, want), "%s:\n  got  %s\n  want %s", what, g, want);
}

static unsigned long seed = 20261121ul;
static unsigned rnd(void)
{
    seed = seed * 1103515245ul + 12345ul;
    return (unsigned)((seed >> 8) & 0xFFFFFFul);
}

int main(void)
{
    is("a page",
       "<!DOCTYPE html><html><head><title> My \n Page </title><style>p { color: red }</style></head>\n<body>\n<h1>Hello</h1>\n"
       "<p>One  <b>two</b>\n<i>three</i> <a href=\"x.html\">four words</a>.</p>\n</body></html>",
       "TITLE:My Page\nH1:Hello\nP0:One {b:two} {i:three} <http://h/d/x.html|four words>.");
    is("headings", "<h1>a</h1><h2>b</h2><h3>c</h3><h4>d</h4><h6>e</h6>text", "H1:a\nH2:b\nH3:c\nH3:d\nH3:e\nP0:text");
    is("styles in styles", "<p><b>b<i>bi<code>bim</code></i></b> <s>s</s> <em>e</em><strong>s</strong><u>u</u></p>",
       "P0:{b:b}{bi:bi}{bim:bim} {s:s} {i:e}{b:s}u");
    is("lists in lists", "<ul><li>One<li>Two<ul><li>Deep</li><li>Deeper<p>with a paragraph</ul></li><li>Three</ul><ol><li>First<li>Second</ol>",
       "B0:One\nB0:Two\nB1:Deep\nB1:Deeper\nP2:with a paragraph\nB0:Three\nN0:First\nN0:Second");
    is("an item of no list", "<li>alone</li><p>after", "B0:alone\nP0:after");
    is("quotes, preformatted text, a rule, line breaks",
       "<blockquote>Quoted <p>para <blockquote>deeper</blockquote></blockquote><pre>\n a  b\n  <b>c</b>\n</pre><hr><p>x<br>y<br/>z</p>",
       "Q0:Quoted\nQ0:para\nQ1:deeper\nC0: a  b\\n  {b:c}\\n\nR\nP0:x\\ny\\nz");
    is("a table", "<table><caption>Cap</caption><thead><tr><th>A<th>B</tr></thead><tr><td>1</td><td><p>2</p><p>3</p> <a href='/y'>y</a></td></tr>"
       "<tr><td>last<br>line</table>after",
       "P0:Cap\nT:{b:A} | {b:B}\nT:1 | 2 3 <http://h/y|y>\nT:last line\nP0:after");
    is("cells of no row", "<td>a<td>b</td><p>p", "T:a | b\nP0:p");
    is("definitions", "<dl><dt>Term<dd>Meaning<dt>Other<dd>More</dl>", "P0:{b:Term}\nP1:Meaning\nP0:{b:Other}\nP1:More");
    is("character references",
       "<p>&amp; &lt;b&gt; &eacute;&Uuml;&ntilde; &#8212; &#x41;&#X42; &#150; &copy 2026 AT&T &nbsp;x &bogus; &#xD800; &#0; R&D; &hellip;&euro;</p>",
       "P0:& <b> \xC3\xA9\xC3\x9C\xC3\xB1 \xE2\x80\x94 AB \xE2\x80\x93 \xC2\xA9 2026 AT&T  x &bogus; \xEF\xBF\xBD \xEF\xBF\xBD R&D; "
       "\xE2\x80\xA6\xE2\x82\xAC");
    is("references in a link's address", "<a href=\"?a=1&amp;b=2&copy=3&lt;\">q</a>", "P0:<http://h/d/p.html?a=1&b=2&copy=3%3C|q>");
    is("what is not shown",
       "<p>a<script>var x = \"<p>no</p>\"; if (a</b) {}</script><span style=\"display: none\">gone <b>too</b></span><span hidden>and</span>b</p>"
       "<style>.x { }</style><!-- a comment <p>no</p> --><noscript><p>shown</noscript><template><p>not</p></template><svg><text>no</text></svg>c"
       "<?php echo 1 ?><![CDATA[x]]>",
       "P0:ab\nP0:shown\nP0:c");
    is("pictures, anchors, another base",
       "<base href=\"http://other/x/\"><p id=\"top\">t<img src=\"a.png\" alt=\"An A\" width=10 height=\"20\"><a href=\"big.png\"><img src=small.png></a>"
       "<a name=old>n</a><img alt='no address'>",
       "@top \nP0:t\nIMG(http://other/x/a.png,An A,10,20,)\nIMG(http://other/x/small.png,,0,0,http://other/x/big.png)@old \nP0:n[no address]");
    is("colours",
       "<p><font color=\"#f00\">red</font> <span style=\"color: rgb(0, 128, 0); font-weight: bold\">green</span> <span style=\"color:white\">white</span>"
       " <span style='COLOR:Navy;font-style:italic'>navy</span></p>",
       "P0:(ff0000)red(008000) {b:green}(-) white(000080) {i:navy}");       /* (a blank between two runs is told with the second's colour) */
    is("a form's fields",
       "<form action=/s><input type=text name=q value=\"hi\"><input type=submit value=\"Go\"> <select name=s><option>One<option>Two</select>"
       "<textarea name=t>typed &amp; kept</textarea><button>Press</button><input type=hidden name=h value=1><input></form>",
       "FIELD(text,q,hi)\nP0:[text]FIELD(submit,,Go)[Go]FIELD(select,s,) [select]FIELD(textarea,t,)[textarea]FIELD(button,,)PressFIELD(input,,)[input]");
    is("tags out of order, and not known",
       "<p>a<b>b<i>c</b>d</i>e</p><custom-el attr>x</custom-el></nonsense></div><p>f<p>g</p></p>h<a href=1>one<a href=2>two</a>",
       "P0:a{b:b}{bi:c}de\nP0:x\nP0:f\nP0:g\nP0:h<http://h/d/1|one><http://h/d/2|two>");
    is("what is no tag", "a < b, c <3, d<=e <> <-> </> < p>", "P0:a < b, c <3, d<=e <> <-> </> < p>");
    is("an end tag does not reach out of a cell", "<div><table><tr><td>a</div>b</td><td>c</table>d</div>e", "T:ab | c\nP0:d\nP0:e");
    is("attributes of every writing", "<a HREF = 'a b.html' title=x class>t</a><A href=q?x=1&y=2>u</A><a href>v</a>",
       "P0:<http://h/d/a%20b.html|t><http://h/d/q?x=1&y=2|u><http://h/d/p.html|v>");      /* (an empty address: this page) */
    is("nothing", "", "");
    is("blanks alone", " \n\t <p> </p> <div>\n</div>", "");
    /* a page's characters */
    CHECK(!strcmp(read_n("<p>caf\xE9 \x93q\x94</p>", 15, "text/html; charset=ISO-8859-1"), "P0:caf\xC3\xA9 \xE2\x80\x9Cq\xE2\x80\x9D"),
          "Latin, as its sender says: %s", got);
    CHECK(!strcmp(read_n("<p>a\0b</p>\0", 12, ""), "P0:ab"), "bytes of nothing are no text: %s", got);
    CHECK(!strcmp(read("<p>caf\xE9</p>"), "P0:caf\xC3\xA9"), "Latin, by its bytes: %s", got);
    CHECK(!strcmp(read("<meta http-equiv=Content-Type content=\"text/html; charset=windows-1252\"><p>\x80 5, na\xEFve"),
                  "P0:\xE2\x82\xAC 5, na\xC3\xAFve"), "Latin, as its own meta says: %s", got);
    CHECK(!strcmp(read("<meta charset=\"utf-8\"><p>caf\xC3\xA9 \xE2\x82\xAC"), "P0:caf\xC3\xA9 \xE2\x82\xAC"), "UTF-8: %s", got);
    CHECK(!strcmp(read("\xEF\xBB\xBF<p>caf\xC3\xA9"), "P0:caf\xC3\xA9"), "UTF-8 with its mark: %s", got);
    CHECK(!strcmp(read_n("<p>caf\xC3\xA9", 9, "utf-8"), "P0:caf\xC3\xA9") && !strcmp(read_n("<p>caf\xC3", 7, ""), "P0:caf\xC3"),
          "UTF-8 said; a character cut by the page's end is no proof of Latin: %s", got);
    {                                           /* text longer than a run's buffer: none of it lost */
        static char big[9000], want[9000];
        unsigned n = 0, w = 0, i;
        n += (unsigned)sprintf(big + n, "<p>");
        w += (unsigned)sprintf(want + w, "P0:");
        for (i = 0; i < 700; i++) {
            n += (unsigned)sprintf(big + n, "word%u \n", i);
            w += (unsigned)sprintf(want + w, "%sword%u", i ? " " : "", i);
        }
        CHECK(!strcmp(read(big), want), "a long paragraph");
    }
    {                                           /* nested deeper than is kept: it comes out, and what follows is right */
        static char deep[8000];
        unsigned n = 0, i;
        for (i = 0; i < 200; i++)
            n += (unsigned)sprintf(deep + n, "<div><b><ul><li>");
        n += (unsigned)sprintf(deep + n, "x");
        for (i = 0; i < 200; i++)
            n += (unsigned)sprintf(deep + n, "</li></ul></b></div>");
        sprintf(deep + n, "<p>after</p>");
        read(deep);
        CHECK(strstr(got, "x") && strlen(got) > 9 && !strcmp(got + strlen(got) - 9, "\nP0:after"), "deep: ...%s", got + (strlen(got) > 30 ? strlen(got) - 30 : 0));
    }
    html_read("<p>no sink", 10, "", "http://h/", NULL);
    html_read("<p>no base <a href=x>x</a><img src=y>", 37, NULL, NULL, &sink);

    /* whatever bytes: pages with bytes changed, and bytes of no sense */
    {
        static const char *const pages[] = {
            "<html><head><title>T</title><base href='http://b/'></head><body><h1 id=a>H</h1><p>One <b>two <i>three</b></i> <a href=\"x?y=1&amp;z\">l</a>"
            "<ul><li>a<li>b<ol><li>c</ol></ul><table><tr><th>h<td>d<tr><td colspan=2>e</table><pre>p\n q</pre><blockquote>q</blockquote>"
            "<img src=i.png alt=I width=3><form><input name=n value=v><select><option>o</select><textarea>t</textarea></form>"
            "<script>s</script><style>s</style><!-- c --><span style='color:#abc;display:none'>h</span>&amp;&#65;&#x42;&eacute;&nbsp;</body></html>",
            "<meta charset=windows-1252><p>caf\xE9 \x93q\x94 <font color=red>r</font><dl><dt>t<dd>d</dl><hr><br><wbr>" };
        static char copy[900];
        unsigned i, k;
        quiet = 1;
        for (i = 0; i < 6000; i++) {
            const char *page = pages[i & 1];
            unsigned n = (unsigned)strlen(page), changes = 1 + rnd() % 8;
            memcpy(copy, page, n);
            for (k = 0; k < changes; k++) {
                unsigned at = rnd() % n;
                copy[at] = rnd() % 3 ? "<>/&;=\"' #!-"[rnd() % 12] : (char)rnd();
            }
            html_read(copy, rnd() % 6 ? n : rnd() % (n + 1), rnd() % 3 ? "" : rnd() % 2 ? "utf-8" : "iso-8859-1", rnd() % 4 ? "http://h/d/" : "", &sink);
        }
        for (i = 0; i < 500; i++) {
            unsigned n = 1 + rnd() % 800;
            for (k = 0; k < n; k++)
                copy[k] = rnd() % 3 ? "<>/&;=\"' abcdeiptrlh1"[rnd() % 21] : (char)rnd();
            html_read(copy, n, "", "http://h/", &sink);
        }
        quiet = 0;
    }
    printf("html_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
