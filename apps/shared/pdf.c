/* PDF files read (pdf.h): objects, cross-reference tables, streams and pages.
 *
 * A PDF is objects (numbers, names, strings, arrays, dictionaries; a dictionary may have a stream of bytes after
 * it), most of them numbered ("12 0 obj ... endobj") and named by others by their numbers ("12 0 R"). Where each
 * numbered object lies is said by cross-reference tables at the file's end ("startxref" says where the newest
 * is; each names the one before it): tables of text, or since PDF 1.5 streams, which can also say that an object
 * lies packed with others in an "object stream". A table's trailer names the catalog ("Root"), which names the
 * tree of pages.
 *
 * Here: the file is read by pieces through the caller's function. An object is read when it is first asked for
 * and kept; pdf_trim lets the kept ones go. A file whose tables are damaged or missing is gone through from its
 * start for "N G obj" and its tables made anew. Written from the specification (ISO 32000-1); whatever the
 * file's bytes are, nothing is read outside what was read from it, and no loop is without an end. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../loader/inflate.h"
#include "pdf.h"

#define MAX_OBJECTS 4000000ul
#define MAX_STREAM (64ul << 20)         /* a stream unpacked at most */
#define MAX_DEPTH 64

struct entry {
    unsigned long at;                   /* where: in the file, or which item of its object stream */
    unsigned stm;                       /* the object stream's number */
    unsigned char kind;                 /* 0: none; 1: in the file; 2: in an object stream */
    struct pdf_obj *obj;                /* read already */
};

struct objstm {                         /* an object stream unpacked: kept while its objects are being read */
    unsigned num, n;
    unsigned long first, len;
    unsigned char *bytes;
};

struct pdf {
    pdf_read_fn read;
    void *ctx;
    unsigned long size;
    struct entry *x;
    unsigned long nx;
    struct pdf_obj *trailer;
    struct objstm os[2];
    unsigned os_next;
    unsigned long held;                 /* bytes of objects read */
    unsigned pages;
    int depth;                          /* (objects being read within objects) */
};

struct src {
    const unsigned char *p, *end;
    struct pdf *d;
    int depth;
    int short_of;                       /* the end was met inside something */
};

/* ---- objects made and unmade */

static void *take(struct pdf *d, unsigned long n)
{
    void *p = calloc(1, n ? n : 1);
    if (p)
        d->held += n;
    return p;
}

static void unmake(struct pdf_obj *o)
{
    unsigned i;
    if (!o)
        return;
    switch (o->type) {
    case PDF_NAME:
    case PDF_STRING:
        free(o->u.s);
        break;
    case PDF_ARRAY:
        for (i = 0; i < o->n; i++)
            unmake(o->u.item[i]);
        free(o->u.item);
        break;
    case PDF_DICT:
        for (i = 0; i < o->n; i++) {
            free(o->u.d.key[i]);
            unmake(o->u.d.value[i]);
        }
        free(o->u.d.key);
        free(o->u.d.value);
        break;
    }
    free(o);
}

/* ---- the words of the file */

static int white(int c) { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
static int delim(int c) { return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}'
                                 || c == '/' || c == '%'; }
static int hexval(int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }

static void blank(struct src *s)        /* white space and comments passed over */
{
    for (;;) {
        while (s->p < s->end && white(*s->p))
            s->p++;
        if (s->p < s->end && *s->p == '%') {
            while (s->p < s->end && *s->p != 10 && *s->p != 13)
                s->p++;
            continue;
        }
        return;
    }
}

static int word_is(struct src *s, const char *w)        /* the keyword w is next (and ends there) */
{
    size_t n = strlen(w);
    return (size_t)(s->end - s->p) >= n && !memcmp(s->p, w, n) && (s->p + n == s->end || white(s->p[n]) || delim(s->p[n]));
}

static int number(struct src *s, double *v, int *whole) /* a number next: read (1) */
{
    static const double tens[] = { 1, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12 };
    const unsigned char *p = s->p;
    double x = 0;
    int neg = 0, digits = 0, places = 0;
    *whole = 1;
    while (p < s->end && (*p == '+' || *p == '-'))
        neg ^= *p++ == '-';
    for (; p < s->end && *p >= '0' && *p <= '9'; p++, digits++)
        x = x * 10 + (*p - '0');
    if (p < s->end && *p == '.') {              /* (its digits as a whole number, divided once: no error adds up) */
        *whole = 0;
        for (p++; p < s->end && *p >= '0' && *p <= '9'; p++, digits++)
            if (places < 12)
                x = x * 10 + (*p - '0'), places++;
        x /= tens[places];
    }
    if (!digits)
        return 0;
    if (p == s->end)
        s->short_of = 1;
    *v = neg ? -x : x;
    s->p = p;
    return 1;
}

static struct pdf_obj *parse(struct src *s);

static struct pdf_obj *text(struct src *s, int type, const unsigned char *bytes, unsigned n)
{
    struct pdf_obj *o = take(s->d, sizeof *o);
    if (!o || !(o->u.s = take(s->d, n + 1ul))) {
        free(o);
        return NULL;
    }
    memcpy(o->u.s, bytes, n);
    o->type = (unsigned char)type, o->n = n;
    return o;
}

static struct pdf_obj *string(struct src *s)    /* ( ... ): escapes undone, its own brackets counted */
{
    const unsigned char *p = s->p + 1;
    unsigned char *b = malloc((size_t)(s->end - s->p) + 1);
    struct pdf_obj *o;
    unsigned n = 0;
    int deep = 1;
    if (!b)
        return NULL;
    while (p < s->end && deep) {
        int c = *p++;
        if (c == '\\' && p < s->end) {
            c = *p++;
            switch (c) {
            case 'n': c = 10; break;
            case 'r': c = 13; break;
            case 't': c = 9; break;
            case 'b': c = 8; break;
            case 'f': c = 12; break;
            case 13:                            /* (a line that goes on) */
                if (p < s->end && *p == 10)
                    p++;
                continue;
            case 10:
                continue;
            default:
                if (c >= '0' && c <= '7') {
                    int k;
                    c -= '0';
                    for (k = 0; k < 2 && p < s->end && *p >= '0' && *p <= '7'; k++)
                        c = c * 8 + (*p++ - '0');
                }
            }
            b[n++] = (unsigned char)c;
            continue;
        }
        if (c == '(')
            deep++;
        else if (c == ')' && !--deep)
            break;
        b[n++] = (unsigned char)c;
    }
    if (deep)
        s->short_of = 1;
    s->p = p;
    o = text(s, PDF_STRING, b, n);
    free(b);
    return o;
}

