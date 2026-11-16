/* GLOS's toolkit (PRD D49, §10.6; docs/gui.md): widgets for GLOS apps, on
 * the 2D library (glos/gfx.h) and the API (2.0: glos/api.h, through libglos's glos/glos.h), in sdk/gui.
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
#include "glos/glos.h"
#include "glos/gfx.h"
#ifdef __cplusplus
extern "C" {
#endif

/* ---- the look: the theme's colours, by what they are for (toolkit 2, docs/v1/toolkit-2.md 5; API 2.0's THEME).
   gui_theme is the theme in force: read at gui_init and again when it changes (everything is then laid out and
   drawn again; an app that draws for itself is told by GLOS_EV_THEME through gui_on_event). Before gui_init, and
   under a GLOS with no themes, it is Slate (PRD D7). Every widget and every app draws with these and never with
   a colour of its own: the built-in themes' colours are all of the 8-bit palette's cube, so a window is the same
   picture at every depth. */
extern struct glos_theme gui_theme;
#define GUI_ROLE(role) (gui_theme.colour[role])
#define GUI_FACE        GUI_ROLE(GLOS_ROLE_FACE)        /* a window's background, buttons ... */
#define GUI_TEXT        GUI_ROLE(GLOS_ROLE_FACE_TEXT)   /* ... and text on it */
#define GUI_FIELD       GUI_ROLE(GLOS_ROLE_FIELD)       /* where the user types or chooses: fields, lists, menus ... */
#define GUI_FIELD_TEXT  GUI_ROLE(GLOS_ROLE_FIELD_TEXT)  /* ... and text there */
#define GUI_LINE        GUI_ROLE(GLOS_ROLE_LINE)        /* outlines and rules (text has its own roles) */
#define GUI_DIM         GUI_ROLE(GLOS_ROLE_DIM)         /* what can't be used now; a pressed button */
#define GUI_SHADE       GUI_ROLE(GLOS_ROLE_SHADE)       /* scroll bars' troughs, a slider's track */
#define GUI_DARK        GUI_ROLE(GLOS_ROLE_DARK)        /* strong fills: headers (text on it: GUI_CHOSEN_TEXT) */
#define GUI_CHOSEN      GUI_ROLE(GLOS_ROLE_CHOSEN)      /* what is chosen: its ground ... */
#define GUI_CHOSEN_TEXT GUI_ROLE(GLOS_ROLE_CHOSEN_TEXT) /* ... and its text */
#define GUI_MARK        GUI_ROLE(GLOS_ROLE_MARK)        /* the accent (D7's one colour) ... */
#define GUI_MARK_TEXT   GUI_ROLE(GLOS_ROLE_MARK_TEXT)   /* ... and text on it */
#define GUI_LINK        GUI_ROLE(GLOS_ROLE_LINK)        /* a link's text, on a field */

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
    GUI_EV_TIMER,                       /* its timer (gui_timer) */
    GUI_EV_WHEEL                        /* the mouse's wheel turned over it: x, y, wheel (to it, then its parents
                                           until one takes it; a scroll bar's widget scrolls) */
};

struct gui_ev {
    int type;
    int x, y;                           /* the pointer, in the window's coordinates */
    unsigned buttons;                   /* the buttons down now (1 left, 2 right, 4 middle) */
    unsigned button;                    /* PRESS, RELEASE: the one that changed */
    unsigned clicks;                    /* PRESS: 1, or 2 for the second of a double click */
    unsigned key, ch, mods;             /* KEY: as GLOS's key event has them */
    unsigned time;                      /* milliseconds (glos_ticks) */
    int wheel;                          /* WHEEL: notches, positive away from the user (up) */
};

