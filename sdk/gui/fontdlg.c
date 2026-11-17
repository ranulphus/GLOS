/* Font (toolkit 2, step C6; docs/v1/toolkit-2.md, Dialogs and 6): gui_font_dialog. GLOS's faces (the interface's
 * sans, its bold, the fixed-width one) and the sizes there are (small and large from FONTS.DAT beside the
 * program; a size that isn't there isn't offered), and a line of text in the font chosen. The scripts that only
 * the smoothed fonts have (M10 step G) are not offered yet. */
#include <stdio.h>
#include <string.h>

#include "guiint.h"

static struct {
    struct gui_widget *faces, *sizes, *preview;
    int size_of[3];                             /* the sizes' rows: which size each is */
    int nsizes;
    int face, size;                             /* what was chosen, taken before the dialog goes */
} F;

static void take(struct gui_window *w)
{
    int face = gui_list_get(F.faces), row = gui_list_get(F.sizes);
    F.face = face >= 0 ? face : 0;
    F.size = F.nsizes && row >= 0 ? F.size_of[row] : 1;
    gui_end_modal(w, GUI_OK);
}

static void show(void)
{
    int face = gui_list_get(F.faces), row = gui_list_get(F.sizes);
    const struct gfx_font *f = gui_font_get(face < 0 ? 0 : face, row < 0 ? 1 : F.size_of[row]);
    if (f) {
        gui_label_style(F.preview, f, GFX_NONE, -1);
        gui_relayout(F.preview->win);
        gui_damage(F.preview);
    }
}

static void list_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_SELECT)
        show();
    else if (what == GUI_ON_ACTIVATE)
        take(w->win);
}

static void ok_click(struct gui_widget *w, int what)
{
    (void)what;
    take(w->win);
}

static void cancel_click(struct gui_widget *w, int what)
{
    (void)what;
    gui_end_modal(w->win, GUI_CANCEL_R);
}

int gui_font_dialog(struct gui_window *owner, const char *title, struct gui_fontspec *f)
{
    static const char *const face_names[3] = { GUI_N("Sans"), GUI_N("Sans bold"), GUI_N("Fixed width") };
    static const char *const size_names[3] = { GUI_N("Small (9 pixels)"), GUI_N("Normal (11 pixels)"),
        GUI_N("Large (14 pixels)") };
    struct gui_window *w = gui_window(title, 0, 0, GUI_W_DIALOG);
    struct gui_widget *box, *top, *col, *row;
    int i, r, face, size;
    memset(&F, 0, sizeof F);
    w->owner = owner;
    box = gui_vbox(w->root, GUI_GAP, 10);
    top = gui_hbox(box, 12, 0);
    col = gui_vbox(top, 2, 0);
    gui_label(col, GUI_T("&Font:"));
    F.faces = gui_list(col, 1, NULL, NULL);
    gui_size(F.faces, 150, 70);
    for (i = 0; i < 3; i++) {
        const char *cells[1] = { GUI_T(face_names[i]) };
        gui_list_add(F.faces, cells, NULL, NULL);
    }
    col = gui_vbox(top, 2, 0);
    gui_label(col, GUI_T("&Size:"));
    F.sizes = gui_list(col, 1, NULL, NULL);
    gui_size(F.sizes, 150, 70);
    for (i = 0; i < 3; i++)
        if (gui_font_get(GUI_FONT_SANS, i)) {   /* (the sizes there are) */
            const char *cells[1] = { GUI_T(size_names[i]) };
            F.size_of[F.nsizes++] = i;
            gui_list_add(F.sizes, cells, NULL, NULL);
        }
    gui_label(box, GUI_T("Sample:"));
    F.preview = gui_label(box, GUI_T("The quick brown fox, 0123456789"));
    gui_size(F.preview, 320, 24);
    row = gui_hbox(box, GUI_GAP, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), ok_click, 0)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), cancel_click, 0)->flags |= GUI_CANCEL;
    face = f->face >= 0 && f->face <= 2 ? f->face : 0;
    gui_list_set(F.faces, face);
    for (i = 0, size = 0; i < F.nsizes; i++)
        if (F.size_of[i] == f->size)
            size = i;
    gui_list_set(F.sizes, size);
    gui_on(F.faces, list_on, 0);
    gui_on(F.sizes, list_on, 0);
    show();
    w->focus = F.faces;
    r = gui_modal(w);
    if (r != GUI_OK)
        return 0;
    f->face = F.face, f->size = F.size;
    return 1;
}
