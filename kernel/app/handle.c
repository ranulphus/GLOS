/* API 2.0's handles (handle.h). */
#include "handle.h"
#include "string.h"

void htab_init(struct htab *t) { memset(t, 0, sizeof *t); }

u32 handle_new(struct htab *t, u8 kind, void *obj)
{
    u32 i;
    if (!kind)
        return 0;
    for (i = 0; i < HANDLE_MAX; i++)
        if (!t->e[i].kind)
            break;
    if (i == HANDLE_MAX)
        return 0;
    t->e[i].kind = kind;
    t->e[i].obj = obj;
    t->used++;
    return ((u32)kind << 24) | ((u32)t->e[i].gen << 16) | i;
}

static struct hent *find(const struct htab *t, u32 h)
{
    u32 i = h & 0xFFFFu;
    const struct hent *e;
    if (i >= HANDLE_MAX)
        return 0;
    e = &t->e[i];
    if (!e->kind || e->kind != (u8)(h >> 24) || e->gen != (u8)(h >> 16))
        return 0;
    return (struct hent *)e;
}

void *handle_obj(const struct htab *t, u32 h, u8 kind)
{
    struct hent *e = find(t, h);
    return e && e->kind == kind ? e->obj : 0;
}

int handle_kind(const struct htab *t, u32 h)
{
    struct hent *e = find(t, h);
    return e ? e->kind : 0;
}

void *handle_close(struct htab *t, u32 h)
{
    struct hent *e = find(t, h);
    void *obj;
    if (!e)
        return 0;
    obj = e->obj;
    e->kind = 0;
    e->obj = 0;
    e->gen++;                           /* (this number is no longer the entry's) */
    t->used--;
    return obj;
}

int handle_next(const struct htab *t, u32 *i, u32 *h, void **obj)
{
    for (; *i < HANDLE_MAX; (*i)++)
        if (t->e[*i].kind) {
            const struct hent *e = &t->e[*i];
            *h = ((u32)e->kind << 24) | ((u32)e->gen << 16) | *i;
            *obj = e->obj;
            (*i)++;
            return 0;
        }
    return -1;
}
