/* Kernel output: COM1 lines (supervisor.md §18) and COM2 bytes (gdb stub). */
#ifndef K_KPRINTF_H
#define K_KPRINTF_H
#include "types.h"

#define COM1 0x3F8
#define COM2 0x2F8

void serial_init(u16 port);
void serial_putc(u16 port, char c);
int serial_getc(u16 port);              /* -1 when nothing is waiting */
void kprintf(const char *fmt, ...);     /* to COM1: %s %c %d %u %x %p, width and 0 flag */
int ksnprintf(char *buf, size_t n, const char *fmt, ...);
void kvprintf(const char *fmt, __builtin_va_list ap);
extern int (*kprintf_hold)(void);       /* 1 while a program's COM1 line is unfinished (vm/vdev.c) */
void kprintf_flush(int force);
void kprintf_direct(void);              /* a panic: no more holding */
int kvsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#endif
