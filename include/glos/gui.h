/* GLOS's toolkit (PRD D49, §10.6; docs/gui.md): widgets for GLOS apps, on
 * the 2D library (glos/gfx.h) and the API (glos/api.h), in sdk/gui.
 *
 * Retained: a window holds a tree of widgets, each of which keeps its own
 * state and draws itself when asked. A change marks the widget's rectangle
 * damaged; when the app next waits for events, what is damaged is drawn
 * again, in the window's own pixels, and shown (GLOS_FN_PRESENT). Nothing is
 * drawn that didn't change: the window manager keeps the pixels (M6).
 *
 * Every widget lies in its window's client area, its rectangle in the
 * window's coordinates. Containers (boxes, forms, tabs, groups, a scrolling
 * area) place their children; the rest are leaves. A window's layout is done
 * again when something asks for it (gui_relayout) and before it is drawn.
 *
 * The keyboard first (PRD D34): Tab and Shift-Tab move the focus through the
 * widgets that take it, in the order they were made; "&" before a letter in
 * a label, button or menu marks Alt and that letter (it is drawn underlined);
 * F10 goes to the menu bar; Enter presses the window's default button and
 * Esc its cancel button.
 *
 * Text is UTF-8 everywhere; the fonts have Latin-1 and Latin Extended-A,
 * Greek, Cyrillic and common signs (M10; docs/gui.md §1.10). DOS's names are
 * bytes in its code page: gui_dos_text and gui_text_dos convert.
 *
 * The same code builds for the host (tests/host/glosshim.c in place of
 * libglos), where the tests drive it and keep its pictures. */
#ifndef GLOS_GUI_H
#define GLOS_GUI_H
#include "glos/api.h"
#include "glos/gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ---- the look (Slate, PRD D7): colours of the palette's cube, so a window is the same picture at every depth */
#define GUI_FACE   0xCCCCCCu            /* a window's background, buttons */
#define GUI_FIELD  0xFFFFFFu            /* where the user types or chooses: fields, lists, menus */
#define GUI_LINE   0x000000u            /* lines and text */
#define GUI_DIM    0x999999u            /* what can't be used now; a pressed button */
#define GUI_SHADE  0x666666u            /* scroll bars' troughs, a slider's track */
#define GUI_DARK   0x333333u            /* headers, what is chosen (with GUI_FIELD's colour on it) */
#define GUI_MARK   0xCC6633u            /* D7's one colour (the toolkit leaves it to the window manager) */

extern const struct gfx_font *gui_font, *gui_bold, *gui_mono;

/* ---- what a widget is sent */
enum {
    GUI_EV_KEY = 1,                     /* a key went down: key, ch, mods (the focus's) */
    GUI_EV_PRESS,                       /* a button went down on it: x, y, button, clicks */
    GUI_EV_RELEASE,                     /* ... and up (to the widget it went down on) */
    GUI_EV_MOVE,                        /* the pointer moved over it, or anywhere while a button pressed on it is down */
    GUI_EV_LEAVE,                       /* the pointer left it */
    GUI_EV_FOCUS,                       /* it has the keyboard */
    GUI_EV_BLUR,                        /* ... and no longer */
    GUI_EV_TIMER                        /* its timer (gui_timer) */
};

struct gui_ev {
    int type;
    int x, y;                           /* the pointer, in the window's coordinates */
    unsigned buttons;                   /* the buttons down now (1 left, 2 right, 4 middle) */
    unsigned button;                    /* PRESS, RELEASE: the one that changed */
    unsigned clicks;                    /* PRESS: 1, or 2 for the second of a double click */
    unsigned key, ch, mods;             /* KEY: as struct glos_event has them */
    unsigned time;                      /* milliseconds (glos_ticks) */
};

