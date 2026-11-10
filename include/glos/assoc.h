/* Kinds of file and what opens them (GLOS.CFG's [open] and [verbs]; API 2.0's SHELL ASSOC and OPEN): the rules,
 * apart from where the settings are kept. One piece of code for the kernel and the host's tests
 * (sdk/config/assoc.c). */
#ifndef GLOS_ASSOC_H
#define GLOS_ASSOC_H

#define ASSOC_EXT   8           /* a kind's key, with its NUL: ".TXT" */
#define ASSOC_LABEL 32          /* a verb's name (its kind, a colon, its label), with its NUL */
#define ASSOC_VERBS 192         /* a kind's verbs as one setting's value, with its NUL */

/* A file's kind from its path or from a bare extension ("C:\\DOCS\\A.txt", "a.txt", ".txt", "txt"): the key
   [open] and [verbs] have for it (".TXT") into out. A link ("http://...", "https://...") is the kind ".URL".
   0, or -1: it has none (no extension, or one longer than three characters). */
int assoc_kind(const char *path, char *out);
int assoc_link(const char *path);       /* 1: it is a link, not a file */
/* Is it a program DOS runs itself (.EXE, .COM, .BAT)? */
int assoc_program(const char *kind);

/* Verbs: what a program offers for kinds of file beside opening them ("Add to ZIP", "Play"), in its own profile
   ([program NAME.EXE] verbs): "KIND:Label=ARGUMENTS;KIND:Label=ARGUMENTS". The command a verb runs is the
   program, its arguments, then the file. Here a verb's name is its kind and its label together (".ZIP:Add to
   ZIP"). */
int assoc_verb_count(const char *verbs);
/* The n-th verb (0 the first): its name and its arguments into the two buffers. 0, or -1: no such verb. */
int assoc_verb_get(const char *verbs, int n, char *name, int name_room, char *args, int args_room);
/* The value with a verb added (or its arguments changed, if the name is there already; letters in either case),
   or taken out (args NULL): into out (ASSOC_VERBS bytes). 0, or -1: a bad name or arguments (a name empty or
   with ';' or '=' in it, arguments with ';'), no room, or nothing to take out. */
int assoc_verb_set(const char *verbs, const char *name, const char *args, char *out);
/* A verb's name from a kind and a label, into out (ASSOC_LABEL bytes): 0, or -1 if either is no good. */
int assoc_verb_name(const char *kind, const char *label, char *out);

/* The command line that opens `path` with `program` (a verb's command, or [open]'s program): the program and
   its arguments, then the path. A program named without a folder is in `home` (GLOS's own). Into out: its
   length, or -1 if it doesn't fit in room. */
int assoc_command(const char *program, const char *path, const char *home, char *out, int room);

#endif
