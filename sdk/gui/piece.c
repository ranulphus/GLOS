/* A piece table (piece.h): the code editor's text. The text as loaded is kept as it was, what is typed is added to
 * a second buffer, and the text is a list of pieces of the two. Each piece counts its line ends, so a line's start
 * is found by adding up pieces, not reading bytes; the piece last found is kept, so reading on from it is cheap. An
 * edit splits at most one piece in two and adds at most one. */
#include <stdlib.h>
#include <string.h>

#include "piece.h"

static size_t count_lines(const char *s, size_t n)
{
    size_t k = 0;
    const char *e = s + n;
    while (s < e && (s = memchr(s, '\n', (size_t)(e - s))) != NULL)
        k++, s++;
    return k;
}

static const char *text_of(const struct ptable *t, const struct pt_piece *p)
{
    return (p->add ? t->add : t->orig) + p->start;
}

static int room_for(struct ptable *t, int more)
{
    if (t->n + more > t->room) {
        int room = (t->n + more) * 2 + 16;
        struct pt_piece *np = realloc(t->p, (size_t)room * sizeof *np);
        if (!np)
            return -1;
        t->p = np, t->room = room;
    }
    return 0;
}

int pt_init(struct ptable *t, char *text, size_t n)
{
    memset(t, 0, sizeof *t);
    t->orig = text, t->orig_len = text ? n : 0;
    if (t->orig_len) {
        if (room_for(t, 1) != 0)
            return -1;
        t->p[0].add = 0, t->p[0].start = 0, t->p[0].len = n, t->p[0].lines = count_lines(text, n);
        t->n = 1;
        t->length = n, t->lines = t->p[0].lines;
    }
    return 0;
}

void pt_free(struct ptable *t)
{
    free(t->orig), free(t->add), free(t->p);
    memset(t, 0, sizeof *t);
}

size_t pt_length(const struct ptable *t) { return t->length; }

size_t pt_lines(const struct ptable *t) { return t->lines + 1; }

/* The piece byte `at` is in (at == the length: n), and where that piece starts. */
static int find(struct ptable *t, size_t at, size_t *start)
{
    int i = 0;
    size_t s = 0;
    if (t->hint > 0 && t->hint < t->n && t->hint_at <= at)    /* (from the piece last found, if it is no further) */
        i = t->hint, s = t->hint_at;
    for (; i < t->n; s += t->p[i].len, i++)
        if (at < s + t->p[i].len)
            break;
    if (i < t->n)
        t->hint = i, t->hint_at = s;
    *start = s;
    return i;
}

size_t pt_line_start(struct ptable *t, size_t line)
{
    size_t s = 0, seen = 0;
    int i;
    if (line == 0)
        return 0;
    if (line > t->lines)
        return t->length;
    for (i = 0; i < t->n; s += t->p[i].len, i++) {
        if (seen + t->p[i].lines >= line) {     /* the line's start is after a '\n' in this piece */
            const char *b = text_of(t, &t->p[i]), *q = b;
            size_t want = line - seen;
            while (want--) {
                q = memchr(q, '\n', t->p[i].len - (size_t)(q - b));
                q++;
            }
            return s + (size_t)(q - b);
        }
        seen += t->p[i].lines;
    }
    return t->length;
}

size_t pt_line_of(struct ptable *t, size_t at)
{
    size_t s = 0, line = 0;
    int i;
    if (at > t->length)
        at = t->length;
    for (i = 0; i < t->n; s += t->p[i].len, i++) {
        if (at < s + t->p[i].len)
            return line + count_lines(text_of(t, &t->p[i]), at - s);
        line += t->p[i].lines;
    }
    return line;
}

size_t pt_read(struct ptable *t, size_t at, char *out, size_t n)
{
    size_t s, got = 0;
    int i;
    if (at >= t->length)
        return 0;
    if (n > t->length - at)
        n = t->length - at;
    for (i = find(t, at, &s); i < t->n && got < n; s += t->p[i].len, i++) {
        size_t from = at + got - s, k = t->p[i].len - from;
        if (k > n - got)
            k = n - got;
        memcpy(out + got, text_of(t, &t->p[i]) + from, k);
        got += k;
    }
    return got;
}

