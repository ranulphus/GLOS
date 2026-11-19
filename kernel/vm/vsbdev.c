/* GLOS's virtual Sound Blaster in the system VM (vsbdev.h; docs/v1/vsb.md).
 *
 * The card's sixteen ports (BLASTER's A) and the two DMA controllers' are trapped. The DSP's and the mixer
 * chip's go to vsb.c; the card's FM ports (the first four, and two more) go to the real FM chip at 388h, which
 * is the session's as before; the DMA controllers' go to vdma.c, which keeps the virtual channels' part and
 * says what is the real chip's.
 *
 * What a program plays is asked for by GLOS's mixer once a period, in the sound card's interrupt
 * (vsbdev_mix): the DSP takes the program's bytes from the VM's memory by its DMA channel and gives 16-bit
 * stereo at the mixer's rate. A block's end raises the card's interrupt in the virtual interrupt controller.
 * The ports are a program's, on the VM's thread; the period is an interrupt's: the ports' work is done with
 * interrupts off.
 *
 * The numbers are BLASTER's where DOS's environment has one (a real Sound Blaster 16 under GLOS's mixer: the
 * program finds the virtual card where it would have found the real one), else A220 I5 D1 H5. */
#include <string.h>

#include "glos/bootinfo.h"
#include "arch.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mixer.h"
#include "session.h"
#include "vdma.h"
#include "vm.h"
#include "vsb.h"
#include "vsbdev.h"

#define PERIOD_MAX 512                          /* frames of a mixer's period, at the most */

static struct {
    u8 on;
    u16 base;
    u8 irq, dma8, dma16;
    struct vsb sb;
    struct vdma dma;
    char blaster[32];
    u32 irqs, starts, said, peak, frames;       /* since the session before ended (the log's): interrupts,
                                                   blocks asked for, sounds begun and said in the log, ... */
} v;

/* The program's bytes under a DMA channel (interrupt time). A buffer above the VM's first megabyte is not
   memory GLOS can say is the program's: silence. */
static u32 dma_bytes(void *ctx, int ch, u8 *buf, u32 bytes)
{
    u32 got = 0, phys = 0, n;
    (void)ctx;
    while (got < bytes && (n = vdma_take(&v.dma, ch, bytes - got, &phys)) != 0) {
        if (phys + n <= 0x110000u)
            memcpy(buf + got, vm_ptr(phys), n);
        else
            memset(buf + got, 0, n);
        got += n;
    }
    return got;
}

static void irq_raise(void *ctx)
{
    (void)ctx;
    v.irqs++;
    vpic_raise(&vm.pic, v.irq);
    vm_kick();
}

static const struct vsb_host host = { 0, dma_bytes, irq_raise };

static void trap(u16 from, u16 to, int on)
{
    u32 p;
    for (p = from; p <= to; p++)
        cpu_io_trap(p, on);
}

void vsbdev_init(const struct bootinfo *bi)
{
    static const u16 pages[] = { 0x81, 0x82, 0x83, 0x87, 0x89, 0x8A, 0x8B, 0x8F };
    char val[16];
    u32 i;
    if (v.on || !mixer_found())
        return;                                 /* (no card under GLOS's mixer: programs have the PC's own, as ever) */
    if (kcfg_loaded() && kcfg_get("sound", "virtual", val, sizeof val) == 1 && !strcmp(val, "off"))
        return;
    v.base = (u16)(bi->sb_port >= 0x200 && bi->sb_port <= 0x2F0 ? bi->sb_port : 0x220);
    v.irq = (u8)(bi->sb_port && bi->sb_irq >= 2 && bi->sb_irq <= 15 ? bi->sb_irq : 5);
    v.dma8 = (u8)(bi->sb_port && bi->sb_dma8 <= 3 && bi->sb_dma8 != 2 ? bi->sb_dma8 : 1);
    v.dma16 = (u8)(bi->sb_port && bi->sb_dma16 >= 5 && bi->sb_dma16 <= 7 ? bi->sb_dma16 : bi->sb_port ? v.dma8 : 5);
    if (v.dma16 >= 5)
        ksnprintf(v.blaster, sizeof v.blaster, "A%x I%u D%u H%u T6", v.base, v.irq, v.dma8, v.dma16);
    else
        ksnprintf(v.blaster, sizeof v.blaster, "A%x I%u D%u T6", v.base, v.irq, v.dma8);
    /* (No real channel is kept masked for a virtual one: GLOS's own driver may be playing through it.) */
    vdma_reset(&v.dma, (u8)(1u << v.dma8 | (v.dma16 >= 5 ? 1u << v.dma16 : 0)), 0);
    vsb_init(&v.sb, &host, v.irq, v.dma8, v.dma16);
    trap(v.base, (u16)(v.base + 15), 1);
    trap(0x00, 0x0F, 1);
    trap(0xC0, 0xDF, 1);
    for (i = 0; i < sizeof pages / sizeof pages[0]; i++)
        cpu_io_trap(pages[i], 1);
    v.on = 1;
    kprintf("GLOS-VSB on blaster=\"%s\"\n", v.blaster);
    mixer_refresh();                            /* (the card is the mixer's all the time from here) */
}

