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
 *   locked files  most open as they are (they are locked only against printing or copying); one that wants a
 *                 password asks for it
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
       C_FINDPREV, C_OUTLINE , C_SELECT, C_CONT, C_TWO};

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
static unsigned *bitmap;                /* the page shown: one of the cache's pictures (continuous: comp) */
static int bw, bh;
/* (2026-11-23) Continuous pages: the page shown with the next under it, one picture (comp), the next's top at
   split_y; once the view's top is in the next, it is the page shown (and the one after it goes under it); Up,
   Page Up or the wheel at the very top go back a page. Two pages' pictures at a time, whatever the file's length. */
#define CONT_GAP 12
static int cont, two, split_y;          /* (two: two pages side by side, the next at the right; split_y 0 then) */
static unsigned *comp;
static int keep_x = -1, keep_y = -1;   /* (draw_page: where the view goes; -1: the page's top) */
static void draw_page(void);

/* (phase F) The pages drawn are kept (CACHED at most, CACHE_PIXELS in all), by page and zoom: going back to one
   is at once. After a page is shown, the next is drawn ahead on a worker's thread (the toolkit's tasks). The
   reader is one thread's at a time: it reads through `file`, and keeps the file's objects; every call into it
   is under `reader` (the window's thread waits, at worst, for the page being drawn ahead, which is the one it
   is likely to want). `gen` counts the files: a page drawn ahead for one that has gone is dropped. */
#define CACHED 4
#define CACHE_PIXELS 4500000ul
static struct cached { unsigned page; int zoom; unsigned *px; int w, h; unsigned long ink, used; } cache[CACHED];
static unsigned long cache_clock;
static glos_handle reader;
static unsigned gen;
static struct gui_task *ahead;          /* the page being drawn ahead, or NULL */

static void lock(void) { if (reader) glos_take(reader, 0xFFFFFFFFu); }
static void unlock(void) { if (reader) glos_give(reader); }

static int cache_find(unsigned pg, int z)
{
    int i;
    for (i = 0; i < CACHED; i++)
        if (cache[i].px && cache[i].page == pg && cache[i].zoom == z)
            return i;
    return -1;
}

static void cache_put(unsigned pg, int z, unsigned *px, int w, int h, unsigned long ink)   /* (px: the cache's now) */
{
    unsigned long total = (unsigned long)w * (unsigned long)h;
    int i, k = -1;
    for (i = 0; i < CACHED; i++)
        total += cache[i].px ? (unsigned long)cache[i].w * (unsigned long)cache[i].h : 0;
    for (;;) {                                  /* the oldest let go (never the page shown) until it fits */
        int old = -1, empty = -1;
        for (i = 0; i < CACHED; i++) {
            if (!cache[i].px) {
                empty = i;
                continue;
            }
            if (cache[i].px != bitmap && (old < 0 || cache[i].used < cache[old].used))
                old = i;
        }
        if (empty >= 0 && total <= CACHE_PIXELS) {
            k = empty;
            break;
        }
        if (old < 0)
            break;
        total -= (unsigned long)cache[old].w * (unsigned long)cache[old].h;
        free(cache[old].px);
        cache[old].px = NULL;
    }
    if (k < 0) {                                /* (no room: it is the shown one's alone, or let go) */
        if (px != bitmap)
            free(px);
        return;
    }
    cache[k].page = pg, cache[k].zoom = z, cache[k].px = px, cache[k].w = w, cache[k].h = h, cache[k].ink = ink;
    cache[k].used = ++cache_clock;
}

static void cache_clear(void)
{
    int i;
    for (i = 0; i < CACHED; i++)
        free(cache[i].px), cache[i].px = NULL;
}
static struct pdf_letter *letters;      /* a page's text (page `lpage`), read when first wanted */
static unsigned nletters, lpage;
static int have_letters;
static char needle[100];                /* what was last looked for */
static long hit = -1;                   /* what was found on this page: its first letter, and how many */
static unsigned hit_len;
static int text_tool;                   /* Select Text: the left button chooses the page's text (else it moves the page) */
static long sel_a = -1, sel_b = -1;     /* ... where the choosing began and where it is: letters of this page's (-1: none) */
static int choosing;                    /* the button is down choosing */
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

