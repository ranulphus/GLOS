/* The terminal (term.c) and GLOS's shell (shell.c): what each gives the other. */
#ifndef TERM_H
#define TERM_H

enum { T_NORMAL, T_PROMPT, T_ERROR, T_INPUT };  /* how text is drawn */

/* term.c: the window */
void term_attr(int a);
void term_put(unsigned char c);
void term_write(const char *s, int n);          /* n -1: to the NUL */
void term_print(const char *s);
void term_clear(void);
void term_prompt_done(void);                    /* the prompt is out: what is typed goes after it */
void term_input_set(const char *s);
const char *term_input(void);
void term_title(const char *t);
void term_quit(int code);
void term_copy(void);
void term_paste(void);

/* shell.c: the shell */
void shell_start(void);
void shell_prompt(void);
void shell_line(const char *line);              /* a line typed and Enter */
void shell_history(int by);                     /* Up (-1), Down (1) */
void shell_complete(void);                      /* Tab */
int term_busy(void);                            /* a program runs: the keys are its */
void shell_type(const unsigned short *keys, int n);
void shell_break(void);                         /* Ctrl+Break */
void shell_poll(void);                          /* the program's output, its end */

#endif
