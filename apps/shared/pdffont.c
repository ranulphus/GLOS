/* A PDF's fonts (pdffont.h).
 *
 * A font in a PDF is a dictionary: what kind it is ("Subtype"), its name, how wide each code is ("Widths", or a
 * composite font's "W"), which character each code stands for (an "Encoding": one of three standard tables,
 * changed by "Differences" that name glyphs; and "ToUnicode", a small program of ranges), and maybe the font
 * itself as a stream ("FontFile2": TrueType; "FontFile": Type 1; "FontFile3": CFF, bare or as an OpenType
 * font's table). A simple font's codes are a byte; a composite font's ("Type0") are two, and are glyph numbers
 * of the font it is made from (straight, or by a table), or a CFF font's own numbers for its glyphs (CIDs).
 *
 * Here: an embedded font gives the outlines (TrueType: apps/shared/ttf.c; Type 1 and CFF: psfont.c). A simple
 * font of a PostScript kind finds a code's glyph by the name the Differences give the code, else by the
 * character the named table gives it, else (no table named) by the font's own encoding. For a font the PDF
 * doesn't carry, or one nothing could be made of, a stand-in face draws: chosen by the font's name and flags
 * (mono, serif or sans; bold; italic), the glyph of the code's character, squeezed or stretched to the width
 * the PDF says where that is near its own. Not yet: Type 3 fonts, the Symbol font's own letters. */
#include <stdlib.h>
#include <string.h>

#include "pdffont.h"
#include "psfont.h"

struct range {                          /* codes first to last: widths (a composite font's), or characters */
    unsigned first, last;
    double w;
    unsigned long to;
};

struct pdf_font {
    int composite, symbolic, has_widths;
    double width[256];                  /* a simple font's, by code: thousandths; below 0: not said */
    double missing;                     /* ... of a code it has no width for; a composite font's default */
    unsigned short uni[256];            /* a simple font's characters by its encoding (0: none) */
    struct range *w;                    /* a composite font's widths */
    unsigned nw;
    struct range *to;                   /* ToUnicode */
    unsigned nto;
    unsigned char *file;                /* an embedded font's bytes (TrueType, CFF) */
    struct ttf ttf;
    int has_ttf;
    struct psfont *ps;                  /* an embedded font of a PostScript kind */
    char *diff[256];                    /* a simple font's glyph names by code, where its Differences give one */
    int own_codes;                      /* its Encoding names no table: the font's own encoding is the one */
    unsigned char *cid2gid;             /* a composite font's glyph of each code (two bytes each), or NULL: itself */
    unsigned long ncid2gid;
    const struct ttf *stand;            /* the stand-in face, or NULL */
};

/* ---- characters by their names and by the standard tables */