/* Keys (struct glos_event's `key`: set 1 make codes, 100h added for an E0 prefix). */
#define GUI_K_ESC    0x01
#define GUI_K_BACK   0x0E
#define GUI_K_TAB    0x0F
#define GUI_K_ENTER  0x1C
#define GUI_K_SPACE  0x39
#define GUI_K_F1     0x3B
#define GUI_K_F10    0x44
#define GUI_K_KPENTER 0x11C
#define GUI_K_HOME   0x147
#define GUI_K_UP     0x148
#define GUI_K_PGUP   0x149
#define GUI_K_LEFT   0x14B
#define GUI_K_RIGHT  0x14D
#define GUI_K_END    0x14F
#define GUI_K_DOWN   0x150
#define GUI_K_PGDN   0x151
#define GUI_K_INS    0x152
#define GUI_K_DEL    0x153

/* What a widget tells the app (its `on` function's `what`). */
enum {
    GUI_ON_CLICK = 1,                   /* a button was pressed (by the mouse, Space, Enter or its letter) */
    GUI_ON_CHANGE,                      /* its value changed (a check box, a field's text, a slider) */
    GUI_ON_SELECT,                      /* what is chosen in it changed (a list, a tree, tabs, a choice) */
    GUI_ON_ACTIVATE                     /* an item was opened (Enter, a double click); a field's Enter */
};

struct gui_widget;
struct gui_window;

struct gui_class {
    const char *name;
    unsigned flags;                     /* GUI_C_ */
    void (*draw)(struct gui_widget *w, struct gfx_surface *s);  /* within w->r, s already clipped there */
    int (*event)(struct gui_widget *w, struct gui_ev *e);       /* 1: taken */
    void (*measure)(struct gui_widget *w, int *pw, int *ph);    /* the size it would like */
    void (*layout)(struct gui_widget *w);                       /* its children placed in w->r */
    void (*destroy)(struct gui_widget *w);                      /* its own memory (not the widget's) */
};
#define GUI_C_FOCUS 1                   /* it takes the keyboard (in Tab's order) */

/* Widget flags. */
#define GUI_HIDDEN   0x01
#define GUI_DISABLED 0x02
#define GUI_DEFAULT  0x04               /* a button: Enter in its window */
#define GUI_CANCEL   0x08               /* a button: Esc in its window */
#define GUI_NOTAB    0x10               /* not in Tab's order (it may still take the focus from the mouse) */

struct gui_widget {
    const struct gui_class *cls;
    struct gui_window *win;
    struct gui_widget *parent, *first, *last, *next, *prev;
    struct gfx_rect r;                  /* in the window's client area */
    unsigned flags;
    int pw, ph;                         /* its size as set (0: as measured) */
    int grow;                           /* its share of what a box has to spare, along the box */
    int id;                             /* the app's */
    void (*on)(struct gui_widget *w, int what);
    void *user;                         /* the app's */
    char *text;                         /* its label (malloc'd), if it has one */
    unsigned mnemonic;                  /* the letter after "&" in it (lower case), or 0 */
};

/* ---- windows */
#define GUI_W_FIXED   1                 /* GLOS_WIN_FIXED */
#define GUI_W_POPUP   2                 /* a pop-up (menus): bare, above the others */
#define GUI_W_DIALOG  4                 /* fixed, and sized to what it holds */
#define GUI_W_DESKTOP 8                 /* GLOS_WIN_DESKTOP: under every window, the whole desktop */
#define GUI_W_BARE    16                /* GLOS_WIN_BARE */

struct gui_menubar;

struct gui_window {
    struct gui_window *next;
    struct gui_window *owner;           /* a pop-up's or a dialog's */
    unsigned id;                        /* GLOS's */
    unsigned flags;                     /* GUI_W_ */
    struct glos_window gw;
    struct gfx_surface s;               /* the window's pixels (found again before each use: glos_ptr) */
    struct gfx_surface back;            /* drawn here first, then copied in whole: the composer never sees half */
    void *back_mem;
    int w, h;                           /* the client area */
    int active;                         /* it has the keyboard */
    struct gui_widget *root;            /* a vertical box the size of the client area */
    struct gui_widget *focus, *capture, *hover;
    struct gui_menubar *menubar;
    struct gfx_region dirty;
    int relayout, shown, closing;
    int result;                         /* a modal window's answer (gui_modal) */
    void (*on_close)(struct gui_window *w);     /* the user asked for it to close (NULL: it closes) */
    void (*on_command)(struct gui_window *w, int id);   /* a menu item or shortcut */
    void (*on_resize)(struct gui_window *w);
    int (*on_key)(struct gui_window *w, struct gui_ev *e);      /* keys no widget took: 1 taken */
    void (*on_popdown)(struct gui_window *w);   /* a pop-up lost the keyboard (a press elsewhere) */
    void *user;
    unsigned last_press_time;
    int last_press_x, last_press_y;
    unsigned last_press_button;
};

