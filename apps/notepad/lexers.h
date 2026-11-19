/* NOTEPAD's highlighting (lexers.c): the lexers the toolkit hasn't, and which one a file gets. */
#ifndef NOTEPAD_LEXERS_H
#define NOTEPAD_LEXERS_H

#include <glos/gui.h>

int lex_batch(void *ctx, const char *line, int n, int state, unsigned char *cls);
int lex_ini(void *ctx, const char *line, int n, int state, unsigned char *cls);
int lex_asm(void *ctx, const char *line, int n, int state, unsigned char *cls);
int lex_pascal(void *ctx, const char *line, int n, int state, unsigned char *cls);
int lex_html(void *ctx, const char *line, int n, int state, unsigned char *cls);
int lex_markdown(void *ctx, const char *line, int n, int state, unsigned char *cls);

/* The lexer for a file by its name's end (NULL: plain text), and (name may be NULL) what its kind is called. */
gui_lexer lexer_for(const char *path, const char **name);
/* The kinds there are, for a menu to choose one by hand: the i-th's lexer (NULL for the first, plain text) and
   its name; past the last, name is NULL. */
gui_lexer lexer_kind(int i, const char **name);

#endif
