/* Kernel basic types (32-bit x86, freestanding). */
#ifndef K_TYPES_H
#define K_TYPES_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef int                s32;
typedef unsigned int       size_t;
typedef u32                uintptr_t;

#define NULL ((void *)0)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define PAGE_SIZE 4096u

#endif
