/* PDF: the PDF Viewer (GLOS v1, phase F; docs/v1/apps/pdf-viewer.md): its first part. A PDF's pages drawn one
 * at a time by GLOS's own reader and renderer (apps/shared/pdf.c, pdfdraw.c, pdffont.c, ttf.c, psfont.c): text in
 * the fonts the file carries (TrueType, Type 1, CFF) or in stand-ins (PDFFONTS.DAT, beside the program), paths,
 * pictures.
 *
 *   the page      in a view that scrolls (the bars, the wheel, the arrows; the left button moves it)
 *   pages         Ctrl+PgDn and Ctrl+PgUp, Ctrl+Home and Ctrl+End, Go to Page... (Ctrl+G)
 *   zoom          Ctrl++ and Ctrl+-, Actual Size (Ctrl+1: a point a pixel), Fit Width (Ctrl+2: as it opens)
 *   text          Find... (Ctrl+F), Find Next (F3) and Find Previous (Shift+F3) through the whole file, what is
 *                 found tinted on its page; Copy Page Text (Ctrl+C)
 *   the outline   a file's own list of its chapters ("bookmarks"), in a pane at the left that a file with one
 *                 opens (View, Outline: F9): an entry chosen shows its page
 *
 * The file is read a piece at a time as its objects are wanted, never whole; what was read for a page is let go
 * when the next is drawn. A page is drawn whole into a picture of its own (no larger than three million
 * pixels: the zoom stops there).
 *
 * PDF FILE opens the file (Files' "open").
 *
 * "PDF ..." lines on stdout (the suite's). */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "../shared/pdfdraw.h"
#include "../shared/pdffont.h"
#include "../shared/pdfnav.h"
#include "../shared/picfile.h"

enum { C_OPEN = 1, C_EXIT, C_PREV, C_NEXT, C_FIRST, C_LAST, C_GOTO, C_ZOOMIN, C_ZOOMOUT, C_ACTUAL, C_FITW, C_COPY, C_FIND, C_FINDNEXT,
       C_FINDPREV, C_OUTLINE };

#define MAX_PIXELS 3000000l
#define KEEP (2ul << 20)                /* the file's objects kept between pages: bytes */

static struct gui_window *win;
static struct gui_widget *view, *status, *split, *tree;
static struct pdf_mark *toc;            /* the file's outline */
static unsigned ntoc;
static int toc_filling;                 /* (the tree is being filled: what it says of its choice is not the user's) */
static FILE *file;
static unsigned long file_size;
static struct pdf *doc;
static char path[260];
static unsigned page;                   /* from 0 */
static int zoom = 100, fit = 1;         /* percent (100: a point a pixel); fit: the zoom follows the view's width */
static unsigned *bitmap;
static int bw, bh;
static struct pdf_letter *letters;      /* a page's text (page `lpage`), read when first wanted */
static unsigned nletters, lpage;
static int have_letters;
static char needle[100];                /* what was last looked for */
static long hit = -1;                   /* what was found on this page: its first letter, and how many */
static unsigned hit_len;
static struct { int x, y, w, h; unsigned *was; } marks[24];    /* the parts of the picture tinted for it */
static int nmarks;

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

/* ---- what is found, tinted in the page's picture (what was there is kept, and put back) */

static void marks_forget(void)          /* (the picture they were in is gone) */
{
    while (nmarks)
        free(marks[--nmarks].was);
}

static void unmark(void)
{
    int i, y;
    for (i = nmarks - 1; i >= 0; i--)
        for (y = 0; y < marks[i].h; y++)
            memcpy(bitmap + (size_t)(marks[i].y + y) * (size_t)bw + marks[i].x, marks[i].was + (size_t)y * (size_t)marks[i].w,
                   (size_t)marks[i].w * sizeof *bitmap);
    marks_forget();
}

