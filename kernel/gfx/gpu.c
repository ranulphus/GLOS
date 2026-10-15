/* The GPU lock (PRD D6, D24, §9.6; docs/milestones-m8.md step B): whose
 * the Matrox card's drawing engine is.
 *
 * The owner is a kernel thread (the desktop's composer, the test card, glos
 * shot: kernel/gfx/mga.c takes it for each use) or an app with its own copy
 * of MGA-Glide's HAL (DOS-GL). The HAL keeps what it believes of the
 * registers in variables of its own, FIFO space included, so a copy may be
 * stopped only where it can be told: an app's turn ends when someone else
 * wants the engine and the app is at a safe point (in a GLOS API call, or
 * waiting for the system VM in a DOS or BIOS call: never inside the HAL's
 * own work), or by force when a session takes the card (the apps are paused
 * then) or the app is ended. The app learns of it from the page GLOS shares
 * with it (glos_gpu_share: held, lost) before its next piece of work, and
 * resynchronises (engine_resync).
 *
 * The registers are mapped into an app only while it has the lock. Its
 * first touch without it faults, and the fault takes the lock for it (the
 * same wait as GLOS_FN_GPU_LOCK's), so a program that never asks is kept
 * out of everyone else's turn too; it just doesn't know when it lost one.
 *
 * Between two owners the engine is drained: a second at most, after which
 * the HAL resets it, and the owner that left it hung (an app) is ended. The
 * kernel's HAL resynchronises when it gets the engine back from an app.
 *
 * The watchdog: a thread that has waited for the engine longer than the
 * limit ([display] gpu_watchdog, 500 ms by default) while an app holds it
 * outside a safe point ends that app and resets the engine.
 *
 * The hardware cursor and the display start are not the engine's: their
 * registers are the RAMDAC's and the CRTC's, written directly, so they move
 * whoever has the engine (mga.c). */
#include <string.h>

#define GLOS_KERNEL
#include "glos/api.h"
#include "../app/appint.h"
#include "gpu.h"
#include "io.h"
#include "kprintf.h"
#include "mgadisp.h"
#include "mm.h"
#include "sched.h"
#include "timer.h"

#define KERNEL ((const void *)1)                /* (g.prev: the kernel's HAL had it last) */
#define REGS   0x4000u                          /* the registers' aperture */

static struct {
    struct thread *kt;                          /* a kernel thread has it (depth times) ... */
    u32 depth;
    struct app *app;                            /* ... or an app */
    const void *prev;                           /* whose it was last: KERNEL, an app, or 0 (unknown) */
    u8 reset;                                   /* the watchdog ended an app: reset before the next use */
    struct waitq q;
    u32 waiting;
    u32 limit;                                  /* the watchdog's, in ticks; 0: none */
    u32 turns, faults, watchdogs, hangs;
    struct app *open[16];                       /* the apps that asked for the card (GLOS_FN_GPU) */
} g;

void gpu_init(u32 watchdog_ms)
{
    g.limit = watchdog_ms ? watchdog_ms + (watchdog_ms * 24 + 999) / 1000 : 0;
}

/* ---- an app's registers in its address space */

static void map_regs(struct app *a)             /* its own thread, its space */
{
    u32 i;
    for (i = 0; i < REGS; i += 4096)
        mm_map(a->gpu.regs + i, mga_regs_phys() + i, MM_W | MM_U | MM_UC | MM_MAPPED);
}

static void unmap_regs(struct app *a)           /* from anywhere */
{
    u32 i;
    if (!a->ctx || !a->gpu.regs)
        return;
    for (i = 0; i < REGS; i += 4096)
        mm_space_set(a->ctx->cr3, a->gpu.regs + i, 0);
}

/* The app's turn ends (interrupts off). */
static void revoke(struct app *a)
{
    unmap_regs(a);
    a->gpu.share->held = 0;
    a->gpu.share->want = 0;
    g.app = 0;
    thread_wake(&g.q);
}

static int safe(const struct app *a) { return (a->gpu.safe && !a->gpu.granting) || a->kill; }

static void watchdog(struct app *a, u32 waited)
{
    kprintf("GLOS-GPU watchdog app=%u waited=%ums\n", a->id, (u32)(((unsigned long long)waited * 125) >> 7));
    g.watchdogs++;
    app_kill(a, "gpu-watchdog");
    revoke(a);
    g.reset = 1;
}

/* Until the engine may be taken by `me` (0: a kernel thread), with
   interrupts off: nobody has it, or an app's turn could be ended (it is,
   then). force: an app's turn ends wherever it is. */
