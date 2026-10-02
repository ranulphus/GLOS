/* GLOS boot contract, version 1 (docs/supervisor.md §2): what GLOS.EXE (16-bit,
 * Open Watcom) hands the kernel (32-bit, gcc). Every field is 32 bits or an
 * array of them, so both compilers lay it out the same way. */
#ifndef GLOS_BOOTINFO_H
#define GLOS_BOOTINFO_H

#ifdef __WATCOMC__
typedef unsigned long  bi_u32;
#else
typedef unsigned int   bi_u32;
#endif

#define BOOTINFO_MAGIC   0x4F4F4247UL           /* "GBOO" */
#define BOOTINFO_VERSION 1

#define KERNEL_MAGIC     0x4B4F4C47UL           /* "GLOK", first word of GLOSK.BIN */
#define KERNEL_LINK      0xC0100000UL           /* where the kernel is linked */
#define KERNEL_PDE       0x300                  /* page-directory slot of 0xC0000000 */

/* Selectors in the loader's GDT, kept by the kernel's (supervisor.md §3.1). */
#define BOOT_SEL_CODE32  0x08
#define BOOT_SEL_DATA32  0x10
#define BOOT_SEL_CODE16  0x38                   /* base = loader CS * 16 */
#define BOOT_SEL_DATA16  0x40                   /* base = loader DS * 16 */

#define BI_MEM_RANGES    32
#define BI_MEM_FREE      1                      /* usable by GLOS */
#define BI_MEM_RESERVED  2

#define BI_MODE_RAW      0                      /* no XMS driver: GLOS owns extended memory */
#define BI_MODE_XMS      1                      /* GLOS owns the XMS blocks it locked */

/* Command-line flags. */
#define BI_F_ROUNDTRIP   0x0001                 /* /ROUNDTRIP: tick for a second, report, return */
#define BI_F_GDB         0x0002                 /* /GDB: stop in the gdb stub on COM2 first */

struct bi_range { bi_u32 base, length, type; };

struct bootinfo {
    bi_u32 magic, version, size;
    bi_u32 flags;
    bi_u32 mode;                                /* BI_MODE_* */
    bi_u32 n_ranges;
    struct bi_range range[BI_MEM_RANGES];       /* physical memory GLOS may use, above 1 MB */
    bi_u32 kernel_phys, kernel_size;            /* the image in extended memory (contiguous) */
    bi_u32 kernel_total;                        /* image plus zeroed bss, rounded to pages */
    bi_u32 pd_phys;                             /* the loader's page directory */
    bi_u32 cs_base, ds_base;                    /* the loader's real-mode CS and DS, times 16 */
    bi_u32 ret_off;                             /* offset of pm_ret in the loader's code segment */
    bi_u32 cpu_family;                          /* 4, 5, 6 */
    bi_u32 cpuid_edx;                           /* CPUID 1 EDX, 0 without CPUID */
    bi_u32 pic_mask;                            /* master in bits 0-7, slave in 8-15, as found */
    bi_u32 video_mode;                          /* INT 10h 0Fh */
    bi_u32 a20_initial;                         /* 1 if A20 was on when GLOS started */
    bi_u32 result;                              /* the kernel's exit code */
};

#endif