int gui_init(int argc, char **argv);    /* the app (glos_init): 0, or -1 without GLOS */
struct gui_window *gui_window(const char *title, int w, int h, unsigned flags);       /* hidden */
struct gui_window *gui_window_at(const char *title, int x, int y, int w, int h, unsigned flags);
void gui_show(struct gui_window *w, int on);
void gui_close(struct gui_window *w);   /* gone, with its widgets */
void gui_close_later(struct gui_window *w);     /* ... once the event being handled is done */
void gui_title(struct gui_window *w, const char *title);
void gui_resize(struct gui_window *w, int cw, int ch);
void gui_fit(struct gui_window *w);     /* the size its widgets want */
int gui_origin(struct gui_window *w, int *x, int *y);  /* the client area's corner on the screen */
void gui_screen_size(int *w, int *h);

/* ---- the loop */
void gui_run(void);                     /* events until gui_quit() or every window is gone */
void gui_quit(int code);
int gui_exit_code(void);
int gui_step(unsigned wait_ms);         /* one turn: what is damaged drawn, then one event (waiting at most
                                           wait_ms): 1 if there was one */
int gui_modal(struct gui_window *w);    /* w shown, the app's other windows deaf, until gui_end_modal(w, r): r */
void gui_end_modal(struct gui_window *w, int result);
void gui_timer(struct gui_widget *w, unsigned ms);     /* GUI_EV_TIMER to w every ms (0: no more) */
void gui_idle(void (*fn)(void *), void *arg, unsigned ms);     /* fn every ms while the loop runs (0: off) */

/* ---- widgets in general */
void gui_damage(struct gui_widget *w);  /* to be drawn again */
void gui_damage_rect(struct gui_window *win, const struct gfx_rect *r);
void gui_relayout(struct gui_window *w);
void gui_focus(struct gui_widget *w);
void gui_enable(struct gui_widget *w, int on);
void gui_visible(struct gui_widget *w, int on);
void gui_set_text(struct gui_widget *w, const char *text);     /* a label's, a button's, a field's */
const char *gui_get_text(struct gui_widget *w);
void gui_size(struct gui_widget *w, int pw, int ph);   /* its size (0: as measured) */
void gui_grow(struct gui_widget *w, int share);
void gui_on(struct gui_widget *w, void (*on)(struct gui_widget *, int), int id);
void gui_remove(struct gui_widget *w);  /* gone, with its children */
void gui_clear(struct gui_widget *w);   /* its children gone */
struct gui_widget *gui_new(struct gui_widget *parent, const struct gui_class *cls, size_t size);

/* ---- containers */
struct gui_widget *gui_vbox(struct gui_widget *parent, int gap, int pad);
struct gui_widget *gui_hbox(struct gui_widget *parent, int gap, int pad);
struct gui_widget *gui_form(struct gui_widget *parent);        /* rows of a label and a widget: gui_row() */
struct gui_widget *gui_row(struct gui_widget *form, const char *label, struct gui_widget **label_out);  /* the row's
                                           box, for its widget(s) */
struct gui_widget *gui_group(struct gui_widget *parent, const char *title);    /* a frame with a title, a vbox in it */
struct gui_widget *gui_space(struct gui_widget *parent, int w, int h);         /* grow 1 if both are 0 */
struct gui_widget *gui_scroll(struct gui_widget *parent);      /* a vbox that scrolls up and down */
struct gui_widget *gui_tabs(struct gui_widget *parent);
struct gui_widget *gui_tab(struct gui_widget *tabs, const char *title);        /* the page: a vbox */
int gui_tabs_get(struct gui_widget *tabs);
void gui_tabs_set(struct gui_widget *tabs, int page);

