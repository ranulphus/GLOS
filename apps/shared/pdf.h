/* PDF files read: their objects, cross-reference tables, streams and pages, for the PDF Viewer (phase F;
   docs/v1/apps/pdf-viewer.md). GLOS's own, written from the format's specification (ISO 32000-1, the PDF Reference).
   The file is read in pieces as they are wanted, never whole. One code for the app and for the host's tests. */
#ifndef PDF_H
#define PDF_H

enum {
    PDF_OK = 0,
    PDF_E_KIND = -1,                    /* no PDF file */
    PDF_E_DAMAGED = -2,                 /* nothing of it could be made out */
    PDF_E_MEMORY = -3,
    PDF_E_FILTER = -4,                  /* a stream packed in a way that isn't unpacked here */
    PDF_E_LOCKED = -5,                  /* encrypted */
    PDF_E_READ = -6                     /* the file couldn't be read */
};

enum { PDF_NULL, PDF_BOOL, PDF_NUMBER, PDF_NAME, PDF_STRING, PDF_ARRAY, PDF_DICT, PDF_REF };

struct pdf_obj {
    unsigned char type;
    unsigned char whole;                /* a number with no fraction */
    unsigned char stream;               /* a dictionary with a stream after it: its bytes begin at `at` in `from` */
    unsigned n;                         /* a string's or a name's bytes; an array's items; a dictionary's keys */
    union {
        double num;                     /* (a boolean: 0 or 1) */
        char *s;                        /* a name (no slash) or a string: n bytes and a 0 after them */
        struct pdf_obj **item;          /* an array's */
        struct { char **key; struct pdf_obj **value; } d;
        struct { unsigned num, gen; } ref;
    } u;
    unsigned long at;                   /* a stream's first byte: in the file, or in its object stream's bytes */
    int from;                           /* 0: the file; else the number of the object stream it lies in (never: a
                                           stream is no part of an object stream; kept for what is damaged) */
};

struct pdf;

/* How the file is read: n bytes at `at` into buf: how many were read (fewer at the file's end; 0: none). */
typedef unsigned long (*pdf_read_fn)(void *ctx, unsigned long at, void *buf, unsigned long n);

/* A file of `size` bytes opened: its cross-reference tables read (or, if they are damaged, made anew by going
   through the file), its pages counted. NULL and *err. */
struct pdf *pdf_open(pdf_read_fn read, void *ctx, unsigned long size, int *err);
void pdf_close(struct pdf *d);

unsigned pdf_pages(const struct pdf *d);
struct pdf_obj *pdf_page(struct pdf *d, unsigned n);            /* page n (from 0): its dictionary, or NULL */
/* What a page has, itself or by its parents in the page tree ("Resources", "MediaBox", "CropBox", "Rotate"). */
struct pdf_obj *pdf_page_attr(struct pdf *d, struct pdf_obj *page, const char *key);
struct pdf_obj *pdf_trailer(struct pdf *d);
struct pdf_obj *pdf_catalog(struct pdf *d);
/* Which page (from 0) object `num` is, by its place in the tree of pages; -1: none of that tree's. */
long pdf_page_number(struct pdf *d, unsigned num);

/* Objects. Every object given here is the document's and stays good until pdf_trim or pdf_close. A reference is
   followed (to PDF_NULL where it leads nowhere); NULL is taken as a null object: these never fail. */
struct pdf_obj *pdf_object(struct pdf *d, unsigned num);        /* object num, or NULL */
struct pdf_obj *pdf_resolve(struct pdf *d, struct pdf_obj *o);  /* o, or what it refers to */
struct pdf_obj *pdf_get(struct pdf *d, struct pdf_obj *dict, const char *key);  /* resolved; NULL: not there */
struct pdf_obj *pdf_item(struct pdf *d, struct pdf_obj *array, unsigned i);
int pdf_is(const struct pdf_obj *o, int type);
double pdf_num(const struct pdf_obj *o, double otherwise);
long pdf_int(const struct pdf_obj *o, long otherwise);
const char *pdf_name(const struct pdf_obj *o);                  /* a name's text, or "" */
const char *pdf_string(const struct pdf_obj *o, unsigned *len); /* a string's bytes, or "" */
int pdf_name_is(const struct pdf_obj *o, const char *name);
/* A string meant to be read (a title, a name in the outline) as UTF-8: malloc'd, the caller's; "" for what is no
   string; NULL: no memory. Such strings are in the format's own table of characters, or in UTF-16 after a mark. */
char *pdf_text_string(const struct pdf_obj *o);

/* A stream's bytes, unpacked through its filters (Flate with its predictors, LZW, ASCII85, ASCIIHex, RunLength);
   the caller frees them. *last: the name of a filter left undone where one is (a JPEG's "DCTDecode": the bytes
   are then as that filter wants them), else "". NULL and *err. */
unsigned char *pdf_stream(struct pdf *d, struct pdf_obj *stream, unsigned long *len, const char **last, int *err);

/* The loaded objects let go where they take more than `keep` bytes (between pages: nothing given before is
   good after). */
void pdf_trim(struct pdf *d, unsigned long keep);

/* A content stream's words (what draws a page): objects, and the operators after them. */
struct pdf_words {
    const unsigned char *p, *end;       /* what is left of the stream's bytes */
    struct pdf *d;
};
/* The next word: 1 and *obj, an object (the caller's: pdf_free); 2 and op, an operator's name (cut to 15
   letters); 0 at the end. */
int pdf_word(struct pdf_words *w, struct pdf_obj **obj, char op[16]);
void pdf_free(struct pdf_obj *o);

#endif
