/* PAINT: GLOS's picture editor (docs/v1/apps/paint.md; phase E). Its first part: a picture drawn on with a
 * pencil, a brush, an eraser, lines, rectangles, ellipses, a flood fill and a colour picker, in two colours from
 * a palette; every step undone and done again; pictures opened (what Images shows) and saved as PNG, BMP or
 * JPEG. The picture and the tools are paintdoc.c (tested on the host by themselves); reading and writing files
 * is Images' own code (apps/shared/picfile.c).
 *
 *   PAINT [FILE]
 *
 * The left button draws in the first colour, the right in the second (the eraser always in the second: the
 * paper's). Keys: P pencil, B brush, E eraser, L line, R rectangle, O ellipse, F fill, I picker; [ and ] a
 * thinner and a wider tip; Ctrl+Z, Ctrl+Y; + and - zoom, 0 fits, 1 actual size (the view's own); the middle
 * button moves the picture about.
 * "PAINT ..." lines on stdout (the suite's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "../shared/picfile.h"
#include "paintdoc.h"

enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_EXIT, C_UNDO, C_REDO, C_IN, C_OUT, C_ACTUAL, C_FIT, C_ABOUT, C_FILLED,
       C_RESIZE, C_CANVAS, C_CUT, C_COPY, C_PASTE, C_ALL, C_CROP, C_LADD, C_LCOPY, C_LDELETE, C_LUP, C_LDOWN, C_LMERGE, C_LSHOWN, C_LOPACITY,
       C_TURN = 50,                     /* + PD_RIGHT, PD_LEFT, PD_ACROSS, PD_OVER */
       C_ADJUST = 60,                   /* + PD_INVERT, PD_GREY, PD_SEPIA, PD_BRIGHT */
       C_TOOL = 100 };
enum { T_PENCIL, T_BRUSH, T_ERASER, T_LINE, T_RECT, T_ELLIPSE, T_FILL, T_PICK, T_SELECT, T_TEXT, T_COUNT };

static const struct {
    const char *name, *text, *glyph, *tip;
    int key;
} tools[T_COUNT] = {
    { "pencil", GUI_N("&Pencil"), "pencil", GUI_N("Pencil (P)"), 'p' },
    { "brush", GUI_N("&Brush"), "brush", GUI_N("Brush (B)"), 'b' },
    { "eraser", GUI_N("&Eraser"), "eraser", GUI_N("Eraser (E)"), 'e' },
    { "line", GUI_N("&Line"), "line", GUI_N("Line (L)"), 'l' },
    { "rectangle", GUI_N("&Rectangle"), "rectangle", GUI_N("Rectangle (R)"), 'r' },
    { "ellipse", GUI_N("Ellip&se"), "ellipse", GUI_N("Ellipse (O)"), 'o' },
    { "fill", GUI_N("&Fill"), "fill", GUI_N("Fill (F)"), 'f' },
    { "picker", GUI_N("Colour P&icker"), "picker", GUI_N("Colour picker (I)"), 'i' },
    { "select", GUI_N("Selec&t"), "select", GUI_N("Select (S)"), 's' },
    { "text", GUI_N("Te&xt"), "underline", GUI_N("Text (T)"), 't' },
};

/* The palette: strong colours along the top, lighter ones under them; black first and white last, which are the
   two the palette itself starts with (its first and its last swatch). */
static const unsigned swatches[28] = {
    0x000000, 0x404040, 0x808080, 0x800000, 0xFF0000, 0x808000, 0xFFFF00, 0x008000, 0x00FF00, 0x008080,
    0x00FFFF, 0x000080, 0x0000FF, 0x800080, 0xFF00FF, 0x804000, 0xFF8000, 0x804040, 0xFF8080, 0x408040,
    0x80FF80, 0x404080, 0x8080FF, 0x008040, 0xFFC080, 0xC0C0C0, 0xE0E0E0, 0xFFFFFF,
};

static struct gui_window *win;
static struct gui_widget *view, *status, *pal, *first, *second, *tip_spin, *layers;
static struct gui_menu *layers_menu;
static int filling;                     /* the layers' list is being filled: its tellings are not the user's */
static struct {                         /* the part of the picture selected (the Select tool, Select All): none, w 0 */
    int x, y, w, h;
} sel;
static struct {                         /* what was copied or cut (a layer's pixels there) */
    unsigned *px;
    int w, h;
} clip;

static void deselect(void);
static struct pd_doc *doc;
static struct gui_undo *undo;
static char path[260];                  /* the file it is ("": never saved) */
static int tool = T_PENCIL, filled, dirty;      /* dirty: changed since it was saved or opened */
static int sec_size, sec_zoom;
static unsigned prev0, prev1;           /* the palette's two colours when last looked at */
static struct {
    int on;
    unsigned button, colour, other;
    int x0, y0, lx, ly;
    unsigned *before;
} drag;

/* ---- what the window says */

static void retitle(void)
{
    char t[300], name[128];
    snprintf(t, sizeof t, GUI_T("%s - Paint"), path[0] ? gui_dos_text(gui_path_name(path), name, sizeof name)
                                                       : GUI_T("Untitled"));
    gui_title(win, t);
}

static void status_parts(void)
{
    char t[40];
    snprintf(t, sizeof t, "%d x %d", doc->w, doc->h);
    gui_status_set(status, sec_size, t, NULL);
    snprintf(t, sizeof t, "%d%%", gui_cview_get_zoom(view));
    gui_status_set(status, sec_zoom, t, NULL);
}

static void show(int x0, int y0, int x1, int y1)        /* that part of the picture made again, and drawn */
{
    pd_flatten(doc, x0, y0, x1, y1);
    if (gui_cview_get_zoom(view) < 100)         /* (shown smaller, the view keeps a small copy: made anew) */
        gui_image_view_set(view, doc->flat, doc->w, doc->h, doc->w);
    gui_damage(view);
}

/* ---- undo: a step is the pixels a tool changed, before and after (paintdoc.c) */

static void step_undo(void *p)
{
    pd_apply(doc, p, 0);
    gui_damage(view);
    dirty = 1;
    printf("PAINT undone\n");
    fflush(stdout);
}

static void step_redo(void *p)
{
    pd_apply(doc, p, 1);
    gui_damage(view);
    dirty = 1;
    printf("PAINT redone\n");
    fflush(stdout);
}

