/* HTML read (html.h).
 *
 * The bytes are gone through once. Text between tags has its character references made characters and its
 * blanks made one (but in <pre>); a tag is looked up, and changes how the text after it is to look (a style,
 * a link, a colour), or where it goes (a new block: a paragraph, a heading, an item of a list, a table's row).
 * The elements that are open are kept in a stack, each with what was so before it, to be so again at its end;
 * an end tag closes back to its start, and one that has none is passed over. What a page may not show is
 * passed over with all that is in it: scripts, styles, what is marked hidden.
 *
 * Whatever the bytes: nothing is read beyond them, the stack and every buffer have their limits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "html.h"
#include "url.h"

enum { K_NONE, K_STYLE, K_BLOCK, K_HEAD, K_LIST, K_ITEM, K_QUOTE, K_PRE, K_BREAK, K_RULE, K_IMAGE, K_LINK, K_SKIP, K_TABLE, K_ROW,
       K_CELL, K_TITLE, K_FIELD, K_TERM, K_BASE, K_META, K_FONT, K_HEADPART, K_OPTION, K_AREA };

static const struct tag {
    const char *name;
    unsigned char kind, arg;
} tags[] = {
    { "a", K_LINK, 0 }, { "b", K_STYLE, HTML_BOLD }, { "strong", K_STYLE, HTML_BOLD }, { "i", K_STYLE, HTML_ITALIC },
    { "em", K_STYLE, HTML_ITALIC }, { "cite", K_STYLE, HTML_ITALIC }, { "var", K_STYLE, HTML_ITALIC }, { "dfn", K_STYLE, HTML_ITALIC },
    { "code", K_STYLE, HTML_MONO }, { "kbd", K_STYLE, HTML_MONO }, { "samp", K_STYLE, HTML_MONO }, { "tt", K_STYLE, HTML_MONO },
    { "s", K_STYLE, HTML_STRIKE }, { "strike", K_STYLE, HTML_STRIKE }, { "del", K_STYLE, HTML_STRIKE }, { "u", K_STYLE, 0 },
    { "span", K_STYLE, 0 }, { "small", K_STYLE, 0 }, { "big", K_STYLE, 0 }, { "sub", K_STYLE, 0 }, { "sup", K_STYLE, 0 },
    { "ins", K_STYLE, 0 }, { "mark", K_STYLE, 0 }, { "abbr", K_STYLE, 0 }, { "q", K_STYLE, 0 }, { "time", K_STYLE, 0 },
    { "label", K_STYLE, 0 }, { "nobr", K_STYLE, 0 }, { "font", K_FONT, 0 },
    { "p", K_BLOCK, 0 }, { "div", K_BLOCK, 0 }, { "section", K_BLOCK, 0 }, { "article", K_BLOCK, 0 }, { "main", K_BLOCK, 0 },
    { "header", K_BLOCK, 0 }, { "footer", K_BLOCK, 0 }, { "nav", K_BLOCK, 0 }, { "aside", K_BLOCK, 0 }, { "address", K_BLOCK, 0 },
    { "figure", K_BLOCK, 0 }, { "figcaption", K_BLOCK, 0 }, { "center", K_BLOCK, 0 }, { "form", K_BLOCK, 0 }, { "fieldset", K_BLOCK, 0 },
    { "legend", K_BLOCK, 0 }, { "details", K_BLOCK, 0 }, { "summary", K_BLOCK, 0 }, { "caption", K_BLOCK, 0 }, { "dl", K_BLOCK, 0 },
    { "body", K_BLOCK, 0 }, { "html", K_NONE, 0 }, { "noscript", K_BLOCK, 0 },
    { "h1", K_HEAD, 1 }, { "h2", K_HEAD, 2 }, { "h3", K_HEAD, 3 }, { "h4", K_HEAD, 3 }, { "h5", K_HEAD, 3 }, { "h6", K_HEAD, 3 },
    { "ul", K_LIST, 1 }, { "ol", K_LIST, 2 }, { "menu", K_LIST, 1 }, { "dir", K_LIST, 1 }, { "li", K_ITEM, 0 },
    { "dt", K_TERM, 1 }, { "dd", K_TERM, 2 },
    { "blockquote", K_QUOTE, 0 }, { "pre", K_PRE, 0 }, { "listing", K_PRE, 0 }, { "xmp", K_PRE, 0 }, { "plaintext", K_PRE, 0 },
    { "br", K_BREAK, 0 }, { "wbr", K_NONE, 0 }, { "hr", K_RULE, 0 }, { "img", K_IMAGE, 0 },
    { "script", K_SKIP, 0 }, { "style", K_SKIP, 0 }, { "template", K_SKIP, 0 }, { "svg", K_SKIP, 0 }, { "math", K_SKIP, 0 },
    { "iframe", K_SKIP, 0 }, { "object", K_SKIP, 0 }, { "canvas", K_SKIP, 0 }, { "audio", K_SKIP, 0 }, { "video", K_SKIP, 0 },
    { "noframes", K_SKIP, 0 }, { "noembed", K_SKIP, 0 }, { "datalist", K_SKIP, 0 }, { "map", K_SKIP, 0 },
    { "table", K_TABLE, 0 }, { "tr", K_ROW, 0 }, { "td", K_CELL, 0 }, { "th", K_CELL, 1 }, { "thead", K_NONE, 0 },
    { "tbody", K_NONE, 0 }, { "tfoot", K_NONE, 0 }, { "colgroup", K_NONE, 0 }, { "col", K_NONE, 0 },
    { "title", K_TITLE, 0 }, { "head", K_HEADPART, 0 }, { "base", K_BASE, 0 }, { "meta", K_META, 0 }, { "link", K_NONE, 0 },
    { "input", K_FIELD, 0 }, { "textarea", K_FIELD, 1 }, { "select", K_FIELD, 2 }, { "button", K_FIELD, 3 }, { "option", K_OPTION, 0 },
    { "optgroup", K_OPTION, 0 }, { "area", K_AREA, 0 },
};
#define NTAGS ((int)(sizeof tags / sizeof tags[0]))

/* Elements that have no content and so no end tag. */
static int is_void(int kind, const char *name)
{
    return kind == K_BREAK || kind == K_RULE || kind == K_IMAGE || kind == K_BASE || kind == K_META || kind == K_AREA
           || !strcmp(name, "input") || !strcmp(name, "link") || !strcmp(name, "col") || !strcmp(name, "wbr");
}