static struct pdf_obj *hexstring(struct src *s) /* < ... > */
{
    const unsigned char *p = s->p + 1;
    unsigned char *b = malloc((size_t)(s->end - s->p) / 2 + 2);
    struct pdf_obj *o;
    unsigned n = 0;
    int half = -1, v;
    if (!b)
        return NULL;
    for (; p < s->end && *p != '>'; p++)
        if ((v = hexval(*p)) >= 0) {
            if (half < 0)
                half = v;
            else
                b[n++] = (unsigned char)(half << 4 | v), half = -1;
        }
    if (half >= 0)
        b[n++] = (unsigned char)(half << 4);
    if (p == s->end)
        s->short_of = 1;
    s->p = p < s->end ? p + 1 : p;
    o = text(s, PDF_STRING, b, n);
    free(b);
    return o;
}

static struct pdf_obj *name(struct src *s)      /* /Name, with its #xx undone */
{
    const unsigned char *p = s->p + 1;
    unsigned char b[128];
    unsigned n = 0;
    for (; p < s->end && !white(*p) && !delim(*p); p++) {
        int c = *p;
        if (c == '#' && s->end - p >= 3 && hexval(p[1]) >= 0 && hexval(p[2]) >= 0)
            c = hexval(p[1]) << 4 | hexval(p[2]), p += 2;
        if (n < sizeof b - 1)
            b[n++] = (unsigned char)c;
    }
    if (p == s->end)
        s->short_of = 1;
    s->p = p;
    return text(s, PDF_NAME, b, n);
}

static int grow(struct pdf *d, void **list, unsigned n, unsigned long each)     /* room for one more of a list's items */
{
    void *more;
    if (n & (n - 1) || (n && n < 4))            /* (4, 8, 16 ...) */
        return 0;
    if (!(more = realloc(*list, (n ? n * 2ul : 4) * each)))
        return -1;
    d->held += (n ? n : 4) * each;
    *list = more;
    return 0;
}

static struct pdf_obj *array(struct src *s)
{
    struct pdf_obj *o = take(s->d, sizeof *o), *v;
    if (!o)
        return NULL;
    o->type = PDF_ARRAY;
    s->p++;
    for (;;) {
        blank(s);
        if (s->p >= s->end) {
            s->short_of = 1;
            break;
        }
        if (*s->p == ']') {
            s->p++;
            break;
        }
        if (!(v = parse(s))) {                  /* (no object there: a word passed over, or the end) */
            const unsigned char *was = s->p;
            while (s->p < s->end && !white(*s->p) && !delim(*s->p))
                s->p++;
            if (s->p == was)
                s->p++;
            continue;
        }
        if (grow(s->d, (void **)&o->u.item, o->n, sizeof o->u.item[0]) != 0) {
            unmake(v);
            break;
        }
        o->u.item[o->n++] = v;
    }
    return o;
}

static struct pdf_obj *dict(struct src *s)
{
    struct pdf_obj *o = take(s->d, sizeof *o), *k, *v;
    if (!o)
        return NULL;
    o->type = PDF_DICT;
    s->p += 2;
    for (;;) {
        blank(s);
        if (s->p >= s->end) {
            s->short_of = 1;
            break;
        }
        if (s->end - s->p >= 2 && s->p[0] == '>' && s->p[1] == '>') {
            s->p += 2;
            break;
        }
        if (*s->p != '/') {                     /* (no key: passed over) */
            if ((v = parse(s)) != NULL) {
                unmake(v);
            } else {
                const unsigned char *was = s->p;
                while (s->p < s->end && !white(*s->p) && !delim(*s->p))
                    s->p++;
                if (s->p == was)
                    s->p++;
            }
            continue;
        }
        if (!(k = name(s)))
            break;
        blank(s);
        v = parse(s);
        if (!v && (v = take(s->d, sizeof *v)) == NULL) {        /* (a key with nothing after it: null) */
            unmake(k);
            break;
        }
        if (grow(s->d, (void **)&o->u.d.key, o->n, sizeof o->u.d.key[0]) != 0
            || grow(s->d, (void **)&o->u.d.value, o->n, sizeof o->u.d.value[0]) != 0) {
            unmake(k);
            unmake(v);
            break;
        }
        o->u.d.key[o->n] = k->u.s, o->u.d.value[o->n++] = v;
        free(k);
    }
    return o;
}

/* The object that begins at s->p (after white space): read, s->p after it. NULL: none there (a keyword that is
   no object, or the end). */
