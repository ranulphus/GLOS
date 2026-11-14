/* tests/host/dev_test.c's: a device's memory is the test's own array. */
#ifndef DEVSTUB_MM_H
#define DEVSTUB_MM_H
#include "types.h"
void *mm_dev_map(u32 phys, u32 bytes);
#endif