static void show_hit_at(int scroll);
static void show_hit(void) { show_hit_at(1); }

static void show_hit_at(int scroll)     /* what was found (or chosen): tinted, a mark a line of it, and scrolled into sight */
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
    if (scroll)
        gui_cview_scroll(view, fx > view->r.w - 80 ? fx - 40 : 0, fy > view->r.h / 3 ? fy - view->r.h / 3 : 0);
    gui_damage(view);
}

/* ---- text chosen with the mouse (Select Text): the letters from where the button went down to where it is, in
   the order the page's words show them, tinted as what is found is; Copy takes them. */

/* The page's letter at the picture's point, or the nearest; (above, below: only those wholly above the line y, or
   wholly below it: Shift and the arrows' line up or down) */
static long letter_near_in(int x, int y, int above, int below, int line)
{
    long best = -1;
    double best_d = 1e18;
    unsigned i;
    for (i = 0; i < nletters; i++) {
        int x0, y0, x1, y1;
        double cx, cy, d;
        if (letters[i].u == '\n')
            continue;
        letter_box(&letters[i], zoom / 100.0, &x0, &y0, &x1, &y1);
        if ((above && y1 > line) || (below && y0 < line))
            continue;
        if (x >= x0 && x < x1 && y >= y0 && y < y1)
            return (long)i;
        cx = x < x0 ? x0 - x : x >= x1 ? x - x1 + 1 : 0;
        cy = y < y0 ? y0 - y : y >= y1 ? y - y1 + 1 : 0;
        d = cx * cx + cy * cy * 4;              /* (the line below is further than a letter beside) */
        if (d < best_d)
            best_d = d, best = (long)i;
    }
    return best;
}

static long letter_near(int x, int y) { return letter_near_in(x, y, 0, 0, 0); }

static void show_sel(void)
{
    if (sel_a < 0 || sel_b < 0)
        return;
    hit = sel_a < sel_b ? sel_a : sel_b;
    hit_len = (unsigned)((sel_a < sel_b ? sel_b - sel_a : sel_a - sel_b) + 1);
    show_hit_at(0);
}

static int page_letters(unsigned n);

static void say_chosen(void)
{
    unsigned long n = 0;
    char *t = hit >= 0 ? pdf_text_utf8(letters, (unsigned)hit, (unsigned)hit + hit_len, &n) : NULL, *c;
    for (c = t; c && *c; c++)                   /* (a line's end said as a space: one line said) */
        if (*c == '\n' || *c == '\r')
            *c = ' ';
    say("chose %u letters: %.60s", hit_len, t ? t : "");        /* (the suite's: what was chosen, its start) */
    free(t);
}

/* (2026-11-23) A double click chooses the word there, a third click the line; Shift and the arrows move the
   chosen text's end a letter, or a line up or down. */
static unsigned dbl_ms;
static int dbl_x, dbl_y;

static int word_letter(unsigned long u) { return u != ' ' && u != '\n' && u != '\t'; }

static void choose_span(long k, int line)
{
    long a = k, b = k;
    if (line) {
        while (a > 0 && letters[a - 1].u != '\n')
            a--;
        while (b + 1 < (long)nletters && letters[b + 1].u != '\n')
            b++;
    } else if (word_letter(letters[k].u)) {
        while (a > 0 && word_letter(letters[a - 1].u))
            a--;
        while (b + 1 < (long)nletters && word_letter(letters[b + 1].u))
            b++;
    }
    sel_a = a, sel_b = b;
    show_sel();
    say_chosen();
}

/* (continuous) The view's top in the next page: it is the page shown now, the same place in sight. */
static void cont_tick(void *arg)
{
    int x, y;
    (void)arg;
    if (!cont || !doc || !split_y || choosing)
        return;
    gui_cview_get_scroll(view, &x, &y, NULL);
    if (y >= split_y && page + 1 < pdf_pages(doc)) {
        page++;
        hit = -1, sel_a = sel_b = -1;
        keep_x = x, keep_y = y - split_y;
        draw_page();
    }
}

