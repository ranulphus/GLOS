/* NOTEPAD: GLOS's text editor (PRD §10.6; docs/milestones-m9.md step D).
 * A file's bytes as they are, in the toolkit's code editor (gui_code, plain:
 * no margin; toolkit 2, C10; DejaVu Sans Mono; Latin-1 until GLOS has code
 * pages): open, save, find, replace, go to a line, word wrap, the clipboard.
 *
 *   NOTEPAD [FILE...]
 *
 * Tabs (phase E, docs/v1/apps/notepad.md): a tab for each open file, with its name and a * while it has changes
 * not saved. New makes an empty one, Open opens the file in a tab of its own (in the empty one, if that is all
 * there is), Close (Ctrl+W, a tab's close box) asks about its changes and takes it away; Ctrl+Tab goes round.
 * Twelve at most.
 *
 * View: Line Numbers shows the editor's margin (numbers, and fold boxes for what is indented under a line);
 * Highlighting colours a file by its kind, which its name's end says (C, batch, INI and GLOS.CFG, assembly,
 * Pascal, HTML: lexers.c; the kind is in the status bar). Both are every document's.
 *
 * Find and Replace are dialogs with Match case; F3 finds the next. View's Keep Session keeps the open files'
 * names, and View's and Format's choices, in NOTEPAD.CFG beside the program, and NOTEPAD started with no file
 * opens them again (off as a rule: a NOTEPAD started bare is an empty one).
 *
 * A file that doesn't exist yet is a new one by that name, made when it is
 * saved. Changes not saved are asked about before the file or the window
 * goes. "NOTEPAD ..." lines on stdout say what happened (the suite's). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/cp.h>
#include <glos/gui.h>

#include "lexers.h"

enum { C_NEW = 1, C_OPEN, C_SAVE, C_SAVEAS, C_EXIT, C_UNDO, C_CUT, C_COPY, C_PASTE, C_DELETE, C_FIND, C_NEXT,
       C_REPLACE, C_GOTO, C_ALL, C_WRAP, C_ABOUT, C_CLOSE, C_NUMBERS, C_COLOURS, C_SESSION, C_BOOK, C_BOOKNEXT,
       C_BOOKPREV, C_INFILES, C_RESULTS,
       C_ENC = 300,                     /* Format's Encoding: C_ENC UTF-8, C_ENC + i: the i-th code page (enc_pages) */
       C_KIND_AUTO = 200, C_KIND };     /* Highlight As: by the file's name; C_KIND + i: the i-th kind (lexers.c) */

#define DOCS 12

struct doc {
    struct gui_widget *page, *ed;
    char path[260];
    int last_mod;                       /* how its tab is marked now: -1 not yet */
    int kind;                           /* its highlighting chosen by hand (lexer_kind's number); -1: by its name */
    int enc;                            /* how its file's bytes are text: 0 UTF-8, else a DOS code page's number */
    int bom;                            /* (UTF-8) its file began with the byte order mark: written again */
};

static struct gui_window *win;
static struct gui_widget *strip, *status;
static struct gui_menu *format, *view, *kind_menu, *enc_menu;
static struct doc doc[DOCS];
static int numbers, colours = 1, kind_at, enc_at;      /* View's two; the status bar's sections for the file's kind
                                                           and for its encoding */
static int match_case, session;                /* Find's Match case; View's Keep Session */
static char home[200];                         /* the program's folder: NOTEPAD.CFG is there (none known: nothing kept) */
static int ndocs;
static int wrap, last_line = -1, last_col = -1;
static struct doc *last_doc;

static void note(const char *fmt, const char *a)
{
    printf("NOTEPAD ");
    printf(fmt, a);
    printf("\n");
    fflush(stdout);
}

static struct doc *cur(void)            /* the document shown */
{
    struct gui_widget *p = gui_tabs_page(strip, gui_tabs_get(strip));
    int i;
    for (i = 0; i < ndocs; i++)
        if (doc[i].page == p)
            return &doc[i];
    return &doc[0];
}

static const char *doc_name(const struct doc *d, char *name, size_t room)
{
    return d->path[0] ? gui_dos_text(gui_path_name(d->path), name, room) : "Untitled";
}

/* A document's kind: the one chosen for it by hand (View's Highlight As), else its name's end's. */
static gui_lexer doc_lexer(const struct doc *d, const char **name)
{
    const char *n = "Text";
    gui_lexer lex = d->kind >= 0 ? lexer_kind(d->kind, &n) : d->path[0] ? lexer_for(d->path, &n) : NULL;
    if (name)
        *name = n ? n : "Text";
    return lex;
}

/* A document's look: the margin or not, wrapped or not, and its kind's colours. */
static void doc_style(struct doc *d)
{
    gui_code_style(d->ed, (numbers ? 0 : GUI_CODE_PLAIN) | (wrap ? GUI_CODE_WRAP : 0));
    gui_code_lexer(d->ed, colours ? doc_lexer(d, NULL) : NULL, NULL);
    gui_damage(d->ed);
}

/* The window's title, the shown document's; and each tab's, its document's name with a * while it has changes. */
/* (Encodings, below: the code pages GLOS has, and an encoding's name for the status bar.) */
static const int enc_pages[] = { 437, 850, 852, 866, 737 };
#define ENC_PAGES ((int)(sizeof enc_pages / sizeof enc_pages[0]))

static const char *enc_name(int enc, char *out, size_t room)
{
    if (!enc)
        return "UTF-8";
    snprintf(out, room, "CP %d", enc);
    return out;
}

static void title(void)
{
    char t[300], name[128];
    struct doc *d = cur();
    int i;
    snprintf(t, sizeof t, GUI_T("%s%s - Notepad"), gui_code_modified(d->ed) ? "*" : "", doc_name(d, name, sizeof name));
    gui_title(win, t);
    {                                           /* (and its kind, in the status bar) */
        const char *kind = "Text", *name;
        int k;
        doc_lexer(d, &kind);
        gui_status_set(status, kind_at, GUI_T(kind), NULL);
        gui_menu_check(kind_menu, C_KIND_AUTO, d->kind < 0);    /* (Highlight As: the shown document's) */
        for (k = 0; lexer_kind(k, &name), name; k++)
            gui_menu_check(kind_menu, C_KIND + k, d->kind == k);
    }
    {                                           /* (and its encoding) */
        char e[16];
        int k;
        gui_status_set(status, enc_at, enc_name(d->enc, e, sizeof e), NULL);
        gui_menu_check(enc_menu, C_ENC, d->enc == 0);
        for (k = 0; k < ENC_PAGES; k++)
            gui_menu_check(enc_menu, C_ENC + 1 + k, d->enc == enc_pages[k]);
    }
    for (i = 0; i < ndocs; i++) {
        int mod = gui_code_modified(doc[i].ed);
        snprintf(t, sizeof t, "%s%s", mod ? "*" : "", doc_name(&doc[i], name, sizeof name));
        if (strcmp(t, gui_get_text(doc[i].page))) {
            gui_set_text(doc[i].page, t);
            gui_damage(strip);
        }
        doc[i].last_mod = mod;
    }
}