static struct pdf_obj *parse(struct src *s)
{
    struct pdf_obj *o = NULL;
    double v;
    int whole, c;
    blank(s);
    if (s->p >= s->end || s->depth >= MAX_DEPTH)
        return NULL;
    s->depth++;
    c = *s->p;
    if (c == '/') {
        o = name(s);
    } else if (c == '(') {
        o = string(s);
    } else if (c == '[') {
        o = array(s);
    } else if (c == '<') {
        o = s->end - s->p >= 2 && s->p[1] == '<' ? dict(s) : hexstring(s);
        if (s->end - s->p < 2)
            s->short_of |= s->p >= s->end;
    } else if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
        if (number(s, &v, &whole) && (o = take(s->d, sizeof *o)) != NULL) {
            const unsigned char *back = s->p;
            double g;
            int gw;
            o->type = PDF_NUMBER, o->whole = (unsigned char)whole, o->u.num = v;
            if (whole && v >= 0 && v < 4294967295.0) {  /* "12 0 R": a reference */
                blank(s);
                if (s->p < s->end && *s->p >= '0' && *s->p <= '9' && number(s, &g, &gw) && gw && g < 65536) {
                    blank(s);
                    if (word_is(s, "R")) {
                        s->p++;
                        o->type = PDF_REF, o->u.ref.num = (unsigned)v, o->u.ref.gen = (unsigned)g;
                        back = s->p;
                    } else if (s->p >= s->end) {
                        s->short_of = 1;
                    }
                }
                s->p = back;
            }
        }
    } else if (word_is(s, "true") || word_is(s, "false")) {
        if ((o = take(s->d, sizeof *o)) != NULL)
            o->type = PDF_BOOL, o->u.num = c == 't';
        s->p += c == 't' ? 4 : 5;
    } else if (word_is(s, "null")) {
        o = take(s->d, sizeof *o);
        s->p += 4;
    }
    s->depth--;
    return o;
}

int pdf_word(struct pdf_words *w, struct pdf_obj **obj, char op[16])
{
    struct src s = { w->p, w->end, w->d, 0, 0 };
    unsigned n = 0;
    for (;;) {
        blank(&s);
        if (s.p >= s.end) {
            w->p = s.p;
            return 0;
        }
        if ((*obj = parse(&s)) != NULL) {
            w->p = s.p;
            return 1;
        }
        if (!delim(*s.p))
            break;
        s.p++;                                  /* (a bracket with no partner: passed over) */
    }
    for (; s.p < s.end && !white(*s.p) && !delim(*s.p); s.p++)
        if (n < 15)
            op[n++] = (char)*s.p;
    op[n] = 0;
    w->p = s.p;
    return 2;
}

void pdf_free(struct pdf_obj *o) { unmake(o); }

/* ---- the file's pieces */

static unsigned char *fetch(struct pdf *d, unsigned long at, unsigned long n, unsigned long *got)
{
    unsigned char *b;
    *got = 0;
    if (at >= d->size)
        return NULL;
    if (n > d->size - at)
        n = d->size - at;
    if (!(b = malloc(n + 1)))
        return NULL;
    *got = d->read(d->ctx, at, b, n);
    if (*got > n)
        *got = n;
    b[*got] = 0;
    return b;
}

/* The numbered object written at `at` ("N G obj ..."): read. *num: its number as written there. */
static struct pdf_obj *object_at(struct pdf *d, unsigned long at, unsigned *num)
{
    unsigned long want = 2048, got;
    for (;;) {
        unsigned char *b = fetch(d, at, want, &got);
        struct src s = { b, b + got, d, 0, 0 };
        struct pdf_obj *o = NULL;
        double n, g;
        int w;
        if (!b)
            return NULL;
        blank(&s);
        if (number(&s, &n, &w) && w && n >= 0 && n < MAX_OBJECTS && (blank(&s), number(&s, &g, &w)) && (blank(&s), word_is(&s, "obj"))) {
            s.p += 3;
            s.short_of = 0;
            o = parse(&s);
            if (o && !s.short_of) {
                blank(&s);
                if (o->type == PDF_DICT && word_is(&s, "stream")) {     /* its bytes begin after the line's end */
                    s.p += 6;
                    if (s.p < s.end && *s.p == 13)
                        s.p++;
                    if (s.p < s.end && *s.p == 10)
                        s.p++;
                    o->stream = 1, o->at = at + (unsigned long)(s.p - b);
                } else if (s.p >= s.end && got == want && at + got < d->size) {
                    s.short_of = 1;             /* (whether a stream follows can't be seen yet) */
                }
            }
            if ((!o || s.short_of) && got == want && at + got < d->size && want < (16ul << 20)) {
                unmake(o);                      /* it doesn't end in the piece read: a larger one */
                free(b);
                want *= 4;
                continue;
            }
            if (num)
                *num = (unsigned)n;
        }
        free(b);
        return o;
    }
}

/* ---- streams unpacked */

struct out {                            /* bytes as they come out of an unpacker */
    unsigned char *p;
    unsigned long n, room;
    int bad;
};

static void out(struct out *o, const unsigned char *b, unsigned long n)
{
    if (o->bad)
        return;
    if (o->n + n > o->room) {
        unsigned long room = o->room ? o->room * 2 : 4096;
        unsigned char *p;
        while (room < o->n + n)
            room *= 2;
        if (room > MAX_STREAM || !(p = realloc(o->p, room))) {
            o->bad = 1;
            return;
        }
        o->p = p, o->room = room;
    }
    memcpy(o->p + o->n, b, n);
    o->n += n;
}

struct flate {
    const unsigned char *in;
    unsigned long left;
    unsigned char *window;
    struct out *to;
};

static unsigned flate_fill(const unsigned char **p, void *ctx)
{
    struct flate *f = ctx;
    unsigned n = f->left > 0x4000 ? 0x4000 : (unsigned)f->left;
    *p = f->in;
    f->in += n, f->left -= n;
    return n;
}

static int flate_flush(unsigned n, void *ctx)
{
    struct flate *f = ctx;
    out(f->to, f->window, n);
    return f->to->bad;
}

static void unflate(const unsigned char *in, unsigned long n, struct out *to)
{
    struct flate f = { in, n, malloc(INFL_WINDOW), to };
    struct infl *z = malloc(inflate_state_size());
    if (f.window && z) {
        if (n >= 2 && (in[0] & 15) == 8 && ((in[0] << 8 | in[1]) % 31) == 0)    /* (zlib's two bytes before it) */
            f.in += 2, f.left -= 2;
        inflate_stream_r(z, flate_fill, flate_flush, &f, f.window);     /* (a damaged end: what came before it) */
    } else {
        to->bad = 1;
    }
    free(f.window);
    free(z);
}

