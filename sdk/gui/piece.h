/* A piece table (toolkit 2, step C8; the code editor's text: sdk/gui/piece.c). Plain C, no GUI: the host tests it
 * alone (tests/host/piece_test.c). */
#ifndef PIECE_H
#define PIECE_H

#include <stddef.h>

struct pt_piece {
    unsigned char add;                  /* 0: the text as loaded; 1: what was added since */
    size_t start, len;
    size_t lines;                       /* the line ends ('\n') in it */
};

struct ptable {
    char *orig;                         /* the text as loaded (the table's: freed with it) */
    size_t orig_len;
    char *add;                          /* what was added, in the order it was (never changed, only added to) */
    size_t add_len, add_room;
    struct pt_piece *p;
    int n, room;
    size_t length, lines;               /* the text's length, and its line ends */
    int hint;                           /* the piece last found ... */
    size_t hint_at;                     /* ... and where it starts */
};

int pt_init(struct ptable *t, char *text, size_t n);   /* text: malloc'd, the table's from now on (NULL: empty) */
void pt_free(struct ptable *t);
size_t pt_length(const struct ptable *t);
size_t pt_lines(const struct ptable *t);               /* lines: line ends + 1 */
size_t pt_line_start(struct ptable *t, size_t line);   /* the first byte of a line (from 0); past the last: the end */
size_t pt_line_of(struct ptable *t, size_t at);        /* the line a byte is on */
size_t pt_read(struct ptable *t, size_t at, char *out, size_t n);      /* the bytes copied */
int pt_byte(struct ptable *t, size_t at);              /* -1 past the end */
int pt_insert(struct ptable *t, size_t at, const char *text, size_t n); /* 0, or -1 (no memory) */
int pt_delete(struct ptable *t, size_t at, size_t n);  /* 0, or -1 */
char *pt_text(struct ptable *t);                       /* the whole text, NUL ended (malloc'd) */

#endif
