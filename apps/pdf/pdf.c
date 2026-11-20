/* PDF: the PDF Viewer (GLOS v1, phase F; docs/v1/apps/pdf-viewer.md): its first part. A PDF's pages drawn one
 * at a time by GLOS's own reader and renderer (apps/shared/pdf.c, pdfdraw.c, pdffont.c, ttf.c): text in the fonts
 * the file carries (TrueType) or in stand-ins (PDFFONTS.DAT, beside the program), paths, pictures.
 *
 *   the page      in a view that scrolls (the bars, the wheel, the arrows; the left button moves it)
 *   pages         Ctrl+PgDn and Ctrl+PgUp, Ctrl+Home and Ctrl+End, Go to Page... (Ctrl+G)
 *   zoom          Ctrl++ and Ctrl+-, Actual Size (Ctrl+1: a point a pixel), Fit Width (Ctrl+2: as it opens)
 *
 * The file is read a piece at a time as its objects are wanted, never whole; what was read for a page is let go
 * when the next is drawn. A page is drawn whole into a picture of its own (no larger than three million
 * pixels: the zoom stops there).
 *
 * PDF FILE opens the file (Files' "open").
 *
 * "PDF ..." lines on stdout (the suite's). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "../shared/pdfdraw.h"
#include "../shared/pdffont.h"
#include "../shared/picfile.h"

enum { C_OPEN = 1, C_EXIT, C_PREV, C_NEXT, C_FIRST, C_LAST, C_GOTO, C_ZOOMIN, C_ZOOMOUT, C_ACTUAL, C_FITW };

#define MAX_PIXELS 3000000l
#define KEEP (2ul << 20)                /* the file's objects kept between pages: bytes */