static void where(void *arg)
{
    struct doc *d = cur();
    int line, col, i, stale = d != last_doc;
    char t[64];
    (void)arg;
    gui_code_where(d->ed, &line, &col);
    if (line != last_line || col != last_col || stale) {
        snprintf(t, sizeof t, GUI_T("Ln %d, Col %d"), line, col);
        gui_set_text(status, t);
        last_line = line, last_col = col;
    }
    for (i = 0; i < ndocs; i++)
        if (gui_code_modified(doc[i].ed) != doc[i].last_mod)
            stale = 1;
    if (stale)
        title();
    last_doc = d;
}

/* ---- encodings (phase E; docs/v1/apps/notepad.md): the editor's text is UTF-8, as all the toolkit's is; a
   file's bytes are UTF-8 too, or one of DOS's code pages (the ones GLOS has: include/glos/cp.h). Opened: a file
   that begins with UTF-8's mark, or has bytes above 7Fh that are all well-formed UTF-8, is UTF-8; any other is in
   the code page DOS has now (a file of plain ASCII too: what is typed into it later is written as DOS's programs
   expect it). Format's Encoding says which, and changes it: a file as it is on the disc is read again as the one
   chosen; a changed or new text will be written as it. Saved: UTF-8 as it is; a code page's bytes, and if some
   characters have no place in that page the user is asked whether UTF-8 would do instead. */
static int enc_dos(void)                /* the code page DOS has, if it is one of GLOS's (else 437) */
{
    int page = cp_active(), i;
    for (i = 0; i < ENC_PAGES; i++)
        if (enc_pages[i] == page)
            return page;
    return 437;
}

/* Well-formed UTF-8 (no byte out of place, none longer than it need be, no half of a pair). *high: it has bytes
   above 7Fh. */
static int utf8_ok(const unsigned char *s, size_t n, int *high)
{
    size_t i = 0;
    *high = 0;
    while (i < n) {
        unsigned c = s[i], k, u, j;
        if (c < 0x80) {
            i++;
            continue;
        }
        *high = 1;
        k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC2 ? 1 : 0;      /* (the bytes that follow) */
        if (!k || c > 0xF4 || i + k >= n)
            return 0;
        u = c & (0x3Fu >> k);
        for (j = 1; j <= k; j++) {
            if ((s[i + j] & 0xC0) != 0x80)
                return 0;
            u = u << 6 | (s[i + j] & 0x3F);
        }
        if ((k == 2 && u < 0x800) || (k == 3 && (u < 0x10000 || u > 0x10FFFF)) || (u >= 0xD800 && u <= 0xDFFF))
            return 0;
        i += k + 1;
    }
    return 1;
}

static char *enc_to_utf8(int page, const char *in, size_t n, size_t *out_n)    /* a code page's bytes (malloc'd) */
{
    char *out = malloc(n * 3 + 1);
    size_t i, o = 0;
    for (i = 0; out && i < n; i++) {
        unsigned u = cp_char(page, (unsigned char)in[i]);
        if (u < 0x80)
            out[o++] = (char)u;
        else if (u < 0x800)
            out[o++] = (char)(0xC0 | u >> 6), out[o++] = (char)(0x80 | (u & 0x3F));
        else
            out[o++] = (char)(0xE0 | u >> 12), out[o++] = (char)(0x80 | (u >> 6 & 0x3F)), out[o++] = (char)(0x80 | (u & 0x3F));
    }
    *out_n = o;
    return out;
}

/* UTF-8 as a code page's bytes (malloc'd), '?' for what the page hasn't got: *lost, how many those were. */
static char *enc_from_utf8(int page, const char *in, size_t n, size_t *out_n, int *lost)
{
    char *out = malloc(n + 1), *z = malloc(n + 1);     /* (z: the text with a NUL at its end, for cp_utf8_next) */
    const char *p;
    size_t o = 0;
    *lost = 0;
    if (!out || !z) {
        free(out);
        free(z);
        return NULL;
    }
    memcpy(z, in, n);
    z[n] = 0;
    for (p = z; p < z + n; ) {
        unsigned u = (unsigned char)*p;
        int b;
        if (u < 0x80) {
            out[o++] = *p++;
            continue;
        }
        u = cp_utf8_next(&p);
        b = cp_byte(page, u);
        if (b < 0)
            (*lost)++;
        out[o++] = (char)(b < 0 ? '?' : b);
    }
    free(z);
    *out_n = o;
    return out;
}

/* A document more, empty, in a tab of its own, shown. NULL: there are twelve. */
static struct doc *doc_new(void)
{
    struct doc *d;
    int i;
    if (ndocs == DOCS) {
        gui_message(win, GUI_T("Notepad"), GUI_T("Twelve files are open: close one first."), GUI_MB_OK);
        return NULL;
    }
    d = &doc[ndocs++];
    memset(d, 0, sizeof *d);
    d->last_mod = -1;
    d->kind = -1;
    d->enc = enc_dos();
    d->page = gui_tab(strip, "Untitled");
    d->ed = gui_code(d->page);
    doc_style(d);
    gui_code_tabs(d->ed, 8);
    gui_grow(d->ed, 1);
    for (i = 0; gui_tabs_page(strip, i); i++)
        if (gui_tabs_page(strip, i) == d->page)
            gui_tabs_set(strip, i);
    gui_relayout(win);
    return d;
}

