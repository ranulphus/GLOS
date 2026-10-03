/* The rest of the C library lwIP uses (kernel/include/libc). */
#include "kprintf.h"

void *memmove(void *d, const void *s, size_t n)
{
    u8 *p = d;
    const u8 *q = s;
    if (p < q || p >= q + n)
        while (n--)
            *p++ = *q++;
    else
        while (n--)
            p[n] = q[n];
    return d;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++) != 0) ;
    return r;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (u8)*a - (u8)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n; n--, a++, b++)
        if (*a != *b || !*a)
            return (u8)*a - (u8)*b;
    return 0;
}

int atoi(const char *s)
{
    int v = 0, neg = 0;
    while (*s == ' ')
        s++;
    if (*s == '-' || *s == '+')
        neg = *s++ == '-';
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

int snprintf(char *buf, size_t n, const char *fmt, ...)
{
    __builtin_va_list ap;
    int r;
    __builtin_va_start(ap, fmt);
    r = kvsnprintf(buf, n, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

void *memchr(const void *s, int c, size_t n)
{
    const u8 *p = s;
    for (; n; n--, p++)
        if (*p == (u8)c)
            return (void *)p;
    return NULL;
}
