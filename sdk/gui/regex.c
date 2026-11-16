/* Finding text (toolkit 2, step C6; docs/v1/toolkit-2.md, Find and replace): the matching every app's Find uses,
 * so that they search alike. Plain text, or a regular expression:
 *
 *   .  any character but a line's end      [abc] [a-z] [^0-9]  one of (or none of) a set
 *   *  +  ?  the one before, any times, once or more, or not at all (as many as can be; *? +? ?? as few)
 *   ^  $  a line's start and end           \d \w \s (and \D \W \S): digits, word characters, spaces
 *   (...)  a group: \1 to \9 in what replaces the found text (\0 or & the whole)
 *   a|b  either                            \ before any other character: that character itself
 *
 * Matching case or not, and whole words only (the found text neither begins nor ends inside a word), are flags.
 * Bytes are matched as they are (the text is UTF-8 or the editor's Latin-1: letters past ASCII match their own
 * case only). A backtracking matcher, its depth and steps bounded so that no pattern hangs an app. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

#define NODES 160
#define STEPS 200000                            /* a search's backtracking steps at most (then: not found) */

enum { N_CHAR, N_ANY, N_SET, N_BOL, N_EOL, N_GROUP, N_ALT, N_END };

struct node {
    unsigned char kind, lazy;
    unsigned char c;
    short min, max;                             /* how many times (max -1: no end) */
    short group;                                /* N_GROUP: its number (1..9; 0: not caught); N_ALT, N_GROUP: ... */
    short sub, next;                            /* the first node inside (N_GROUP, N_ALT's other way), the one after */
    unsigned char set[32];                      /* N_SET: a bit a byte */
};