static void unlzw(const unsigned char *in, unsigned long n, int early, struct out *to)
{
    static const unsigned short none = 0xFFFF;
    unsigned short *prefix = malloc(4096 * sizeof *prefix);
    unsigned char *suffix = malloc(4096), *stack = malloc(4097);
    unsigned long at = 0, acc = 0;
    unsigned bits = 0, width = 9, next = 258, prev = none, code, c, top;
    if (!prefix || !suffix || !stack) {
        to->bad = 1;
        goto done;
    }
    for (;;) {
        while (bits < width && at < n)
            acc = acc << 8 | in[at++], bits += 8;
        if (bits < width)
            break;
        code = (unsigned)(acc >> (bits - width)) & ((1u << width) - 1);
        bits -= width;
        if (code == 257)
            break;
        if (code == 256) {
            width = 9, next = 258, prev = none;
            continue;
        }
        top = 0;
        c = code;
        if (code >= next) {                     /* (the code being made now: the last string and its first byte) */
            if (prev == none || code > next)
                break;
            c = prev;
            top = 1;
        }
        while (c >= 258 && top < 4096)
            stack[top++] = suffix[c], c = prefix[c];
        stack[top++] = (unsigned char)c;
        if (code >= next)
            stack[0] = (unsigned char)c;
        if (prev != none && next < 4096) {
            prefix[next] = (unsigned short)prev, suffix[next] = (unsigned char)c;
            next++;
        }
        while (top)
            out(to, &stack[--top], 1);
        prev = code;
        if (next + (unsigned)early >= (1u << width) && width < 12)
            width++;
    }
done:
    free(prefix);
    free(suffix);
    free(stack);
}

static void un85(const unsigned char *in, unsigned long n, struct out *to)
{
    unsigned long i, v = 0;
    unsigned k = 0;
    unsigned char b[4];
    for (i = 0; i < n; i++) {
        int c = in[i];
        if (c == '~')
            break;
        if (white(c))
            continue;
        if (c == 'z' && !k) {
            memset(b, 0, 4);
            out(to, b, 4);
            continue;
        }
        if (c < '!' || c > 'u')
            break;
        v = v * 85 + (unsigned long)(c - '!');
        if (++k == 5) {
            b[0] = (unsigned char)(v >> 24), b[1] = (unsigned char)(v >> 16), b[2] = (unsigned char)(v >> 8), b[3] = (unsigned char)v;
            out(to, b, 4);
            k = 0, v = 0;
        }
    }
    if (k > 1) {                                /* a last group, short: filled up with the highest digit */
        unsigned j;
        for (j = k; j < 5; j++)
            v = v * 85 + 84;
        b[0] = (unsigned char)(v >> 24), b[1] = (unsigned char)(v >> 16), b[2] = (unsigned char)(v >> 8);
        out(to, b, k - 1);
    }
}

static void unhex(const unsigned char *in, unsigned long n, struct out *to)
{
    unsigned long i;
    int half = -1, v;
    unsigned char b;
    for (i = 0; i < n && in[i] != '>'; i++)
        if ((v = hexval(in[i])) >= 0) {
            if (half < 0) {
                half = v;
            } else {
                b = (unsigned char)(half << 4 | v);
                out(to, &b, 1);
                half = -1;
            }
        }
    if (half >= 0) {
        b = (unsigned char)(half << 4);
        out(to, &b, 1);
    }
}

static void unrun(const unsigned char *in, unsigned long n, struct out *to)
{
    unsigned long i = 0;
    while (i < n && in[i] != 128) {
        unsigned c = in[i++];
        if (c < 128) {
            unsigned long k = c + 1 > n - i ? n - i : c + 1;
            out(to, in + i, k);
            i += k;
        } else if (i < n) {
            unsigned k;
            for (k = 0; k < 257 - c; k++)
                out(to, in + i, 1);
            i++;
        }
    }
}

/* A predictor undone in place (what Flate and LZW streams of pictures and of cross-reference tables have):
   the bytes left. */
static unsigned long unpredict(unsigned char *b, unsigned long n, int predictor, long colors, long bpc, long columns)
{
    unsigned long row, bpp, rows, r, i, o = 0;
    if (predictor < 2 || colors < 1 || colors > 32 || bpc < 1 || bpc > 16 || columns < 1 || columns > 1000000)
        return n;
    row = ((unsigned long)columns * (unsigned long)colors * (unsigned long)bpc + 7) / 8;
    bpp = ((unsigned long)colors * (unsigned long)bpc + 7) / 8;
    if (predictor == 2) {                       /* TIFF's: each sample less the one to its left (whole bytes here) */
        if (bpc == 8)
            for (r = 0; r + row <= n; r += row)
                for (i = bpp; i < row; i++)
                    b[r + i] = (unsigned char)(b[r + i] + b[r + i - bpp]);
        return n;
    }
    rows = n / (row + 1);                       /* PNG's: a byte before each row says how it was made */
    for (r = 0; r < rows; r++) {
        unsigned char *in = b + r * (row + 1), *cur = b + o, *up = r ? cur - row : NULL;
        int how = *in++;
        for (i = 0; i < row; i++) {
            int a = i >= bpp ? cur[i - bpp] : 0, u = up ? up[i] : 0, c = up && i >= bpp ? up[i - bpp] : 0, v = in[i];
            switch (how) {
            case 1: v += a; break;
            case 2: v += u; break;
            case 3: v += (a + u) / 2; break;
            case 4: {
                int p = a + u - c, pa = abs(p - a), pb = abs(p - u), pc = abs(p - c);
                v += pa <= pb && pa <= pc ? a : pb <= pc ? u : c;
                break;
            }
            }
            cur[i] = (unsigned char)v;
        }
        o += row;
    }
    return o;
}

/* A stream's bytes as they lie in the file: `Length` of them, or (where that is missing or wrong) as many as
   there are before "endstream". */
