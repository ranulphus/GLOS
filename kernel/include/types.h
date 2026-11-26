/* Kernel basic types (32-bit x86, freestanding). */
#ifndef K_TYPES_H
#define K_TYPES_H

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef short              s16;
typedef int                s32;
typedef __SIZE_TYPE__      size_t;     /* (the compiler's own: unsigned int here; DJGPP's, where sdk/fm builds the
                                           kernel's FM driver, is unsigned long, as its stddef.h says) */
typedef u32                uintptr_t;

#ifndef NULL
#define NULL ((void *)0)
#endif
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define PAGE_SIZE 4096u

#endif
