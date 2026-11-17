/* The rich-text view and its document (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): Help's pages, the Web
 * browser's, the Chat's lines, the IDE's conversation.
 *
 * A document (struct gui_rdoc) is a list of blocks: paragraphs, three levels of heading, items of bulleted and
 * numbered lists (nested by depth), quotes, code, rules, the rows of simple tables, pictures, and blocks that open
 * and close (their summary shown, what they hold below it while open). A block's text is runs, each with a style
 * (bold, italic, code, a link to somewhere). Markdown (sdk/gui/markdown.c) and the app build documents; the view
 * keeps the one it is given.
 *
 * The view wraps the blocks to its width and scrolls (the bar, the wheel, the arrows and pages); the mouse chooses
 * text (Ctrl+C copies it, as plain text: a line a block, a tab between cells); a link pressed, or reached by Tab
 * and Enter, is the app's (GUI_ON_ACTIVATE, gui_richview_link); a block that opens and closes does so when its
 * summary is pressed. Italic is drawn as plain text in another colour until the toolkit has an italic font. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

struct rrun {
    char *text;
    int len;
    unsigned style;
    char *link;
    unsigned rgb;                       /* its own colour (gui_rdoc_colour), or GFX_NONE: its style's */
};

struct rblock {
    int kind, depth, number;
    struct rrun *run;
    int nrun, room;
    int table;                          /* (GUI_RB_ROW) the table it is a row of (rows of one table follow on) */
    int parent;                         /* the block that opens and closes round it, or -1 */
    int open;                           /* (GUI_RB_DETAILS) */
    const unsigned *px;                 /* (GUI_RB_IMAGE) its picture, kept, not copied; or NULL: its text */
    int iw, ih;
};

struct gui_rdoc {
    struct rblock *b;
    int n, room;
    int in;                             /* the block that opens and closes the next blocks are in, or -1 */
    int tables;
    int numbers[8];                     /* the next number at each depth of a numbered list */
    unsigned rgb;                       /* the colour of the text added next (GFX_NONE: its style's) */
};

/* ---- the document */
struct gui_rdoc *gui_rdoc_new(void)
{
    struct gui_rdoc *d = calloc(1, sizeof *d);
    if (d)
        d->in = -1, d->rgb = GFX_NONE;
    return d;
}

void gui_rdoc_free(struct gui_rdoc *d)
{
    int i, k;
    if (!d)
        return;
    for (i = 0; i < d->n; i++) {
        for (k = 0; k < d->b[i].nrun; k++)
            free(d->b[i].run[k].text), free(d->b[i].run[k].link);
        free(d->b[i].run);
    }
    free(d->b);
    free(d);
}

int gui_rdoc_blocks(const struct gui_rdoc *d) { return d ? d->n : 0; }

/* Block i as a line, for tests: its kind, depth and number, then its runs, each marked by its style (*bold*,
   /italic/, `code`, ~struck~, [text](link), | between cells). Its length. */
int gui_rdoc_describe(const struct gui_rdoc *d, int i, char *out, int room)
{
    static const char *const kinds[] = { "P", "H1", "H2", "H3", "BULLET", "NUMBER", "QUOTE", "CODE", "RULE", "ROW",
                                         "IMAGE", "DETAILS" };
    const struct rblock *b;
    int n, r, k;
    if (!d || i < 0 || i >= d->n || room < 1)
        return -1;
    b = &d->b[i];
    n = snprintf(out, (size_t)room, "%s", b->kind >= 0 && b->kind < 12 ? kinds[b->kind] : "?");
    if (b->depth && n < room)
        n += snprintf(out + n, (size_t)(room - n), " d%d", b->depth);
    if (b->kind == GUI_RB_NUMBER && n < room)
        n += snprintf(out + n, (size_t)(room - n), " %d.", b->number);
    if (b->parent >= 0 && n < room)
        n += snprintf(out + n, (size_t)(room - n), " in%d", b->parent);
    if (b->kind == GUI_RB_DETAILS && n < room)
        n += snprintf(out + n, (size_t)(room - n), b->open ? " open" : " shut");
    if (n < room)
        n += snprintf(out + n, (size_t)(room - n), ":");
    for (r = 0; r < b->nrun && n < room - 1; r++) {
        const struct rrun *rr = &b->run[r];
        const char *pre = rr->style & GUI_RS_CELL ? " |" : rr->link ? "[" : rr->style & GUI_RS_CODE ? "`"
                          : rr->style & GUI_RS_BOLD ? "*" : rr->style & GUI_RS_ITALIC ? "/" : rr->style & GUI_RS_STRIKE
                          ? "~" : "";
        const char *post = rr->style & GUI_RS_CELL ? "" : rr->link ? "]" : pre;
        n += snprintf(out + n, (size_t)(room - n), " %s", pre);
        for (k = 0; k < rr->len && n < room - 1; k++)
            out[n++] = rr->text[k] == '\n' ? '|' : rr->text[k];
        if (n < room - 1)
            n += snprintf(out + n, (size_t)(room - n), "%s", post);
        if (rr->link && n < room - 1)
            n += snprintf(out + n, (size_t)(room - n), "(%s)", rr->link);
    }
    out[n < room ? n : room - 1] = 0;
    return n;
}

static struct rblock *last(struct gui_rdoc *d) { return d->n ? &d->b[d->n - 1] : NULL; }