/* A file read into a document. as: how its bytes are text (0 UTF-8, a code page's number), or -1: found out. */
static int load_as(struct doc *d, const char *p, int as)
{
    FILE *f = gui_fopen(p, "rb");
    char *buf = NULL;
    size_t n = 0, room = 0, k, size;
    int high = 0;
    if (!f) {
        strncpy(d->path, p, sizeof d->path - 1);        /* (a new file by that name) */
        gui_code_set(d->ed, NULL, 0);
        doc_style(d);
        note("new %s", p);
        title();
        return 0;
    }
    for (;;) {
        if (n == room) {
            char *nb = realloc(buf, room = room * 2 + 65536);
            if (!nb) {
                fclose(f);
                free(buf);
                gui_message(win, GUI_T("Notepad"), GUI_T("The file is too big for the memory there is."), GUI_MB_OK);
                return -1;
            }
            buf = nb;
        }
        if (!(k = fread(buf + n, 1, room - n, f)))
            break;
        n += k;
    }
    fclose(f);
    size = n;
    d->bom = 0;
    if (as != 0 && as != -1) {                  /* (said: that code page's) */
        d->enc = as;
    } else if (n >= 3 && !memcmp(buf, "\xEF\xBB\xBF", 3)) {
        d->enc = 0, d->bom = 1;
        memmove(buf, buf + 3, n -= 3);
    } else if (as == 0 || (utf8_ok((const unsigned char *)buf, n, &high) && high)) {
        d->enc = 0;
    } else {
        d->enc = enc_dos();
    }
    if (d->enc) {                               /* its bytes as the editor's text */
        size_t m = 0;
        char *u = enc_to_utf8(d->enc, buf, n, &m);
        if (!u) {
            free(buf);
            gui_message(win, GUI_T("Notepad"), GUI_T("The file is too big for the memory there is."), GUI_MB_OK);
            return -1;
        }
        free(buf);
        buf = u, n = m;
    }
    gui_code_set(d->ed, buf, n);                /* (the buffer is the editor's now) */
    strncpy(d->path, p, sizeof d->path - 1);
    d->path[sizeof d->path - 1] = 0;
    doc_style(d);
    title();
    {
        char t[300];
        snprintf(t, sizeof t, "%s %lu", p, (unsigned long)size);
        note("open %s", t);
    }
    return 0;
}

static int load(struct doc *d, const char *p) { return load_as(d, p, -1); }

/* A file opened: in the shown tab if that is an empty, untouched Untitled; in a tab of its own otherwise. */
static void open_file(const char *p)
{
    struct doc *d = cur();
    int i;
    for (i = 0; i < ndocs; i++)                 /* (open already: its tab) */
        if (doc[i].path[0] && !strcasecmp(doc[i].path, p)) {
            int k;
            for (k = 0; gui_tabs_page(strip, k); k++)
                if (gui_tabs_page(strip, k) == doc[i].page)
                    gui_tabs_set(strip, k);
            title();
            return;
        }
    if (d->path[0] || gui_code_modified(d->ed) || gui_code_length(d->ed))
        d = doc_new();
    if (d)
        load(d, p);
}

static int save_to(struct doc *d, const char *p)
{
    size_t n;
    char *buf = gui_code_text(d->ed, &n);
    FILE *f;
    if (!buf)
        return -1;
    if (d->enc) {                               /* as its code page's bytes; what the page hasn't got is asked about */
        size_t m = 0;
        int lost = 0;
        char *b = enc_from_utf8(d->enc, buf, n, &m, &lost);
        if (b && lost) {
            char t[300], name[16];
            int r;
            snprintf(t, sizeof t, GUI_T("Not all of this text can be written in %s: %d of its characters would become \"?\". "
                                        "Save it as UTF-8 instead?"), enc_name(d->enc, name, sizeof name), lost);
            r = gui_message(win, GUI_T("Notepad"), t, GUI_MB_YESNOCANCEL);
            if (r != GUI_YES && r != GUI_NO) {
                free(b);
                free(buf);
                return -1;
            }
            if (r == GUI_YES) {
                free(b);
                b = NULL;
                d->enc = 0, d->bom = 0;
            }
        }
        if (b) {
            free(buf);
            buf = b, n = m;
        } else if (d->enc) {
            free(buf);
            gui_message(win, GUI_T("Notepad"), GUI_T("The file is too big for the memory there is."), GUI_MB_OK);
            return -1;
        }
    }
    if (!(f = gui_fopen(p, "wb")) || (!d->enc && d->bom && fwrite("\xEF\xBB\xBF", 1, 3, f) != 3)
        || fwrite(buf, 1, n, f) != n || fclose(f) != 0) {
        free(buf);
        gui_message(win, GUI_T("Notepad"), GUI_T("The file couldn't be written."), GUI_MB_OK);
        return -1;
    }
    free(buf);
    strncpy(d->path, p, sizeof d->path - 1);
    gui_code_saved(d->ed);
    doc_style(d);                               /* (another name may be another kind) */
    title();
    note("saved %s", p);
    return 0;
}

/* Open's and Save As's: text files first (a name saved without a type gets .TXT), or every file; the one chosen
   kept from one to the next. */
static const char *const filters[] = { GUI_N("Text files|*.TXT;*.MD;*.LOG;*.INI;*.CFG;*.BAT"), GUI_N("All files|*.*"),
    NULL };
static struct gui_file_opts file_opts = { filters, 0, NULL, 0, NULL, 0, NULL, NULL };

static int save_as(struct doc *d)
{
    char p[260];
    snprintf(p, sizeof p, "%s", d->path[0] ? d->path : glos_boot_root());
    if (!gui_file_dialog_ex(win, GUI_T("Save As"), 1, p, sizeof p, &file_opts))
        return -1;
    return save_to(d, p);
}

static int save(struct doc *d) { return d->path[0] ? save_to(d, d->path) : save_as(d); }

/* A document's changes not saved: kept (0) or not wanted after all (-1: cancelled). Its tab is shown to ask. */
static int keep_changes(struct doc *d)
{
    char t[300], name[128];
    int r, i;
    if (!gui_code_modified(d->ed))
        return 0;
    for (i = 0; gui_tabs_page(strip, i); i++)
        if (gui_tabs_page(strip, i) == d->page && gui_tabs_get(strip) != i) {
            gui_tabs_set(strip, i);
            title();
        }
    snprintf(t, sizeof t, GUI_T("Save the changes to %s?"), doc_name(d, name, sizeof name));
    r = gui_message(win, GUI_T("Notepad"), t, GUI_MB_YESNOCANCEL);
    if (r == GUI_YES)
        return save(d);
    return r == GUI_NO ? 0 : -1;
}

