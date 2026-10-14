/* The Matrox card's drawing engine (PRD §9.1; docs/milestones-m7.md step
 * C): MGA-Glide's HAL (third_party/mgahal, its FPU-free part, with the
 * port hal/port/glos.c), brought up on the desktop's mode.
 *
 * The mode is still set through the video BIOS, as on any card
 * (display.c). When the framebuffer it gives is a G100, G200, G400 or
 * G450's, the card's registers and all of its memory are mapped into the
 * kernel (mm_dev_map), its memory is sized once (the G200's BIOS tells VBE
 * 2 MB whatever it has), and the engine is set up on the screen as its
 * target, the screen's bytes kept in the VRAM manager. Before a session
 * takes the screen, and when the desktop ends, the engine is drained and
 * let go of: from then on the card is the session's. GLOS.CFG's [display]
 * accel = off keeps it out of use. */
#include <string.h>

#include "glos/bootinfo.h"
#include "kprintf.h"
#include "mgadisp.h"

#include "mga/hal.h"
#include "mga/mmio.h"

#define OWN_SCREEN 1u                           /* (VRAM owners) */

static struct {
    u8 off;                                     /* [display] accel = off */
    u8 found, mapped, up;
    u32 vram;                                   /* bytes, sized at the first desktop */
    mga_vram heap;
    mga_target screen;
} m;

void mga_display_init(const struct bootinfo *bi)
{
    m.off = (bi->display_flags & BI_DISPLAY_NOACCEL) != 0;
    if (m.off || mga_find(&mga) != 0 || mga.family == MGA_FAMILY_G200E)
        return;
    m.found = 1;
    mga.fb_size = mga.family == MGA_FAMILY_G400 ? 32u << 20 : 16u << 20;   /* (the apertures: the G400's and
                                                   G450's are 32 MB, the others' 16; the HAL would assume 16) */
    kprintf("GLOS-MGA found %s id=%04x rev=%02x at %u:%u.%u fb=%08x mmio=%08x\n", mga.name, mga.device_id,
            mga.revision, mga.bus, mga.dev, mga.fn, mga.fb_phys, mga.mmio_phys);
}

int mga_display_up(u32 lfb, u32 pitch, u32 bpp, u32 h)
{
    u32 bytes = pitch * h;
    m.up = 0;
    if (!m.found)
        return 0;
    if (lfb < mga.fb_phys || lfb - mga.fb_phys >= mga.fb_size) {
        kprintf("GLOS-MGA other lfb=%08x\n", lfb);  /* (not the card's framebuffer: VBE alone) */
        return 0;
    }
    if (!m.mapped) {
        if (mga_map(&mga) != 0) {
            kprintf("GLOS-MGA error=map\n");
            m.found = 0;
            return 0;
        }
        m.mapped = 1;
    }
    if (!m.vram)                                /* (before anything is drawn: it writes a word a megabyte) */
        mga.vram_bytes = m.vram = mga_probe_vram();
    memset(&m.screen, 0, sizeof m.screen);
    m.screen.color_off = lfb - mga.fb_phys;
    m.screen.pitch_px = (int)(pitch / (bpp / 8));
    m.screen.bpp = (int)bpp;
    m.screen.zbits = 16;
    engine_init(m.screen.pitch_px, m.screen.bpp);
    engine_set_target(&m.screen);
    engine_set_clip(0, 0, m.screen.pitch_px, (int)h);
    if (engine_sync(200000) != 0) {
        kprintf("GLOS-MGA error=engine resets=%u\n", engine_resets);
        return 0;
    }
    vram_init(&m.heap, 0, m.vram);
    vram_reserve(&m.heap, m.screen.color_off, bytes, OWN_SCREEN);
    m.up = 1;
    kprintf("GLOS-MGA up %s vram=%u screen=%x+%u pitch=%u bpp=%u\n", mga.name, m.vram, m.screen.color_off, bytes,
            m.screen.pitch_px, bpp);
    return 1;
}

void mga_display_down(void)
{
    if (!m.up)
        return;
    engine_sync(200000);
    m.up = 0;
    kprintf("GLOS-MGA down\n");
}

int mga_display_on(void) { return m.up; }

u32 mga_display_stat(char *buf, u32 max)
{
    u32 largest, free_;
    if (m.off)
        return (u32)ksnprintf(buf, max, "accel off (GLOS.CFG)\n");
    if (!m.found)
        return (u32)ksnprintf(buf, max, "accel none (no Matrox card)\n");
    if (!m.up)
        return (u32)ksnprintf(buf, max, "accel idle %s\n", mga.name);
    free_ = vram_free_bytes(&m.heap, &largest);
    return (u32)ksnprintf(buf, max, "accel on %s vram=%uK free=%uK largest=%uK\n", mga.name, m.vram >> 10,
                          free_ >> 10, largest >> 10);
}
