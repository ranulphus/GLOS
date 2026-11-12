/* What the system is doing (kernel/app/stat.c; GLOS_FN_STAT, M9). */
#ifndef K_STAT_H
#define K_STAT_H
#include "types.h"

u32 stat_apps(void *out, u32 room);             /* struct kprog for each app and job */

#endif