static const struct { const char *name; unsigned short u; } names[] = {
    { "space", 0x20 }, { "exclam", 0x21 }, { "quotedbl", 0x22 }, { "numbersign", 0x23 }, { "dollar", 0x24 }, { "percent", 0x25 },
    { "ampersand", 0x26 }, { "quotesingle", 0x27 }, { "parenleft", 0x28 }, { "parenright", 0x29 }, { "asterisk", 0x2A },
    { "plus", 0x2B }, { "comma", 0x2C }, { "hyphen", 0x2D }, { "period", 0x2E }, { "slash", 0x2F }, { "zero", 0x30 },
    { "one", 0x31 }, { "two", 0x32 }, { "three", 0x33 }, { "four", 0x34 }, { "five", 0x35 }, { "six", 0x36 }, { "seven", 0x37 },
    { "eight", 0x38 }, { "nine", 0x39 }, { "colon", 0x3A }, { "semicolon", 0x3B }, { "less", 0x3C }, { "equal", 0x3D },
    { "greater", 0x3E }, { "question", 0x3F }, { "at", 0x40 }, { "bracketleft", 0x5B }, { "backslash", 0x5C },
    { "bracketright", 0x5D }, { "asciicircum", 0x5E }, { "underscore", 0x5F }, { "grave", 0x60 }, { "braceleft", 0x7B },
    { "bar", 0x7C }, { "braceright", 0x7D }, { "asciitilde", 0x7E }, { "nbspace", 0xA0 }, { "exclamdown", 0xA1 }, { "cent", 0xA2 },
    { "sterling", 0xA3 }, { "currency", 0xA4 }, { "yen", 0xA5 }, { "brokenbar", 0xA6 }, { "section", 0xA7 }, { "dieresis", 0xA8 },
    { "copyright", 0xA9 }, { "ordfeminine", 0xAA }, { "guillemotleft", 0xAB }, { "logicalnot", 0xAC }, { "sfthyphen", 0xAD },
    { "registered", 0xAE }, { "macron", 0xAF }, { "degree", 0xB0 }, { "plusminus", 0xB1 }, { "twosuperior", 0xB2 },
    { "threesuperior", 0xB3 }, { "acute", 0xB4 }, { "mu", 0xB5 }, { "paragraph", 0xB6 }, { "periodcentered", 0xB7 },
    { "cedilla", 0xB8 }, { "onesuperior", 0xB9 }, { "ordmasculine", 0xBA }, { "guillemotright", 0xBB }, { "onequarter", 0xBC },
    { "onehalf", 0xBD }, { "threequarters", 0xBE }, { "questiondown", 0xBF }, { "Agrave", 0xC0 }, { "Aacute", 0xC1 },
    { "Acircumflex", 0xC2 }, { "Atilde", 0xC3 }, { "Adieresis", 0xC4 }, { "Aring", 0xC5 }, { "AE", 0xC6 }, { "Ccedilla", 0xC7 },
    { "Egrave", 0xC8 }, { "Eacute", 0xC9 }, { "Ecircumflex", 0xCA }, { "Edieresis", 0xCB }, { "Igrave", 0xCC }, { "Iacute", 0xCD },
    { "Icircumflex", 0xCE }, { "Idieresis", 0xCF }, { "Eth", 0xD0 }, { "Ntilde", 0xD1 }, { "Ograve", 0xD2 }, { "Oacute", 0xD3 },
    { "Ocircumflex", 0xD4 }, { "Otilde", 0xD5 }, { "Odieresis", 0xD6 }, { "multiply", 0xD7 }, { "Oslash", 0xD8 },
    { "Ugrave", 0xD9 }, { "Uacute", 0xDA }, { "Ucircumflex", 0xDB }, { "Udieresis", 0xDC }, { "Yacute", 0xDD }, { "Thorn", 0xDE },
    { "germandbls", 0xDF }, { "agrave", 0xE0 }, { "aacute", 0xE1 }, { "acircumflex", 0xE2 }, { "atilde", 0xE3 },
    { "adieresis", 0xE4 }, { "aring", 0xE5 }, { "ae", 0xE6 }, { "ccedilla", 0xE7 }, { "egrave", 0xE8 }, { "eacute", 0xE9 },
    { "ecircumflex", 0xEA }, { "edieresis", 0xEB }, { "igrave", 0xEC }, { "iacute", 0xED }, { "icircumflex", 0xEE },
    { "idieresis", 0xEF }, { "eth", 0xF0 }, { "ntilde", 0xF1 }, { "ograve", 0xF2 }, { "oacute", 0xF3 }, { "ocircumflex", 0xF4 },
    { "otilde", 0xF5 }, { "odieresis", 0xF6 }, { "divide", 0xF7 }, { "oslash", 0xF8 }, { "ugrave", 0xF9 }, { "uacute", 0xFA },
    { "ucircumflex", 0xFB }, { "udieresis", 0xFC }, { "yacute", 0xFD }, { "thorn", 0xFE }, { "ydieresis", 0xFF },
    { "OE", 0x152 }, { "oe", 0x153 }, { "Scaron", 0x160 }, { "scaron", 0x161 }, { "Ydieresis", 0x178 }, { "Zcaron", 0x17D },
    { "zcaron", 0x17E }, { "florin", 0x192 }, { "circumflex", 0x2C6 }, { "tilde", 0x2DC }, { "endash", 0x2013 },
    { "emdash", 0x2014 }, { "quoteleft", 0x2018 }, { "quoteright", 0x2019 }, { "quotesinglbase", 0x201A },
    { "quotedblleft", 0x201C }, { "quotedblright", 0x201D }, { "quotedblbase", 0x201E }, { "dagger", 0x2020 },
    { "daggerdbl", 0x2021 }, { "bullet", 0x2022 }, { "ellipsis", 0x2026 }, { "perthousand", 0x2030 },
    { "guilsinglleft", 0x2039 }, { "guilsinglright", 0x203A }, { "Euro", 0x20AC }, { "trademark", 0x2122 }, { "minus", 0x2212 },
    { "fi", 0xFB01 }, { "fl", 0xFB02 }, { "dotlessi", 0x131 }, { "Lslash", 0x141 }, { "lslash", 0x142 }, { "breve", 0x2D8 },
    { "dotaccent", 0x2D9 }, { "ring", 0x2DA }, { "ogonek", 0x2DB }, { "hungarumlaut", 0x2DD }, { "caron", 0x2C7 },
    { "fraction", 0x2044 }, { "Omega", 0x3A9 }, { "pi", 0x3C0 }, { "partialdiff", 0x2202 }, { "Delta", 0x2206 },
    { "product", 0x220F }, { "summation", 0x2211 }, { "radical", 0x221A }, { "infinity", 0x221E }, { "integral", 0x222B },
    { "approxequal", 0x2248 }, { "notequal", 0x2260 }, { "lessequal", 0x2264 }, { "greaterequal", 0x2265 }, { "lozenge", 0x25CA } };