static void step_drop(void *p) { pd_step_free(p); }

/* What the tools changed since `before` was taken becomes a step (none if nothing did). */
static void commit(const char *name, unsigned *before)
{
    struct pd_step *s = pd_step(doc, before);
    pd_clean(doc);
    if (!s)
        return;
    dirty = 1;
    gui_undo_push(undo, name, step_undo, step_redo, s, pd_step_bytes(s), step_drop, NULL, 0);
}

/* ---- the whole picture turned or flipped (Image's menu): undone by the turn the other way, with no pixels kept */

static void turned(int how)                     /* (the picture is another size: the view's, and what the bar says) */
{
    static const char *const names[] = { "", "right", "left", "across", "over" };
    deselect();
    gui_image_view_set(view, doc->flat, doc->w, doc->h, doc->w);
    status_parts();
    gui_damage(view);
    dirty = 1;
    printf("PAINT turned %s %dx%d\n", names[how], doc->w, doc->h);
    fflush(stdout);
}

static void turn_undo(void *p)
{
    int how = pd_turn_back(*(int *)p);
    if (pd_turn(doc, how) == 0)
        turned(how);
}

static void turn_redo(void *p)
{
    if (pd_turn(doc, *(int *)p) == 0)
        turned(*(int *)p);
}

static void turn(int how)
{
    static const char *const names[] = { "", GUI_N("Turn Right"), GUI_N("Turn Left"), GUI_N("Flip Across"),
                                         GUI_N("Flip Over") };
    int *h = malloc(sizeof *h);
    if (!h || pd_turn(doc, how) != 0) {
        free(h);
        gui_message(win, GUI_T("Paint"), GUI_T("There isn't the memory to turn this picture."), GUI_MB_OK);
        return;
    }
    *h = how;
    turned(how);
    gui_undo_push(undo, GUI_T(names[how]), turn_undo, turn_redo, h, sizeof *h, free, NULL, 0);
}

/* ---- the whole picture another size (Image's Resize and Canvas Size): the picture before is kept whole, and
   undo and redo change the kept one and the shown one over */

/* ---- the selection: a rectangle of the picture, marked by the view (dashes round it) */

static void deselect(void)
{
    sel.w = sel.h = 0;
    gui_image_view_mark(view, 0, 0, 0, 0);
}

static void select_rect(int ax, int ay, int bx, int by)         /* two corners, either way round; clipped */
{
    int t;
    if (ax > bx)
        t = ax, ax = bx, bx = t;
    if (ay > by)
        t = ay, ay = by, by = t;
    ax = ax < 0 ? 0 : ax, ay = ay < 0 ? 0 : ay;
    bx = bx >= doc->w ? doc->w - 1 : bx, by = by >= doc->h ? doc->h - 1 : by;
    if (bx <= ax || by <= ay) {                 /* (a click, or all of it outside: nothing selected) */
        deselect();
        return;
    }
    sel.x = ax, sel.y = ay, sel.w = bx - ax + 1, sel.h = by - ay + 1;
    gui_image_view_mark(view, sel.x, sel.y, sel.w, sel.h);
}

/* The layers' list beside the picture: the top one first, the current one chosen; one not shown says so. */
static void layers_fill(void)
{
    int i;
    filling = 1;
    gui_list_clear(layers);
    for (i = doc->n - 1; i >= 0; i--) {
        char t[48];
        const char *cells[1];
        snprintf(t, sizeof t, doc->layer[i].visible ? "%s" : GUI_T("%s (hidden)"), doc->layer[i].name);
        cells[0] = t;
        gui_list_add(layers, cells, NULL, NULL);
    }
    gui_list_set(layers, doc->n - 1 - doc->cur);
    filling = 0;
    gui_menu_check(layers_menu, C_LSHOWN, doc->layer[doc->cur].visible);
}

static void layers_on(struct gui_widget *w, int what)
{
    int r = gui_list_get(w);
    if (what != GUI_ON_SELECT || filling || drag.on || r < 0 || r >= doc->n)
        return;
    doc->cur = doc->n - 1 - r;
    gui_menu_check(layers_menu, C_LSHOWN, doc->layer[doc->cur].visible);
    printf("PAINT layer %d\n", doc->cur + 1);
    fflush(stdout);
}

static void kept_swap(void *p)
{
    if (pd_swap(doc, p) != 0)
        return;
    deselect();
    gui_image_view_set(view, doc->flat, doc->w, doc->h, doc->w);
    layers_fill();
    status_parts();
    gui_damage(view);
    dirty = 1;
    printf("PAINT size %dx%d layers %d\n", doc->w, doc->h, doc->n);
    fflush(stdout);
}

static void kept_drop(void *p) { pd_kept_free(p); }

/* Edit's Cut, Copy and Paste, and Image's Crop: of the selection, or of the whole picture with none. What is
   copied is the current layer's pixels there, put on the desktop's clipboard as a picture (GLOS_CLIP_IMAGE: any
   program that takes pictures can paste it, and Paint pastes what any program put there). A picture the
   clipboard would not take (no memory for it) stays Paint's own, and is what Paint pastes until the next copy.
   A paste is a new layer, its corner at the selection's (the picture's, with none). */