void gui_rdoc_block(struct gui_rdoc *d, int kind, int depth)
{
    struct rblock *b, *prev = last(d);
    if (d->n == d->room) {
        int room = d->room * 2 + 16;
        struct rblock *nb = realloc(d->b, (size_t)room * sizeof *nb);
        if (!nb)
            return;
        d->b = nb, d->room = room;
    }
    b = &d->b[d->n++];
    memset(b, 0, sizeof *b);
    b->kind = kind, b->depth = depth < 0 ? 0 : depth > 7 ? 7 : depth;
    b->parent = d->in;
    if (kind == GUI_RB_NUMBER) {               /* (numbered from 1, again after anything but deeper items) */
        if (!prev || !(prev->kind == GUI_RB_NUMBER || (prev->kind == GUI_RB_BULLET) || prev->depth > b->depth))
            memset(d->numbers, 0, sizeof d->numbers);
        else if (prev->depth < b->depth)
            d->numbers[b->depth] = 0;
        b->number = ++d->numbers[b->depth];
    }
    if (kind == GUI_RB_ROW)
        b->table = prev && prev->kind == GUI_RB_ROW ? prev->table : ++d->tables;
}

void gui_rdoc_text(struct gui_rdoc *d, const char *text, int n, unsigned style, const char *link)
{
    struct rblock *b = last(d);
    struct rrun *r;
    if (!b)
        gui_rdoc_block(d, GUI_RB_PARA, 0), b = last(d);
    if (!b || !text)
        return;
    n = n < 0 ? (int)strlen(text) : n;
    if (b->nrun && !link && !b->run[b->nrun - 1].link && b->run[b->nrun - 1].style == style && !(style & GUI_RS_CELL)
        && b->run[b->nrun - 1].rgb == d->rgb) {
        r = &b->run[b->nrun - 1];               /* (more of the same style: one run) */
        char *t = realloc(r->text, (size_t)(r->len + n + 1));
        if (!t)
            return;
        memcpy(t + r->len, text, (size_t)n), t[r->len + n] = 0;
        r->text = t, r->len += n;
        return;
    }
    if (b->nrun == b->room) {
        int room = b->room * 2 + 4;
        struct rrun *nr = realloc(b->run, (size_t)room * sizeof *nr);
        if (!nr)
            return;
        b->run = nr, b->room = room;
    }
    r = &b->run[b->nrun++];
    r->text = malloc((size_t)n + 1);
    if (r->text)
        memcpy(r->text, text, (size_t)n), r->text[n] = 0;
    r->len = r->text ? n : 0;
    r->style = style;
    r->link = link ? strdup(link) : NULL;
    r->rgb = d->rgb;
}

void gui_rdoc_colour(struct gui_rdoc *d, unsigned rgb) { d->rgb = rgb; }      /* the text added next (GFX_NONE: as
                                                   its style has it) */

void gui_rdoc_cell(struct gui_rdoc *d)          /* (a row) the next cell begins */
{
    gui_rdoc_text(d, "", 0, GUI_RS_CELL, NULL);
}

void gui_rdoc_image(struct gui_rdoc *d, const unsigned *px, int w, int h, const char *alt)
{
    struct rblock *b;
    gui_rdoc_block(d, GUI_RB_IMAGE, 0);
    b = last(d);
    if (!b)
        return;
    b->px = px, b->iw = w, b->ih = h;
    if (alt)
        gui_rdoc_text(d, alt, -1, 0, NULL);
}

void gui_rdoc_details(struct gui_rdoc *d, int open)
{
    gui_rdoc_block(d, GUI_RB_DETAILS, 0);
    if (last(d))
        last(d)->open = open != 0;
    d->in = d->n - 1;
}

void gui_rdoc_end_details(struct gui_rdoc *d)
{
    if (d->in >= 0)
        d->in = d->b[d->in].parent;
}

int gui_rdoc_last_empty(const struct gui_rdoc *d)      /* (markdown.c) the last block a paragraph with no text */
{
    int k;
    if (!d || !d->n || d->b[d->n - 1].kind != GUI_RB_PARA)
        return 0;
    for (k = 0; k < d->b[d->n - 1].nrun; k++)
        if (d->b[d->n - 1].run[k].len)
            return 0;
    return 1;
}

void gui_rdoc_drop_last(struct gui_rdoc *d)    /* (markdown.c: a table's head made again) the last block goes */
{
    struct rblock *b = last(d);
    int k;
    if (!b)
        return;
    for (k = 0; k < b->nrun; k++)
        free(b->run[k].text), free(b->run[k].link);
    free(b->run);
    d->n--;
}

void gui_rdoc_drop_first(struct gui_rdoc *d, int n)    /* (the chat view's limit) the first n blocks go */
{
    int i, k;
    n = n > d->n ? d->n : n;
    for (i = 0; i < n; i++) {
        for (k = 0; k < d->b[i].nrun; k++)
            free(d->b[i].run[k].text), free(d->b[i].run[k].link);
        free(d->b[i].run);
    }
    memmove(d->b, d->b + n, (size_t)(d->n - n) * sizeof *d->b);
    d->n -= n;
    for (i = 0; i < d->n; i++)
        d->b[i].parent = d->b[i].parent >= n ? d->b[i].parent - n : -1;
    d->in = d->in >= n ? d->in - n : -1;
}

/* ---- the view's layout: lines of pieces */
enum { P_TEXT, P_BULLET, P_NUMBER, P_IMAGE, P_RULE, P_ARROW, P_CELL };      /* (P_CELL: the line between two of a
                                                   row's cells, as tall as the row) */

struct piece {
    int block, run, off, len;           /* the run's bytes it shows (P_TEXT) */
    int at;                             /* its first byte's place in the block's text (selection) */
    int x, w;
    unsigned char kind;
};

struct rline {
    int y, h;
    int first, n;                       /* its pieces */
    int block;
    int x0, x1;                         /* the block's extent on this line (code's shading, a quote's bar) */
    int mark, mark_x;                   /* a list's mark or a summary's arrow before it (P_), at mark_x */
    int cell_box;                       /* (a table's row) its cells' frames are drawn with its first line */
};