/* Windows' table (WinAnsiEncoding) from 80h to 9Fh; the rest of it is Latin-1 */
static const unsigned short win[32] = {
    0x20AC, 0, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0, 0x17D, 0,
    0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0, 0x17E, 0x178 };
/* the Macintosh's (MacRomanEncoding) from 80h */
static const unsigned short mac[128] = {
    0xC4, 0xC5, 0xC7, 0xC9, 0xD1, 0xD6, 0xDC, 0xE1, 0xE0, 0xE2, 0xE4, 0xE3, 0xE5, 0xE7, 0xE9, 0xE8,
    0xEA, 0xEB, 0xED, 0xEC, 0xEE, 0xEF, 0xF1, 0xF3, 0xF2, 0xF4, 0xF6, 0xF5, 0xFA, 0xF9, 0xFB, 0xFC,
    0x2020, 0xB0, 0xA2, 0xA3, 0xA7, 0x2022, 0xB6, 0xDF, 0xAE, 0xA9, 0x2122, 0xB4, 0xA8, 0x2260, 0xC6, 0xD8,
    0x221E, 0xB1, 0x2264, 0x2265, 0xA5, 0xB5, 0x2202, 0x2211, 0x220F, 0x3C0, 0x222B, 0xAA, 0xBA, 0x3A9, 0xE6, 0xF8,
    0xBF, 0xA1, 0xAC, 0x221A, 0x192, 0x2248, 0x2206, 0xAB, 0xBB, 0x2026, 0xA0, 0xC0, 0xC3, 0xD5, 0x152, 0x153,
    0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0xF7, 0x25CA, 0xFF, 0x178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0x2021, 0xB7, 0x201A, 0x201E, 0x2030, 0xC2, 0xCA, 0xC1, 0xCB, 0xC8, 0xCD, 0xCE, 0xCF, 0xCC, 0xD3, 0xD4,
    0, 0xD2, 0xDA, 0xDB, 0xD9, 0x131, 0x2C6, 0x2DC, 0xAF, 0x2D8, 0x2D9, 0x2DA, 0xB8, 0x2DD, 0x2DB, 0x2C7 };
/* Adobe's own (StandardEncoding) from A0h */
static const unsigned short std[96] = {
    0, 0xA1, 0xA2, 0xA3, 0x2044, 0xA5, 0x192, 0xA7, 0xA4, 0x27, 0x201C, 0xAB, 0x2039, 0x203A, 0xFB01, 0xFB02,
    0, 0x2013, 0x2020, 0x2021, 0xB7, 0, 0xB6, 0x2022, 0x201A, 0x201E, 0x201D, 0xBB, 0x2026, 0x2030, 0, 0xBF,
    0, 0x60, 0xB4, 0x2C6, 0x2DC, 0xAF, 0x2D8, 0x2D9, 0xA8, 0, 0x2DA, 0xB8, 0, 0x2DD, 0x2DB, 0x2C7,
    0x2014, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0xC6, 0, 0xAA, 0, 0, 0, 0, 0x141, 0xD8, 0x152, 0xBA, 0, 0, 0, 0,
    0, 0xE6, 0, 0, 0, 0x131, 0, 0, 0x142, 0xF8, 0x153, 0xDF, 0, 0, 0, 0 };