static struct gui_window *win;
static struct gui_widget *view, *status;
static FILE *file;
static unsigned long file_size;
static struct pdf *doc;
static char path[260];
static unsigned page;                   /* from 0 */
static int zoom = 100, fit = 1;         /* percent (100: a point a pixel); fit: the zoom follows the view's width */
static unsigned *bitmap;
static int bw, bh;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("PDF ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- what the reader and the renderer ask of the program */

static unsigned long file_read(void *ctx, unsigned long at, void *buf, unsigned long n)
{
    (void)ctx;
    if (!file || fseek(file, (long)at, SEEK_SET) != 0)
        return 0;
    return (unsigned long)fread(buf, 1, n, file);
}

static unsigned *a_jpeg(const unsigned char *bytes, unsigned long n, int *w, int *h)
{
    char kind[8];
    return pic_decode(bytes, n, "x.jpg", w, h, kind, sizeof kind, 0xFFFFFF);
}

/* The stand-in fonts: PDFFONTS.DAT beside the program ("GPDF", how many, then where each lies and how long it
   is: family * 4 + bold * 2 + italic), each read when it is first wanted and kept. */
static const unsigned char *a_face(int family, int bold, int italic, unsigned long *len)
{
    static unsigned char *kept[12];
    static unsigned long size[12];
    unsigned char head[8 + 12 * 8], *b;
    unsigned i = (unsigned)(family * 4 + bold * 2 + italic);
    unsigned long at, n;
    FILE *f;
    if (i >= 12)
        return NULL;
    if (!kept[i]) {
        if (!(f = gui_open_beside("PDFFONTS.DAT")))
            return NULL;
        if (fread(head, 1, sizeof head, f) == sizeof head && !memcmp(head, "GPDF", 4)) {
            const unsigned char *e = head + 8 + 8 * i;
            at = e[0] | (unsigned long)e[1] << 8 | (unsigned long)e[2] << 16 | (unsigned long)e[3] << 24;
            n = e[4] | (unsigned long)e[5] << 8 | (unsigned long)e[6] << 16 | (unsigned long)e[7] << 24;
            if (n && n < (1ul << 20) && fseek(f, (long)at, SEEK_SET) == 0 && (b = malloc(n)) != NULL) {
                if (fread(b, 1, n, f) == n)
                    kept[i] = b, size[i] = n;
                else
                    free(b);
            }
        }
        fclose(f);
    }
    *len = size[i];
    return kept[i];
}

/* ---- the page shown */

static void show_status(void)
{
    char t[200];
    if (doc)
        snprintf(t, sizeof t, GUI_T("%s: page %u of %u, %d%%"), gui_path_name(path), page + 1, pdf_pages(doc), zoom);
    else
        snprintf(t, sizeof t, "%s", GUI_T("No file is open. File, Open..."));
    gui_set_text(status, t);
}

static void draw_page(void)             /* the page into its picture, at the zoom (or the view's width) */
{
    struct pdf_canvas cv;
    double w = 612, h = 792, scale;
    unsigned *px;
    unsigned long i, ink = 0;
    if (!doc)
        return;
    pdf_page_size(doc, page, &w, &h);
    if (fit) {                                  /* as wide as the view (less its bar), 25% to 400% */
        int room = view->r.w - GUI_SB - 4;
        zoom = (int)((room > 100 ? room : 100) * 100 / w);
        zoom = zoom < 25 ? 25 : zoom > 400 ? 400 : zoom;
    }
    while (zoom > 25 && (long)(w * zoom / 100 + 1) * (long)(h * zoom / 100 + 1) > MAX_PIXELS)
        zoom -= 5;
    scale = zoom / 100.0;
    cv.w = (int)(w * scale + 0.999), cv.h = (int)(h * scale + 0.999);
    if (cv.w < 1 || cv.h < 1 || !(px = malloc((size_t)cv.w * (size_t)cv.h * sizeof *px))) {
        gui_message(win, GUI_T("PDF Viewer"), GUI_T("There is not enough memory to draw the page at this size."), GUI_MB_OK);
        say("refused memory");
        return;
    }
    memset(px, 0xFF, (size_t)cv.w * (size_t)cv.h * sizeof *px);
    cv.px = px;
    gui_cview_cursor(view, GLOS_CURSOR_BUSY);
    pdf_draw(doc, page, scale, 0, 0, &cv);
    pdf_trim(doc, KEEP);
    gui_cview_cursor(view, GLOS_CURSOR_ARROW);
    for (i = 0; i < (unsigned long)cv.w * (unsigned long)cv.h; i++)
        px[i] &= 0xFFFFFF, ink += (px[i] & 0xFF) < 128;
    gui_image_view_set(view, px, cv.w, cv.h, cv.w);
    gui_cview_zoom(view, 100);
    gui_cview_scroll(view, 0, 0);
    free(bitmap);
    bitmap = px, bw = cv.w, bh = cv.h;
    gui_damage(view);
    show_status();
    say("page %u of %u %dx%d at %d%%", page + 1, pdf_pages(doc), cv.w, cv.h, zoom);
    say("ink %lu", ink / 100);                  /* (hundreds of dark pixels: for a look at the log, not for a test: the
                                                   drawing's sums are floating point, and a shade's last step is the
                                                   compiler's) */
}

static void forget(void)                /* the file and its picture let go (the view is not touched) */
{
    free(bitmap);
    bitmap = NULL;
    pdf_close(doc);
    doc = NULL;
    if (file)
        fclose(file);
    file = NULL;
}

static void close_file(void)
{
    gui_image_view_set(view, NULL, 0, 0, 0);
    forget();
}

static int open_file(const char *name)
{
    char t[300];
    FILE *f = gui_fopen(name, "rb");
    struct pdf *d;
    long size;
    int err = PDF_E_READ;
    if (f && fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0) {
        FILE *was = file;
        unsigned long was_size = file_size;
        file = f, file_size = (unsigned long)size;      /* (the reader reads through `file`) */
        d = pdf_open(file_read, NULL, file_size, &err);
        if (d) {
            file = was;
            close_file();
            file = f, file_size = (unsigned long)size, doc = d;
            snprintf(path, sizeof path, "%s", name);
            page = 0, fit = 1;
            say("opened %s pages=%u", gui_path_name(name), pdf_pages(doc));
            draw_page();
            return 0;
        }
        file = was, file_size = was_size;
    }
    if (f)
        fclose(f);
    say("refused %s %d", gui_path_name(name), err);
    snprintf(t, sizeof t, err == PDF_E_LOCKED ? GUI_T("%s is locked with a password: such files aren't read here yet.")
             : err == PDF_E_KIND ? GUI_T("%s is no PDF file.") : err == PDF_E_READ ? GUI_T("%s couldn't be read.")
             : GUI_T("%s is damaged: nothing of it could be made out."), gui_path_name(name));
    gui_message(win, GUI_T("PDF Viewer"), t, GUI_MB_OK);
    return -1;
}

static void go(long to)
{
    if (!doc || !pdf_pages(doc))
        return;
    to = to < 0 ? 0 : to >= (long)pdf_pages(doc) ? (long)pdf_pages(doc) - 1 : to;
    if ((unsigned)to != page) {
        page = (unsigned)to;
        draw_page();
    }
}

/* ---- Go to Page */

static struct gui_widget *goto_spin;
static int goto_value;

static void goto_done(struct gui_widget *b, int id)
{
    if (id == 1)                                /* (read while the dialog is there: gui_modal closes it) */
        goto_value = gui_value(goto_spin);
    gui_end_modal(b->win, id == 1 ? GUI_OK : GUI_CANCEL_R);
}

static void go_to(void)
{
    struct gui_window *d;
    struct gui_widget *box, *form, *row;
    if (!doc)
        return;
    d = gui_window(GUI_T("Go to Page"), 220, 0, GUI_W_DIALOG);
    d->owner = win;
    box = gui_vbox(d->root, 6, 8);
    form = gui_form(box);
    goto_spin = gui_spin(gui_row(form, GUI_T("&Page:"), NULL), 1, (int)pdf_pages(doc), (int)page + 1);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), goto_done, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), goto_done, 2)->flags |= GUI_CANCEL;
    gui_focus(goto_spin);
    goto_value = 0;
    if (gui_modal(d) == GUI_OK && goto_value >= 1)
        go(goto_value - 1);
}