struct rview {
    struct gui_widget w;
    struct gui_rdoc *doc;
    struct piece *p;
    int np, proom;
    struct rline *l;
    int nl, lroom;
    int laid_w, total;                  /* the width laid out for (0: lay out again); the height of it all */
    struct gui_sb sb;
    int sel_on, dragging;
    int sa_block, sa_at, sb_block, sb_at;       /* the choice: from (block, byte), to */
    int link;                           /* the piece of the link reached by Tab (-1: none) */
    char *link_text;                    /* the link GUI_ON_ACTIVATE told of */
    int follow;                         /* (the app's: gui_richview_follow) kept at the end as the document grows ... */
    int at_end;                         /* ... while it is scrolled to the end */
    int limit;                          /* (a chat view) the blocks kept at most (0: no limit) */
};

#define RV(w) ((struct rview *)(w))
#define MARGIN 6
#define INDENT 20

static const struct gfx_font *font_of(int kind, unsigned style)
{
    if (style & GUI_RS_CODE || kind == GUI_RB_CODE)
        return gui_mono;
    if (kind == GUI_RB_HEAD1 || kind == GUI_RB_HEAD2) {
        const struct gfx_font *f = gui_font_get(GUI_FONT_BOLD, 2);
        return f ? f : gui_bold;
    }
    if (kind == GUI_RB_HEAD3 || style & GUI_RS_BOLD)
        return gui_bold;
    return gui_font;
}

static struct piece *add_piece(struct rview *v)
{
    if (v->np == v->proom) {
        int room = v->proom * 2 + 64;
        struct piece *np = realloc(v->p, (size_t)room * sizeof *np);
        if (!np)
            return NULL;
        v->p = np, v->proom = room;
    }
    memset(&v->p[v->np], 0, sizeof v->p[0]);
    return &v->p[v->np++];
}

static struct rline *add_line(struct rview *v, int block, int y)
{
    if (v->nl == v->lroom) {
        int room = v->lroom * 2 + 32;
        struct rline *nl = realloc(v->l, (size_t)room * sizeof *nl);
        if (!nl)
            return NULL;
        v->l = nl, v->lroom = room;
    }
    memset(&v->l[v->nl], 0, sizeof v->l[0]);
    v->l[v->nl].y = y, v->l[v->nl].block = block, v->l[v->nl].first = v->np;
    return &v->l[v->nl++];
}

static int hidden(const struct gui_rdoc *d, int i)     /* inside a closed block */
{
    for (i = d->b[i].parent; i >= 0; i = d->b[i].parent)
        if (!d->b[i].open)
            return 1;
    return 0;
}

/* The runs of block bi from run r0 up to (not with) run r1, wrapped into x0 .. x1 from y: the lines' height. */
static int flow(struct rview *v, int bi, int r0, int r1, int x0, int x1, int y, int first_x)
{
    struct rblock *b = &v->doc->b[bi];
    struct rline *ln = add_line(v, bi, y);
    int x = first_x, top = y, r, at = 0;
    if (!ln)
        return 0;
    ln->x0 = x0, ln->x1 = x1;
    for (r = 0; r < r0; r++)
        at += b->run[r].len;
    for (r = r0; r < r1; r++) {
        struct rrun *rr = &b->run[r];
        const struct gfx_font *f = font_of(b->kind, rr->style);
        int off = 0;
        while (off < rr->len) {
            int n = 0, cut = -1, wd;
            if (rr->text[off] == '\n') {        /* a line's end within the block (code, a hard break) */
                ln->h = ln->h ? ln->h : f->height + 2;
                y += ln->h;
                ln->n = v->np - ln->first;
                ln = add_line(v, bi, y);
                if (!ln)
                    return y - top;
                ln->x0 = x0, ln->x1 = x1;
                x = x0, off++;
                continue;
            }
            while (off + n < rr->len && rr->text[off + n] != '\n') {
                int k = n + 1;
                while (off + k < rr->len && (rr->text[off + k] & 0xC0) == 0x80)
                    k++;                        /* (whole UTF-8 characters) */
                if (rr->text[off + n] == ' ' && b->kind != GUI_RB_CODE)
                    cut = k;
                if (x + gfx_text_width(f, rr->text + off, k) > x1 && (cut > 0 || x > x0 || n > 0))
                    break;
                n = k;
            }
            if (off + n < rr->len && rr->text[off + n] != '\n' && cut <= 0 && x > x0 && b->kind != GUI_RB_CODE)
                n = 0;                          /* (a word that doesn't fit after what is on the line: all of it to
                                                   the next; split only if it is wider than a line) */
            if (off + n < rr->len && rr->text[off + n] != '\n' && cut > 0 && b->kind != GUI_RB_CODE)
                n = cut;                        /* (at the last space that fits) */
            if (n == 0 && x > x0) {             /* nothing fits after what is on the line: the next line */
                ln->h = ln->h ? ln->h : f->height + 2;
                y += ln->h;
                ln->n = v->np - ln->first;
                ln = add_line(v, bi, y);
                if (!ln)
                    return y - top;
                ln->x0 = x0, ln->x1 = x1;
                x = x0;
                while (off < rr->len && rr->text[off] == ' ')
                    off++, at++;
                continue;
            }
            if (n == 0)
                n = 1;
            wd = gfx_text_width(f, rr->text + off, n);
            {
                struct piece *p = add_piece(v);
                if (!p)
                    return y - top;
                p->kind = P_TEXT, p->block = bi, p->run = r, p->off = off, p->len = n, p->at = at + off;
                p->x = x, p->w = wd;
            }
            if (ln->h < f->height + 2)
                ln->h = f->height + 2;
            x += wd, off += n;
            if (off < rr->len && rr->text[off] != '\n') {      /* (wrapped: the rest on the next line) */
                y += ln->h;
                ln->n = v->np - ln->first;
                ln = add_line(v, bi, y);
                if (!ln)
                    return y - top;
                ln->x0 = x0, ln->x1 = x1;
                x = x0;
                while (off < rr->len && rr->text[off] == ' ' && b->kind != GUI_RB_CODE)
                    off++;
            }
        }
        at += rr->len;
    }
    if (!ln->h)
        ln->h = font_of(b->kind, 0)->height + 2;
    ln->n = v->np - ln->first;
    return y + ln->h - top;
}

