/* Markdown into a rich-text document (toolkit 2, step C8; sdk/gui/rich.c): Help's pages, the Chat's and the IDE's
 * messages. What CommonMark has that such pages use:
 *
 *   # ## ### headings (more #s: the third level); paragraphs (lines joined; two spaces or a backslash at a line's
 *   end break it); - * + bullets and 1. 1) numbers, nested by indentation; > quotes (>> deeper); ``` fenced code
 *   and code indented four spaces; --- *** ___ rules; | tables | with a |---| line under the head; and inline,
 *   **bold** __bold__ *italic* _italic_ `code` ~~struck~~ [links](to) <autolinks> ![pictures](from) and \-escapes.
 *   <details> (or <details open>), <summary>...</summary> and </details>, each alone on its line, make a block that
 *   opens and closes.
 *
 * A picture is the app's to find (its function given the address; NULL: its words are shown, framed). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

struct md {
    struct gui_rdoc *d;
    int (*image)(void *ctx, const char *src, const unsigned **px, int *w, int *h);
    void *ctx;
};

/* ---- inline: runs of text with their styles */
static const char *find(const char *p, const char *end, const char *mark)      /* the closing mark, or NULL */
{
    size_t n = strlen(mark);
    for (; p + n <= end; p++) {
        if (*p == '\\' && p + 1 < end) {
            p++;
            continue;
        }
        if (!strncmp(p, mark, n))
            return p;
    }
    return NULL;
}

static void put(struct md *m, const char *t, int n, unsigned style, const char *link)
{
    if (n > 0)
        gui_rdoc_text(m->d, t, n, style, link);
}

static void inline_text(struct md *m, const char *p, const char *end, unsigned style, const char *link)
{
    const char *run = p;
    while (p < end) {
        const char *q;
        if (*p == '\\' && p + 1 < end && strchr("\\`*_{}[]()#+-.!|~<>", p[1])) {
            put(m, run, (int)(p - run), style, link);
            run = p + 1, p += 2;                /* (the escaped character starts the next run) */
            continue;
        }
        if (*p == '\n') {                       /* a hard break (marked as such by the block's reader) */
            put(m, run, (int)(p - run), style, link);
            put(m, "\n", 1, style, link);
            run = ++p;
            continue;
        }
        if (*p == '`' && (q = find(p + 1, end, "`")) != NULL) {
            put(m, run, (int)(p - run), style, link);
            put(m, p + 1, (int)(q - p - 1), style | GUI_RS_CODE, link);
            run = p = q + 1;
            continue;
        }
        if ((!strncmp(p, "**", 2) || !strncmp(p, "__", 2)) && p + 2 < end && p[2] != ' ') {
            char mk[3] = { p[0], p[0], 0 };
            if ((q = find(p + 2, end, mk)) != NULL && q > p + 2) {
                put(m, run, (int)(p - run), style, link);
                inline_text(m, p + 2, q, style | GUI_RS_BOLD, link);
                run = p = q + 2;
                continue;
            }
        }
        if (!strncmp(p, "~~", 2) && (q = find(p + 2, end, "~~")) != NULL && q > p + 2) {
            put(m, run, (int)(p - run), style, link);
            inline_text(m, p + 2, q, style | GUI_RS_STRIKE, link);
            run = p = q + 2;
            continue;
        }
        if ((*p == '*' || (*p == '_' && (p == run || p[-1] == ' ' || p[-1] == '('))) && p + 1 < end && p[1] != ' '
            && p[1] != *p) {
            char mk[2] = { *p, 0 };
            if ((q = find(p + 1, end, mk)) != NULL && q > p + 1 && (*p == '*' || q + 1 >= end || q[1] == ' '
                                                                   || strchr(".,;:!?)", q[1]))) {
                put(m, run, (int)(p - run), style, link);
                inline_text(m, p + 1, q, style | GUI_RS_ITALIC, link);
                run = p = q + 1;
                continue;
            }
        }
        if ((*p == '[' || (*p == '!' && p + 1 < end && p[1] == '[')) && !link) {
            const char *t0 = p + (*p == '!' ? 2 : 1), *t1 = find(t0, end, "]"), *u1;
            if (t1 && t1 + 1 < end && t1[1] == '(' && (u1 = find(t1 + 2, end, ")")) != NULL) {
                char url[256];
                int un = (int)(u1 - t1 - 2) < (int)sizeof url - 1 ? (int)(u1 - t1 - 2) : (int)sizeof url - 1;
                memcpy(url, t1 + 2, (size_t)un), url[un] = 0;
                put(m, run, (int)(p - run), style, link);
                if (*p == '!') {                /* a picture: its own block, the text going on after it */
                    const unsigned *px = NULL;
                    int w = 0, h = 0;
                    char alt[128];
                    int an = (int)(t1 - t0) < (int)sizeof alt - 1 ? (int)(t1 - t0) : (int)sizeof alt - 1;
                    memcpy(alt, t0, (size_t)an), alt[an] = 0;
                    if (!m->image || m->image(m->ctx, url, &px, &w, &h) != 0)
                        px = NULL;
                    if (gui_rdoc_last_empty(m->d))      /* (no text before it in its paragraph) */
                        gui_rdoc_drop_last(m->d);
                    gui_rdoc_image(m->d, px, w, h, alt[0] ? alt : url);
                    gui_rdoc_block(m->d, GUI_RB_PARA, 0);
                } else {
                    inline_text(m, t0, t1, style, url);
                }
                run = p = u1 + 1;
                continue;
            }
        }
        if (*p == '<' && !link && (!strncmp(p + 1, "http://", 7) || !strncmp(p + 1, "https://", 8)
                                   || !strncmp(p + 1, "mailto:", 7)) && (q = find(p + 1, end, ">")) != NULL) {
            char url[256];
            int un = (int)(q - p - 1) < (int)sizeof url - 1 ? (int)(q - p - 1) : (int)sizeof url - 1;
            memcpy(url, p + 1, (size_t)un), url[un] = 0;
            put(m, run, (int)(p - run), style, link);
            put(m, url, un, style, url);
            run = p = q + 1;
            continue;
        }
        p++;
    }
    put(m, run, (int)(p - run), style, link);
}