/* ---- simple widgets */
struct gui_widget *gui_label(struct gui_widget *parent, const char *text);
void gui_label_style(struct gui_widget *w, const struct gfx_font *font, unsigned rgb, int align);   /* -1 left, 0
                                           centred, 1 right */
void gui_label_box(struct gui_widget *w, int on);      /* drawn in a white box, as a field (a display) */
struct gui_widget *gui_button(struct gui_widget *parent, const char *text, void (*on)(struct gui_widget *, int), int id);
struct gui_widget *gui_check(struct gui_widget *parent, const char *text, int on);
struct gui_widget *gui_radio(struct gui_widget *parent, const char *text, struct gui_widget *first_of_group);
int gui_checked(struct gui_widget *w);
void gui_set_checked(struct gui_widget *w, int on);
struct gui_widget *gui_progress(struct gui_widget *parent, int max);
void gui_progress_set(struct gui_widget *w, int value, int max);
struct gui_widget *gui_canvas(struct gui_widget *parent, int w, int h,
                              void (*draw)(struct gui_widget *, struct gfx_surface *),
                              int (*event)(struct gui_widget *, struct gui_ev *));
struct gui_widget *gui_image(struct gui_widget *parent, const unsigned *rgb, int w, int h);    /* rgb is kept */
struct gui_widget *gui_status(struct gui_window *win);         /* a bar along the bottom: gui_set_text */
struct gui_widget *gui_sep(struct gui_widget *parent);         /* a line across */

/* ---- numbers */
struct gui_widget *gui_spin(struct gui_widget *parent, int min, int max, int value);
struct gui_widget *gui_slider(struct gui_widget *parent, int min, int max, int value);
int gui_value(struct gui_widget *w);    /* a spin's, a slider's, a choice's (its item), a progress bar's */
void gui_set_value(struct gui_widget *w, int value);

/* ---- text */
struct gui_widget *gui_field(struct gui_widget *parent, int chars);    /* one line */
void gui_field_mask(struct gui_widget *w, int on);             /* shown as stars */
struct gui_widget *gui_editor(struct gui_widget *parent);      /* many lines, scrolling */
void gui_editor_set(struct gui_widget *w, const char *text, size_t n);  /* the whole text, bytes as they are */
size_t gui_editor_get(struct gui_widget *w, char *out, size_t room);    /* its length; as much as fits copied */
size_t gui_editor_length(struct gui_widget *w);
int gui_editor_modified(struct gui_widget *w);
void gui_editor_saved(struct gui_widget *w);
void gui_editor_wrap(struct gui_widget *w, int on);
void gui_editor_font(struct gui_widget *w, const struct gfx_font *f);
int gui_editor_find(struct gui_widget *w, const char *what, int match_case, int forward);   /* found and chosen */
void gui_editor_replace_sel(struct gui_widget *w, const char *text);
void gui_editor_goto(struct gui_widget *w, int line);         /* (1 is the first) */
void gui_editor_where(struct gui_widget *w, int *line, int *col);       /* the caret's (from 1) */
int gui_editor_command(struct gui_widget *w, int cmd);        /* GUI_CMD_: 1 done */
enum { GUI_CMD_CUT = 1, GUI_CMD_COPY, GUI_CMD_PASTE, GUI_CMD_DELETE, GUI_CMD_ALL, GUI_CMD_UNDO,
       GUI_CMD_END };                    /* (END: the caret to the text's end, shown at the bottom) */

/* ---- lists and trees */
struct gui_widget *gui_list(struct gui_widget *parent, int columns, const char *const *heads, const int *widths);
                                        /* heads NULL: no header; widths in pixels (the last takes what is left) */