/* A table: rows from block bi on with the same table: columns as wide as their widest cell, shared out if they
   don't fit. The block after it. */
static int table(struct rview *v, int bi, int x0, int x1, int *py)
{
    struct gui_rdoc *d = v->doc;
    int cols = 0, i, r, c, wd[16], need = 0, end = bi, y = *py;
    memset(wd, 0, sizeof wd);
    for (end = bi; end < d->n && d->b[end].kind == GUI_RB_ROW && d->b[end].table == d->b[bi].table; end++) {
        struct rblock *b = &d->b[end];
        for (r = 0, c = 0; r < b->nrun; r++) {
            if (b->run[r].style & GUI_RS_CELL) {
                c++;
                continue;
            }
            if (c < 16) {
                int t = gfx_text_width(font_of(b->kind, b->run[r].style), b->run[r].text, b->run[r].len) + 12;
                wd[c] = t > wd[c] ? t : wd[c];
            }
        }
        cols = c + 1 > cols ? c + 1 : cols;
    }
    cols = cols > 16 ? 16 : cols;
    for (c = 0; c < cols; c++)
        wd[c] = wd[c] < 24 ? 24 : wd[c], need += wd[c];
    if (need > x1 - x0)                         /* (too wide: each its share, by what it wanted) */
        for (c = 0; c < cols; c++)
            wd[c] = wd[c] * (x1 - x0) / need > 24 ? wd[c] * (x1 - x0) / need : 24;
    for (i = bi; i < end; i++) {
        struct rblock *b = &d->b[i];
        int x = x0, h = 0, r0 = 0, first_line = v->nl;
        if (hidden(d, i))
            continue;
        for (c = 0; c < cols; c++) {
            int r1 = r0, hh;
            while (r1 < b->nrun && !(b->run[r1].style & GUI_RS_CELL))
                r1++;
            hh = flow(v, i, r0, r1, x + 6, x + wd[c] - 6, y + 2, x + 6);
            h = hh > h ? hh : h;
            x += wd[c];
            r0 = r1 < b->nrun ? r1 + 1 : r1;
        }
        if (first_line < v->nl) {               /* (the row's frame: its first line carries its height, and the
                                                   lines between its cells) */
            struct rline *fl = &v->l[first_line];
            int cx = x0, k;
            fl->cell_box = h + 4;
            fl->x0 = x0, fl->x1 = x;
            for (c = 0; c + 1 < cols; c++) {
                struct piece *p;
                cx += wd[c];
                if (!(p = add_piece(v)))
                    break;
                p->kind = P_CELL, p->block = i, p->x = cx, p->w = 1;
                if (fl->first + fl->n != v->np - 1) {   /* (with the first line's pieces) */
                    struct piece t = *p;
                    memmove(&v->p[fl->first + fl->n + 1], &v->p[fl->first + fl->n],
                            (size_t)(v->np - 1 - (fl->first + fl->n)) * sizeof *p);
                    v->p[fl->first + fl->n] = t;
                    for (k = first_line + 1; k < v->nl; k++)
                        v->l[k].first++;
                }
                fl->n++;
            }
        }
        y += h + 4;
    }
    *py = y;
    return end;
}

