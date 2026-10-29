/* The disk cache (kernel/drv/cache.c; docs/disk-cache.md). */
#ifndef K_CACHE_H
#define K_CACHE_H
#include "types.h"

struct bootinfo;
struct trapframe;
struct rmregs;

void cache_init(struct bootinfo *bi);   /* after pci_scan: the drives, the mode, INT 13h trapped if on */
void cache_resident(u32 buf_lin);       /* the stub has gone resident: its block buffer (0: none) */
int cache_int13(struct trapframe *from, struct rmregs *r, int kind);
                                        /* an INT 13h (V86: from 0, RM_INT; protected mode: rm_call's): 1 if the
                                           cache made it and r holds what it returns */
int cache_busy(void);                   /* inside the BIOS for an INT 13h the cache makes: no kill, no posted work */
void cache_nest_ended(int depth);       /* rm_nest_end: the nested calls from depth on are gone */
void cache_port_out(u16 port, u8 v);    /* a write to a trapped port (vdev.c): a watched IDE command register? */
void cache_direct(int on);              /* a direct-mode session starts (bypass) or ends (everything dropped) */
void cache_apply(const char *key, const char *v);      /* [cache] enabled or size changed (config.c) */
void cache_drop(const char *why);       /* every block */
u32 cache_stat(char *b, u32 max);       /* glos cache: a line of JSON */
u32 cache_lbas(char *b, u32 max);       /* glos cache lbas: count mode's sectors */
u32 cache_kb(void);
void cache_end(void);                   /* GLOS leaves: the totals */

/* For kernel/mm/vmm.c, with interrupts off. */
u32 cache_frames(void);                 /* frames it would give back */
u32 cache_reclaim(u32 n);               /* the coldest n blocks' frames back to the bitmap: how many */
int cache_frame_evictable(u32 frame, void *arg);
void cache_evict_frames(u32 first, u32 n);
#endif