static void edit_command(int id)
{
    int x = sel.w ? sel.x : 0, y = sel.w ? sel.y : 0, w = sel.w ? sel.w : doc->w, h = sel.w ? sel.h : doc->h;
    struct pd_kept *k;
    unsigned *before;
    switch (id) {
    case C_ALL:
        select_rect(0, 0, doc->w - 1, doc->h - 1);
        printf("PAINT select %d,%d %dx%d\n", sel.x, sel.y, sel.w, sel.h);
        break;
    case C_COPY:
    case C_CUT: {
        unsigned *px = pd_copy(doc, x, y, &w, &h);
        if (!px)
            return;
        free(clip.px);
        clip.px = NULL;
        if (gui_clip_set_image(px, w, h) == 0)
            free(px);                           /* (the desktop's clipboard has it) */
        else
            clip.px = px, clip.w = w, clip.h = h;       /* (Paint's own) */
        printf("PAINT %s %dx%d%s\n", id == C_CUT ? "cut" : "copy", w, h, clip.px ? " kept here" : "");
        if (id == C_COPY || !(before = pd_before(doc)))
            break;
        pd_clean(doc);                          /* cut: clear there (the paper's colour on the bottom layer) */
        pd_rect(doc, x, y, x + w - 1, y + h - 1, 0, 0, doc->cur ? PD_CLEAR : gui_colour_get(second));
        show(doc->x0, doc->y0, doc->x1, doc->y1);
        commit(GUI_T("Cut"), before);
        break;
    }
    case C_PASTE: {
        unsigned *got = clip.px ? NULL : gui_clip_get_image(&w, &h);   /* the desktop's picture (or Paint's own) */
        const unsigned *px = clip.px ? clip.px : got;
        if (clip.px)
            w = clip.w, h = clip.h;
        if (!px) {
            printf("PAINT paste none\n");
            break;
        }
        if (!(k = pd_keep(doc))) {
            free(got);
            return;
        }
        if (pd_paste(doc, px, w, h, x, y) != 0) {
            pd_kept_free(k);
            free(got);
            gui_message(win, GUI_T("Paint"), GUI_T("There is no room for another layer."), GUI_MB_OK);
            return;
        }
        free(got);
        layers_fill();
        gui_damage(view);
        dirty = 1;
        printf("PAINT paste %dx%d at %d,%d\n", w, h, x, y);
        gui_undo_push(undo, GUI_T("Paste"), kept_swap, kept_swap, k, pd_kept_bytes(k), kept_drop, NULL, 0);
        break;
    }
    case C_CROP:
        if (!sel.w || !(k = pd_keep(doc)))
            return;
        if (pd_crop(doc, x, y, w, h) != 0) {
            pd_kept_free(k);
            return;
        }
        deselect();
        gui_image_view_set(view, doc->flat, doc->w, doc->h, doc->w);
        status_parts();
        gui_damage(view);
        dirty = 1;
        printf("PAINT crop %dx%d\n", doc->w, doc->h);
        gui_undo_push(undo, GUI_T("Crop"), kept_swap, kept_swap, k, pd_kept_bytes(k), kept_drop, NULL, 0);
        break;
    }
    fflush(stdout);
}

/* An adjustment (Adjustments' menu): the current layer's colours changed, in the selection or all of it; a step
   as a tool's stroke is. */
static struct {
    struct gui_widget *a, *b;
    int va, vb;
} bc;

static void bright_button(struct gui_widget *w, int what)
{
    (void)what;
    bc.va = gui_value(bc.a), bc.vb = gui_value(bc.b);   /* (read while the dialog is there) */
    gui_end_modal(w->win, w->id);
}

static int bright_ask(void)
{
    struct gui_window *d = gui_window(GUI_T("Brightness and Contrast"), 240, 0, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(d->root, 6, 8), *form = gui_form(box), *row;
    d->owner = win;
    bc.a = gui_spin(gui_row(form, GUI_T("&Brightness:"), NULL), -100, 100, 0);
    bc.b = gui_spin(gui_row(form, GUI_T("&Contrast:"), NULL), -100, 100, 0);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), bright_button, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), bright_button, 0)->flags |= GUI_CANCEL;
    return gui_modal(d) == 1;                   /* (which closes it) */
}

static void adjust(int kind)
{
    static const char *const names[] = { "", GUI_N("Invert"), GUI_N("Black and White"), GUI_N("Sepia"),
                                         GUI_N("Brightness and Contrast") };
    static const char *const words[] = { "", "invert", "grey", "sepia", "bright" };
    int x = sel.w ? sel.x : 0, y = sel.w ? sel.y : 0, w = sel.w ? sel.w : doc->w, h = sel.w ? sel.h : doc->h;
    unsigned *before;
    bc.va = bc.vb = 0;
    if (kind == PD_BRIGHT && !bright_ask())
        return;
    if (!(before = pd_before(doc))) {
        gui_message(win, GUI_T("Paint"), GUI_T("There isn't the memory for that."), GUI_MB_OK);
        return;
    }
    pd_clean(doc);
    pd_adjust(doc, kind, bc.va, bc.vb, x, y, w, h);
    show(doc->x0, doc->y0, doc->x1, doc->y1);
    printf("PAINT adjust %s %d %d\n", words[kind], bc.va, bc.vb);
    fflush(stdout);
    commit(GUI_T(names[kind]), before);
}

/* A layer's command (Layers' menu): the picture before is kept whole for undo, as for a resize. */
static struct {
    struct gui_widget *spin;
    int v;
} op;

static void opacity_button(struct gui_widget *b, int what)
{
    (void)what;
    op.v = gui_value(op.spin);                  /* (read while the dialog is there) */
    gui_end_modal(b->win, b->id);
}

static int opacity_ask(void)                    /* per cent, or -1: cancelled */
{
    struct gui_window *d = gui_window(GUI_T("Layer Opacity"), 220, 0, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(d->root, 6, 8), *form = gui_form(box), *row;
    d->owner = win;
    op.spin = gui_spin(gui_row(form, GUI_T("&Opacity (per cent):"), NULL), 0, 100,
                       (doc->layer[doc->cur].opacity * 100 + 127) / 255);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), opacity_button, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), opacity_button, 0)->flags |= GUI_CANCEL;
    return gui_modal(d) == 1 ? op.v : -1;       /* (which closes it) */
}

static void layer_command(int id)
{
    static const char *const names[] = { GUI_N("New Layer"), GUI_N("Duplicate Layer"), GUI_N("Delete Layer"),
                                         GUI_N("Move Layer Up"), GUI_N("Move Layer Down"), GUI_N("Merge Down"),
                                         GUI_N("Show Layer"), GUI_N("Layer Opacity") };
    static const char *const words[] = { "add", "copy", "delete", "up", "down", "merge", "shown", "opacity" };
    struct pd_kept *k;
    int r = 0, pct = 0;
    if (id == C_LOPACITY && (pct = opacity_ask()) < 0)
        return;
    if (!(k = pd_keep(doc))) {
        gui_message(win, GUI_T("Paint"), GUI_T("There isn't the memory for that."), GUI_MB_OK);
        return;
    }
    switch (id) {
    case C_LADD: r = pd_layer_add(doc); break;
    case C_LCOPY: r = pd_layer_copy(doc); break;
    case C_LDELETE: r = pd_layer_delete(doc); break;
    case C_LUP: r = pd_layer_move(doc, 1); break;
    case C_LDOWN: r = pd_layer_move(doc, 0); break;
    case C_LMERGE: r = pd_layer_merge(doc); break;
    case C_LSHOWN: pd_layer_set(doc, doc->cur, !doc->layer[doc->cur].visible, -1); break;
    case C_LOPACITY: pd_layer_set(doc, doc->cur, -1, (pct * 255 + 50) / 100); break;
    }
    if (r != 0) {                               /* (the only layer deleted, the top one moved up: nothing done) */
        pd_kept_free(k);
        return;
    }
    layers_fill();
    gui_damage(view);
    dirty = 1;
    printf("PAINT layer %s %d of %d\n", words[id - C_LADD], doc->cur + 1, doc->n);
    fflush(stdout);
    gui_undo_push(undo, GUI_T(names[id - C_LADD]), kept_swap, kept_swap, k, pd_kept_bytes(k), kept_drop, NULL, 0);
}

