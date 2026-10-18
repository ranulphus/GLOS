/* The terminal (term.c) and GLOS's shell (shell.c): what each gives the other. */
#ifndef TERM_H
#define TERM_H

enum { T_NORMAL, T_PROMPT, T_ERROR, T_INPUT };  /* how text is drawn */

/* term.c: the window */
void term_attr(int a);
void term_put(unsigned c);                      /* a character (Unicode) */
void term_write(const char *s, int n);          /* UTF-8; n -1: to the NUL */
void term_write_dos(const char *s, int n);      /* a DOS program's bytes, in DOS's code page (M10) */
void term_print(const char *s);
void term_clear(void);
void term_prompt_done(void);                    /* the prompt is out: what is typed goes after it */
void term_input_set(const char *s);
const char *term_input(void);
void term_title(const char *t);
void term_quit(int code);
void term_copy(void);
void term_paste(void);
void term_keys(const unsigned short *k, int n);  /* keys a program left unread: the prompt's or the next one's */
void term_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
const char *term_home(void);                    /* TERM.EXE's folder (C:\GLOS) */
void term_input_hidden(int on);                 /* what is typed isn't shown: a password */
void term_size(int *cols, int *rows);           /* the window's, in characters */
struct vt;
void term_screen_on(struct vt *v);              /* an ssh session's xterm screen shows instead (M10) */
void term_screen_off(void);
void term_screen_dirty(void);                   /* its changed rows drawn again */

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
void shell_resume(int code);                    /* a built-in that went on (ssh) has ended with code */

/* ssh.c: ssh, sftp, ssh-keygen (M10) */
int ssh_command(int argc, char **argv);         /* -2: under way (shell_resume() at its end); or the exit code */
int sftp_command(int argc, char **argv);
int keygen_command(int argc, char **argv);
int ssh_active(void);                           /* a session (ssh or sftp) is open */
int ssh_screen(void);
int ssh_asking(void);                           /* the line typed is its: a question's answer, sftp's command */
void ssh_answer(const char *line);
void ssh_key(unsigned key, unsigned ch, unsigned mods);
void ssh_paste(const char *text, int n);
void ssh_resize(int cols, int rows);
void ssh_break(void);
void ssh_poll(void);                            /* its network: on GLOS_EV_NET and the tick */
void ssh_log_screens(int on);                   /* -log: the screen on stdout when it's still */

#endif