static void mark(int x0, int y0, int x1, int y1)
{
    unsigned *was;
    int x, y;
    x0 = x0 < 0 ? 0 : x0, y0 = y0 < 0 ? 0 : y0, x1 = x1 > bw ? bw : x1, y1 = y1 > bh ? bh : y1;
    if (!bitmap || nmarks == 24 || x1 <= x0 || y1 <= y0 || !(was = malloc((size_t)(x1 - x0) * (size_t)(y1 - y0) * sizeof *was)))
        return;
    for (y = y0; y < y1; y++)
        for (x = x0; x < x1; x++) {
            unsigned px = bitmap[(size_t)y * (size_t)bw + x];
            was[(size_t)(y - y0) * (size_t)(x1 - x0) + (x - x0)] = px;
            bitmap[(size_t)y * (size_t)bw + x] = (px & 0xFFFF00) | ((px & 0xFF) * 3 / 8);       /* (yellow where it was white) */
        }
    marks[nmarks].x = x0, marks[nmarks].y = y0, marks[nmarks].w = x1 - x0, marks[nmarks].h = y1 - y0, marks[nmarks].was = was;
    nmarks++;
}

/* The box round a letter in the picture: from a quarter of its size under its baseline to four fifths over it,
   however the line lies. */
static void letter_box(const struct pdf_letter *l, double scale, int *x0, int *y0, int *x1, int *y1)
{
    double len = sqrt((double)l->dx * l->dx + (double)l->dy * l->dy), ux = len > 1e-6 ? l->dx / len : 1, uy = len > 1e-6 ? l->dy / len : 0;
    double lo_x = 1e9, lo_y = 1e9, hi_x = -1e9, hi_y = -1e9;
    int i;
    for (i = 0; i < 4; i++) {
        double along = i & 1 ? 1 : 0, up = i & 2 ? 0.8 * l->size : -0.25 * l->size;
        double x = (l->x + l->dx * along + uy * up) * scale, y = (l->y + l->dy * along - ux * up) * scale;
        lo_x = x < lo_x ? x : lo_x, hi_x = x > hi_x ? x : hi_x, lo_y = y < lo_y ? y : lo_y, hi_y = y > hi_y ? y : hi_y;
    }
    *x0 = (int)floor(lo_x), *y0 = (int)floor(lo_y), *x1 = (int)ceil(hi_x), *y1 = (int)ceil(hi_y);
}