static unsigned by_name(const char *n)
{
    unsigned i;
    if (!n[1] && ((n[0] >= 'A' && n[0] <= 'Z') || (n[0] >= 'a' && n[0] <= 'z')))
        return (unsigned char)n[0];
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(names[i].name, n))
            return names[i].u;
    if (!strncmp(n, "uni", 3) && strlen(n) >= 7) {      /* uniXXXX */
        unsigned v = 0, k;
        for (k = 3; k < 7; k++) {
            int c = n[k];
            v = v << 4 | (unsigned)(c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0);
        }
        return v;
    }
    return 0;
}

static void table(unsigned short *uni, int which)       /* 0 Windows', 1 the Macintosh's, 2 Adobe's own */
{
    unsigned c;
    memset(uni, 0, 256 * sizeof uni[0]);
    for (c = 0x20; c < 0x7F; c++)
        uni[c] = (unsigned short)c;
    for (c = 0x80; c < 0x100; c++)
        uni[c] = which == 1 ? mac[c - 0x80] : which == 2 ? (c >= 0xA0 ? std[c - 0xA0] : 0) : c < 0xA0 ? win[c - 0x80] : (unsigned short)c;
    if (which == 2)
        uni[0x27] = 0x2019, uni[0x60] = 0x2018;
}

/* ---- the stand-in faces */

static pdf_face_fn faces;
static struct { struct ttf t; int state; } face[3][2][2];       /* state: 0 not asked for yet, 1 there, 2 none */

void pdf_font_faces(pdf_face_fn fn)
{
    faces = fn;
    memset(face, 0, sizeof face);
}

static const struct ttf *stand_in(int family, int bold, int italic)
{
    int tries;
    for (tries = 0; tries < 4; tries++) {       /* (the face asked for; else upright; else not bold; else the sans) */
        int f = tries == 3 ? 0 : family, b = tries >= 2 ? 0 : bold, i = tries >= 1 ? 0 : italic;
        if (!face[f][b][i].state) {
            unsigned long len = 0;
            const unsigned char *bytes = faces ? faces(f, b, i, &len) : NULL;
            face[f][b][i].state = bytes && ttf_open(&face[f][b][i].t, bytes, len) == 0 ? 1 : 2;
        }
        if (face[f][b][i].state == 1)
            return &face[f][b][i].t;
    }
    return NULL;
}

static int has(const char *name, const char *part)      /* part is in name, whatever the letters' case */
{
    size_t n = strlen(part), i, k;
    for (i = 0; name[i]; i++) {
        for (k = 0; k < n && name[i + k]; k++) {
            int a = name[i + k], b = part[k];
            a = a >= 'A' && a <= 'Z' ? a + 32 : a;
            if (a != b)
                break;
        }
        if (k == n)
            return 1;
    }
    return 0;
}

/* ---- a font opened */

static int by_first(const void *a, const void *b)
{
    const struct range *x = a, *y = b;
    return x->first < y->first ? -1 : x->first > y->first;
}

static int range_add(struct range **list, unsigned *n, unsigned first, unsigned last, double w, unsigned long to)
{
    struct range *r;
    if (*n >= 60000)
        return -1;
    if (!(*n & (*n - 1)) && (*n >= 16 || !*n)) {
        if (!(r = realloc(*list, (*n ? *n * 2 : 16) * sizeof *r)))
            return -1;
        *list = r;
    }
    r = &(*list)[(*n)++];
    r->first = first, r->last = last, r->w = w, r->to = to;
    return 0;
}