/* A tab closed, its changes asked about; the last one's leaves an empty Untitled in its place. */
static void doc_close(struct doc *d)
{
    int i, at = (int)(d - doc);
    if (keep_changes(d) != 0)
        return;
    note("closed %s", d->path[0] ? d->path : "Untitled");
    if (ndocs == 1) {
        d->path[0] = 0;
        gui_code_set(d->ed, NULL, 0);
        gui_code_saved(d->ed);
        doc_style(d);
        title();
        return;
    }
    for (i = 0; gui_tabs_page(strip, i); i++)
        if (gui_tabs_page(strip, i) == d->page) {
            gui_tab_remove(strip, i);
            break;
        }
    memmove(&doc[at], &doc[at + 1], (size_t)(ndocs - at - 1) * sizeof doc[0]);
    ndocs--;
    last_doc = NULL;
    gui_relayout(win);
    title();
}

/* What is kept between runs, when View's Keep Session is on: the choices, and the open files' names in their
   order (a file a line; an Untitled has none). */
static void session_save(void)
{
    char path[240];
    FILE *f;
    int i;
    snprintf(path, sizeof path, "%s\\NOTEPAD.CFG", home);
    if (!home[0] || !(f = gui_fopen(path, "w")))
        return;
    fprintf(f, "session %d\nwrap %d\nnumbers %d\nhighlighting %d\ncase %d\n", session, wrap, numbers, colours, match_case);
    for (i = 0; session && i < ndocs; i++)
        if (doc[i].path[0])
            fprintf(f, "file %s\n", doc[i].path);
    fclose(f);
}

static void open_file(const char *p);

static void session_load(int files)
{
    char path[240], line[300];
    FILE *f;
    snprintf(path, sizeof path, "%s\\NOTEPAD.CFG", home);
    if (!home[0] || !(f = gui_fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "session ", 8))
            session = atoi(line + 8) != 0;
        else if (!strncmp(line, "wrap ", 5))
            wrap = atoi(line + 5) != 0;
        else if (!strncmp(line, "numbers ", 8))
            numbers = atoi(line + 8) != 0;
        else if (!strncmp(line, "highlighting ", 13))
            colours = atoi(line + 13) != 0;
        else if (!strncmp(line, "case ", 5))
            match_case = atoi(line + 5) != 0;
        else if (!strncmp(line, "file ", 5) && files && session && line[5])
            open_file(line + 5);
    }
    fclose(f);
}

/* Find and Replace: the toolkit's own dialog (gui_find), which stays up beside the text: Find next and previous,
   Replace, Replace all; Match case, Whole words, Regular expressions. What is looked for, what is put for it and
   the three choices are kept in `fr`; the finding is the editor's (gui_code_find_ex, by the toolkit's matching). */
static struct gui_find fr;

/* The next one (back: the one before), round the text's end once: chosen, 1; 0 none; -1 a wrong expression. */
static int find_step(int back)
{
    struct gui_widget *ed = cur()->ed;
    unsigned flags = fr.flags | (back ? GUI_FIND_BACK : 0);
    size_t a, b, end = gui_code_length(ed);
    int r = gui_code_find_ex(ed, fr.what, flags, NULL);
    if (r != 0)
        return r;
    gui_code_sel(ed, &a, &b);
    gui_code_select(ed, back ? end : 0, back ? end : 0);
    if ((r = gui_code_find_ex(ed, fr.what, flags, NULL)) != 1)
        gui_code_select(ed, a, b);
    return r;
}

/* What is chosen, if it is a match, replaced: 1. */
static int replace_here(void)
{
    struct gui_widget *ed = cur()->ed;
    struct gui_found m;
    size_t a, b, n = 0;
    char *text, with[512];
    int ok = 0;
    gui_code_sel(ed, &a, &b);
    if (!(text = gui_code_text(ed, &n)))
        return 0;
    if (gui_match(fr.what, fr.flags, text, (int)n, (int)a, &m) == 1 && (size_t)m.start == a && (size_t)m.end == b
        && gui_match_replace(fr.with, fr.flags, &m, text, with, sizeof with) >= 0) {
        gui_code_replace_sel(ed, with);
        ok = 1;
    }
    free(text);
    return ok;
}

static void find_say(int r)
{
    char t[200];
    if (r == 1) {
        if (fr.w)
            gui_find_say(&fr, "");
        return;
    }
    snprintf(t, sizeof t, r < 0 ? GUI_T("That expression is wrong.") : GUI_T("\"%s\" isn't there."), fr.what);
    if (fr.w)
        gui_find_say(&fr, t);
    else
        gui_message(win, GUI_T("Notepad"), t, GUI_MB_OK);
}

static void find_on(struct gui_find *f, int todo)
{
    struct gui_widget *ed = cur()->ed;
    char t[80];
    int n = 0;
    if (match_case != ((f->flags & GUI_FIND_CASE) != 0)) {      /* (kept, with the session's other choices) */
        match_case = (f->flags & GUI_FIND_CASE) != 0;
        session_save();
    }
    if (!f->what[0])
        return;
    switch (todo) {
    case GUI_FIND_NEXT:
    case GUI_FIND_PREV:
        find_say(find_step(todo == GUI_FIND_PREV));
        break;
    case GUI_FIND_REPLACE:                      /* the one chosen, then on to the next */
        replace_here();
        find_say(find_step(0));
        break;
    case GUI_FIND_ALL: {
        struct gui_found m;
        gui_code_select(ed, 0, 0);
        while (n < 100000 && gui_code_find_ex(ed, f->what, f->flags, &m) == 1) {
            size_t a, b;
            if (!replace_here())
                break;
            n++;
            if (m.end == m.start) {             /* (an expression that matched nothing at all: one on, or it
                                                   would match there for ever) */
                gui_code_sel(ed, &a, &b);
                if (b >= gui_code_length(ed))
                    break;
                gui_code_select(ed, b + 1, b + 1);
            }
        }
        snprintf(t, sizeof t, GUI_T("%d replaced."), n);
        gui_find_say(f, t);
        printf("NOTEPAD replaced %d\n", n);
        fflush(stdout);
        break;
    }
    }
}

static void find_open(int replace)
{
    fr.replace = replace;
    fr.on_find = find_on;
    fr.flags = (fr.flags & ~(unsigned)GUI_FIND_CASE) | (match_case ? GUI_FIND_CASE : 0);
    gui_find(win, &fr);
}

/* ---- Find in Files (phase E; docs/v1/apps/notepad.md): text looked for in the files of a folder and of the
   folders under it. The dialog takes the text, the folder (the shown file's at first) and the files' names (DOS's
   wildcards, several with ; between them); the files are read on a worker's thread, and each line that has the
   text is listed as it is found in a pane under the tabs: the file, the line's number, the line. Enter or a double
   click on one opens its file (or shows its tab) at that line. Another search, or the pane's Close, ends the
   one at work. Letters' case apart unless the box says otherwise; a file with a zero byte in its first 512 is no
   text and is left out, as is one of more than INF_BIG bytes. */
