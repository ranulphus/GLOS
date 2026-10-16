/* Watches of directories (kernel/dos/watch.c; GLOS_FN_WATCH, M9). */
#ifndef K_WATCH_H
#define K_WATCH_H
#include "types.h"

int watch_add(void *app, const char *dir, u32 *id);    /* 0, or a GLOS_E_ */
int watch_remove(void *app, u32 id);
void watch_app_gone(void *app);
void watch_path(const char *path);              /* a change there (NULL: somewhere) */
void watch_int21(u16 ax, u16 bx, u16 cx, u16 dx, u32 dx_lin, u32 si_lin);      /* a DOS call about to be made */
void watch_tick(u32 now);

#endif
