/* Pictures read and written: the viewer's (apps/images) and the editor's (apps/paint), one code in both, so that
   a picture is the same in each (docs/v1/apps/paint.md). Pixels are 00RRGGBBh, a row after a row. */
#ifndef PICFILE_H
#define PICFILE_H
#include <stddef.h>

const char *pic_ext(const char *path);          /* what follows its last dot ("" if none) */
int pic_is_picture(const char *name);           /* by its name's end: one of the kinds read */
unsigned char *pic_read_all(const char *path, size_t *pn);      /* the file's bytes (malloc'd), or NULL: not read */
int pic_info(const unsigned char *d, size_t n, int *pw, int *ph);       /* its size without decoding it: 1, or 0 if
                                           that kind can't say (PCX) or it is no picture */
/* A picture's pixels from its file's bytes (malloc'd; NULL: not one that can be read): PNG, JPEG, BMP, GIF (its
   first frame), TGA and PNM by stb_image, PCX here. kind: its type ("PNG"). What is clear in it is shown over
   `back` (00RRGGBBh). */
unsigned *pic_decode(const unsigned char *d, size_t n, const char *path, int *pw, int *ph, char *kind, size_t room,
                     unsigned back);
/* A picture written as its name's end says: PNG, BMP, or JPG (quality 1 to 100; 0: 90). 0; -1 the file can't be
   written; -2 no such kind; -3 no memory. */
int pic_write(const char *path, const unsigned *px, int w, int h, int quality);
int pic_can_write(const char *name);            /* by its name's end: a kind pic_write writes */
#endif
