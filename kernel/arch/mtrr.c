/* Memory types by the MTRRs (Intel SDM vol. 3A, 11.11), for GLOS_FN_CACHE
 * (API 1.4): a program's conventional memory made write-combining, for a
 * device that fetches it without snooping the CPU's caches (the G400 by AGP
 * cycles). Only the fixed ranges below A0000h are changed: below 1 MB they
 * take precedence over the variable ranges, so the change never meets the
 * BIOS's write-back ranges over RAM (where WC overlapping WB is undefined).
 * Their units are 64 KB below 80000h and 16 KB from there to A0000h. Each
 * unit's type at boot is kept, and given back when the program that changed
 * it ends (mtrr_release). The variable ranges are only logged. */
#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "dpmi.h"

#define MSR_MTRRCAP     0x0FE
#define MSR_PHYSBASE0   0x200
#define MSR_FIX64K      0x250           /* 00000h-7FFFFh, a byte a 64 KB unit */
#define MSR_FIX16K      0x258           /* 80000h-9FFFFh, a byte a 16 KB unit */
#define MSR_DEFTYPE     0x2FF
#define DEF_FE          (1u << 10)      /* the fixed ranges enabled */
#define DEF_E           (1u << 11)      /* the MTRRs enabled */
#define CR0_NW          (1u << 29)
#define CR0_CD          (1u << 30)
#define UNITS           16              /* 8 of 64 KB, 8 of 16 KB */

static int usable;                      /* fixed ranges on, and WC a type the CPU has */
static u8 boot_type[UNITS], cur_type[UNITS];
static struct dpmi_ctx *owner[UNITS];

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

static void wbinvd(void) { __asm__ volatile("wbinvd" ::: "memory"); }

static u32 unit_base(int u) { return u < 8 ? (u32)u << 16 : 0x80000u + ((u32)(u - 8) << 14); }
static u32 unit_size(int u) { return u < 8 ? 0x10000u : 0x4000u; }

void mtrr_init(u32 cpuid_edx)
{
    u64 cap, def, b, m;
    u32 i;
    if (!(cpuid_edx & (1u << 12))) {
        kprintf("GLOS-MTRR none\n");
        return;
    }
    cap = rdmsr(MSR_MTRRCAP);
    def = rdmsr(MSR_DEFTYPE);
    for (i = 0; i < UNITS; i++)
        boot_type[i] = cur_type[i] = (u8)(rdmsr(i < 8 ? MSR_FIX64K : MSR_FIX16K) >> (8 * (i & 7)));
    usable = (cap & (1u << 8)) && (cap & (1u << 10)) && (def & (DEF_E | DEF_FE)) == (DEF_E | DEF_FE);
    kprintf("GLOS-MTRR cap=%x def=%x fix64k=%x%08x fix16k=%x%08x wc=%s\n", (u32)cap, (u32)def,
            (u32)(rdmsr(MSR_FIX64K) >> 32), (u32)rdmsr(MSR_FIX64K), (u32)(rdmsr(MSR_FIX16K) >> 32),
            (u32)rdmsr(MSR_FIX16K), usable ? "yes" : "no");
    for (i = 0; i < (cap & 0xFF) && i < 16; i++) {
        m = rdmsr(MSR_PHYSBASE0 + 2 * i + 1);
        if (!(m & (1u << 11)))
            continue;
        b = rdmsr(MSR_PHYSBASE0 + 2 * i);
        kprintf("GLOS-MTRR var=%u base=%x%08x mask=%x%08x type=%u\n", i, (u32)(b >> 32), (u32)b & ~0xFFFu,
                (u32)(m >> 32), (u32)m & ~0xFFFu, (u32)b & 0xFF);
    }
}

/* The fixed ranges written from cur_type, by the SDM's procedure (11.11.7.2):
   caching off and the caches written back around the change, which is made
   with the MTRRs off. GLOS doesn't use global pages, so CR3 flushes the TLB. */
static void write_fixed(void)
{
    u64 v64 = 0, v16 = 0, def;
    u32 cr0, f, i;
    for (i = 0; i < 8; i++) {
        v64 |= (u64)cur_type[i] << (8 * i);
        v16 |= (u64)cur_type[8 + i] << (8 * i);
    }
    f = irq_save();
    cr0 = read_cr0();
    write_cr0((cr0 | CR0_CD) & ~CR0_NW);
    wbinvd();
    write_cr3(read_cr3());
    def = rdmsr(MSR_DEFTYPE);
    wrmsr(MSR_DEFTYPE, def & ~(u64)DEF_E);
    wrmsr(MSR_FIX64K, v64);
    wrmsr(MSR_FIX16K, v16);
    wbinvd();
    write_cr3(read_cr3());
    wrmsr(MSR_DEFTYPE, def);
    write_cr0(cr0);
    irq_restore(f);
}

/* GLOS_FN_CACHE: lin..lin+bytes, whole units of conventional memory, made
   `type` for context c (MTRR_WC, or MTRR_WB: as at boot). 0, -1 when the
   CPU can't, -2 for a range that isn't whole units below A0000h or a type
   other than those. */
int mtrr_set(struct dpmi_ctx *c, u32 lin, u32 bytes, u32 type)
{
    u32 end = lin + bytes;
    int u, any = 0, changed = 0;
    if (!usable)
        return -1;
    if ((type != MTRR_WC && type != MTRR_WB) || !bytes || end < lin || end > 0xA0000u)
        return -2;
    for (u = 0; u < UNITS; u++) {
        u32 b = unit_base(u), e = b + unit_size(u);
        if (e <= lin || b >= end)
            continue;
        if (b < lin || e > end)
            return -2;                  /* (part of a unit) */
        if (owner[u] && owner[u] != c)
            return -2;                  /* (another program's) */
        any = 1;
    }
    if (!any)
        return -2;
    for (u = 0; u < UNITS; u++) {
        u32 b = unit_base(u);
        u8 t;
        if (b < lin || b >= end)
            continue;
        t = type == MTRR_WB ? boot_type[u] : (u8)type;
        owner[u] = t == boot_type[u] ? 0 : c;
        if (cur_type[u] != t) {
            cur_type[u] = t;
            changed = 1;
        }
    }
    if (changed) {
        write_fixed();
        kprintf("GLOS-MTRR set lin=%x bytes=%x type=%u psp=%04x\n", lin, bytes, type, c ? c->psp : 0);
    }
    return 0;
}

/* A program's context ends: the units it changed as they were at boot. */
void mtrr_release(struct dpmi_ctx *c)
{
    int u, changed = 0;
    for (u = 0; u < UNITS; u++)
        if (owner[u] == c && c) {
            owner[u] = 0;
            cur_type[u] = boot_type[u];
            changed = 1;
        }
    if (changed) {
        write_fixed();
        kprintf("GLOS-MTRR restored psp=%04x\n", c->psp);
    }
}