static void layout(struct rview *v)
{
    struct gui_rdoc *d = v->doc;
    int width = v->w.r.w - GUI_SB - 2 * MARGIN, y = MARGIN, i;
    v->np = v->nl = 0;
    v->laid_w = v->w.r.w;
    if (!d)
        return;
    for (i = 0; i < d->n;) {
        struct rblock *b = &d->b[i];
        int x0 = MARGIN + b->depth * INDENT, x1 = MARGIN + width, gap = 6, k;
        for (k = b->parent; k >= 0; k = d->b[k].parent)
            x0 += 16;                           /* (what a block that opens holds: under its summary's text) */
        if (hidden(d, i)) {
            i++;
            continue;
        }
        switch (b->kind) {
        case GUI_RB_ROW:
            i = table(v, i, x0, x1, &y);
            y += gap;
            continue;
        case GUI_RB_RULE: {
            struct rline *ln = add_line(v, i, y);
            if (ln) {
                struct piece *p = add_piece(v);
                ln->h = 9, ln->x0 = x0, ln->x1 = x1;
                if (p)
                    p->kind = P_RULE, p->block = i, p->x = x0, p->w = x1 - x0, ln->n = 1;
            }
            y += 9 + gap;
            break;
        }
        case GUI_RB_IMAGE:
            if (b->px && b->iw > 0) {
                struct rline *ln = add_line(v, i, y);
                if (ln) {
                    struct piece *p = add_piece(v);
                    ln->h = b->ih, ln->x0 = x0, ln->x1 = x1;
                    if (p)
                        p->kind = P_IMAGE, p->block = i, p->x = x0, p->w = b->iw, ln->n = 1;
                }
                y += b->ih + gap;
            } else {                            /* (no picture: its words, framed) */
                y += flow(v, i, 0, b->nrun, x0 + 4, x1, y + 2, x0 + 4) + 4 + gap;
            }
            break;
        case GUI_RB_BULLET:
        case GUI_RB_NUMBER: {
            int n0 = v->nl, h = flow(v, i, 0, b->nrun, x0 + INDENT, x1, y, x0 + INDENT);
            if (n0 < v->nl)                     /* the mark before its first line */
                v->l[n0].mark = b->kind == GUI_RB_BULLET ? P_BULLET : P_NUMBER, v->l[n0].mark_x = x0;
            y += h + 2;
            break;
        }
        case GUI_RB_DETAILS: {
            int n0 = v->nl, h = flow(v, i, 0, b->nrun, x0 + 16, x1, y, x0 + 16);
            if (n0 < v->nl)
                v->l[n0].mark = P_ARROW, v->l[n0].mark_x = x0;
            y += h + gap;
            break;
        }
        case GUI_RB_QUOTE:
            y += flow(v, i, 0, b->nrun, x0 + 12, x1, y, x0 + 12) + gap;
            break;
        case GUI_RB_CODE:
            y += 3;
            y += flow(v, i, 0, b->nrun, x0 + 6, x1 - 6, y, x0 + 6) + 3 + gap;
            break;
        case GUI_RB_HEAD1:
            y += flow(v, i, 0, b->nrun, x0, x1, y, x0) + 4 + gap;
            break;
        default:
            y += flow(v, i, 0, b->nrun, x0, x1, y, x0) + (b->kind == GUI_RB_HEAD2 || b->kind == GUI_RB_HEAD3 ? 2 : 0)
                 + gap;
            break;
        }
        i++;
    }
    v->total = y + MARGIN;
}

static void place(struct rview *v)
{
    if (v->laid_w != v->w.r.w)
        layout(v);
    v->sb.r.x = v->w.r.x + v->w.r.w - GUI_SB, v->sb.r.y = v->w.r.y, v->sb.r.w = GUI_SB, v->sb.r.h = v->w.r.h;
    v->sb.total = v->total, v->sb.page = v->w.r.h;
    if (v->follow && v->at_end)
        v->sb.pos = v->total;
    gui_sb_clamp(&v->sb);
}

/* ---- drawing */
static int before(int ab, int aa, int bb, int ba) { return ab < bb || (ab == bb && aa < ba); }

static void sel_range(struct rview *v, int *b0, int *a0, int *b1, int *a1)
{
    if (before(v->sb_block, v->sb_at, v->sa_block, v->sa_at))
        *b0 = v->sb_block, *a0 = v->sb_at, *b1 = v->sa_block, *a1 = v->sa_at;
    else
        *b0 = v->sa_block, *a0 = v->sa_at, *b1 = v->sb_block, *a1 = v->sb_at;
}

