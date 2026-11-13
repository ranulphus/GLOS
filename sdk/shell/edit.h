/* The line editor of a shell's session (docs/v1/shell.md, step S2): what is typed at a terminal that is only
 * bytes each way (an SSH connection, the Terminal app's screen, the PC's own).
 *
 * In: the bytes a terminal sends, one at a time, in the PC's code page (the console has turned UTF-8 into
 * it): characters, and the keys an xterm sends as sequences (the arrows, Home, End, Delete), Backspace, Tab,
 * Enter, Ctrl+A E K U W C D L. Out: the line drawn again after each change, with a few ANSI sequences
 * (carriage return, erase to the line's end, cursor up, down and right), over as many rows as it takes at the
 * terminal's width. The prompt is the editor's to draw again, so it is told what it is.
 *
 * Portable C with <string.h> alone; nothing waits. */
#ifndef GLOS_SHELL_EDIT_H
#define GLOS_SHELL_EDIT_H

#define ED_LINE_MAX   480               /* (a shell's line is 512 with its end) */
#define ED_PROMPT_MAX 160

enum {
    ED_NOTHING,                         /* the key is taken: nothing for the session to do */
    ED_ENTER,                           /* Enter: the line is in ed->line; ed_begin() before the next */
    ED_UP, ED_DOWN,                     /* the history is asked for: ed_set() with what it gives */
    ED_TAB,                             /* completion is asked for, of ed->line: ed_set() with more of it */
    ED_BREAK,                           /* Ctrl+C: the line is given up (a "^C" and a new row are written) */
    ED_END,                             /* Ctrl+D on an empty line: the session's end */
    ED_CLEAR                            /* Ctrl+L: the screen cleared; the prompt and the line are drawn again */
};

struct ed {
    void (*put)(void *ctx, const char *s, int n);       /* to the terminal */
    void *ctx;
    int cols;                           /* the terminal's width */
    char prompt[ED_PROMPT_MAX];         /* as written, its colours' sequences in it */
    int prompt_len, prompt_cols;        /* its bytes, and the columns it takes */
    char line[ED_LINE_MAX + 1];
    int len, cur;                       /* the line's length, and where the cursor is in it */
    int rows, row;                      /* as last drawn: the rows it took, and the cursor's (1 the first) */
    int drawn;                          /* the prompt is on the screen */
    int esc;                            /* a sequence under way: 0 none, 1 after ESC, 2 after ESC [ or ESC O */
    int arg;                            /* ... its number so far */
    int cr;                             /* the last key was Enter's CR: a LF after it is its own */
};

void ed_init(struct ed *e, void (*put)(void *ctx, const char *s, int n), void *ctx, int cols);
void ed_size(struct ed *e, int cols);                   /* the terminal's width has changed */
void ed_begin(struct ed *e, const char *prompt, int n); /* a new line: the prompt (drawn here), nothing typed */
void ed_set(struct ed *e, const char *line);            /* what is typed is this now; the cursor at its end */
void ed_away(struct ed *e);                             /* something else is about to be written: the cursor past
                                                           what is typed (ed_begin() draws a prompt again) */
int ed_key(struct ed *e, unsigned char c);              /* a byte from the terminal: ED_... */

#endif
