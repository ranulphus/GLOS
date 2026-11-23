/* WEB: the Web Reader (GLOS v1, phase F; docs/v1/apps/web-reader.md): its first part. Reading the web from a
 * 486: pages with their text, headings, lists, tables and links, over HTTPS; no scripts, no styles' sheets. Not a
 * browser of today's kind: a reader.
 *
 *   the address   Enter goes there: a whole address, a site's name (https:// is put before it), a file's path,
 *                 or words to search for (DuckDuckGo's plain pages); Ctrl+L puts the keyboard in it
 *   the page      its text as HTML says it (apps/shared/html.c), wrapped to the window; a link pressed (or
 *                 reached by Tab, then Enter) is followed; pictures are shown in it (a file's at once, a web
 *                 server's once the page is there: the page is made again with them), made no wider than the
 *                 page; one not had by the words that stand for it (a picture that a link leads to: alone)
 *   going         Back (Alt+Left) and Forward (Alt+Right) through where one has been, Reload (F5), Stop (Esc),
 *                 Home (the page it starts with)
 *
 * http: and https: pages are fetched on a thread of their own (apps/shared/webget.c); file: pages and a DOS path
 * are read from the disk; about:home is its own. Text files are shown as they are.
 *
 * WEB URL opens that page. WEB -dump URL [-resolve NAME:ADDRESS] fetches a page with no window and prints what
 * was read of it, a line a block (the suite's: a page over the network on a real GLOS).
 *
 * "WEB ..." lines on stdout (the suite's). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/gui.h>
#include <glos/http.h>
#include <glos/tls.h>

#include "../shared/html.h"
#include "../shared/picfile.h"
#include "../shared/url.h"
#include "../shared/webget.h"

enum { C_BACK = 1, C_FORWARD, C_RELOAD, C_STOP, C_HOME, C_ADDRESS, C_OPEN, C_EXIT, C_FIND, C_FINDNEXT, C_FINDPREV, C_MARK,
       C_MARKS, C_READER, C_MARK_RENAME, C_MARK_GO = 100 /* (C_MARK_GO + n: the Bookmarks menu's nth) */ };
#define MENU_MARKS 20                   /* (the bookmarks the menu lists: the first so many) */

#define PAGE_MAX (2ul << 20)            /* a page's bytes kept at most */
#define HISTORY 40

static struct gui_window *win;
static struct gui_widget *address, *view, *status;
static char current[1024];              /* the page shown */
static char *history[HISTORY];          /* where one has been; `at` is the page shown */
static int hpos[HISTORY];               /* (phase F) how far each was scrolled when it was left (-1: never left) */
static int restoring;                   /* (Back, Forward: the page shown at its place when it comes) */
static int restored;                    /* (... and so it was: not at its #place) */
static void push_place(const char *url);
static int nhistory, at = -1;
static struct webget *getting;          /* a page on its way ... */
static char getting_url[1024];
static int getting_push;                /* ... and whether it is a new place (or Back, Forward, Reload) */
static unsigned *picture;               /* a picture shown as a page: its pixels (the document only points at them) */
static unsigned blocks, links;

