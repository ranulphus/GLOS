/* Ways about a PDF, for the PDF Viewer (phase F; docs/v1/apps/pdf-viewer.md): its outline ("bookmarks": a tree of
   titles, each leading to a page) and the destinations those and links name. On apps/shared/pdf.c. */
#ifndef PDFNAV_H
#define PDFNAV_H

#include "pdf.h"

struct pdf_mark {
    char *title;                        /* UTF-8 */
    int depth;                          /* 0: one of the outline's top entries; an entry is before those under it */
    int open;                           /* the file would have those under it shown from the start */
    long page;                          /* from 0; -1: it leads nowhere in this file (to another file, to the web) */
};

/* The file's outline, in its order: malloc'd (pdf_marks_free), *count entries (0: it has none). PDF_OK or
   PDF_E_MEMORY. At most 4000 entries, 16 deep. */
int pdf_outline(struct pdf *d, struct pdf_mark **marks, unsigned *count);
void pdf_marks_free(struct pdf_mark *m, unsigned count);

/* The page a destination leads to: written out (an array that begins with a page), or the name of one in the
   file's lists of them (the catalog's Dests, or its tree of names). -1: none. */
long pdf_dest_page(struct pdf *d, struct pdf_obj *dest);

#endif
