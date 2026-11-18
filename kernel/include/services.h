/* Background apps (kernel/core/services.c; phase D, D7): [services] run's programs, started with GLOS and started
 * again when they end. */
#ifndef K_SERVICES_H
#define K_SERVICES_H
#include "types.h"

struct bootinfo;
void services_init(const struct bootinfo *bi);  /* their supervisor's thread (agent mode) */
void services_changed(void);                    /* [services] run changed: read again */
u32 services_stat(char *buf, u32 max);          /* glos services: a line each */
int services_ask(const char *name, int what);   /* glos services stop NAME (1), start NAME (2): 0; -1 no such */

#endif