static void show_hit(void)              /* what was found: tinted, a mark a line of it, and scrolled into sight */
{
    int rx0 = 0, ry0 = 0, rx1 = 0, ry1 = 0, have = 0, fx = 0, fy = 0, first = 1;
    unsigned i;
    unmark();
    if (hit < 0 || !have_letters || lpage != page || !bitmap)
        return;
    for (i = 0; i < hit_len && (unsigned long)hit + i < nletters; i++) {
        const struct pdf_letter *l = &letters[hit + i];
        int x0, y0, x1, y1;
        if (l->u == '\n')
            continue;
        letter_box(l, zoom / 100.0, &x0, &y0, &x1, &y1);
        if (have && y0 < ry1 && y1 > ry0 && x0 <= rx1 + 2 && x1 >= rx0 - 2) {  /* (on with the line) */
            rx0 = x0 < rx0 ? x0 : rx0, ry0 = y0 < ry0 ? y0 : ry0, rx1 = x1 > rx1 ? x1 : rx1, ry1 = y1 > ry1 ? y1 : ry1;
        } else {
            if (have)
                mark(rx0, ry0, rx1, ry1);
            rx0 = x0, ry0 = y0, rx1 = x1, ry1 = y1, have = 1;
        }
        if (first)
            fx = x0, fy = y0, first = 0;
    }
    if (have)
        mark(rx0, ry0, rx1, ry1);
    gui_cview_scroll(view, fx > view->r.w - 80 ? fx - 40 : 0, fy > view->r.h / 3 ? fy - view->r.h / 3 : 0);
    gui_damage(view);
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

/* The view's width as the window is laid out, or is about to be: a pane folded or unfolded is laid out only
   before the next drawing, and the page is drawn to fit before that (the split's bar is 6 pixels). */
static int view_width(void)
{
    int w = split->r.w - 6;
    if (gui_split_folded(split) != 1)
        w -= gui_split_get(split);
    return w > 0 ? w : view->r.w;
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
        int room = view_width() - GUI_SB - 4;
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
    marks_forget();
    free(bitmap);
    bitmap = px, bw = cv.w, bh = cv.h;
    show_hit();                                 /* (what was found on this page: marked at this size too) */
    gui_damage(view);
    show_status();
    say("page %u of %u %dx%d at %d%%", page + 1, pdf_pages(doc), cv.w, cv.h, zoom);
    say("ink %lu", ink / 100);                  /* (hundreds of dark pixels: for a look at the log, not for a test: the
                                                   drawing's sums are floating point, and a shade's last step is the
                                                   compiler's) */
}

static void forget(void)                /* the file and its picture let go (the view is not touched) */
{
    marks_forget();
    free(letters);
    letters = NULL, nletters = 0, have_letters = 0, hit = -1;
    pdf_marks_free(toc, ntoc);
    toc = NULL, ntoc = 0;
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
    toc_filling = 1;
    gui_tree_clear(tree, NULL);
    toc_filling = 0;
    forget();
}

/* The file's outline into the pane's tree, each entry under the one it belongs to (its number, and one, kept
   with it); the pane opened if there is one, folded away if not. */
static void load_outline(void)
{
    struct gui_node *at[17], **nodes;
    unsigned i;
    if (pdf_outline(doc, &toc, &ntoc) != PDF_OK)
        toc = NULL, ntoc = 0;
    if (ntoc && (nodes = malloc(ntoc * sizeof *nodes)) != NULL) {
        toc_filling = 1;
        for (i = 0; i < ntoc; i++) {
            int depth = toc[i].depth < 0 ? 0 : toc[i].depth > 15 ? 15 : toc[i].depth;
            nodes[i] = at[depth] = gui_tree_add(tree, depth ? at[depth - 1] : NULL, toc[i].title[0] ? toc[i].title : "?", NULL,
                                                (void *)(unsigned long)(i + 1), i + 1 < ntoc && toc[i + 1].depth > toc[i].depth);
        }
        for (i = 0; i + 1 < ntoc; i++)          /* (opened as the file says, now that they have what is under them) */
            if (toc[i].open && toc[i + 1].depth > toc[i].depth)
                gui_tree_expand(tree, nodes[i], 1);
        toc_filling = 0;
        free(nodes);
    }
    gui_split_fold(split, ntoc ? 0 : 1);
    if (ntoc)
        say("outline %u", ntoc);
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
            load_outline();
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
        hit = -1;
        draw_page();
    }
}

/* ---- the text: found, and copied */

static int page_letters(unsigned n)     /* page n's letters, read (and kept until another page's are wanted) */
{
    if (have_letters && lpage == n)
        return 0;
    free(letters);
    letters = NULL, nletters = 0, have_letters = 0;
    if (pdf_text(doc, n, &letters, &nletters) != PDF_OK)
        return -1;
    pdf_trim(doc, KEEP);
    have_letters = 1, lpage = n;
    return 0;
}

/* The next place the text is (or the one before): on this page after what was last found, then page after
   page, round the file's end to where the search began. */
static void find_next(int back)
{
    unsigned pages, tried, p, len = 0;
    char t[200];
    if (!doc || !needle[0] || !(pages = pdf_pages(doc)))
        return;
    gui_cview_cursor(view, GLOS_CURSOR_BUSY);
    for (tried = 0, p = page; tried <= pages; tried++) {
        if (page_letters(p) == 0) {
            long from = tried == 0 && hit >= 0 ? (back ? hit - 1 : hit + 1) : back ? (long)nletters : 0, at = -1;
            if (from >= 0)
                at = pdf_text_find(letters, nletters, needle, (unsigned)from, back, &len);
            if (at >= 0) {
                hit = at, hit_len = len;
                gui_cview_cursor(view, GLOS_CURSOR_ARROW);
                if (p != page) {
                    page = p;
                    draw_page();                /* (which marks it) */
                } else {
                    show_hit();
                }
                show_status();
                say("found %s page %u marks=%d", needle, p + 1, nmarks);
                return;
            }
        }
        if (tried == 0)
            hit = -1;
        p = back ? (p ? p - 1 : pages - 1) : (p + 1 < pages ? p + 1 : 0);
    }
    gui_cview_cursor(view, GLOS_CURSOR_ARROW);
    hit = -1;
    unmark();
    gui_damage(view);
    say("not found %s", needle);
    snprintf(t, sizeof t, GUI_T("\"%s\" was not found in this file."), needle);
    gui_message(win, GUI_T("Find"), t, GUI_MB_OK);
}