/* ---- commands */

static const char *const kinds[] = { "PDF files|*.PDF", "All files|*.*", NULL };

static void command(struct gui_window *w, int id)
{
    static const int steps[] = { 25, 33, 50, 67, 75, 100, 125, 150, 200, 300, 400 };
    struct gui_file_opts o;
    char p[300];
    int i;
    (void)w;
    switch (id) {
    case C_OPEN:
        memset(&o, 0, sizeof o);
        o.filters = kinds;
        snprintf(p, sizeof p, "%s", path);
        if (gui_file_dialog_ex(win, GUI_T("Open"), 0, p, sizeof p, &o))
            open_file(p);
        break;
    case C_EXIT:
        gui_quit(0);
        break;
    case C_PREV:
        go((long)page - 1);
        break;
    case C_NEXT:
        go((long)page + 1);
        break;
    case C_FIRST:
        go(0);
        break;
    case C_LAST:
        go(doc ? (long)pdf_pages(doc) - 1 : 0);
        break;
    case C_GOTO:
        go_to();
        break;
    case C_ZOOMIN:
    case C_ZOOMOUT:
        if (!doc)
            break;
        if (id == C_ZOOMIN) {
            for (i = 0; i < 10 && steps[i] <= zoom; i++)
                ;
        } else {
            for (i = 10; i > 0 && steps[i] >= zoom; i--)
                ;
        }
        zoom = steps[i], fit = 0;
        draw_page();
        break;
    case C_ACTUAL:
        zoom = 100, fit = 0;
        draw_page();
        break;
    case C_FITW:
        fit = 1;
        draw_page();
        break;
    }
}