static const char home_page[] =
    "<title>Web Reader</title><h1>Web Reader</h1>"
    "<p>Type an address above and press Enter: a site's name (<b>example.org</b>), a whole address, a file's path, or "
    "words to search for.</p>"
    "<p>Pages are shown as text with their links and pictures: no scripts are run. Ctrl+D keeps the page shown "
    "among your <a href=\"about:bookmarks\">bookmarks</a> (Ctrl+B).</p>"
    "<ul><li><a href=\"https://lite.duckduckgo.com/lite/\">Search the web (DuckDuckGo's plain pages)</a>"
    "<li><a href=\"https://wiby.me/\">Wiby: a search of the small, old web</a>"
    "<li><a href=\"http://frogfind.com/\">FrogFind: pages made plain</a></ul>";

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("WEB ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- a page into the view: what the HTML reader says, as a rich-text document */

static struct gui_rdoc *doc;
static char page_title[240];
static int text_in_block;                /* (the block begun last has text: an anchor now is the next one's) */

/* (phase F) The reader view: the page's main text alone. The page is read twice: first each block's text and
   how much of it is links counted; then made with the blocks that are mostly links (a site's menus, lists of
   other pages) left out, and those before the page's first heading where many paragraphs follow it (a site's
   banner), and the page's own colours. */
static int reader;                      /* (the user's: View, Reader View) */
static struct rstat { unsigned text, link; int kind, picture; } *rs;
static int nrs, rs_room, rbi, skipping;
static unsigned char *rkeep;

static void c_block(void *c, int kind, int depth)
{
    (void)c, (void)depth;
    if (nrs == rs_room) {
        int room = rs_room ? rs_room * 2 : 256;
        struct rstat *n = realloc(rs, (size_t)room * sizeof *rs);
        unsigned char *k = realloc(rkeep, (size_t)room);
        if (!n || !k) {
            if (n)
                rs = n;
            if (k)
                rkeep = k;
            return;
        }
        rs = n, rkeep = k, rs_room = room;
    }
    memset(&rs[nrs], 0, sizeof rs[nrs]);
    rs[nrs++].kind = kind;
}

static void c_text(void *c, const char *text, int n, unsigned style, const char *link)
{
    unsigned k = 0;
    int i;
    (void)c, (void)style;
    for (i = 0; n < 0 ? text[i] != 0 : i < n; i++)      /* (letters, not blanks) */
        k += text[i] != ' ' && text[i] != '\n' && text[i] != '\t';
    if (nrs) {
        rs[nrs - 1].text += k;
        if (link)
            rs[nrs - 1].link += k;
    }
}

static void c_image(void *c, const char *src, const char *alt, int w, int h, const char *link)
{
    (void)c, (void)src, (void)alt, (void)w, (void)h, (void)link;
    if (nrs)
        rs[nrs - 1].picture = 1;
}

static const struct html_sink counting = { c_block, c_text, NULL, NULL, c_image, NULL, NULL, NULL, NULL, NULL };

static int reader_choose(void)          /* rkeep[] for each block: the number kept */
{
    int i, first = -1, after = 0, kept = 0;
    for (i = 0; i < nrs && first < 0; i++)
        if (rs[i].kind == GUI_RB_HEAD1)
            first = i;
    for (i = first + 1; first >= 0 && i < nrs; i++)
        after += rs[i].kind == GUI_RB_PARA && rs[i].text >= 40 && rs[i].link * 2 < rs[i].text;
    if (after < 2)
        first = -1;                             /* (no heading that a text follows: from the start) */
    for (i = 0; i < nrs; i++) {
        const struct rstat *r = &rs[i];
        int keep = 1;
        if (i < first)
            keep = 0;
        else if (!r->text && !r->picture && r->kind != GUI_RB_RULE)
            keep = 0;                           /* (nothing in it) */
        else if (r->kind != GUI_RB_HEAD1 && r->kind != GUI_RB_HEAD2 && r->kind != GUI_RB_CODE && r->link * 2 > r->text
                 && r->text < 400)
            keep = 0;                           /* (mostly links: a menu, a list of other pages) */
        rkeep[i] = (unsigned char)keep;
        kept += keep;
    }
    for (i = nrs - 1; i >= 0; i--)              /* (a heading whose part is all left out goes with it) */
        if (rkeep[i] && (rs[i].kind == GUI_RB_HEAD2 || rs[i].kind == GUI_RB_HEAD3)) {
            int j, any = 0;
            for (j = i + 1; j < nrs && rs[j].kind != GUI_RB_HEAD1 && rs[j].kind != GUI_RB_HEAD2
                            && (rs[j].kind != GUI_RB_HEAD3 || rs[i].kind == GUI_RB_HEAD2); j++)
                any |= rkeep[j];
            if (!any)
                rkeep[i] = 0, kept--;
        }
    return kept;
}

static void d_block(void *c, int kind, int depth)
{
    (void)c;
    if (reader && rs) {
        rbi++;
        skipping = rbi < nrs && !rkeep[rbi];
        if (skipping)
            return;
    }
    gui_rdoc_block(doc, kind, depth);   /* (the reader's kinds are the toolkit's) */
    blocks++, text_in_block = 0;
}

static void d_text(void *c, const char *text, int n, unsigned style, const char *link)
{
    (void)c;
    if (skipping)
        return;
    text_in_block |= n != 0;
    gui_rdoc_text(doc, text, n, style, link);
    links += link != NULL;
}

static void d_cell(void *c) { (void)c; if (!skipping) gui_rdoc_cell(doc); }
static void d_colour(void *c, long rgb) { (void)c; if (!reader) gui_rdoc_colour(doc, rgb < 0 ? GFX_NONE : (unsigned)rgb); }

/* ---- pictures in pages: a page's pictures (24 at most), each fetched once and kept while the page is: a file's
   at once, as the page is made; a web server's one after another once the page shows (the page is then made
   again with them). Each is made no wider than the page (560 pixels) and no taller than 700; 3 million pixels
   in all at most. One not had is shown by its words, as before. */

#define PICS 24
#define PIC_W 560
#define PIC_H 700
#define PIC_BYTES (1ul << 20)
#define PIC_PIXELS 3000000ul

static struct { char url[400]; unsigned *px; int w, h, state; } pics[PICS];     /* state: 0 wanted, 1 here, -1 not had */
static int npics;
static unsigned long pic_pixels;
static struct webget *pic_get;          /* the one on its way */
static int pic_at = -1;
static char *page_bytes, page_type[128], page_url[1024];       /* the page shown, kept to be made again with them */
static unsigned long page_n;
static int remaking;                    /* (the page made again: not a new place, and its pictures kept) */
static int pics_came;                   /* web pictures that came since the page was last made */

static void pics_forget(void)
{
    int i;
    if (pic_get)
        webget_free(pic_get);
    pic_get = NULL, pic_at = -1;
    for (i = 0; i < npics; i++)
        free(pics[i].px);
    npics = 0, pic_pixels = 0, pics_came = 0;
}

/* A picture's bytes decoded and made to fit: 1, or 0 (not had). */
static int pic_take(int k, const unsigned char *bytes, unsigned long n)
{
    char kind[16];
    int w = 0, h = 0, nw, nh, x, y;
    unsigned *px = pic_decode(bytes, n, pics[k].url, &w, &h, kind, sizeof kind, 0xFFFFFF), *small;
    if (!px || w <= 0 || h <= 0) {
        free(px);
        pics[k].state = -1;
        return 0;
    }
    nw = w, nh = h;
    if (nw > PIC_W)
        nh = (int)((long)nh * PIC_W / nw), nw = PIC_W;
    if (nh > PIC_H)
        nw = (int)((long)nw * PIC_H / nh), nh = PIC_H;
    if (nw < 1)
        nw = 1;
    if (nh < 1)
        nh = 1;
    if (pic_pixels + (unsigned long)nw * (unsigned long)nh > PIC_PIXELS) {
        free(px);
        pics[k].state = -1;
        return 0;
    }
    if (nw != w || nh != h) {                   /* (the nearest of the picture's pixels to each of the smaller's) */
        if (!(small = malloc((size_t)nw * (size_t)nh * sizeof *small))) {
            free(px);
            pics[k].state = -1;
            return 0;
        }
        for (y = 0; y < nh; y++)
            for (x = 0; x < nw; x++)
                small[(size_t)y * (size_t)nw + (size_t)x] = px[(size_t)(y * h / nh) * (size_t)w + (size_t)(x * w / nw)];
        free(px);
        px = small;
    }
    pics[k].px = px, pics[k].w = nw, pics[k].h = nh, pics[k].state = 1;
    pic_pixels += (unsigned long)nw * (unsigned long)nh;
    return 1;
}

static void file_path(const char *url, char *out, unsigned room);
static int starts(const char *s, const char *with);

static int pic_find(const char *url)    /* its place in the list (made if new: a file's read now), or -1 */
{
    int i;
    for (i = 0; i < npics; i++)
        if (!strcmp(pics[i].url, url))
            return i;
    if (npics == PICS || strlen(url) >= sizeof pics[0].url)
        return -1;
    memset(&pics[npics], 0, sizeof pics[npics]);
    snprintf(pics[npics].url, sizeof pics[npics].url, "%s", url);
    if (starts(url, "file:")) {
        char path[300];
        FILE *f;
        long size;
        unsigned char *b;
        pics[npics].state = -1;
        file_path(url, path, sizeof path);
        if ((f = gui_fopen(path, "rb")) != NULL) {
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            rewind(f);
            if (size > 0 && (unsigned long)size <= PIC_BYTES && (b = malloc((size_t)size)) != NULL) {
                if (fread(b, 1, (size_t)size, f) == (size_t)size)
                    pic_take(npics, b, (unsigned long)size);
                free(b);
            }
            fclose(f);
        }
    } else if (!starts(url, "http:") && !starts(url, "https:")) {
        pics[npics].state = -1;
    }
    return npics++;
}

static void d_image(void *c, const char *src, const char *alt, int w, int h, const char *link)
{
    char t[300];
    int k;
    (void)c, (void)w, (void)h;
    if (skipping)
        return;
    k = src && src[0] ? pic_find(src) : -1;
    if (k >= 0 && pics[k].state == 1) {         /* the picture itself; a link it is part of under it, by its words */
        gui_rdoc_image(doc, pics[k].px, pics[k].w, pics[k].h, alt);
        blocks++;
        if (!link)
            return;
    }
    /* a picture's place: the words that stand for it, as a link to the picture itself (or to where it leads) */
    gui_rdoc_block(doc, GUI_RB_PARA, 0);
    snprintf(t, sizeof t, "[%s]", alt[0] ? alt : GUI_T("picture"));
    gui_rdoc_colour(doc, GFX_NONE);
    gui_rdoc_text(doc, t, -1, GUI_RS_ITALIC, link ? link : src);
    blocks++, links++;
}

/* (phase F) The places a page's "#name" leads to: each name, and the block it is (the one begun next, or the one
   just begun when no text is in it yet). */
#define ANCHORS 256
static struct { char name[48]; int block; } anchors[ANCHORS];
static int nanchors;

static void d_anchor(void *c, const char *name)
{
    int n = gui_rdoc_blocks(doc);
    (void)c;
    if (nanchors == ANCHORS || strlen(name) >= sizeof anchors[0].name)
        return;
    snprintf(anchors[nanchors].name, sizeof anchors[0].name, "%s", name);
    anchors[nanchors++].block = text_in_block || n == 0 ? n : n - 1;
}

static void d_title(void *c, const char *title)
{
    (void)c;
    if (!page_title[0])
        snprintf(page_title, sizeof page_title, "%s", title);
}

/* (phase F) A page's forms, as it is made: where each is sent, how, and its fields (their names and values as
   the page gives them). A field's word in the page is a link to about:form?N, which opens the form's dialog. */
#define FORMS 16
#define FIELDS 24
static struct wform {
    char action[600], method[8];
    int n;
    int touched;                        /* (2026-11-23) typed in or chosen since the page was made: kept if it is
                                           made again (its pictures come, the reader view) */
    struct {
        char kind[12], name[48], value[120], label[64];
        int on;
        int typed;                      /* typed in the page: its words show what was typed */
        char words[64];                 /* ... and its words as the page had them (shown again if it is emptied) */
    } f[FIELDS];
} forms[FORMS], forms_kept[FORMS];
static int nforms, nforms_kept;

static void d_form(void *c, const char *action, const char *method)
{
    (void)c;
    if (nforms == FORMS) {
        nforms++;                               /* (past the last kept: its fields go nowhere) */
        return;
    }
    if (nforms > FORMS)
        return;
    snprintf(forms[nforms].action, sizeof forms[0].action, "%s", action);
    snprintf(forms[nforms].method, sizeof forms[0].method, "%s", method);
    forms[nforms].touched = 0;
    forms[nforms++].n = 0;
}

static void d_field(void *c, const char *kind, const char *name, const char *value, const char *label, int on)
{
    struct wform *f;
    (void)c;
    if (!nforms || nforms > FORMS || (f = &forms[nforms - 1])->n == FIELDS)
        return;
    snprintf(f->f[f->n].kind, sizeof f->f[0].kind, "%s", kind);
    snprintf(f->f[f->n].name, sizeof f->f[0].name, "%s", name);
    snprintf(f->f[f->n].value, sizeof f->f[0].value, "%s", value);
    snprintf(f->f[f->n].label, sizeof f->f[0].label, "%s", label);
    f->f[f->n].on = on;
    f->f[f->n].typed = 0, f->f[f->n].words[0] = 0;
    f->n++;
}

static const struct html_sink to_doc = { d_block, d_text, d_cell, d_colour, d_image, d_title, d_anchor, d_field, d_form, NULL };

/* The page shown at the place "#name" leads to ("": its top); said for the suite. */
static void show_place(const char *name)
{
    int i;
    if (!name[0]) {
        gui_richview_show(view, 0);
        say("at top");
        return;
    }
    for (i = 0; i < nanchors; i++)
        if (!strcmp(anchors[i].name, name)) {
            if (gui_richview_show(view, anchors[i].block) != 0)
                gui_richview_show(view, anchors[i].block - 1);
            say("at #%s block=%d", name, anchors[i].block);
            return;
        }
    say("no #%s", name);
}

static int same_page(const char *a, const char *b)     /* the same address but for a "#name" */
{
    size_t na = strcspn(a, "#"), nb = strcspn(b, "#");
    return na == nb && !strncmp(a, b, na);
}

static int starts(const char *s, const char *with)      /* whatever the letters' case */
{
    for (; *with; s++, with++)
        if ((*s >= 'A' && *s <= 'Z' ? *s + 32 : *s) != *with)
            return 0;
    return 1;
}

static const char *ends_with(const char *url)   /* a URL's path's ending (".png"), small, or "" */
{
    static char e[8];
    struct url_parts p;
    const char *dot;
    unsigned i;
    e[0] = 0;
    if (url_split(url, &p) != 0 || !(dot = strrchr(p.path, '.')) || strlen(dot) > 6 || strchr(dot, '/'))
        return e;
    for (i = 0; dot[i]; i++)
        e[i] = (char)(dot[i] >= 'A' && dot[i] <= 'Z' ? dot[i] + 32 : dot[i]);
    e[i] = 0;
    return e;
}

/* A field typed in the page: the words it shows (its value; a password's as stars; emptied, the page's own). */
static void field_shown(const struct wform *f, int i, char *out, size_t room)
{
    const char *v = f->f[i].value;
    if (!v[0])
        snprintf(out, room, "%s", f->f[i].words);
    else if (!strcmp(f->f[i].kind, "password"))
        snprintf(out, room, "[%.*s]", (int)(strlen(v) < 12 ? strlen(v) : 12), "************");
    else
        snprintf(out, room, "[%.120s]", v);
}

/* The page made again (its pictures came, the reader view): the forms' fields as they were typed and chosen,
   where the forms are the same (as many fields, of the same kinds and names). */
static void forms_restore(void)
{
    int k, i;
    for (k = 0; k < nforms_kept && k < nforms && k < FORMS; k++) {
        struct wform *f = &forms[k];
        const struct wform *was = &forms_kept[k];
        if (!was->touched || was->n != f->n)
            continue;
        for (i = 0; i < f->n && !strcmp(f->f[i].kind, was->f[i].kind) && !strcmp(f->f[i].name, was->f[i].name); i++)
            ;
        if (i < f->n)
            continue;
        memcpy(f->f, was->f, sizeof f->f);
        f->touched = 1;
        say("form %d kept", k);
        for (i = 0; i < f->n; i++)
            if (f->f[i].typed) {
                char link[32], shown[140];
                snprintf(link, sizeof link, "about:form?%d.%d", k, i);
                field_shown(f, i, shown, sizeof shown);
                gui_richview_set_words_of(view, link, shown);
            }
    }
    nforms_kept = 0;
}

/* What came, shown: by what its sender says it is, or by its name's ending. */
static void show_page(const char *url, const char *bytes, unsigned long n, const char *type, int push)
{
    const char *end = ends_with(url);
    char t[1200];
    int is_text = starts(type, "text/plain") || (!type[0] && (!strcmp(end, ".txt") || !strcmp(end, ".md") || !strcmp(end, ".log")));
    int is_picture = starts(type, "image/") || (!type[0] && (!strcmp(end, ".png") || !strcmp(end, ".jpg") || !strcmp(end, ".jpeg")
                                                             || !strcmp(end, ".gif") || !strcmp(end, ".bmp")));
    unsigned *px = NULL;
    if (!remaking) {                            /* (another page: its pictures, and the page kept to be made again) */
        pics_forget();
        free(page_bytes);
        page_bytes = NULL, page_n = 0;
        if ((page_bytes = malloc(n + 1)) != NULL) {
            memcpy(page_bytes, bytes, n);
            page_bytes[n] = 0, page_n = n;
        }
        snprintf(page_type, sizeof page_type, "%s", type);
        snprintf(page_url, sizeof page_url, "%s", url);
    }
    doc = gui_rdoc_new();
    nforms_kept = 0;                            /* (made again: what was typed and chosen in its forms kept) */
    if (remaking && nforms) {
        nforms_kept = nforms < FORMS ? nforms : FORMS;
        memcpy(forms_kept, forms, (size_t)nforms_kept * sizeof forms[0]);
    }
    page_title[0] = 0, blocks = links = 0, nanchors = 0, text_in_block = 0, nforms = 0;
    if (!doc)
        return;
    if (is_picture) {
        char kind[16];
        int w = 0, h = 0;
        px = pic_decode((const unsigned char *)bytes, n, url, &w, &h, kind, sizeof kind, 0xFFFFFF);
        if (px)
            gui_rdoc_image(doc, px, w, h, NULL), blocks++;
        else
            gui_rdoc_block(doc, GUI_RB_PARA, 0), gui_rdoc_text(doc, GUI_T("This picture could not be shown."), -1, 0, NULL), blocks++;
    } else if (is_text) {
        unsigned long len = n;
        char *latin = NULL;
        const char *p = bytes;
        if (starts(type, "text/plain") && (strstr(type, "8859") || strstr(type, "1252")) && (latin = html_latin_utf8(bytes, n, &len)) != NULL)
            p = latin;
        gui_rdoc_block(doc, GUI_RB_CODE, 0);
        gui_rdoc_text(doc, p, (int)(len > 400000 ? 400000 : len), 0, NULL);
        blocks++;
        free(latin);
    } else if (!type[0] || starts(type, "text/html") || starts(type, "application/xhtml")) {
        int kept = -1;
        if (reader) {                           /* (counted first: what to leave out) */
            nrs = 0;
            html_read(bytes, n, type, url, &counting);
            kept = reader_choose();
            rbi = -1, skipping = 0;
        }
        html_read(bytes, n, type, url, &to_doc);
        skipping = 0;
        if (kept >= 0)
            say("reader kept %d of %d blocks", kept, nrs);
    } else {
        gui_rdoc_block(doc, GUI_RB_PARA, 0);
        snprintf(t, sizeof t, GUI_T("This is no page to read but a file (%s, %lu bytes). Files are not saved from here yet."), type, n);
        gui_rdoc_text(doc, t, -1, 0, NULL);
        blocks++;
    }
    {
        int keep = remaking ? gui_richview_pos(view) : -1;
        gui_richview_set(view, doc);    /* (the view's now; the one before is freed, which pointed at the old picture) */
        forms_restore();
        if (keep > 0)
            gui_richview_set_pos(view, keep);   /* (made again with its pictures: where it was) */
    }
    free(picture);
    picture = px;
    snprintf(current, sizeof current, "%s", url);
    gui_set_text(address, current);
    if (push)                           /* a new place: what was ahead of here is forgotten */
        push_place(url);
    else if (restoring && !remaking && at >= 0 && at < nhistory && hpos[at] >= 0) {
        gui_richview_set_pos(view, hpos[at]);   /* (Back, Forward: where it was left, its #place or not) */
        say("back at %d", hpos[at]);
        restored = 1;
    }
    if (!remaking)
        restoring = 0;
    snprintf(t, sizeof t, "%s%sWeb Reader", page_title, page_title[0] ? " - " : "");
    gui_title(win, t);
    gui_set_text(status, page_title[0] ? page_title : url);
    if (remaking) {
        int i, shown = 0;
        for (i = 0; i < npics; i++)
            shown += pics[i].state == 1;
        say("pictures %d of %d", shown, npics);
        if (strchr(url, '#'))                   /* (the place again: the pictures moved it) */
            show_place(strchr(url, '#') + 1);
        return;
    }
    say("loaded %s title=%s blocks=%u links=%u", url, page_title, blocks, links);
    if (strchr(url, '#') && !restored)
        show_place(strchr(url, '#') + 1);
    restored = 0;
    {
        int i, here = 0;
        for (i = 0; i < npics; i++)
            here += pics[i].state == 1;
        if (here)
            say("pictures %d of %d", here, npics);
    }
}

/* The next picture of the page's from a web server, if one is wanted; when none is, and some came, the page made
   again with them. */
static void pics_next(void)
{
    char certs[300];
    int i;
    if (pic_get)
        return;
    for (i = 0; i < npics; i++)
        if (pics[i].state == 0) {
            gui_path_join(certs, sizeof certs, glos_home(), TLS_CERTS);
            if ((pic_get = webget_start(pics[i].url, certs, PIC_BYTES)) != NULL) {
                pic_at = i;
                return;
            }
            pics[i].state = -1;
        }
    if (pics_came && page_bytes) {
        pics_came = 0;
        remaking = 1;
        show_page(page_url, page_bytes, page_n, page_type, 0);
        remaking = 0;
    }
}

static void show_failure(const char *url, const char *why)
{
    char page[1600], safe[1100];
    unsigned i, n = 0;
    for (i = 0; url[i] && n + 6 < sizeof safe; i++)    /* (the address as text: its own marks are no tags) */
        if (url[i] == '<' || url[i] == '&')
            n += (unsigned)sprintf(safe + n, url[i] == '<' ? "&lt;" : "&amp;");
        else
            safe[n++] = url[i];
    safe[n] = 0;
    snprintf(page, sizeof page, "<title>%s</title><h2>%s</h2><p>%s</p><p>%s: %s.</p>", GUI_T("The page could not be had"),
             GUI_T("The page could not be had"), safe, GUI_T("Why"), why);
    say("failed %s %s", url, why);
    {
        char was[1024];
        snprintf(was, sizeof was, "%s", url);
        show_page(was, page, strlen(page), "text/html; charset=utf-8", 0);
    }
}

/* ---- (phase F) bookmarks: the pages kept, in WEBMARKS.TXT in GLOS's folder (WEB -marks FILE: another), a line
   each: the address, a tab, its title. Shown as a page of links (about:bookmarks), each with a link that takes it
   away (about:unmark?N). */

#define MARKS 200
static char marks_file[300];
static struct { char *url, *title; } marks[MARKS];
static int nmarks, marks_read;
static struct gui_menu *marks_m;        /* the Bookmarks menu: its commands, then the bookmarks (marks_menu) */

static void marks_menu(void)            /* (made again whenever the bookmarks change) */
{
    int i;
    if (!marks_m)
        return;
    gui_menu_clear(marks_m);
    gui_menu_command(marks_m, C_MARK), gui_menu_command(marks_m, C_MARKS), gui_menu_command(marks_m, C_MARK_RENAME);
    if (nmarks)
        gui_menu_sep(marks_m);
    for (i = 0; i < nmarks && i < MENU_MARKS; i++) {
        char t[64];
        if (i < 9)
            snprintf(t, sizeof t, "&%d %.40s%s", i + 1, marks[i].title, strlen(marks[i].title) > 40 ? "..." : "");
        else
            snprintf(t, sizeof t, "%.43s%s", marks[i].title, strlen(marks[i].title) > 43 ? "..." : "");
        gui_menu_item(marks_m, t, C_MARK_GO + i, NULL);
    }
}

static void marks_load(void)
{
    char line[1400];
    FILE *f;
    if (marks_read)
        return;
    marks_read = 1;
    if (!(f = gui_fopen(marks_file, "r")))
        return;
    while (nmarks < MARKS && fgets(line, sizeof line, f)) {
        char *tab = strchr(line, '\t'), *e = line + strcspn(line, "\r\n");
        *e = 0;
        if (!tab || tab == line)
            continue;
        *tab = 0;
        marks[nmarks].url = strdup(line);
        marks[nmarks].title = strdup(tab + 1);
        if (marks[nmarks].url && marks[nmarks].title)
            nmarks++;
    }
    fclose(f);
}

static int marks_save(void)
{
    FILE *f = gui_fopen(marks_file, "w");
    int i;
    if (!f)
        return -1;
    for (i = 0; i < nmarks; i++)
        fprintf(f, "%s\t%s\n", marks[i].url, marks[i].title);
    return fclose(f) == 0 ? 0 : -1;
}

static void mark_page(void)             /* the page shown kept (once) */
{
    char t[300], title[240];
    int i;
    marks_load();
    if (!current[0] || !strncmp(current, "about:", 6))
        return;
    for (i = 0; i < nmarks; i++)
        if (!strcmp(marks[i].url, current)) {
            gui_set_text(status, GUI_T("This page is among the bookmarks already."));
            say("marked already %s", current);
            return;
        }
    if (nmarks == MARKS) {
        gui_set_text(status, GUI_T("There are as many bookmarks as can be kept: take some away first."));
        return;
    }
    for (i = 0; page_title[i] && i < (int)sizeof title - 1; i++)       /* (a tab or a line's end in it: a blank) */
        title[i] = page_title[i] == '\t' || page_title[i] == '\r' || page_title[i] == '\n' ? ' ' : page_title[i];
    title[i] = 0;
    marks[nmarks].url = strdup(current);
    marks[nmarks].title = strdup(title[0] ? title : current);
    if (!marks[nmarks].url || !marks[nmarks].title)
        return;
    nmarks++;
    if (marks_save() != 0) {
        gui_set_text(status, GUI_T("The bookmarks couldn't be written."));
        say("marks not written %s", marks_file);
        return;
    }
    marks_menu();
    snprintf(t, sizeof t, GUI_T("Kept among the bookmarks: %.200s"), marks[nmarks - 1].title);
    gui_set_text(status, t);
    say("marked %s", current);
}

static void html_put(char **b, size_t *n, size_t *room, const char *s, int escape)     /* text onto a page made here */
{
    for (; *s; s++) {
        const char *e = !escape ? NULL : *s == '<' ? "&lt;" : *s == '&' ? "&amp;" : *s == '"' ? "&quot;" : NULL;
        size_t k = e ? strlen(e) : 1;
        if (*n + k + 1 > *room) {
            char *nb = realloc(*b, *room * 2 + k + 1);
            if (!nb)
                return;
            *b = nb, *room = *room * 2 + k + 1;
        }
        memcpy(*b + *n, e ? e : s, k);
        *n += k;
    }
    (*b)[*n] = 0;
}

static void show_page(const char *url, const char *bytes, unsigned long n, const char *type, int push);

static void show_marks(int push)        /* about:bookmarks */
{
    size_t n = 0, room = 1024;
    char *b = malloc(room), num[48];
    int i;
    if (!b)
        return;
    b[0] = 0;
    marks_load();
    html_put(&b, &n, &room, "<title>", 0);
    html_put(&b, &n, &room, GUI_T("Bookmarks"), 1);
    html_put(&b, &n, &room, "</title><h1>", 0);
    html_put(&b, &n, &room, GUI_T("Bookmarks"), 1);
    html_put(&b, &n, &room, "</h1>", 0);
    if (!nmarks) {
        html_put(&b, &n, &room, "<p>", 0);
        html_put(&b, &n, &room, GUI_T("None yet: Ctrl+D keeps the page shown here."), 1);
        html_put(&b, &n, &room, "</p>", 0);
    }
    if (nmarks)
        html_put(&b, &n, &room, "<ul>", 0);
    for (i = 0; i < nmarks; i++) {
        html_put(&b, &n, &room, "<li><a href=\"", 0);
        html_put(&b, &n, &room, marks[i].url, 1);
        html_put(&b, &n, &room, "\">", 0);
        html_put(&b, &n, &room, marks[i].title, 1);
        snprintf(num, sizeof num, "</a> (<a href=\"about:unmark?%d\">", i);
        html_put(&b, &n, &room, num, 0);
        html_put(&b, &n, &room, GUI_T("take away"), 1);
        html_put(&b, &n, &room, "</a>)", 0);
    }
    if (nmarks)
        html_put(&b, &n, &room, "</ul>", 0);
    show_page("about:bookmarks", b, n, "text/html; charset=utf-8", push);
    free(b);
}

static void unmark(int k)
{
    if (k < 0 || k >= nmarks)
        return;
    say("unmarked %s", marks[k].url);
    free(marks[k].url), free(marks[k].title);
    memmove(&marks[k], &marks[k + 1], (size_t)(nmarks - k - 1) * sizeof marks[0]);
    nmarks--;
    if (marks_save() != 0)
        say("marks not written %s", marks_file);
    marks_menu();
}

static int marked(const char *url)      /* the bookmark of that page: its number, or -1 */
{
    int i;
    marks_load();
    for (i = 0; i < nmarks; i++)
        if (!strcmp(marks[i].url, url))
            return i;
    return -1;
}

static void rename_mark(void)           /* the page shown's bookmark given another name */
{
    char t[200];
    int k = marked(current);
    char *now;
    if (k < 0) {
        gui_set_text(status, GUI_T("This page is not among the bookmarks."));
        say("rename no mark %s", current);
        return;
    }
    snprintf(t, sizeof t, "%s", marks[k].title);
    if (!gui_ask(win, GUI_T("Rename Bookmark"), GUI_T("The bookmark's name:"), t, sizeof t) || !t[0] || !(now = strdup(t)))
        return;
    free(marks[k].title);
    marks[k].title = now;
    for (now = marks[k].title; *now; now++)     /* (a tab would end the line's address in the file) */
        if (*now == '\t')
            *now = ' ';
    if (marks_save() != 0)
        say("marks not written %s", marks_file);
    marks_menu();
    say("renamed bookmark %s %s", marks[k].url, marks[k].title);
}

/* ---- going somewhere */

static void stop(void)
{
    if (getting) {
        webget_free(getting);
        getting = NULL;
        gui_set_text(status, GUI_T("Stopped."));
        say("stopped");
    }
}

static void file_path(const char *url, char *out, unsigned room)       /* file:///C:/WEB/A.HTM -> C:\WEB\A.HTM */
{
    struct url_parts p;
    unsigned i, n = 0;
    out[0] = 0;
    if (url_split(url, &p) != 0)
        return;
    for (i = p.path[0] == '/' && p.path[2] == ':' ? 1 : 0; p.path[i] && n + 1 < room; i++) {
        int c = p.path[i];
        if (c == '%' && p.path[i + 1] && p.path[i + 2]) {       /* an escape: the byte it stands for */
            char hex[3] = { p.path[i + 1], p.path[i + 2], 0 };
            c = (int)strtol(hex, NULL, 16);
            i += 2;
        }
        out[n++] = (char)(c == '/' ? '\\' : c);
    }
    out[n] = 0;
}

/* ---- a form filled in: its dialog, then sent. A box for each field one types in, a check box for each of its
   check boxes, a choice for each group of radio buttons (by name) and each list; the page's values and choices
   in them. Sent by GET (the pairs the query of the form's address) or POST (the pairs the body): the hidden
   fields as the page gave them, the first button's, what was typed and chosen, a check box's value if it is
   checked. */

static int typed_kind(const char *k)
{
    return !strcmp(k, "text") || !strcmp(k, "search") || !strcmp(k, "input") || !strcmp(k, "email") || !strcmp(k, "url")
        || !strcmp(k, "tel") || !strcmp(k, "number") || !strcmp(k, "password") || !strcmp(k, "textarea");
}

#define CONTROLS 12
enum { W_TEXT, W_CHECK, W_GROUP };
static struct control {
    int kind;                           /* W_ */
    struct gui_widget *w;
    int field;                          /* the field (W_GROUP: its first: a radio button, or the list's "select") */
    int n, idx[16];                     /* (W_GROUP) its choices' fields */
    char got[120];                      /* -> what was typed (W_TEXT) */
    int got_on, got_choice;             /* -> checked (W_CHECK), the choice (W_GROUP) */
} ctl[CONTROLS];
static int nctl;

static void form_done(struct gui_widget *b, int what)
{
    int i;
    (void)what;
    if (b->id == 1) {
        for (i = 0; i < nctl; i++) {
            if (ctl[i].kind == W_TEXT)
                snprintf(ctl[i].got, sizeof ctl[i].got, "%s", gui_get_text(ctl[i].w));
            else if (ctl[i].kind == W_CHECK)
                ctl[i].got_on = gui_checked(ctl[i].w);
            else
                ctl[i].got_choice = gui_value(ctl[i].w);
        }
    }
    gui_end_modal(b->win, b->id == 1 ? GUI_OK : GUI_CANCEL_R);
}

static void go(const char *url, int push);
static void go_post(const char *url, const char *body);

static int group_of(const struct wform *f, const char *kind, const char *name)    /* a group's control, or -1 */
{
    int i;
    for (i = 0; i < nctl; i++)
        if (ctl[i].kind == W_GROUP && !strcmp(f->f[ctl[i].field].kind, kind) && !strcmp(f->f[ctl[i].field].name, name))
            return i;
    return -1;
}

static void add_pair(char *pairs, size_t room, const char *name, const char *value)
{
    char esc[400];
    if (strlen(pairs) + 3 * strlen(name) + 3 * strlen(value) + 3 >= room)
        return;
    url_escape(name, esc, sizeof esc);
    strcat(pairs, pairs[0] ? "&" : "");
    strcat(pairs, esc);
    strcat(pairs, "=");
    url_escape(value, esc, sizeof esc);
    strcat(pairs, esc);
}

static void form_send(int k);

static void form_fill(int k)
{
    struct wform *f;
    struct gui_window *dlg;
    struct gui_widget *box, *form, *row;
    char label[120];
    int i, c, last_select = -1;
    if (k < 0 || k >= nforms || k >= FORMS) {
        say("form none %d", k);
        return;
    }
    f = &forms[k];
    if (strcmp(f->method, "get") && strcmp(f->method, "post")) {
        say("form %d %s refused", k, f->method);
        gui_message(win, GUI_T("Form Not Sent"), GUI_T("This form is sent in a way the Web Reader does not know."), GUI_MB_OK);
        return;
    }
    nctl = 0;                                   /* its controls, from its fields */
    for (i = 0; i < f->n; i++) {
        const char *kind = f->f[i].kind;
        struct control *x = &ctl[nctl];
        if (!strcmp(kind, "option")) {          /* (a choice of the list before it) */
            if (last_select >= 0 && ctl[last_select].n < 16)
                ctl[last_select].idx[ctl[last_select].n++] = i;
            continue;
        }
        if (!strcmp(kind, "radio") && (c = group_of(f, "radio", f->f[i].name)) >= 0) {
            if (ctl[c].n < 16)
                ctl[c].idx[ctl[c].n++] = i;
            continue;
        }
        if (nctl == CONTROLS || (!typed_kind(kind) && strcmp(kind, "checkbox") && strcmp(kind, "radio")
                                  && strcmp(kind, "select")))
            continue;
        memset(x, 0, sizeof *x);
        x->field = i;
        x->kind = typed_kind(kind) ? W_TEXT : !strcmp(kind, "checkbox") ? W_CHECK : W_GROUP;
        if (!strcmp(kind, "radio"))
            x->idx[x->n++] = i;
        if (!strcmp(kind, "select"))
            last_select = nctl;
        nctl++;
    }
    dlg = gui_window(GUI_T("Fill In the Form"), 420, 0, GUI_W_DIALOG);
    dlg->owner = win;
    box = gui_vbox(dlg->root, 6, 8);
    form = gui_form(box);
    for (c = 0; c < nctl; c++) {
        struct control *x = &ctl[c];
        const char *name = f->f[x->field].name;
        if (x->kind == W_CHECK) {               /* (its name, and its value where it has one of its own) */
            snprintf(label, sizeof label, strcmp(f->f[x->field].value, "on") ? "%.40s: %.60s" : "%.40s", name[0] ? name : "?",
                     f->f[x->field].value);
            x->w = gui_check(box, label, f->f[x->field].on);
            continue;
        }
        snprintf(label, sizeof label, "%.40s:", name[0] ? name : f->f[x->field].kind);
        if (x->kind == W_TEXT) {
            x->w = gui_field(gui_row(form, label, NULL), 30);
            gui_set_text(x->w, f->f[x->field].value);
            gui_field_mask(x->w, !strcmp(f->f[x->field].kind, "password"));     /* (a password: as stars) */
        } else {
            int j, chosen = 0;
            x->w = gui_choice(gui_row(form, label, NULL), 24);
            for (j = 0; j < x->n; j++) {
                const char *t = f->f[x->idx[j]].label[0] ? f->f[x->idx[j]].label : f->f[x->idx[j]].value;
                gui_choice_add(x->w, t[0] ? t : "?");
                if (f->f[x->idx[j]].on)
                    chosen = j;
            }
            gui_set_value(x->w, chosen);
        }
    }
    if (!nctl)
        gui_label(box, GUI_T("The form has nothing to fill in: Send sends it as it is."));
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("Send"), form_done, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), form_done, 2)->flags |= GUI_CANCEL;
    for (c = 0; c < nctl; c++)
        if (ctl[c].kind == W_TEXT)
            break;
    if (c < nctl)
        gui_focus(ctl[c].w);
    say("form %d fields=%d", k, nctl);
    if (gui_modal(dlg) != GUI_OK) {
        say("form left");
        return;
    }
    f->touched = 1;
    for (c = 0; c < nctl; c++) {                /* what was typed and chosen, the form's (the page's words show it) */
        struct control *x = &ctl[c];
        if (x->kind == W_TEXT) {
            snprintf(f->f[x->field].value, sizeof f->f[0].value, "%s", x->got);
        } else if (x->kind == W_CHECK) {
            f->f[x->field].on = x->got_on;
        } else {
            for (i = 0; i < x->n; i++)
                f->f[x->idx[i]].on = i == x->got_choice;
        }
    }
    form_send(k);
}

/* A form sent as it is now (its fields' values, as the page gave them and as typed and chosen since): by GET
   (the pairs the query of its address) or POST (the pairs the body). The hidden fields, the first button's,
   what is typed, a checked box's or radio button's value, a list's choice (its first if none is). */
static void form_send(int k)
{
    struct wform *f = &forms[k];
    char url[1600], pairs[900], said[900];     /* (said: what is said of it, a password's value as stars) */
    int i, j, sent_button = 0, secret = 0;
    size_t at;
    if (strcmp(f->method, "get") && strcmp(f->method, "post")) {
        say("form %d %s refused", k, f->method);
        gui_message(win, GUI_T("Form Not Sent"), GUI_T("This form is sent in a way the Web Reader does not know."), GUI_MB_OK);
        return;
    }
    pairs[0] = 0;                               /* what is sent, in the form's order */
    for (i = 0; i < f->n; i++) {
        const char *kind = f->f[i].kind, *name = f->f[i].name;
        if (!name[0])
            continue;
        if (!strcmp(kind, "hidden") || typed_kind(kind)) {
            add_pair(pairs, sizeof pairs, name, f->f[i].value);
            secret |= !strcmp(kind, "password") && f->f[i].value[0];
        } else if (!strcmp(kind, "submit") || !strcmp(kind, "image")) {
            if (!sent_button++)                 /* (the first button is the one pressed) */
                add_pair(pairs, sizeof pairs, name, f->f[i].value);
        } else if ((!strcmp(kind, "checkbox") || !strcmp(kind, "radio")) && f->f[i].on) {
            add_pair(pairs, sizeof pairs, name, f->f[i].value);
        } else if (!strcmp(kind, "select")) {
            int pick = -1, first = -1;
            for (j = i + 1; j < f->n && !strcmp(f->f[j].kind, "option"); j++) {
                if (first < 0)
                    first = j;
                if (f->f[j].on && pick < 0)
                    pick = j;
            }
            if ((pick = pick >= 0 ? pick : first) >= 0)
                add_pair(pairs, sizeof pairs, name, f->f[pick].value);
        }
    }
    snprintf(said, sizeof said, "%s", secret ? "(a password in it)" : pairs);
    if (!strcmp(f->method, "post")) {           /* POST: the pairs are the body, sent to the form's address */
        say("form %d sent post %s body %s", k, f->action, said);
        go_post(f->action, pairs);
        return;
    }
    at = strcspn(f->action, "?#");              /* GET: they are the query, in place of the address's */
    snprintf(url, sizeof url, "%.*s?%s", (int)(at < 600 ? at : 600), f->action, pairs);
    if (secret)
        say("form %d sent %.*s?(a password in it)", k, (int)(at < 600 ? at : 600), f->action);
    else
        say("form %d sent %s", k, url);
    go(url, 1);
}

/* ---- (2026-11-23) A field typed in the page itself: a box over its words (about:form?N.F, a field one types in).
   Enter keeps what is typed (the page's words show it) and, in a form with no other field to type in, sends the
   form; Tab, or a press elsewhere, keeps it; Esc leaves it as it was. A button's words send the form as it is;
   the other fields' (a list, a box, a radio button) open the form's dialog, which has what was typed. */
static struct {
    struct gui_window *pop;
    struct gui_widget *box;
    int form, idx;
} inp;

static int typed_fields(const struct wform *f)
{
    int i, n = 0;
    for (i = 0; i < f->n; i++)
        n += typed_kind(f->f[i].kind);
    return n;
}

static void inpage_end(int keep, int send, int refocus)
{
    struct gui_window *p = inp.pop;
    int k = inp.form;
    if (!p)
        return;
    inp.pop = NULL;
    if (keep && k >= 0 && k < nforms && k < FORMS && inp.idx < forms[k].n) {
        struct wform *f = &forms[k];
        char shown[140];
        snprintf(f->f[inp.idx].value, sizeof f->f[0].value, "%s", gui_get_text(inp.box));
        f->f[inp.idx].typed = 1, f->touched = 1;
        field_shown(f, inp.idx, shown, sizeof shown);
        gui_richview_set_link_words(view, shown);
        if (!strcmp(f->f[inp.idx].kind, "password"))
            say("field %d.%d typed (a password)", k, inp.idx);
        else
            say("field %d.%d typed %s", k, inp.idx, f->f[inp.idx].value);
    } else {
        say("field %d.%d left", k, inp.idx);
        send = 0;
    }
    gui_close_later(p);
    if (refocus) {                              /* (a key ended it: the page has the keyboard again) */
        if (win->id)
            glos_focus(win->id);
        gui_focus(view);
    }
    if (send && typed_fields(&forms[k]) == 1)
        form_send(k);
}

static void inpage_popdown(struct gui_window *w)
{
    (void)w;
    inpage_end(1, 0, 0);
}

static int inpage_keys(struct gui_widget *w, struct gui_ev *e)
{
    (void)w;
    if (e->type != GUI_EV_KEY || e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
        return 0;
    if (e->key == GUI_K_ENTER)
        inpage_end(1, 1, 1);
    else if (e->key == GUI_K_ESC)
        inpage_end(0, 0, 1);
    else if (e->key == GUI_K_TAB)
        inpage_end(1, 0, 1);
    else
        return 0;
    return 1;
}

static void inpage_open(int k, int i)
{
    struct gfx_rect r;
    int ox, oy, w, mh, right;
    inpage_end(1, 0, 0);
    if (!gui_richview_link_place(view, &r) || gui_origin(win, &ox, &oy) != 0) {
        form_fill(k);                           /* (its words not shown: the dialog) */
        return;
    }
    if (!forms[k].f[i].typed)                   /* (the page's own words, before anything was typed) */
        gui_richview_link_words(view, forms[k].f[i].words, (int)sizeof forms[k].f[i].words);
    right = view->r.x + view->r.w - GUI_SB;
    w = r.w < 200 ? 200 : r.w;
    if (w > right - view->r.x)
        w = right - view->r.x;
    if (r.x + w > right)
        r.x = right - w;
    inp.pop = gui_window_at("", ox + r.x, oy + r.y, w, r.h + 6, GUI_W_POPUP | GUI_W_BARE);
    inp.pop->owner = win;
    inp.pop->on_popdown = inpage_popdown;
    inp.box = gui_field(inp.pop->root, 20);
    gui_grow(inp.box, 1);
    gui_set_text(inp.box, forms[k].f[i].value);
    gui_field_mask(inp.box, !strcmp(forms[k].f[i].kind, "password"));     /* (a password: as stars) */
    gui_keys(inp.box, inpage_keys);
    mh = gui_font->height + 6;                  /* (as tall as a field, over the words' line) */
    gui_resize(inp.pop, w, mh);
    inp.pop->gw.x = ox + r.x, inp.pop->gw.y = oy + r.y + (r.h - mh) / 2;
    inp.form = k, inp.idx = i;
    inp.pop->focus = inp.box;
    gui_show(inp.pop, 1);
    say("field %d.%d typing", k, i);
}

/* A form's field's words activated (about:form?N.F): typed in the page, sent, or the form's dialog. */
static void form_field(int k, int i)
{
    const char *kind;
    if (k < 0 || k >= nforms || k >= FORMS || i < 0 || i >= forms[k].n) {
        form_fill(k);
        return;
    }
    kind = forms[k].f[i].kind;
    if (typed_kind(kind))
        inpage_open(k, i);
    else if (!strcmp(kind, "submit") || !strcmp(kind, "image"))
        form_send(k);
    else
        form_fill(k);
}

static int reloading;                   /* (Reload: the page read again, though it is the one shown) */

static void push_place(const char *url)        /* a page or a place in it: kept in the history */
{
    while (nhistory > at + 1)
        free(history[--nhistory]);
    if (nhistory == HISTORY) {
        free(history[0]);
        memmove(history, history + 1, (HISTORY - 1) * sizeof history[0]);
        memmove(hpos, hpos + 1, (HISTORY - 1) * sizeof hpos[0]);
        nhistory--;
    }
    if ((history[nhistory] = malloc(strlen(url) + 1)) != NULL)
        strcpy(history[nhistory], url), hpos[nhistory] = -1, at = nhistory++;
}

static void keep_pos(void)              /* the page shown left: how far it was scrolled, kept with it */
{
    if (at >= 0 && at < nhistory && current[0] && !strcmp(history[at], current))
        hpos[at] = gui_richview_pos(view);
}

static void go(const char *url, int push)
{
    char certs[300], path[300], t[1100];
    if (push) {                                 /* (somewhere new: this one's place kept; nothing to restore) */
        keep_pos();
        restoring = 0;
    }
    if (!reloading && current[0] && !getting && same_page(url, current) && (strchr(url, '#') || strchr(current, '#'))) {
        const char *h = strchr(url, '#');       /* (another place in the page shown: scrolled to) */
        char was[1024];
        snprintf(was, sizeof was, "%s", url);
        if (restoring && at >= 0 && at < nhistory && hpos[at] >= 0) {
            gui_richview_set_pos(view, hpos[at]);       /* (Back to the page's own entry: where it was left) */
            say("back at %d", hpos[at]);
        } else {
            show_place(h ? h + 1 : "");
        }
        restoring = 0;
        snprintf(current, sizeof current, "%s", was);
        gui_set_text(address, current);
        if (push)
            push_place(was);
        return;
    }
    stop();
    if (starts(url, "about:bookmarks")) {
        show_marks(push);
    } else if (starts(url, "about:form?")) {    /* (a form's field pressed: typed in the page, or its dialog) */
        const char *dot = strchr(url + 11, '.');
        form_field(atoi(url + 11), dot ? atoi(dot + 1) : -1);
    } else if (starts(url, "about:unmark?")) {
        marks_load();
        unmark(atoi(url + 13));
        show_marks(0);                          /* (the list again, in the history's place of the list) */
    } else if (starts(url, "about:")) {
        show_page("about:home", home_page, sizeof home_page - 1, "text/html; charset=utf-8", push);
    } else if (starts(url, "file:")) {
        FILE *f;
        char *b;
        long size;
        file_path(url, path, sizeof path);
        if (!(f = gui_fopen(path, "rb"))) {
            show_failure(url, "no such file");
            return;
        }
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        rewind(f);
        size = size < 0 ? 0 : (unsigned long)size > PAGE_MAX ? (long)PAGE_MAX : size;
        if ((b = malloc((size_t)size + 1)) != NULL) {
            size = (long)fread(b, 1, (size_t)size, f);
            b[size] = 0;
            show_page(url, b, (unsigned long)size, "", push);
            free(b);
        }
        fclose(f);
    } else if (starts(url, "http:") || starts(url, "https:")) {
        gui_path_join(certs, sizeof certs, glos_home(), TLS_CERTS);
        if (!(getting = webget_start(url, certs, PAGE_MAX))) {
            show_failure(url, "no memory");
            return;
        }
        snprintf(getting_url, sizeof getting_url, "%s", url);
        getting_push = push;
        snprintf(t, sizeof t, GUI_T("Asking for %s..."), url);
        gui_set_text(status, t);
        say("asking %s", url);
    } else {
        snprintf(t, sizeof t, GUI_T("This reader does not open such addresses: %.900s"), url);
        gui_set_text(status, t);
        say("refused %s", url);
    }
}

/* A form sent by POST: to a web server, its body with it (webget_post); a file's address is read as it is. */
static void go_post(const char *url, const char *body)
{
    char certs[300], t[1100];
    if (!starts(url, "http:") && !starts(url, "https:")) {
        go(url, 1);
        return;
    }
    stop();
    keep_pos();
    restoring = 0;
    gui_path_join(certs, sizeof certs, glos_home(), TLS_CERTS);
    if (!(getting = webget_post(url, certs, PAGE_MAX, body, strlen(body)))) {
        show_failure(url, "no memory");
        return;
    }
    snprintf(getting_url, sizeof getting_url, "%s", url);
    getting_push = 1;
    snprintf(t, sizeof t, GUI_T("Sending the form to %.900s..."), url);
    gui_set_text(status, t);
    say("posting %s", url);
}

/* What was typed in the address: a whole address; a DOS path; a site's name; or words to search for. */
static void go_typed(const char *typed)
{
    struct url_parts p;
    char url[1100], esc[700];
    while (*typed == ' ')
        typed++;
    if (!*typed)
        return;
    if (typed[1] == ':' && (typed[2] == '\\' || typed[2] == '/')) {     /* C:\WEB\INDEX.HTM */
        unsigned i;
        snprintf(esc, sizeof esc, "/%s", typed);        /* (a slash first: "C:" is no scheme) */
        for (i = 0; esc[i]; i++)
            if (esc[i] == '\\')
                esc[i] = '/';
        if (url_resolve("file:///", esc, url, sizeof url) != 0)
            return;
    } else if (url_split(typed, &p) == 0 && p.scheme[0] && (p.has_host || !strcmp(p.scheme, "about") || !strcmp(p.scheme, "file"))) {
        if (url_resolve("about:blank", typed, url, sizeof url) != 0)
            return;
    } else if (!strchr(typed, ' ') && (strchr(typed, '.') || strchr(typed, ':') || strchr(typed, '/'))) {      /* a site's name */
        snprintf(esc, sizeof esc, "https://%.600s", typed);
        if (url_resolve("about:blank", esc, url, sizeof url) != 0)
            return;
    } else {
        url_escape(typed, esc, sizeof esc);
        snprintf(url, sizeof url, "https://lite.duckduckgo.com/lite/?q=%s", esc);
    }
    go(url, 1);
}

static void tick(void *arg)             /* a page on its way: how far, and when it is here */
{
    unsigned long got = 0, total = 0, n = 0;
    char t[1200], url[1024];
    int state;
    (void)arg;
    if (pic_get && !getting) {                  /* a picture of the page's on its way */
        state = webget_state(pic_get, &got, &total);
        if (state != 0) {
            const char *b = state > 0 ? webget_bytes(pic_get, &n) : NULL;
            if (!b || !pic_take(pic_at, (const unsigned char *)b, n))
                pics[pic_at].state = -1;
            else
                pics_came++;
            webget_free(pic_get);
            pic_get = NULL;
            pics_next();
        }
        return;
    }
    if (!getting) {
        int i;
        for (i = 0; i < npics && pics[i].state != 0; i++)
            ;
        if (i < npics)                          /* (one is wanted: on its way) */
            pics_next();
        return;
    }
    state = webget_state(getting, &got, &total);
    if (state == 0) {
        if (got) {
            if (total)
                snprintf(t, sizeof t, GUI_T("%lu of %lu KB of %s"), got / 1024, (total + 1023) / 1024, getting_url);
            else
                snprintf(t, sizeof t, GUI_T("%lu KB of %s"), got / 1024, getting_url);
            gui_set_text(status, t);
        }
        return;
    }
    if (state > 0) {
        const char *bytes = webget_bytes(getting, &n);
        snprintf(url, sizeof url, "%s", webget_url(getting));
        snprintf(t, sizeof t, "%s", webget_type(getting));
        {                                       /* (where it was asked for with a "#part", it is shown with it) */
            const char *hash = strchr(getting_url, '#');
            if (hash && !strchr(url, '#') && strlen(url) + strlen(hash) < sizeof url)
                strcat(url, hash);
        }
        show_page(url, bytes, n, t, getting_push);
    } else {
        snprintf(t, sizeof t, "%s", webget_why(getting));
        snprintf(url, sizeof url, "%s", getting_url);
        webget_free(getting);
        getting = NULL;
        show_failure(url, t);
        return;
    }
    webget_free(getting);
    getting = NULL;
}

/* ---- (phase F) find in the page: the rich view's find, from what is chosen, round the end */

static char needle[128];
static void find_ask(void);

static void find_next(int back)
{
    char t[200];
    int r, from = 0, to = 0, block;
    if (!needle[0]) {
        find_ask();
        return;
    }
    r = gui_richview_find(view, needle, 0, back);
    if (r > 0) {
        int all = gui_richview_tint(view, needle, 0);   /* (the others tinted: where else it is) */
        block = gui_richview_chosen(view, &from, &to);
        snprintf(t, sizeof t, all == 1 ? GUI_T("Found once in this page.") : GUI_T("Found %d times in this page."), all);
        gui_set_text(status, t);
        say("found %s block=%d at=%d", needle, block, from);
        say("tinted %d", all);
        return;
    }
    snprintf(t, sizeof t, GUI_T("\"%s\" is not in this page."), needle);
    gui_set_text(status, t);
    say("not found %s", needle);
}

static void find_ask(void)
{
    char what[sizeof needle];
    snprintf(what, sizeof what, "%s", needle);
    if (!gui_ask(win, GUI_T("Find"), GUI_T("Find in the page:"), what, sizeof what) || !what[0])
        return;
    snprintf(needle, sizeof needle, "%s", what);
    find_next(0);
}

/* ---- commands */

static void command(struct gui_window *w, int id)
{
    char p[300] = "";
    (void)w;
    if (id >= C_MARK_GO && id < C_MARK_GO + MENU_MARKS) {      /* a bookmark chosen in the menu */
        if (id - C_MARK_GO < nmarks)
            go(marks[id - C_MARK_GO].url, 1);
        return;
    }
    switch (id) {
    case C_BACK:
        if (at > 0) {
            keep_pos();
            at--;
            say("back");
            restoring = 1;
            go(history[at], 0);
        }
        break;
    case C_FORWARD:
        if (at + 1 < nhistory) {
            keep_pos();
            at++;
            say("forward");
            restoring = 1;
            go(history[at], 0);
        }
        break;
    case C_RELOAD:
        if (current[0]) {
            char was[1024];
            snprintf(was, sizeof was, "%s", current);
            reloading = 1;
            go(was, 0);
            reloading = 0;
        }
        break;
    case C_FIND:
        find_ask();
        break;
    case C_MARK:
        mark_page();
        break;
    case C_MARKS:
        go("about:bookmarks", 1);
        break;
    case C_MARK_RENAME:
        rename_mark();
        break;
    case C_READER:                              /* the page made again, with or without what is not its text */
        reader = !reader;
        say("reader %s", reader ? "on" : "off");
        if (page_bytes && page_url[0]) {
            char was[1024];
            snprintf(was, sizeof was, "%s", current);
            remaking = 1;
            show_page(page_url, page_bytes, page_n, page_type, 0);
            remaking = 0;
            snprintf(current, sizeof current, "%s", was);
            gui_set_text(address, current);
        }
        break;
    case C_FINDNEXT:
    case C_FINDPREV:
        find_next(id == C_FINDPREV);
        break;
    case C_STOP:
        stop();
        break;
    case C_HOME:
        go("about:home", 1);
        break;
    case C_ADDRESS:
        gui_focus(address);
        break;
    case C_OPEN:
        if (gui_file_dialog(win, GUI_T("Open a Page"), 0, p, sizeof p))
            go_typed(p);
        break;
    case C_EXIT:
        gui_quit(0);
        break;
    }
}

static void update(struct gui_window *w)
{
    gui_command_enable(w, C_BACK, at > 0);
    gui_command_enable(w, C_FORWARD, at + 1 < nhistory);
    gui_command_enable(w, C_RELOAD, current[0] != 0);
    gui_command_enable(w, C_STOP, getting != NULL);
    gui_command_enable(w, C_MARK, current[0] && strncmp(current, "about:", 6));
    gui_command_enable(w, C_MARK_RENAME, current[0] && marked(current) >= 0);
    gui_command_check(w, C_READER, reader);
}

static void address_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE) {
        char typed[700];
        snprintf(typed, sizeof typed, "%s", gui_get_text(w));
        gui_focus(view);
        go_typed(typed);
    }
}

static void view_on(struct gui_widget *w, int what)
{
    const char *link = what == GUI_ON_ACTIVATE ? gui_richview_link(w) : NULL;
    if (link && link[0]) {
        char url[1024];
        snprintf(url, sizeof url, "%s", link);  /* (the link's text is the document's, which is about to go) */
        go(url, 1);
    }
}

static void build(void)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *tb, *row;
    win = gui_window(GUI_T("Web Reader"), 620, 440, 0);
    win->on_command = command;
    win->on_update = update;
    gui_command(win, C_BACK, GUI_T("&Back"), "Alt+Left", gui_glyph("back"), GUI_T("The page before"));
    gui_command(win, C_FORWARD, GUI_T("&Forward"), "Alt+Right", gui_glyph("forward"), GUI_T("The page after"));
    gui_command(win, C_RELOAD, GUI_T("&Reload"), "F5", gui_glyph("refresh"), GUI_T("Ask for the page again"));
    gui_command(win, C_STOP, GUI_T("&Stop"), "Esc", gui_glyph("close"), GUI_T("Stop asking"));
    gui_command(win, C_HOME, GUI_T("&Home"), "Alt+Home", gui_glyph("home"), GUI_T("The page it starts with"));
    gui_command(win, C_ADDRESS, GUI_T("&Address"), "Ctrl+L", NULL, NULL);
    gui_command(win, C_OPEN, GUI_T("&Open a File..."), "Ctrl+O", NULL, NULL);
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    gui_command(win, C_FIND, GUI_T("&Find in the Page..."), "Ctrl+F", gui_glyph("find"), GUI_T("Find words in the page"));
    gui_command(win, C_FINDNEXT, GUI_T("Find &Next"), "F3", NULL, NULL);
    gui_command(win, C_FINDPREV, GUI_T("Find &Previous"), "Shift+F3", NULL, NULL);
    gui_command(win, C_MARK, GUI_T("&Keep This Page"), "Ctrl+D", gui_glyph("star"), GUI_T("Keep the page among the bookmarks"));
    gui_command(win, C_MARKS, GUI_T("&Bookmarks"), "Ctrl+B", NULL, GUI_T("The pages kept"));
    gui_command(win, C_MARK_RENAME, GUI_T("&Rename This Page's Bookmark..."), NULL, NULL, NULL);
    gui_command(win, C_READER, GUI_T("&Reader View"), "F9", NULL, GUI_T("The page's text alone: its menus and lists of links left out"));
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_command(m, C_OPEN), gui_menu_command(m, C_ADDRESS);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_command(m, C_FIND), gui_menu_command(m, C_FINDNEXT), gui_menu_command(m, C_FINDPREV);
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_command(m, C_READER);
    m = gui_menu(bar, GUI_T("&Go"));
    gui_menu_command(m, C_BACK), gui_menu_command(m, C_FORWARD), gui_menu_command(m, C_HOME);
    gui_menu_sep(m);
    gui_menu_command(m, C_RELOAD), gui_menu_command(m, C_STOP);
    marks_m = gui_menu(bar, GUI_T("&Bookmarks"));
    marks_load();
    marks_menu();
    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_BACK), gui_toolbar_command(tb, C_FORWARD);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_RELOAD), gui_toolbar_command(tb, C_STOP), gui_toolbar_command(tb, C_HOME);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_MARK);
    row = gui_hbox(win->root, 6, 4);
    gui_label(row, GUI_T("Address:"));
    address = gui_field(row, 60);
    gui_grow(address, 1);
    gui_on(address, address_on, 0);
    view = gui_richview(win->root);
    gui_grow(view, 1);
    gui_on(view, view_on, 0);
    status = gui_status(win);
}