static int view_mouse(struct gui_widget *w, struct gui_ev *e, int x, int y)
{
    long k;
    (void)w;
    if (cont && doc && page > 0 && !(e->type == GUI_EV_KEY && e->mods)
        && ((e->type == GUI_EV_KEY && (e->key == GUI_K_UP || e->key == GUI_K_PGUP)) || (e->type == GUI_EV_WHEEL && e->wheel > 0))) {
        int sx, sy, vh;
        gui_cview_get_scroll(view, &sx, &sy, &vh);
        if (sy <= 0) {                          /* (at the very top: the page before, its end in sight) */
            int step = e->type == GUI_EV_KEY && e->key == GUI_K_PGUP ? vh : 40;
            page--;
            hit = -1, sel_a = sel_b = -1;
            keep_x = sx, keep_y = 0;            /* (put right once the picture is made: below) */
            {
                int sp;
                draw_page();
                sp = split_y;                   /* (the page that was shown starts there now) */
                gui_cview_scroll(view, sx, sp - step > 0 ? sp - step : 0);
            }
            return 1;
        }
    }
    if (!text_tool || !doc)
        return 0;                               /* (the button moves the page, as ever) */
    if (e->type == GUI_EV_KEY && (e->mods & GLOS_MOD_SHIFT) && sel_a >= 0 && sel_b >= 0 && nletters
        && (e->key == GUI_K_LEFT || e->key == GUI_K_RIGHT || e->key == GUI_K_UP || e->key == GUI_K_DOWN)) {
        k = sel_b;
        if (e->key == GUI_K_LEFT)
            k = k > 0 ? k - 1 : 0;
        else if (e->key == GUI_K_RIGHT)
            k = k + 1 < (long)nletters ? k + 1 : k;
        else {                                  /* (the nearest letter a line above or below the end's) */
            int x0, y0, x1, y1;
            letter_box(&letters[sel_b], zoom / 100.0, &x0, &y0, &x1, &y1);
            k = e->key == GUI_K_UP ? letter_near_in((x0 + x1) / 2, y0, 1, 0, y0) : letter_near_in((x0 + x1) / 2, y1, 0, 1, y1);
            if (k < 0)
                k = sel_b;
        }
        sel_b = k;
        show_sel();
        say_chosen();
        return 1;
    }
    if (e->type == GUI_EV_PRESS && e->button == 1) {
        unsigned now = glos_ticks();
        if (page_letters(page) != 0 || !nletters)
            return 1;
        k = letter_near(x, y);
        if (k >= 0 && dbl_ms && now - dbl_ms < 500 && abs(x - dbl_x) < 5 && abs(y - dbl_y) < 5) {
            dbl_ms = 0, choosing = 0;           /* the third: the line */
            choose_span(k, 1);
            return 1;
        }
        if (k >= 0 && e->clicks >= 2) {         /* the second: the word */
            dbl_ms = now ? now : 1, dbl_x = x, dbl_y = y, choosing = 0;
            choose_span(k, 0);
            return 1;
        }
        dbl_ms = 0;
        sel_a = sel_b = k;
        choosing = k >= 0;
        show_sel();
        return 1;
    }
    if (e->type == GUI_EV_MOVE && choosing) {
        k = letter_near(x, y);
        if (k >= 0 && k != sel_b) {
            sel_b = k;
            show_sel();
        }
        return 1;
    }
    if (e->type == GUI_EV_RELEASE && choosing) {
        choosing = 0;
        say_chosen();
        return 1;
    }
    return e->type == GUI_EV_PRESS || e->type == GUI_EV_RELEASE || e->type == GUI_EV_MOVE;
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

/* The zoom page pg is drawn at: the view's width (fit) or the user's, within MAX_PIXELS. (Under the lock.) */
static int zoom_for(unsigned pg, double *w, double *h)
{
    int z = zoom;
    *w = 612, *h = 792;
    pdf_page_size(doc, pg, w, h);
    if (fit) {                                  /* as wide as the view (less its bar), 25% to 400% */
        int room = view_width() - GUI_SB - 4;
        if (two)                                /* (two pages: each half of it) */
            room = (room - CONT_GAP) / 2;
        z = (int)((room > 100 ? room : 100) * 100 / *w);
        z = z < 25 ? 25 : z > 400 ? 400 : z;
    }
    while (z > 25 && (long)(*w * z / 100 + 1) * (long)(*h * z / 100 + 1) > MAX_PIXELS)
        z -= 5;
    return z;
}

/* Page pg drawn at z into a new picture (under the lock): it, or NULL (no memory). */
static unsigned *draw(unsigned pg, int z, double w, double h, int *pw, int *ph, unsigned long *ink)
{
    struct pdf_canvas cv;
    double scale = z / 100.0;
    unsigned *px;
    unsigned long i;
    cv.w = (int)(w * scale + 0.999), cv.h = (int)(h * scale + 0.999);
    if (cv.w < 1 || cv.h < 1 || !(px = malloc((size_t)cv.w * (size_t)cv.h * sizeof *px)))
        return NULL;
    memset(px, 0xFF, (size_t)cv.w * (size_t)cv.h * sizeof *px);
    cv.px = px;
    pdf_draw(doc, pg, scale, 0, 0, &cv);
    pdf_trim(doc, KEEP);
    *ink = 0;
    for (i = 0; i < (unsigned long)cv.w * (unsigned long)cv.h; i++)
        px[i] &= 0xFFFFFF, *ink += (px[i] & 0xFF) < 128;
    *pw = cv.w, *ph = cv.h;
    return px;
}

struct ahead_job { unsigned page, gen; int zoom; double w, h; unsigned *px; int pw, ph; unsigned long ink; };

static void ahead_work(struct gui_task *t, void *arg)  /* (a worker's thread) */
{
    struct ahead_job *a = arg;
    (void)t;
    lock();
    if (doc && a->gen == gen && !gui_task_cancelled(t))
        a->px = draw(a->page, a->zoom, a->w, a->h, &a->pw, &a->ph, &a->ink);
    unlock();
}

static void draw_ahead(void);

static void ahead_done(struct gui_window *owner, void *arg, int cancelled)
{
    struct ahead_job *a = arg;
    (void)owner;
    ahead = NULL;
    if (a->px && !cancelled && a->gen == gen && cache_find(a->page, a->zoom) < 0) {
        cache_put(a->page, a->zoom, a->px, a->pw, a->ph, a->ink);
        say("ahead page %u", a->page + 1);
    } else {
        free(a->px);
    }
    free(a);
    if (!cancelled)
        draw_ahead();                           /* (the next is kept: the one before, if it is not) */
}

static void draw_ahead(void)            /* the next page, then the one before, if not kept already and nothing is being
                                           drawn */
{
    struct ahead_job *a;
    int k;
    if (!doc || ahead)
        return;
    for (k = 0; k < 2; k++) {
        unsigned pg = k == 0 ? page + 1 : page - 1;
        if ((k == 0 && page + 1 >= pdf_pages(doc)) || (k == 1 && page == 0) || !(a = calloc(1, sizeof *a)))
            continue;
        a->page = pg, a->gen = gen;
        lock();
        a->zoom = zoom_for(a->page, &a->w, &a->h);
        unlock();
        if (cache_find(a->page, a->zoom) >= 0) {
            free(a);
            continue;
        }
        if (!(ahead = gui_task_start(win, NULL, ahead_work, a, ahead_done, 0)))
            free(a);
        return;
    }
}


/* (continuous) The page shown and the next one in one picture, the next's top at split_y: 0, or -1 (none made:
   the last page, or no memory) */
static int compose(const unsigned *px, int pw, int ph, int z)
{
    double w, h;
    unsigned *nx;
    unsigned long ink = 0;
    int nw = 0, nh = 0, k, cw, ch, y;
    free(comp), comp = NULL, split_y = 0;
    if ((!cont && !two) || page + 1 >= pdf_pages(doc))
        return -1;
    if ((k = cache_find(page + 1, z)) >= 0) {
        nx = cache[k].px, nw = cache[k].w, nh = cache[k].h;
        cache[k].used = ++cache_clock;
    } else {
        lock();
        w = 612, h = 792;
        pdf_page_size(doc, page + 1, &w, &h);
        nx = draw(page + 1, z, w, h, &nw, &nh, &ink);
        unlock();
        if (!nx)
            return -1;
        cache_put(page + 1, z, nx, nw, nh, ink);
    }
    if (two)                                    /* side by side: the next at the right */
        cw = pw + CONT_GAP + nw, ch = ph > nh ? ph : nh;
    else                                        /* one under the other */
        cw = pw > nw ? pw : nw, ch = ph + CONT_GAP + nh;
    if (!(comp = malloc((size_t)cw * (size_t)ch * sizeof *comp)))
        return -1;
    for (y = 0; y < ch; y++)                    /* (grey round and between them, the page shown at the corner) */
        for (k = 0; k < cw; k++)
            comp[(size_t)y * cw + k] = 0x808080;
    for (y = 0; y < ph; y++)
        memcpy(comp + (size_t)y * cw, px + (size_t)y * pw, (size_t)pw * sizeof *px);
    for (y = 0; y < nh; y++)
        if (two)
            memcpy(comp + (size_t)y * cw + pw + CONT_GAP, nx + (size_t)y * nw, (size_t)nw * sizeof *nx);
        else
            memcpy(comp + (size_t)(ph + CONT_GAP + y) * cw, nx + (size_t)y * nw, (size_t)nw * sizeof *nx);
    split_y = two ? 0 : ph + CONT_GAP;
    gui_image_view_set(view, comp, cw, ch, cw);
    bitmap = comp, bw = cw, bh = ch;
    say("%s %u and %u", two ? "two pages" : "continuous", page + 1, page + 2);
    return 0;
}

static void draw_page(void)             /* the page into its picture, at the zoom (or the view's width) */
{
    double w, h;
    unsigned *px;
    unsigned long ink = 0;
    int k, pw = 0, ph = 0, z;
    if (!doc)
        return;
    lock();
    z = zoom_for(page, &w, &h);
    unlock();
    unmark();                                   /* (the picture shown as it was drawn: it is kept) */
    if ((k = cache_find(page, z)) >= 0) {
        px = cache[k].px, pw = cache[k].w, ph = cache[k].h, ink = cache[k].ink;
        cache[k].used = ++cache_clock;
        say("cached %u", page + 1);
    } else {
        gui_cview_cursor(view, GLOS_CURSOR_BUSY);
        lock();
        px = draw(page, z, w, h, &pw, &ph, &ink);
        unlock();
        gui_cview_cursor(view, GLOS_CURSOR_ARROW);
        if (!px) {
            gui_message(win, GUI_T("PDF Viewer"), GUI_T("There is not enough memory to draw the page at this size."), GUI_MB_OK);
            say("refused memory");
            return;
        }
    }
    zoom = z;
    if (k < 0)
        cache_put(page, z, px, pw, ph, ink);
    if (compose(px, pw, ph, z) != 0) {          /* (one page: its own picture) */
        free(comp), comp = NULL, split_y = 0;
        gui_image_view_set(view, px, pw, ph, pw);
        bitmap = px, bw = pw, bh = ph;
    }
    gui_cview_zoom(view, 100);
    gui_cview_scroll(view, keep_x > 0 ? keep_x : 0, keep_y > 0 ? keep_y : 0);
    keep_x = keep_y = -1;
    show_hit();                                 /* (what was found on this page: marked at this size too) */
    gui_damage(view);
    show_status();
    say("page %u of %u %dx%d at %d%%", page + 1, pdf_pages(doc), pw, ph, zoom);
    say("ink %lu", ink / 100);                  /* (hundreds of dark pixels: for a look at the log, not for a test: the
                                                   drawing's sums are floating point, and a shade's last step is the
                                                   compiler's) */
    draw_ahead();
}

static void forget(void)                /* the file and its pictures let go (the view is not touched) */
{
    free(comp), comp = NULL, split_y = 0;
    marks_forget();
    free(letters);
    letters = NULL, nletters = 0, have_letters = 0, hit = -1, sel_a = sel_b = -1;
    pdf_marks_free(toc, ntoc);
    toc = NULL, ntoc = 0;
    if (ahead)
        gui_task_cancel(ahead);                 /* (its page is dropped when it is done: gen has moved on) */
    lock();
    gen++;
    cache_clear();
    bitmap = NULL;
    pdf_close(doc);
    doc = NULL;
    if (file)
        fclose(file);
    file = NULL;
    unlock();
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
    lock();
    if (pdf_outline(doc, &toc, &ntoc) != PDF_OK)
        toc = NULL, ntoc = 0;
    unlock();
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
    int err = PDF_E_READ, asked = 0, gave_up = 0;
    if (f && fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0) {
        FILE *was = file;
        unsigned long was_size = file_size;
        lock();                                 /* (nothing drawn ahead from the file shown while `file` is this one) */
        file = f, file_size = (unsigned long)size;      /* (the reader reads through `file`) */
        d = pdf_open(file_read, NULL, file_size, &err);
        while (!d && err == PDF_E_LOCKED) {             /* it wants a password: asked for until it is right, or given up */
            char pw[128] = "";
            snprintf(t, sizeof t, asked ? GUI_T("That is not its password. The password of %s:") : GUI_T("%s is locked. Its password:"),
                     gui_path_name(name));
            if (!gui_ask_secret(win, GUI_T("Password"), t, pw, sizeof pw)) {
                gave_up = 1;
                break;
            }
            asked = 1;
            d = pdf_open_password(file_read, NULL, file_size, pw, &err);
            memset(pw, 0, sizeof pw);
            if (!d && err == PDF_E_LOCKED)
                say("wrong password %s", gui_path_name(name));
        }
        if (d) {
            file = was;
            unlock();
            close_file();
            lock();
            file = f, file_size = (unsigned long)size, doc = d;
            unlock();
            snprintf(path, sizeof path, "%s", name);
            page = 0, fit = 1;
            say("opened %s pages=%u", gui_path_name(name), pdf_pages(doc));
            load_outline();
            draw_page();
            return 0;
        }
        file = was, file_size = was_size;
        unlock();
    }
    if (f)
        fclose(f);
    say("refused %s %d", gui_path_name(name), err);
    if (gave_up)                                /* (the password was not given: nothing more to say) */
        return -1;
    snprintf(t, sizeof t, err == PDF_E_KIND ? GUI_T("%s is no PDF file.") : err == PDF_E_READ ? GUI_T("%s couldn't be read.")
             : err == PDF_E_MEMORY ? GUI_T("There is not enough memory to open %s.")
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
        hit = -1, sel_a = sel_b = -1;
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
    lock();
    if (pdf_text(doc, n, &letters, &nletters) != PDF_OK) {
        unlock();
        return -1;
    }
    pdf_trim(doc, KEEP);
    unlock();
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
            hit = -1, sel_a = sel_b = -1;
        p = back ? (p ? p - 1 : pages - 1) : (p + 1 < pages ? p + 1 : 0);
    }
    gui_cview_cursor(view, GLOS_CURSOR_ARROW);
    hit = -1, sel_a = sel_b = -1;
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
        hit = -1, sel_a = sel_b = -1;
        unmark();
    }
    find_next(0);
}

