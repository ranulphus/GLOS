/* A PDF's fonts, for the PDF Viewer's text (phase F): what each code of a shown string is (its character), how
   wide it is, and its outline: from the TrueType font the PDF carries, or else from one of GLOS's stand-in fonts
   (apps/pdf/fonts), chosen by what the font says of itself. On apps/shared/pdf.c and ttf.c. */
#ifndef PDFFONT_H
#define PDFFONT_H

#include "pdf.h"
#include "ttf.h"

struct pdf_font;

/* Where the stand-in fonts come from: the bytes of one (family 0 sans, 1 serif, 2 mono), kept by the giver for
   good; NULL where it has none. Asked once a face. */
typedef const unsigned char *(*pdf_face_fn)(int family, int bold, int italic, unsigned long *len);
void pdf_font_faces(pdf_face_fn fn);

struct pdf_font *pdf_font_open(struct pdf *d, struct pdf_obj *dict);   /* NULL: no memory */
void pdf_font_close(struct pdf_font *f);
/* The next code of a shown string (len bytes left at s): how many bytes it took (one, or a composite font's
   two; 0 at the end). */
unsigned pdf_font_next(const struct pdf_font *f, const unsigned char *s, unsigned len, unsigned *code);
double pdf_font_width(const struct pdf_font *f, unsigned code);        /* thousandths of the font's size */
unsigned long pdf_font_unicode(const struct pdf_font *f, unsigned code);       /* its character; 0: not known */
int pdf_font_is_space(const struct pdf_font *f, unsigned code);        /* (the code word spacing is added to) */
/* The code's outline, in units of the font's size (1 is the em; y up), to the sink. 0; -1: nothing to draw. */
int pdf_font_outline(const struct pdf_font *f, unsigned code, const struct ttf_sink *s);

#endif
