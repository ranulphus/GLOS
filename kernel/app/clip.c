/* The clipboard (PRD §10.2; docs/milestones-m9.md step A): text, the
 * system's, one for every app and the agent (GLOS_FN_CLIP, glos clip).
 *
 * Its bytes are copied in from an app before the lock is taken (the app's
 * memory may have to be paged in) and the new text swapped in under it; a
 * read copies out with the lock held, which is a thread's, not an
 * interrupt's, so a page fault on the way is no matter. */
#include "clip.h"
#include "dpmi.h"
#include "io.h"
#include "mm.h"
#include "sched.h"

static char *text;
static u32 len;
static struct waitq lockq;
static struct thread *owner;

static void lock(void)
{
    u32 f = irq_save();
    while (owner && owner != current)
        thread_block(&lockq);
    owner = current;
    irq_restore(f);
}

static void unlock(void)
{
    u32 f = irq_save();
    owner = 0;
    thread_wake(&lockq);
    irq_restore(f);
}

static void swap_in(char *t, u32 n)
{
    char *old;
    lock();
    old = text;
    text = t;
    len = n;
    unlock();
    if (old)
        kfree(old);
}

int clip_set(const char *t, u32 n)
{
    char *c;
    u32 i;
    if (n > CLIP_MAX || !(c = kmalloc(n ? n : 1)))
        return -1;
    for (i = 0; i < n; i++)
        c[i] = t[i];
    swap_in(c, n);
    return 0;
}

int clip_set_user(u16 sel, u32 off, u32 n)
{
    char *c;
    if (n > CLIP_MAX || !(c = kmalloc(n ? n : 1)))
        return -1;
    if (n && user_rd(sel, off, c, n) != 0) {
        kfree(c);
        return -1;
    }
    swap_in(c, n);
    return 0;
}

u32 clip_get(char *out, u32 room)
{
    u32 n, i;
    lock();
    n = len;
    for (i = 0; i < n && i < room; i++)
        out[i] = text[i];
    unlock();
    return n;
}

int clip_get_user(u16 sel, u32 off, u32 room, u32 *n)
{
    int r = 0;
    lock();
    *n = len;
    if (room && len && user_wr(sel, off, text, room < len ? room : len) != 0)
        r = -1;
    unlock();
    return r;
}
