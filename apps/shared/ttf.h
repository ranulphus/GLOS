/* TrueType fonts read: a glyph's outline, its advance, and which glyph a character is, for the PDF Viewer's text
   (phase F): the fonts PDFs embed, and the stand-ins GLOS brings (apps/pdf/fonts). GLOS's own reader: a PDF's
   font is whatever its writer put there, and every number in it is checked here against the table it lies in
   (stb_truetype, which the plan named, says of itself that it is not for fonts one cannot trust). */
#ifndef TTF_H
#define TTF_H

struct ttf {
    const unsigned char *d;             /* the font's bytes: the caller's, kept as long as this is used */
    unsigned long n;
    unsigned long glyf, glyf_len, loca, loca_len, hmtx, hmtx_len;
    unsigned long map_unicode, map_symbol, map_mac;     /* character maps found (0: not there), their lengths after */
    unsigned long len_unicode, len_symbol, len_mac;
    unsigned glyphs, long_loca, advances, em;
};

/* What draws an outline: its pieces in the font's units (y up), curves with one control point. */
struct ttf_sink {
    void (*move)(void *ctx, double x, double y);
    void (*line)(void *ctx, double x, double y);
    void (*curve)(void *ctx, double cx, double cy, double x, double y);
    void *ctx;
};

int ttf_open(struct ttf *t, const unsigned char *data, unsigned long n);       /* 0, or -1: no font to be read */
/* The glyph of a character by one of the font's maps: 0 where it has none (glyph 0 is "no such character"). */
unsigned ttf_by_unicode(const struct ttf *t, unsigned long code);
unsigned ttf_by_symbol(const struct ttf *t, unsigned code);     /* (3,0): the code, or F000h and the code */
unsigned ttf_by_mac(const struct ttf *t, unsigned code);        /* (1,0) */
int ttf_advance(const struct ttf *t, unsigned glyph);           /* in the font's units (t->em to the em) */
/* A glyph's outline given to the sink, each contour closed by its last piece. 0; -1: no such glyph, or damaged
   (what was given before the damage stays given). */
int ttf_outline(const struct ttf *t, unsigned glyph, const struct ttf_sink *s);

#endif