static void copy_text(void)
{
    unsigned long n = 0, from = 0, count;
    char *t;
    int some;
    if (!doc || page_letters(page) != 0)
        return;
    if (!nletters) {
        gui_set_text(status, GUI_T("This page has no text to copy (it may be a picture of a page)."));
        say("copied 0");
        return;
    }
    some = sel_a >= 0 && sel_b >= 0 && hit >= 0;       /* (the text chosen with the mouse, or the page's) */
    count = nletters;
    if (some)
        from = (unsigned long)hit, count = hit_len;
    if (!(t = pdf_text_utf8(letters, (unsigned)from, (unsigned)(from + count), &n)))
        return;
    gui_clip_set(t, n);
    free(t);
    gui_set_text(status, some ? GUI_T("The chosen text was copied.") : GUI_T("The page's text was copied."));
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

static void goto_done(struct gui_widget *b, int what)
{
    (void)what;                         /* (the button's own number is b->id) */
    if (b->id == 1)                                /* (read while the dialog is there: gui_modal closes it) */
        goto_value = gui_value(goto_spin);
    gui_end_modal(b->win, b->id == 1 ? GUI_OK : GUI_CANCEL_R);
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
    if (gui_modal(d) != GUI_OK)
        say("go to left");
    else if (goto_value >= 1)
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
        go((long)page - (two ? 2 : 1));
        break;
    case C_NEXT:
        go((long)page + (two ? 2 : 1));
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
    case C_CONT:                                /* continuous pages, or one at a time */
    case C_TWO:                                 /* ... two side by side (the one or the other: neither) */
        if (id == C_CONT)
            cont = !cont, two = 0;
        else
            two = !two, cont = 0;
        say("%s %s", id == C_CONT ? "continuous" : "two pages", (id == C_CONT ? cont : two) ? "on" : "off");
        if (doc)
            draw_page();
        break;
    case C_SELECT:                              /* the mouse chooses text, or moves the page again */
        text_tool = !text_tool;
        gui_cview_style(view, GUI_CV_CENTRE | GUI_CV_RING | (text_tool ? GUI_CV_TOOLS : 0), 0);   /* (the left button: the
                                                   app's, not the view's pan) */
        gui_cview_cursor(view, text_tool ? GLOS_CURSOR_TEXT : GLOS_CURSOR_MOVE);
        say("select text %s", text_tool ? "on" : "off");
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
    gui_command_check(w, C_SELECT, text_tool);
    gui_command_check(w, C_CONT, cont);
    gui_command_check(w, C_TWO, two);
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
    gui_window_icon(win, "app-pdf");
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
    gui_command(win, C_COPY, GUI_T("&Copy"), "Ctrl+C", gui_glyph("copy"), GUI_T("Copy the text chosen, or the page's"));
    gui_command(win, C_SELECT, GUI_T("&Select Text"), "Ctrl+T", gui_glyph("select"),
                GUI_T("The mouse chooses text to copy (else it moves the page)"));
    gui_command(win, C_FIND, GUI_T("&Find..."), "Ctrl+F", gui_glyph("find"), GUI_T("Find text in the file"));
    gui_command(win, C_FINDNEXT, GUI_T("Find &Next"), "F3", NULL, NULL);
    gui_command(win, C_FINDPREV, GUI_T("Find &Previous"), "Shift+F3", NULL, NULL);
    gui_command(win, C_OUTLINE, GUI_T("&Outline"), "F9", NULL, NULL);
    gui_command(win, C_CONT, GUI_T("&Continuous Pages"), "F7", NULL, GUI_T("The pages one under another, scrolled through"));
    gui_command(win, C_TWO, GUI_T("&Two Pages"), "F8", NULL, GUI_T("Two pages side by side, as a book opens"));
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_command(m, C_OPEN);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_command(m, C_COPY), gui_menu_command(m, C_SELECT);
    gui_menu_sep(m);
    gui_menu_command(m, C_FIND), gui_menu_command(m, C_FINDNEXT), gui_menu_command(m, C_FINDPREV);
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_command(m, C_ZOOMIN), gui_menu_command(m, C_ZOOMOUT);
    gui_menu_sep(m);
    gui_menu_command(m, C_ACTUAL), gui_menu_command(m, C_FITW);
    gui_menu_sep(m);
    gui_menu_command(m, C_CONT), gui_menu_command(m, C_TWO), gui_menu_command(m, C_OUTLINE);
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
    gui_cview_on_event(view, view_mouse);       /* (Select Text: the mouse chooses text there) */
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
    reader = glos_lock();                       /* (the reader's: the window's thread and the one drawing ahead) */
    build();
    gui_idle(cont_tick, NULL, 100);             /* (continuous pages: the page shown follows the scrolling) */
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
