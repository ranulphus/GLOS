/* qdev.h - a queued drawing engine for the host tests (tests/host/qdev.c). */
#ifndef QDEV_H
#define QDEV_H
#include "gfx.h"

extern const struct gfx_dev qdev;
extern unsigned long qdev_done;                 /* operations done */
void qdev_sync(void);                           /* everything queued, done */
void qdev_refuse(unsigned one_in);              /* decline about one call in this many (0: none) */

#endif
