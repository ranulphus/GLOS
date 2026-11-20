/* FILES: the file manager and the desktop's shell (files.c, folder.c, icons.c). */
#ifndef FILES_H
#define FILES_H
#include <glos/gui.h>

/* icons.c: 16- and 32-pixel pictures, palette indices (0 clear) */
enum { IC_FOLDER, IC_DRIVE, IC_FILE, IC_TEXT, IC_PICTURE, IC_PROGRAM, IC_BATCH, IC_TERMINAL, IC_NOTEPAD, IC_CALC,
       IC_IMAGES, IC_MONITOR, IC_CONTROL, IC_FILES, IC_GAME, IC_TOOL, IC_SOUND, IC_NETWORK, IC_PAINT, IC_ARCHIVE,
       IC_RECYCLE, IC_COUNT };
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
void files_shell(const char *line);     /* a line run by GLOS's own shell (glos exit ...) */

/* diskwarn.c: a disk nearly full is said (phase D) */
void diskwarn_start(void);

/* wall.c: the desktop's wallpaper (phase D) */
int wall_load(int w, int h);            /* made for a desktop that size from [desktop] wallpaper: 1 if there is one */
void wall_draw(struct gfx_surface *s, int ox, int oy, int w, int h);    /* it, or the desktop's colour */

/* start.c: the Start menu (phase D) */
void start_open(int x, int y);          /* at the task bar's edge (GLOS_SHELL_LAUNCH); up already: away */
void start_end(void);                   /* the shell is ending: its window goes */

/* folder.c */
void folder_open(const char *dir);      /* its window (spatial: one a folder), or brought to the front */
void browser_open(const char *dir);
void folder_changed(unsigned watch);
void folders_save(void);
void folders_load(void);
extern int folder_slow;                 /* -slowcopy, the suites': a paste waits ten seconds before each file */

/* recycle.c: the Recycle folder (phase E): what is deleted in Files, kept on its drive until it is emptied */
struct recycled {
    char drive;                         /* 'C' */
    char stored[16];                    /* its name in the drive's folder */
    char was[260];                      /* where it was */
    unsigned short date, time;          /* when it was deleted (DOS's) */
    unsigned long kb;
    int dir;
};
/* thumbs.c: a picture's thumbnail (the thumbnail view) */
struct gui_dirent;
int thumb_is_picture(const char *name);         /* by its name's end: a kind that has one */
unsigned *thumb_make(const char *path, const struct gui_dirent *e, int *pw, int *ph, int *made);       /* its pixels
                                           (malloc'd, 00RRGGBBh, at most GUI_THUMB each way), from the kept one or
                                           made now and kept (*made 1); NULL: none. For a worker's thread */
void thumb_trim(void);                  /* the kept ones' folder brought under its limits (the oldest go) */

int recycle_on(void);                   /* [desktop] recycle */
int recycle_can(const char *path);      /* a thing of this path would go there (a hard disc's, and it is on) */
int recycle_is(const char *dir, const char *name);      /* name in dir is a drive's Recycle folder itself: not shown */
int recycle_put(const char *path);      /* moved there: 0, or -1 (it is where it was) */
int recycle_list(struct recycled **out);        /* all the drives': how many (malloc'd) */
int recycle_restore(const struct recycled *it, char *where, int room);  /* back where it was (where: its path now) */
int recycle_delete(const struct recycled *it);  /* gone for good */
void recycle_window(struct gui_window *owner);  /* its window: Restore, Delete, Empty */

#endif
