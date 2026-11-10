/* The clipboard's store (clipstore.h). */
#include "clipstore.h"

void clipstore_init(struct clipstore *s, const struct clip_host *host)
{
    u32 i;
    s->host = host;
    s->now = 0;
    s->serial = 0;
    s->nnames = 0;
    for (i = 0; i < CLIP_NAMES; i++)
        s->names[i][0] = 0;
}

struct clip_content *clip_new(struct clipstore *s)
{
    struct clip_content *c = s->host->alloc(sizeof *c);
    u32 i;
    if (!c)
        return 0;
    c->refs = 1;
    c->serial = 0;
    c->n = 0;
    for (i = 0; i < CLIP_ITEMS; i++)
        c->item[i].format = c->item[i].len = 0, c->item[i].data = 0;
    return c;
}

void *clip_add(struct clipstore *s, struct clip_content *c, u32 format, u32 len)
{
    struct clip_item *it = 0;
    void *data;
    u32 i;
    if (!format || c->serial)
        return 0;
    for (i = 0; i < c->n; i++)
        if (c->item[i].format == format)
            it = &c->item[i];
    if (!it && c->n == CLIP_ITEMS)
        return 0;
    if (!(data = s->host->alloc(len ? len : 1)))
        return 0;
    if (it)
        s->host->free(it->data, it->len ? it->len : 1);
    else
        it = &c->item[c->n++];
    it->format = format;
    it->len = len;
    it->data = data;
    return data;
}

const struct clip_item *clip_find(const struct clip_content *c, u32 format)
{
    u32 i;
    for (i = 0; c && i < c->n; i++)
        if (c->item[i].format == format)
            return &c->item[i];
    return 0;
}

struct clip_content *clip_keep(struct clip_content *c)
{
    if (c)
        c->refs++;
    return c;
}

struct clip_content *clip_ref(struct clipstore *s) { return clip_keep(s->now); }

void clip_unref(struct clipstore *s, struct clip_content *c)
{
    u32 i;
    if (!c || --c->refs)
        return;
    for (i = 0; i < c->n; i++)
        s->host->free(c->item[i].data, c->item[i].len ? c->item[i].len : 1);
    s->host->free(c, sizeof *c);
}

void clip_commit(struct clipstore *s, struct clip_content *c)
{
    struct clip_content *old = s->now;
    s->serial++;
    if (c)
        c->serial = s->serial;
    s->now = c;
    clip_unref(s, old);
}

static int name_ok(const char *name)
{
    u32 i;
    for (i = 0; name[i]; i++)
        if (i >= CLIP_NAME_MAX - 1 || name[i] <= ' ' || name[i] > '~')
            return 0;
    return i != 0;
}

u32 clip_format(struct clipstore *s, const char *name)
{
    u32 i, k;
    if (!name_ok(name))
        return 0;
    for (i = 0; i < s->nnames; i++) {
        for (k = 0; name[k] && name[k] == s->names[i][k]; k++) ;
        if (name[k] == s->names[i][k])
            return CLIP_PRIVATE + i;
    }
    if (s->nnames == CLIP_NAMES)
        return 0;
    for (k = 0; name[k]; k++)
        s->names[i][k] = name[k];
    s->names[i][k] = 0;
    s->nnames++;
    return CLIP_PRIVATE + i;
}

const char *clip_format_name(const struct clipstore *s, u32 format)
{
    return format >= CLIP_PRIVATE && format - CLIP_PRIVATE < s->nnames ? s->names[format - CLIP_PRIVATE] : 0;
}
