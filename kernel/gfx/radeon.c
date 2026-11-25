/* An ATI Radeon's engine settled before the BIOS sets a mode (the P3's RV200, 2026-11-25). A program GLOS ends
 * by force can leave the card's command processor stuck (DOS-GL's T&L on the P3: RBBM_STATUS 80116100, the setup
 * engine busy, the command FIFO full, the CP's read pointer still). The PC then lived for 20 minutes with the
 * program spinning, and froze 13 s after the runner's clean-up began (its kill): GLOS then sets a mode through
 * the BIOS (the desktop's, or text), whose mode set writes the engine's registers, and a write into a full FIFO
 * holds the PCI bus until there is room, which a stuck engine never makes (the likeliest cause; unproven). So before any mode set GLOS reads RBBM_STATUS (a read is never held), waits for the
 * engine to go idle, and if it doesn't, stops the CP and resets the engines as RetroGPU's HAL does
 * (radeon_engine_reset in hal/src/radeon/engine2d.c, after X.org's RADEONEngineReset). An engine that is idle
 * (64 free FIFO entries, not active) is left alone: a CP left on but idle was proven harmless to the BIOS's mode
 * set (a GL program killed on the P3, 2026-11-24).
 *
 * Moving: the FIFO's count, the CP's read pointer or its queue status changes between two waits. Still for
 * STILL waits (~275 ms): stuck. Never idle in WAITS waits (~1.1 s) though moving: reset all the same (no program
 * may use the engine across GLOS's mode set; the desktop doesn't draw with a Radeon's engine).
 *
 * SURFACE0's registers (0B04h, 0B08h, 0B0Ch) cleared too when asked (DOS-GL's note, 2026-11-24): the device sets
 * SURFACE0 over a GL program's tiled depth buffer and clears it at the program's close; one ended by force never
 * got there, and the CPU's accesses in that range would stay tile-translated, the desktop scrambled. */
#include "kprintf.h"
#include "radeon.h"

#define RBBM_SOFT_RESET     0x00F0
#define   RESET_CP          (1u << 0)
#define   RESET_HI          (1u << 1)
#define   RESET_SE          (1u << 2)
#define   RESET_RE          (1u << 3)
#define   RESET_PP          (1u << 4)
#define   RESET_E2          (1u << 5)
#define   RESET_RB          (1u << 6)
#define HOST_PATH_CNTL      0x0130
#define   HDP_SOFT_RESET    (1u << 26)
#define CP_RB_RPTR          0x0710
#define CP_CSQ_CNTL         0x0740      /* (0: the CP off, the FIFO the host's: PRIDIS_INDDIS) */
#define CP_CSQ_STAT         0x07F8
#define SURFACE0_LOWER      0x0B04
#define SURFACE0_UPPER      0x0B08
#define SURFACE0_INFO       0x0B0C
#define RBBM_STATUS         0x0E40
#define   FIFOCNT           0x7Fu
#define   ACTIVE            (1u << 31)
#define RB3D_DSTCACHE       0x325C
#define RB2D_DSTCACHE       0x342C
#define   DC_FLUSH_ALL      0xFu
#define   DC_BUSY           (1u << 31)

#define STILL 5
#define WAITS 20
#define R(o) r[(o) / 4]

static int idle(u32 st) { return (st & FIFOCNT) >= 64 && !(st & ACTIVE); }

/* The destination caches out, bounded by a count of reads (each is a bus cycle, ~1 us): 0, or -1. */
static int flush(volatile u32 *r)
{
    u32 n;
    R(RB2D_DSTCACHE) = R(RB2D_DSTCACHE) | DC_FLUSH_ALL;
    for (n = 0; R(RB2D_DSTCACHE) & DC_BUSY; n++)
        if (n > 100000)
            return -1;
    R(RB3D_DSTCACHE) = R(RB3D_DSTCACHE) | DC_FLUSH_ALL;
    for (n = 0; R(RB3D_DSTCACHE) & DC_BUSY; n++)
        if (n > 100000)
            return -1;
    return 0;
}

static void reset(volatile u32 *r)
{
    const u32 eng = RESET_CP | RESET_SE | RESET_RE | RESET_PP | RESET_E2 | RESET_RB;
    u32 rbbm, host;
    R(CP_CSQ_CNTL) = 0;                         /* the CP stopped first */
    rbbm = R(RBBM_SOFT_RESET);
    R(RBBM_SOFT_RESET) = rbbm | eng | RESET_HI;
    (void)R(RBBM_SOFT_RESET);
    R(RBBM_SOFT_RESET) = rbbm & ~(eng | RESET_HI);
    (void)R(RBBM_SOFT_RESET);
    R(RBBM_SOFT_RESET) = rbbm;
    (void)R(RBBM_SOFT_RESET);
    /* The caches' registers are the engine's: written only with the FIFO's room back, or they would hold the bus. */
    if ((R(RBBM_STATUS) & FIFOCNT) >= 64)
        flush(r);
    host = R(HOST_PATH_CNTL);
    rbbm = R(RBBM_SOFT_RESET);
    R(RBBM_SOFT_RESET) = rbbm | eng;
    (void)R(RBBM_SOFT_RESET);
    R(RBBM_SOFT_RESET) = rbbm & ~eng;
    (void)R(RBBM_SOFT_RESET);
    R(HOST_PATH_CNTL) = host | HDP_SOFT_RESET;
    (void)R(HOST_PATH_CNTL);
    R(HOST_PATH_CNTL) = host;
    R(RBBM_SOFT_RESET) = rbbm;
}

int radeon_settle_regs(volatile u32 *r, void (*wait)(void), int surfaces)
{
    u32 st = R(RBBM_STATUS), csq, rptr, k, still = 0, st0 = st;
    int how = RADEON_IDLE;
    if (!idle(st)) {
        csq = R(CP_CSQ_STAT), rptr = R(CP_RB_RPTR);
        for (k = 0; k < WAITS && still < STILL; k++) {
            u32 st2, csq2, rptr2;
            wait();
            st2 = R(RBBM_STATUS), csq2 = R(CP_CSQ_STAT), rptr2 = R(CP_RB_RPTR);
            if (idle(st2)) {
                st = st2;
                break;
            }
            still = (st2 & FIFOCNT) == (st & FIFOCNT) && csq2 == csq && rptr2 == rptr ? still + 1 : 0;
            st = st2, csq = csq2, rptr = rptr2;
        }
        if (idle(st)) {
            how = RADEON_SETTLED;
            kprintf("GLOS-DISPLAY radeon idle after %u waits (rbbm=%08x)\n", k + 1, st0);
        } else {
            how = still >= STILL ? RADEON_WEDGED : RADEON_NEVER_IDLE;
            kprintf("GLOS-WARN radeon-%s rbbm=%08x csq_stat=%08x rptr=%x csq_cntl=%08x: the engine reset\n",
                    how == RADEON_WEDGED ? "stuck" : "never-idle", st, csq, rptr, R(CP_CSQ_CNTL));
            reset(r);
            kprintf("GLOS-DISPLAY radeon reset rbbm=%08x\n", R(RBBM_STATUS));
        }
    }
    if (surfaces) {
        R(SURFACE0_INFO) = 0;
        R(SURFACE0_LOWER) = 0;
        R(SURFACE0_UPPER) = 0;
    }
    return how;
}