/* Keys (GLOS's key codes: set 1 make codes, 100h added for an E0 prefix). */
#define GUI_K_ESC    0x01
#define GUI_K_BACK   0x0E
#define GUI_K_TAB    0x0F
#define GUI_K_ENTER  0x1C
#define GUI_K_SPACE  0x39
#define GUI_K_F1     0x3B
#define GUI_K_F4     0x3E
#define GUI_K_F10    0x44
#define GUI_K_MENU   0x15D              /* the Menu key (a context menu, as Shift+F10) */
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
    GUI_ON_ACTIVATE,                    /* an item was opened (Enter, a double click); a field's Enter */
    GUI_ON_SORT,                        /* a virtual list's column head was pressed: the app sorts (gui_vlist_sort) */
    GUI_ON_CONTEXT,                     /* its context menu is about to open (gui_context): the app may change the
                                           menu for what is chosen there */
    GUI_ON_CLOSE,                       /* a page's close was asked (tabs with GUI_TABS_CLOSE: gui_tabs_closing) */
    GUI_ON_MOVE,                        /* a page was moved along the tabs (GUI_TABS_MOVE): the pages' order is new */
    GUI_ON_NEW,                         /* the tabs' "+" was pressed (GUI_TABS_NEW): the app adds a page */
    GUI_ON_DROP                         /* a title was dragged out of the tabs' strip and let go (GUI_TABS_DRAG:
                                           gui_tabs_dropped) */
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
    char *tip;                          /* its tooltip (gui_tip), or NULL */
    struct gui_menu *context;           /* its context menu (gui_context), or NULL: its parent's */
    unsigned mnemonic;                  /* the letter after "&" in it (lower case), or 0 */
};

/* ---- windows */
#define GUI_W_FIXED   1                 /* GLOS_WIN_FIXED */
#define GUI_W_POPUP   2                 /* a pop-up (menus): bare, above the others */
#define GUI_W_DIALOG  4                 /* fixed, and sized to what it holds */
#define GUI_W_DESKTOP 8                 /* GLOS_WIN_DESKTOP: under every window, the whole desktop */
#define GUI_W_BARE    16                /* GLOS_WIN_BARE */
#define GUI_W_TOOL    32                /* a tool window (GLOS_WK_TOOL): framed, in front of its owner (set
                                           w->owner before it is shown), no button of its own: a floating pane */

struct gui_menubar;
struct gui_cmd;