static struct {
    struct gui_widget *w, *h, *smooth, *where;
    int vw, vh, vsmooth, vwhere;
} sz;

static void size_button(struct gui_widget *b, int what)
{
    (void)what;
    sz.vw = gui_value(sz.w), sz.vh = gui_value(sz.h);   /* (read while the dialog is there) */
    sz.vsmooth = sz.smooth ? gui_checked(sz.smooth) : 0;
    sz.vwhere = sz.where ? gui_value(sz.where) : 0;
    gui_end_modal(b->win, b->id);
}

/* Resize (canvas 0): every layer scaled to the size asked. Canvas Size (1): the picture kept as it is on a
   canvas that size, at one of nine places; the new part is the second colour's. */
static void resize(int canvas)
{
    static const char *const places[9] = { GUI_N("Top left"), GUI_N("Top"), GUI_N("Top right"), GUI_N("Left"),
                                           GUI_N("Middle"), GUI_N("Right"), GUI_N("Bottom left"), GUI_N("Bottom"),
                                           GUI_N("Bottom right") };
    struct gui_window *d = gui_window(canvas ? GUI_T("Canvas Size") : GUI_T("Resize"), 240, 0, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(d->root, 6, 8), *form = gui_form(box), *row;
    struct pd_kept *k;
    int r, i;
    d->owner = win;
    sz.w = gui_spin(gui_row(form, GUI_T("&Width:"), NULL), 1, PD_MAX, doc->w);
    sz.h = gui_spin(gui_row(form, GUI_T("&Height:"), NULL), 1, PD_MAX, doc->h);
    sz.smooth = sz.where = NULL;
    if (canvas) {
        sz.where = gui_choice(gui_row(form, GUI_T("&Picture at:"), NULL), 12);
        for (i = 0; i < 9; i++)
            gui_choice_add(sz.where, GUI_T(places[i]));
        gui_set_value(sz.where, 4);
    } else {
        sz.smooth = gui_check(box, GUI_T("&Smooth"), 1);
    }
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), size_button, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), size_button, 0)->flags |= GUI_CANCEL;
    r = gui_modal(d);                           /* (which closes it) */
    if (r != 1 || (sz.vw == doc->w && sz.vh == doc->h))
        return;
    if (!(k = pd_keep(doc))
        || (canvas ? pd_canvas(doc, sz.vw, sz.vh, sz.vwhere % 3, sz.vwhere / 3, gui_colour_get(second))
                   : pd_resize(doc, sz.vw, sz.vh, sz.vsmooth)) != 0) {
        pd_kept_free(k);
        gui_message(win, GUI_T("Paint"), GUI_T("There isn't the memory for a picture that large."), GUI_MB_OK);
        return;
    }
    deselect();
    gui_image_view_set(view, doc->flat, doc->w, doc->h, doc->w);
    status_parts();
    gui_damage(view);
    dirty = 1;
    printf("PAINT %s %dx%d\n", canvas ? "canvas" : "resized", doc->w, doc->h);
    fflush(stdout);
    gui_undo_push(undo, canvas ? GUI_T("Canvas Size") : GUI_T("Resize"), kept_swap, kept_swap, k, pd_kept_bytes(k), kept_drop,
                  NULL, 0);
}

/* ---- the tools */

static void choose(int t)
{
    int i;
    tool = t;
    for (i = 0; i < T_COUNT; i++)
        gui_command_check(win, C_TOOL + i, i == t);
    gui_set_text(status, GUI_T(tools[t].tip));
    printf("PAINT tool %s\n", tools[t].name);
    fflush(stdout);
}

static int tip(void)                    /* the tip's width: the pencil's is a pixel */
{
    return tool == T_PENCIL ? 1 : gui_value(tip_spin);
}

/* The shape being dragged out, from where the button went down to (x, y). */
static void shape(int x, int y)
{
    unsigned fill = filled ? drag.other : PD_NONE;
    if (tool == T_LINE)
        pd_line(doc, drag.x0, drag.y0, x, y, drag.colour, tip());
    else if (tool == T_RECT)
        pd_rect(doc, drag.x0, drag.y0, x, y, drag.colour, tip(), fill);
    else
        pd_ellipse(doc, drag.x0, drag.y0, x, y, drag.colour, tip(), fill);
    show(doc->x0, doc->y0, doc->x1, doc->y1);
}

/* ---- the Text tool (phase E): a press on the picture asks for a line of text and its font (GLOS's own faces and
   sizes: the toolkit's font dialog), and the letters are put on the current layer in the button's colour, their
   top left corner where the press was: one step to undo. The letters are drawn by the 2D library into a picture
   of their own, and the pixels they took are stamped on the layer (pd_stamp). */
static struct gui_fontspec text_font = { GUI_FONT_SANS, 1 };
static struct {
    struct gui_window *win;
    struct gui_widget *field;
    char text[160];
} tx;

static void text_button(struct gui_widget *b, int what)
{
    (void)what;
    if (b->id == 1) {                           /* (read while the dialog is there: gui_modal closes it) */
        snprintf(tx.text, sizeof tx.text, "%s", gui_get_text(tx.field));
        gui_end_modal(tx.win, 1);
    } else if (b->id == 2) {
        gui_end_modal(tx.win, 0);
    } else {
        gui_font_dialog(tx.win, GUI_T("Font"), &text_font);
    }
}