int gui_list_add(struct gui_widget *w, const char *const *cells, const unsigned char *icon, void *user);  /* its row */
void gui_list_clear(struct gui_widget *w);
int gui_list_count(struct gui_widget *w);
int gui_list_get(struct gui_widget *w);         /* the row chosen, or -1 */
void gui_list_set(struct gui_widget *w, int row);
void *gui_list_user(struct gui_widget *w, int row);
const char *gui_list_cell(struct gui_widget *w, int row, int col);
void gui_list_sort(struct gui_widget *w, int col, int (*cmp)(struct gui_widget *, int a, int b, int col));
void gui_list_multi(struct gui_widget *w, int on);      /* more than one row chosen (Ctrl, Shift) */
int gui_list_chosen(struct gui_widget *w, int row);
void gui_list_icons(struct gui_widget *w, int big);     /* rows (0), or 32-pixel icons in a grid (1) */
/* An icon: GUI_ICON_W x GUI_ICON_W pixels, a byte each: 0 clear, else a palette index of the 8-bit palette. */
#define GUI_ICON_W 16
#define GUI_BIG_W  32

struct gui_node;
struct gui_widget *gui_tree(struct gui_widget *parent);
struct gui_node *gui_tree_add(struct gui_widget *w, struct gui_node *parent, const char *text,
                              const unsigned char *icon, void *user, int may_have_children);
void gui_tree_expand(struct gui_widget *w, struct gui_node *n, int open);      /* its `on` is told (GUI_ON_CHANGE)
                                           before a node opens, to add its children then */
void gui_tree_clear(struct gui_widget *w, struct gui_node *n);  /* n's children (NULL: everything) */
struct gui_node *gui_tree_get(struct gui_widget *w);
void gui_tree_set(struct gui_widget *w, struct gui_node *n);
struct gui_node *gui_tree_opening(struct gui_widget *w);        /* in GUI_ON_CHANGE: the node opening */
void *gui_node_user(struct gui_node *n);
const char *gui_node_text(struct gui_node *n);
struct gui_node *gui_node_parent(struct gui_node *n);
struct gui_node *gui_tree_child(struct gui_widget *w, struct gui_node *n, const char *text);     /* n's child by its
                                           text (n NULL: the top's), or NULL */

/* ---- choosing one of a few */
struct gui_widget *gui_choice(struct gui_widget *parent, int chars);   /* a button that drops a list down */
void gui_choice_add(struct gui_widget *w, const char *text);
void gui_choice_clear(struct gui_widget *w);

/* ---- menus */
struct gui_menu;
struct gui_menubar *gui_menubar(struct gui_window *w);
struct gui_menu *gui_menu(struct gui_menubar *bar, const char *title);
struct gui_menu *gui_menu_new(void);    /* for gui_popup_menu */
void gui_menu_item(struct gui_menu *m, const char *text, int id, const char *shortcut);  /* "Ctrl+S", "F3", "Del" */
void gui_menu_sep(struct gui_menu *m);
void gui_menu_enable(struct gui_menu *m, int id, int on);
void gui_menu_check(struct gui_menu *m, int id, int on);
void gui_popup_menu(struct gui_window *w, struct gui_menu *m, int x, int y);    /* at x, y of w's client area */

/* ---- dialogs (modal) */
enum { GUI_OK = 1, GUI_CANCEL_R = 2, GUI_YES = 3, GUI_NO = 4 };
#define GUI_MB_OK     1
#define GUI_MB_OKCANCEL 2
#define GUI_MB_YESNO  3
#define GUI_MB_YESNOCANCEL 4
int gui_message(struct gui_window *owner, const char *title, const char *text, int buttons);    /* GUI_OK ... */
int gui_ask(struct gui_window *owner, const char *title, const char *prompt, char *buf, int room);  /* 1: OK */
int gui_file_dialog(struct gui_window *owner, const char *title, int save, char *path, int room);  /* path in: the
                                           folder (and the name); out: the file chosen. 1: chosen */

/* ---- files, by DOS paths ("C:\GLOS\GLOS.CFG"). The host tests map each drive to a directory
   ($GLOS_HOST_DRIVES/C and so on), so an app shows the same names there as on DOS. */