static unsigned char *stream_raw(struct pdf *d, struct pdf_obj *o, unsigned long *len)
{
    long said = pdf_int(pdf_get(d, o, "Length"), -1);
    unsigned long got, n = 0, tail;
    unsigned char *b, *t;
    *len = 0;
    if (said >= 0 && (unsigned long)said <= d->size && o->at <= d->size - (unsigned long)said) {
        t = fetch(d, o->at + (unsigned long)said, 32, &tail);   /* (is its end where it says?) */
        if (t) {
            struct src s = { t, t + tail, d, 0, 0 };
            blank(&s);
            if (word_is(&s, "endstream") || word_is(&s, "endobj") || !tail)
                n = (unsigned long)said;
            else
                said = -1;
            free(t);
        }
    } else {
        said = -1;
    }
    if (said < 0) {                             /* looked for, a piece at a time */
        unsigned long at = o->at, want = 65536;
        for (n = 0; at < d->size && at - o->at < MAX_STREAM; at += got - 16) {
            unsigned long i;
            if (!(b = fetch(d, at, want, &got)))
                return NULL;
            for (i = 0; i + 9 <= got; i++)
                if (b[i] == 'e' && !memcmp(b + i, "endstream", 9))
                    break;
            if (i + 9 <= got) {                 /* (the line's end before it is not the stream's) */
                if (i && b[i - 1] == 10)
                    i--;
                if (i && b[i - 1] == 13)
                    i--;
                n = at + i - o->at;
                free(b);
                break;
            }
            free(b);
            if (got < 32) {
                n = at + got - o->at;
                break;
            }
            n = at + got - o->at;
        }
    }
    if (n > MAX_STREAM)
        n = MAX_STREAM;
    if (!(b = fetch(d, o->at, n ? n : 1, &got)))
        return n ? NULL : calloc(1, 1);
    *len = n ? got : 0;
    return b;
}

unsigned char *pdf_stream(struct pdf *d, struct pdf_obj *o, unsigned long *len, const char **last, int *err)
{
    struct pdf_obj *filters, *parms;
    unsigned char *b;
    unsigned long n;
    unsigned i, count;
    int dummy;
    const char *none;
    if (!err)
        err = &dummy;
    if (!last)
        last = &none;
    *last = "", *len = 0;
    o = pdf_resolve(d, o);
    *err = PDF_E_DAMAGED;
    if (!o || o->type != PDF_DICT || !o->stream)
        return NULL;
    *err = PDF_E_MEMORY;
    if (!(b = stream_raw(d, o, &n)))
        return NULL;
    filters = pdf_get(d, o, "Filter");
    if (!(parms = pdf_get(d, o, "DecodeParms")))
        parms = pdf_get(d, o, "DP");
    count = pdf_is(filters, PDF_ARRAY) ? filters->n : pdf_is(filters, PDF_NAME) ? 1 : 0;
    for (i = 0; i < count && i < 8; i++) {
        struct pdf_obj *f = pdf_is(filters, PDF_ARRAY) ? pdf_item(d, filters, i) : filters;
        struct pdf_obj *p = pdf_is(parms, PDF_ARRAY) ? pdf_item(d, parms, i) : i == 0 ? parms : NULL;
        const char *fn = pdf_name(f);
        struct out to = { 0, 0, 0, 0 };
        int flate = !strcmp(fn, "FlateDecode") || !strcmp(fn, "Fl"), lzw = !strcmp(fn, "LZWDecode") || !strcmp(fn, "LZW");
        if (flate)
            unflate(b, n, &to);
        else if (lzw)
            unlzw(b, n, (int)pdf_int(pdf_get(d, p, "EarlyChange"), 1) != 0, &to);
        else if (!strcmp(fn, "ASCII85Decode") || !strcmp(fn, "A85"))
            un85(b, n, &to);
        else if (!strcmp(fn, "ASCIIHexDecode") || !strcmp(fn, "AHx"))
            unhex(b, n, &to);
        else if (!strcmp(fn, "RunLengthDecode") || !strcmp(fn, "RL"))
            unrun(b, n, &to);
        else {                                  /* (a picture's own packing, or one not known here: the caller's) */
            *last = fn;
            break;
        }
        free(b);
        if (to.bad) {
            free(to.p);
            *err = PDF_E_MEMORY;
            return NULL;
        }
        if (!(b = to.p) && !(b = calloc(1, 1))) {
            *err = PDF_E_MEMORY;
            return NULL;
        }
        n = to.n;
        if (flate || lzw)
            n = unpredict(b, n, (int)pdf_int(pdf_get(d, p, "Predictor"), 1), pdf_int(pdf_get(d, p, "Colors"), 1),
                          pdf_int(pdf_get(d, p, "BitsPerComponent"), 8), pdf_int(pdf_get(d, p, "Columns"), 1));
    }
    *len = n;
    *err = PDF_OK;
    return b;
}

/* ---- objects by their numbers */

static struct objstm *objstm(struct pdf *d, unsigned num)       /* object stream num unpacked, its index read */
{
    struct objstm *os;
    struct pdf_obj *o;
    unsigned i;
    for (i = 0; i < 2; i++)
        if (d->os[i].bytes && d->os[i].num == num)
            return &d->os[i];
    if (num >= d->nx || d->x[num].kind != 1 || !(o = pdf_object(d, num)) || !o->stream)
        return NULL;
    os = &d->os[d->os_next++ & 1];
    free(os->bytes);
    memset(os, 0, sizeof *os);
    if (!(os->bytes = pdf_stream(d, o, &os->len, NULL, NULL)))
        return NULL;
    os->num = num;
    os->n = (unsigned)pdf_int(pdf_get(d, o, "N"), 0);
    os->first = (unsigned long)pdf_int(pdf_get(d, o, "First"), 0);
    if (os->first > os->len)
        os->first = os->len;
    return os;
}

static struct pdf_obj *from_objstm(struct pdf *d, unsigned stm, unsigned long index, unsigned want)
{
    struct objstm *os = objstm(d, stm);
    struct src s;
    double num = -1, off = 0;
    unsigned long i;
    int w;
    if (!os)
        return NULL;
    s.p = os->bytes, s.end = os->bytes + os->first, s.d = d, s.depth = 0, s.short_of = 0;
    for (i = 0; i <= index && i < os->n; i++) { /* its index: pairs of a number and where the object begins */
        blank(&s);
        if (!number(&s, &num, &w))
            return NULL;
        blank(&s);
        if (!number(&s, &off, &w))
            return NULL;
    }
    if (i <= index || (unsigned)num != want || off < 0 || off >= os->len - os->first)
        return NULL;
    s.p = os->bytes + os->first + (unsigned long)off, s.end = os->bytes + os->len;
    return parse(&s);
}