static void text_at(int x, int y, unsigned colour)
{
    struct gui_widget *box, *row;
    const struct gfx_font *f;
    struct gfx_surface s;
    unsigned *px, *before;
    unsigned char *mask;
    int w, h, i;
    tx.win = gui_window(GUI_T("Text"), 300, 0, GUI_W_DIALOG);
    tx.win->owner = win;
    box = gui_vbox(tx.win->root, 6, 8);
    gui_label(box, GUI_T("The text to put on the picture:"));
    tx.field = gui_field(box, 36);
    gui_set_text(tx.field, tx.text);
    row = gui_hbox(box, 6, 0);
    gui_button(row, GUI_T("&Font..."), text_button, 3);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), text_button, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), text_button, 2)->flags |= GUI_CANCEL;
    gui_focus(tx.field);
    if (gui_modal(tx.win) != 1 || !tx.text[0])  /* (which closes it) */
        return;
    f = gui_font_get(text_font.face, text_font.size);
    if (!f)
        f = gui_font;
    w = gfx_text_width(f, tx.text, -1) + 2, h = f->height;
    if (w < 1 || h < 1 || !(px = calloc((size_t)w * (size_t)h, sizeof *px)))
        return;
    if (!(mask = malloc((size_t)w * (size_t)h)) || !(before = pd_before(doc))) {
        free(px);
        free(mask);
        gui_message(win, GUI_T("Paint"), GUI_T("There isn't the memory to draw on this picture."), GUI_MB_OK);
        return;
    }
    gfx_init(&s, px, w, h, (unsigned)w * sizeof *px, &gfx_fmt888);
    gfx_text(&s, 1, 0, f, tx.text, -1, 0xFFFFFF);
    for (i = 0; i < w * h; i++)
        mask[i] = (px[i] & 0xFFFFFF) != 0;
    pd_clean(doc);
    pd_stamp(doc, mask, w, h, x, y, colour);
    show(doc->x0, doc->y0, doc->x1, doc->y1);
    printf("PAINT text %d,%d %dx%d %06X %s\n", x, y, w, h, colour, tx.text);
    fflush(stdout);
    commit(GUI_T("Text"), before);
    free(px);
    free(mask);
}

static void press(struct gui_ev *e, int x, int y)
{
    unsigned a = gui_colour_get(first), b = gui_colour_get(second);
    unsigned colour = e->button == 1 ? a : b, other = e->button == 1 ? b : a;
    if (tool == T_TEXT) {
        text_at(x, y, colour);
        return;
    }
    if (tool == T_SELECT) {                     /* a rectangle dragged out; a click takes the selection away */
        deselect();
        drag.on = 1;
        drag.button = e->button;
        drag.x0 = drag.lx = x, drag.y0 = drag.ly = y;
        return;
    }
    if (tool == T_PICK) {
        if (x >= 0 && y >= 0 && x < doc->w && y < doc->h) {
            unsigned c = pd_get(doc, x, y);
            gui_colour_set(e->button == 1 ? first : second, c);
            printf("PAINT pick %06X\n", c);
            fflush(stdout);
        }
        return;
    }
    if (tool == T_ERASER)                       /* (the paper's colour on the bottom layer; over another, clear) */
        colour = doc->cur ? PD_CLEAR : b;
    if (!(drag.before = pd_before(doc))) {
        gui_message(win, GUI_T("Paint"), GUI_T("There isn't the memory to draw on this picture."), GUI_MB_OK);
        return;
    }
    pd_clean(doc);
    if (tool == T_FILL) {
        long n = pd_fill(doc, x, y, colour, 0);
        show(doc->x0, doc->y0, doc->x1, doc->y1);
        printf("PAINT fill %ld %06X\n", n, colour);
        fflush(stdout);
        commit(GUI_T("Fill"), drag.before);
        drag.before = NULL;
        return;
    }
    drag.on = 1;
    drag.button = e->button, drag.colour = colour, drag.other = other;
    drag.x0 = drag.lx = x, drag.y0 = drag.ly = y;
    if (tool <= T_ERASER) {
        pd_dot(doc, x, y, colour, tip());
        show(doc->x0, doc->y0, doc->x1, doc->y1);
    } else {
        shape(x, y);
    }
}

static void move(int x, int y)
{
    if (tool == T_SELECT) {
        select_rect(drag.x0, drag.y0, x, y);
        drag.lx = x, drag.ly = y;
        return;
    }
    if (tool <= T_ERASER) {                     /* a stroke: one more piece of it */
        int s = tip(), ax = x < drag.lx ? x : drag.lx, ay = y < drag.ly ? y : drag.ly;
        int bx = x < drag.lx ? drag.lx : x, by = y < drag.ly ? drag.ly : y;
        pd_line(doc, drag.lx, drag.ly, x, y, drag.colour, s);
        show(ax - s, ay - s, bx + s + 1, by + s + 1);
    } else {                                    /* a shape: the last one taken away, this one drawn */
        pd_restore(doc, drag.before);
        shape(x, y);
    }
    drag.lx = x, drag.ly = y;
}

static void release(int x, int y)
{
    static const char *const names[] = { GUI_N("Pencil"), GUI_N("Brush"), GUI_N("Eraser"), GUI_N("Line"),
                                         GUI_N("Rectangle"), GUI_N("Ellipse") };
    if (tool == T_SELECT) {
        select_rect(drag.x0, drag.y0, x, y);
        drag.on = 0;
        printf("PAINT select %d,%d %dx%d\n", sel.x, sel.y, sel.w, sel.h);
        fflush(stdout);
        return;
    }
    if (x != drag.lx || y != drag.ly)
        move(x, y);
    drag.on = 0;
    printf("PAINT drew %s %d,%d %d,%d %06X\n", tools[tool].name, drag.x0, drag.y0, x, y, drag.colour);
    fflush(stdout);
    commit(GUI_T(names[tool]), drag.before);
    drag.before = NULL;
}

static int key(struct gui_window *w, struct gui_ev *e);

static int tool_event(struct gui_widget *w, struct gui_ev *e, int x, int y)
{
    (void)w;
    switch (e->type) {
    case GUI_EV_KEY:
        return key(win, e);
    case GUI_EV_PRESS:
        if (drag.on || (e->button != 1 && e->button != 2))
            return drag.on;                     /* (the middle button moves the picture: the view's own) */
        press(e, x, y);
        return 1;
    case GUI_EV_MOVE: {
        char t[40];
        if (x >= 0 && y >= 0 && x < doc->w && y < doc->h) {
            snprintf(t, sizeof t, "%d, %d", x, y);
            gui_set_text(status, t);
        }
        if (!drag.on)
            return 0;
        move(x, y);
        return 1;
    }
    case GUI_EV_RELEASE:
        if (!drag.on || e->button != drag.button)
            return drag.on;
        release(x, y);
        return 1;
    }
    return 0;
}

