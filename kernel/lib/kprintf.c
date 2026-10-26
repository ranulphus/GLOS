/* A small printf for the kernel and its string helpers. Lines go to COM1
 * whole: while a program in the system VM is part-way through a line of
 * its own there (vm/vdev.c traps the data port and says so), the kernel's
 * output is held and written when that line ends, or at the latest 100
 * ticks on (supervisor.md §18). A panic writes at once.
 *
 * Once the threads run, a line is not written where kprintf() is called
 * (M6): COM1 takes 87 microseconds a character, and most callers have
 * interrupts off, so a hundred characters cost the kernel's clock eight
 * ticks and every interrupt nine milliseconds. The line waits in a ring,
 * and kprintf_pump() writes from it with interrupts on while each
 * character waits for the UART, where that is safe: on the way out of a
 * trap from user mode, in the idle loop, and in kprintf() itself for a
 * caller that had interrupts on. No thread switch happens meanwhile but to
 * urgent threads, between characters, which come back to the writer (they
 * write nothing to COM1 themselves): so lines come out whole and in the
 * order they were made, and before any program's own line that starts
 * later (vdev.c flushes first). */
#include "io.h"
#include "kprintf.h"
#include "sched.h"

/* By dwords, then the bytes left: an LDT is 64 KB to clear, and most callers
   have interrupts off (M6: byte loops cost a 486 seven ticks there). */
void *memset(void *d, int c, size_t n)
{
    void *p = d;
    u32 v = (u8)c * 0x01010101u, k = n >> 2;
    __asm__ volatile("rep stosl" : "+D"(p), "+c"(k) : "a"(v) : "memory");
    k = n & 3;
    __asm__ volatile("rep stosb" : "+D"(p), "+c"(k) : "a"(v) : "memory");
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    void *p = d;
    const void *q = s;
    u32 k = n >> 2;
    __asm__ volatile("rep movsl" : "+D"(p), "+S"(q), "+c"(k) :: "memory");
    k = n & 3;
    __asm__ volatile("rep movsb" : "+D"(p), "+S"(q), "+c"(k) :: "memory");
    return d;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q) return *p - *q;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

struct out { char *buf; size_t n, len; };

#define HELD 0x2000u                    /* a power of two */

int (*kprintf_hold)(void);
static char held[HELD];
static u32 held_head, held_tail;        /* written to, written from */
static u32 direct, deferred;
static u8 pumping;

/* The COM1 mirror for glos log: the last 16 KB of the kernel's lines and of
   what programs write there, without CRs. Writers run with interrupts off. */
#define LOG_RING 0x4000u
static char log_ring[LOG_RING];
static u32 log_head;

void klog_put(const char *s, u32 n)
{
    for (; n; n--, s++)
        if (*s != '\r')
            log_ring[log_head++ & (LOG_RING - 1)] = *s;
}

u32 klog_read(char *out, u32 max)
{
    u32 f, n, start, i;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory");
    n = log_head < LOG_RING ? log_head : LOG_RING;
    if (n > max)
        n = max;
    start = log_head - n;
    for (i = 0; i < n; i++)
        out[i] = log_ring[(start + i) & (LOG_RING - 1)];
    if (f & 0x200)
        sti();
    if (n == max && n < log_head) {             /* cut short: start at a whole line */
        for (i = 0; i < n && out[i] != '\n'; i++) ;
        if (i < n) {
            u32 k;
            for (k = 0; k + i + 1 < n; k++)
                out[k] = out[k + i + 1];
            n -= i + 1;
        }
    }
    return n;
}

u32 klog_pos(void) { return log_head; }

u32 klog_from(u32 *pos, char *out, u32 max)
{
    u32 f = irq_save(), n, i;
    if (log_head - *pos > LOG_RING)             /* the ring has gone round: what is left */
        *pos = log_head - LOG_RING;
    n = log_head - *pos;
    if (n > max)
        n = max;
    for (i = 0; i < n; i++)
        out[i] = log_ring[(*pos + i) & (LOG_RING - 1)];
    *pos += n;
    irq_restore(f);
    return n;
}

void (*kprintf_screen)(const char *s, u32 n);

static void raw_out(const char *s, u32 n)
{
    klog_put(s, n);
    if (kprintf_screen)                         /* (/VERBOSE: the text screen too, vm/vcon.c) */
        kprintf_screen(s, n);
    for (; n; n--, s++) {
        if (*s == '\n') serial_putc(COM1, '\r');
        serial_putc(COM1, *s);
    }
}

static void emit(struct out *o, char c)
{
    if (o->buf) {
        if (o->len + 1 < o->n) o->buf[o->len] = c;
    } else {
        if (c == '\n') serial_putc(COM1, '\r');
        serial_putc(COM1, c);
    }
    o->len++;
}