#define INF_HITS 1000
#define INF_BIG (4ul << 20)

struct hit {
    char path[260];
    int line, col;
};

static struct {
    struct gui_widget *box, *list, *note;      /* the pane (hidden until the first search) */
    int on;                             /* it shows */
    struct hit *hit;
    int n;
    struct gui_task *task;              /* the search at work (NULL: none) */
    int gen;                            /* which search: an older one's word is nobody's */
    char what[128], dir[260], names[80];
    int match_case;
} inf;

struct isearch {                        /* a search at work */
    int gen, match_case, nwhat;
    char what[128], dir[260], names[80];
    int found, files, more;
    unsigned at;                        /* when the window was last told how far it is */
};

struct inote {                          /* its word to the window's thread */
    int gen, kind;                      /* kind: 0 a line found; 1 how far it is; 2 its end */
    int found, files, more;
    struct hit h;
    char text[120];
};

static int inf_wild(const char *p, const char *s)      /* a name against a pattern of * and ?, case apart */
{
    for (; *p; p++, s++) {
        if (*p == '*') {
            while (p[1] == '*')
                p++;
            if (!p[1])
                return 1;
            for (; *s; s++)
                if (inf_wild(p + 1, s))
                    return 1;
            return 0;
        }
        if (!*s || (*p != '?' && toupper((unsigned char)*p) != toupper((unsigned char)*s)))
            return 0;
    }
    return !*s;
}

static int inf_named(const char *names, const char *name)      /* one of the patterns (; or a space between) */
{
    char one[80];
    const char *p = names;
    while (*p) {
        size_t k = strcspn(p, "; ");
        if (k && k < sizeof one) {
            memcpy(one, p, k);
            one[k] = 0;
            if (inf_wild(one, name))
                return 1;
        }
        p += k + (p[k] ? 1 : 0);
    }
    return 0;
}

static void inf_said(void *data)        /* (the window's thread) */
{
    struct inote *n = data;
    char t[300], num[16];
    if (inf.box && n->gen == inf.gen) {
        if (n->kind == 0 && inf.n < INF_HITS) {
            const char *cells[3];
            const char *rel = n->h.path + strlen(inf.dir);
            while (*rel == '\\')
                rel++;
            snprintf(num, sizeof num, "%d", n->h.line);
            cells[0] = rel, cells[1] = num, cells[2] = n->text;
            inf.hit[inf.n] = n->h;
            gui_list_add(inf.list, cells, NULL, (void *)(size_t)inf.n);
            inf.n++;
        } else if (n->kind == 1) {
            snprintf(t, sizeof t, GUI_T("\"%s\": %d lines so far, %d files read..."), inf.what, n->found, n->files);
            gui_set_text(inf.note, t);
        } else if (n->kind == 2) {
            if (n->more)
                snprintf(t, sizeof t, GUI_T("\"%s\": %d lines and more, in %s"), inf.what, inf.n, inf.dir);
            else
                snprintf(t, sizeof t, GUI_T("\"%s\": %d lines in %d files read, in %s"), inf.what, inf.n, n->files, inf.dir);
            gui_set_text(inf.note, t);
            if (inf.n && gui_list_get(inf.list) < 0)
                gui_list_set(inf.list, 0);
            printf("NOTEPAD found in files %d %s\n", inf.n, inf.what);
            fflush(stdout);
        }
    }
    free(n);
}

static void inf_tell(struct gui_task *t, struct isearch *q, int kind, const char *path, int line, int col,
                     const char *text, int ntext)
{
    struct inote *n = calloc(1, sizeof *n);
    int i, k = 0;
    if (!n)
        return;
    n->gen = q->gen, n->kind = kind, n->found = q->found, n->files = q->files, n->more = q->more;
    if (path)
        snprintf(n->h.path, sizeof n->h.path, "%s", path);
    n->h.line = line, n->h.col = col;
    for (i = 0; text && i < ntext && (text[i] == ' ' || text[i] == '\t'); i++)
        ;                                       /* (the line without what it is indented by) */
    for (; text && i < ntext && k < (int)sizeof n->text - 1; i++)
        n->text[k++] = text[i] == '\t' ? ' ' : text[i];
    n->text[k] = 0;
    q->at = glos_ticks();
    gui_task_post(t, inf_said, n);
}

static void inf_file(struct gui_task *t, struct isearch *q, const char *path, unsigned long size)
{
    FILE *f;
    char *buf;
    size_t n, i, ls = 0;
    int line = 1;
    if (size > INF_BIG || !size || !(buf = malloc(size + 1)))
        return;
    if (!(f = gui_fopen(path, "rb"))) {
        free(buf);
        return;
    }
    n = fread(buf, 1, size, f);
    fclose(f);
    q->files++;
    for (i = 0; i < n && i < 512; i++)
        if (!buf[i]) {                          /* (no text) */
            free(buf);
            return;
        }
    buf[n] = '\n';
    for (i = 0; i <= n && !q->more; i++) {
        if (buf[i] != '\n')
            continue;
        {                                       /* the line ls..i: the text's first place in it */
            size_t len = i - ls, at, where = 0;
            int hit = 0;
            if (len && buf[i - 1] == '\r')
                len--;
            for (at = 0; !hit && at + (size_t)q->nwhat <= len; at++) {
                int k;
                for (k = 0; k < q->nwhat; k++) {
                    unsigned char a = (unsigned char)buf[ls + at + (size_t)k], b = (unsigned char)q->what[k];
                    if (a != b && (q->match_case || toupper(a) != toupper(b)))
                        break;
                }
                if (k == q->nwhat)
                    hit = 1, where = at;
            }
            if (hit) {
                if (q->found == INF_HITS) {
                    q->more = 1;
                } else {
                    q->found++;
                    inf_tell(t, q, 0, path, line, (int)where, buf + ls, (int)len);
                }
            }
        }
        ls = i + 1;
        line++;
    }
    free(buf);
}

static void inf_walk(struct gui_task *t, struct isearch *q, const char *dir, int depth)
{
    struct gui_dirent *e = NULL;
    int n = gui_dir_read(dir, &e), i;
    char p[260];
    for (i = 0; i < n && !q->more && !gui_task_cancelled(t); i++) {
        gui_path_join(p, sizeof p, dir, e[i].name);
        if (e[i].dir) {
            if (depth < 24)
                inf_walk(t, q, p, depth + 1);
        } else if (inf_named(q->names, e[i].name)) {
            inf_file(t, q, p, e[i].size);
        }
        if (glos_ticks() - q->at >= 500)
            inf_tell(t, q, 1, NULL, 0, 0, NULL, 0);
    }
    if (n >= 0)
        free(e);
}

