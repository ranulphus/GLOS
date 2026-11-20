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

#endif