struct pdf_obj *pdf_object(struct pdf *d, unsigned num)
{
    struct entry *e;
    unsigned said = 0;
    if (num >= d->nx)
        return NULL;
    e = &d->x[num];
    if (e->obj || !e->kind || d->depth >= MAX_DEPTH)
        return e->obj;
    d->depth++;
    if (e->kind == 1)
        e->obj = object_at(d, e->at, &said);
    else if (e->stm != num)
        e->obj = from_objstm(d, e->stm, e->at, num);
    d->depth--;
    return e->obj;
}

struct pdf_obj *pdf_resolve(struct pdf *d, struct pdf_obj *o)
{
    int hops;
    for (hops = 0; o && o->type == PDF_REF && hops < 32; hops++)
        o = pdf_object(d, o->u.ref.num);
    return o && o->type != PDF_REF ? o : NULL;
}

struct pdf_obj *pdf_get(struct pdf *d, struct pdf_obj *dict, const char *key)
{
    unsigned i;
    if (!dict || dict->type != PDF_DICT)
        return NULL;
    for (i = 0; i < dict->n; i++)
        if (!strcmp(dict->u.d.key[i], key))
            return pdf_resolve(d, dict->u.d.value[i]);
    return NULL;
}

struct pdf_obj *pdf_item(struct pdf *d, struct pdf_obj *array, unsigned i)
{
    return array && array->type == PDF_ARRAY && i < array->n ? pdf_resolve(d, array->u.item[i]) : NULL;
}

int pdf_is(const struct pdf_obj *o, int type) { return o && o->type == type; }
double pdf_num(const struct pdf_obj *o, double otherwise) { return o && o->type == PDF_NUMBER ? o->u.num : otherwise; }
const char *pdf_name(const struct pdf_obj *o) { return o && o->type == PDF_NAME ? o->u.s : ""; }
int pdf_name_is(const struct pdf_obj *o, const char *name) { return o && o->type == PDF_NAME && !strcmp(o->u.s, name); }

long pdf_int(const struct pdf_obj *o, long otherwise)
{
    if (!o || o->type != PDF_NUMBER || o->u.num > 2147483647.0 || o->u.num < -2147483647.0)
        return otherwise;
    return (long)o->u.num;
}

const char *pdf_string(const struct pdf_obj *o, unsigned *len)
{
    if (len)
        *len = o && o->type == PDF_STRING ? o->n : 0;
    return o && o->type == PDF_STRING ? o->u.s : "";
}

/* ---- the cross-reference tables */

static int room(struct pdf *d, unsigned long n)         /* the table has entries up to n - 1 */
{
    struct entry *x;
    if (n <= d->nx)
        return 0;
    if (n > MAX_OBJECTS || !(x = realloc(d->x, n * sizeof *x)))
        return -1;
    memset(x + d->nx, 0, (n - d->nx) * sizeof *x);
    d->x = x, d->nx = n;
    return 0;
}

static void entry(struct pdf *d, unsigned long num, int kind, unsigned long at, unsigned stm)  /* (the newest stands) */
{
    if (num < d->nx && !d->x[num].kind && kind)
        d->x[num].kind = (unsigned char)kind, d->x[num].at = at, d->x[num].stm = stm;
}

static void trailer(struct pdf *d, struct pdf_obj *t)   /* a table's dictionary: the newest is the document's */
{
    if (!d->trailer)
        d->trailer = t;
    else
        unmake(t);
}

/* The table at `at`, of either kind: its entries and its dictionary taken. Where the one before it is (0: none);
   -1: nothing could be made of it. */