/* ---- blocks */
struct lines {
    const char *p, *end;
};

static int next_line(struct lines *L, const char **s, int *n)
{
    const char *e;
    if (L->p >= L->end)
        return 0;
    for (e = L->p; e < L->end && *e != '\n'; e++)
        ;
    *s = L->p, *n = (int)(e - L->p);
    if (*n > 0 && (*s)[*n - 1] == '\r')
        (*n)--;
    L->p = e < L->end ? e + 1 : e;
    return 1;
}

static int blank(const char *s, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (s[i] != ' ' && s[i] != '\t')
            return 0;
    return 1;
}

static int indent(const char *s, int n)        /* its leading spaces (a tab four) */
{
    int i, k = 0;
    for (i = 0; i < n && (s[i] == ' ' || s[i] == '\t'); i++)
        k += s[i] == '\t' ? 4 : 1;
    return k;
}

static const char *skip(const char *s, int n, int *left)       /* past the leading spaces */
{
    int i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\t'))
        i++;
    *left = n - i;
    return s + i;
}

static int rule(const char *s, int n)
{
    int i, k = 0;
    char c = 0;
    for (i = 0; i < n; i++) {
        if (s[i] == ' ')
            continue;
        if (!c && strchr("-*_", s[i]))
            c = s[i];
        if (s[i] != c)
            return 0;
        k++;
    }
    return k >= 3;
}

static int item(const char *s, int n, int *number)     /* a list item's mark: its length (0: none) */
{
    int i = 0;
    if (n >= 2 && strchr("-*+", s[0]) && s[1] == ' ')
        return *number = 0, 2;
    while (i < n && i < 9 && s[i] >= '0' && s[i] <= '9')
        i++;
    if (i > 0 && i + 1 < n && (s[i] == '.' || s[i] == ')') && s[i + 1] == ' ')
        return *number = 1, i + 2;
    return 0;
}

static int is_tag(const char *s, int n, const char *tag)
{
    int k = (int)strlen(tag);
    return n >= k && !strncmp(s, tag, (size_t)k) && blank(s + k, n - k);
}

/* What a paragraph's lines hold, as one text: the line breaks two spaces or a backslash ask for kept as \n. */
struct para {
    char *t;
    int n, room;
};

static void para_add(struct para *P, const char *s, int n)
{
    int hard = (n >= 2 && s[n - 1] == ' ' && s[n - 2] == ' ') || (n >= 1 && s[n - 1] == '\\');
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\\'))
        n--;
    if (P->n + n + 2 > P->room) {
        int room = (P->n + n + 2) * 2;
        char *t = realloc(P->t, (size_t)room);
        if (!t)
            return;
        P->t = t, P->room = room;
    }
    if (P->n && P->t[P->n - 1] != '\n')
        P->t[P->n++] = ' ';
    memcpy(P->t + P->n, s, (size_t)n), P->n += n;
    if (hard)
        P->t[P->n++] = '\n';
}