static const struct range *range_of(const struct range *list, unsigned n, unsigned code)
{
    unsigned lo = 0, hi = n;
    while (lo < hi) {                           /* the last range that begins at or before the code */
        unsigned mid = lo + (hi - lo) / 2;
        if (list[mid].first <= code)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo && code <= list[lo - 1].last ? &list[lo - 1] : NULL;
}

static unsigned long utf16(const char *s, unsigned n)   /* a ToUnicode's answer: its first character */
{
    unsigned long a = n >= 2 ? (unsigned long)((unsigned char)s[0] << 8 | (unsigned char)s[1]) : n == 1 ? (unsigned char)s[0] : 0;
    if (a >= 0xD800 && a < 0xDC00 && n >= 4) {
        unsigned long b = (unsigned long)((unsigned char)s[2] << 8 | (unsigned char)s[3]);
        if (b >= 0xDC00 && b < 0xE000)
            a = 0x10000 + ((a - 0xD800) << 10) + (b - 0xDC00);
    }
    return a;
}

/* An OpenType font's table of that name (a CFF font may come as one's "CFF " table), or NULL. */
static const unsigned char *sfnt_table(const unsigned char *d, unsigned long n, const char *tag, unsigned long *len)
{
    unsigned count = n >= 12 ? (unsigned)d[4] << 8 | d[5] : 0, i;
    for (i = 0; i < count && 12 + 16ul * (i + 1) <= n; i++) {
        const unsigned char *e = d + 12 + 16ul * i;
        unsigned long at = (unsigned long)e[8] << 24 | (unsigned long)e[9] << 16 | (unsigned long)e[10] << 8 | e[11];
        unsigned long size = (unsigned long)e[12] << 24 | (unsigned long)e[13] << 16 | (unsigned long)e[14] << 8 | e[15];
        if (!memcmp(e, tag, 4)) {
            *len = size;
            return at <= n && size <= n - at ? d + at : NULL;
        }
    }
    return NULL;
}

static char *copy_of(const char *s)
{
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (c)
        memcpy(c, s, n);
    return c;
}

static unsigned code_of(const struct pdf_obj *o)        /* a code written as a string of one or two bytes */
{
    unsigned n = 0;
    const char *s = pdf_string(o, &n);
    return n >= 2 ? (unsigned)((unsigned char)s[0] << 8 | (unsigned char)s[1]) : n == 1 ? (unsigned char)s[0] : 0;
}

static void to_unicode(struct pdf *d, struct pdf_font *f, struct pdf_obj *stream)
{
    unsigned long len = 0;
    unsigned char *b = pdf_stream(d, stream, &len, NULL, NULL);
    struct pdf_words w = { b, b + len, d };
    struct pdf_obj *arg[4], *o;
    char op[16];
    int n = 0, kind, i, in = 0;                 /* in: 1 within bfchar, 2 within bfrange */
    if (!b)
        return;
    while ((kind = pdf_word(&w, &o, op)) != 0) {
        if (kind == 2) {
            in = !strcmp(op, "beginbfchar") ? 1 : !strcmp(op, "beginbfrange") ? 2 : !strncmp(op, "endbf", 5) ? 0 : in;
            for (i = 0; i < n; i++)
                pdf_free(arg[i]);
            n = 0;
            continue;
        }
        arg[n++] = o;
        if (in == 1 && n == 2) {                /* <code> <character> */
            unsigned un = 0;
            const char *us = pdf_string(arg[1], &un);
            range_add(&f->to, &f->nto, code_of(arg[0]), code_of(arg[0]), 0, utf16(us, un));
        } else if (in == 2 && n == 3) {         /* <first> <last> <character of the first>, or [one each] */
            unsigned first = code_of(arg[0]), last = code_of(arg[1]), un = 0, k;
            if (pdf_is(arg[2], PDF_ARRAY)) {
                for (k = 0; k < arg[2]->n && first + k <= last; k++) {
                    const char *us = pdf_string(arg[2]->u.item[k], &un);
                    range_add(&f->to, &f->nto, first + k, first + k, 0, utf16(us, un));
                }
            } else {
                const char *us = pdf_string(arg[2], &un);
                if (last >= first && last - first < 65536)
                    range_add(&f->to, &f->nto, first, last, 0, utf16(us, un));
            }
        }
        if ((in == 1 && n == 2) || (in == 2 && n == 3) || n == 4 || !in) {
            for (i = 0; i < n; i++)
                pdf_free(arg[i]);
            n = 0;
        }
    }
    for (i = 0; i < n; i++)
        pdf_free(arg[i]);
    free(b);
    if (f->nto > 1)
        qsort(f->to, f->nto, sizeof f->to[0], by_first);
}

struct pdf_font *pdf_font_open(struct pdf *d, struct pdf_obj *dict)
{
    struct pdf_font *f = calloc(1, sizeof *f);
    struct pdf_obj *desc, *enc, *widths, *o, *self = dict;
    const char *name, *sub = pdf_name(pdf_get(d, dict, "Subtype"));
    long flags;
    unsigned i;
    int family, bold, italic;
    if (!f)
        return NULL;
    f->composite = !strcmp(sub, "Type0");
    if (f->composite && pdf_is(pdf_item(d, pdf_get(d, dict, "DescendantFonts"), 0), PDF_DICT))
        self = pdf_item(d, pdf_get(d, dict, "DescendantFonts"), 0);     /* the font it is made from */
    name = pdf_name(pdf_get(d, self, "BaseFont"));
    if (strlen(name) > 7 && name[6] == '+')     /* ("ABCDEF+Name": a part of a font) */
        name += 7;
    desc = pdf_get(d, self, "FontDescriptor");
    flags = pdf_int(pdf_get(d, desc, "Flags"), 0);
    f->symbolic = (flags & 4) && !(flags & 32);
    f->missing = pdf_num(pdf_get(d, f->composite ? self : desc, f->composite ? "DW" : "MissingWidth"), f->composite ? 1000 : -1);
    for (i = 0; i < 256; i++)
        f->width[i] = -1;
    if (f->composite) {                         /* W: "first [w w ...]" and "first last w", any number of them */
        struct pdf_obj *w = pdf_get(d, self, "W");
        for (i = 0; pdf_is(w, PDF_ARRAY) && i + 1 < w->n; ) {
            unsigned first = (unsigned)pdf_int(pdf_item(d, w, i), 0), k;
            struct pdf_obj *second = pdf_item(d, w, i + 1);
            if (pdf_is(second, PDF_ARRAY)) {
                for (k = 0; k < second->n; k++)
                    range_add(&f->w, &f->nw, first + k, first + k, pdf_num(pdf_item(d, second, k), 1000), 0);
                i += 2;
            } else {
                unsigned last = (unsigned)pdf_int(second, 0);
                if (i + 2 < w->n && last >= first)
                    range_add(&f->w, &f->nw, first, last, pdf_num(pdf_item(d, w, i + 2), 1000), 0);
                i += 3;
            }
        }
        if (f->nw > 1)
            qsort(f->w, f->nw, sizeof f->w[0], by_first);
        o = pdf_get(d, self, "CIDToGIDMap");
        if (pdf_is(o, PDF_DICT) && o->stream)
            f->cid2gid = pdf_stream(d, o, &f->ncid2gid, NULL, NULL);
    } else {
        long first = pdf_int(pdf_get(d, dict, "FirstChar"), 0);
        widths = pdf_get(d, dict, "Widths");
        f->has_widths = pdf_is(widths, PDF_ARRAY) && widths->n > 0;
        for (i = 0; pdf_is(widths, PDF_ARRAY) && i < widths->n; i++)
            if (first + (long)i >= 0 && first + (long)i < 256)
                f->width[first + (long)i] = pdf_num(pdf_item(d, widths, i), -1);
        /* its characters: the table its Encoding names (or the kind's own), then the Differences */
        enc = pdf_get(d, dict, "Encoding");
        o = pdf_is(enc, PDF_DICT) ? pdf_get(d, enc, "BaseEncoding") : enc;
        table(f->uni, pdf_name_is(o, "MacRomanEncoding") ? 1 : pdf_name_is(o, "WinAnsiEncoding") ? 0
              : pdf_name_is(o, "StandardEncoding") || (!o && !strcmp(sub, "Type1")) ? 2 : 0);
        f->own_codes = !pdf_name_is(o, "MacRomanEncoding") && !pdf_name_is(o, "WinAnsiEncoding") && !pdf_name_is(o, "StandardEncoding");
        if (pdf_is(enc, PDF_DICT)) {
            struct pdf_obj *diff = pdf_get(d, enc, "Differences");
            unsigned code = 0;
            for (i = 0; pdf_is(diff, PDF_ARRAY) && i < diff->n; i++) {
                struct pdf_obj *e = pdf_item(d, diff, i);
                if (pdf_is(e, PDF_NUMBER)) {
                    code = (unsigned)pdf_int(e, 0);
                } else if (pdf_is(e, PDF_NAME) && code < 256) {
                    free(f->diff[code]);
                    f->diff[code] = copy_of(pdf_name(e));
                    f->uni[code++] = (unsigned short)by_name(pdf_name(e));
                }
            }
        }
    }
    if ((o = pdf_get(d, dict, "ToUnicode")) != NULL && pdf_is(o, PDF_DICT) && o->stream)
        to_unicode(d, f, o);
    /* the font itself, where the PDF carries one of a kind read here */
    o = pdf_get(d, desc, "FontFile2");
    if (pdf_is(o, PDF_DICT) && o->stream) {
        unsigned long len = 0;
        if ((f->file = pdf_stream(d, o, &len, NULL, NULL)) != NULL && ttf_open(&f->ttf, f->file, len) == 0)
            f->has_ttf = 1;
    } else if (pdf_is(o = pdf_get(d, desc, "FontFile3"), PDF_DICT) && o->stream) {
        unsigned long len = 0, part = 0;        /* a CFF font: bare, or within an OpenType font (which may be TrueType) */
        if ((f->file = pdf_stream(d, o, &len, NULL, NULL)) != NULL) {
            if (len >= 12 && (!memcmp(f->file, "OTTO", 4) || !memcmp(f->file, "\0\1\0\0", 4) || !memcmp(f->file, "true", 4))) {
                const unsigned char *cff = sfnt_table(f->file, len, "CFF ", &part);
                if (cff)
                    f->ps = psfont_cff(cff, part, by_name);
                else if (ttf_open(&f->ttf, f->file, len) == 0)
                    f->has_ttf = 1;
            } else {
                f->ps = psfont_cff(f->file, len, by_name);
            }
        }
    } else if (pdf_is(o = pdf_get(d, desc, "FontFile"), PDF_DICT) && o->stream) {
        unsigned long len = 0;
        unsigned char *b = pdf_stream(d, o, &len, NULL, NULL);
        if (b)
            f->ps = psfont_type1(b, len, by_name);      /* (it keeps a copy) */
        free(b);
    }
    if (f->ps && !f->composite && f->own_codes)         /* its codes' characters: the font's own encoding says */
        for (i = 0; i < 256; i++)
            if (!f->diff[i]) {
                int g = psfont_by_code(f->ps, i);
                f->uni[i] = g >= 0 && psfont_unicode(f->ps, g) < 0x10000 ? (unsigned short)psfont_unicode(f->ps, g) : 0;
            }
    if (!f->has_ttf && !f->ps) {                /* a stand-in, by what the font says of itself */
        family = (flags & 1) || has(name, "courier") || has(name, "mono") || has(name, "typewriter") || has(name, "consol")
                 || !strncmp(name, "CMTT", 4) ? 2
                 : (flags & 2) || has(name, "times") || has(name, "serif") || has(name, "roman") || has(name, "georgia")
                   || has(name, "garamond") || has(name, "palatino") || has(name, "book") || has(name, "minion")
                   || !strncmp(name, "CMR", 3) || !strncmp(name, "CMBX", 4) || !strncmp(name, "CMTI", 4) ? 1 : 0;
        if (has(name, "sans") || has(name, "helvet") || has(name, "arial"))
            family = family == 2 ? 2 : 0;
        bold = (flags & 262144) || has(name, "bold") || has(name, "black") || has(name, "heavy") || has(name, "demi")
               || !strncmp(name, "CMBX", 4);
        italic = (flags & 64) || has(name, "italic") || has(name, "oblique") || !strncmp(name, "CMTI", 4) || !strncmp(name, "CMSL", 4);
        f->stand = stand_in(family, bold, italic);
    }
    return f;
}

void pdf_font_close(struct pdf_font *f)
{
    unsigned i;
    if (!f)
        return;
    for (i = 0; i < 256; i++)
        free(f->diff[i]);
    psfont_close(f->ps);
    free(f->w);
    free(f->to);
    free(f->file);
    free(f->cid2gid);
    free(f);
}

unsigned pdf_font_next(const struct pdf_font *f, const unsigned char *s, unsigned len, unsigned *code)
{
    if (!len)
        return 0;
    if (f->composite && len >= 2) {
        *code = (unsigned)s[0] << 8 | s[1];
        return 2;
    }
    *code = s[0];
    return 1;
}

int pdf_font_is_space(const struct pdf_font *f, unsigned code) { return !f->composite && code == 32; }

unsigned long pdf_font_unicode(const struct pdf_font *f, unsigned code)
{
    const struct range *r = f->nto ? range_of(f->to, f->nto, code) : NULL;
    if (r)
        return r->to + (r->to ? code - r->first : 0);
    return f->composite ? 0 : f->uni[code & 255];
}

static unsigned stand_glyph(const struct pdf_font *f, unsigned code)
{
    unsigned long u = pdf_font_unicode(f, code);
    return f->stand && u ? ttf_by_unicode(f->stand, u) : 0;
}

double pdf_font_width(const struct pdf_font *f, unsigned code)
{
    if (f->composite) {
        const struct range *r = f->nw ? range_of(f->w, f->nw, code) : NULL;
        return r ? r->w : f->missing;
    }
    if (f->width[code & 255] >= 0)
        return f->width[code & 255];
    if (f->has_widths)
        return f->missing >= 0 ? f->missing : 0;
    if (f->stand) {                             /* (an old PDF's standard font, its widths unsaid: the stand-in's) */
        unsigned g = stand_glyph(f, code);
        return g ? ttf_advance(f->stand, g) * 1000.0 / f->stand->em : 500;
    }
    return 500;
}

struct scaled {                         /* a sink that scales what it is given, and hands it on */
    const struct ttf_sink *to;
    double sx, sy;
};

static void sc_move(void *c, double x, double y) { struct scaled *s = c; s->to->move(s->to->ctx, x * s->sx, y * s->sy); }
static void sc_line(void *c, double x, double y) { struct scaled *s = c; s->to->line(s->to->ctx, x * s->sx, y * s->sy); }
static void sc_curve(void *c, double cx, double cy, double x, double y)
{
    struct scaled *s = c;
    s->to->curve(s->to->ctx, cx * s->sx, cy * s->sy, x * s->sx, y * s->sy);
}

static void sc_cubic(void *c, double ax, double ay, double bx, double by, double x, double y)
{
    struct scaled *s = c;
    if (s->to->cubic)
        s->to->cubic(s->to->ctx, ax * s->sx, ay * s->sy, bx * s->sx, by * s->sy, x * s->sx, y * s->sy);
}

int pdf_font_outline(const struct pdf_font *f, unsigned code, const struct ttf_sink *to)
{
    struct scaled s = { to, 1, 1 };
    struct ttf_sink mine = { sc_move, sc_line, sc_curve, &s, sc_cubic };
    unsigned g = 0;
    if (f->has_ttf) {
        if (f->composite) {                     /* the code is the glyph, or the table says which */
            g = f->cid2gid ? (2ul * code + 1 < f->ncid2gid ? (unsigned)f->cid2gid[2 * code] << 8 | f->cid2gid[2 * code + 1] : 0) : code;
        } else {
            unsigned long u = pdf_font_unicode(f, code);
            if (!f->symbolic && u)
                g = ttf_by_unicode(&f->ttf, u);
            if (!g)
                g = ttf_by_symbol(&f->ttf, code);
            if (!g)
                g = ttf_by_mac(&f->ttf, code);
            if (!g && u)
                g = ttf_by_unicode(&f->ttf, u);
        }
        if (!g)
            return -1;
        s.sx = s.sy = 1.0 / f->ttf.em;
        return ttf_outline(&f->ttf, g, &mine);
    }
    if (f->ps) {
        int pg = -1;
        if (f->composite) {                     /* the code is the font's own number for the glyph, or the glyph */
            pg = psfont_is_cid(f->ps) ? psfont_by_cid(f->ps, code) : (int)code;
        } else {
            code &= 255;
            if (f->diff[code])
                pg = psfont_by_name(f->ps, f->diff[code]);
            else if (!f->own_codes && f->uni[code])
                pg = psfont_by_unicode(f->ps, f->uni[code]);
            if (pg < 0 && !f->diff[code])
                pg = psfont_by_code(f->ps, code);
            if (pg < 0 && f->uni[code])
                pg = psfont_by_unicode(f->ps, f->uni[code]);
        }
        return pg < 0 || !to->cubic ? -1 : psfont_outline(f->ps, pg, to);
    }
    if (!f->stand || !(g = stand_glyph(f, code)))
        return -1;
    s.sx = s.sy = 1.0 / f->stand->em;
    {                                           /* as wide as the PDF says, where that is near the stand-in's own */
        double own = ttf_advance(f->stand, g) * 1000.0 / f->stand->em, said = pdf_font_width(f, code);
        if (own > 0 && said > 0 && said / own > 0.6 && said / own < 1.6)
            s.sx *= said / own;
    }
    return ttf_outline(f->stand, g, &mine);
}
