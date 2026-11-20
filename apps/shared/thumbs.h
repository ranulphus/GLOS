/* A picture's thumbnail, made and kept (thumbs.c): Files' thumbnail view and Images' filmstrip. */
#ifndef THUMBS_H
#define THUMBS_H

struct gui_dirent;
void thumb_home(const char *dir);       /* the folder the kept ones' folder (THUMBS) is in: the programs' own.
                                           Before the first is made; without it none is kept */
int thumb_is_picture(const char *name);         /* by its name's end: a kind that has one */
unsigned *thumb_make(const char *path, const struct gui_dirent *e, int *pw, int *ph, int *made);       /* its pixels
                                           (malloc'd, 00RRGGBBh, at most GUI_THUMB each way), from the kept one or
                                           made now and kept (*made 1); NULL: none. e: the file as its folder has
                                           it (its size and date say whether the kept one is still its). For a
                                           worker's thread */
void thumb_trim(void);                  /* the kept ones' folder brought under its limits (the oldest go) */

#endif
