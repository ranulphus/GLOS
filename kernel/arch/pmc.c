/* The CPU's performance counters for a measurement over SSH (DOS-GL's request with the owner's choice,
 * 2026-11-24: GLQuake on the P3's Radeon 7500 ~6% slower after a change, and only the counters can say whether
 * it is the instruction cache, the branch predictor or stalls).
 *
 *   glos pmc start EV0 [EV1] [os]   the two counters set to these events and zeroed
 *   glos pmc read                   GLOS-PMC ev0=.. c0=.. ev1=.. c1=.. tsc=.. (the counts and the TSC, hexadecimal)
 *   glos pmc stop                   both off
 *
 * Intel's P6 family alone (CPUID family 6: Pentium Pro, II, III, M; GenuineIntel): PERFEVTSEL0/1 (MSRs 186h,
 * 187h) and PERFCTR0/1 (C1h, C2h, 40 bits). An event is given in hexadecimal as its unit mask and event number,
 * UUEE (81: IFU_IFETCH_MISS; C0: INST_RETIRED; C5: BR_MISS_PRED_RETIRED; 79: CPU_CLK_UNHALTED). They count in
 * user mode (CPL > 0: DPMI programs, which run in ring 3, and the V86 machine) unless "os" is given, which adds
 * the kernel's ring 0. EVTSEL0's enable bit starts both. While they run CR4.PCE is set, so a program can read
 * them itself with RDPMC. They count for the whole PC (every thread, every program), not for one program.
 * Anything else (a Pentium 4, an AMD, a CPU without MSRs) is refused: a missing MSR would fault in ring 0.
 * From Intel's Software Developer's Manual (the P6 family's performance-monitoring MSRs). */
#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "pmc.h"

#define EVTSEL0 0x186
#define EVTSEL1 0x187
#define PERFCTR0 0xC1
#define PERFCTR1 0xC2
#define SEL_USR (1u << 16)
#define SEL_OS  (1u << 17)
#define SEL_EN  (1u << 22)
#define CR4_PCE (1u << 8)

static u32 ev[2], running;

static u64 rdmsr(u32 msr)
{
    u32 lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

static void wrmsr(u32 msr, u64 v)
{
    __asm__ volatile("wrmsr" :: "c"(msr), "a"((u32)v), "d"((u32)(v >> 32)));
}

static void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf));
}

/* 0 if this CPU is a P6 with MSRs; else why not. */
static const char *p6(u32 cpuid_edx)
{
    u32 a, b, c, d;
    if (!cpuid_edx)
        return "no CPUID";
    if (!(cpuid_edx & (1u << 5)))
        return "no MSRs";
    cpuid(0, &a, &b, &c, &d);
    if (b != 0x756E6547u || d != 0x49656E69u || c != 0x6C65746Eu)      /* "GenuineIntel" */
        return "not an Intel CPU (only Intel's P6 family is known)";
    cpuid(1, &a, &b, &c, &d);
    if (((a >> 8) & 0x0F) != 6)
        return "not a P6-family CPU (a Pentium Pro, II, III or M)";
    return 0;
}

const char *pmc_start(u32 cpuid_edx, u32 ev0, u32 ev1, int os)
{
    const char *why = p6(cpuid_edx);
    u32 f, mode = SEL_USR | (os ? SEL_OS : 0);
    if (why)
        return why;
    f = irq_save();
    wrmsr(EVTSEL0, 0);                          /* (both stopped while they are set) */
    wrmsr(EVTSEL1, (ev1 & 0xFFFF) | mode);
    wrmsr(PERFCTR0, 0);
    wrmsr(PERFCTR1, 0);
    wrmsr(EVTSEL0, (ev0 & 0xFFFF) | mode | SEL_EN);
    if (cpu_has_cr4)
        cpu_set_cr4(CR4_PCE, 0);
    ev[0] = ev0 & 0xFFFF, ev[1] = ev1 & 0xFFFF, running = 1;
    irq_restore(f);
    kprintf("GLOS-PMC start ev0=%x ev1=%x os=%d\n", ev[0], ev[1], os);
    return 0;
}

const char *pmc_read(u32 cpuid_edx, char *out, u32 room)
{
    const char *why = p6(cpuid_edx);
    u64 c0, c1, t;
    u32 f;
    if (why)
        return why;
    f = irq_save();
    c0 = rdmsr(PERFCTR0) & 0xFFFFFFFFFFull;
    c1 = rdmsr(PERFCTR1) & 0xFFFFFFFFFFull;
    t = rdtsc();
    irq_restore(f);
    ksnprintf(out, room, "GLOS-PMC ev0=%x c0=%02x%08x ev1=%x c1=%02x%08x tsc=%08x%08x running=%u\n", ev[0],
              (u32)(c0 >> 32), (u32)c0, ev[1], (u32)(c1 >> 32), (u32)c1, (u32)(t >> 32), (u32)t, running);
    return 0;
}

const char *pmc_stop(u32 cpuid_edx)
{
    const char *why = p6(cpuid_edx);
    u32 f;
    if (why)
        return why;
    f = irq_save();
    wrmsr(EVTSEL0, 0);
    wrmsr(EVTSEL1, 0);
    if (cpu_has_cr4)
        cpu_set_cr4(0, CR4_PCE);
    running = 0;
    irq_restore(f);
    kprintf("GLOS-PMC stop\n");
    return 0;
}