struct gui_dirent {
    char name[64];                      /* DOS's bytes, for paths */
    char text[128];                     /* name as text to show (gui_dos_text) */
    unsigned long size;
    unsigned short date, time;          /* DOS's: ((year - 1980) << 9 | month << 5 | day), (h << 11 | m << 5 | s / 2) */
    unsigned char dir, attr;
};
int gui_dir_read(const char *dir, struct gui_dirent **out);    /* its entries (malloc'd; no "." or ".."), sorted:
                                           folders first, then by name; -1 if it can't be read */
int gui_drives(char *letters);          /* the drives there are, "ACD": their number */
int gui_is_dir(const char *path);
void *gui_fopen(const char *path, const char *mode);    /* a FILE * */
int gui_mkdir(const char *path);
int gui_rmdir(const char *path);
int gui_unlink(const char *path);
int gui_rename(const char *from, const char *to);
int gui_stat(const char *path, struct gui_dirent *e);
int gui_set_time(const char *path, unsigned short date, unsigned short time);
void gui_path_join(char *out, int room, const char *dir, const char *name);
void gui_path_up(char *path);           /* "C:\A\B" -> "C:\A"; "C:\A" -> "C:\" */
const char *gui_path_name(const char *path);    /* the last part */
/* DOS's names and paths are bytes in its code page (glos/cp.h, cp_active's); the toolkit's text is UTF-8 (M10). */
const char *gui_dos_text(const char *dos, char *out, int room); /* a name or path DOS has, as text to show: out */
int gui_text_dos(const char *text, char *out, int room);        /* text typed, as DOS's bytes: 0, or -1 if the
                                           code page hasn't got one of its characters ('?' there) or out is short */

/* ---- the clipboard (GLOS_FN_CLIP) */
int gui_clip_set(const char *text, size_t n);
char *gui_clip_get(size_t *n);          /* malloc'd (NUL after), or NULL */

/* ---- scroll bars, for widgets of an app's own (a part of the widget that has one) */
#define GUI_SB 14                       /* a scroll bar's width */
struct gui_sb {
    int pos, page, total;               /* the first unit shown, how many show, how many there are */
    struct gfx_rect r;                  /* the bar (empty: none) */
    int drag, drag_off;                 /* the thumb held, and where in it */
    int held;                           /* an arrow (1 up, 2 down) or the trough (3 above, 4 below) held */
};
void gui_sb_draw(struct gfx_surface *s, const struct gui_sb *b, int enabled);
int gui_sb_event(struct gui_widget *w, struct gui_sb *b, struct gui_ev *e, int line);  /* 1: taken; pos may move */
void gui_sb_clamp(struct gui_sb *b);


/* ---- events of the app's own (window 0: GLOS_EV_JOB, GLOS_EV_FILES, GLOS_EV_LAUNCH, GLOS_EV_SYSKEY) */
void gui_on_event(void (*fn)(const struct glos_event *ev));

/* ---- drawing, for widgets of an app's own */
void gui_draw_text(struct gfx_surface *s, int x, int y, const struct gfx_font *f, const char *text, unsigned rgb);
                                        /* with "&x" drawn as x underlined ("&&": "&") */
int gui_text_w(const struct gfx_font *f, const char *text);    /* ... its width */
void gui_draw_focus(struct gfx_surface *s, const struct gfx_rect *r);  /* a dotted line inside r */
void gui_draw_box(struct gfx_surface *s, const struct gfx_rect *r, unsigned face, int pressed);   /* a button's */
void gui_draw_field(struct gfx_surface *s, const struct gfx_rect *r, int enabled);     /* white, framed */
void gui_draw_icon(struct gfx_surface *s, int x, int y, const unsigned char *icon, int side);
int gui_is_focus(struct gui_widget *w);
int gui_char_w(const struct gfx_font *f, unsigned c);   /* one character's advance */
void gui_text_fit(struct gfx_surface *s, int x, int y, int w, const struct gfx_font *f, const char *text,
                  unsigned rgb);        /* cut with "..." to fit w */
void gui_send(struct gui_widget *w, int what);          /* its `on` told, as if the user did it */
int gui_window_active(struct gui_window *w);            /* it has the keyboard */



#ifdef __cplusplus
}
#endif
#endif
