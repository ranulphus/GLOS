/* PDF pages drawn into pixels, for the PDF Viewer (phase F; docs/v1/apps/pdf-viewer.md): on apps/shared/pdf.c.
   Paths filled and stroked with their edges smoothed, colours, clipping to boxes, pictures. One code for the app
   and for the host's tests. Its sums are in floating point: a Pentium's work, as the plan has it. */
#ifndef PDFDRAW_H
#define PDFDRAW_H

#include "pdf.h"

struct pdf_canvas {
    unsigned *px;                       /* 00RRGGBBh, w a row: the caller's, and the caller's to make white first */
    int w, h;
};

/* A JPEG's bytes as pixels (00RRGGBBh, malloc'd: freed here), or NULL: the app's own decoder
   (apps/shared/picfile.c). Without one a JPEG is a gray box. */
typedef unsigned *(*pdf_jpeg_fn)(const unsigned char *bytes, unsigned long n, int *w, int *h);
void pdf_draw_jpeg(pdf_jpeg_fn fn);

/* Page n's size as it is shown, in points (1/72 inch): its CropBox or MediaBox, turned as its Rotate says. PDF_OK
   or PDF_E_DAMAGED (no such page: 612 by 792 is said). */
int pdf_page_size(struct pdf *d, unsigned n, double *w, double *h);

/* Page n drawn at `scale` pixels to the point, the canvas's top left corner being (ox, oy) pixels into the page
   (a part of a page at a time, for big zooms). PDF_OK; PDF_E_MEMORY; PDF_E_DAMAGED: no such page. What of the
   page can't be drawn is left out. */
int pdf_draw(struct pdf *d, unsigned n, double scale, int ox, int oy, struct pdf_canvas *c);

/* A letter of a page's text, as it lies on the page shown at one pixel to the point (the page's top left the
   origin, x to the right, y down): its character, where it begins on its baseline, the step along the baseline
   to its end, and its size there (its font's height). */
struct pdf_letter {
    unsigned long u;
    float x, y, dx, dy, size;
};

/* Page n's letters in the order its words show them: malloc'd (the caller's to free), *count of them. Where the
   page only leaves a gap between words, or goes to another line, a space or a '\n' is put in. Text that is not
   painted (a scanned page's) is there too; letters whose characters nothing says are not. PDF_OK; PDF_E_MEMORY;
   PDF_E_DAMAGED: no such page. */
int pdf_text(struct pdf *d, unsigned n, struct pdf_letter **letters, unsigned *count);

/* Where `what` (UTF-8) is next in the letters from letter `from` on, or with `back` last before it: its first
   letter's number and *len letters, or -1. Capitals and small letters are one; a line's end is a space; curled
   quotes and long dashes are plain ones. */
long pdf_text_find(const struct pdf_letter *l, unsigned n, const char *what, unsigned from, int back, unsigned *len);

/* Letters first to last-1 as UTF-8 (lines end "\r\n"): malloc'd, NUL after, *bytes without it. NULL: no memory. */
char *pdf_text_utf8(const struct pdf_letter *l, unsigned first, unsigned last, unsigned long *bytes);

#endif