static void vfmt(struct out *o, const char *f, __builtin_va_list ap)
{
    for (; *f; f++) {
        char tmp[12], pad = ' ';
        int width = 0, i = 0, neg = 0;
        u32 v, base = 10;
        const char *s;
        if (*f != '%') { emit(o, *f); continue; }
        f++;
        if (*f == '0') { pad = '0'; f++; }
        while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
        switch (*f) {
        case 's':
            s = __builtin_va_arg(ap, const char *);
            if (!s) s = "(null)";
            for (i = (int)strlen(s); i < width; i++) emit(o, ' ');
            while (*s) emit(o, *s++);
            continue;
        case 'c': emit(o, (char)__builtin_va_arg(ap, int)); continue;
        case 'd': { s32 sv = __builtin_va_arg(ap, s32); if (sv < 0) { neg = 1; v = (u32)-sv; } else v = (u32)sv; break; }
        case 'u': v = __builtin_va_arg(ap, u32); break;
        case 'p': pad = '0'; width = 8; /* fall through */
        case 'x': v = __builtin_va_arg(ap, u32); base = 16; break;
        case '%': emit(o, '%'); continue;
        default: emit(o, *f); continue;
        }
        do { u32 d = v % base; tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); } while ((v /= base));
        if (neg) tmp[i++] = '-';
        for (; i < width; width--) emit(o, pad);
        while (i) emit(o, tmp[--i]);
    }
}

void kprintf(const char *fmt, ...)
{
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    kvprintf(fmt, ap);
    __builtin_va_end(ap);
}

/* Held lines out, here and now, unless a program is still mid-line (force:
   regardless). */
void kprintf_flush(int force)
{
    u32 f = irq_save();
    if (force || !kprintf_hold || !kprintf_hold())
        while (held_tail != held_head)
            raw_out(&held[held_tail++ & (HELD - 1)], 1);
    irq_restore(f);
}

/* From now on everything is written at once (a panic, the leave). */
void kprintf_direct(void)
{
    kprintf_flush(1);
    direct = 1;
}

/* The threads run: lines wait for kprintf_pump(). */
void kprintf_defer(void) { deferred = 1; }

/* Held lines out, whole ones, until max characters have gone: with
   interrupts on while each character waits for the UART, and no thread
   switch (called with interrupts off, which they are again at the return).
   Whole, because a program in a direct-mode session writes to COM1 without
   the kernel seeing it: a line of the kernel's must not be left half-way.
   1 if anything was written. */
int kprintf_pump(u32 max)
{
    u32 n = 0, spin;
    char last = '\n';
    if (pumping || held_tail == held_head || (kprintf_hold && kprintf_hold()))
        return 0;
    pumping = 1;
    preempt_disable();
    while (held_tail != held_head && (n < max || last != '\n')) {
        char c = held[held_tail & (HELD - 1)];
        int cr = c == '\n';
        do {
            spin = 0;
            sti();
            while (!(inb(COM1 + 5) & 0x20) && ++spin < 100000) ;
            cli();
            outb(COM1, (u8)(cr ? '\r' : c));
        } while (cr--);
        klog_put(&c, 1);
        held_tail++;
        n++;
        last = c;
        sched_urgent_turn();                    /* (network receive and the like don't wait for the line's end) */
    }
    pumping = 0;
    preempt_enable();
    return 1;
}

int ksnprintf(char *buf, size_t n, const char *fmt, ...)
{
    struct out o = { buf, n, 0 };
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vfmt(&o, fmt, ap);
    __builtin_va_end(ap);
    if (n) buf[o.len < n ? o.len : n - 1] = 0;
    return (int)o.len;
}

void kvprintf(const char *fmt, __builtin_va_list ap)
{
    char line[256];
    struct out o = { line, sizeof line, 0 };
    u32 n, f, i;
    vfmt(&o, fmt, ap);
    n = o.len < sizeof line ? o.len : sizeof line - 1;
    f = irq_save();
    if (direct || (!deferred && held_tail == held_head && !(kprintf_hold && kprintf_hold()))) {
        raw_out(line, n);                       /* (before the threads run, and in a panic) */
    } else {
        if (held_head - held_tail + n > HELD)   /* no room: what waits goes out now */
            kprintf_flush(1);
        for (i = 0; i < n; i++)
            held[held_head++ & (HELD - 1)] = line[i];
        if (deferred && (f & 0x200))            /* a thread with interrupts on: its line out before it goes on */
            kprintf_pump(~0u);
    }
    irq_restore(f);
}

int kvsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap)
{
    struct out o = { buf, n, 0 };
    vfmt(&o, fmt, ap);
    if (n) buf[o.len < n ? o.len : n - 1] = 0;
    return (int)o.len;
}