struct open {                           /* an element that is open, and what was so before it */
    short tag;
    unsigned char hidden;
    unsigned style;
    long colour;
    short depth, head, quote, pre, list, in_link;
};

struct attr {
    char name[16];
    char value[600];
};

struct reader {
    const struct html_sink *s;
    char base[600];
    struct open stack[64];
    int n;
    unsigned style;
    long colour, told_colour;
    char link[600];
    int in_link, depth, head, quote, pre, list, hidden;
    int item;                           /* an item of a list has begun: the next block is its (1 bullet, 2 number) */
    int row, cells;                     /* in a table's row; its cells so far */
    unsigned cell_text;                 /* ... the letters of this cell so far */
    int open;                           /* a block is open */
    int space;                          /* a blank is owed before the next word */
    int in_title;
    char title[240];
    unsigned ntitle;
    char buf[1024];                     /* text gathered for one call of the sink's */
    unsigned nbuf;
    int field_skip;                     /* in a <textarea> or <select>: what is in it is the field's, not the page's */
    int forms, in_form;                 /* forms begun so far; the one fields are in now (-1: none) */
};

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int blank(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

static void emit(struct reader *r, const char *text, unsigned n, unsigned style, const char *link)
{
    if (!n || !r->s->text)
        return;
    if (r->s->colour && r->colour != r->told_colour) {
        r->s->colour(r->s->ctx, r->colour);
        r->told_colour = r->colour;
    }
    r->s->text(r->s->ctx, text, (int)n, style, link);
}

static void flush(struct reader *r)
{
    emit(r, r->buf, r->nbuf, r->style, r->in_link && r->link[0] ? r->link : NULL);
    r->nbuf = 0;
}

static void end_block(struct reader *r)
{
    flush(r);
    if (r->row) {                               /* (within a table's row a block's end is a blank: the row is the block) */
        r->space = r->cell_text != 0;
        return;
    }
    r->open = 0, r->space = 0;
}

static void begin_block(struct reader *r)       /* the block the text to come is in, if none is open */
{
    int kind, depth;
    if (r->open)
        return;
    if (r->row) {
        r->open = 1;
        return;
    }
    kind = r->head ? HTML_HEAD1 + r->head - 1 : r->pre ? HTML_CODE : r->item ? (r->item == 2 ? HTML_NUMBER : HTML_BULLET)
           : r->quote ? HTML_QUOTE : HTML_PARA;
    depth = r->depth - (kind == HTML_BULLET || kind == HTML_NUMBER || kind == HTML_QUOTE ? 1 : 0);
    if (r->s->block)
        r->s->block(r->s->ctx, kind, depth > 0 ? depth : 0);
    r->item = 0;
    r->open = 1, r->space = 0;
}

static void put(struct reader *r, const char *text, unsigned n)        /* text (UTF-8, its references undone) in the page */
{
    unsigned i;
    if (r->hidden || r->field_skip)
        return;
    if (r->in_title) {
        for (i = 0; i < n && r->ntitle < sizeof r->title - 1; i++)
            if (!blank(text[i]))
                r->title[r->ntitle++] = text[i];
            else if (r->ntitle && r->title[r->ntitle - 1] != ' ')
                r->title[r->ntitle++] = ' ';
        return;
    }
    for (i = 0; i < n; i++) {
        char c = text[i];
        if (!c)                                 /* (a byte of nothing is no text) */
            continue;
        if (r->pre) {
            if (c == '\r')
                continue;
            begin_block(r);
        } else if (blank(c)) {
            r->space = r->open;                 /* (blanks at a block's start are none) */
            continue;
        } else {
            begin_block(r);
            r->cell_text++;
            if (r->space) {                     /* the blank owed: within a run its own; between two runs a run of
                                                   its own, plain (no link is underlined into the blank before it) */
                if (r->nbuf == sizeof r->buf)
                    flush(r);
                if (r->nbuf)
                    r->buf[r->nbuf++] = ' ';
                else
                    emit(r, " ", 1, 0, NULL);
                r->space = 0;
            }
        }
        if (r->nbuf == sizeof r->buf)
            flush(r);
        r->buf[r->nbuf++] = c;
    }
}

/* ---- character references */

static const struct { const char *name; unsigned short u; } named[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", 0xA0 }, { "copy", 0xA9 }, { "reg", 0xAE },
    { "trade", 0x2122 }, { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "hellip", 0x2026 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
    { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "sbquo", 0x201A }, { "bdquo", 0x201E }, { "laquo", 0xAB }, { "raquo", 0xBB },
    { "lsaquo", 0x2039 }, { "rsaquo", 0x203A }, { "bull", 0x2022 }, { "middot", 0xB7 }, { "euro", 0x20AC }, { "pound", 0xA3 },
    { "yen", 0xA5 }, { "cent", 0xA2 }, { "curren", 0xA4 }, { "sect", 0xA7 }, { "para", 0xB6 }, { "deg", 0xB0 }, { "plusmn", 0xB1 },
    { "times", 0xD7 }, { "divide", 0xF7 }, { "frac12", 0xBD }, { "frac14", 0xBC }, { "frac34", 0xBE }, { "sup1", 0xB9 }, { "sup2", 0xB2 },
    { "sup3", 0xB3 }, { "micro", 0xB5 }, { "iexcl", 0xA1 }, { "iquest", 0xBF }, { "ordf", 0xAA }, { "ordm", 0xBA }, { "not", 0xAC },
    { "shy", 0xAD }, { "macr", 0xAF }, { "acute", 0xB4 }, { "cedil", 0xB8 }, { "uml", 0xA8 }, { "brvbar", 0xA6 }, { "dagger", 0x2020 },
    { "Dagger", 0x2021 }, { "permil", 0x2030 }, { "minus", 0x2212 }, { "larr", 0x2190 }, { "rarr", 0x2192 }, { "uarr", 0x2191 },
    { "darr", 0x2193 }, { "harr", 0x2194 }, { "ne", 0x2260 }, { "le", 0x2264 }, { "ge", 0x2265 }, { "infin", 0x221E }, { "asymp", 0x2248 },
    { "thinsp", 0x2009 }, { "ensp", 0x2002 }, { "emsp", 0x2003 }, { "zwj", 0x200D }, { "zwnj", 0x200C }, { "prime", 0x2032 },
    { "Prime", 0x2033 }, { "check", 0x2713 }, { "hearts", 0x2665 }, { "star", 0x2606 }, { "szlig", 0xDF }, { "aelig", 0xE6 },
    { "AElig", 0xC6 }, { "oslash", 0xF8 }, { "Oslash", 0xD8 }, { "aring", 0xE5 }, { "Aring", 0xC5 }, { "ccedil", 0xE7 }, { "Ccedil", 0xC7 },
    { "ntilde", 0xF1 }, { "Ntilde", 0xD1 }, { "eth", 0xF0 }, { "ETH", 0xD0 }, { "thorn", 0xFE }, { "THORN", 0xDE }, { "yuml", 0xFF },
    { "oelig", 0x153 }, { "OElig", 0x152 }, { "scaron", 0x161 }, { "Scaron", 0x160 }, { "alpha", 0x3B1 }, { "beta", 0x3B2 },
    { "gamma", 0x3B3 }, { "delta", 0x3B4 }, { "pi", 0x3C0 }, { "mu", 0x3BC }, { "omega", 0x3C9 }, { "Omega", 0x3A9 }, { "lambda", 0x3BB },
    { "sigma", 0x3C3 }, { "theta", 0x3B8 } };

/* The vowels with their marks: "eacute", "Uuml", "ograve", "Acirc", "atilde": the letter, then the mark. */
static unsigned accented(const char *name, unsigned n)
{
    static const char letters[] = "AEIOUYaeiouy";
    static const struct { const char *mark; unsigned char at[12]; } marks[] = {
        { "grave", { 0xC0, 0xC8, 0xCC, 0xD2, 0xD9, 0, 0xE0, 0xE8, 0xEC, 0xF2, 0xF9, 0 } },
        { "acute", { 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xDD, 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xFD } },
        { "circ", { 0xC2, 0xCA, 0xCE, 0xD4, 0xDB, 0, 0xE2, 0xEA, 0xEE, 0xF4, 0xFB, 0 } },
        { "tilde", { 0xC3, 0, 0, 0xD5, 0, 0, 0xE3, 0, 0, 0xF5, 0, 0 } },
        { "uml", { 0xC4, 0xCB, 0xCF, 0xD6, 0xDC, 0, 0xE4, 0xEB, 0xEF, 0xF6, 0xFC, 0xFF } } };
    const char *l = n > 1 ? strchr(letters, name[0]) : NULL;
    unsigned i;
    for (i = 0; l && i < 5; i++)
        if (strlen(marks[i].mark) == n - 1 && !strncmp(name + 1, marks[i].mark, n - 1))
            return marks[i].at[l - letters];
    return 0;
}

static unsigned utf8(unsigned long u, char *out)
{
    if (u < 0x80) {
        out[0] = (char)u;
        return 1;
    }
    if (u < 0x800) {
        out[0] = (char)(0xC0 | u >> 6), out[1] = (char)(0x80 | (u & 63));
        return 2;
    }
    if (u < 0x10000) {
        out[0] = (char)(0xE0 | u >> 12), out[1] = (char)(0x80 | (u >> 6 & 63)), out[2] = (char)(0x80 | (u & 63));
        return 3;
    }
    out[0] = (char)(0xF0 | (u >> 18 & 7)), out[1] = (char)(0x80 | (u >> 12 & 63)), out[2] = (char)(0x80 | (u >> 6 & 63));
    out[3] = (char)(0x80 | (u & 63));
    return 4;
}

/* Windows-1252's characters from 80h to 9Fh (what "&#150;" means on pages, and those bytes in Latin pages). */
static const unsigned short high1252[32] = { 0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039,
                                             0x152, 0x8D, 0x17D, 0x8F, 0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                             0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178 };

/* A reference at p (after its "&"): its character as UTF-8 into out (8 bytes of room): how many bytes of the
   page it was (0: no reference: the "&" is itself), *len the UTF-8's length. */
static unsigned reference(const char *p, const char *end, char *out, unsigned *len)
{
    unsigned long u = 0;
    unsigned n = 0, i;
    if (p < end && *p == '#') {
        int hex = p + 1 < end && (p[1] == 'x' || p[1] == 'X');
        const char *q = p + 1 + hex, *first = q;
        for (; q < end && q - first < 8; q++) {
            int c = lower((unsigned char)*q), v = c >= '0' && c <= '9' ? c - '0' : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (v < 0)
                break;
            u = u * (hex ? 16 : 10) + (unsigned long)v;
        }
        if (q == first)
            return 0;
        if (u >= 0x80 && u < 0xA0)
            u = high1252[u - 0x80];
        if (!u || u > 0x10FFFF || (u >= 0xD800 && u < 0xE000))
            u = 0xFFFD;
        *len = utf8(u, out);
        return (unsigned)(q - p) + (q < end && *q == ';');
    }
    while (p + n < end && n < 10 && ((lower((unsigned char)p[n]) >= 'a' && lower((unsigned char)p[n]) <= 'z') || (p[n] >= '0' && p[n] <= '9')))
        n++;
    if (!n)
        return 0;
    for (i = 0; i < sizeof named / sizeof named[0] && !u; i++)
        if (strlen(named[i].name) == n && !strncmp(named[i].name, p, n))
            u = named[i].u;
    if (!u)
        u = accented(p, n);
    if (!u || (p + n < end && p[n] != ';' && u > 0xFF))        /* (without its ";" only the old ones are taken) */
        return 0;
    *len = utf8(u, out);
    return n + (p + n < end && p[n] == ';');
}

/* Text with references into out (room bytes, cut): its length. For attributes' values. */
static unsigned unreference(const char *p, const char *end, char *out, unsigned room)
{
    unsigned n = 0;
    while (p < end && n + 5 < room) {
        char u[8];
        unsigned len = 0, used;
        if (*p == '&' && (used = reference(p + 1, end, u, &len)) != 0
            && (p[used] == ';' || p + 1 + used >= end || !(p[1 + used] == '=' || (p[1 + used] >= '0' && p[1 + used] <= '9')
                                                          || (lower((unsigned char)p[1 + used]) >= 'a' && lower((unsigned char)p[1 + used]) <= 'z')))) {
            memcpy(out + n, u, len), n += len;
            p += 1 + used;
        } else {
            out[n++] = *p++;
        }
    }
    out[n] = 0;
    return n;
}

/* ---- colours: #rgb, #rrggbb, rgb(r, g, b), and the old names */

static long colour_of(const char *v)
{
    static const struct { const char *name; long rgb; } names[] = {
        { "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 }, { "blue", 0x0000FF },
        { "yellow", 0xFFFF00 }, { "gray", 0x808080 }, { "grey", 0x808080 }, { "silver", 0xC0C0C0 }, { "maroon", 0x800000 },
        { "navy", 0x000080 }, { "olive", 0x808000 }, { "purple", 0x800080 }, { "teal", 0x008080 }, { "aqua", 0x00FFFF },
        { "fuchsia", 0xFF00FF }, { "lime", 0x00FF00 }, { "orange", 0xFFA500 }, { "brown", 0xA52A2A }, { "pink", 0xFFC0CB } };
    unsigned r, g, b, i, n;
    char name[12];
    while (blank(*v))
        v++;
    if (*v == '#') {
        unsigned long x = 0;
        for (n = 0; n < 6; n++) {
            int c = lower((unsigned char)v[1 + n]), d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
            if (d < 0)
                break;
            x = x << 4 | (unsigned long)d;
        }
        if (n == 6)
            return (long)x;
        if (n == 3)
            return (long)((x >> 8 & 15) * 0x110000 + (x >> 4 & 15) * 0x1100 + (x & 15) * 0x11);
        return -1;
    }
    if (sscanf(v, "rgb(%u ,%u ,%u", &r, &g, &b) == 3 && r < 256 && g < 256 && b < 256)
        return (long)(r << 16 | g << 8 | b);
    for (n = 0; n < 11 && lower((unsigned char)v[n]) >= 'a' && lower((unsigned char)v[n]) <= 'z'; n++)
        name[n] = (char)lower((unsigned char)v[n]);
    name[n] = 0;
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(names[i].name, name))
            return names[i].rgb;
    return -1;
}

/* A text colour a reader can show on its white: one too near white is not taken (white text on a dark ground
   the reader doesn't draw would be no text at all). */
static long readable(long rgb)
{
    if (rgb < 0)
        return -1;
    return ((rgb >> 16 & 255) * 3 + (rgb >> 8 & 255) * 6 + (rgb & 255)) / 10 > 200 ? -1 : rgb;
}

/* A style written on an element ("color: red; display: none"): a property's value into out; 1 if it is there. */
static int style_value(const char *style, const char *property, char *out, unsigned room)
{
    size_t pl = strlen(property);
    const char *p = style;
    while (*p) {
        const char *end = p + strcspn(p, ";"), *colon;
        while (blank(*p))
            p++;
        colon = memchr(p, ':', (size_t)(end - p));
        if (colon && (size_t)(colon - p) >= pl) {
            const char *q = p;
            size_t k;
            for (k = 0; k < pl && lower((unsigned char)q[k]) == property[k]; k++)
                ;
            q += k;
            while (q < colon && blank(*q))
                q++;
            if (k == pl && q == colon) {
                unsigned n = 0;
                for (q = colon + 1; q < end && blank(*q); q++)
                    ;
                for (; q < end && n < room - 1; q++)
                    out[n++] = (char)lower((unsigned char)*q);
                while (n && blank(out[n - 1]))
                    n--;
                out[n] = 0;
                return 1;
            }
        }
        p = *end ? end + 1 : end;
    }
    return 0;
}

/* ---- tags */

static const char *attr_of(const struct attr *a, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(a[i].name, name))
            return a[i].value;
    return NULL;
}

static void whole(struct reader *r, const char *ref, char *out, unsigned room)  /* a page's address made whole */
{
    if (!ref || url_resolve(r->base, ref, out, (int)room) != 0)
        out[0] = 0;
}

static void push(struct reader *r, int tag, int hidden)
{
    struct open *o;
    if (r->n == 64)
        return;
    o = &r->stack[r->n++];
    o->tag = (short)tag, o->hidden = (unsigned char)hidden;
    o->style = r->style, o->colour = r->colour;
    o->depth = (short)r->depth, o->head = (short)r->head, o->quote = (short)r->quote, o->pre = (short)r->pre, o->list = (short)r->list;
    o->in_link = (short)r->in_link;
    r->hidden += hidden;
}

static void pop(struct reader *r)       /* the innermost open element closed: what was so before it is so again */
{
    struct open *o;
    int kind;
    if (!r->n)
        return;
    o = &r->stack[--r->n];
    kind = tags[o->tag].kind;
    flush(r);
    if (kind == K_BLOCK || kind == K_HEAD || kind == K_LIST || kind == K_ITEM || kind == K_QUOTE || kind == K_PRE || kind == K_TERM
        || kind == K_TABLE) {
        end_block(r);
        r->item = 0;
    }
    if (kind == K_ROW || kind == K_TABLE) {
        flush(r);
        r->row = 0, r->open = 0, r->space = 0;
    }
    if (kind == K_TITLE) {
        r->in_title = 0;
        r->title[r->ntitle] = 0;
        while (r->ntitle && r->title[r->ntitle - 1] == ' ')
            r->title[--r->ntitle] = 0;
        if (r->s->title && !r->hidden)
            r->s->title(r->s->ctx, r->title);
    }
    if (kind == K_FIELD)
        r->field_skip = 0;
    r->style = o->style, r->colour = o->colour;
    r->depth = o->depth, r->head = o->head, r->quote = o->quote, r->pre = o->pre, r->list = o->list, r->in_link = o->in_link;
    r->hidden -= o->hidden;
}

static int find_open(const struct reader *r, int tag)
{
    int i;
    for (i = r->n - 1; i >= 0; i--)
        if (r->stack[i].tag == tag)
            return i;
    return -1;
}

static void close_kinds(struct reader *r, int kind, int stop_kind)     /* the innermost open element of a kind closed, with
                                                                          all in it; not beyond one of stop_kind */
{
    int i;
    for (i = r->n - 1; i >= 0; i--) {
        if (tags[r->stack[i].tag].kind == stop_kind)
            return;
        if (tags[r->stack[i].tag].kind == kind) {
            while (r->n > i)
                pop(r);
            return;
        }
    }
}

static int tag_named(const char *name)
{
    int i;
    for (i = 0; i < NTAGS; i++)
        if (!strcmp(tags[i].name, name))
            return i;
    return -1;
}

static void start_tag(struct reader *r, int tag, const struct attr *a, int na, int self_closed)
{
    const struct tag *t = &tags[tag];
    const char *style = attr_of(a, na, "style"), *id = attr_of(a, na, "id");
    char v[64], url[600];
    int hidden = attr_of(a, na, "hidden") != NULL || (style && style_value(style, "display", v, sizeof v) && !strcmp(v, "none"));
    flush(r);
    /* an element that begins ends some that are open: a paragraph any block; an item the item before it; a cell the
       cell before it, a row the row before it */
    if (t->kind == K_BLOCK || t->kind == K_HEAD || t->kind == K_LIST || t->kind == K_QUOTE || t->kind == K_PRE || t->kind == K_RULE
        || t->kind == K_TABLE || t->kind == K_ITEM || t->kind == K_TERM) {
        int p = find_open(r, tag_named("p"));
        if (p >= 0 && !r->row)
            while (r->n > p)
                pop(r);
    }
    /* a block that begins in a table's row but in none of its cells: the row has ended (its table was never closed) */
    if (r->row && (t->kind == K_BLOCK || t->kind == K_HEAD || t->kind == K_LIST || t->kind == K_QUOTE || t->kind == K_PRE || t->kind == K_RULE)) {
        int i, in_cell = 0;
        for (i = 0; i < r->n; i++)
            in_cell |= tags[r->stack[i].tag].kind == K_CELL;
        if (!in_cell)
            r->row = 0, r->open = 0, r->space = 0;
    }
    if (t->kind == K_ITEM)
        close_kinds(r, K_ITEM, K_LIST);
    if (t->kind == K_TERM)
        close_kinds(r, K_TERM, K_BLOCK);
    if (t->kind == K_CELL)
        close_kinds(r, K_CELL, K_ROW);
    if (t->kind == K_ROW)
        close_kinds(r, K_ROW, K_TABLE);
    if (t->kind == K_OPTION)
        close_kinds(r, K_OPTION, K_FIELD);
    if (t->kind == K_LINK)                      /* (a link within a link: the first has ended) */
        close_kinds(r, K_LINK, K_CELL);
    if (id && id[0] && r->s->anchor && !r->hidden && !hidden)
        r->s->anchor(r->s->ctx, id);
    if (!is_void(t->kind, t->name) && !self_closed)
        push(r, tag, hidden);
    else if (hidden)
        return;
    if (r->hidden)
        return;
    if (style && style_value(style, "color", v, sizeof v))
        r->colour = readable(colour_of(v));
    switch (t->kind) {
    case K_STYLE:
        r->style |= t->arg;
        if (style && style_value(style, "font-weight", v, sizeof v) && (!strcmp(v, "bold") || atoi(v) >= 600))
            r->style |= HTML_BOLD;
        if (style && style_value(style, "font-style", v, sizeof v) && !strcmp(v, "italic"))
            r->style |= HTML_ITALIC;
        break;
    case K_FONT:
        if (attr_of(a, na, "color"))
            r->colour = readable(colour_of(attr_of(a, na, "color")));
        break;
    case K_LINK:
        if (attr_of(a, na, "name") && r->s->anchor)
            r->s->anchor(r->s->ctx, attr_of(a, na, "name"));
        if (attr_of(a, na, "href")) {
            whole(r, attr_of(a, na, "href"), r->link, sizeof r->link);
            r->in_link = r->link[0] != 0;
        }
        break;
    case K_BLOCK:
        end_block(r);
        if (!strcmp(t->name, "form") && r->s->form) {   /* (phase F) a form: where it goes, how */
            char url[600], m[8];
            const char *act = attr_of(a, na, "action"), *how = attr_of(a, na, "method");
            unsigned k;
            if (act && act[0])
                whole(r, act, url, sizeof url);
            else
                snprintf(url, sizeof url, "%s", r->base);
            for (k = 0; how && how[k] && k < sizeof m - 1; k++)
                m[k] = (char)lower((unsigned char)how[k]);
            m[how ? k : 0] = 0;
            r->s->form(r->s->ctx, url, m[0] ? m : "get");
            r->in_form = r->forms++;
        }
        break;
    case K_HEAD:
        end_block(r);
        r->head = t->arg;
        break;
    case K_LIST:
        end_block(r);
        r->list = t->arg;
        r->depth++;
        break;
    case K_ITEM:
        end_block(r);
        if (!r->list)                           /* (an item of no list: of one all the same) */
            r->list = 1, r->depth++;
        r->item = r->list;
        break;
    case K_TERM:
        end_block(r);
        if (t->arg == 1)
            r->style |= HTML_BOLD;
        else
            r->depth++;
        break;
    case K_QUOTE:
        end_block(r);
        r->quote++;
        r->depth++;
        break;
    case K_PRE:
        end_block(r);
        r->pre = 1;
        break;
    case K_BREAK:
        if (r->open && !r->row) {
            if (r->nbuf == sizeof r->buf)
                flush(r);
            r->buf[r->nbuf++] = '\n';
            flush(r);
            r->space = 0;
        } else if (r->row) {
            r->space = r->open;
        }
        break;
    case K_RULE:
        end_block(r);
        if (r->s->block && !r->row)
            r->s->block(r->s->ctx, HTML_RULE, 0);
        break;
    case K_IMAGE: {
        const char *alt = attr_of(a, na, "alt"), *w = attr_of(a, na, "width"), *h = attr_of(a, na, "height");
        whole(r, attr_of(a, na, "src"), url, sizeof url);
        if (r->s->image && url[0] && !r->row) {
            end_block(r);
            r->s->image(r->s->ctx, url, alt ? alt : "", w ? atoi(w) : 0, h ? atoi(h) : 0, r->in_link ? r->link : NULL);
        } else if (alt && alt[0]) {             /* (in a table, or with no address: the words that stand for it) */
            put(r, "[", 1), put(r, alt, (unsigned)strlen(alt)), put(r, "]", 1);
        }
        break;
    }
    case K_TABLE:
        flush(r);
        r->row = 0, r->open = 0, r->space = 0;
        break;
    case K_ROW:
        flush(r);
        if (r->s->block)
            r->s->block(r->s->ctx, HTML_ROW, 0);
        r->row = 1, r->cells = 0, r->open = 1, r->cell_text = 0, r->space = 0;
        break;
    case K_CELL:
        flush(r);
        if (!r->row) {                          /* (a cell of no row: of one all the same) */
            r->open = 0;
            if (r->s->block)
                r->s->block(r->s->ctx, HTML_ROW, 0);
            r->row = 1, r->cells = 0, r->open = 1;
        }
        if (r->cells++ && r->s->cell)
            r->s->cell(r->s->ctx);
        r->space = 0, r->cell_text = 0;
        if (t->arg)
            r->style |= HTML_BOLD;
        break;
    case K_TITLE:
        r->in_title = 1, r->ntitle = 0;
        break;
    case K_BASE:
        if (attr_of(a, na, "href")) {
            whole(r, attr_of(a, na, "href"), url, sizeof url);
            if (url[0])
                snprintf(r->base, sizeof r->base, "%s", url);
        }
        break;
    case K_FIELD: {
        static const char *const kinds[] = { "input", "textarea", "select", "button" };
        const char *type = attr_of(a, na, "type"), *value = attr_of(a, na, "value"), *name = attr_of(a, na, "name");
        const char *kind = t->arg == 0 && type ? type : kinds[t->arg], *ph = attr_of(a, na, "placeholder");
        int forming = r->s->form && r->in_form >= 0;
        if (!strcmp(kind, "hidden")) {
            if (r->s->field && forming)         /* (a form's hidden field is sent with it) */
                r->s->field(r->s->ctx, kind, name ? name : "", value ? value : "");
            break;
        }
        if (r->s->field && (!r->s->form || forming))  /* (a sink of forms: a form's fields alone) */
            r->s->field(r->s->ctx, kind, name ? name : "", value && value[0] ? value : "");
        if (t->arg == 1 || t->arg == 2)
            r->field_skip = 1;
        if (t->arg != 3) {                      /* (a button's own words are in it; the others are shown by a word) */
            char word[80], keep[600];
            int was_in = r->in_link;
            int pressed = !strcmp(kind, "submit") || !strcmp(kind, "button") || !strcmp(kind, "reset") || !strcmp(kind, "image");
            snprintf(word, sizeof word, "[%.60s]", pressed ? (value && value[0] ? value : kind)
                     : forming && ph && ph[0] ? ph : forming && name && name[0] ? name : kind);
            r->field_skip = 0;
            if (forming) {                      /* (phase F) the word a link to the form's dialog */
                flush(r);
                memcpy(keep, r->link, sizeof keep);
                snprintf(r->link, sizeof r->link, "about:form?%d", r->in_form);
                r->in_link = 1;
                put(r, word, (unsigned)strlen(word));
                flush(r);
                memcpy(r->link, keep, sizeof keep);
                r->in_link = was_in;
            } else {
                put(r, word, (unsigned)strlen(word));
            }
            r->field_skip = t->arg == 1 || t->arg == 2;
        }
        break;
    }
    default:
        break;
    }
}

static void end_tag(struct reader *r, int tag)
{
    int at = find_open(r, tag), i;
    flush(r);
    if (!strcmp(tags[tag].name, "form"))
        r->in_form = -1;
    if (at < 0) {
        if (tags[tag].kind == K_BLOCK && !strcmp(tags[tag].name, "p"))  /* ("</p>" with no "<p>": a paragraph's end all the same) */
            end_block(r);
        return;
    }
    /* (an end tag does not reach out of a table's cell for what was opened outside it) */
    for (i = r->n - 1; i > at; i--)
        if (tags[r->stack[i].tag].kind == K_CELL && tags[tag].kind != K_ROW && tags[tag].kind != K_TABLE && tags[tag].kind != K_CELL)
            return;
    while (r->n > at)
        pop(r);
}

/* ---- the page's characters */

static int valid_utf8(const unsigned char *p, unsigned long n)
{
    unsigned long i = 0;
    while (i < n) {
        unsigned c = p[i], more = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 9, k;
        if (more == 9)
            return 0;
        if (more && i + more >= n)              /* (a character cut off by the page's end: no proof against it) */
            return 1;
        for (k = 1; k <= more; k++)
            if ((p[i + k] & 0xC0) != 0x80)
                return 0;
        i += more + 1;
    }
    return 1;
}

char *html_latin_utf8(const char *bytes, unsigned long n, unsigned long *len)
{
    char *out = malloc(3 * n + 1);
    unsigned long i, o = 0;
    if (!out)
        return NULL;
    for (i = 0; i < n; i++) {
        unsigned c = (unsigned char)bytes[i];
        o += utf8(c >= 0x80 && c < 0xA0 ? high1252[c - 0x80] : c, out + o);
    }
    out[o] = 0;
    *len = o;
    return out;
}

static int begins(const char *p, const char *end, const char *word)    /* whatever the letters' case */
{
    for (; *word; word++, p++)
        if (p >= end || lower((unsigned char)*p) != *word)
            return 0;
    return 1;
}

static int says(const char *charset, const char *what)  /* a charset's name has that in it, whatever the letters' case */
{
    size_t n = strlen(what), i, k;
    for (i = 0; charset && charset[i]; i++) {
        for (k = 0; k < n && lower((unsigned char)charset[i + k]) == what[k]; k++)
            ;
        if (k == n)
            return 1;
    }
    return 0;
}

/* ---- the page */

void html_read(const char *bytes, unsigned long n, const char *charset, const char *base, const struct html_sink *sink)
{
    static const struct html_sink none;
    struct reader *r = calloc(1, sizeof *r);
    char *converted = NULL;
    const char *p, *end;
    char meta[64] = "";
    if (!r)
        return;
    r->s = sink ? sink : &none;
    r->colour = r->told_colour = -1;
    r->in_form = -1;
    snprintf(r->base, sizeof r->base, "%s", base ? base : "");
    /* Its characters: as its sender said; else as its own <meta> says in its first bytes; else UTF-8 if its bytes
       are that, and Latin if not. A mark at its start (EF BB BF) settles it. */
    if (n >= 3 && !memcmp(bytes, "\xEF\xBB\xBF", 3)) {
        bytes += 3, n -= 3;
        charset = "utf-8";
    } else if (!charset || !charset[0]) {
        unsigned long look = n < 2048 ? n : 2048, i;
        for (i = 0; i + 8 < look && !meta[0]; i++)
            if (begins(bytes + i, bytes + look, "charset")) {
                unsigned long k = i + 7;
                unsigned m = 0;
                while (k < look && (blank(bytes[k]) || bytes[k] == '=' || bytes[k] == '"' || bytes[k] == '\''))
                    k++;
                for (; k < look && m < sizeof meta - 1 && bytes[k] != '"' && bytes[k] != '\'' && bytes[k] != '>' && !blank(bytes[k])
                     && bytes[k] != ';'; k++)
                    meta[m++] = bytes[k];
                meta[m] = 0;
            }
        charset = meta;
    }
    if (says(charset, "8859-1") || says(charset, "1252") || says(charset, "latin1") || says(charset, "ascii")
        || (!says(charset, "utf") && !valid_utf8((const unsigned char *)bytes, n))) {
        unsigned long len = 0;
        if ((converted = html_latin_utf8(bytes, n, &len)) != NULL)
            bytes = converted, n = len;
    }
    p = bytes, end = bytes + n;
    while (p < end) {
        if (*p == '<' && p + 1 < end) {
            const char *q = p + 1;
            int closing = *q == '/';
            if (*q == '!' || *q == '?') {               /* a comment, a doctype, an instruction: passed over */
                if (end - q >= 3 && q[1] == '-' && q[2] == '-') {
                    const char *c = q + 3;
                    while (c + 2 < end && !(c[0] == '-' && c[1] == '-' && c[2] == '>'))
                        c++;
                    p = c + 2 < end ? c + 3 : end;
                } else {
                    while (q < end && *q != '>')
                        q++;
                    p = q < end ? q + 1 : end;
                }
                continue;
            }
            q += closing;
            if (q < end && ((lower((unsigned char)*q) >= 'a' && lower((unsigned char)*q) <= 'z'))) {
                struct attr a[12];
                char name[16];
                int na = 0, tag = -1, self = 0, i;
                unsigned k = 0;
                for (; q < end && !blank(*q) && *q != '>' && *q != '/'; q++)
                    if (k < sizeof name - 1)
                        name[k++] = (char)lower((unsigned char)*q);
                name[k] = 0;
                for (i = 0; i < NTAGS; i++)
                    if (!strcmp(tags[i].name, name)) {
                        tag = i;
                        break;
                    }
                /* its attributes: name, name=value, name="value", name='value' */
                for (;;) {
                    struct attr *one = na < 12 ? &a[na] : NULL;
                    const char *v0, *v1;
                    char quote;
                    while (q < end && (blank(*q) || *q == '/')) {
                        self = *q == '/';
                        q++;
                    }
                    if (q >= end || *q == '>')
                        break;
                    self = 0;
                    for (k = 0; q < end && !blank(*q) && *q != '=' && *q != '>' && *q != '/'; q++)
                        if (one && k < sizeof one->name - 1)
                            one->name[k++] = (char)lower((unsigned char)*q);
                        else if (k >= sizeof one->name - 1)
                            one = NULL;                 /* (a name too long is no attribute known here) */
                    if (one)
                        one->name[k] = 0, one->value[0] = 0;
                    while (q < end && blank(*q))
                        q++;
                    if (q < end && *q == '=') {
                        q++;
                        while (q < end && blank(*q))
                            q++;
                        quote = q < end && (*q == '"' || *q == '\'') ? *q++ : 0;
                        v0 = q;
                        while (q < end && (quote ? *q != quote : !blank(*q) && *q != '>'))
                            q++;
                        v1 = q;
                        if (quote && q < end)
                            q++;
                        if (one)
                            unreference(v0, v1, one->value, sizeof one->value);
                    }
                    if (one && one->name[0])
                        na++;
                }
                p = q < end ? q + 1 : end;
                if (tag < 0)
                    continue;                           /* an element not known: as if its tags were not there */
                if (closing) {
                    end_tag(r, tag);
                } else {
                    start_tag(r, tag, a, na, self);
                    if (tags[tag].kind == K_SKIP || (tags[tag].kind == K_PRE && !strcmp(name, "plaintext"))) {
                        /* what is in it is not the page's text: to its end tag (plaintext: to the page's end, as text) */
                        const char *c = p;
                        size_t nl = strlen(name);
                        if (tags[tag].kind == K_PRE) {
                            put(r, p, (unsigned)(end - p));
                            p = end;
                            continue;
                        }
                        for (; c < end; c++)
                            if (c[0] == '<' && c + 1 < end && c[1] == '/' && (size_t)(end - c) > nl + 2) {
                                size_t m;
                                for (m = 0; m < nl && lower((unsigned char)c[2 + m]) == name[m]; m++)
                                    ;
                                if (m == nl && (blank(c[2 + nl]) || c[2 + nl] == '>' || c[2 + nl] == '/'))
                                    break;
                            }
                        p = c;                          /* (at its end tag, which closes it; or the page's end) */
                    } else if (tags[tag].kind == K_PRE && p < end && *p == '\n') {
                        p++;                            /* (a line's end straight after <pre> is not its text) */
                    }
                }
                continue;
            }
        }
        if (*p == '&') {
            char u[8];
            unsigned len = 0, used = reference(p + 1, end, u, &len);
            if (used) {
                if (len == 2 && (unsigned char)u[0] == 0xC2 && (unsigned char)u[1] == 0xA0 && !r->pre) {
                    /* a hard blank: a blank that is kept (it is text: blanks round it are still made one) */
                    if (!r->hidden && !r->in_title && !r->field_skip) {
                        begin_block(r);
                        if (r->nbuf + 2 >= sizeof r->buf)
                            flush(r);
                        if (r->space)
                            r->buf[r->nbuf++] = ' ', r->space = 0;
                        r->buf[r->nbuf++] = ' ';
                    }
                } else {
                    put(r, u, len);
                }
                p += 1 + used;
                continue;
            }
        }
        {
            const char *q = p + 1;                      /* text: up to the next thing that may be a tag or a reference */
            while (q < end && *q != '<' && *q != '&')
                q++;
            put(r, p, (unsigned)(q - p));
            p = q;
        }
    }
    while (r->n)
        pop(r);
    end_block(r);
    free(converted);
    free(r);
}
