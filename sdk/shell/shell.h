/* GLOS's shell (docs/v1/shell.md): the command interpreter as a library, one body of code for every way in.
 *
 * It knows DOS's commands and syntax, the variables, the folders of each drive, the history; it asks its host
 * for everything else: a terminal to write to, files, programs run, memory. Step S1: its host is the Terminal
 * app; from S2 the kernel, a thread a session (kernel/shell/shsess.c), which is the Terminal's too since S2c.
 * The host's test shim has a host of its own (tests/host/shimshell.c).
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
#define SH_BATCH   16                   /* batch files that CALL one another, deep */
#define SH_ARGS    32                   /* a batch file's arguments kept (%0 to %9 are the first ten after SHIFTs) */
#define SH_MACROS  1024                 /* DOSKEY's macros: NAME=text, each ended by a NUL, the last by two */

struct sh_dirent {
    char name[64];                      /* DOS's bytes */
    unsigned long size;
    unsigned short date, time;          /* DOS's: ((year - 1980) << 9 | month << 5 | day), (h << 11 | m << 5 | s / 2) */
    unsigned char dir;
    unsigned char attr;                 /* DOS's attribute byte: 01h read-only, 02h hidden, 04h system, 20h archive */
};

enum { SH_OUT, SH_ERR, SH_PROMPT, SH_TEXT };    /* what is written: output, an error, the prompt, a file's own bytes */

/* CHKDSK's (C1d): a drive's volume. */
struct sh_fsinfo {
    unsigned long unit, units, free_units;      /* bytes in each allocation unit; units in all, and free */
    char label[12];                             /* "": none */
    unsigned short date, time;                  /* the label's entry's, DOS's (0: none) */
    unsigned long serial;
    int has_serial;
};

/* MEM's figures (C1c, 2026-11-27): DOS's memory as a program started now would find it, in bytes (0: none). */
struct sh_mem {
    unsigned long conv_total, conv_free;        /* conventional memory (640 KB) and what of it is free */
    unsigned long umb_total, umb_free;          /* upper memory blocks DOS has */
    unsigned long reserved;                     /* upper memory set aside (MS-DOS 7.1's MEM says 0) */
    unsigned long xms_total, xms_free;          /* extended memory through XMS */
    unsigned long ems_total, ems_free;          /* expanded memory (ems_total 0: no EMS) */
    unsigned long largest, largest_umb;         /* the largest free conventional block (a program's room), and upper */
    int hma;                                    /* DOS is in the high memory area */
};

/* A block of DOS's memory (an MCB's, MEM /C /D /F /M): conventional memory's first, then upper memory's. */
enum { SH_MB_FREE, SH_MB_PROGRAM, SH_MB_ENV, SH_MB_DATA, SH_MB_SYSDATA, SH_MB_SYSPROG, SH_MB_DEVICE };
struct sh_mem_block {
    unsigned seg;                               /* its memory's segment */
    unsigned long bytes;
    char name[9];                               /* its owner's name ("MSDOS" DOS's own; "" free) */
    unsigned char type;                         /* SH_MB_* */
    unsigned char region;                       /* 0 conventional memory, n: upper memory's n-th region */
};

#define SH_NOT_MINE  (-3)               /* extra(): not a command of the host's */
#define SH_UNDER_WAY (-2)               /* extra(): it goes on; sh_ended() when it has ended */