static void rv_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct rview *v = RV(w);
    struct gui_rdoc *d = v->doc;
    struct gfx_rect clip = s->clip, in = { w->r.x + 1, w->r.y + 1, w->r.w - GUI_SB - 1, w->r.h - 2 };
    struct gfx_rect frame = { w->r.x, w->r.y, w->r.w - GUI_SB + 1, w->r.h };
    int i, k, ox = w->r.x, oy = w->r.y - v->sb.pos, b0 = 0, a0 = 0, b1 = 0, a1 = 0;
    place(v);
    oy = w->r.y - v->sb.pos;
    gui_draw_field(s, &frame, 1);
    gfx_rect_cut(&in, &clip);
    gfx_clip(s, &in);
    if (v->sel_on)
        sel_range(v, &b0, &a0, &b1, &a1);
    for (i = 0; d && i < v->nl; i++) {
        struct rline *ln = &v->l[i];
        struct rblock *b = &d->b[ln->block];
        int y = oy + ln->y;
        if (y > in.y + in.h || (y + ln->h + ln->cell_box < in.y))
            continue;
        if (b->kind == GUI_RB_CODE) {           /* the block's ground (3 more above its first line and below its
                                                   last), a quote's bar, a row's frame */
            int top = i == 0 || v->l[i - 1].block != ln->block ? 3 : 0;
            int bottom = i + 1 == v->nl || v->l[i + 1].block != ln->block ? 3 : 0;
            gfx_fill(s, ox + ln->x0, y - top, ln->x1 - ln->x0, ln->h + top + bottom, GUI_FACE);
        }
        if (b->kind == GUI_RB_QUOTE)
            gfx_fill(s, ox + ln->x0 - 12, y, 3, ln->h, GUI_LINE);
        if (b->kind == GUI_RB_ROW && ln->cell_box)
            gfx_frame(s, ox + ln->x0, y, ln->x1 - ln->x0, ln->cell_box, GUI_LINE);
        if (b->kind == GUI_RB_IMAGE && !b->px && (i == 0 || v->l[i - 1].block != ln->block))
            gfx_frame(s, ox + ln->x0 - 4, y - 2, ln->x1 - ln->x0 + 4, ln->h + 4, GUI_LINE);
        if (ln->mark == P_BULLET)
            gfx_fill(s, ox + ln->mark_x + 6, y + ln->h / 2 - 2, 4 - (b->depth & 1), 4 - (b->depth & 1), GUI_FIELD_TEXT);
        else if (ln->mark == P_NUMBER) {
            char t[12];
            snprintf(t, sizeof t, "%d.", b->number);
            gfx_text(s, ox + ln->mark_x + INDENT - 4 - gfx_text_width(gui_font, t, -1), y + ln->h - gui_font->height - 1,
                     gui_font, t, -1, GUI_FIELD_TEXT);
        } else if (ln->mark == P_ARROW) {
            gui_draw_arrow(s, ox + ln->mark_x + 6, y + ln->h / 2, b->open ? 1 : 3, GUI_FIELD_TEXT);
        }
        for (k = ln->first; k < ln->first + ln->n; k++) {
            struct piece *p = &v->p[k];
            int x = ox + p->x;
            switch (p->kind) {
            case P_RULE:
                gfx_fill(s, x, y + 4, p->w, 1, GUI_LINE);
                break;
            case P_CELL:
                gfx_fill(s, x, y, 1, ln->cell_box, GUI_LINE);
                break;
            case P_IMAGE:
                gfx_image(s, x, y, b->px, b->iw, b->ih, (unsigned)b->iw);
                break;
            default: {
                struct rrun *r = &b->run[p->run];
                const struct gfx_font *f = font_of(b->kind, r->style);
                unsigned fg = r->link ? GUI_LINK : r->rgb != GFX_NONE ? r->rgb : r->style & GUI_RS_ITALIC ? GUI_DIM
                              : GUI_FIELD_TEXT;
                int ty = y + ln->h - f->height - 1, s0 = 0, s1 = 0;
                if (r->style & GUI_RS_CODE && b->kind != GUI_RB_CODE)
                    gfx_fill(s, x - 1, ty, p->w + 2, f->height, GUI_FACE);
                if (v->sel_on && before(b0, a0, p->block, p->at + p->len) && before(p->block, p->at, b1, a1)) {
                    s0 = p->block == b0 && a0 > p->at ? a0 - p->at : 0;      /* (the part of it chosen) */
                    s1 = p->block == b1 && a1 < p->at + p->len ? a1 - p->at : p->len;
                    gfx_fill(s, x + gfx_text_width(f, r->text + p->off, s0), ty,
                             gfx_text_width(f, r->text + p->off + s0, s1 - s0), f->height, GUI_CHOSEN);
                }
                if (s1 > s0) {
                    gfx_text(s, x, ty, f, r->text + p->off, s0, fg);
                    gfx_text(s, x + gfx_text_width(f, r->text + p->off, s0), ty, f, r->text + p->off + s0, s1 - s0,
                             GUI_CHOSEN_TEXT);
                    gfx_text(s, x + gfx_text_width(f, r->text + p->off, s1), ty, f, r->text + p->off + s1, p->len - s1, fg);
                } else {
                    gfx_text(s, x, ty, f, r->text + p->off, p->len, fg);
                }
                if (r->link || r->style & GUI_RS_STRIKE)
                    gfx_fill(s, x, r->link ? ty + f->height - 1 : ty + f->height / 2, p->w, 1, fg);
                if (b->kind == GUI_RB_HEAD1 && k == ln->first + ln->n - 1 && (i + 1 == v->nl || v->l[i + 1].block != ln->block))
                    gfx_fill(s, ox + ln->x0, y + ln->h + 2, ln->x1 - ln->x0, 1, GUI_LINE);
                if (k == v->link && gui_is_focus(w)) {
                    struct gfx_rect fr = { x - 1, ty, p->w + 2, f->height };
                    gui_draw_focus(s, &fr);
                }
                break;
            }
            }
        }
    }
    gfx_clip(s, &clip);
    gui_sb_draw(s, &v->sb, 1);
}

/* ---- the mouse and the keys */
static int hit(struct rview *v, int ex, int ey, int *block, int *at, int *piece)  /* the place under the pointer */
{
    int i, k, x = ex - v->w.r.x, y = ey - v->w.r.y + v->sb.pos, best = -1;
    *piece = -1;
    for (i = 0; i < v->nl; i++) {
        struct rline *ln = &v->l[i];
        if (y < ln->y)
            break;
        if (y >= ln->y + ln->h && i + 1 < v->nl && y >= v->l[i + 1].y)
            continue;
        best = i;
        if (y < ln->y + ln->h)
            break;
    }
    if (best < 0)
        return 0;
    {
        struct rline *ln = &v->l[best];
        struct rblock *b = &v->doc->b[ln->block];
        int lastp = -1;
        for (k = ln->first; k < ln->first + ln->n; k++) {
            struct piece *p = &v->p[k];
            if (p->kind != P_TEXT) {
                if (x >= p->x && x < p->x + p->w)
                    *piece = k;
                continue;
            }
            lastp = k;
            if (x < p->x + p->w) {
                const struct gfx_font *f = font_of(b->kind, b->run[p->run].style);
                int n = 0;
                while (n < p->len && p->x + gfx_text_width(f, b->run[p->run].text + p->off, n + 1)
                                     - gui_char_w(f, (unsigned char)b->run[p->run].text[p->off + n]) / 2 <= x)
                    n++;
                *block = p->block, *at = p->at + n;
                if (x >= p->x)
                    *piece = k;
                return 1;
            }
        }
        if (lastp >= 0) {
            *block = v->p[lastp].block, *at = v->p[lastp].at + v->p[lastp].len;
            return 1;
        }
        *block = ln->block, *at = 0;
        return 1;
    }
}

static void activate(struct rview *v, int k)
{
    struct rblock *b = &v->doc->b[v->p[k].block];
    struct rrun *r = &b->run[v->p[k].run];
    if (!r->link)
        return;
    free(v->link_text);
    v->link_text = strdup(r->link);
    gui_send(&v->w, GUI_ON_ACTIVATE);
}

static void scroll_to_piece(struct rview *v, int k)
{
    int i;
    for (i = 0; i < v->nl; i++)
        if (k >= v->l[i].first && k < v->l[i].first + v->l[i].n) {
            if (v->l[i].y < v->sb.pos)
                v->sb.pos = v->l[i].y;
            else if (v->l[i].y + v->l[i].h > v->sb.pos + v->sb.page)
                v->sb.pos = v->l[i].y + v->l[i].h - v->sb.page;
            gui_sb_clamp(&v->sb);
            return;
        }
}