static void inf_work(struct gui_task *t, void *arg)
{
    struct isearch *q = arg;
    q->at = glos_ticks();
    inf_walk(t, q, q->dir, 0);
    if (!gui_task_cancelled(t))
        inf_tell(t, q, 2, NULL, 0, 0, NULL, 0);
}

static void inf_done(struct gui_window *owner, void *arg, int cancelled)
{
    struct isearch *q = arg;
    (void)owner, (void)cancelled;
    if (q->gen == inf.gen)
        inf.task = NULL;                        /* (the toolkit frees it after this) */
    free(q);
}

static void inf_stop(void)              /* the search at work, if any, ends, and has nobody to tell */
{
    if (inf.task)
        gui_task_cancel(inf.task);
    inf.task = NULL;
    inf.gen++;
}

static void inf_show(int on)
{
    inf.on = on;
    gui_visible(inf.box, on);
    gui_menu_check(view, C_RESULTS, on);
    gui_relayout(win);
    if (!on) {
        inf_stop();
        gui_focus(cur()->ed);
    }
}

static void inf_list_on(struct gui_widget *w, int what_on)     /* Enter, a double click: its file at its line */
{
    int r = gui_list_get(w), k;
    if (what_on != GUI_ON_ACTIVATE || r < 0)
        return;
    k = (int)(size_t)gui_list_user(w, r);
    if (k < 0 || k >= inf.n)
        return;
    open_file(inf.hit[k].path);
    gui_code_goto(cur()->ed, inf.hit[k].line, inf.hit[k].col + 1);
    gui_focus(cur()->ed);
    printf("NOTEPAD went to %s %d\n", inf.hit[k].path, inf.hit[k].line);
    fflush(stdout);
}

static void inf_close(struct gui_widget *b, int what_on)
{
    (void)b, (void)what_on;
    inf_show(0);
}

static struct {                         /* the dialog */
    struct gui_window *win;
    struct gui_widget *what, *dir, *names, *mcase;
} infd;

static void infd_button(struct gui_widget *b, int what_on)
{
    (void)what_on;
    if (b->id == 1) {                           /* Find: read while the dialog is there (gui_modal closes it) */
        if (!gui_get_text(infd.what)[0])
            return;
        snprintf(inf.what, sizeof inf.what, "%s", gui_get_text(infd.what));
        snprintf(inf.dir, sizeof inf.dir, "%s", gui_get_text(infd.dir));
        snprintf(inf.names, sizeof inf.names, "%s", gui_get_text(infd.names)[0] ? gui_get_text(infd.names) : "*.*");
        inf.match_case = gui_checked(infd.mcase);
        gui_end_modal(infd.win, 1);
    } else if (b->id == 2) {
        gui_end_modal(infd.win, 0);
    } else if (b->id == 3) {                    /* Browse: the folder chosen in the toolkit's dialog */
        char p[260];
        snprintf(p, sizeof p, "%s", gui_get_text(infd.dir));
        if (gui_folder_dialog(infd.win, GUI_T("Find in Folder"), p, sizeof p))
            gui_set_text(infd.dir, p);
    }
}

static void find_in_files(void)
{
    struct gui_widget *box, *form, *row;
    struct isearch *q;
    char *p;
    size_t n;
    infd.win = gui_window(GUI_T("Find in Files"), 360, 0, GUI_W_DIALOG);
    infd.win->owner = win;
    box = gui_vbox(infd.win->root, 6, 8);
    form = gui_form(box);
    infd.what = gui_field(gui_row(form, GUI_T("Find &what:"), NULL), 30);
    row = gui_row(form, GUI_T("In &folder:"), NULL);
    infd.dir = gui_field(row, 24);
    gui_button(row, GUI_T("&Browse..."), infd_button, 3);
    infd.names = gui_field(gui_row(form, GUI_T("Files &named:"), NULL), 30);
    infd.mcase = gui_check(box, GUI_T("Match &case"), inf.match_case);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("Find"), infd_button, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), infd_button, 2)->flags |= GUI_CANCEL;
    if (!inf.dir[0]) {                          /* the shown file's folder, or the boot drive's root */
        snprintf(inf.dir, sizeof inf.dir, "%s", cur()->path[0] ? cur()->path : glos_boot_root());
        if (cur()->path[0])
            gui_path_up(inf.dir);
    }
    {                                           /* what is chosen in the text, if it is of one line: looked for */
        char *sel = gui_code_selection(cur()->ed);
        if (sel && sel[0] && !strchr(sel, '\n') && strlen(sel) < sizeof inf.what)
            snprintf(inf.what, sizeof inf.what, "%s", sel);
        free(sel);
    }
    gui_set_text(infd.what, inf.what);
    gui_set_text(infd.dir, inf.dir);
    gui_set_text(infd.names, inf.names[0] ? inf.names : "*.*");
    gui_focus(infd.what);
    if (gui_modal(infd.win) != 1)               /* (which closes it) */
        return;
    for (p = inf.dir; *p; p++)                  /* (a folder typed: as DOS has it) */
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 32);
    for (n = strlen(inf.dir); n > 3 && inf.dir[n - 1] == '\\'; n--)
        inf.dir[n - 1] = 0;
    if (!gui_is_dir(inf.dir)) {
        gui_message(win, GUI_T("Find in Files"), GUI_T("There is no such folder."), GUI_MB_OK);
        return;
    }
    inf_stop();
    if (!inf.hit && !(inf.hit = malloc(INF_HITS * sizeof *inf.hit)))
        return;
    if (!(q = calloc(1, sizeof *q)))
        return;
    q->gen = inf.gen, q->match_case = inf.match_case;
    if (gui_text_dos(inf.what, q->what, sizeof q->what) != 0)  /* (as the files have it: DOS's bytes) */
        snprintf(q->what, sizeof q->what, "%s", inf.what);
    q->nwhat = (int)strlen(q->what);
    snprintf(q->dir, sizeof q->dir, "%s", inf.dir);
    snprintf(q->names, sizeof q->names, "%s", inf.names);
    gui_list_clear(inf.list);
    inf.n = 0;
    gui_set_text(inf.note, GUI_T("Looking..."));
    inf_show(1);
    if (!(inf.task = gui_task_start(NULL, NULL, inf_work, q, inf_done, 0))) {
        gui_set_text(inf.note, GUI_T("There are no threads to search with."));
        free(q);
        return;
    }
    gui_focus(inf.list);
}

