/* GLOS's shell (docs/v1/shell.md): the command interpreter as a library, one body of code for every way in.
 *
 * It knows DOS's commands and syntax, the variables, the folders of each drive, the history; it asks its host
 * for everything else: a terminal to write to, files, programs run, memory. Step S1: its host is the Terminal
 * app (apps/term/shell.c); from S2 the kernel, a thread a session.
 *
 * Nothing here waits. A line's commands run one after another until one is a program (or a command of the
 * host's that goes on): sh_line() then returns, and the host says when it has ended (sh_ended()), which runs
 * the rest of the line and then writes the prompt.
 *
 * Portable C with <string.h> and <stdarg.h> alone: no C library's files or printf. Text is DOS's (the PC's
 * code page), as COMMAND.COM's would be. */
#ifndef GLOS_SHELL_H
#define GLOS_SHELL_H

#define SH_PATH    260
#define SH_LINE    512
#define SH_HISTORY 50
#define SH_VARS    64

struct sh_dirent {
    char name[64];                      /* DOS's bytes */
    unsigned long size;
    unsigned short date, time;          /* DOS's: ((year - 1980) << 9 | month << 5 | day), (h << 11 | m << 5 | s / 2) */
    unsigned char dir;
};

enum { SH_OUT, SH_ERR, SH_PROMPT, SH_TEXT };    /* what is written: output, an error, the prompt, a file's own bytes */

#define SH_NOT_MINE  (-3)               /* extra(): not a command of the host's */
#define SH_UNDER_WAY (-2)               /* extra(): it goes on; sh_ended() when it has ended */

struct sh_host {
    void *ctx;
    /* the terminal */
    void (*write)(void *ctx, int kind, const char *s, int n);
    void (*clear)(void *ctx);
    void (*prompted)(void *ctx);                        /* the prompt is out: what is typed goes after it */
    void (*input)(void *ctx, const char *line);         /* the line being typed is this now (history, completion) */
    void (*quit)(void *ctx, int code);                  /* EXIT */
    /* files, by full paths ("C:\DIR\NAME") */
    int (*dir_read)(void *ctx, const char *dir, struct sh_dirent **out);   /* its entries (no "." or ".."), folders
                                                           first, then by name: how many, or -1; dir_free() after */
    void (*dir_free)(void *ctx, struct sh_dirent *e);
    int (*is_dir)(void *ctx, const char *path);
    int (*stat)(void *ctx, const char *path, struct sh_dirent *e);         /* 0, or -1 */
    void *(*open)(void *ctx, const char *path, int mode);                  /* 'r', 'w' (made new) or 'a'; 0: no */
    int (*read)(void *ctx, void *f, void *buf, int n);                     /* bytes read, 0 at the end */
    int (*put)(void *ctx, void *f, const void *buf, int n);                /* 0, or -1 */
    int (*close)(void *ctx, void *f);                                      /* 0, or -1: not all was written */
    int (*mkdir)(void *ctx, const char *path);
    int (*rmdir)(void *ctx, const char *path);
    int (*unlink)(void *ctx, const char *path);
    int (*rename)(void *ctx, const char *from, const char *to);
    /* programs: command is the line to run; prog the program's file where the shell found it, or 0 (a pipe or
       a redirection: the DOS's COMMAND.COM has the line); name the command's first word. 1: under way
       (sh_ended() at its end); 0: started and left to itself (detach); -1: it can't be run. */
    int (*run)(void *ctx, const char *command, const char *prog, const char *name, const char *cwd, const char *env,
               int detach);
    /* commands of the host's own (the Terminal's SSH, SFTP, SSH-KEYGEN): their names in capitals and their
       lines for HELP, each list ended by 0 */
    const char *const *extra_names, *const *extra_help;
    int (*extra)(void *ctx, int argc, char **argv);     /* its exit code, SH_UNDER_WAY or SH_NOT_MINE */
    /* memory */
    void *(*alloc)(void *ctx, unsigned n);
    void (*free)(void *ctx, void *p);
};

struct sh {
    const struct sh_host *h;
    char cwd[26][80];                   /* each drive's folder: "C:\", "C:\GUI" */
    int drive;                          /* 2: C: */
    char *vars[SH_VARS];                /* NAME=VALUE */
    unsigned char changed[SH_VARS];     /* set by this shell: given to the programs it starts */
    int nvars;
    char *hist[SH_HISTORY];
    int nhist, hist_at;
    int errorlevel;
    char rest[SH_LINE];                 /* the line's commands still to come */
    int rest_op;                        /* how they follow: 0 none, 1 &&, 2 || */
    int under_way;                      /* a program runs (or a command of the host's): sh_ended() goes on */
    void *redirect;                     /* a built-in's output goes to this file (> and >>) */
};

void sh_start(struct sh *s, const struct sh_host *h, const char *const *env);  /* env: NAME=VALUE ..., 0 at the end */
void sh_prompt(struct sh *s);
void sh_line(struct sh *s, const char *line);           /* a line typed and Enter */
void sh_ended(struct sh *s, int code);                  /* what was under way has ended with this exit code */
void sh_history(struct sh *s, int by);                  /* Up (-1), Down (1): input() with the line */
void sh_complete(struct sh *s, const char *typed);      /* Tab: input() with more of a name, or the names listed */

/* A small snprintf: %s %d %u %lu %c %%, a width, - and 0, a precision for %s (a number or *). What it wrote. */
int sh_fmt(char *out, unsigned room, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif
