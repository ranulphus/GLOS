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

enum { C_BACK = 1, C_FORWARD, C_RELOAD, C_STOP, C_HOME, C_ADDRESS, C_OPEN, C_EXIT };

#define PAGE_MAX (2ul << 20)            /* a page's bytes kept at most */
#define HISTORY 40

static struct gui_window *win;
static struct gui_widget *address, *view, *status;
static char current[1024];              /* the page shown */
static char *history[HISTORY];          /* where one has been; `at` is the page shown */
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
    "<p>Pages are shown as text with their links: no scripts are run, and pictures are shown by the words that stand "
    "for them.</p>"
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

static void d_block(void *c, int kind, int depth)
{
    (void)c;
    gui_rdoc_block(doc, kind, depth);   /* (the reader's kinds are the toolkit's) */
    blocks++;
}

static void d_text(void *c, const char *text, int n, unsigned style, const char *link)
{
    (void)c;
    gui_rdoc_text(doc, text, n, style, link);
    links += link != NULL;
}

static void d_cell(void *c) { (void)c; gui_rdoc_cell(doc); }
static void d_colour(void *c, long rgb) { (void)c; gui_rdoc_colour(doc, rgb < 0 ? GFX_NONE : (unsigned)rgb); }

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
    int k = src && src[0] ? pic_find(src) : -1;
    (void)c, (void)w, (void)h;
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

static void d_title(void *c, const char *title)
{
    (void)c;
    if (!page_title[0])
        snprintf(page_title, sizeof page_title, "%s", title);
}

static const struct html_sink to_doc = { d_block, d_text, d_cell, d_colour, d_image, d_title, NULL, NULL, NULL };

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
    page_title[0] = 0, blocks = links = 0;
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
        html_read(bytes, n, type, url, &to_doc);
    } else {
        gui_rdoc_block(doc, GUI_RB_PARA, 0);
        snprintf(t, sizeof t, GUI_T("This is no page to read but a file (%s, %lu bytes). Files are not saved from here yet."), type, n);
        gui_rdoc_text(doc, t, -1, 0, NULL);
        blocks++;
    }
    gui_richview_set(view, doc);        /* (the view's now; the one before is freed, which pointed at the old picture) */
    free(picture);
    picture = px;
    snprintf(current, sizeof current, "%s", url);
    gui_set_text(address, current);
    if (push) {                         /* a new place: what was ahead of here is forgotten */
        while (nhistory > at + 1)
            free(history[--nhistory]);
        if (nhistory == HISTORY) {
            free(history[0]);
            memmove(history, history + 1, (HISTORY - 1) * sizeof history[0]);
            nhistory--;
        }
        if ((history[nhistory] = malloc(strlen(url) + 1)) != NULL)
            strcpy(history[nhistory], url), at = nhistory++;
    }
    snprintf(t, sizeof t, "%s%sWeb Reader", page_title, page_title[0] ? " - " : "");
    gui_title(win, t);
    gui_set_text(status, page_title[0] ? page_title : url);
    if (remaking) {
        int i, shown = 0;
        for (i = 0; i < npics; i++)
            shown += pics[i].state == 1;
        say("pictures %d of %d", shown, npics);
        return;
    }
    say("loaded %s title=%s blocks=%u links=%u", url, page_title, blocks, links);
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

static void go(const char *url, int push)
{
    char certs[300], path[300], t[1100];
    stop();
    if (starts(url, "about:")) {
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

/* ---- commands */

static void command(struct gui_window *w, int id)
{
    char p[300] = "";
    (void)w;
    switch (id) {
    case C_BACK:
        if (at > 0) {
            at--;
            say("back");
            go(history[at], 0);
        }
        break;
    case C_FORWARD:
        if (at + 1 < nhistory) {
            at++;
            say("forward");
            go(history[at], 0);
        }
        break;
    case C_RELOAD:
        if (current[0])
            go(current, 0);
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
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_command(m, C_OPEN), gui_menu_command(m, C_ADDRESS);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Go"));
    gui_menu_command(m, C_BACK), gui_menu_command(m, C_FORWARD), gui_menu_command(m, C_HOME);
    gui_menu_sep(m);
    gui_menu_command(m, C_RELOAD), gui_menu_command(m, C_STOP);
    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_BACK), gui_toolbar_command(tb, C_FORWARD);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_RELOAD), gui_toolbar_command(tb, C_STOP), gui_toolbar_command(tb, C_HOME);
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
    static const struct html_sink to_text = { p_block, p_text, p_cell, NULL, p_image, p_title, NULL, NULL, NULL };
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
        else if (argv[i][0] != '-')
            first = argv[i];
    }
    if (dumping)
        return first ? dump(first, resolve) : 2;
    if (gui_init(argc, argv) != 0) {
        printf("WEB no GLOS\n");
        return 1;
    }
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
    free(picture);
    printf("WEB end\n");
    return 0;
}