static int rv_event(struct gui_widget *w, struct gui_ev *e)
{
    struct rview *v = RV(w);
    int block, at, k;
    place(v);
    if (e->type != GUI_EV_KEY && gui_sb_event(w, &v->sb, e, 16)) {
        v->at_end = v->sb.pos + v->sb.page >= v->total;
        gui_damage(w);
        return 1;
    }
    switch (e->type) {
    case GUI_EV_PRESS:
        if (e->button != 1 || !v->doc)
            return 1;
        gui_focus(w);
        if (!hit(v, e->x, e->y, &block, &at, &k)) {
            v->sel_on = 0;
            gui_damage(w);
            return 1;
        }
        if (k >= 0 && v->p[k].kind == P_TEXT && v->doc->b[v->p[k].block].run[v->p[k].run].link) {
            v->link = k;
            activate(v, k);
            gui_damage(w);
            return 1;
        }
        if (v->doc->b[block].kind == GUI_RB_DETAILS) {
            v->doc->b[block].open = !v->doc->b[block].open;   /* the summary pressed: open or closed */
            v->laid_w = 0, v->sel_on = 0;
            gui_damage(w);
            return 1;
        }
        v->sel_on = 1, v->dragging = 1;
        v->sa_block = v->sb_block = block, v->sa_at = v->sb_at = at;
        gui_damage(w);
        return 1;
    case GUI_EV_MOVE:
        if (v->dragging && hit(v, e->x, e->y, &block, &at, &k)) {
            v->sb_block = block, v->sb_at = at;
            gui_damage(w);
        }
        return 1;
    case GUI_EV_RELEASE:
        v->dragging = 0;
        if (v->sel_on && v->sa_block == v->sb_block && v->sa_at == v->sb_at)
            v->sel_on = 0;
        gui_damage(w);
        return 1;
    case GUI_EV_WHEEL:
        v->sb.pos -= e->wheel * 48;
        gui_sb_clamp(&v->sb);
        v->at_end = v->sb.pos + v->sb.page >= v->total;
        gui_damage(w);
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    case GUI_EV_KEY:
        break;
    default:
        return 0;
    }
    if (e->mods & GLOS_MOD_ALT)
        return 0;
    if (e->mods & GLOS_MOD_CTRL) {
        if (e->ch == 3) {                       /* Ctrl+C */
            char *t = gui_richview_text(w, 1);
            if (t) {
                gui_clip_set(t, strlen(t));
                free(t);
            }
            return 1;
        }
        if (e->ch == 1 && v->doc && v->doc->n) {      /* Ctrl+A */
            v->sel_on = 1, v->sa_block = 0, v->sa_at = 0, v->sb_block = v->doc->n - 1, v->sb_at = 1 << 30;
            gui_damage(w);
            return 1;
        }
        return 0;
    }
    switch (e->key) {
    case GUI_K_UP: v->sb.pos -= 16; break;
    case GUI_K_DOWN: v->sb.pos += 16; break;
    case GUI_K_PGUP: v->sb.pos -= v->sb.page - 16; break;
    case GUI_K_PGDN: v->sb.pos += v->sb.page - 16; break;
    case GUI_K_HOME: v->sb.pos = 0; break;
    case GUI_K_END: v->sb.pos = v->total; break;
    case GUI_K_TAB:                             /* the next link (Shift: the one before); none left: on to the next
                                                   widget */
        for (k = v->link + (e->mods & GLOS_MOD_SHIFT ? -1 : 1); k >= 0 && k < v->np;
             k += e->mods & GLOS_MOD_SHIFT ? -1 : 1)
            if (v->p[k].kind == P_TEXT && v->doc->b[v->p[k].block].run[v->p[k].run].link
                && (k == 0 || v->p[k - 1].run != v->p[k].run || v->p[k - 1].block != v->p[k].block)) {
                v->link = k;
                scroll_to_piece(v, k);
                gui_damage(w);
                return 1;
            }
        v->link = -1;
        gui_damage(w);
        return 0;
    case GUI_K_ENTER:
    case GUI_K_KPENTER:
        if (v->link >= 0 && v->link < v->np) {
            activate(v, v->link);
            return 1;
        }
        return 0;
    default:
        return 0;
    }
    gui_sb_clamp(&v->sb);
    v->at_end = v->sb.pos + v->sb.page >= v->total;
    gui_damage(w);
    return 1;
}

static void rv_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 300, *ph = 200; }

static void rv_destroy(struct gui_widget *w)
{
    struct rview *v = RV(w);
    gui_rdoc_free(v->doc);
    free(v->p), free(v->l), free(v->link_text);
}

static const struct gui_class rv_class = { "richview", GUI_C_FOCUS, rv_draw, rv_event, rv_measure, NULL, rv_destroy };

struct gui_widget *gui_richview(struct gui_widget *parent)
{
    struct rview *v = (struct rview *)gui_new(parent, &rv_class, sizeof *v);
    v->link = -1;
    return &v->w;
}

void gui_richview_set(struct gui_widget *w, struct gui_rdoc *doc)
{
    struct rview *v = RV(w);
    if (v->doc != doc)
        gui_rdoc_free(v->doc);
    v->doc = doc;
    v->laid_w = 0, v->sel_on = 0, v->link = -1, v->sb.pos = 0;
    gui_damage(w);
}

struct gui_rdoc *gui_richview_doc(struct gui_widget *w) { return RV(w)->doc; }