/* The card programs have now is the virtual one: it is on, and no session in direct mode has the screen. Such a
   session's protected-mode code reaches the PC's ports untrapped: the real card is that session's, as it was
   before there was a virtual one (the mixer hands it over: mixer.c), its real-mode code's ports go to the real
   card too, and BLASTER is the real card's. */
int vsbdev_on(void) { return v.on && !session_direct(); }

const char *vsbdev_blaster(void) { return vsbdev_on() ? v.blaster : 0; }

int vsbdev_port(u16 port)
{
    return vsbdev_on() && ((port >= v.base && port < v.base + 16) || vdma_port(port));
}

/* The card's FM ports: the real chip's, at 388h (ports 0 to 3 are its two halves, 8 and 9 the first again). */
static int fm_port(unsigned reg) { return reg <= 3 ? (int)(0x388 + reg) : reg == 8 || reg == 9 ? (int)(0x388 + reg - 8) : -1; }

u8 vsbdev_in(u16 port)
{
    u32 f = irq_save();
    u8 val = 0xFF, mask = 0;
    if (port >= v.base && port < v.base + 16) {
        unsigned reg = port - v.base;
        if (current != vm.thread)               /* an app's thread: the card is a session's program's. An app plays
                                                   through the mixer; here it finds no card (its interrupt would
                                                   never reach it: Echoes as an app on SDL's Sound Blaster driver,
                                                   2026-11-19) */
            val = 0xFF;
        else
            val = fm_port(reg) >= 0 ? inb((u16)fm_port(reg)) : vsb_in(&v.sb, reg);
    } else {
        int r = vdma_in(&v.dma, port, &val, &mask);
        if (r == 0)
            val = inb(port);
        else if (r == 2)
            val = (u8)((inb(port) & ~mask) | val);
    }
    irq_restore(f);
    return val;
}

void vsbdev_out(u16 port, u8 val)
{
    u32 f = irq_save();
    int began = 0;
    if (port >= v.base && port < v.base + 16) {
        unsigned reg = port - v.base;
        if (current != vm.thread) {             /* (an app's: no card there) */
        } else if (fm_port(reg) >= 0) {
            outb((u16)fm_port(reg), val);
        } else {
            u32 was = v.sb.begun, fresh = v.sb.fresh;
            vsb_out(&v.sb, reg, val);
            v.starts += v.sb.begun - was;
            began = v.sb.fresh != fresh;        /* (not a block that goes on from the one before it) */
        }
    } else {
        struct vdma_real real;
        int i;
        vdma_out(&v.dma, port, val, &real);
        for (i = 0; i < real.n; i++)
            outb(real.port[i], real.val[i]);
    }
    irq_restore(f);
    if (began && v.said++ < 8)                  /* (the first few sounds a session begins: what it plays, how) */
        kprintf("GLOS-VSB play rate=%u bits=%u channels=%u block=%u auto=%u\n", v.sb.rate, v.sb.wide ? 16 : 8,
                v.sb.stereo ? 2 : 1, v.sb.block, v.sb.auto_init);
}

void vsbdev_mix(s32 *acc, u32 frames, u32 rate)
{
    static s32 one[PERIOD_MAX * 2];
    u32 i, n = frames > PERIOD_MAX ? PERIOD_MAX : frames;
    if (!v.on || !vsb_playing(&v.sb))
        return;
    memset(one, 0, n * 2 * sizeof one[0]);
    i = v.sb.played;
    if (!vsb_mix(&v.sb, one, n, rate))
        return;
    v.frames += v.sb.played - i;
    for (i = 0; i < n * 2; i++) {
        s32 x = one[i];
        acc[i] += x;
        if (x < 0)
            x = -x;
        if ((u32)x > v.peak)
            v.peak = (u32)x;
    }
}

void vsbdev_leave(void) { v.on = 0; }

void vsbdev_session_end(void)
{
    u32 f;
    if (!v.on)
        return;
    if (v.starts || v.irqs)
        kprintf("GLOS-VSB end starts=%u irqs=%u played=%u peak=%u\n", v.starts, v.irqs, v.frames, v.peak);
    f = irq_save();
    vsb_init(&v.sb, &host, v.irq, v.dma8, v.dma16);
    vdma_reset(&v.dma, (u8)(1u << v.dma8 | (v.dma16 >= 5 ? 1u << v.dma16 : 0)), 0);     /* (its channels masked) */
    v.starts = v.said = v.irqs = v.peak = v.frames = 0;
    irq_restore(f);
}
