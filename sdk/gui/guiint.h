/* The toolkit's own (sdk/gui; include/glos/gui.h is the app's side). */
#ifndef GUIINT_H
#define GUIINT_H
#include <stdlib.h>
#include <string.h>

#include "glos/gui.h"

#define GUI_PAD      6                  /* a window's edge to its widgets */
#define GUI_GAP      6                  /* between widgets */
/* Heights by the UI font's (toolkit 2, C1b): with the normal size (14 rows) these are the 22, 20 and 16 pixels
   they always were; the small and the large size (12 and 17 rows) make them smaller and larger. */
#define GUI_BTN_H    (gui_font->height + 8)
#define GUI_BTN_W    (gui_font->height * 5 + 2) /* a button's width at least (72) */
#define GUI_FIELD_H  (gui_font->height + 6)
#define GUI_ROW_H    (gui_font->height + 2)     /* a list's row, a tree's, a menu's item less its gap */
#define GUI_DBLCLICK 500                /* milliseconds (step B makes it a setting) */

/* Classes the toolkit's files share. */
extern const struct gui_class gui_box_class, gui_label_class, gui_button_class, gui_menubar_class, gui_status_class,
    gui_field_class, gui_tabs_class;
void gui_fonts_start(const char *argv0);        /* (fonts.c) the UI's fonts of the theme's text size, and with
                                                   [display] font_smoothing their coverage (SMOOTH.DAT) */
void gui_fonts_apply(void);                     /* ... again, the theme having changed */
void gui_field_changed(struct gui_widget *w);          /* its text was set: the caret at its end */
void gui_tabs_step(struct gui_widget *tabs, int by);

/* A box (gui_vbox, gui_hbox; the window's root). */
struct gui_box {
    struct gui_widget w;
    int vertical, gap, pad;
    unsigned bg;                        /* GFX_NONE: its parent's background shows; GUI_BG(role): the theme's
                                           colour of that role, as it is when the box is drawn */
};
#define GUI_BG(role) (0xFE000000u | (unsigned)(role))
#define GUI_BG_RGB(v) (((v) & 0xFF000000u) == 0xFE000000u ? GUI_ROLE((v) & 0xFFu) : (v))

/* ---- core.c */
struct gui_widget *gui_hit(struct gui_widget *w, int x, int y);
void gui_measure(struct gui_widget *w, int *pw, int *ph);
void gui_place(struct gui_widget *w, int x, int y, int cw, int ch);    /* w->r, and its children laid out */
void gui_set_mnemonic(struct gui_widget *w, const char *text);
struct gui_window *gui_window_of(unsigned id);
unsigned gui_now(void);
void gui_win_realize(struct gui_window *w, int x, int y, int anywhere);
int gui_shortcut_parse(const char *s, unsigned *key, unsigned *mods);
int gui_visible_in(struct gui_widget *w);               /* it and its parents not hidden */
void gui_focus_next(struct gui_window *win, int back);

/* ---- draw.c */
void gui_fill(struct gfx_surface *s, const struct gfx_rect *r, unsigned rgb);
void gui_frame(struct gfx_surface *s, const struct gfx_rect *r, unsigned rgb);
void gui_text_latin1(struct gfx_surface *s, int x, int y, const struct gfx_font *f, const char *bytes, int n,
                     unsigned rgb);     /* bytes as Latin-1 code points (a file's text) */
int gui_latin1_w(const struct gfx_font *f, const char *bytes, int n);
void gui_draw_arrow(struct gfx_surface *s, int cx, int cy, int dir, unsigned rgb);    /* 0 up 1 down 2 left 3 right */
void gui_draw_check(struct gfx_surface *s, int x, int y, int on, int enabled);
void gui_draw_radio(struct gfx_surface *s, int x, int y, int on, int enabled);
void gui_strip_amp(char *out, const char *in, int room);       /* "&File" -> "File" */

/* ---- icons.c, glyphs.c */
struct gui_glyph_def {
    const char *name;
    unsigned char mask[4 + 2 * GUI_ICON_W];     /* GUI_MASK_MAGIC's four bytes, then 16 rows of two bytes */
};
extern const struct gui_glyph_def gui_glyphs[];
extern const int gui_glyphs_n;
#define GUI_MASK_MAGIC(p) ((p)[0] == 0xFF && (p)[1] == 'G' && (p)[2] == 'M' && (p)[3] == 0xFF)
void *gui_open_beside(const char *name);        /* (fonts.c) a file beside the program, to read: a FILE *, or NULL */

/* ---- command.c */
struct gui_cmd {
    int id;
    char *text, *shortcut, *tip;
    const unsigned char *icon;
    unsigned key, mods;                 /* the shortcut's (key 0: none) */
    unsigned state;                     /* GUI_CS_ */
};
struct gui_cmd *gui_cmd_find(struct gui_window *w, int id);
struct gui_cmd *gui_cmd_key(struct gui_window *w, unsigned key, unsigned mods);
void gui_cmd_update(struct gui_window *w);      /* the window's on_update */
void gui_cmd_free(struct gui_window *w);
extern const struct gui_class gui_toolbar_class;