/* ---- WEB -dump URL: a page fetched and read with no window, a line a block (on GLOS: the host has no network) */
static const char *dump_post;          /* (WEB -dump -post BODY: the page asked for by POST, BODY its form's) */

#ifdef __DJGPP__

static struct { char *b; unsigned long n, room; } dumped;
static int dump_open;

static int dump_body(void *arg, const void *data, unsigned n)
{
    (void)arg;
    if (dumped.n + n + 1 > dumped.room) {
        unsigned long room = (dumped.n + n + 1) * 2;
        char *b = realloc(dumped.b, room);
        if (!b)
            return 1;
        dumped.b = b, dumped.room = room;
    }
    memcpy(dumped.b + dumped.n, data, n);
    dumped.n += n;
    return dumped.n > PAGE_MAX;
}

static void p_block(void *c, int kind, int depth)
{
    static const char *const names[] = { "P", "H1", "H2", "H3", "B", "N", "Q", "C", "R", "T" };
    (void)c;
    if (kind >= HTML_HEAD1 && kind <= HTML_HEAD3)
        printf("%sWEB dump %s: ", dump_open ? "\n" : "", names[kind]);
    else
        printf("%sWEB dump %s%d: ", dump_open ? "\n" : "", kind >= 0 && kind < 10 ? names[kind] : "?", depth);
    dump_open = 1;
}

