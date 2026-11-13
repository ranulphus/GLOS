/* What the client (ssh.c) and its terminal (cli.c) ask of each other. */
#ifndef SSH_CLI_H
#define SSH_CLI_H

enum { T_NORMAL, T_PROMPT, T_ERROR, T_INPUT };  /* how text is written */

/* cli.c */
void term_attr(int a);
void term_write(const char *s, int n);          /* UTF-8; n -1: to the NUL. A line feed alone gets its carriage return */
void term_raw(const char *d, int n);            /* bytes for the terminal as they are (a session's) */
void term_print(const char *s);
void term_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void term_title(const char *t);
void term_prompt_done(void);                    /* a question is out: the line typed next is its answer */
void term_input_hidden(int on);                 /* what is typed isn't shown: a password */
void term_size(int *cols, int *rows);
const char *term_home(void);                    /* the program's folder (C:\GLOS) */
void shell_resume(int code);                    /* the command has ended with this exit code */

/* ssh.c */
int ssh_command(int argc, char **argv);         /* -2: under way (shell_resume() at its end); or the exit code */
int sftp_command(int argc, char **argv);
int keygen_command(int argc, char **argv);
int ssh_screen(void);                           /* a session with a pty runs: what is typed is its */
int ssh_asking(void);                           /* the line typed is its: a question's answer, sftp's command */
void ssh_answer(const char *line);
void ssh_raw(const char *d, int n);             /* what was typed, as the terminal sent it */
void ssh_resize(int cols, int rows);
void ssh_break(void);
void ssh_poll(void);                            /* its network: on GLOS_EV_NET and the tick */

#endif