static void find(void)
{
    char what[sizeof needle];
    if (!doc)
        return;
    snprintf(what, sizeof what, "%s", needle);
    if (!gui_ask(win, GUI_T("Find"), GUI_T("Find what:"), what, sizeof what) || !what[0])
        return;
    if (strcmp(what, needle)) {                 /* (another text: from this page's top) */
        snprintf(needle, sizeof needle, "%s", what);
        hit = -1;
        unmark();
    }
    find_next(0);
}

static void copy_text(void)
{
    unsigned long n = 0;
    char *t;
    if (!doc || page_letters(page) != 0)
        return;
    if (!nletters) {
        gui_set_text(status, GUI_T("This page has no text to copy (it may be a picture of a page)."));
        say("copied 0");
        return;
    }
    if (!(t = pdf_text_utf8(letters, 0, nletters, &n)))
        return;
    gui_clip_set(t, n);
    free(t);
    gui_set_text(status, GUI_T("The page's text was copied."));
    say("copied %lu", n);
}

/* ---- the outline's pane */

static void outline_on(struct gui_widget *w, int what)
{
    struct gui_node *n = gui_tree_get(w);
    unsigned long i = n ? (unsigned long)gui_node_user(n) : 0;
    if (toc_filling || (what != GUI_ON_SELECT && what != GUI_ON_ACTIVATE) || !doc || !i || i > ntoc)
        return;
    say("outline chose %lu page %ld", i, toc[i - 1].page + 1);
    if (toc[i - 1].page >= 0)
        go(toc[i - 1].page);
    else
        gui_set_text(status, GUI_T("This entry leads out of the file."));
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
    case C_COPY:
        copy_text();
        break;
    case C_FIND:
        find();
        break;
    case C_FINDNEXT:
    case C_FINDPREV:
        find_next(id == C_FINDPREV);
        break;
    case C_OUTLINE:                             /* the pane folded away, or back (the page fitted again to what is left) */
        gui_split_fold(split, gui_split_folded(split) == 1 ? 0 : 1);
        if (fit)
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
    gui_command_enable(w, C_COPY, doc != NULL);
    gui_command_enable(w, C_FIND, doc != NULL);
    gui_command_enable(w, C_FINDNEXT, doc && needle[0]);
    gui_command_enable(w, C_FINDPREV, doc && needle[0]);
    gui_command_enable(w, C_OUTLINE, ntoc > 0);
    gui_command_check(w, C_OUTLINE, ntoc && gui_split_folded(split) != 1);
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
    gui_command(win, C_COPY, GUI_T("&Copy Page Text"), "Ctrl+C", gui_glyph("copy"), GUI_T("Copy this page's text"));
    gui_command(win, C_FIND, GUI_T("&Find..."), "Ctrl+F", gui_glyph("find"), GUI_T("Find text in the file"));
    gui_command(win, C_FINDNEXT, GUI_T("Find &Next"), "F3", NULL, NULL);
    gui_command(win, C_FINDPREV, GUI_T("Find &Previous"), "Shift+F3", NULL, NULL);
    gui_command(win, C_OUTLINE, GUI_T("&Outline"), "F9", NULL, NULL);
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_command(m, C_OPEN);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_command(m, C_COPY);
    gui_menu_sep(m);
    gui_menu_command(m, C_FIND), gui_menu_command(m, C_FINDNEXT), gui_menu_command(m, C_FINDPREV);
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_command(m, C_ZOOMIN), gui_menu_command(m, C_ZOOMOUT);
    gui_menu_sep(m);
    gui_menu_command(m, C_ACTUAL), gui_menu_command(m, C_FITW);
    gui_menu_sep(m);
    gui_menu_command(m, C_OUTLINE);
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
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_FIND);
    split = gui_split(win->root, 0, 170);       /* the outline | the page */
    gui_grow(split, 1);
    tree = gui_tree(split);
    gui_on(tree, outline_on, 0);
    view = gui_image_view(split);
    gui_split_fold(split, 1);                   /* (no outline until a file has one) */
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
