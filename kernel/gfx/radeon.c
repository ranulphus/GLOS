/* An ATI Radeon's engine settled before the BIOS sets a mode (the P3's RV200, 2026-11-25). A program GLOS ends
 * by force can leave the card's command processor stuck (DOS-GL's T&L on the P3: RBBM_STATUS 80116100, the setup
 * engine busy, the command FIFO full, the CP's read pointer still). The PC then lived for 20 minutes with the
 * program spinning, and froze 13 s after the runner's clean-up began (its kill): GLOS then sets a mode through
 * the BIOS (the desktop's, or text), whose mode set writes the engine's registers, and a write into a full FIFO
 * holds the PCI bus until there is room, which a stuck engine never makes (the likeliest cause; DOS-GL's own
 * drawing after a reset froze the P3 the same way, 18:44 that day). So before any mode set GLOS reads
 * RBBM_STATUS (a read is never held) and waits for the engine to go idle. An engine that is idle (64 free FIFO
 * entries, not active) is left alone: a CP left on but idle was proven harmless to the BIOS's mode set (a GL
 * program killed on the P3, 2026-11-24).
 *
 * Moving: the FIFO's count, the CP's read pointer or its queue status changes between two waits. Still for
 * STILL waits (~275 ms): stuck. Never idle in WAITS waits (~1.1 s) though moving: reset all the same (no program
 * may use the engine across GLOS's mode set; the desktop doesn't draw with a Radeon's engine).
 *
 * The reset, first as RetroGPU's HAL resets after a lockup (radeon_engine_reset in hal/src/radeon/engine2d.c,
 * after X.org's RADEONEngineReset): the CP stopped, the engines' and the host interface's reset bits set and
 * cleared, the caches flushed (only with the FIFO's room back: those registers are the engine's), the engines'
 * again and the host data path's. That emptied the FIFO on the P3 but left the engine busy (DOS-GL, 18:44), so if
 * the FIFO's room isn't back then, a harder one in the order of Linux KMS's r100 reset after a hang (a behaviour
 * reference only): the CP stopped and its ring's pointers put to 0, bus mastering off (BUS_CNTL's
 * BUS_MASTER_DIS and the PCI command register's, by bm()), the engines' reset held about half a second, then the
 * CP's, bus mastering back. If the FIFO still has no room the card is dead to GLOS: RADEON_DEAD, and its callers
 * make no BIOS mode set at all (the screen stays as it is; the PC lives).
 *
 * SURFACE0's registers (0B04h, 0B08h, 0B0Ch) cleared too when asked (DOS-GL's note, 2026-11-24): the device sets
 * SURFACE0 over a GL program's tiled depth buffer and clears it at the program's close; one ended by force never
 * got there, and the CPU's accesses in that range would stay tile-translated, the desktop scrambled. */
#include "kprintf.h"
#include "radeon.h"

#define BUS_CNTL            0x0030
#define   BUS_MASTER_DIS    (1u << 6)
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
#define CP_RB_CNTL          0x0704
#define   RB_RPTR_WR_ENA    (1u << 31)
#define   RB_NO_UPDATE      (1u << 27)      /* (no write-back of the read pointer to the host's memory) */
#define SCRATCH_UMSK        0x0770          /* (which scratch registers are written back to memory: 0 none) */
#define CP_RB_RPTR          0x0710
#define CP_RB_WPTR          0x0714
#define CP_RB_RPTR_WR       0x071C
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
#define HOLD 9                          /* (waits: about half a second) */
#define R(o) r[(o) / 4]

static int idle(u32 st) { return (st & FIFOCNT) >= 64 && !(st & ACTIVE); }
static int room(u32 st) { return (st & FIFOCNT) >= 64; }

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

static void reset(volatile u32 *r, void (*wait)(void))
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
    if (room(R(RBBM_STATUS)))
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
    wait();                                     /* (a moment for the engine before it is looked at again) */
}

static void reset_hard(volatile u32 *r, void (*wait)(void), void (*bm)(int on))
{
    u32 bus = R(BUS_CNTL), rb = R(CP_RB_CNTL), k;
    R(CP_CSQ_CNTL) = 0;                         /* the CP stopped, its ring's pointers 0 */
    R(CP_RB_CNTL) = rb | RB_RPTR_WR_ENA;
    R(CP_RB_RPTR_WR) = 0;
    R(CP_RB_WPTR) = 0;
    R(CP_RB_CNTL) = rb;
    R(BUS_CNTL) = bus | BUS_MASTER_DIS;         /* bus mastering off, the card's and the bus's */
    if (bm)
        bm(0);
    R(RBBM_SOFT_RESET) = RESET_SE | RESET_RE | RESET_PP | RESET_RB;
    (void)R(RBBM_SOFT_RESET);
    for (k = 0; k < HOLD; k++)
        wait();
    R(RBBM_SOFT_RESET) = 0;
    wait();
    R(RBBM_SOFT_RESET) = RESET_CP;              /* then the CP's */
    (void)R(RBBM_SOFT_RESET);
    for (k = 0; k < HOLD; k++)
        wait();
    R(RBBM_SOFT_RESET) = 0;
    wait();
    if (bm)
        bm(1);
    R(BUS_CNTL) = bus;
}

int radeon_settle_regs(volatile u32 *r, void (*wait)(void), void (*bm)(int on), int surfaces)
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
            reset(r, wait);
            if (!room(st = R(RBBM_STATUS))) {
                kprintf("GLOS-WARN radeon-still rbbm=%08x: the harder reset (bus mastering off)\n", st);
                reset_hard(r, wait, bm);
                st = R(RBBM_STATUS);
            }
            if (!room(st)) {
                how = RADEON_DEAD;
                kprintf("GLOS-WARN radeon-dead rbbm=%08x: no mode set by the BIOS (its writes would hold the bus)\n", st);
            } else {
                kprintf("GLOS-DISPLAY radeon reset rbbm=%08x%s\n", st, idle(st) ? "" : " (active, its FIFO free)");
            }
        }
    }
    if (surfaces && how != RADEON_DEAD) {
        R(SURFACE0_INFO) = 0;
        R(SURFACE0_LOWER) = 0;
        R(SURFACE0_UPPER) = 0;
    }
    return how;
}

/* A session's end or a kill, the engine settled (2026-11-26, DOS-GL's P3 reset, insurance whatever its cause):
   nothing the program left running may reach memory that is GLOS's again once its memory is freed. The CP
   stopped (its fetches from the program's ring), and no write-back to host memory (the read pointer's, the
   scratch registers'). Bus mastering is left as it is: cleared while the engine is busy it hangs the card
   (DOS-GL: only after the soft resets). A program on the card sets all of these again when it starts. */
void radeon_quiet_regs(volatile u32 *r)
{
    R(CP_CSQ_CNTL) = 0;
    R(SCRATCH_UMSK) = 0;
    R(CP_RB_CNTL) = R(CP_RB_CNTL) | RB_NO_UPDATE;
}