int pt_byte(struct ptable *t, size_t at)
{
    size_t s;
    int i;
    if (at >= t->length)
        return -1;
    i = find(t, at, &s);
    return (unsigned char)text_of(t, &t->p[i])[at - s];
}

int pt_insert(struct ptable *t, size_t at, const char *text, size_t n)
{
    size_t s, lines;
    int i;
    struct pt_piece np;
    if (!n)
        return 0;
    if (at > t->length)
        at = t->length;
    if (t->add_len + n > t->add_room) {         /* (what is added goes on the end of the second buffer) */
        size_t room = (t->add_len + n) * 2 + 256;
        char *a = realloc(t->add, room);
        if (!a)
            return -1;
        t->add = a, t->add_room = room;
    }
    if (room_for(t, 2) != 0)
        return -1;
    memcpy(t->add + t->add_len, text, n);
    lines = count_lines(text, n);
    np.add = 1, np.start = t->add_len, np.len = n, np.lines = lines;
    t->add_len += n;
    i = find(t, at, &s);
    if (i > 0 && at == s && t->p[i - 1].add && t->p[i - 1].start + t->p[i - 1].len == np.start) {
        t->p[i - 1].len += n;                   /* (typing on at the end of what was typed: the same piece) */
        t->p[i - 1].lines += lines;
    } else if (i < t->n && at > s) {            /* inside a piece: split it, the new one between */
        struct pt_piece *p = &t->p[i], tail = *p;
        size_t head = at - s;
        tail.start += head, tail.len -= head;
        tail.lines = count_lines(text_of(t, &tail), tail.len);
        p->len = head, p->lines -= tail.lines;
        memmove(&t->p[i + 3], &t->p[i + 1], (size_t)(t->n - i - 1) * sizeof *t->p);
        t->p[i + 1] = np, t->p[i + 2] = tail;
        t->n += 2;
    } else {                                    /* between pieces (or at the end) */
        memmove(&t->p[i + 1], &t->p[i], (size_t)(t->n - i) * sizeof *t->p);
        t->p[i] = np;
        t->n++;
    }
    t->length += n, t->lines += lines;
    t->hint = 0, t->hint_at = 0;
    return 0;
}

int pt_delete(struct ptable *t, size_t at, size_t n)
{
    size_t s;
    int i;
    if (at >= t->length || !n)
        return 0;
    if (n > t->length - at)
        n = t->length - at;
    if (room_for(t, 1) != 0)
        return -1;
    i = find(t, at, &s);
    t->length -= n;
    while (n && i < t->n) {
        struct pt_piece *p = &t->p[i];
        size_t from = at - s, k = p->len - from < n ? p->len - from : n;
        size_t gone = count_lines(text_of(t, p) + from, k);
        t->lines -= gone;
        if (from == 0 && k == p->len) {         /* the whole piece */
            memmove(&t->p[i], &t->p[i + 1], (size_t)(t->n - i - 1) * sizeof *t->p);
            t->n--;
        } else if (from == 0) {                 /* its head (and that is all: what was left to go ends in it) */
            p->start += k, p->len -= k, p->lines -= gone;
        } else if (from + k == p->len) {        /* its tail */
            p->len -= k, p->lines -= gone;
            s += p->len, i++;
        } else {                                /* its middle: in two */
            struct pt_piece tail = *p;
            tail.start += from + k, tail.len -= from + k;
            tail.lines = count_lines(text_of(t, &tail), tail.len);
            p->len = from, p->lines = count_lines(text_of(t, p), from);
            memmove(&t->p[i + 2], &t->p[i + 1], (size_t)(t->n - i - 1) * sizeof *t->p);
            t->p[i + 1] = tail;
            t->n++;
        }
        n -= k;
    }
    t->hint = 0, t->hint_at = 0;
    return 0;
}

char *pt_text(struct ptable *t)
{
    char *s = malloc(t->length + 1);
    if (!s)
        return NULL;
    pt_read(t, 0, s, t->length);
    s[t->length] = 0;
    return s;
}
