/* devstub/kprintf.h - the log for dev_test.c: kept in a buffer the test reads. */
#ifndef K_KPRINTF_H
#define K_KPRINTF_H
#include "types.h"
void kprintf(const char *fmt, ...);
int ksnprintf(char *buf, size_t n, const char *fmt, ...);
#endif
