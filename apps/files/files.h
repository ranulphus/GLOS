/* FILES: the file manager and the desktop's shell (files.c, folder.c, icons.c). */
#ifndef FILES_H
#define FILES_H
#include <glos/gui.h>

/* icons.c: 16- and 32-pixel pictures, palette indices (0 clear) */
enum { IC_FOLDER, IC_DRIVE, IC_FILE, IC_TEXT, IC_PICTURE, IC_PROGRAM, IC_BATCH, IC_TERMINAL, IC_NOTEPAD, IC_CALC,
       IC_IMAGES, IC_MONITOR, IC_CONTROL, IC_FILES, IC_GAME, IC_TOOL, IC_SOUND, IC_NETWORK, IC_COUNT };
const unsigned char *icon(int kind, int big);
int icon_by_name(const char *name);     /* [program] icon = NAME */
int icon_for_file(const char *name, int dir);

/* files.c */
extern char files_home[260];            /* where GLOS.EXE and the apps are: FILES.EXE's own folder */
int files_open(const char *path);       /* a folder's window, or the file opened as [open] says: 0, or -1 */
int files_run(const char *command, const char *dir);    /* a program started, nobody's */
void files_watch_event(unsigned id);
int files_app(int i, const char **title, const char **exe, int *kind);  /* GLOS's own apps beside FILES.EXE: the
                                           i-th (its command, its icon): 1, or 0 past the last */
void files_run_app(const char *exe_and_args, const char *arg);  /* one of them started (arg: one more, or NULL) */
void files_run_typed(const char *typed);        /* what was typed to run: a folder or a file opened, else run */

/* start.c: the Start menu (phase D) */
void start_open(int x, int y);          /* at the task bar's edge (GLOS_SHELL_LAUNCH); up already: away */
void start_end(void);                   /* the shell is ending: its window goes */

/* folder.c */
void folder_open(const char *dir);      /* its window (spatial: one a folder), or brought to the front */
void browser_open(const char *dir);
void folder_changed(unsigned watch);
void folders_save(void);
void folders_load(void);

#endif
