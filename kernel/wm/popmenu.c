/* The desktop's own pop-up menus (popmenu.h): items, the current one, keys and presses, the layout. */
#include "popmenu.h"

static int can(const struct popmenu *p, s32 i)
{
    return i >= 0 && (u32)i < p->n && !(p->it[i].flags & (PMI_OFF | PMI_SEP | PMI_TEXT));
}

void pm_clear(struct popmenu *p)
{
    p->n = 0;
    p->cur = -1;
    p->w = p->h = 0;
}

int pm_add(struct popmenu *p, const char *text, u32 flags, u32 data)
{
    struct pm_item *it;
    u32 i;
    if (p->n == PM_ITEMS)
        return -1;
    it = &p->it[p->n];
    for (i = 0; text && text[i] && i < PM_TEXT - 1; i++)
        it->text[i] = text[i];
    it->text[i] = 0;
    it->flags = flags;
    it->data = data;
    return (int)p->n++;
}

int pm_first(struct popmenu *p)
{
    u32 i;
    for (i = 0; i < p->n; i++)
        if (can(p, (s32)i))
            return p->cur = (s32)i;
    return p->cur = -1;
}

/* The text as shown: "&" before the letter taken out ("&&" is one). */
static u32 shown(const char *t, char *out, u32 room)
{
    u32 n = 0;
    for (; *t && n < room - 1; t++) {
        if (*t == '&' && t[1] != '&')
            continue;
        if (*t == '&')
            t++;
        out[n++] = *t;
    }
    out[n] = 0;
    return n;
}

void pm_layout(struct popmenu *p, int (*width)(const char *text, int len, int bold), s32 line_h, s32 min_w)
{
    char t[PM_TEXT];
    u32 i;
    s32 w = min_w, y;
    p->pad = 3;
    p->item_h = line_h + 6;
    p->sep_h = 7;
    for (i = 0, y = p->pad; i < p->n; i++) {
        if (!(p->it[i].flags & PMI_SEP)) {
            s32 tw = width(t, (int)shown(p->it[i].text, t, sizeof t), 0) + 20 + 16;    /* (a tick's room, and 16) */
            if (tw > w)
                w = tw;
        }
        y += p->it[i].flags & PMI_SEP ? p->sep_h : p->item_h;
    }
    p->w = w + 2 * p->pad;
    p->h = y + p->pad;
}

s32 pm_item_y(const struct popmenu *p, u32 i)
{
    s32 y = p->pad;
    u32 k;
    for (k = 0; k < i && k < p->n; k++)
        y += p->it[k].flags & PMI_SEP ? p->sep_h : p->item_h;
    return y;
}

int pm_at(const struct popmenu *p, s32 x, s32 y)
{
    u32 i;
    if (x < 0 || x >= p->w)
        return -1;
    for (i = 0; i < p->n; i++) {
        s32 top = pm_item_y(p, i), h = p->it[i].flags & PMI_SEP ? p->sep_h : p->item_h;
        if (y >= top && y < top + h)
            return can(p, (s32)i) ? (int)i : -1;
    }
    return -1;
}

static int lower(u32 c) { return c >= 'A' && c <= 'Z' ? (int)(c + 32) : (int)c; }

/* An item's letter: the one after "&", else its first. */
static int letter_of(const struct popmenu *p, u32 i)
{
    const char *t = p->it[i].text, *a;
    for (a = t; *a; a++) {
        if (*a == '&' && a[1] && a[1] != '&')
            return lower((u8)a[1]);
        if (*a == '&')
            a++;
    }
    return lower((u8)t[0]);
}

/* The next item after the current one (round to the start) that can be chosen and has the letter c, or -1. */
static int by_letter(const struct popmenu *p, u32 c)
{
    u32 k;
    for (k = 1; k <= p->n; k++) {
        s32 i = (p->cur + (s32)k + (s32)p->n) % (s32)p->n;
        if (can(p, i) && letter_of(p, (u32)i) == lower(c))
            return (int)i;
    }
    return -1;
}

static void step(struct popmenu *p, int by)
{
    u32 k;
    s32 i = p->cur;
    for (k = 0; k < p->n; k++) {
        i = i < 0 ? (by > 0 ? 0 : (s32)p->n - 1) : (i + by + (s32)p->n) % (s32)p->n;
        if (can(p, i)) {
            p->cur = i;
            return;
        }
    }
}

int pm_key(struct popmenu *p, u32 key, u32 ch)
{
    int i;
    switch (key) {
    case 0x01:                                  /* Esc */
        return PM_CANCEL;
    case 0x1C: case 0x11C:                      /* Enter */
        return can(p, p->cur) ? (int)p->cur : PM_NONE;
    case 0x48: case 0x148:                      /* Up */
        step(p, -1);
        return PM_NONE;
    case 0x50: case 0x150:                      /* Down */
        step(p, 1);
        return PM_NONE;
    case 0x47: case 0x147:                      /* Home */
        p->cur = -1;
        step(p, 1);
        return PM_NONE;
    case 0x4F: case 0x14F:                      /* End */
        p->cur = -1;
        step(p, -1);
        return PM_NONE;
    }
    if (ch > ' ' && ch < 127 && (i = by_letter(p, ch)) >= 0) {
        int more = 0;
        u32 k;
        for (k = 0; k < p->n; k++)              /* (a letter only one item has chooses it; else it moves there) */
            if ((s32)k != i && can(p, (s32)k) && letter_of(p, k) == lower(ch))
                more = 1;
        p->cur = i;
        return more ? PM_NONE : i;
    }
    return PM_NONE;
}
