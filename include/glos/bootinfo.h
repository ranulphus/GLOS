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
#define BI_F_VM          0x0004                 /* /RUN: keep DOS running in the system VM (M2) */
#define BI_F_SELFTEST    0x0008                 /* /SELFTEST: scheduler test threads beside the VM (M3) */
#define BI_F_NOVME       0x0010                 /* /NOVME: trap every INT and CLI/STI even with VME (M3) */
#define BI_F_SHELL       0x0020                 /* GLOS is the DOS shell (SHELL=, supervisor.md §2.2) */
#define BI_F_AGENT       0x0040                 /* headless: SSH commands run in DOS (no mode option, or /AGENT) */

/* glos_call() functions (the ARPL at bp_call_off, AX = fn, EBX = arg). */
#define GLOS_CALL_LEAVE  1                      /* stop the VM, back to real mode; arg = exit code */
#define GLOS_CALL_DOSPTR 2                      /* unused since M3: bootinfo.indos and .sda instead */
#define GLOS_CALL_EXEC   3                      /* unused since M3: RESIDENT gives the PSP */
#define GLOS_CALL_NEXT   4                      /* the stub asks what to do; arg = how the last EXEC ended
                                                   (INT 21h 4Dh's AX, 10000h + the error if EXEC failed,
                                                   FFFFFFFFh: nothing ran); EAX = 1: EXEC the stub's
                                                   path/tail; 2: halt until an interrupt, then ask again */
#define GLOS_CALL_RESIDENT 5                    /* the stub moves to arg:0, right after GLOS.EXE's PSP */

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
    /* version 1, M2: resuming the loader in V86 mode */
    bi_u32 vm_resume_off;                       /* _vm_resume in the loader's code segment */
    bi_u32 vm_state_off;                        /* save_ss, save_sp, save_ds (words) there */
    bi_u32 bp_call_off;                         /* the ARPL of glos_call() */
    bi_u32 bp_xms_off;                          /* the ARPL of the XMS entry point (entry + 5) */
    bi_u32 sda;                                 /* DOS's swappable data area (linear), 0 unknown */
    bi_u32 kill_off, kill_sp;                   /* _glos_kill and its stack's top, in the code segment */
    bi_u32 xms_ver, xms_rev, xms_hma;           /* XMS mode: the driver's function 00h (AX, BX, DX) */
    bi_u32 hma_used;                            /* XMS mode: the HMA was taken (DOS=HIGH) */
    bi_u32 xms_table;                           /* XMS mode: INT 2Fh 4309h's handle table (linear), 0 none */
    /* M3 */
    bi_u32 indos;                               /* DOS's InDOS flag (linear) */
    bi_u32 lol;                                 /* DOS's List of Lists (INT 21h 52h, linear), 0 unknown */
    bi_u32 font16, font14, font8, font8hi;      /* the video BIOS's 8x16, 8x14 and 8x8 (two halves) fonts */
    bi_u32 stub_paras;                          /* the resident stub's size, in paragraphs */
    /* M3: GLOS as the shell (BI_F_SHELL) */
    char comspec[80];                           /* COMMAND.COM, for batch files and the console (and the agent's) */
    char autoexec[80];                          /* run first through COMSPEC /C; empty: none */
    char console[128];                          /* then, again and again, COMSPEC /C this; empty: COMSPEC */
    /* M3: KEYS\ beside GLOS.EXE, read at start (the kernel has no files yet) */
    unsigned char seed[32];                     /* KEYS\SEED.BIN: entropy from the last run */
    bi_u32 seed_len;
    char hostkey[1024];                         /* KEYS\HOSTKEY: OpenSSH private key (ed25519, unencrypted) */
    bi_u32 hostkey_len;
    char authkeys[2048];                        /* KEYS\AUTHKEYS: authorized_keys lines (ssh-ed25519) */
    bi_u32 authkeys_len;
};

/* The resident stub's data (loader/stub.asm), at offset 0 of its segment.
   Byte arrays and naturally aligned fields only: no compiler pads it. */
struct stub_data {
    unsigned char mode, a20init, resident, nxms;        /* mode 1: XMS */
    unsigned char shell, realmode, pad[2];              /* realmode: no kernel, EXEC path/tail forever */
    bi_u32 xms;                                 /* far pointer: the XMS driver's entry */
    unsigned short handles[4];
    bi_u32 env_src;                             /* far pointer: the master environment to install */
    unsigned short env_len, env_paras;          /* its bytes; the block to allocate (0: keep ours) */
    char path[80];                              /* EXEC: the program, ASCIIZ */
    unsigned char tail[128];                    /* EXEC: length, text, CR */
    char comspec[80];                           /* the shell's fallback: COMSPEC ... */
    unsigned char fbtail[64];                   /* ... with this tail (length, text, CR) */
};

#endif