static void wait_turn(int force, struct app *me)
{
    u32 start = timer_ticks();
    for (;;) {
        if (!g.kt && !g.app)
            return;
        if (g.app && g.app != me && (force || safe(g.app))) {
            revoke(g.app);
            return;
        }
        if (me && me->kill)
            return;
        if (g.app && g.limit && timer_ticks() - start >= g.limit) {
            watchdog(g.app, timer_ticks() - start);
            continue;
        }
        if (g.app)
            g.app->gpu.share->want = 1;
        g.waiting++;
        thread_wait(&g.q, g.app && g.limit ? start + g.limit : 0);
        g.waiting--;
    }
}

/* The engine was someone else's: drained (and reset if it had hung, which
   the last owner, an app, pays for), the watchdog's reset done. */
static void handover(int kernel)
{
    const void *prev = g.prev;
    g.turns++;
    if (mga_handover(kernel, g.reset) < 0 && prev && prev != KERNEL) {
        g.hangs++;                              /* (an app that is gone is no longer prev) */
        app_kill((struct app *)prev, "gpu-hang");
    }
    g.reset = 0;
}

/* ---- kernel threads */

static void take(int force)
{
    u32 f = irq_save();
    if (g.kt == current) {
        g.depth++;
        irq_restore(f);
        return;
    }
    do
        wait_turn(force, 0);
    while (g.kt || g.app);
    g.kt = current;
    g.depth = 1;
    irq_restore(f);
    if (g.prev != KERNEL || g.reset) {
        handover(1);
        g.prev = KERNEL;
    }
}

void gpu_take(void) { take(0); }
void gpu_take_force(void) { take(1); }

void gpu_give(void)
{
    u32 f = irq_save();
    if (g.kt == current && --g.depth == 0) {
        g.kt = 0;
        thread_wake(&g.q);
    }
    irq_restore(f);
}

/* ---- apps */

int gpu_app_open(struct app *a, struct glos_gpu *out)
{
    const mga_card_info *c = mga_card();
    struct gpu_app *ga = &a->gpu;
    if (!c)
        return GLOS_E_NO_GPU;
    if (!ga->share) {
        struct glos_gpu_share *s = mm_win_alloc(4096);
        u32 lin;
        if (!s)
            return GLOS_E_MEMORY;
        if (win_map(s, 4096, &lin) != 0) {
            mm_win_free(s, 4096);
            return GLOS_E_MEMORY;
        }
        if (lin_reserve(REGS, &ga->regs) != 0 || lin_reserve(c->vram, &ga->view) != 0) {
            if (ga->regs)
                phys_unmap(ga->regs);
            phys_unmap(lin);
            mm_win_free(s, 4096);
            ga->regs = 0;
            return GLOS_E_MEMORY;
        }
        s->size = sizeof *s;
        ga->share_lin = lin;
        ga->share = s;
        for (u32 i = 0; i < ARRAY_SIZE(g.open); i++)
            if (!g.open[i]) {
                g.open[i] = a;
                break;
            }
    }
    memset(out, 0, sizeof *out);
    out->size = sizeof *out;
    out->device = c->device;
    out->revision = c->revision;
    out->vram = c->vram;
    out->regs = ga->regs;
    out->base = ga->view;
    out->share = ga->share_lin;
    out->bpp = c->bpp;
    out->fb_phys = c->fb_phys;
    out->regs_phys = c->regs_phys;
    out->option = c->option;
    out->subsys = c->subsys;
    return 0;
}

int gpu_app_lock(struct app *a)
{
    u32 f;
    if (!a->gpu.share)
        return -1;
    f = irq_save();
    if (g.app == a) {
        irq_restore(f);
        return 0;
    }
    for (;;) {
        while (!mga_card() && !a->kill)          /* no desktop on the card now: until there is (gpu_give wakes) */
            thread_wait(&g.q, 0);
        wait_turn(0, a);
        if ((!g.kt && !g.app && mga_card()) || a->kill)
            break;
    }
    if (a->kill) {
        irq_restore(f);
        return -1;
    }
    g.app = a;
    a->gpu.granting = 1;                        /* (in a GLOS call, but not to be ended until it has begun) */
    irq_restore(f);
    if (g.prev != a || g.reset) {
        handover(0);
        if (g.prev != a)
            a->gpu.share->lost++;
        g.prev = a;
    }
    f = irq_save();
    map_regs(a);
    a->gpu.share->held = 1;
    a->gpu.granting = 0;
    irq_restore(f);
    return 0;
}

int gpu_app_holds(const struct app *a) { return g.app == a && !a->gpu.safe; }