static void para_end(struct md *m, struct para *P, int kind, int depth)
{
    if (!P->n)
        return;
    while (P->n && P->t[P->n - 1] == '\n')
        P->n--;
    gui_rdoc_block(m->d, kind, depth);
    inline_text(m, P->t, P->t + P->n, 0, NULL);
    if (gui_rdoc_last_empty(m->d))              /* (nothing after a picture) */
        gui_rdoc_drop_last(m->d);
    P->n = 0;
}

static void table_row(struct md *m, const char *s, int n, int head)
{
    const char *e = s + n, *c;
    if (n && *s == '|')
        s++;
    if (e > s && e[-1] == '|')
        e--;
    gui_rdoc_block(m->d, GUI_RB_ROW, 0);
    for (c = s; c <= e;) {
        const char *q = c, *a, *b;
        while (q < e && !(*q == '|' && (q == c || q[-1] != '\\')))
            q++;
        for (a = c; a < q && *a == ' '; a++)
            ;
        for (b = q; b > a && b[-1] == ' '; b--)
            ;
        if (c != s)
            gui_rdoc_cell(m->d);
        if (head)
            put(m, a, (int)(b - a), GUI_RS_BOLD, NULL);
        else
            inline_text(m, a, b, 0, NULL);
        if (q >= e)
            break;
        c = q + 1;
    }
}