struct gui_window {
    struct gui_window *next;
    struct gui_window *owner;           /* a pop-up's or a dialog's */
    struct gui_window *modal_prev;      /* (while it is the modal window without gui_modal: the one before it) */
    unsigned id;                        /* GLOS's: the window's handle */
    unsigned flags;                     /* GUI_W_ */
    struct {                            /* as it was asked for: where, GLOS_WF_ANYWHERE or not, its title */
        int x, y;
        unsigned flags;
        char title[64];
    } gw;
    struct glos_pixels px;              /* where its pixels are (after it is made, and after every change of size) */
    struct gfx_surface s;               /* the window's pixels (found again before each use: glos_ptr) */
    struct gfx_surface back;            /* drawn here first, then copied in whole: the composer never sees half */
    void *back_mem;
    int w, h;                           /* the client area */
    int active;                         /* it has the keyboard */
    struct gui_widget *root;            /* a vertical box the size of the client area */
    struct gui_widget *focus, *capture, *hover;
    struct gui_menubar *menubar;
    struct gui_widget *toolbar;         /* its toolbar (gui_toolbar), or NULL */
    struct gfx_region dirty;
    int relayout, shown, closing;
    int result;                         /* a modal window's answer (gui_modal) */
    void (*on_close)(struct gui_window *w);     /* the user asked for it to close (NULL: it closes) */
    void (*on_command)(struct gui_window *w, int id);   /* a menu item, a toolbar's button or a shortcut */
    void (*on_update)(struct gui_window *w);    /* which commands can be used now? (gui_command_enable, _check):
                                                   asked before a menu opens, before a shortcut is looked up and
                                                   before a toolbar is drawn */
    struct gui_cmd *cmds;                       /* its commands (gui_command) */
    int ncmds;
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
void gui_tip(struct gui_widget *w, const char *text);  /* its tooltip: shown by the pointer once it has rested over
                                           the widget (NULL: none). A toolbar's buttons have their commands' */
void gui_tip_at(struct gui_widget *w, int key, const char *text);      /* for a widget of an app's own, from its
                                           handling of GUI_EV_MOVE: what a tooltip here would say (key: which part
                                           of the widget; text NULL: nothing here) */
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
/* A splitter (toolkit 2): two panes, made in it by the app (the first, then the second), and a bar between them,
   side by side (vertical 0: the bar upright) or one above the other (1). at: the first pane's size in pixels, kept
   as the window's size changes; less than 0: minus the second pane's, kept instead. The bar is dragged, or takes
   the keyboard between the panes in Tab's order: the arrows move it 8 pixels (Ctrl: 1); Home folds the first pane
   away, End the second, the same key again unfolds it. GUI_ON_CHANGE (the split's gui_on) when the user moved or
   folded it, for the app to keep gui_split_get in its settings. */
struct gui_widget *gui_split(struct gui_widget *parent, int vertical, int at);
int gui_split_get(struct gui_widget *split);            /* `at` as it is now (the place when unfolded) */
void gui_split_set(struct gui_widget *split, int at);   /* (unfolded) */
void gui_split_min(struct gui_widget *split, int first, int second);   /* the least each pane has while both fit (32) */
int gui_split_folded(struct gui_widget *split);         /* 0, or 1 / 2: that pane folded away */
void gui_split_fold(struct gui_widget *split, int which);      /* fold pane 1 or 2 away (0: neither) */
/* Docking panes (toolkit 2): a window's main area, the app's content in its centre (gui_dock_centre, a vbox) and
   panes at its left, right and bottom, several at a side as tabs, or floating in tool windows; a side with none is
   folded away. A pane's title: a close box (hidden), dragged along to reorder, and a context menu to dock it at
   another side, float it or hide it. GUI_ON_CHANGE (the dock's gui_on) when the user changed the layout. */
enum { GUI_DOCK_LEFT, GUI_DOCK_RIGHT, GUI_DOCK_BOTTOM, GUI_DOCK_FLOAT };
struct gui_widget *gui_dock(struct gui_widget *parent);
struct gui_widget *gui_dock_centre(struct gui_widget *dock);
struct gui_widget *gui_pane(struct gui_widget *dock, int id, const char *title, int side);     /* the pane's vbox,
                                           for what goes in it (id: the app's, not 0); NULL: none more (16) */
void gui_pane_show(struct gui_widget *dock, int id, int on);   /* hidden and shown again (the View menu's checks) */
int gui_pane_shown(struct gui_widget *dock, int id);
void gui_pane_move(struct gui_widget *dock, int id, int side); /* (and shown) */
int gui_pane_side(struct gui_widget *dock, int id);
int gui_dock_save(struct gui_widget *dock, char *buf, int room);       /* the layout as one line of text, for the
                                           app's settings: its length, or -1 if room is short */
int gui_dock_load(struct gui_widget *dock, const char *text);  /* and back: 0, or -1 (not a layout); panes it
                                           doesn't name stay hidden */
struct gui_widget *gui_tabs(struct gui_widget *parent);
struct gui_widget *gui_tab(struct gui_widget *tabs, const char *title);        /* the page: a vbox */
int gui_tabs_get(struct gui_widget *tabs);
void gui_tabs_set(struct gui_widget *tabs, int page);
enum { GUI_TABS_ROWS, GUI_TABS_SCROLL, GUI_TABS_LIST };
#define GUI_TABS_CLOSE 1                /* a close box on each title; its press, the middle button on a title or
                                           Ctrl+F4 in the pages: GUI_ON_CLOSE (the app removes the page if it agrees) */
#define GUI_TABS_MOVE  2                /* a title dragged along the strip moves its page (GUI_ON_MOVE) */
#define GUI_TABS_NEW   4                /* a "+" at the strip's end (GUI_ON_NEW) */
#define GUI_TABS_MENU  8                /* a button at the strip's end that drops a list of every title */
#define GUI_TABS_DRAG  16               /* (with GUI_TABS_MOVE) a title dragged well out of the strip is dropped
                                           there, not moved along: GUI_ON_DROP (a docked pane: dock.c) */
int gui_tabs_dropped(struct gui_widget *tabs, int *x, int *y);  /* at GUI_ON_DROP: the page, and where it was let go
                                           (the window's coordinates) */
void gui_tabs_flags(struct gui_widget *tabs, unsigned flags);  /* (the strip's styles, not GUI_TABS_LIST) */
int gui_tabs_closing(struct gui_widget *tabs);         /* at GUI_ON_CLOSE: the page asked to close */
struct gui_widget *gui_tabs_page(struct gui_widget *tabs, int page);   /* a page by its place, or NULL */
void gui_tab_remove(struct gui_widget *tabs, int page);        /* a page gone (its widgets too); the one after it
                                           chosen if it was the chosen one (else the one before) */
void gui_tabs_style(struct gui_widget *tabs, int style);       /* titles that don't fit in a row: in more rows
                                           (GUI_TABS_ROWS, the default), one row with arrows that scroll it
                                           (GUI_TABS_SCROLL), or a list down the left (GUI_TABS_LIST); each laid out
                                           for the width the tabs have, so a wider window takes fewer rows */

/* ---- simple widgets */
struct gui_widget *gui_label(struct gui_widget *parent, const char *text);
void gui_label_style(struct gui_widget *w, const struct gfx_font *font, unsigned rgb, int align);   /* -1 left, 0
                                           centred, 1 right; rgb GFX_NONE: the theme's text, whatever the theme
                                           comes to be (a colour given stays as given) */
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
int gui_status_section(struct gui_widget *status, int width);  /* a section more at its right end, so wide (pixels):
                                           its number, from 1 (the text gui_set_text sets is section 0's, which has
                                           what the others leave) */
void gui_status_set(struct gui_widget *status, int section, const char *text, const unsigned char *icon);
                                        /* its text, and a GUI_ICON_W icon before it (or NULL) */
void gui_status_progress(struct gui_widget *status, int section, int value, int max);  /* a small bar there instead
                                           (max 0: the text again) */
struct gui_widget *gui_sep(struct gui_widget *parent);         /* a line across */

/* ---- numbers */
struct gui_widget *gui_spin(struct gui_widget *parent, int min, int max, int value);
struct gui_widget *gui_slider(struct gui_widget *parent, int min, int max, int value);
int gui_value(struct gui_widget *w);    /* a spin's, a slider's, a choice's (its item), a progress bar's */
void gui_set_value(struct gui_widget *w, int value);

/* ---- text */
struct gui_widget *gui_field(struct gui_widget *parent, int chars);    /* one line */
/* A combo box (toolkit 2): a field with a drop button and a list (the app's items, and its history). Typing drops
   the items that begin with what is typed, the field keeping the keys (Up, Down, Enter, Esc steer the list);
   Alt+Down or F4 drops all of them. Its text is the value; GUI_ON_CHANGE as it changes, GUI_ON_ACTIVATE at Enter. */
struct gui_widget *gui_combo(struct gui_widget *parent, int chars);
void gui_combo_add(struct gui_widget *combo, const char *text);
void gui_combo_add_top(struct gui_widget *combo, const char *text);    /* at the top, once (moved if it was there) */
void gui_combo_clear(struct gui_widget *combo);
void gui_combo_history(struct gui_widget *combo, int keep);    /* a text accepted by Enter goes to the top of the
                                           list, which keeps so many (0: no history) */
void gui_combo_source(struct gui_widget *combo, void (*fn)(struct gui_widget *combo, const char *typed));
                                        /* asked before the list is shown for what is typed: the app may clear and
                                           add the items to suit it (Files' folders) */
int gui_combo_count(struct gui_widget *combo);
const char *gui_combo_item(struct gui_widget *combo, int i);
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
/* GLOS's own icons, by name (docs/gui.md 1.11). Wherever a 16-pixel icon is taken (a command's, a row's, a
   node's, a status section's) either kind may be given. */
const unsigned char *gui_icon(const char *name, int big);      /* a colour icon ("folder", "file-text", "drive",
                                           "app-files" ...: sdk/gui/icons/colour.list) of GUI_ICON_W, or big:
                                           GUI_BIG_W; from ICONS.DAT beside the program. NULL: no such icon */
const unsigned char *gui_glyph(const char *name);              /* a one-colour glyph ("save", "cut", "undo" ...:
                                           sdk/gui/icons/glyphs.list), drawn in the text's colour where it is
                                           shown: for toolbars and menus. NULL: no such glyph */

/* ---- the virtual list (toolkit 2; docs/v1/toolkit-2.md 2): a list that keeps no rows. The app's model says
   how many there are and, for those on the screen alone, what is in them; rows may go on arriving while the
   list shows. What is chosen is kept by the rows' ids, so it survives sorting and arrivals. */
#define GUI_VCOLS 8
struct gui_vrow {
    const char *cell[GUI_VCOLS];        /* its cells' texts (the model's to keep until it is next asked) */
    const unsigned char *icon;          /* a GUI_ICON_W icon before the first cell, or NULL */
    const unsigned char *big;           /* a GUI_BIG_W icon for the icon view (NULL: the small one, in its middle) */
    unsigned long id;                   /* the row's own, whatever place it has (not 0) */
    const unsigned *thumb;              /* GUI_VIEW_THUMBS: a picture, 00RRGGBBh pixels, thumb_w by thumb_h (at most
                                           GUI_THUMB of each shown), the model's to keep until it is next asked; NULL:
                                           the big icon in its place (a picture not made yet, a folder) */
    int thumb_w, thumb_h;
};
#define GUI_THUMB 96
struct gui_model {
    int (*count)(void *ctx);            /* the rows there are now */
    void (*row)(void *ctx, int row, struct gui_vrow *out);     /* a row's content */
    int (*find)(void *ctx, unsigned long id);  /* the place of the row with that id now, or -1 (the function NULL:
                                           the list looks a screen either side of where the row was) */
    int (*match)(void *ctx, const char *typed, int from);      /* the first row from `from` on (and round to the
                                           start) whose first cell begins with what was typed, or -1 (NULL: typing
                                           does nothing): the app's search, since only it has all the rows */
    void *ctx;
};
struct gui_widget *gui_vlist(struct gui_widget *parent, int columns, const char *const *heads, const int *widths,
                             const struct gui_model *model);   /* heads NULL: no header; widths in pixels (NULL:
                                           100 each; the last column has what is left) */
void gui_vlist_changed(struct gui_widget *w);          /* rows came, went, or were sorted: drawn again, the current
                                           row found by its id */
int gui_vlist_get(struct gui_widget *w);               /* the current row's place, or -1 */
unsigned long gui_vlist_id(struct gui_widget *w);      /* ... its id, or 0 */
void gui_vlist_set(struct gui_widget *w, int row);     /* the current row, chosen alone (-1: none) */
void gui_vlist_multi(struct gui_widget *w, int on);    /* more than one row chosen (Ctrl, Shift) */
enum { GUI_VIEW_DETAIL, GUI_VIEW_ICONS, GUI_VIEW_SMALL, GUI_VIEW_THUMBS };
void gui_vlist_view(struct gui_widget *w, int view);   /* rows of cells under the header (GUI_VIEW_DETAIL), or big
                                           icons in a grid with the first cell's text under each (GUI_VIEW_ICONS:
                                           the arrows move four ways; with gui_vlist_multi, a press on nothing
                                           held and moved is a rubber band that chooses the icons it touches);
                                           GUI_VIEW_SMALL: small icons in a grid, each name beside its icon;
                                           GUI_VIEW_THUMBS: the rows' thumbnails (else their big icons), each
                                           name under; the arrows and the band as in the icon view */
int gui_vlist_chosen(struct gui_widget *w, unsigned long *ids, int room);      /* how many rows are chosen, and the
                                           ids of as many as there is room for */
void gui_vlist_sort(struct gui_widget *w, int *col, int *down);        /* at GUI_ON_SORT: the column whose head was
                                           pressed, and whether downward (a second press on the same head) */
void gui_vlist_columns(struct gui_widget *w, int *order, int *widths);   /* the columns as they are: the model's
                                           column at each place, left to right, and each column's width by the
                                           model's number (either NULL), for the app to keep */
void gui_vlist_set_columns(struct gui_widget *w, const int *order, const int *widths);    /* and back (an order
                                           that doesn't name each column once is not used; either NULL) */

/* ---- the virtual tree (toolkit 2; docs/v1/toolkit-2.md 2): a tree that keeps no texts. The app's model says a
   node's children's ids when the node is opened, and a node's text, icon and badge when it is drawn. */
struct gui_tnode {
    const char *text;                   /* (the model's to keep until it is next asked) */
    const unsigned char *icon;          /* a GUI_ICON_W icon, or NULL */
    const char *badge;                  /* a short text at the row's right end (a count), or NULL */
    int may;                            /* it has, or may have, children: a box to open it */
};
struct gui_tmodel {
    int (*children)(void *ctx, unsigned long id, unsigned long *ids, int room, int from);  /* the ids of the node's
                                           children (id 0: the top's) from the from'th on, as many as there is
                                           room for: how many it has in all */
    void (*node)(void *ctx, unsigned long id, struct gui_tnode *out);
    void *ctx;
};
struct gui_widget *gui_vtree(struct gui_widget *parent, const struct gui_tmodel *model);
unsigned long gui_vtree_get(struct gui_widget *w);     /* the current node's id, or 0 */
int gui_vtree_set(struct gui_widget *w, unsigned long id);     /* the current node (one on show: its parents open):
                                           0, or -1 */
int gui_vtree_open(struct gui_widget *w, unsigned long id, int on);    /* a node on show opened or closed: 0, or -1 */
void gui_vtree_changed(struct gui_widget *w, unsigned long id);        /* a node's children changed (0: the top's):
                                           asked for again; what was open under it, and the current node, are
                                           found by their ids */

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

/* ---- commands (toolkit 2; docs/v1/toolkit-2.md 1): what the app can be asked to do, each set once for its
   window; menus and toolbars show them, and follow when one changes. on_command is told the id. */
void gui_command(struct gui_window *w, int id, const char *text, const char *shortcut, const unsigned char *icon,
                 const char *tip);      /* made, or changed: "&Save", "Ctrl+S" (or NULL), a GUI_ICON_W icon (or NULL:
                                           kept by the caller), a tooltip (or NULL) */
void gui_command_enable(struct gui_window *w, int id, int on);
void gui_command_check(struct gui_window *w, int id, int on);  /* a tick in a menu, a toolbar's button held in */
void gui_command_radio(struct gui_window *w, int id, int on);  /* one of a group: a dot when it is the one chosen */
unsigned gui_command_state(struct gui_window *w, int id);      /* GUI_CS_ */
#define GUI_CS_OFF     1                /* it can't be used now */
#define GUI_CS_CHECKED 2
#define GUI_CS_RADIO   4

/* ---- the toolbar (toolkit 2; docs/v1/toolkit-2.md 7): a bar under the menu bar whose buttons show commands:
   an icon (and the text, if asked for; the text alone where a command has no icon), greyed while the command
   can't be used, held in while it is checked. What doesn't fit goes into a menu at the bar's end. */
struct gui_widget *gui_toolbar(struct gui_window *win);        /* the window's (made at the first call) */
void gui_toolbar_command(struct gui_widget *bar, int id);
void gui_toolbar_sep(struct gui_widget *bar);
void gui_toolbar_text(struct gui_widget *bar, int on);         /* the commands' texts beside their icons */

/* ---- menus */
struct gui_menu;
struct gui_menubar *gui_menubar(struct gui_window *w);
struct gui_menu *gui_menu(struct gui_menubar *bar, const char *title);
struct gui_menu *gui_menu_new(void);    /* for gui_popup_menu */
void gui_menu_item(struct gui_menu *m, const char *text, int id, const char *shortcut);  /* "Ctrl+S", "F3", "Del" */
void gui_menu_command(struct gui_menu *m, int id);      /* an item that shows the window's command of that id */
void gui_menu_sep(struct gui_menu *m);
struct gui_menu *gui_menu_sub(struct gui_menu *m, const char *text);   /* an item that opens a menu of its own
                                           beside it (Right or Enter, the mouse; Left or Esc closes it): that menu,
                                           to be filled. It is freed with m */
void gui_menu_clear(struct gui_menu *m);               /* its items gone */
void gui_menu_free(struct gui_menu *m);                /* a gui_menu_new one, gone */
void gui_menu_enable(struct gui_menu *m, int id, int on);
void gui_menu_check(struct gui_menu *m, int id, int on);
void gui_popup_menu(struct gui_window *w, struct gui_menu *m, int x, int y);    /* at x, y of w's client area */
void gui_context(struct gui_widget *w, struct gui_menu *m);    /* w's context menu (a gui_menu_new one; NULL: none):
                                           opened by the right button over w or what is in it, after the press
                                           has chosen what is under the pointer, and by the Menu key or Shift+F10
                                           while the keyboard is there, by the widget. Its `on` is told
                                           (GUI_ON_CONTEXT) first; the menu's commands are asked for as for any
                                           menu. The menu stays the app's to free */

/* ---- dialogs (modal) */
enum { GUI_OK = 1, GUI_CANCEL_R = 2, GUI_YES = 3, GUI_NO = 4 };
#define GUI_MB_OK     1
#define GUI_MB_OKCANCEL 2
#define GUI_MB_YESNO  3
#define GUI_MB_YESNOCANCEL 4
int gui_message(struct gui_window *owner, const char *title, const char *text, int buttons);    /* GUI_OK ... */
int gui_ask(struct gui_window *owner, const char *title, const char *prompt, char *buf, int room);  /* 1: OK */
int gui_message_check(struct gui_window *owner, const char *title, const char *text, int buttons, const char *check,
                      int *checked);    /* (toolkit 2) with a check box under the text ("&Don't ask again", "Do
                                           this for &all"): its state in and out through *checked, the app's to
                                           keep */
struct gui_about {
    const char *name, *version, *copyright;
    const char *licence;                /* its text, or a line naming it */
    const char *const *credits;         /* what it uses, a line each ("stb_image 2.30: pictures, public domain"),
                                           NULL-ended; the toolkit's own fonts and icons are added */
    const unsigned char *icon;          /* a GUI_BIG_W icon, or NULL */
};
void gui_about(struct gui_window *owner, const struct gui_about *a);  /* (toolkit 2) */
int gui_folder_dialog(struct gui_window *owner, const char *title, char *path, int room);     /* (toolkit 2) a
                                           folder chosen in a tree of the drives' folders, or typed; New folder
                                           makes one. path in: where to start; out: the folder. 1: chosen */
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

/* ---- background work (toolkit 2; docs/v1/toolkit-2.md 3): a function run on a thread of the app's own, while
   the window's thread goes on taking events. Only the window's thread touches widgets: the work tells it things
   by the calls below. The program is linked with @gloswrap.opt (libglos's lock round the C library). */
struct gui_task;
typedef void (*gui_task_fn)(struct gui_task *t, void *arg);
#define GUI_TASK_DIALOG 1               /* a dialog over the owner (the title, a bar, a line, Cancel) once the work
                                           has run a second */
struct gui_task *gui_task_start(struct gui_window *owner, const char *title, gui_task_fn work, void *arg,
                                void (*done)(struct gui_window *owner, void *arg, int cancelled), unsigned flags);
                                        /* work(task, arg) runs on a worker's thread when one is free; done (or
                                           NULL) on the window's thread when it has returned. NULL: no threads */
void gui_task_cancel(struct gui_task *t);       /* asked: the work looks (gui_task_cancelled) and returns early */
void gui_task_threads(int n);           /* the workers (before the first task): 2, and 1 on a 486, unless said */
/* From the work: */
void gui_task_progress(struct gui_task *t, int done, int total, const char *text);     /* (text NULL: as it was) */
int gui_task_cancelled(struct gui_task *t);
void gui_task_post(struct gui_task *t, void (*fn)(void *data), void *data);    /* fn(data) on the window's thread,
                                           in the order posted (rows for a list, a picture made): the data is
                                           the app's to free there */

/* ---- the clipboard (GLOS_FN_CLIP) */
int gui_clip_set(const char *text, size_t n);
char *gui_clip_get(size_t *n);          /* malloc'd (NUL after), or NULL */

/* ---- scroll bars, for widgets of an app's own (a part of the widget that has one) */
#define GUI_SB (gui_theme.scroll)        /* a scroll bar's width: the theme's (14 in the built-in ones) */
struct gui_sb {
    int pos, page, total;               /* the first unit shown, how many show, how many there are */
    struct gfx_rect r;                  /* the bar (empty: none) */
    int drag, drag_off;                 /* the thumb held, and where in it */
    int held;                           /* an arrow (1 up, 2 down) or the trough (3 above, 4 below) held */
};
void gui_sb_draw(struct gfx_surface *s, const struct gui_sb *b, int enabled);
int gui_sb_event(struct gui_widget *w, struct gui_sb *b, struct gui_ev *e, int line);  /* 1: taken; pos may move */
void gui_sb_clamp(struct gui_sb *b);


/* ---- GLOS's events that are not a window's keys, mouse, focus, size or close (a job's, a watched folder's, the
   shell's, a setting's): as GLOS gives them (struct glos_ev and what follows it, glos/api.h) */
void gui_on_event(void (*fn)(const struct glos_ev *ev));

/* ---- drawing, for widgets of an app's own */
void gui_draw_text(struct gfx_surface *s, int x, int y, const struct gfx_font *f, const char *text, unsigned rgb);
                                        /* with "&x" drawn as x underlined ("&&": "&") */
int gui_text_w(const struct gfx_font *f, const char *text);    /* ... its width */
void gui_draw_focus(struct gfx_surface *s, const struct gfx_rect *r);  /* a dotted line inside r */
void gui_draw_box(struct gfx_surface *s, const struct gfx_rect *r, unsigned face, int pressed);   /* a button's */
void gui_draw_field(struct gfx_surface *s, const struct gfx_rect *r, int enabled);     /* white, framed */
void gui_draw_icon(struct gfx_surface *s, int x, int y, const unsigned char *icon, int side);
void gui_draw_icon_dim(struct gfx_surface *s, int x, int y, const unsigned char *icon, int side);       /* its shape,
                                           as what can't be used now */
void gui_draw_icon_ink(struct gfx_surface *s, int x, int y, const unsigned char *icon, int side, unsigned ink);
                                        /* ... a glyph in the colour ink (the text's, where it is drawn); a colour
                                           icon as it is */
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
