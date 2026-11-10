/* The clipboard (clip.h; PRD §10.2; docs/v1/phase-b.md step 6): the kernel's store (clipstore.c), kept outside
 * any app, with its memory and its lock.
 *
 * Memory: a content's small pieces are the kernel heap's; data over a page is whole pages of the windows' region
 * (mm_win_alloc), which is what has room for a picture. Each call on the store is made with interrupts off, so
 * it is whole; the copying of data to and from an app's memory is done outside that, on a content the copier
 * holds a reference to (a reader) or alone knows of (a writer), so a page fault on the way is no matter. */
#include "clip.h"
#include "clipstore.h"
#include "dpmi.h"
#include "io.h"
#include "mm.h"
#include "sched.h"

#define SMALL 4096u

static void *c_alloc(u32 bytes) { return bytes <= SMALL ? kmalloc(bytes) : mm_win_alloc(bytes); }

static void c_free(void *p, u32 bytes)
{
    if (bytes <= SMALL)
        kfree(p);
    else
        mm_win_free(p, bytes);
}

static const struct clip_host host = { c_alloc, c_free };
static struct clipstore store;
static u8 ready;
static void (*told)(u32 serial);

static struct clipstore *st(void)
{
    if (!ready) {
        clipstore_init(&store, &host);
        ready = 1;
    }
    return &store;
}

struct clip_content *clip_content_new(void)
{
    u32 f = irq_save();
    struct clip_content *c = clip_new(st());
    irq_restore(f);
    return c;
}

void *clip_content_add(struct clip_content *c, u32 format, u32 len)
{
    u32 f = irq_save();
    void *p = clip_add(st(), c, format, len);
    irq_restore(f);
    return p;
}

struct clip_content *clip_content_now(void)
{
    u32 f = irq_save();
    struct clip_content *c = clip_ref(st());
    irq_restore(f);
    return c;
}

struct clip_content *clip_content_keep(struct clip_content *c)
{
    u32 f = irq_save();
    clip_keep(c);
    irq_restore(f);
    return c;
}

void clip_content_drop(struct clip_content *c)
{
    u32 f = irq_save();
    clip_unref(st(), c);
    irq_restore(f);
}

void clip_content_commit(struct clip_content *c)
{
    u32 f = irq_save(), serial;
    clip_commit(st(), c);
    serial = store.serial;
    irq_restore(f);
    if (told)
        told(serial);
}

u32 clip_serial(void) { return st()->serial; }

u32 clip_private(const char *name)
{
    u32 f = irq_save(), n = clip_format(st(), name);
    irq_restore(f);
    return n;
}

void clip_hook(void (*changed)(u32 serial)) { told = changed; }

/* ---- its text, for API 1.x and the agent */

int clip_set(const char *t, u32 n)
{
    struct clip_content *c;
    char *p;
    u32 i;
    if (n > CLIP_MAX || !(c = clip_content_new()))
        return -1;
    if (!(p = clip_content_add(c, CLIP_TEXT, n))) {
        clip_content_drop(c);
        return -1;
    }
    for (i = 0; i < n; i++)
        p[i] = t[i];
    clip_content_commit(c);
    return 0;
}

int clip_set_user(u16 sel, u32 off, u32 n)
{
    struct clip_content *c;
    char *p;
    if (n > CLIP_MAX || !(c = clip_content_new()))
        return -1;
    if (!(p = clip_content_add(c, CLIP_TEXT, n)) || (n && user_rd(sel, off, p, n) != 0)) {
        clip_content_drop(c);
        return -1;
    }
    clip_content_commit(c);
    return 0;
}

u32 clip_get(char *out, u32 room)
{
    struct clip_content *c = clip_content_now();
    const struct clip_item *it = clip_find(c, CLIP_TEXT);
    u32 n = it ? it->len : 0, i;
    for (i = 0; i < n && i < room; i++)
        out[i] = ((const char *)it->data)[i];
    clip_content_drop(c);
    return n;
}

int clip_get_user(u16 sel, u32 off, u32 room, u32 *n)
{
    struct clip_content *c = clip_content_now();
    const struct clip_item *it = clip_find(c, CLIP_TEXT);
    int r = 0;
    *n = it ? it->len : 0;
    if (room && *n && user_wr(sel, off, it->data, room < *n ? room : *n) != 0)
        r = -1;
    clip_content_drop(c);
    return r;
}