struct gui_rdoc *gui_markdown(const char *text, int len,
                              int (*image)(void *ctx, const char *src, const unsigned **px, int *w, int *h), void *ctx)
{
    struct md m;
    struct lines L;
    struct para P = { NULL, 0, 0 };
    const char *s;
    int n, kind = GUI_RB_PARA, depth = 0, base[8], nlevels = 0, quote = 0;
    m.d = gui_rdoc_new(), m.image = image, m.ctx = ctx;
    if (!m.d || !text)
        return m.d;
    L.p = text, L.end = text + (len < 0 ? (int)strlen(text) : len);
    while (next_line(&L, &s, &n)) {
        int ind = indent(s, n), left, number, mk, q;
        const char *t = skip(s, n, &left);
        if (blank(s, n)) {                      /* a blank line ends the paragraph (and a quote) */
            para_end(&m, &P, kind, depth);
            kind = GUI_RB_PARA, quote = 0;
            continue;
        }
        if (left >= 3 && !strncmp(t, "```", 3)) {   /* fenced code, to its closing fence */
            struct para C = { NULL, 0, 0 };
            int fence = ind;
            para_end(&m, &P, kind, depth);
            while (next_line(&L, &s, &n)) {
                int l2, i2 = indent(s, n);
                const char *t2 = skip(s, n, &l2);
                if (l2 >= 3 && !strncmp(t2, "```", 3))
                    break;
                if (C.n + n + 2 > C.room) {
                    int room = (C.n + n + 2) * 2;
                    char *c = realloc(C.t, (size_t)room);
                    if (!c)
                        break;
                    C.t = c, C.room = room;
                }
                if (C.n)
                    C.t[C.n++] = '\n';
                (void)i2;
                {
                    int cut = 0;                /* (the fence's own indentation taken off its lines) */
                    while (cut < fence && cut < n && s[cut] == ' ')
                        cut++;
                    memcpy(C.t + C.n, s + cut, (size_t)(n - cut)), C.n += n - cut;
                }
            }
            gui_rdoc_block(m.d, GUI_RB_CODE, ind > 0 && nlevels ? nlevels : 0);     /* (indented under an item:
                                                   as deep as it) */
            if (C.n)
                gui_rdoc_text(m.d, C.t, C.n, 0, NULL);
            free(C.t);
            kind = GUI_RB_PARA, nlevels = 0;
            continue;
        }
        if (is_tag(t, left, "<details>") || is_tag(t, left, "<details open>")) {
            para_end(&m, &P, kind, depth);
            gui_rdoc_details(m.d, left > 9);
            continue;
        }
        if (left > 19 && !strncmp(t, "<summary>", 9)) {  /* the summary: the details block's own text */
            const char *e = strstr(t, "</summary>");
            if (e && e < t + left)
                inline_text(&m, t + 9, e, 0, NULL);
            continue;
        }
        if (is_tag(t, left, "</details>")) {
            para_end(&m, &P, kind, depth);
            gui_rdoc_end_details(m.d);
            continue;
        }
        if (rule(s, n) && !(kind == GUI_RB_PARA && P.n && t[0] == '-')) {
            para_end(&m, &P, kind, depth);
            nlevels = 0;
            gui_rdoc_block(m.d, GUI_RB_RULE, 0);
            kind = GUI_RB_PARA;
            continue;
        }
        if (t[0] == '#') {                      /* a heading */
            int h = 0;
            while (h < left && t[h] == '#')
                h++;
            if (h < left && t[h] == ' ') {
                const char *e = t + left;
                para_end(&m, &P, kind, depth);
                while (e > t + h && (e[-1] == '#' || e[-1] == ' '))
                    e--;
                gui_rdoc_block(m.d, h == 1 ? GUI_RB_HEAD1 : h == 2 ? GUI_RB_HEAD2 : GUI_RB_HEAD3, 0);
                nlevels = 0;
                inline_text(&m, t + h + 1, e, 0, NULL);
                kind = GUI_RB_PARA;
                continue;
            }
        }
        if (t[0] == '|') {                      /* a table: its head, the line under it, its rows */
            const char *s2;
            int n2, l2;
            para_end(&m, &P, kind, depth);
            nlevels = 0;
            table_row(&m, t, left, 0);
            {
                struct lines save = L;          /* (the line under the head: |---|---|) */
                if (next_line(&L, &s2, &n2)) {
                    const char *t2 = skip(s2, n2, &l2);
                    if (l2 && t2[0] == '|' && strspn(t2, "|-: ") == (size_t)l2) {
                        gui_rdoc_drop_last(m.d);        /* (the row just made was the head: made again, bold) */
                        table_row(&m, t, left, 1);
                    } else {
                        L = save;
                    }
                }
            }
            kind = GUI_RB_PARA;
            continue;
        }
        if (t[0] == '>') {                      /* a quote (>> deeper) */
            int dq = 0;
            while (left > 0 && (*t == '>' || *t == ' '))
                dq += *t == '>', t++, left--;
            if (kind != GUI_RB_QUOTE || dq != quote)
                para_end(&m, &P, kind, depth);
            nlevels = 0;
            kind = GUI_RB_QUOTE, depth = dq - 1, quote = dq;
            para_add(&P, t, left);
            continue;
        }
        if ((mk = item(t, left, &number)) > 0) {        /* a list's item, as deep as its indentation */
            para_end(&m, &P, kind, depth);
            while (nlevels > 0 && ind < base[nlevels - 1])
                nlevels--;
            if (nlevels == 0 || ind > base[nlevels - 1]) {
                if (nlevels < 8)
                    base[nlevels++] = ind;
            }
            kind = number ? GUI_RB_NUMBER : GUI_RB_BULLET, depth = nlevels - 1;
            para_add(&P, t + mk, left - mk);
            continue;
        }
        if ((kind == GUI_RB_BULLET || kind == GUI_RB_NUMBER) && P.n && ind > 0) {
            para_add(&P, t, left);              /* (an item's next line) */
            continue;
        }
        if (ind >= 4 && !P.n && kind == GUI_RB_PARA) {  /* code indented four */
            struct para C = { NULL, 0, 0 };
            do {
                if (C.n + n + 2 > C.room) {
                    int room = (C.n + n + 2) * 2;
                    char *c = realloc(C.t, (size_t)room);
                    if (!c)
                        break;
                    C.t = c, C.room = room;
                }
                if (C.n)
                    C.t[C.n++] = '\n';
                q = 0;
                while (q < 4 && q < n && s[q] == ' ')
                    q++;
                memcpy(C.t + C.n, s + q, (size_t)(n - q)), C.n += n - q;
                {
                    struct lines save = L;
                    if (!next_line(&L, &s, &n) || indent(s, n) < 4 || blank(s, n)) {
                        L = save;
                        break;
                    }
                }
            } while (1);
            gui_rdoc_block(m.d, GUI_RB_CODE, 0);
            if (C.n)
                gui_rdoc_text(m.d, C.t, C.n, 0, NULL);
            free(C.t);
            continue;
        }
        if (kind != GUI_RB_PARA && kind != GUI_RB_QUOTE && !P.n)
            kind = GUI_RB_PARA;
        if (kind == GUI_RB_BULLET || kind == GUI_RB_NUMBER) {   /* (a line at the left after an item: a paragraph) */
            para_end(&m, &P, kind, depth);
            kind = GUI_RB_PARA, nlevels = 0;
        }
        if (kind == GUI_RB_PARA)
            depth = 0;
        para_add(&P, t, left);
    }
    para_end(&m, &P, kind, depth);
    free(P.t);
    return m.d;
}
