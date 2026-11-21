/* The PostScript kinds of font a PDF may carry, read for the PDF Viewer's text (phase F): CFF ("Type 1C", the
   compact form: FontFile3) and Type 1 (FontFile), whose glyphs are small programs ("charstrings") of moves, lines
   and curves with two control points. GLOS's own reader, written from Adobe's specifications (Technical Notes
   5176 and 5177; "Adobe Type 1 Font Format"); every count and offset is checked: the font is whatever the PDF's
   writer put there. */
#ifndef PSFONT_H
#define PSFONT_H

#include "ttf.h"

struct psfont;

/* A glyph name's character (0: not known): the opener's table of names (apps/shared/pdffont.c has one). */
typedef unsigned (*psfont_name_fn)(const char *name);

/* A CFF font's bytes (the caller's, kept as long as the font is used), or a Type 1 font's (copied: they may be
   freed): opened. NULL: nothing could be made of it. */
struct psfont *psfont_cff(const unsigned char *d, unsigned long n, psfont_name_fn name);
struct psfont *psfont_type1(const unsigned char *d, unsigned long n, psfont_name_fn name);
void psfont_close(struct psfont *f);

unsigned psfont_glyphs(const struct psfont *f);
int psfont_is_cid(const struct psfont *f);              /* a CFF whose glyphs are known by numbers (CIDs), not names */
/* A glyph by its name, by its character, by the font's own encoding of a code, by its CID: its number, or -1. */
int psfont_by_name(const struct psfont *f, const char *name);
int psfont_by_unicode(const struct psfont *f, unsigned long u);
int psfont_by_code(const struct psfont *f, unsigned code);
int psfont_by_cid(const struct psfont *f, unsigned cid);
unsigned long psfont_unicode(const struct psfont *f, int glyph);        /* a glyph's character, by its name (0: not known) */
/* A glyph's outline in ems (y up) to the sink, which must have `cubic`. 0; -1: no such glyph, or its program is
   damaged (what was given before the damage stays given). */
int psfont_outline(const struct psfont *f, int glyph, const struct ttf_sink *s);

#endif
