/* A screen's timings for a refresh rate (kernel/gfx/timing.c): VESA's Generalized Timing Formula, the monitor's
 * limits from its EDID, and the check of one against the other. Integers only (the kernel has no FPU state). */
#ifndef K_TIMING_H
#define K_TIMING_H
#include "types.h"

/* As VBE 3.0's CRTCInfoBlock has them (4F02h with bit 11 of BX): pixels and lines, the pixel clock in Hz, the
   refresh in hundredths of a Hz. Flags: TIMING_HNEG, TIMING_VNEG (the syncs' polarities). */
struct timing {
    u16 htotal, hss, hse, vtotal, vss, vse;
    u8 flags;
    u32 pclk;
    u16 refresh;
};
#define TIMING_HNEG 0x04
#define TIMING_VNEG 0x08

/* What the monitor says it takes (EDID 1.x's range limits descriptor, FDh), and whether it is digital. */
struct edid_limits {
    u8 valid;                           /* a good EDID block was read */
    u8 range;                           /* ... with its range limits */
    u8 digital;                         /* its input is digital (a flat panel on DVI, say) */
    u8 vmin, vmax;                      /* Hz */
    u8 hmin, hmax;                      /* kHz */
    u16 pclk_max;                       /* MHz (0: not said) */
    char name[14];                      /* its name (FCh), or "" */
};

int timing_gtf(u32 w, u32 h, u32 hz, struct timing *t);    /* 0, or -1 (a size or rate out of reach) */
void timing_clock(struct timing *t, u32 pclk);             /* the clock the card can make: the refresh again */
int edid_parse(const u8 *e, u32 n, struct edid_limits *l); /* 0, or -1 (no EDID: l->valid 0) */
const char *timing_check(const struct timing *t, const struct edid_limits *l);     /* 0 if it may be set, or why not */
void timing_block(const struct timing *t, u8 out[64]);     /* VBE's CRTCInfoBlock */

#endif
