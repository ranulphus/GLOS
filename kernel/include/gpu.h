/* The GPU lock: whose the Matrox card's drawing engine is (PRD D6, D24,
 * §9.6; kernel/gfx/gpu.c; docs/milestones-m8.md step B). */
#ifndef K_GPU_H
#define K_GPU_H
#include "types.h"

struct app;
struct glos_gpu;
struct glos_gpu_share;

/* What GLOS keeps of an app's use of the card (in its struct app). */
struct gpu_app {
    u32 regs;                                   /* the registers' place in the app (16 KB), 0 before GLOS_FN_GPU */
    u32 view;                                   /* the card's memory as the app sees it: its own blocks mapped */
    u32 share_lin;                              /* the page GLOS shares with it, in the app ... */
    struct glos_gpu_share *share;               /* ... and in the kernel */
    u32 safe;                                   /* it is in a GLOS or DOS call: its turn may end there */
    u8 granting;                                /* (its turn is being begun: not to be ended meanwhile) */
};

void gpu_watchdog(u32 watchdog_ms);     /* [display] gpu_watchdog changed (0: none) */
void gpu_init(u32 watchdog_ms);                 /* 0 ms: no watchdog */

/* Kernel threads. The engine is one owner's at a time; a kernel thread
   that has it may take it again (nested). */
void gpu_take(void);                            /* waits while an app has it outside a safe point */
void gpu_take_force(void);                      /* ... or not: a session or a mode set (the apps are paused) */
void gpu_give(void);

/* Apps, on their own threads. */
int gpu_app_open(struct app *a, struct glos_gpu *out);  /* GLOS_FN_GPU: 0, or a GLOS_E_ */
int gpu_app_lock(struct app *a);                /* the engine its own (waits): 0, or -1 if it is ended meanwhile */
void gpu_app_unlock(struct app *a);
int gpu_app_holds(const struct app *a);         /* it has the engine outside a safe point: not to be paused there */
void gpu_safe_begin(struct app *a);             /* it entered a GLOS or DOS call: its turn ends here if wanted */
void gpu_safe_end(struct app *a);
int gpu_fault(struct app *a, u32 lin);          /* its page fault at lin: 1 if on its registers (now its own) or
                                                   its blocks of VRAM (mapped again) */
int gpu_vram_alloc(struct app *a, u32 min, u32 max, u32 align, u32 *off, u32 *bytes);  /* GLOS_FN_VRAM: 0, GLOS_E_ */
int gpu_vram_free(struct app *a, u32 off);
void gpu_views_out(void);                       /* mga.c: the card is about to be another's: apps' blocks unmapped */
void gpu_app_gone(struct app *a);               /* its end */

int gpu_guards(u32 phys, u32 size);             /* the range touches the card's registers or memory (0800h for apps) */
u32 gpu_stat(char *buf, u32 max);               /* glos display: a line */

#endif