/* ---- dnd.c */
extern int (*gui_dnd_hook)(const struct glos_ev *ev);  /* (core.c) DND's events: 1 if taken; NULL until used */
void gui_dnd_register(struct gui_window *win);  /* the window's DND target made again from its widgets' */
int gui_drag_start(struct gui_widget *w);       /* (a class's) its chosen things dragged: 1 if a drag began */

/* ---- field.c */
extern const struct gui_class gui_combo_class;

/* ---- basic.c */
void gui_tabs_fix(struct gui_widget *tabs);     /* pages came or went other than by the tabs: the chosen one kept
                                                   among them */

/* ---- split.c */
void gui_split_gone(struct gui_widget *split, int which);      /* pane 1 or 2 folded away with the bar (0: back) */

/* ---- core.c */
void gui_adopt(struct gui_widget *w, struct gui_widget *parent);       /* moved, with what is in it, to the end of
                                                   parent's children (another window's too) */

/* ---- menu.c */
void gui_menu_to(struct gui_menu *m, struct gui_widget *w, void (*fn)(struct gui_widget *w, int id));
                                                /* the menu's choice to w's fn, not the window's on_command */

/* ---- task.c */
extern void (*gui_libc_hook)(int on);           /* (files.c) the C library's lock taken (1) and given back (0), in a
                                                   program with tasks (task.c sets it: glos_libc_lock is in such a
                                                   program only); NULL: one thread, nothing to lock */
struct gui_window *gui_menu_popup_of(struct gui_window *owner);  /* (menu.c) its open menu's deepest pop-up (owner:
                                           the menu's window, or a pop-up of the menu with a submenu over it), or NULL */
void gui_menu_gone(struct gui_window *w);       /* (menu.c) w is being closed: an open menu that is its is forgotten */
extern int (*gui_post_hook)(unsigned code);     /* (core.c) an event of the app's own: 1 if it was the tasks'
                                                   (handled); NULL until a task is started */
void gui_modal_on(struct gui_window *w);        /* (core.c) w shown, the app's other windows deaf, without waiting
                                                   for its end (gui_modal waits) ... */
void gui_modal_off(struct gui_window *w);       /* ... and heard again */

/* ---- tip.c */
void gui_tip_hide(void);
void gui_tip_forget(struct gui_widget *w);
void gui_tip_moved(int x, int y);
unsigned gui_tip_due(unsigned now, unsigned wait);
void gui_tip_fire(unsigned now);
void gui_show_nofocus(struct gui_window *w, int sx, int sy);   /* (core.c) a pop-up shown at sx, sy of the screen,
                                                                   the keyboard staying where it is */

/* ---- menu.c */
int gui_menubar_key(struct gui_window *w, struct gui_ev *e, int late);  /* Alt+letter, F10, shortcuts: 1 taken.
                                           late 0: before the focus has the key (shortcuts with Ctrl or Alt); 1: after
                                           it has left it (those without) */
void gui_post_command(struct gui_window *w, int id);    /* w's on_command(id), then */
void gui_popdown_all(void);
int gui_menubar_height(struct gui_menubar *b);

/* res.c's: what a resource file has of the widgets whose data is their own (basic.c, field.c, menu.c) */
void gui_res_check(struct gui_widget *w, int *radio, struct gui_widget **group);
void gui_res_number(struct gui_widget *w, int *slider, int *min, int *max);
void gui_res_number_range(struct gui_widget *w, int min, int max);
int gui_res_progress_max(struct gui_widget *w);
int gui_res_field(struct gui_widget *w, int *mask);
void gui_res_field_chars(struct gui_widget *w, int chars);
int gui_res_combo_items(struct gui_widget *w, char ***items);
int gui_res_choice(struct gui_widget *w, int *chars, const char **items, int room);
void gui_res_choice_chars(struct gui_widget *w, int chars);

/* vlist.c's, for list.c (gui_list on the virtual list) */
void gui_vlist_hooks(struct gui_widget *w, void (*sort)(struct gui_widget *w, int col, int down), void (*drop)(void *ctx));
int gui_vlist_is_chosen(struct gui_widget *w, unsigned long id);
void *gui_vlist_ctx(struct gui_widget *w);     /* its model's ctx */
/* vtree.c's, for list.c (gui_tree on the virtual tree) */
void gui_vtree_hooks(struct gui_widget *w, void (*opening)(struct gui_widget *w, unsigned long id), void (*drop)(void *ctx));
void *gui_vtree_ctx(struct gui_widget *w);
void gui_vlist_sort_show(struct gui_widget *w, int col, int down);

/* rich.c */
void gui_rdoc_drop_last(struct gui_rdoc *d);
int gui_rdoc_last_empty(const struct gui_rdoc *d);

#endif
