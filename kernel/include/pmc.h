/* The CPU's performance counters, over SSH (kernel/arch/pmc.c: glos pmc). Each returns 0, or why not. */
#ifndef K_PMC_H
#define K_PMC_H
#include "types.h"

const char *pmc_start(u32 cpuid_edx, u32 ev0, u32 ev1, int os);
const char *pmc_read(u32 cpuid_edx, char *out, u32 room);      /* the line: GLOS-PMC ev0=.. c0=.. ev1=.. c1=.. */
const char *pmc_stop(u32 cpuid_edx);

#endif