static void command(struct gui_window *w, int id)
{
    struct doc *d = cur();
    struct gui_widget *ed = d->ed;
    char p[260], t[200];
    int i;
    (void)w;
    if (id >= C_KIND_AUTO && id < C_KIND + 32) {        /* Highlight As: this document's kind, by hand */
        const char *name;
        d->kind = id == C_KIND_AUTO ? -1 : id - C_KIND;
        doc_style(d);
        title();
        doc_lexer(d, &name);
        note("kind %s", name);
        return;
    }
    switch (id) {
    case C_NEW:
        doc_new();
        title();
        break;
    case C_OPEN:
        snprintf(p, sizeof p, "%s", d->path[0] ? d->path : glos_boot_root());
        if (gui_file_dialog_ex(win, GUI_T("Open"), 0, p, sizeof p, &file_opts))
            open_file(p);
        break;
    case C_SAVE: save(d); break;
    case C_SAVEAS: save_as(d); break;
    case C_CLOSE: doc_close(d); break;
    case C_EXIT:
        for (i = 0; i < ndocs; i++)             /* each one's changes; any "cancel" keeps the window */
            if (keep_changes(&doc[i]) != 0)
                return;
        session_save();                         /* (the files open at the end are the session) */
        gui_quit(0);
        return;
    case C_UNDO: gui_code_command(ed, GUI_CMD_UNDO); break;
    case C_CUT: gui_code_command(ed, GUI_CMD_CUT); break;
    case C_COPY: gui_code_command(ed, GUI_CMD_COPY); break;
    case C_PASTE: gui_code_command(ed, GUI_CMD_PASTE); break;
    case C_DELETE: gui_code_command(ed, GUI_CMD_DELETE); break;
    case C_ALL: gui_code_command(ed, GUI_CMD_ALL); break;
    case C_FIND:
        find_open(0);
        break;
    case C_INFILES:
        find_in_files();
        return;
    case C_ENC: case C_ENC + 1: case C_ENC + 2: case C_ENC + 3: case C_ENC + 4: case C_ENC + 5: {
        struct doc *d = cur();                  /* Format's Encoding: the file as it is on the disc read again as
                                                   the one chosen; a changed or new text written as it */
        int enc = id == C_ENC ? 0 : enc_pages[id - C_ENC - 1];
        char e[16];
        if (d->path[0] && !gui_code_modified(d->ed) && gui_code_length(d->ed))
            load_as(d, d->path, enc);
        else
            d->enc = enc, d->bom = 0;
        note("encoding %s", enc_name(d->enc, e, sizeof e));
        title();
        return;
    }
    case C_RESULTS:
        inf_show(!inf.on);
        return;
    case C_NEXT:                                /* (F3: the next one, with nothing to look for yet, asked) */
        if (!fr.what[0])
            find_open(0);
        else
            find_say(find_step(0));
        break;
    case C_REPLACE:
        find_open(1);
        break;
    case C_GOTO:
        t[0] = 0;
        if (gui_ask(win, GUI_T("Go To Line"), GUI_T("Line number:"), t, sizeof t) && atoi(t) > 0)
            gui_code_goto(ed, atoi(t), 1);
        break;
    case C_BOOK: {                              /* the line's bookmark, put or taken away (they are the editor's:
                                                   they move with their lines); the margin shows them */
        int line, col;
        unsigned m;
        gui_code_where(ed, &line, &col);
        m = gui_code_marks(ed, line);
        gui_code_mark(ed, line, m ^ GUI_CODE_BOOK, NULL);
        if (!numbers) {
            numbers = 1;
            for (i = 0; i < ndocs; i++)
                doc_style(&doc[i]);
            gui_menu_check(view, C_NUMBERS, numbers);
            session_save();
        }
        printf("NOTEPAD bookmark %d %s\n", line, m & GUI_CODE_BOOK ? "off" : "on");
        fflush(stdout);
        break;
    }
    case C_BOOKNEXT:
    case C_BOOKPREV: {                          /* to the next line with one (the one before), round the end */
        int line, col, to;
        gui_code_where(ed, &line, &col);
        if ((to = gui_code_mark_next(ed, line, GUI_CODE_BOOK, id == C_BOOKPREV)) > 0) {
            gui_code_goto(ed, to, 1);
            printf("NOTEPAD at bookmark %d\n", to);
            fflush(stdout);
        }
        break;
    }
    case C_WRAP:                                /* (every document's, as View's two are) */
    case C_NUMBERS:
    case C_COLOURS:
        if (id == C_WRAP)
            wrap = !wrap;
        else if (id == C_NUMBERS)
            numbers = !numbers;
        else
            colours = !colours;
        for (i = 0; i < ndocs; i++)
            doc_style(&doc[i]);
        gui_menu_check(format, C_WRAP, wrap);
        gui_menu_check(view, C_NUMBERS, numbers);
        gui_menu_check(view, C_COLOURS, colours);
        session_save();
        note("view %s", id == C_WRAP ? "wrap" : id == C_NUMBERS ? "numbers" : "highlighting");
        break;
    case C_SESSION:
        session = !session;
        gui_menu_check(view, C_SESSION, session);
        session_save();
        note("view %s", session ? "session kept" : "session not kept");
        break;
    case C_ABOUT:
        gui_message(win, GUI_T("About Notepad"), GUI_T("Notepad: GLOS's text editor."), GUI_MB_OK);
        break;
    }
    gui_focus(cur()->ed);
}

static void closing(struct gui_window *w)
{
    command(w, C_EXIT);
}

static void strip_on(struct gui_widget *w, int what_on)
{
    int i;
    if (what_on == GUI_ON_NEW) {
        command(win, C_NEW);
    } else if (what_on == GUI_ON_CLOSE) {
        struct gui_widget *p = gui_tabs_page(w, gui_tabs_closing(w));
        for (i = 0; i < ndocs; i++)
            if (doc[i].page == p) {
                doc_close(&doc[i]);
                break;
            }
        gui_focus(cur()->ed);
    } else if (what_on == GUI_ON_CHANGE) {
        title();
        gui_focus(cur()->ed);
    }
}

