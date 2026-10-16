/* The toolkit's own (sdk/gui; include/glos/gui.h is the app's side). */
#ifndef GUIINT_H
#define GUIINT_H
#include <stdlib.h>
#include <string.h>

#include "glos/gui.h"

#define GUI_PAD      6                  /* a window's edge to its widgets */
#define GUI_GAP      6                  /* between widgets */
#define GUI_BTN_H    22
#define GUI_BTN_W    72                 /* a button's width at least */
#define GUI_FIELD_H  20
#define GUI_ROW_H    16                 /* a list's row, a tree's, a menu's item less its gap */
#define GUI_DBLCLICK 500                /* milliseconds (step B makes it a setting) */

/* Classes the toolkit's files share. */
extern const struct gui_class gui_box_class, gui_label_class, gui_button_class, gui_menubar_class, gui_status_class,
    gui_field_class, gui_tabs_class;
void gui_field_changed(struct gui_widget *w);          /* its text was set: the caret at its end */
void gui_tabs_step(struct gui_widget *tabs, int by);

/* A box (gui_vbox, gui_hbox; the window's root). */
struct gui_box {
    struct gui_widget w;
    int vertical, gap, pad;
    unsigned bg;                        /* GFX_NONE: its parent's background shows */
};

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

/* ---- menu.c */
int gui_menubar_key(struct gui_window *w, struct gui_ev *e, int late);  /* Alt+letter, F10, shortcuts: 1 taken.
                                           late 0: before the focus has the key (shortcuts with Ctrl or Alt); 1: after
                                           it has left it (those without) */
void gui_post_command(struct gui_window *w, int id);    /* w's on_command(id), then */
void gui_popdown_all(void);
int gui_menubar_height(struct gui_menubar *b);

#endif
