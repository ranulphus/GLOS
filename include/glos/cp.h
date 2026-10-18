/* DOS code pages (M10 step G; sdk/config/cp.c, cp_tables.c): DOS's file
 * names and a DOS program's output are bytes in the active code page; GLOS's
 * text is UTF-8. Pages 437, 850, 852, 866 and 737; another is taken as 437.
 * Plain C, no C library: the SDK's and the host tests'. */
#ifndef GLOS_CP_H
#define GLOS_CP_H

const unsigned short *cp_table(int page);       /* 80h-FFh as Unicode; NULL: a page GLOS hasn't got */
unsigned cp_char(int page, unsigned char b);    /* a byte as Unicode (below 80h: itself) */
int cp_byte(int page, unsigned u);              /* Unicode as the page's byte, or -1 if it hasn't got it */
int cp_to_utf8(int page, const char *in, char *out, int room);  /* bytes to UTF-8 (NUL-ended): its length */
int cp_from_utf8(int page, const char *in, char *out, int room); /* UTF-8 to bytes, '?' for what the page hasn't
                                           got: how many those were (0: all of it), -1 if out is too small */
unsigned cp_utf8_next(const char **p);          /* a UTF-8 character's code point, *p past it (a stray byte: as Latin-1) */
int cp_active(void);                            /* (libglos, DJGPP) the code page DOS has: GLOSCP in the environment,
                                                   else INT 21h 6601h's, else 437 */

#endif
