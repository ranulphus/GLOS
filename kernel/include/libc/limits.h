/* The limits lwIP uses (i386, ILP32); GCC's own limits.h wants the C
   library's beside it. No SSIZE_MAX: lwIP then defines ssize_t itself. */
#ifndef GLOS_LIBC_LIMITS_H
#define GLOS_LIBC_LIMITS_H
#define CHAR_BIT   8
#define SCHAR_MAX  127
#define SCHAR_MIN  (-128)
#define UCHAR_MAX  255
#define CHAR_MAX   SCHAR_MAX
#define CHAR_MIN   SCHAR_MIN
#define SHRT_MAX   32767
#define SHRT_MIN   (-32768)
#define USHRT_MAX  65535
#define INT_MAX    2147483647
#define INT_MIN    (-INT_MAX - 1)
#define UINT_MAX   4294967295U
#define LONG_MAX   2147483647L
#define LONG_MIN   (-LONG_MAX - 1)
#define ULONG_MAX  4294967295UL
#endif
