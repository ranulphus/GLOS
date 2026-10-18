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

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n && s[i]; i++)
        d[i] = s[i];
    for (; i < n; i++)
        d[i] = 0;
    return d;
}

char *strchr(const char *s, int c)
{
    for (; *s; s++)
        if (*s == (char)c)
            return (char *)s;
    return c ? 0 : (char *)s;
}

char *strstr(const char *s, const char *t)
{
    for (;; s++) {
        size_t i = 0;
        while (t[i] && s[i] == t[i])
            i++;
        if (!t[i])
            return (char *)s;
        if (!*s)
            return 0;
    }
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