struct re {
    struct node n[NODES];
    int count, groups, icase, bad;
    const char *text;
    int len, steps;
    int cap[10][2];
};

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int word_ch(int c) { return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_' || c >= 0x80; }

static int add(struct re *r, int kind)
{
    struct node *n;
    if (r->count == NODES) {
        r->bad = 1;
        return 0;
    }
    n = &r->n[r->count];
    memset(n, 0, sizeof *n);
    n->kind = (unsigned char)kind;
    n->min = n->max = 1;
    n->sub = n->next = -1;
    return r->count++;
}

static void set_bit(unsigned char *s, int c) { s[c >> 3] |= (unsigned char)(1 << (c & 7)); }

static void set_class(unsigned char *s, int k)  /* \d \w \s */
{
    int c;
    for (c = 0; c < 256; c++)
        if ((k == 'd' && c >= '0' && c <= '9') || (k == 'w' && word_ch(c))
            || (k == 's' && (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v')))
            set_bit(s, c);
}

static int parse_alt(struct re *r, const char **p, int depth);

/* A sequence of atoms (each with its count), up to | or ) or the end: its first node (or an N_END). */
static int parse_seq(struct re *r, const char **p, int depth)
{
    int first = -1, last = -1, i, k;
    while (**p && **p != '|' && **p != ')' && !r->bad) {
        const char *s = *p;
        int c = (unsigned char)*s++;
        if (c == '.') {
            i = add(r, N_ANY);
        } else if (c == '^') {
            i = add(r, N_BOL);
        } else if (c == '$') {
            i = add(r, N_EOL);
        } else if (c == '(') {
            i = add(r, N_GROUP);
            r->n[i].group = (short)(r->groups < 9 ? ++r->groups : 0);
            r->n[i].sub = (short)parse_alt(r, &s, depth + 1);
            if (*s != ')') {
                r->bad = 1;
                return -1;
            }
            s++;
        } else if (c == '[') {
            int neg = 0, prev = -1;
            i = add(r, N_SET);
            if (*s == '^')
                neg = 1, s++;
            if (*s == ']')                      /* (a ] first is itself) */
                set_bit(r->n[i].set, ']'), prev = ']', s++;
            while (*s && *s != ']') {
                int a = (unsigned char)*s++;
                if (a == '\\' && *s) {
                    a = (unsigned char)*s++;
                    if (a == 'd' || a == 'w' || a == 's') {
                        set_class(r->n[i].set, a);
                        prev = -1;
                        continue;
                    }
                }
                if (a == '-' && prev >= 0 && *s && *s != ']') {
                    int b = (unsigned char)*s++;
                    for (k = prev; k <= b; k++)
                        set_bit(r->n[i].set, k);
                    prev = -1;
                    continue;
                }
                set_bit(r->n[i].set, a);
                prev = a;
            }
            if (*s != ']') {
                r->bad = 1;
                return -1;
            }
            s++;
            if (r->icase)                       /* (any case: both cases in the set, before it is turned over) */
                for (k = 'A'; k <= 'Z'; k++)
                    if (r->n[i].set[k >> 3] & (1 << (k & 7)) || r->n[i].set[(k + 32) >> 3] & (1 << ((k + 32) & 7))) {
                        set_bit(r->n[i].set, k);
                        set_bit(r->n[i].set, k + 32);
                    }
            if (neg)
                for (k = 0; k < 32; k++)
                    r->n[i].set[k] = (unsigned char)~r->n[i].set[k];
        } else if (c == '*' || c == '+' || c == '?') {
            r->bad = 1;                         /* (nothing before it to repeat) */
            return -1;
        } else {
            if (c == '\\' && *s) {
                c = (unsigned char)*s++;
                if (c == 'd' || c == 'w' || c == 's' || c == 'D' || c == 'W' || c == 'S') {
                    i = add(r, N_SET);
                    set_class(r->n[i].set, lower(c));
                    if (c < 'a')
                        for (k = 0; k < 32; k++)
                            r->n[i].set[k] = (unsigned char)~r->n[i].set[k];
                    goto counted;
                }
                if (c == 'n')
                    c = '\n';
                else if (c == 't')
                    c = '\t';
            }
            i = add(r, N_CHAR);
            r->n[i].c = (unsigned char)c;
        }
    counted:
        if (r->bad)
            return -1;
        if (*s == '*' || *s == '+' || *s == '?') {
            struct node *n = &r->n[i];
            if (n->kind == N_BOL || n->kind == N_EOL) {
                r->bad = 1;
                return -1;
            }
            n->min = (short)(*s == '+' ? 1 : 0);
            n->max = (short)(*s == '?' ? 1 : -1);
            s++;
            if (*s == '?')
                n->lazy = 1, s++;
        }
        *p = s;
        if (last >= 0)
            r->n[last].next = (short)i;
        else
            first = i;
        last = i;
    }
    i = add(r, N_END);                          /* (each sequence ends in an N_END: the way back out) */
    if (last >= 0)
        r->n[last].next = (short)i;
    else
        first = i;
    return first;
}

/* Sequences with | between them: an N_ALT chain, or the one sequence. */
static int parse_alt(struct re *r, const char **p, int depth)
{
    int first, alt, a;
    if (depth > 20) {
        r->bad = 1;
        return -1;
    }
    first = parse_seq(r, p, depth);
    if (**p != '|' || r->bad)
        return first;
    alt = add(r, N_ALT);
    r->n[alt].sub = (short)first;
    a = alt;
    while (**p == '|' && !r->bad) {
        int seq;
        (*p)++;
        seq = parse_seq(r, p, depth);
        if (**p == '|') {                       /* (more: another N_ALT as this one's other way) */
            int b = add(r, N_ALT);
            r->n[b].sub = (short)seq;
            r->n[a].next = (short)b;
            a = b;
        } else {
            r->n[a].next = (short)seq;
        }
    }
    return alt;
}

static int one(struct re *r, const struct node *n, int at)       /* does the node's single step match at at */
{
    int c;
    if (at >= r->len)
        return 0;
    c = (unsigned char)r->text[at];
    switch (n->kind) {
    case N_ANY:
        return c != '\n' && c != '\r';
    case N_CHAR:
        return r->icase ? lower(c) == lower(n->c) : c == n->c;
    case N_SET:                                 /* (any case: folded into the set when it was read) */
        return (n->set[c >> 3] & (1 << (c & 7))) != 0;
    }
    return 0;
}

/* What follows a step: a node to go on at (K_NODE), or a group's inside that has ended (K_GROUP: once more, or
   on after it). NULL: the whole pattern has matched. */
enum { K_NODE, K_GROUP };
struct cont {
    int kind, node;
    const struct node *g;
    int done, start;                            /* K_GROUP: the passes made with this one, and where it began */
    const struct cont *up;
};

static int run(struct re *r, int i, int at, const struct cont *k);
static int group_loop(struct re *r, const struct node *g, int at, int done, const struct cont *after);

static int go_on(struct re *r, int at, const struct cont *k)
{
    int e, o0, o1, num;
    if (!k)
        return at;
    if (k->kind == K_NODE)
        return run(r, k->node, at, k->up);
    if (at == k->start && k->done > k->g->min)  /* (a pass that matched nothing, past the least: no further) */
        return -1;
    num = k->g->group;
    o0 = r->cap[num][0], o1 = r->cap[num][1];
    if (num)
        r->cap[num][0] = k->start, r->cap[num][1] = at;
    if ((e = group_loop(r, k->g, at, k->done, k->up)) >= 0)
        return e;
    if (num)
        r->cap[num][0] = o0, r->cap[num][1] = o1;
    return -1;
}

/* A group done so many times: its inside once more (as many as can be, or as few, lazy), or what follows it. */
static int group_loop(struct re *r, const struct node *g, int at, int done, const struct cont *after)
{
    int e, more = (g->max < 0 || done < g->max) && done < 1000;
    struct cont again;
    again.kind = K_GROUP, again.node = -1, again.g = g, again.done = done + 1, again.start = at, again.up = after;
    if (done < g->min)
        return more ? run(r, g->sub, at, &again) : -1;
    if (g->lazy) {
        if ((e = go_on(r, at, after)) >= 0)
            return e;
        return more ? run(r, g->sub, at, &again) : -1;
    }
    if (more && (e = run(r, g->sub, at, &again)) >= 0)
        return e;
    return go_on(r, at, after);
}

static int run(struct re *r, int i, int at, const struct cont *k)
{
    const struct node *n;
    struct cont after;
    int e, count, max;
    if (++r->steps > STEPS)
        return -1;
    if (i < 0)
        return go_on(r, at, k);
    n = &r->n[i];
    switch (n->kind) {
    case N_END:
        return go_on(r, at, k);
    case N_BOL:
        return at == 0 || r->text[at - 1] == '\n' ? run(r, n->next, at, k) : -1;
    case N_EOL:
        return at == r->len || r->text[at] == '\n' || r->text[at] == '\r' ? run(r, n->next, at, k) : -1;
    case N_ALT:
        if ((e = run(r, n->sub, at, k)) >= 0)
            return e;
        return run(r, n->next, at, k);
    case N_GROUP:
        after.kind = K_NODE, after.node = n->next, after.g = NULL, after.done = after.start = 0, after.up = k;
        return group_loop(r, n, at, 0, &after);
    }
    max = n->max < 0 ? r->len - at : n->max;    /* a single-step atom, counted */
    for (count = 0; count < max && one(r, n, at + count); count++)
        ;
    if (count < n->min)
        return -1;
    if (n->lazy) {
        int c;
        for (c = n->min; c <= count; c++)
            if ((e = run(r, n->next, at + c, k)) >= 0)
                return e;
        return -1;
    }
    for (; count >= n->min; count--)
        if ((e = run(r, n->next, at + count, k)) >= 0)
            return e;
    return -1;
}

/* An N_ALT's ways each end in an N_END, which goes on with what follows the N_ALT: the parser links the ways' ends
   to nothing, so a sequence's N_END is the place to go on from (the continuation k). */

static int is_word(const char *t, int len, int at) { return at >= 0 && at < len && word_ch((unsigned char)t[at]); }

static int words_ok(const char *t, int len, int a, int b)
{
    return !(is_word(t, len, a - 1) && is_word(t, len, a)) && !(is_word(t, len, b - 1) && is_word(t, len, b));
}

/* The first match at or after `from` (GUI_FIND_BACK: the last that starts before it). 1, 0, or -1 (the pattern is
   wrong). */
int gui_match(const char *what, unsigned flags, const char *text, int len, int from, struct gui_found *f)
{
    static struct re r;                         /* (big: not on the stack) */
    int at, first = 0, found = 0, wl = (int)strlen(what), k;
    struct gui_found best;
    memset(&best, 0, sizeof best);
    if (!wl)
        return 0;
    if (flags & GUI_FIND_REGEX) {
        const char *p = what;
        memset(&r, 0, sizeof r);
        r.icase = !(flags & GUI_FIND_CASE);    /* (before the sets are read) */
        first = parse_alt(&r, &p, 0);
        if (r.bad || *p)
            return -1;
        r.text = text, r.len = len;
    }
    for (at = flags & GUI_FIND_BACK ? 0 : from; at <= len; at++) {
        int end = -1;
        if ((flags & GUI_FIND_BACK) && at >= from)
            break;
        if (flags & GUI_FIND_REGEX) {
            memset(r.cap, -1, sizeof r.cap);
            r.steps = 0;
            end = run(&r, first, at, NULL);
        } else if (at + wl <= len) {
            for (k = 0; k < wl; k++) {
                int a = (unsigned char)text[at + k], b = (unsigned char)what[k];
                if (a != b && ((flags & GUI_FIND_CASE) || lower(a) != lower(b)))
                    break;
            }
            if (k == wl)
                end = at + wl;
        }
        if (end < 0 || ((flags & GUI_FIND_WORDS) && !words_ok(text, len, at, end)))
            continue;
        best.start = at, best.end = end;
        for (k = 0; k < 10; k++) {
            best.group[k][0] = flags & GUI_FIND_REGEX ? r.cap[k][0] : -1;
            best.group[k][1] = flags & GUI_FIND_REGEX ? r.cap[k][1] : -1;
        }
        best.group[0][0] = at, best.group[0][1] = end;
        found = 1;
        if (!(flags & GUI_FIND_BACK))
            break;
    }
    if (found && f)
        *f = best;
    return found;
}

/* What replaces the found text: `with`, and with GUI_FIND_REGEX \0 to \9 (or &) the groups. Its length, or -1. */
int gui_match_replace(const char *with, unsigned flags, const struct gui_found *f, const char *text, char *out, int room)
{
    int n = 0;
    const char *p;
    for (p = with; *p; p++) {
        int g = -1, a, b;
        if ((flags & GUI_FIND_REGEX) && *p == '\\' && p[1] >= '0' && p[1] <= '9')
            g = *++p - '0';
        else if ((flags & GUI_FIND_REGEX) && *p == '&')
            g = 0;
        else if ((flags & GUI_FIND_REGEX) && *p == '\\' && p[1])
            p++;                                /* (\& and \\: the character itself) */
        if (g < 0) {
            if (n + 1 >= room)
                return -1;
            out[n++] = *p;
            continue;
        }
        a = f->group[g][0], b = f->group[g][1];
        if (a < 0 || b < a)
            continue;                           /* (a group that matched nothing: nothing) */
        if (n + (b - a) >= room)
            return -1;
        memcpy(out + n, text + a, (size_t)(b - a));
        n += b - a;
    }
    if (n >= room)
        return -1;
    out[n] = 0;
    return n;
}