/* ---- the picture as a whole: a new one, one opened, saved */

static void adopt(struct pd_doc *d, const char *p)
{
    pd_free(doc);
    doc = d;
    snprintf(path, sizeof path, "%s", p ? p : "");
    dirty = 0;
    gui_undo_clear(undo);
    deselect();
    gui_image_view_set(view, doc->flat, doc->w, doc->h, doc->w);
    layers_fill();
    gui_cview_zoom(view, 100);
    gui_cview_scroll(view, 0, 0);
    retitle();
    status_parts();
    gui_damage(view);
}

static int save_to(const char *p)
{
    unsigned *out = malloc((size_t)doc->w * (size_t)doc->h * sizeof *out);
    int r = -3;
    if (out) {                                  /* as it shows, what is clear white: not the chequered ground */
        pd_render(doc, out, 0xFFFFFF);
        r = pic_write(p, out, doc->w, doc->h, 0);
        free(out);
    }
    if (r != 0) {
        gui_message(win, GUI_T("Paint"), r == -2 ? GUI_T("Paint saves PNG, BMP, PCX and JPG pictures: give the name one of those ends.")
                                         : r == -3 ? GUI_T("There isn't the memory to save this picture.")
                                                   : GUI_T("The picture couldn't be written there."), GUI_MB_OK);
        return 0;
    }
    snprintf(path, sizeof path, "%s", p);
    dirty = 0;
    retitle();
    printf("PAINT saved %s %dx%d\n", p, doc->w, doc->h);
    fflush(stdout);
    return 1;
}

static const char *const filters[] = { GUI_N("Pictures|*.PNG;*.JPG;*.BMP;*.GIF;*.TGA;*.PNM;*.PPM;*.PGM;*.PCX"),
                                       GUI_N("All files|*.*"), NULL };
static const char *const save_filters[] = { GUI_N("PNG|*.PNG"), GUI_N("BMP|*.BMP"), GUI_N("JPEG|*.JPG"),
                                            GUI_N("PCX|*.PCX"), NULL };

static int save_as(void)
{
    static const char *const ends[] = { ".PNG", ".BMP", ".JPG", ".PCX" };
    struct gui_file_opts o;
    char p[260];
    memset(&o, 0, sizeof o);
    o.filters = save_filters;
    snprintf(p, sizeof p, "%s", path[0] ? path : glos_boot_root());
    if (!gui_file_dialog_ex(win, GUI_T("Save As"), 1, p, sizeof p, &o))
        return 0;
    if (!pic_ext(gui_path_name(p))[0] && strlen(p) + 5 < sizeof p)     /* (no end: the kind chosen's) */
        strcat(p, ends[o.filter >= 0 && o.filter < 4 ? o.filter : 0]);
    return save_to(p);
}

static int save(void)
{
    return path[0] && pic_can_write(path) ? save_to(path) : save_as();
}

/* Changes not saved, before the picture goes: saved, let go (1), or the going called off (0). */
static int let_go(void)
{
    char t[300], name[128];
    int r;
    if (!dirty)
        return 1;
    snprintf(t, sizeof t, GUI_T("Save the changes to %s?"), path[0] ? gui_dos_text(gui_path_name(path), name, sizeof name)
                                                                    : GUI_T("Untitled"));
    r = gui_message(win, GUI_T("Paint"), t, GUI_MB_YESNOCANCEL);
    return r == GUI_YES ? save() : r == GUI_NO;
}

static int open_file(const char *p)
{
    unsigned char *d;
    unsigned *px;
    struct pd_doc *n;
    size_t len = 0;
    int w = 0, h = 0;
    char kind[8], t[300], name[128];
    if (!(d = pic_read_all(p, &len))) {
        gui_message(win, GUI_T("Paint"), GUI_T("The file isn't there."), GUI_MB_OK);
        return 0;
    }
    px = pic_decode(d, len, p, &w, &h, kind, sizeof kind, 0xFFFFFF);
    free(d);
    if (!px || w > PD_MAX || h > PD_MAX || !(n = pd_from(px, w, h))) {
        free(px);
        snprintf(t, sizeof t, GUI_T("%s isn't a picture Paint can open."), gui_dos_text(gui_path_name(p), name, sizeof name));
        gui_message(win, GUI_T("Paint"), t, GUI_MB_OK);
        return 0;
    }
    adopt(n, p);
    printf("PAINT open %s %dx%d %s\n", p, w, h, kind);
    fflush(stdout);
    return 1;
}

/* New's question: how large. */
static struct {
    struct gui_widget *w, *h;
    int vw, vh;
} nd;

static void new_button(struct gui_widget *b, int what)
{
    (void)what;
    nd.vw = gui_value(nd.w), nd.vh = gui_value(nd.h);   /* (read while the dialog is there) */
    gui_end_modal(b->win, b->id);
}

static void new_picture(void)
{
    struct gui_window *d = gui_window(GUI_T("New Picture"), 220, 0, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(d->root, 6, 8), *form = gui_form(box), *row;
    struct pd_doc *n;
    int r;
    d->owner = win;
    nd.w = gui_spin(gui_row(form, GUI_T("&Width:"), NULL), 1, PD_MAX, doc ? doc->w : 320);
    nd.h = gui_spin(gui_row(form, GUI_T("&Height:"), NULL), 1, PD_MAX, doc ? doc->h : 240);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), new_button, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), new_button, 0)->flags |= GUI_CANCEL;
    r = gui_modal(d);                           /* (which closes it) */
    if (r != 1)
        return;
    if (!(n = pd_new(nd.vw, nd.vh, 0xFFFFFF))) {
        gui_message(win, GUI_T("Paint"), GUI_T("There isn't the memory for a picture that large."), GUI_MB_OK);
        return;
    }
    adopt(n, NULL);
    printf("PAINT new %dx%d\n", nd.vw, nd.vh);
    fflush(stdout);
}

/* ---- the commands */

static void closing(struct gui_window *w)
{
    (void)w;
    if (let_go())
        gui_quit(0);
}

