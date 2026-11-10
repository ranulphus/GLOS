/* A drag's states (dnd.h). */
#include "dnd.h"

static struct dnd_note *note(struct dnd_note *out, u32 *n, u8 kind, u32 app, u32 win, s32 x, s32 y, u32 effect)
{
    struct dnd_note *o = &out[(*n)++];
    o->kind = kind, o->app = app, o->win = win, o->x = x, o->y = y, o->effect = effect;
    return o;
}

static u32 first(u32 effects)           /* the effect a target has until it says another: copy before move before link */
{
    return effects & DND_COPY ? DND_COPY : effects & DND_MOVE ? DND_MOVE : effects & DND_LINK ? DND_LINK : 0;
}

int dnd_begin(struct dnd *d, u32 app, u32 win, u32 allowed)
{
    allowed &= DND_COPY | DND_MOVE | DND_LINK;
    if (d->on || !allowed || !app)
        return -1;
    d->on = 1;
    d->src_app = app, d->src_win = win;
    d->allowed = allowed;
    d->t_app = d->t_win = d->t_effects = d->answer = 0;
    d->x = d->y = 0;
    return 0;
}

static void leave(struct dnd *d, struct dnd_note *out, u32 *n)
{
    if (d->t_app)
        note(out, n, DND_LEAVE, d->t_app, d->t_win, d->x, d->y, 0);
    d->t_app = d->t_win = d->t_effects = d->answer = 0;
}

u32 dnd_move(struct dnd *d, const struct dnd_target *t, s32 x, s32 y, struct dnd_note *out)
{
    u32 n = 0, effects = t ? t->effects & d->allowed : 0;
    int moved = x != d->x || y != d->y;
    if (!d->on)
        return 0;
    if (!effects || !t->app)
        t = 0;
    if (!t || t->app != d->t_app || t->win != d->t_win) {
        leave(d, out, &n);
        d->x = x, d->y = y;
        if (t) {
            d->t_app = t->app, d->t_win = t->win;
            d->t_effects = effects;
            d->answer = first(effects);
            note(out, &n, DND_ENTER, t->app, t->win, x, y, d->allowed);
        }
        return n;
    }
    d->x = x, d->y = y;
    if (d->t_effects != effects) {      /* (it takes other effects now: its answer only if it still can) */
        d->t_effects = effects;
        if (!(d->answer & effects))
            d->answer = first(effects);
    }
    if (moved)
        note(out, &n, DND_OVER, t->app, t->win, x, y, d->allowed);
    return n;
}

int dnd_answer(struct dnd *d, u32 app, u32 win, u32 effect)
{
    if (!d->on || !d->t_app || d->t_app != app || d->t_win != win)
        return -1;
    if (effect && (effect & (effect - 1) || !(effect & d->t_effects)))
        return -1;                      /* (one effect, of those it could have) */
    d->answer = effect;
    return 0;
}

u32 dnd_end(struct dnd *d, int drop, struct dnd_note *out)
{
    u32 n = 0, effect = 0;
    if (!d->on)
        return 0;
    if (drop && d->t_app && d->answer) {
        effect = d->answer;
        note(out, &n, DND_DROP, d->t_app, d->t_win, d->x, d->y, effect);
        d->t_app = d->t_win = 0;
    } else {
        leave(d, out, &n);
    }
    note(out, &n, DND_DONE, d->src_app, d->src_win, d->x, d->y, effect);
    d->on = 0;
    d->t_app = d->t_win = d->t_effects = d->answer = 0;
    return n;
}

u32 dnd_gone(struct dnd *d, u32 app, u32 win, struct dnd_note *out)
{
    u32 n = 0;
    if (!d->on)
        return 0;
    if (d->src_app == app && (!win || d->src_win == win)) {     /* the source: over, and nobody is told DONE */
        leave(d, out, &n);
        d->on = 0;
        return n;
    }
    if (d->t_app == app && (!win || d->t_win == win))           /* the target: as if the pointer had left it */
        d->t_app = d->t_win = d->t_effects = d->answer = 0;
    return n;
}
