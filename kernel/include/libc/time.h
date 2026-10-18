/* The C library lwIP needs (third_party/lwip): its SNTP client includes this
 * for a ctime() it calls only in debug builds, which GLOS's aren't (M10). */
#ifndef GLOS_LIBC_TIME_H
#define GLOS_LIBC_TIME_H

typedef long time_t;

#endif
