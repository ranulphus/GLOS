/* Notifications (notes.h): the queue, the time each has on the screen, the layout. */
#include "notes.h"

#define TICKS 1024u                     /* a second */
#define PAD   8
#define LINE  14                        /* a line's height */

static void gone(struct notes *s)       /* the first leaves the queue */
{
    u32 i;
    for (i = 1; i < s->n; i++)
        s->q[i - 1] = s->q[i];
    s->n--;
    s->q[s->n].id = 0;
    s->shown = 0;
}

u32 notes_add(struct notes *s, const struct note *n, u32 owner)
{
    struct note *q;
    u32 secs = n->life;
    if (s->n == NOTES_MAX)
        return 0;
    q = &s->q[s->n++];
    *q = *n;
    q->owner = owner;
    q->life = (secs ? secs < 2 ? 2 : secs > 60 ? 60 : secs : NOTE_LIFE) * TICKS;
    if (q->nactions > 2)
        q->nactions = 2;
    if (!++s->next_id)
        s->next_id = 1;
    q->id = s->next_id;
    return q->id;
}

int notes_remove(struct notes *s, u32 owner, u32 id)
{
    u32 i, k;
    int changed = 0;
    for (i = 0; i < s->n; ) {
        if (s->q[i].owner != owner || (id && s->q[i].id != id)) {
            i++;
            continue;
        }
        if (i == 0 && s->shown) {
            changed = 1;
            s->shown = 0;
        }
        for (k = i + 1; k < s->n; k++)
            s->q[k - 1] = s->q[k];
        s->q[--s->n].id = 0;
    }
    return changed;
}

int notes_turn(struct notes *s, u32 now, int can, int over, struct note_news *news)
{
    news->id = 0;
    if (s->shown && !can) {             /* a full-screen program has the screen: back to wait, with the time it had */
        s32 left = (s32)(s->until - now);
        s->q[0].life = left > (s32)TICKS ? (u32)left : TICKS;
        s->shown = 0;
        return 1;
    }
    if (s->shown && over && (s32)(s->until - now) < (s32)TICKS)
        s->until = now + TICKS;         /* (the pointer is on it: a second more, from when it leaves) */
    if (s->shown && (s32)(now - s->until) >= 0) {
        news->id = s->q[0].id;
        news->owner = s->q[0].owner;
        news->what = NOTE_TIMEOUT;
        gone(s);
    }
    if (!s->shown && s->n && can) {
        s->shown = 1;
        s->until = now + s->q[0].life;
        return 1;
    }
    return news->id != 0;
}

int notes_click(struct notes *s, int part, struct note_news *news)
{
    news->id = 0;
    if (!s->shown || part == NP_NONE)
        return 0;
    if ((part == NP_ACT1 && s->q[0].nactions < 1) || (part == NP_ACT2 && s->q[0].nactions < 2))
        part = NP_BODY;
    news->id = s->q[0].id;
    news->owner = s->q[0].owner;
    news->what = part == NP_ACT1 ? NOTE_ACTION1 : part == NP_ACT2 ? NOTE_ACTION2 : NOTE_DISMISSED;
    gone(s);
    return 1;
}

const struct note *notes_shown(const struct notes *s) { return s->shown ? &s->q[0] : 0; }
u32 notes_due(const struct notes *s) { return s->shown ? (s->until ? s->until : 1) : 0; }

/* ---- the layout */

static u32 len_of(const char *t)
{
    u32 n = 0;
    while (t[n])
        n++;
    return n;
}

void note_layout(const struct note *n, int (*width)(const char *text, int len, int bold), struct note_layout *l)
{
    const char *t = n->text;
    u32 at = 0, total = len_of(t), i;
    s32 room, y, x;
    l->w = NOTE_W;
    l->text_x = PAD + (n->has_icon ? 16 + 6 : 0);
    l->title_y = PAD;
    l->close.w = l->close.h = 10;
    l->close.x = NOTE_W - PAD - 10;
    l->close.y = PAD;
    room = NOTE_W - PAD - l->text_x;
    l->nlines = 0;
    l->cut = 0;
    y = PAD + LINE + 2;
    while (at < total && l->nlines < NOTE_LINES) {     /* the text in lines: broken at spaces, or where it must be */
        u32 end = at, last = 0, k;
        while (end < total && t[end] != '\n') {
            if (width(t + at, (int)(end + 1 - at), 0) > room)
                break;
            if (t[end] == ' ')
                last = end;
            end++;
        }
        k = end;
        if (end < total && t[end] != '\n') {            /* (it didn't all fit) */
            if (last > at)
                k = last;                               /* (back to the last space) */
            else if (end == at)
                k = end = at + 1;                       /* (not even one character: one, all the same) */
        }
        l->line_at[l->nlines] = at;
        l->line_len[l->nlines] = k - at;
        l->line_y[l->nlines] = y;
        l->nlines++;
        y += LINE;
        at = k;
        while (at < total && (t[at] == ' ' || t[at] == '\n')) {
            if (t[at++] == '\n')
                break;
        }
    }
    if (at < total)
        l->cut = 1;
    if (y < PAD + 16 + 2)
        y = PAD + 16 + 2;                               /* (room for the icon) */
    for (i = 0; i < 2; i++)
        l->act[i].x = l->act[i].y = l->act[i].w = l->act[i].h = 0;
    if (n->nactions) {
        y += 4;
        x = NOTE_W - PAD;
        for (i = n->nactions; i-- > 0; ) {              /* from the right: the first action leftmost */
            s32 w = width(n->action[i], (int)len_of(n->action[i]), 0) + 16;
            x -= w;
            l->act[i].x = x, l->act[i].y = y, l->act[i].w = w, l->act[i].h = 18;
            x -= 6;
        }
        y += 18;
    }
    l->h = y + PAD;
}

static int in(const struct note_rect *r, s32 x, s32 y)
{
    return r->w && x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

int note_part(const struct note_layout *l, s32 x, s32 y)
{
    if (x < 0 || y < 0 || x >= l->w || y >= l->h)
        return NP_NONE;
    return in(&l->close, x, y) ? NP_CLOSE : in(&l->act[0], x, y) ? NP_ACT1 : in(&l->act[1], x, y) ? NP_ACT2 : NP_BODY;
}