static long table(struct pdf *d, unsigned long at, unsigned long *also)
{
    unsigned long got, want = 65536;
    unsigned char *b;
    struct pdf_obj *t;
    long prev;
    *also = 0;
again:
    if (!(b = fetch(d, at, want, &got)))
        return -1;
    {
        struct src s = { b, b + got, d, 0, 0 };
        blank(&s);
        if (word_is(&s, "xref")) {              /* a table of text: "first count", then a line an object */
            s.p += 4;
            for (;;) {
                double first, count, off, gen;
                unsigned long i;
                int w;
                blank(&s);
                if (word_is(&s, "trailer"))
                    break;
                if (!number(&s, &first, &w) || (blank(&s), !number(&s, &count, &w)) || first < 0 || count < 0
                    || first + count > MAX_OBJECTS) {
                    if (s.p >= s.end && got == want && at + got < d->size && want < (64ul << 20))
                        goto more;
                    free(b);
                    return -1;
                }
                if (room(d, (unsigned long)(first + count)) != 0) {
                    free(b);
                    return -1;
                }
                for (i = 0; i < (unsigned long)count; i++) {
                    blank(&s);
                    if (!number(&s, &off, &w) || (blank(&s), !number(&s, &gen, &w)))
                        break;
                    blank(&s);
                    if (s.p >= s.end)
                        break;
                    if (*s.p == 'n' && off > 0)
                        entry(d, (unsigned long)first + i, 1, (unsigned long)off, 0);
                    s.p++;
                }
                if (i < (unsigned long)count && got == want && at + got < d->size && want < (64ul << 20))
                    goto more;
            }
            s.p += 7;
            s.short_of = 0;
            t = parse(&s);
            if ((!t || s.short_of) && got == want && at + got < d->size && want < (64ul << 20)) {
                unmake(t);
                goto more;
            }
            free(b);
            if (!t || t->type != PDF_DICT) {
                unmake(t);
                return -1;
            }
        } else {                                /* a stream: "/W [1 2 1]" bytes an entry, "/Index [first count ...]" */
            unsigned char *rows;
            unsigned long len, r = 0;
            struct pdf_obj *w, *index;
            unsigned w0, w1, w2, k, pairs;
            free(b);
            if (!(t = object_at(d, at, NULL)) || t->type != PDF_DICT || !t->stream || !pdf_name_is(pdf_get(d, t, "Type"), "XRef")
                || !(rows = pdf_stream(d, t, &len, NULL, NULL))) {
                unmake(t);
                return -1;
            }
            w = pdf_get(d, t, "W"), index = pdf_get(d, t, "Index");
            w0 = (unsigned)pdf_int(pdf_item(d, w, 0), 0), w1 = (unsigned)pdf_int(pdf_item(d, w, 1), 0);
            w2 = (unsigned)pdf_int(pdf_item(d, w, 2), 0);
            pairs = pdf_is(index, PDF_ARRAY) ? index->n / 2 : 1;
            if (w0 > 4 || w1 > 8 || w2 > 4 || !(w0 + w1 + w2)) {
                free(rows);
                unmake(t);
                return -1;
            }
            for (k = 0; k < pairs; k++) {
                long first = pdf_is(index, PDF_ARRAY) ? pdf_int(pdf_item(d, index, 2 * k), 0) : 0;
                long count = pdf_is(index, PDF_ARRAY) ? pdf_int(pdf_item(d, index, 2 * k + 1), 0) : pdf_int(pdf_get(d, t, "Size"), 0);
                long i;
                if (first < 0 || count < 0 || (unsigned long)first > MAX_OBJECTS)
                    break;
                if ((unsigned long)count > (len - r) / (w0 + w1 + w2))  /* (no more than there are bytes for) */
                    count = (long)((len - r) / (w0 + w1 + w2));
                if ((unsigned long)first + (unsigned long)count > MAX_OBJECTS || room(d, (unsigned long)first + (unsigned long)count) != 0)
                    break;
                for (i = 0; i < count; i++) {
                    unsigned long f[3] = { 1, 0, 0 };
                    unsigned widths[3] = { w0, w1, w2 }, j, c;
                    for (j = 0; j < 3; j++)
                        for (c = 0, f[j] = widths[j] ? 0 : f[j]; c < widths[j]; c++)
                            f[j] = f[j] << 8 | rows[r++];
                    if (f[0] == 1 && f[1])
                        entry(d, (unsigned long)first + (unsigned long)i, 1, f[1], 0);
                    else if (f[0] == 2)
                        entry(d, (unsigned long)first + (unsigned long)i, 2, f[2], (unsigned)f[1]);
                }
            }
            free(rows);
        }
    }
    prev = pdf_int(pdf_get(d, t, "Prev"), 0);
    *also = (unsigned long)pdf_int(pdf_get(d, t, "XRefStm"), 0);
    if (prev < 0 || (unsigned long)prev >= d->size)
        prev = 0;
    trailer(d, t);
    return prev;
more:
    free(b);
    want *= 4;
    goto again;
}

static int has_key(const struct pdf_obj *o, const char *key)   /* (whether or not what it names can be read yet) */
{
    unsigned k;
    for (k = 0; o && o->type == PDF_DICT && k < o->n; k++)
        if (!strcmp(o->u.d.key[k], key))
            return 1;
    return 0;
}

/* The tables made anew by going through the file for "N G obj" at lines' starts, and for a trailer. */
static void rebuild(struct pdf *d)
{
    unsigned long at, got, i;
    unsigned char *b;
    for (i = 0; i < d->nx; i++) {
        unmake(d->x[i].obj);
        memset(&d->x[i], 0, sizeof d->x[i]);
    }
    unmake(d->trailer);
    d->trailer = NULL;
    for (at = 0; at < d->size; at += got > 64 ? got - 64 : got) {
        if (!(b = fetch(d, at, 65536, &got)) || !got) {
            free(b);
            break;
        }
        for (i = 0; i + 8 < got; i++) {
            struct src s = { b + i, b + got, d, 0, 0 };
            double n, g;
            int w;
            if (i && b[i - 1] != 10 && b[i - 1] != 13 && b[i - 1] != 32)
                continue;
            if (b[i] >= '0' && b[i] <= '9') {
                if (number(&s, &n, &w) && w && n < MAX_OBJECTS && s.p < s.end && white(*s.p) && (blank(&s), number(&s, &g, &w))
                    && (blank(&s), word_is(&s, "obj")) && room(d, (unsigned long)n + 1) == 0) {
                    d->x[(unsigned long)n].kind = 1, d->x[(unsigned long)n].at = at + i, d->x[(unsigned long)n].stm = 0;
                }
            } else if (b[i] == 't' && word_is(&s, "trailer")) { /* (the last one with a Root stands) */
                struct pdf_obj *t;
                s.p += 7;
                if ((t = parse(&s)) != NULL && has_key(t, "Root")) {
                    unmake(d->trailer);
                    d->trailer = t;
                } else {
                    unmake(t);
                }
            }
        }
        free(b);
        if (got < 65536)
            break;
    }
    /* What lies in object streams, and (a file with no trailer of text) the catalog by its type. */
    for (i = 0; i < d->nx; i++) {
        struct pdf_obj *o, *type;
        if (d->x[i].kind != 1 || !(o = pdf_object(d, (unsigned)i)) || o->type != PDF_DICT)
            continue;
        type = pdf_get(d, o, "Type");
        if (pdf_name_is(type, "ObjStm")) {
            struct objstm *os = objstm(d, (unsigned)i);
            struct src s;
            unsigned k;
            if (!os)
                continue;
            s.p = os->bytes, s.end = os->bytes + os->first, s.d = d, s.depth = 0, s.short_of = 0;
            for (k = 0; k < os->n; k++) {
                double n, off;
                int w;
                blank(&s);
                if (!number(&s, &n, &w) || (blank(&s), !number(&s, &off, &w)) || n < 0 || n >= MAX_OBJECTS)
                    break;
                if (room(d, (unsigned long)n + 1) == 0 && !d->x[(unsigned long)n].kind)
                    d->x[(unsigned long)n].kind = 2, d->x[(unsigned long)n].at = k, d->x[(unsigned long)n].stm = (unsigned)i;
            }
        } else if (!d->trailer && pdf_name_is(type, "XRef") && has_key(o, "Root")) {
            d->trailer = object_at(d, d->x[i].at, NULL);        /* (its own dictionary is a trailer: a copy to keep) */
        } else if (!d->trailer && pdf_name_is(type, "Catalog")) {
            char text[40];
            struct src s;
            sprintf(text, "<</Root %lu 0 R>>", i);
            s.p = (const unsigned char *)text, s.end = s.p + strlen(text), s.d = d, s.depth = 0, s.short_of = 0;
            d->trailer = parse(&s);
        }
    }
}