struct sh_host {
    void *ctx;
    /* the terminal */
    void (*write)(void *ctx, int kind, const char *s, int n);
    int (*clear)(void *ctx);                            /* CLS: 0, or SH_UNDER_WAY (a host with no terminal of its
                                                           own may have the PC's screen cleared by a program) */
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
    /* a drive's volume (drive 0: A:): its size and what is free, in KB, and its label ("" for none). 0, or -1:
       no such drive, or it can't be read. May be 0: DF then has nothing to say. */
    int (*space)(void *ctx, int drive, unsigned long *total_kb, unsigned long *free_kb, char label[12]);
    /* programs: command is the line to run; prog the program's file where the shell found it, or 0 (a pipe or
       a redirection: the DOS's COMMAND.COM has the line); name the command's first word. 1: under way
       (sh_ended() at its end); 0: started and left to itself (detach); -1: it can't be run. */
    int (*run)(void *ctx, const char *command, const char *prog, const char *name, const char *cwd, const char *env,
               int detach);
    /* A host that finds programs itself (the kernel's, for a line with no terminal: the agent's search, as
       before there was this shell): 1, and what is no built-in is run as it is typed, prog 0, with nothing
       looked for and no "Bad command" of the shell's. May be 0. */
    int (*as_typed)(void *ctx);
    /* commands of the host's own (the Terminal's SSH, SFTP, SSH-KEYGEN): their names in capitals and their
       lines for HELP, each list ended by 0 */
    const char *const *extra_names, *const *extra_help;
    int (*extra)(void *ctx, int argc, char **argv, const char *line);   /* its exit code, SH_UNDER_WAY or
                                                           SH_NOT_MINE; line: the command as it was typed (its
                                                           variables put in), for a host that wants the spacing */
    /* memory */
    void *(*alloc)(void *ctx, unsigned n);
    void (*free)(void *ctx, void *p);
    /* (S4, batch files; each may be 0) */
    int (*seek)(void *ctx, void *f, unsigned long pos); /* a file opened to read: the next read is from pos. 0, or
                                                           -1. With none the shell reads up to the place */
    int (*key)(void *ctx);                              /* a key is waited for (PAUSE, "Terminate batch job
                                                           (Y/N)?"): SH_UNDER_WAY, and sh_key() when it comes; or
                                                           0: nobody is at a keyboard here, the shell goes on */
    int (*stop)(void *ctx);                             /* 1: Ctrl+C has come since this was last asked (asked
                                                           between a batch file's lines and a FOR's turns: a
                                                           program it ended has ended by then) */
    /* (S4b) The program run() starts next has these files for its standard input and output (full paths, or
       "NUL"; 0: its own); append: the output is added to the file. May be 0: a line with < or > around a
       program is then the host's to run whole, as before. */
    void (*io)(void *ctx, const char *in, const char *out, int append);
    /* ATTRIB (2026-11-24): a file's or a folder's attributes set (DOS's 01h, 02h, 04h, 20h). 0, or -1. May be 0:
       ATTRIB then only lists. */
    int (*setattr)(void *ctx, const char *path, unsigned attr);
    /* (C1c, 2026-11-27: TREE, LABEL, MEM, MODE, KEYB; each may be 0, and the command then says what it can't do)
       A drive's volume serial number (drive 0: A:), and its label set ("": none). 0, or -1. */
    int (*serial)(void *ctx, int drive, unsigned long *serial);
    int (*setlabel)(void *ctx, int drive, const char *label);
    /* MEM: DOS's memory (0, or -1); its blocks, conventional memory's then upper memory's (how many, or -1; the
       list is free()d after) */
    int (*mem)(void *ctx, struct sh_mem *m);
    int (*mem_blocks)(void *ctx, struct sh_mem_block **out);
    /* MODE CON: the terminal's size in characters. 0, or -1 */
    int (*term_size)(void *ctx, int *cols, int *lines);
    /* KEYB, MODE CON RATE= DELAY=: a setting of GLOS.CFG's ([region] layout, [input] repeat_rate ...) read into
       out (value 0: as it applies now, "codepage" as a number) or set, at once and saved. 0, or -1 with why in out */
    int (*setting)(void *ctx, const char *section, const char *key, const char *value, char *out, unsigned room);
    /* (C1d) CHKDSK: a drive's volume. 0, or -1 */
    int (*fsinfo)(void *ctx, int drive, struct sh_fsinfo *f);
};

