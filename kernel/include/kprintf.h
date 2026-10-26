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
extern void (*kprintf_screen)(const char *s, u32 n);    /* /VERBOSE: lines on the text screen too (vm/vcon.c) */
void vcon_start(void);
extern int (*kprintf_hold)(void);       /* 1 while a program's COM1 line is unfinished (vm/vdev.c) */
void kprintf_flush(int force);
void kprintf_direct(void);              /* a panic: no more holding */
void kprintf_defer(void);               /* the threads run: lines wait in a ring for kprintf_pump() */
int kprintf_pump(u32 max);              /* up to max waiting characters to COM1, interrupts on meanwhile; 1 if any */
int kvsnprintf(char *buf, size_t n, const char *fmt, __builtin_va_list ap);
void klog_put(const char *s, u32 n);    /* into the COM1 mirror (glos log), interrupts off */
u32 klog_read(char *out, u32 max);      /* its last max bytes or fewer, from a line start */
u32 klog_pos(void);                     /* bytes ever written to it: a place to follow from */
u32 klog_from(u32 *pos, char *out, u32 max);    /* what came since *pos (moved on), max bytes at most */

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#endif