void gui_richview_changed(struct gui_widget *w)        /* the document grew or changed: laid out again */
{
    struct rview *v = RV(w);
    v->laid_w = 0;
    if (v->link >= v->np)
        v->link = -1;
    gui_damage(w);
}

void gui_richview_follow(struct gui_widget *w, int on)  /* kept at the end as the document grows */
{
    RV(w)->follow = on != 0;
    RV(w)->at_end = 1;
    gui_damage(w);
}

const char *gui_richview_link(struct gui_widget *w) { return RV(w)->link_text ? RV(w)->link_text : ""; }

/* The document's text, or what is chosen of it (chosen 1): a line a block, a tab between cells (malloc'd). */
char *gui_richview_text(struct gui_widget *w, int chosen)
{
    struct rview *v = RV(w);
    struct gui_rdoc *d = v->doc;
    size_t room = 256, n = 0;
    char *t = malloc(room);
    int b0 = 0, a0 = 0, b1 = d ? d->n : 0, a1 = 0, i, r;
    if (!t || !d)
        return t;
    if (chosen) {
        if (!v->sel_on) {
            t[0] = 0;
            return t;
        }
        sel_range(v, &b0, &a0, &b1, &a1);
    }
    for (i = b0; i < d->n && i <= b1; i++) {
        int at = 0;
        if (i == b1 && chosen && a1 == 0)
            break;
        for (r = 0; r < d->b[i].nrun; r++) {
            struct rrun *rr = &d->b[i].run[r];
            int s0 = 0, s1 = rr->len;
            if (chosen && i == b0)
                s0 = a0 - at > 0 ? (a0 - at < rr->len ? a0 - at : rr->len) : 0;
            if (chosen && i == b1)
                s1 = a1 - at < rr->len ? (a1 - at > 0 ? a1 - at : 0) : rr->len;
            if (n + (size_t)(s1 - s0) + 4 > room) {
                char *nt;
                room = (n + (size_t)(s1 - s0) + 4) * 2;
                if (!(nt = realloc(t, room)))
                    return t[n] = 0, t;
                t = nt;
            }
            if (rr->style & GUI_RS_CELL && r > 0)
                t[n++] = '\t';
            if (s1 > s0)
                memcpy(t + n, rr->text + s0, (size_t)(s1 - s0)), n += (size_t)(s1 - s0);
            at += rr->len;
        }
        if (i < b1 || !chosen) {
            if (n + 3 > room) {
                char *nt = realloc(t, room * 2);
                if (!nt)
                    return t[n] = 0, t;
                t = nt, room *= 2;
            }
            t[n++] = '\r', t[n++] = '\n';
        }
    }
    t[n] = 0;
    return t;
}

/* ---- the chat view: the rich-text view's lines, kept to a limit, kept at the end as they come */
struct gui_widget *gui_chatview(struct gui_widget *parent, int limit)
{
    struct gui_widget *w = gui_richview(parent);
    gui_richview_set(w, gui_rdoc_new());
    gui_richview_follow(w, 1);
    RV(w)->limit = limit > 0 ? limit : 500;
    return w;
}

static void chat_trim(struct gui_widget *w)
{
    struct rview *v = RV(w);
    if (v->doc && v->limit && v->doc->n > v->limit) {
        int k = v->doc->n - v->limit;
        gui_rdoc_drop_first(v->doc, k);
        if (v->sel_on)                          /* (the choice moves with its blocks; gone with them, gone) */
            v->sa_block -= k, v->sb_block -= k, v->sel_on = v->sa_block >= 0 && v->sb_block >= 0;
    }
    gui_richview_changed(w);
}

void gui_chat_line(struct gui_widget *w, const char *who, unsigned rgb, const char *text)
{
    struct rview *v = RV(w);
    struct gui_rdoc *d = v->doc, *m;
    int i;
    if (!d)
        return;
    gui_rdoc_block(d, GUI_RB_PARA, 0);
    if (who && who[0]) {
        gui_rdoc_colour(d, rgb);
        gui_rdoc_text(d, who, -1, GUI_RS_BOLD, NULL);
        gui_rdoc_text(d, ": ", 2, GUI_RS_BOLD, NULL);
        gui_rdoc_colour(d, GFX_NONE);
    }
    m = gui_markdown(text, -1, NULL, NULL);     /* (its text as Markdown's inline: the first block's runs) */
    if (m && m->n) {
        for (i = 0; i < m->b[0].nrun; i++) {
            struct rrun *r = &m->b[0].run[i];
            gui_rdoc_text(d, r->text, r->len, r->style, r->link);
        }
    }
    gui_rdoc_free(m);
    chat_trim(w);
}

void gui_chat_markdown(struct gui_widget *w, const char *md)  /* a message of several blocks */
{
    struct rview *v = RV(w);
    struct gui_rdoc *m = gui_markdown(md, -1, NULL, NULL);
    int i, k;
    if (!v->doc || !m) {
        gui_rdoc_free(m);
        return;
    }
    for (i = 0; i < m->n; i++) {                /* (its blocks, moved over) */
        struct rblock *b = &m->b[i];
        gui_rdoc_block(v->doc, b->kind, b->depth);
        if (b->kind == GUI_RB_IMAGE && v->doc->n)
            v->doc->b[v->doc->n - 1].px = b->px, v->doc->b[v->doc->n - 1].iw = b->iw, v->doc->b[v->doc->n - 1].ih = b->ih;
        for (k = 0; k < b->nrun; k++)
            gui_rdoc_text(v->doc, b->run[k].text, b->run[k].len, b->run[k].style, b->run[k].link);
    }
    gui_rdoc_free(m);
    chat_trim(w);
}