static int usable(struct pdf *d)        /* a catalog with a tree of pages can be reached */
{
    struct pdf_obj *root = pdf_get(d, d->trailer, "Root");
    return pdf_is(root, PDF_DICT) && pdf_is(pdf_get(d, root, "Pages"), PDF_DICT);
}

struct pdf *pdf_open(pdf_read_fn read, void *ctx, unsigned long size, int *err)
{
    struct pdf *d;
    unsigned char *b;
    unsigned long got, at = 0, also, i;
    long prev;
    int dummy, turns, found = 0;
    if (!err)
        err = &dummy;
    *err = PDF_E_MEMORY;
    if (!(d = calloc(1, sizeof *d)))
        return NULL;
    d->read = read, d->ctx = ctx, d->size = size;
    *err = PDF_E_KIND;
    if (!(b = fetch(d, 0, 1024, &got)))
        goto bad;
    for (i = 0; i + 5 <= got && memcmp(b + i, "%PDF-", 5); i++)
        ;
    free(b);
    if (i + 5 > got)
        goto bad;
    *err = PDF_E_DAMAGED;
    at = size > 2048 ? size - 2048 : 0;         /* "startxref" and a number, near the end */
    if ((b = fetch(d, at, 2048, &got)) != NULL) {
        for (i = got; i-- > 0; )
            if (b[i] == 's' && got - i >= 9 && !memcmp(b + i, "startxref", 9)) {
                struct src s = { b + i + 9, b + got, d, 0, 0 };
                double v;
                int w;
                blank(&s);
                if (number(&s, &v, &w) && v > 0 && v < size)
                    at = (unsigned long)v, found = 1;
                break;
            }
        free(b);
    }
    for (turns = 0; found && turns < 256; turns++) {    /* the newest table, then those before it */
        if ((prev = table(d, at, &also)) < 0) {
            found = turns > 0 && d->trailer != NULL;    /* (an older one damaged: the newer ones may do) */
            break;
        }
        if (also && also < size)
            table(d, also, &also);
        if (!prev)
            break;
        at = (unsigned long)prev;
    }
    if (!found || !usable(d)) {
        rebuild(d);
        if (!usable(d))
            goto bad;
    }
    *err = PDF_E_LOCKED;
    if (pdf_get(d, d->trailer, "Encrypt"))
        goto bad;
    {
        long count = pdf_int(pdf_get(d, pdf_get(d, pdf_catalog(d), "Pages"), "Count"), 0);
        d->pages = count < 0 ? 0 : (unsigned long)count > d->nx ? (unsigned)d->nx : (unsigned)count;
    }
    *err = PDF_OK;
    return d;
bad:
    pdf_close(d);
    return NULL;
}

void pdf_trim(struct pdf *d, unsigned long keep)
{
    unsigned long i;
    if (d->held <= keep)
        return;
    for (i = 0; i < d->nx; i++) {
        unmake(d->x[i].obj);
        d->x[i].obj = NULL;
    }
    d->held = 0;
}

void pdf_close(struct pdf *d)
{
    unsigned long i;
    if (!d)
        return;
    for (i = 0; i < d->nx; i++)
        unmake(d->x[i].obj);
    unmake(d->trailer);
    free(d->os[0].bytes);
    free(d->os[1].bytes);
    free(d->x);
    free(d);
}

struct pdf_obj *pdf_trailer(struct pdf *d) { return d->trailer; }
struct pdf_obj *pdf_catalog(struct pdf *d) { return pdf_get(d, d->trailer, "Root"); }
unsigned pdf_pages(const struct pdf *d) { return d->pages; }

/* ---- pages */

static struct pdf_obj *page_in(struct pdf *d, struct pdf_obj *node, unsigned *n, int depth)
{
    struct pdf_obj *kids = pdf_get(d, node, "Kids");
    unsigned i;
    if (depth > MAX_DEPTH || !pdf_is(kids, PDF_ARRAY))
        return NULL;
    for (i = 0; i < kids->n; i++) {
        struct pdf_obj *kid = pdf_item(d, kids, i);
        if (!pdf_is(kid, PDF_DICT))
            continue;
        if (pdf_is(pdf_get(d, kid, "Kids"), PDF_ARRAY)) {      /* a branch: so many pages under it */
            long count = pdf_int(pdf_get(d, kid, "Count"), -1);
            struct pdf_obj *p;
            if (count >= 0 && *n >= (unsigned long)count) {
                *n -= (unsigned)count;
                continue;
            }
            if ((p = page_in(d, kid, n, depth + 1)) != NULL || count >= 0)
                return p;
        } else if (!(*n)--) {
            return kid;
        }
    }
    return NULL;
}

struct pdf_obj *pdf_page(struct pdf *d, unsigned n)
{
    return n < d->pages ? page_in(d, pdf_get(d, pdf_catalog(d), "Pages"), &n, 0) : NULL;
}

struct pdf_obj *pdf_page_attr(struct pdf *d, struct pdf_obj *page, const char *key)
{
    int up;
    for (up = 0; page && up < MAX_DEPTH; up++) {
        struct pdf_obj *v = pdf_get(d, page, key);
        if (v)
            return v;
        page = pdf_get(d, page, "Parent");
    }
    return NULL;
}
