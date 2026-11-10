/* GLOS.DAT: kernel/include/kdata.h. */
#include <string.h>

#include "kdata.h"

/* ---- the list (the host's too) */

static u32 kind_len(const char *l)
{
    u32 n = 0;
    while (l[n] && l[n] != ' ')
        n++;
    return n;
}

static int is_kind(const char *l, const char *kind)
{
    u32 n = kind_len(l);
    return n == strlen(kind) && !memcmp(l, kind, n) && l[n] == ' ';
}

static int nth(const struct kdata *d, const char *kind, u32 n)  /* the line's index, or -1 */
{
    u32 i;
    for (i = 0; i < d->n; i++)
        if (is_kind(d->line[i], kind) && !n--)
            return (int)i;
    return -1;
}

static int text_ok(const char *kind, const char *text)
{
    u32 i, k = (u32)strlen(kind);
    if (!k || k >= KDATA_KIND)
        return 0;
    for (i = 0; i < k; i++)
        if (kind[i] <= ' ' || kind[i] == 0x7F)
            return 0;
    for (i = 0; text[i]; i++)
        if (text[i] == '\n' || text[i] == '\r' || text[i] == 0x1A)
            return 0;
    return i <= KDATA_TEXT;
}

static void make(char *line, const char *kind, const char *text)
{
    u32 k = (u32)strlen(kind);
    memcpy(line, kind, k);
    line[k] = ' ';
    strcpy(line + k + 1, text);
}

u32 kd_count(const struct kdata *d, const char *kind)
{
    u32 i, n = 0;
    for (i = 0; i < d->n; i++)
        n += (u32)is_kind(d->line[i], kind);
    return n;
}

int kd_get(const struct kdata *d, const char *kind, u32 n, char *out, u32 room)
{
    int i = nth(d, kind, n);
    const char *t;
    u32 len;
    if (i < 0)
        return -1;
    t = d->line[i] + kind_len(d->line[i]) + 1;
    len = (u32)strlen(t);
    if (len >= room)
        return -1;
    memcpy(out, t, len + 1);
    return (int)len;
}

int kd_add(struct kdata *d, const char *kind, const char *text, int front)
{
    u32 at = d->n;
    int first;
    if (d->n >= KDATA_MAX || !text_ok(kind, text))
        return -1;
    if (front && (first = nth(d, kind, 0)) >= 0)
        at = (u32)first;
    memmove(d->line[at + 1], d->line[at], (d->n - at) * sizeof d->line[0]);
    make(d->line[at], kind, text);
    d->n++;
    return 0;
}

int kd_remove(struct kdata *d, const char *kind, u32 n)
{
    int i = nth(d, kind, n);
    if (i < 0)
        return -1;
    memmove(d->line[i], d->line[i + 1], (d->n - (u32)i - 1) * sizeof d->line[0]);
    d->n--;
    return 0;
}

int kd_set(struct kdata *d, const char *kind, u32 n, const char *text)
{
    int i = nth(d, kind, n);
    if (i < 0 || !text_ok(kind, text))
        return -1;
    make(d->line[i], kind, text);
    return 0;
}

u32 kd_write(const struct kdata *d, char *buf, u32 room)
{
    u32 i, n = 0;
    for (i = 0; i < d->n; i++) {
        u32 l = (u32)strlen(d->line[i]);
        if (n + l + 2 > room)
            break;
        memcpy(buf + n, d->line[i], l);
        n += l;
        buf[n++] = '\r', buf[n++] = '\n';
    }
    return n;
}

void kd_read(struct kdata *d, const char *buf, u32 len)
{
    u32 i, start = 0;
    d->n = 0;
    for (i = 0; i <= len; i++)
        if (i == len || buf[i] == '\n' || buf[i] == 0x1A) {
            u32 e = i, k;
            if (e > start && buf[e - 1] == '\r')
                e--;
            for (k = start; k < e && buf[k] != ' '; k++) ;
            if (k > start && k < e && k - start < KDATA_KIND && e - k - 1 <= KDATA_TEXT && d->n < KDATA_MAX) {
                memcpy(d->line[d->n], buf + start, e - start);
                d->line[d->n][e - start] = 0;
                d->n++;
            }
            if (i < len && buf[i] == 0x1A)
                break;
            start = i + 1;
        }
}

/* ---- the kernel's store and its file */
#if !__STDC_HOSTED__                     /* (the kernel is built freestanding; the host's test is not) */
#include "dos.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "timer.h"

#define FILE_MAX (KDATA_MAX * (KDATA_KIND + KDATA_TEXT + 4))

static struct kdata store;
static char path[80];
static u32 serial;
static volatile u8 dirty;
static u8 can;                          /* there is a file to keep it in (the agent's DOS server) */
static struct waitq q;

