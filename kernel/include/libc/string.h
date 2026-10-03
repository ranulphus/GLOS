/* The C library lwIP needs (third_party/lwip): kernel/lib/kprintf.c and
   kernel/lib/libc.c implement it. */
#ifndef GLOS_LIBC_STRING_H
#define GLOS_LIBC_STRING_H
#include <stddef.h>
void *memcpy(void *d, const void *s, size_t n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
char *strcpy(char *d, const char *s);
int strncmp(const char *a, const char *b, size_t n);
void *memchr(const void *s, int c, size_t n);
#endif
