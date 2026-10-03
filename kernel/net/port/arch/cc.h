/* lwIP's port to the GLOS kernel (third_party/lwip, M3 item 3): types from
 * the compiler's freestanding headers, lwIP's own ctype and format macros,
 * diagnostics on COM1, and the kernel's random numbers. */
#ifndef GLOS_LWIP_CC_H
#define GLOS_LWIP_CC_H

#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H    1
#define LWIP_NO_UNISTD_H   1

#define X8_F  "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "u"

#define BYTE_ORDER LITTLE_ENDIAN

void glos_lwip_diag(const char *fmt, ...);
void glos_lwip_assert(const char *msg, const char *file, int line) __attribute__((noreturn));
unsigned int glos_net_rand(void);

#define LWIP_PLATFORM_DIAG(x)   do { glos_lwip_diag x; } while (0)
#define LWIP_PLATFORM_ASSERT(x) glos_lwip_assert((x), __FILE__, __LINE__)
#define LWIP_RAND()             ((u32_t)glos_net_rand())

#endif