static void p_text(void *c, const char *text, int n, unsigned style, const char *link)
{
    (void)c, (void)style;
    if (link)
        printf("<%s|%.*s>", link, n, text);
    else
        printf("%.*s", n, text);
}

static void p_cell(void *c) { (void)c; printf(" | "); }
static void p_title(void *c, const char *title) { (void)c; printf("%sWEB dump TITLE: %s", dump_open ? "\n" : "", title); dump_open = 1; }

static void p_image(void *c, const char *src, const char *alt, int w, int h, const char *link)
{
    (void)c, (void)w, (void)h, (void)link;
    printf("%sWEB dump IMG: %s (%s)", dump_open ? "\n" : "", src, alt);
    dump_open = 1;
}

static int dump(const char *url, const char *resolve)
{
    static const struct html_sink to_text = { p_block, p_text, p_cell, NULL, p_image, p_title, NULL, NULL, NULL, NULL };
    struct tls_anchors anchors = { 0, 0 };
    struct http_get h;
    char certs[300];
    const char *home = glos_home();
    int have, r;
    snprintf(certs, sizeof certs, "%s%s%s", home, home[0] && home[strlen(home) - 1] != '\\' ? "\\" : "", TLS_CERTS);
    have = tls_anchors_load(&anchors, certs) == 0;
    memset(&h, 0, sizeof h);
    h.url = url, h.anchors = have ? &anchors : NULL, h.resolve = resolve, h.body = dump_body;
    h.agent = "GLOS-Web-Reader/1.0", h.accept = "text/html, text/plain;q=0.9, */*;q=0.5";
    if (dump_post)
        h.post = dump_post, h.post_len = strlen(dump_post);
    r = http_get(&h);
    if (r != 0 && dumped.n <= PAGE_MAX) {
        printf("WEB dump failed: %s\n", h.why);
        return 1;
    }
    printf("WEB dump got %lu bytes type=%s from %s\n", dumped.n, h.type, h.url_got);
    html_read(dumped.b ? dumped.b : "", dumped.n, h.type, h.url_got, &to_text);
    printf("%sWEB dump end\n", dump_open ? "\n" : "");
    free(dumped.b);
    if (have)
        tls_anchors_free(&anchors);
    return 0;
}

#else
static int dump(const char *url, const char *resolve)
{
    (void)url, (void)resolve;
    printf("WEB dump failed: no network here\n");
    return 1;
}
#endif

int main(int argc, char **argv)
{
    const char *first = NULL, *resolve = NULL;
    int i, dumping = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-dump"))
            dumping = 1;
        else if (!strcmp(argv[i], "-resolve") && i + 1 < argc)
            resolve = argv[++i];
        else if (!strcmp(argv[i], "-post") && i + 1 < argc)
            dump_post = argv[++i];
        else if (!strcmp(argv[i], "-marks") && i + 1 < argc)
            snprintf(marks_file, sizeof marks_file, "%s", argv[++i]);
        else if (argv[i][0] != '-')
            first = argv[i];
    }
    if (dumping)
        return first ? dump(first, resolve) : 2;
    if (gui_init(argc, argv) != 0) {
        printf("WEB no GLOS\n");
        return 1;
    }
    if (!marks_file[0])
        gui_path_join(marks_file, sizeof marks_file, glos_home(), "WEBMARKS.TXT");
    build();
    gui_show(win, 1);
    gui_focus(view);
    gui_idle(tick, NULL, 100);
    if (first)
        go_typed(first);
    else
        go("about:home", 1);
    gui_run();
    gui_idle(tick, NULL, 0);
    stop();
    for (i = 0; i < nhistory; i++)
        free(history[i]);
    for (i = 0; i < nmarks; i++)
        free(marks[i].url), free(marks[i].title);
    free(rs), free(rkeep), free(page_bytes);
    free(picture);
    printf("WEB end\n");
    return 0;
}
