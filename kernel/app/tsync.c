/* Locks and signals of an app's threads (tsync.h). */
#include "tsync.h"

int tlock_take(struct tlock *l, u32 who)
{
    if (l->owner && l->owner != who)
        return 0;
    if (l->depth == 0xFFFFFFFFu)
        return 0;
    l->owner = who;
    l->depth++;
    return 1;
}

int tlock_give(struct tlock *l, u32 who)
{
    if (!who || l->owner != who)
        return -1;
    if (--l->depth)
        return 0;
    l->owner = 0;
    return 1;
}

int tlock_drop(struct tlock *l, u32 who)
{
    if (!who || l->owner != who)
        return 0;
    l->owner = 0;
    l->depth = 0;
    return 1;
}

void tsignal_set(struct tsignal *s, int on) { s->set = on != 0; }

int tsignal_take(struct tsignal *s)
{
    if (!s->set)
        return 0;
    if (s->one)
        s->set = 0;
    return 1;
}
