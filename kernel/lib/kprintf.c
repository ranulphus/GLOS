/* A small printf for the kernel and its string helpers. */
#include "io.h"
#include "kprintf.h"

void *memset(void *d, int c, size_t n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, size_t n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
int memcmp(const void *a, const void *b, size_t n)
{
    const u8 *p = a, *q = b;
    for (; n; n--, p++, q++)
        if (*p != *q) return *p - *q;
    return 0;
}
size_t strlen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

struct out { char *buf; size_t n, len; };

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
    struct out o = { NULL, 0, 0 };
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    vfmt(&o, fmt, ap);
    __builtin_va_end(ap);
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