void gpu_app_unlock(struct app *a)
{
    u32 f = irq_save();
    if (g.app == a)
        revoke(a);
    irq_restore(f);
}

void gpu_safe_begin(struct app *a)
{
    u32 f = irq_save();
    a->gpu.safe++;
    if (g.app == a && g.waiting && !a->gpu.granting)
        revoke(a);
    irq_restore(f);
}

void gpu_safe_end(struct app *a)
{
    u32 f = irq_save();
    if (a->gpu.safe)
        a->gpu.safe--;
    irq_restore(f);
}

/* ---- an app's blocks of the card's memory (step C), seen at their offsets
   from its view's base. They are mapped while the desktop is on the card;
   when it leaves (a session, another mode) they are unmapped first, and a
   touch waits for the next desktop, where they are again (mga.c keeps their
   contents meanwhile). */

static void view_map(struct app *a, u32 off, u32 bytes)        /* its own thread */
{
    const mga_card_info *c = mga_card();
    u32 i, phys = c ? c->fb_phys : 0;
    for (i = 0; i < bytes && c; i += 4096)
        mm_map(a->gpu.view + off + i, phys + off + i, MM_W | MM_U | MM_UC | MM_MAPPED);
}

static void view_unmap(struct app *a, u32 off, u32 bytes)      /* from anywhere */
{
    u32 i;
    if (!a->ctx)
        return;
    for (i = 0; i < bytes; i += 4096)
        mm_space_set(a->ctx->cr3, a->gpu.view + off + i, 0);
}

void gpu_views_out(void)
{
    u32 i, off, start, bytes;
    for (i = 0; i < ARRAY_SIZE(g.open); i++) {
        struct app *a = g.open[i];
        if (!a)
            continue;
        for (off = 0; off < mga_vram_size(); off = start + bytes)
            if (mga_app_next(a->id, off, &start, &bytes))
                view_unmap(a, start, bytes);
            else
                break;
    }
}

int gpu_vram_alloc(struct app *a, u32 min, u32 max, u32 align, u32 *off, u32 *bytes)
{
    if (!a->gpu.share || !mga_card())
        return GLOS_E_NO_GPU;
    if (mga_app_vram(a->id, min, max, align, off, bytes) != 0)
        return GLOS_E_MEMORY;
    view_map(a, *off, *bytes);
    return 0;
}

int gpu_vram_free(struct app *a, u32 off)
{
    u32 bytes = a->gpu.share ? mga_app_vram_free(a->id, off) : 0;
    if (!bytes)
        return GLOS_E_ARGUMENT;
    view_unmap(a, off, bytes);
    return 0;
}

int gpu_fault(struct app *a, u32 lin)
{
    u32 start, bytes;
    if (a->gpu.regs && lin >= a->gpu.regs && lin - a->gpu.regs < REGS) {
        g.faults++;
        gpu_app_lock(a);                        /* (ended meanwhile: its return to its code ends it) */
        return 1;
    }
    if (!a->gpu.view || lin < a->gpu.view || lin - a->gpu.view >= mga_vram_size()
        || !mga_app_block(a->id, lin - a->gpu.view, 1, &start, &bytes))
        return 0;
    while (!mga_card() && !a->kill) {           /* the card is a session's now: until the next desktop */
        u32 f = irq_save();
        if (!mga_card())
            thread_wait(&g.q, 0);
        irq_restore(f);
    }
    if (!a->kill && mga_app_block(a->id, lin - a->gpu.view, 1, &start, &bytes))
        view_map(a, start, bytes);
    return 1;
}

void gpu_app_gone(struct app *a)
{
    u32 f = irq_save(), i;
    if (g.app == a) {
        g.app = 0;
        thread_wake(&g.q);
    }
    if (g.prev == a)
        g.prev = 0;
    for (i = 0; i < ARRAY_SIZE(g.open); i++)
        if (g.open[i] == a)
            g.open[i] = 0;
    irq_restore(f);
    mga_app_gone(a->id);
    if (a->gpu.share)
        mm_win_free(a->gpu.share, 4096);        /* (its mappings went with its context) */
    a->gpu.share = 0;
}

u32 gpu_stat(char *buf, u32 max)
{
    char who[16];
    if (g.app)
        ksnprintf(who, sizeof who, "app%u", g.app->id);
    else
        ksnprintf(who, sizeof who, "%s", g.kt ? "kernel" : "none");
    return (u32)ksnprintf(buf, max, "gpu owner=%s turns=%u faults=%u watchdogs=%u hangs=%u\n", who, g.turns,
                          g.faults, g.watchdogs, g.hangs);
}