int main(int argc, char **argv)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("NOTEPAD no GLOS\n");
        return 1;
    }
    {                                           /* its own folder, as DOS writes it: NOTEPAD.CFG's place */
        char *q;
        snprintf(home, sizeof home, "%s", argv[0]);
        for (q = home; *q; q++)
            *q = *q == '/' ? '\\' : (*q >= 'a' && *q <= 'z') ? (char)(*q - 32) : *q;
        if ((q = strrchr(home, '\\')) != NULL)
            *q = 0;
        else
            home[0] = 0;
    }
    session_load(0);                            /* (the choices first: the window is made by them) */
    win = gui_window(GUI_T("Untitled - Notepad"), 520, 340, 0);
    gui_window_icon(win, "app-notepad");
    win->on_command = command;
    win->on_close = closing;
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_item(m, GUI_T("&New"), C_NEW, GUI_T("Ctrl+N"));
    gui_menu_item(m, GUI_T("&Open..."), C_OPEN, GUI_T("Ctrl+O"));
    gui_menu_item(m, GUI_T("&Save"), C_SAVE, GUI_T("Ctrl+S"));
    gui_menu_item(m, GUI_T("Save &As..."), C_SAVEAS, NULL);
    gui_menu_item(m, GUI_T("&Close"), C_CLOSE, GUI_T("Ctrl+W"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("E&xit"), C_EXIT, NULL);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_item(m, GUI_T("&Undo"), C_UNDO, GUI_T("Ctrl+Z"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Cu&t"), C_CUT, GUI_T("Ctrl+X"));
    gui_menu_item(m, GUI_T("&Copy"), C_COPY, GUI_T("Ctrl+C"));
    gui_menu_item(m, GUI_T("&Paste"), C_PASTE, GUI_T("Ctrl+V"));
    gui_menu_item(m, GUI_T("De&lete"), C_DELETE, GUI_T("Del"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("&Find..."), C_FIND, GUI_T("Ctrl+F"));
    gui_menu_item(m, GUI_T("Find &Next"), C_NEXT, GUI_T("F3"));
    gui_menu_item(m, GUI_T("&Replace..."), C_REPLACE, GUI_T("Ctrl+H"));
    gui_menu_item(m, GUI_T("Find in F&iles..."), C_INFILES, GUI_T("Ctrl+Shift+F"));
    gui_menu_item(m, GUI_T("&Go To..."), C_GOTO, GUI_T("Ctrl+G"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Toggle &Bookmark"), C_BOOK, GUI_T("Ctrl+F2"));
    gui_menu_item(m, GUI_T("&Next Bookmark"), C_BOOKNEXT, GUI_T("F2"));
    gui_menu_item(m, GUI_T("Pre&vious Bookmark"), C_BOOKPREV, GUI_T("Shift+F2"));
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("Select &All"), C_ALL, GUI_T("Ctrl+A"));
    format = m = gui_menu(bar, GUI_T("F&ormat"));
    gui_menu_item(m, GUI_T("&Word Wrap"), C_WRAP, NULL);
    {                                           /* how the shown document's file is written */
        static const char *const names[] = { GUI_N("Code Page &437 (DOS, United States)"), GUI_N("Code Page &850 (Western Europe)"),
                                             GUI_N("Code Page 85&2 (Central Europe)"), GUI_N("Code Page 8&66 (Cyrillic)"),
                                             GUI_N("Code Page &737 (Greek)") };
        int k;
        enc_menu = gui_menu_sub(m, GUI_T("&Encoding"));
        gui_menu_item(enc_menu, GUI_T("&UTF-8"), C_ENC, NULL);
        gui_menu_sep(enc_menu);
        for (k = 0; k < ENC_PAGES; k++)
            gui_menu_item(enc_menu, GUI_T(names[k]), C_ENC + 1 + k, NULL);
    }
    view = m = gui_menu(bar, GUI_T("&View"));
    gui_menu_item(m, GUI_T("&Line Numbers"), C_NUMBERS, NULL);
    gui_menu_item(m, GUI_T("&Highlighting"), C_COLOURS, NULL);
    {                                           /* the shown document's kind, chosen by hand */
        const char *name;
        int k;
        kind_menu = gui_menu_sub(m, GUI_T("Highlight &As"));
        gui_menu_item(kind_menu, GUI_T("By the File's &Name"), C_KIND_AUTO, NULL);
        gui_menu_sep(kind_menu);
        for (k = 0; lexer_kind(k, &name), name; k++)
            gui_menu_item(kind_menu, GUI_T(name), C_KIND + k, NULL);
    }
    gui_menu_item(m, GUI_T("&Keep Session"), C_SESSION, NULL);
    gui_menu_item(m, GUI_T("Find in Files' &Results"), C_RESULTS, NULL);
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_item(m, GUI_T("&About Notepad"), C_ABOUT, NULL);
    strip = gui_tabs(gui_vbox(win->root, 0, 0));
    gui_tabs_style(strip, GUI_TABS_SCROLL);
    gui_tabs_flags(strip, GUI_TABS_CLOSE | GUI_TABS_NEW);
    gui_on(strip, strip_on, 0);
    gui_grow(strip->parent, 1);
    gui_grow(strip, 1);
    {                                           /* Find in Files' pane, under the tabs: there once a search is made */
        static const char *const heads[3] = { GUI_N("File"), GUI_N("Line"), GUI_N("Text") };
        static const int widths[3] = { 150, 40, 300 };
        struct gui_widget *row;
        inf.box = gui_vbox(strip->parent, 2, 2);
        row = gui_hbox(inf.box, 6, 0);
        inf.note = gui_label(row, "");
        gui_grow(inf.note, 1);
        gui_button(row, GUI_T("Close"), inf_close, 0);
        inf.list = gui_list(inf.box, 3, heads, widths);
        gui_size(inf.list, 0, 100);
        gui_grow(inf.list, 0);
        gui_on(inf.list, inf_list_on, 0);
        gui_visible(inf.box, 0);
    }
    status = gui_status(win);
    enc_at = gui_status_section(status, 60);
    kind_at = gui_status_section(status, 90);
    gui_menu_check(format, C_WRAP, wrap);
    gui_menu_check(view, C_NUMBERS, numbers);
    gui_menu_check(view, C_COLOURS, colours);
    gui_menu_check(view, C_SESSION, session);
    doc_new();
    for (i = 1; i < argc; i++)                  /* NOTEPAD FILE...: each in a tab */
        open_file(argv[i]);
    if (argc < 2)
        session_load(1);                        /* (started bare: the session's files, if one is kept) */
    if (ndocs > 1) {
        gui_tabs_set(strip, 0);
        title();
    }
    win->focus = cur()->ed;
    gui_show(win, 1);
    gui_idle(where, NULL, 100);
    where(NULL);
    gui_run();
    note("end%s", "");
    return 0;
}