static void command(struct gui_window *w, int id)
{
    char p[260];
    struct gui_file_opts o;
    (void)w;
    if (drag.on)
        return;                                 /* (a stroke is under way: nothing else until the button is up) */
    if (id >= C_TOOL && id < C_TOOL + T_COUNT) {
        choose(id - C_TOOL);
        return;
    }
    if (id > C_TURN && id <= C_TURN + PD_OVER) {
        turn(id - C_TURN);
        return;
    }
    if (id >= C_LADD && id <= C_LOPACITY) {
        layer_command(id);
        return;
    }
    if (id >= C_CUT && id <= C_CROP) {
        edit_command(id);
        return;
    }
    if (id > C_ADJUST && id <= C_ADJUST + PD_BRIGHT) {
        adjust(id - C_ADJUST);
        return;
    }
    switch (id) {
    case C_NEW:
        if (let_go())
            new_picture();
        break;
    case C_OPEN:
        if (!let_go())
            break;
        memset(&o, 0, sizeof o);
        o.filters = filters;
        snprintf(p, sizeof p, "%s", path[0] ? path : glos_boot_root());
        if (gui_file_dialog_ex(win, GUI_T("Open"), 0, p, sizeof p, &o))
            open_file(p);
        break;
    case C_SAVE: save(); break;
    case C_SAVEAS: save_as(); break;
    case C_EXIT: closing(win); break;
    case C_UNDO: gui_undo(undo); break;
    case C_REDO: gui_redo(undo); break;
    case C_IN: gui_cview_zoom(view, gui_cview_get_zoom(view) < 800 ? gui_cview_get_zoom(view) * 2 : 800); break;
    case C_OUT: gui_cview_zoom(view, gui_cview_get_zoom(view) > 25 ? gui_cview_get_zoom(view) / 2 : 25); break;
    case C_ACTUAL: gui_cview_zoom(view, 100); break;
    case C_FIT: gui_cview_zoom(view, 0); break;
    case C_RESIZE: resize(0); break;
    case C_CANVAS: resize(1); break;
    case C_FILLED:
        filled = !filled;
        gui_command_check(win, C_FILLED, filled);
        printf("PAINT filled %d\n", filled);
        fflush(stdout);
        break;
    case C_ABOUT:
        gui_message(win, GUI_T("About Paint"),
                    GUI_T("Paint: GLOS's picture editor. Pictures read by stb_image, written by stb_image_write."),
                    GUI_MB_OK);
        break;
    }
    status_parts();
    gui_damage(view);
}