/* A batch file being run (S4): where it is, how far it has got, what it was given. */
struct sh_batch {
    char path[SH_PATH];
    unsigned long pos;                  /* the next line's place in the file (it is opened anew for every line) */
    char *line;                         /* its command line, cut into words ... */
    char *arg[SH_ARGS];                 /* ... which these point at: %0 is arg[shift] */
    int argc, shift;
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
    /* batch files (S4) */
    struct sh_batch *bat[SH_BATCH];     /* those being run: the last is the one whose lines are read */
    int nbat;
    int echo_off;                       /* ECHO OFF: a batch file's lines are not shown as they are run, nor the prompt */
    int echo_was;                       /* ... as it was when the first batch file began: so again at its end */
    int level;                          /* the last program's exit code: IF ERRORLEVEL's (a command of the shell's
                                           own does not change it, as under MS-DOS) */
    struct {                            /* a FOR under way */
        char var;                       /* its variable's letter */
        char *items;                    /* what it goes through, each ended by a NUL, the last by two */
        const char *at;                 /* the next of them */
        char cmd[SH_LINE];              /* what is DOne with each */
    } loop;
    int key_for;                        /* what a key is waited for: 1 PAUSE, 2 the question after Ctrl+C */
    int rest_plain;                     /* `rest` has had its variables put in already (a batch file's line, a
                                           FOR's turn): its commands are run as they stand */
    int pipes;                          /* pipes made so far: their temporary files' names ... */
    unsigned id;                        /* ... with this in them, which the host may set after sh_start() to tell
                                           this shell's files from another's in the same folder */
    int discard;                        /* a built-in's output goes nowhere (> NUL) */
    struct {                            /* (2026-11-25) a built-in that waits for keys between its steps (key_for 3):
                                           MORE's pages, DELTREE's questions; its output's file stays open meanwhile */
        int (*step)(struct sh *s, int key);     /* its next step with the key: its exit code, or SH_UNDER_WAY */
        char path[SH_PATH];             /* MORE: its file; DELTREE: the one asked about */
        char shown[SH_PATH];            /* DELTREE: that one as it is named in the question */
        char last[64];                  /* DELTREE: the name a pattern matched last (the next is after it) ... */
        int last_dir;                   /* ... and whether it is a folder's (the host lists folders first) */
        char args[SH_LINE];             /* DELTREE: the paths still to do, each ended by a NUL, the last by two */
        const char *at;                 /* ... the one being done */
        unsigned long pos;              /* MORE: where its next page begins */
        int yes, code;                  /* DELTREE: /Y; the exit code so far */
        char choices[40];               /* CHOICE: its keys (/C), ... */
        int sens;                       /* ... told apart by case (/S) */
        int def;                        /* ... /T's key (0: none): the answer when nobody is at a keyboard */
        char var[64];                   /* SET /P: the variable, ... */
        char edit[128];                 /* ... and the line being typed */
        int nedit;
    } tool;
    char macros[SH_MACROS];             /* (C1d) DOSKEY's: what a typed line's first word becomes */
};

void sh_start(struct sh *s, const struct sh_host *h, const char *const *env);  /* env: NAME=VALUE ..., 0 at the end */
void sh_folder(struct sh *s, const char *path);         /* after sh_start(): the folder it starts in ("C:\" if never
                                                           said) */
int sh_builtin(const struct sh *s, const char *line);   /* the line's first command is the shell's own (or the
                                                           host's): it will want files, not only a program run */
void sh_prompt(struct sh *s);
void sh_line(struct sh *s, const char *line);           /* a line typed and Enter */
void sh_ended(struct sh *s, int code);                  /* what was under way has ended with this exit code */
void sh_key(struct sh *s, int ch);                      /* the key the host's key() was asked for */
void sh_history(struct sh *s, int by);                  /* Up (-1), Down (1): input() with the line */
void sh_complete(struct sh *s, const char *typed);      /* Tab: input() with more of a name, or the names listed */

/* A small snprintf: %s %d %u %lu %c %%, a width, - and 0, a precision for %s (a number or *). What it wrote. */
int sh_fmt(char *out, unsigned room, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

#endif