void kdata_init(const char *cfg_path)
{
    u32 i, n = 0;
    for (i = 0; cfg_path[i]; i++)
        if (cfg_path[i] == '\\')
            n = i + 1;
    if (!n || n + 9 > sizeof path)
        return;
    memcpy(path, cfg_path, n);
    strcpy(path + n, "GLOS.DAT");
}

static void changed(void)               /* (interrupts off) */
{
    serial++;
    dirty = 1;
}

u32 kdata_count(const char *kind)
{
    u32 f = irq_save(), n = kd_count(&store, kind);
    irq_restore(f);
    return n;
}

int kdata_get(const char *kind, u32 n, char *out, u32 room)
{
    u32 f = irq_save();
    int r = kd_get(&store, kind, n, out, room);
    irq_restore(f);
    return r;
}

int kdata_add(const char *kind, const char *text, int front)
{
    u32 f = irq_save();
    int r = kd_add(&store, kind, text, front);
    if (r == 0)
        changed();
    irq_restore(f);
    if (r == 0)
        kcfg_writer_kick();
    return r;
}

int kdata_remove(const char *kind, u32 n)
{
    u32 f = irq_save();
    int r = kd_remove(&store, kind, n);
    if (r == 0)
        changed();
    irq_restore(f);
    if (r == 0)
        kcfg_writer_kick();
    return r;
}

int kdata_set(const char *kind, u32 n, const char *text)
{
    u32 f = irq_save();
    int r = kd_set(&store, kind, n, text);
    if (r == 0)
        changed();
    irq_restore(f);
    if (r == 0)
        kcfg_writer_kick();
    return r;
}

u32 kdata_serial(void) { return serial; }
int kdata_dirty(void) { return can && dirty; }

static int dos_wait(struct dos_req *r)
{
    if (dos_post(r) != 0)
        return -1;
    while (r->state != DR_DONE)
        thread_wait(&q, timer_ticks() + 64);
    return r->error ? -1 : 0;
}

void kdata_load(void)
{
    struct dos_req r;
    u32 room = dos_xfer_size(), got, f;
    char *buf;
    u16 h;
    if (!path[0])
        return;
    can = 1;
    memset(&r, 0, sizeof r);
    r.n = 1;
    r.r[0].ax = 0x3D00;
    r.r[0].ds = DOS_XFER;
    r.in = (const u8 *)path, r.in_len = strlen(path) + 1, r.in_at = 0;
    if (dos_wait(&r) != 0)
        return;                                 /* (no file yet: written when there is something to keep) */
    h = r.r[0].ax;
    if (room > FILE_MAX)
        room = FILE_MAX;
    if (!(buf = kmalloc(room + 1)))
        room = 0;
    memset(&r, 0, sizeof r);
    r.n = 2;
    r.r[0].ax = 0x3F00, r.r[0].bx = h, r.r[0].cx = (u16)room, r.r[0].ds = DOS_XFER;
    r.r[1].ax = 0x3E00, r.r[1].bx = h;
    r.out = (u8 *)buf, r.out_len = room, r.out_at = 0;
    if (dos_wait(&r) != 0 && r.ran < 1) {
        kprintf("GLOS-DATA error=read dos=%u\n", r.error);
    } else if (buf) {
        got = r.r[0].ax;
        f = irq_save();
        if (!store.n)                           /* (nothing was added before the file was read) */
            kd_read(&store, buf, got);
        serial++;
        irq_restore(f);
        kprintf("GLOS-DATA loaded records=%u bytes=%u\n", store.n, got);
    }
    if (buf)
        kfree(buf);
}

int kdata_save(void)
{
    struct dos_req r;
    char *buf = kmalloc(FILE_MAX);
    u32 n, f;
    u16 h;
    int ok = -1;
    if (!buf)
        return -1;
    f = irq_save();
    n = kd_write(&store, buf, FILE_MAX);
    dirty = 0;
    irq_restore(f);
    memset(&r, 0, sizeof r);
    r.n = 1;
    r.r[0].ax = 0x3C00, r.r[0].cx = 0, r.r[0].ds = DOS_XFER;
    r.in = (const u8 *)path, r.in_len = strlen(path) + 1, r.in_at = 0;
    if (dos_wait(&r) == 0) {
        h = r.r[0].ax;
        memset(&r, 0, sizeof r);
        r.n = 2;
        r.r[0].ax = 0x4000, r.r[0].bx = h, r.r[0].cx = (u16)n, r.r[0].ds = DOS_XFER;
        r.r[1].ax = 0x3E00, r.r[1].bx = h;
        r.in = (const u8 *)buf, r.in_len = n, r.in_at = 0;
        ok = dos_wait(&r) == 0 && r.r[0].ax == n ? 0 : -1;
    }
    kfree(buf);
    kprintf(ok == 0 ? "GLOS-DATA written records=%u\n" : "GLOS-DATA error=write records=%u\n", store.n);
    return ok;
}
#endif