static int key(struct gui_window *w, struct gui_ev *e)
{
    int i, c = (int)e->ch;
    (void)w;
    if (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
        return 0;
    if (c >= 'A' && c <= 'Z')
        c += 32;
    for (i = 0; c && i < T_COUNT; i++)
        if (c == tools[i].key) {
            command(win, C_TOOL + i);
            return 1;
        }
    if (e->key == GUI_K_ESC && sel.w) {         /* (Esc: the selection taken away) */
        deselect();
        return 1;
    }
    if (c == '[' || c == ']') {
        int v = gui_value(tip_spin) + (c == ']' ? 1 : -1);
        gui_set_value(tip_spin, v < 1 ? 1 : v > 64 ? 64 : v);
        return 1;
    }
    return 0;
}

static void pal_on(struct gui_widget *w, int what)
{
    unsigned a = gui_palette_get(w, 0), b = gui_palette_get(w, 1);
    if (what != GUI_ON_SELECT)
        return;
    if (a != prev0)                             /* (which of the two was taken: the one that is another now) */
        gui_colour_set(first, a);
    if (b != prev1)
        gui_colour_set(second, b);
    if (a == prev0 && b == prev1)               /* (the same swatch again: the first colour's, as a left click) */
        gui_colour_set(first, a);
    prev0 = a, prev1 = b;
    printf("PAINT colours %06X %06X\n", gui_colour_get(first), gui_colour_get(second));
    fflush(stdout);
}

static void resized(struct gui_window *w)
{
    (void)w;
    status_parts();
}

static void zoomed(struct gui_widget *w, int what)
{
    (void)w;
    if (what == GUI_ON_CHANGE)
        status_parts();
}

int main(int argc, char **argv)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *tb, *row;
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("PAINT no GLOS\n");
        return 1;
    }
    win = gui_window(GUI_T("Paint"), 560, 420, 0);
    gui_window_icon(win, "app-paint");
    win->on_command = command;
    win->on_key = key;
    win->on_close = closing;
    win->on_resize = resized;
    undo = gui_undo_new(8u << 20);
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_item(m, GUI_T("&New..."), C_NEW, GUI_T("Ctrl+N"));
    gui_menu_item(m, GUI_T("&Open..."), C_OPEN, GUI_T("Ctrl+O"));
    gui_menu_item(m, GUI_T("&Save"), C_SAVE, GUI_T("Ctrl+S"));
    gui_menu_item(m, GUI_T("Save &As..."), C_SAVEAS, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("E&xit"), C_EXIT, NULL);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_item(m, GUI_T("&Undo"), C_UNDO, GUI_T("Ctrl+Z"));
    gui_menu_item(m, GUI_T("&Redo"), C_REDO, GUI_T("Ctrl+Y"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Cu&t"), C_CUT, GUI_T("Ctrl+X"));
    gui_menu_item(m, GUI_T("&Copy"), C_COPY, GUI_T("Ctrl+C"));
    gui_menu_item(m, GUI_T("&Paste"), C_PASTE, GUI_T("Ctrl+V"));
    gui_menu_item(m, GUI_T("Select &All"), C_ALL, GUI_T("Ctrl+A"));
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_item(m, GUI_T("Zoom &in"), C_IN, NULL);
    gui_menu_item(m, GUI_T("Zoom &out"), C_OUT, NULL);
    gui_menu_item(m, GUI_T("&Actual size"), C_ACTUAL, NULL);
    gui_menu_item(m, GUI_T("&Fit to window"), C_FIT, NULL);
    m = gui_menu(bar, GUI_T("&Image"));
    gui_menu_item(m, GUI_T("Turn &Left"), C_TURN + PD_LEFT, NULL);
    gui_menu_item(m, GUI_T("Turn &Right"), C_TURN + PD_RIGHT, NULL);
    gui_menu_item(m, GUI_T("Flip &Across"), C_TURN + PD_ACROSS, NULL);
    gui_menu_item(m, GUI_T("Flip &Over"), C_TURN + PD_OVER, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Re&size..."), C_RESIZE, NULL);
    gui_menu_item(m, GUI_T("&Canvas Size..."), C_CANVAS, NULL);
    gui_menu_item(m, GUI_T("Cro&p to Selection"), C_CROP, NULL);
    m = gui_menu(bar, GUI_T("&Adjustments"));
    gui_menu_item(m, GUI_T("&Invert"), C_ADJUST + PD_INVERT, GUI_T("Ctrl+I"));
    gui_menu_item(m, GUI_T("Black and &White"), C_ADJUST + PD_GREY, NULL);
    gui_menu_item(m, GUI_T("&Sepia"), C_ADJUST + PD_SEPIA, NULL);
    gui_menu_item(m, GUI_T("&Brightness and Contrast..."), C_ADJUST + PD_BRIGHT, NULL);
    layers_menu = m = gui_menu(bar, GUI_T("&Layers"));
    gui_menu_item(m, GUI_T("&New Layer"), C_LADD, GUI_T("Ctrl+Shift+N"));
    gui_menu_item(m, GUI_T("&Duplicate"), C_LCOPY, NULL);
    gui_menu_item(m, GUI_T("D&elete"), C_LDELETE, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Move &Up"), C_LUP, NULL);
    gui_menu_item(m, GUI_T("Move Do&wn"), C_LDOWN, NULL);
    gui_menu_item(m, GUI_T("&Merge Down"), C_LMERGE, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("&Shown"), C_LSHOWN, NULL);
    gui_menu_item(m, GUI_T("&Opacity..."), C_LOPACITY, NULL);
    m = gui_menu(bar, GUI_T("&Tools"));
    for (i = 0; i < T_COUNT; i++)
        gui_menu_item(m, GUI_T(tools[i].text), C_TOOL + i, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Shapes Fille&d"), C_FILLED, NULL);
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_item(m, GUI_T("&About Paint"), C_ABOUT, NULL);

    tb = gui_toolbar(win);
    gui_command(win, C_NEW, GUI_T("&New..."), NULL, gui_glyph("new"), GUI_T("New (Ctrl+N)"));
    gui_command(win, C_OPEN, GUI_T("&Open..."), NULL, gui_glyph("open"), GUI_T("Open (Ctrl+O)"));
    gui_command(win, C_SAVE, GUI_T("&Save"), NULL, gui_glyph("save"), GUI_T("Save (Ctrl+S)"));
    gui_command(win, C_UNDO, GUI_T("&Undo"), NULL, gui_glyph("undo"), GUI_T("Undo (Ctrl+Z)"));
    gui_command(win, C_REDO, GUI_T("&Redo"), NULL, gui_glyph("redo"), GUI_T("Redo (Ctrl+Y)"));
    gui_toolbar_command(tb, C_NEW);
    gui_toolbar_command(tb, C_OPEN);
    gui_toolbar_command(tb, C_SAVE);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_UNDO);
    gui_toolbar_command(tb, C_REDO);
    gui_toolbar_sep(tb);
    for (i = 0; i < T_COUNT; i++) {
        gui_command(win, C_TOOL + i, GUI_T(tools[i].text), NULL, gui_glyph(tools[i].glyph), GUI_T(tools[i].tip));
        gui_toolbar_command(tb, C_TOOL + i);
    }
    gui_toolbar_sep(tb);
    gui_command(win, C_FILLED, GUI_T("Shapes Fille&d"), NULL, gui_glyph("star"), GUI_T("Shapes filled with the other colour"));
    gui_toolbar_command(tb, C_FILLED);
    gui_toolbar_sep(tb);
    gui_command(win, C_IN, GUI_T("Zoom &in"), NULL, gui_glyph("zoom-in"), GUI_T("Zoom in (+)"));
    gui_command(win, C_OUT, GUI_T("Zoom &out"), NULL, gui_glyph("zoom-out"), GUI_T("Zoom out (-)"));
    gui_toolbar_command(tb, C_IN);
    gui_toolbar_command(tb, C_OUT);
    gui_undo_commands(undo, win, C_UNDO, C_REDO);

    row = gui_hbox(win->root, 0, 0);            /* the picture, and its layers beside it */
    gui_grow(row, 1);
    view = gui_image_view(gui_vbox(row, 0, 0));
    {
        static const char *const heads[1] = { GUI_N("Layers") };
        static const int widths[1] = { 100 };
        layers = gui_list(row, 1, heads, widths);
        gui_size(layers, 120, 0);
        gui_grow(layers, 0);
        gui_on(layers, layers_on, 0);
    }
    gui_cview_on_event(view, tool_event);
    gui_cview_style(view, GUI_CV_CENTRE | GUI_CV_RING | GUI_CV_TOOLS, 0);       /* (the left button is the tools') */
    gui_cview_cursor(view, GLOS_CURSOR_CROSS);
    gui_on(view, zoomed, 0);
    gui_grow(view->parent, 1);
    gui_grow(view, 1);
    win->focus = view;

    row = gui_hbox(win->root, 6, 3);            /* the colours: the two in hand, the palette; the tip's width */
    first = gui_colour_button(row, 0x000000, 0);
    second = gui_colour_button(row, 0xFFFFFF, 0);
    pal = gui_palette(row, swatches, 28, 14);
    gui_on(pal, pal_on, 0);
    prev0 = gui_palette_get(pal, 0), prev1 = gui_palette_get(pal, 1);
    gui_label(row, GUI_T("Tip:"));
    tip_spin = gui_spin(row, 1, 64, 5);
    status = gui_status(win);
    sec_size = gui_status_section(status, 90);
    sec_zoom = gui_status_section(status, 50);

    doc = pd_new(320, 240, 0xFFFFFF);
    if (!doc) {
        printf("PAINT no memory\n");
        return 1;
    }
    gui_image_view_set(view, doc->flat, doc->w, doc->h, doc->w);
    layers_fill();
    gui_cview_zoom(view, 100);
    retitle();
    choose(T_PENCIL);
    gui_show(win, 1);
    status_parts();
    if (argc > 1)
        open_file(argv[1]);
    else {
        printf("PAINT new %dx%d\n", doc->w, doc->h);
        fflush(stdout);
    }
    gui_run();
    printf("PAINT end\n");
    return 0;
}
