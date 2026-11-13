/* The line editor of a shell's session (edit.h). The line is drawn whole after every change: the old rows
 * cleared from the last up to the first, the prompt and the line written, the cursor put where it belongs. A
 * terminal that has just written its last column keeps the cursor there until the next character (xterm's
 * way): when the text ends at a row's end a new row is begun, so the cursor's row is what the sums say. */
#include <string.h>

#include "edit.h"

static void put(struct ed *e, const char *s) { e->put(e->ctx, s, (int)strlen(s)); }

/* ESC [ n LETTER (nothing for n 0) */
static void seq(struct ed *e, int n, char letter)
{
    char b[16];
    int at = (int)sizeof b;
    if (n <= 0)
        return;
    b[--at] = letter;
    do
        b[--at] = (char)('0' + n % 10);
    while (n /= 10);
    b[--at] = '[';
    b[--at] = 27;
    e->put(e->ctx, b + at, (int)sizeof b - at);
}

/* The columns a prompt takes: its bytes less the sequences in it (ESC [ ... a letter) and what begins a row. */
static int columns(const char *s, int n)
{
    int i, cols = 0;
    for (i = 0; i < n; i++) {
        if (s[i] == 27 && i + 1 < n && s[i + 1] == '[') {
            for (i += 2; i < n && !((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z')); i++)
                ;
        } else if (s[i] == '\n' || s[i] == '\r') {
            cols = 0;
        } else if ((unsigned char)s[i] >= 32) {
            cols++;
        }
    }
    return cols;
}

static void draw(struct ed *e)
{
    int cols = e->cols > 0 ? e->cols : 80, at = e->prompt_cols + e->cur, all = e->prompt_cols + e->len, rows, row, i;
    if (e->drawn) {                             /* what was there, from its last row up */
        seq(e, e->rows - e->row, 'B');
        for (i = 1; i < e->rows; i++)
            put(e, "\r\033[K\033[A");
    }
    put(e, "\r\033[K");
    e->put(e->ctx, e->prompt, e->prompt_len);
    e->put(e->ctx, e->line, e->len);
    rows = (all + cols - 1) / cols;
    if (!rows)
        rows = 1;
    if (all && all % cols == 0) {               /* the last column written: the next row begun */
        put(e, "\r\n");
        rows++;
    }
    row = at / cols + 1;
    seq(e, rows - row, 'A');
    put(e, "\r");
    seq(e, at % cols, 'C');
    e->rows = rows, e->row = row, e->drawn = 1;
}

void ed_init(struct ed *e, void (*putf)(void *ctx, const char *s, int n), void *ctx, int cols)
{
    memset(e, 0, sizeof *e);
    e->put = putf, e->ctx = ctx, e->cols = cols;
}

void ed_size(struct ed *e, int cols)
{
    e->cols = cols;
    if (e->drawn) {                             /* (the terminal has laid its rows out again: begin below them) */
        e->rows = e->row = 1;
        draw(e);
    }
}

void ed_begin(struct ed *e, const char *prompt, int n)
{
    int i;
    for (i = n; i > 0 && prompt[i - 1] != '\n'; i--)
        ;
    if (i) {                                    /* (rows of the prompt above its last: written once, here) */
        e->put(e->ctx, prompt, i);
        prompt += i, n -= i;
    }
    if (n > ED_PROMPT_MAX)
        prompt += n - ED_PROMPT_MAX, n = ED_PROMPT_MAX;
    memcpy(e->prompt, prompt, (size_t)n);
    e->prompt_len = n;
    e->prompt_cols = columns(prompt, n);
    e->len = e->cur = 0;
    e->line[0] = 0;
    e->drawn = 0, e->rows = e->row = 1;
    e->esc = 0;
    draw(e);
}

void ed_set(struct ed *e, const char *line)
{
    size_t n = strlen(line);
    if (n > ED_LINE_MAX)
        n = ED_LINE_MAX;
    memcpy(e->line, line, n);
    e->line[n] = 0;
    e->len = e->cur = (int)n;
    draw(e);
}

static void cut(struct ed *e, int from, int to)
{
    if (from >= to)
        return;
    memmove(e->line + from, e->line + to, (size_t)(e->len - to) + 1);
    e->len -= to - from;
    e->cur = from;
    draw(e);
}

static void move(struct ed *e, int to)
{
    if (to < 0)
        to = 0;
    if (to > e->len)
        to = e->len;
    if (to != e->cur) {
        e->cur = to;
        draw(e);
    }
}

void ed_away(struct ed *e)
{
    if (!e->drawn)
        return;
    e->cur = e->len;
    draw(e);
    e->drawn = 0;
}

void ed_back(struct ed *e)
{
    e->drawn = 0, e->rows = e->row = 1;
    draw(e);
}

/* The cursor past the line's end, a mark if any, and a new row: the line is done with (Enter, Ctrl+C). */
static void leave(struct ed *e, const char *mark)
{
    int cols = e->cols > 0 ? e->cols : 80, all = e->prompt_cols + e->len;
    ed_away(e);
    put(e, mark);
    if (*mark || !all || all % cols)            /* (a line that ended at a row's end has begun the next already) */
        put(e, "\r\n");
}

/* A sequence's last byte: what the key was. */
static int sequence(struct ed *e, unsigned char c)
{
    switch (c) {
    case 'A': return ED_UP;
    case 'B': return ED_DOWN;
    case 'C': move(e, e->cur + 1); break;
    case 'D': move(e, e->cur - 1); break;
    case 'H': move(e, 0); break;
    case 'F': move(e, e->len); break;
    case '~':
        if (e->arg == 1 || e->arg == 7)
            move(e, 0);
        else if (e->arg == 4 || e->arg == 8)
            move(e, e->len);
        else if (e->arg == 3)
            cut(e, e->cur, e->cur < e->len ? e->cur + 1 : e->cur);
        break;
    }
    return ED_NOTHING;
}

int ed_key(struct ed *e, unsigned char c)
{
    int was_cr = e->cr, i;
    e->cr = 0;
    if (e->esc == 1) {
        e->esc = c == '[' || c == 'O' ? 2 : 0;
        e->arg = 0;
        return ED_NOTHING;
    }
    if (e->esc == 2) {
        if (c >= '0' && c <= '9') {
            e->arg = e->arg * 10 + (c - '0');
            return ED_NOTHING;
        }
        if (c == ';')                           /* (a modifier follows: the key is as without it) */
            return ED_NOTHING;
        e->esc = 0;
        return sequence(e, c);
    }
    switch (c) {
    case 27:
        e->esc = 1;
        return ED_NOTHING;
    case '\r':
        e->cr = 1;
        leave(e, "");
        return ED_ENTER;
    case '\n':
        if (was_cr)
            return ED_NOTHING;
        leave(e, "");
        return ED_ENTER;
    case '\t':
        return ED_TAB;
    case 3:                                     /* Ctrl+C */
        leave(e, "^C");
        e->len = e->cur = 0;
        e->line[0] = 0;
        return ED_BREAK;
    case 4:                                     /* Ctrl+D: the end on an empty line, else Delete */
        if (!e->len)
            return ED_END;
        cut(e, e->cur, e->cur < e->len ? e->cur + 1 : e->cur);
        return ED_NOTHING;
    case 1: move(e, 0); return ED_NOTHING;      /* Ctrl+A, Ctrl+E */
    case 5: move(e, e->len); return ED_NOTHING;
    case 2: move(e, e->cur - 1); return ED_NOTHING;     /* Ctrl+B, Ctrl+F */
    case 6: move(e, e->cur + 1); return ED_NOTHING;
    case 11: cut(e, e->cur, e->len); return ED_NOTHING; /* Ctrl+K: to the end */
    case 21: cut(e, 0, e->cur); return ED_NOTHING;      /* Ctrl+U: from the start */
    case 23:                                    /* Ctrl+W: the word before */
        for (i = e->cur; i > 0 && e->line[i - 1] == ' '; i--)
            ;
        for (; i > 0 && e->line[i - 1] != ' '; i--)
            ;
        cut(e, i, e->cur);
        return ED_NOTHING;
    case 12:                                    /* Ctrl+L */
        put(e, "\033[H\033[2J");
        e->drawn = 0, e->rows = e->row = 1;
        draw(e);
        return ED_CLEAR;
    case 8: case 127:                           /* Backspace, as either */
        if (e->cur)
            cut(e, e->cur - 1, e->cur);
        return ED_NOTHING;
    }
    if (c < 32 || e->len >= ED_LINE_MAX)
        return ED_NOTHING;
    memmove(e->line + e->cur + 1, e->line + e->cur, (size_t)(e->len - e->cur) + 1);
    e->line[e->cur++] = (char)c;
    e->len++;
    if (e->cur == e->len && e->drawn && (e->prompt_cols + e->len) % (e->cols > 0 ? e->cols : 80)) {
        e->put(e->ctx, e->line + e->cur - 1, 1);        /* (at the end, not at a row's: the character alone) */
        return ED_NOTHING;
    }
    draw(e);
    return ED_NOTHING;
}