static void update(struct gui_window *w)
{
    unsigned pages = doc ? pdf_pages(doc) : 0;
    gui_command_enable(w, C_PREV, doc && page > 0);
    gui_command_enable(w, C_FIRST, doc && page > 0);
    gui_command_enable(w, C_NEXT, doc && page + 1 < pages);
    gui_command_enable(w, C_LAST, doc && page + 1 < pages);
    gui_command_enable(w, C_GOTO, pages > 1);
    gui_command_enable(w, C_ZOOMIN, doc && zoom < 400);
    gui_command_enable(w, C_ZOOMOUT, doc && zoom > 25);
    gui_command_enable(w, C_ACTUAL, doc != NULL);
    gui_command_enable(w, C_FITW, doc != NULL);
    gui_command_check(w, C_FITW, fit);
}

static void build(void)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *tb;
    win = gui_window(GUI_T("PDF Viewer"), 620, 440, 0);
    win->on_command = command;
    win->on_update = update;
    gui_command(win, C_OPEN, GUI_T("&Open..."), "Ctrl+O", gui_glyph("open"), GUI_T("Open a PDF file"));
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    gui_command(win, C_PREV, GUI_T("&Previous Page"), "Ctrl+PgUp", gui_glyph("back"), GUI_T("The page before"));
    gui_command(win, C_NEXT, GUI_T("&Next Page"), "Ctrl+PgDn", gui_glyph("forward"), GUI_T("The next page"));
    gui_command(win, C_FIRST, GUI_T("&First Page"), "Ctrl+Home", NULL, NULL);
    gui_command(win, C_LAST, GUI_T("&Last Page"), "Ctrl+End", NULL, NULL);
    gui_command(win, C_GOTO, GUI_T("&Go to Page..."), "Ctrl+G", NULL, NULL);
    gui_command(win, C_ZOOMIN, GUI_T("Zoom &In"), "Ctrl++", gui_glyph("zoom-in"), GUI_T("Zoom in"));
    gui_command(win, C_ZOOMOUT, GUI_T("Zoom &Out"), "Ctrl+-", gui_glyph("zoom-out"), GUI_T("Zoom out"));
    gui_command(win, C_ACTUAL, GUI_T("&Actual Size"), "Ctrl+1", NULL, NULL);
    gui_command(win, C_FITW, GUI_T("Fit &Width"), "Ctrl+2", NULL, NULL);
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_command(m, C_OPEN);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_command(m, C_ZOOMIN), gui_menu_command(m, C_ZOOMOUT);
    gui_menu_sep(m);
    gui_menu_command(m, C_ACTUAL), gui_menu_command(m, C_FITW);
    m = gui_menu(bar, GUI_T("&Go"));
    gui_menu_command(m, C_PREV), gui_menu_command(m, C_NEXT);
    gui_menu_sep(m);
    gui_menu_command(m, C_FIRST), gui_menu_command(m, C_LAST), gui_menu_command(m, C_GOTO);
    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_OPEN);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_PREV), gui_toolbar_command(tb, C_NEXT);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_ZOOMOUT), gui_toolbar_command(tb, C_ZOOMIN);
    view = gui_image_view(win->root);
    gui_grow(view, 1);
    status = gui_status(win);
}

int main(int argc, char **argv)
{
    const char *first = NULL;
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("PDF no GLOS\n");
        return 1;
    }
    for (i = 1; i < argc; i++)
        if (argv[i][0] != '-')
            first = argv[i];
    pdf_draw_jpeg(a_jpeg);
    pdf_font_faces(a_face);
    build();
    show_status();
    gui_show(win, 1);
    gui_focus(view);
    if (first)
        open_file(first);
    gui_run();
    forget();                                   /* (the window and its view are gone by now) */
    printf("PDF end\n");
    return 0;
}
