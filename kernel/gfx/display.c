/* The display (PRD §9.1; docs/milestones-m6.md step C): the screen as a
 * surface for the 2D library.
 *
 * One screen is driven, through the video BIOS's VBE: a mode with a linear
 * framebuffer, at the size and depth GLOS.CFG's [display] asks for (mode =
 * 640x480x8) or `glos display on WxHxBPP` does. The BIOS calls are nested
 * real-mode calls on the VM thread, made when the stub asks for its next
 * job (nothing else is in the BIOS then) or from a session's own start and
 * end. The mode is found by its description, not its number: the BIOS's
 * mode list (4F00h) if there is room to ask for it, VESA's numbers 100h to
 * 1FFh otherwise, each described by 4F01h. 16 bits means 5-6-5, or 5-5-5
 * where that is all the card has. The framebuffer is mapped into the
 * kernel at FB_BASE.
 *
 * At 8 bits the DAC is loaded with the GLOS palette (gfx/pal.c), 8 bits a
 * colour if the BIOS can switch the DAC to that (4F08h), 6 otherwise.
 *
 * A session has the screen to itself (PRD §6.9): text mode 3 is set before
 * it starts, and the desktop's mode again when it has ended, when the
 * painter draws everything again (the window manager's composer, from the
 * windows' own pixels; until there is one, whatever was shown last). A
 * program marked as a GLOS app (vm/session.c) starts with the desktop on.
 *
 * While the desktop has the screen nothing else may draw on it. A program's
 * video BIOS calls are answered here instead (display_quiet(): an app's
 * console output goes to its job, not to the BIOS's teletype, which on a
 * Matrox card writes into a VBE mode's framebuffer), and the VGA's own
 * memory at A0000h-BFFFFh is out of reach (mm_vga_hide()). A mode set by a
 * program that is still starting, marked as an app, says it is a session
 * after all: it gets the screen.
 *
 * glos shot sees the desktop as any VBE mode (the mode sets go through
 * rm_call(), which tells it). */
#include <string.h>

#include "glos/bootinfo.h"
#include "display.h"
#include "gpu.h"
#include "dpmi.h"
#include "arch.h"
#include "session.h"
#include "agent.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mgadisp.h"
#include "mixer.h"
#include "mm.h"
#include "pci.h"
#include "radeon.h"
#include "timing.h"
#include "sched.h"
#include "timer.h"
#include "vm.h"

void gfx_testcard(struct gfx_surface *s);

static struct {
    u8 on;                              /* the screen is in the desktop's mode */
    u8 held;                            /* ... but for a session, which has it in text mode */
    u8 ask;                             /* 1 + DISPLAY_: what the VM thread is to do */
    u8 turning;                         /* ... and it is doing it now: w, h, bpp and mode are that change's */
    u8 next;                            /* 1 + DISPLAY_: asked for meanwhile, taken up at that one's end ... */
    u16 next_w, next_h, next_bpp;       /* ... with its mode */
    u8 dac;                             /* bits a colour in the DAC, at 8 bits a pixel */
    u16 w, h, bpp;                      /* the mode wanted */
    u16 mode;                           /* its VBE number */
    u32 phys;
    struct gfx_surface fb;
    void (*paint)(struct gfx_surface *fb);      /* who draws the whole screen now ... */
    void (*home)(struct gfx_surface *fb);       /* ... and when it isn't the test card's */
    void (*evict)(void);                        /* ... and whose pixels are to leave the card's memory first */
    u8 trap;                                    /* a console job runs in a window: the VGA out of reach (M9) */
    u8 list_ask, listed;                        /* the modes: to be listed by the VM thread; listed */
    u8 power_ask;                               /* 1 + the DPMS state the VM thread is to set */
    u8 power, power_can, power_known;           /* the screen's power now (DISPLAY_POWER_); what the card can; asked */
    u32 nmodes;
    struct display_mode modes[DISPLAY_MODES];
    struct waitq asked;                         /* whoever waits for the list or the power */
    /* A mode of its own for a full-screen window of another size than the desktop's (display_app_mode): */
    u16 app_w, app_h;                           /* the size the card is in now; 0: the desktop's own mode */
    u16 want_w, want_h;                         /* ... and the one asked for */
    u16 bad_w, bad_h;                           /* a size the card had no mode for, or would not set */
    u8 app_ask;                                 /* the VM thread is to make it so */
    void (*app_done)(void);                     /* ... and tell this when it has */
    /* The refresh rate ([display] refresh, G2): */
    u16 vbe_ver;                                /* the BIOS's VBE (0300h: 3.0, which takes CRTC timings) */
    u8 edid_asked;                              /* the monitor's EDID read (once) ... */
    struct edid_limits edid;                    /* ... and what it said */
    u16 hz;                                     /* the refresh the card was set to, in hundredths of a Hz (0: the
                                                   BIOS's own) */
} d;

/* The VGA's memory as the desktop leaves it: hidden behind a spare frame, or not there (a console job's). */
static int hidden(void) { return d.on && !d.held ? (d.trap ? 2 : 1) : 0; }

static u32 le16(const u8 *p) { return p[0] | (u32)p[1] << 8; }
static u32 le32(const u8 *p) { return le16(p) | le16(p + 2) << 16; }

/* INT 10h on the stub's spare stack, with the VGA's memory in reach for it:
   1 if the BIOS answers 004Fh. */
static int vbe(struct rmregs *r)
{
    int ok;
    r->ss = vm.loader_cs;
    r->sp = (u16)vm.bi->kill_sp;
    r->flags = 2;
    mm_vga_hide(0);
    ok = rm_call(0, r, RM_INT, 0x10, 0, 0) == 0 && (r->eax & 0xFFFF) == 0x004F;
    mm_vga_hide(hidden());
    return ok;
}

/* 4F01h's first 64 bytes for a mode, through the stub's EXEC buffers (256
   bytes that nothing reads meanwhile: lent and put back, as glos shot does). */
static int describe(u32 mode, u8 *info)
{
    u32 off = __builtin_offsetof(struct stub_data, path), buf = vm_lin(vm.loader_cs, off);
    struct rmregs r;
    u8 keep[256];
    int ok;
    memcpy(keep, vm_ptr(buf), 256);
    memset(vm_ptr(buf), 0, 256);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F01;
    r.ecx = mode;
    r.es = vm.loader_cs;
    r.edi = off;
    ok = vbe(&r);
    if (ok)
        memcpy(info, vm_ptr(buf), 64);
    memcpy(vm_ptr(buf), keep, 256);
    return ok;
}

/* VBE 3.0's calls (4F0Bh, 4F02h with CRTC timings, 4F15h) get a block of DOS's memory for the call: their stack
   (the stub's spare 256 bytes, which the older calls fit in, are too few for these: 86Box's G400 BIOS ran past
   them into the stub's code below, and GLOS's way out printed half a line and hung) and their buffers (the CRTC
   block at 0, the EDID at 100h); given back after it. BIG_PARAS 4 KB, the stack's top at its end. */
#define BIG_PARAS 0x100

static u32 big_get(void)                        /* its segment, or 0 */
{
    struct rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x4800;
    r.ebx = BIG_PARAS;
    r.ss = vm.loader_cs;
    r.sp = (u16)vm.bi->kill_sp;
    r.flags = 2;
    if (rm_call(0, &r, RM_INT, 0x21, 0, 0) != 0 || (r.flags & FL_CF))
        return 0;
    return r.eax & 0xFFFF;
}

static void big_put(u32 seg)
{
    struct rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x4900;
    r.es = (u16)seg;
    r.ss = vm.loader_cs;
    r.sp = (u16)vm.bi->kill_sp;
    r.flags = 2;
    rm_call(0, &r, RM_INT, 0x21, 0, 0);
}

static int vbe_big(struct rmregs *r, u32 seg)   /* INT 10h on the block's stack: 1 if the BIOS answers 004Fh */
{
    int ok;
    r->ss = (u16)seg;
    r->sp = BIG_PARAS * 16 - 2;
    r->flags = 2;
    mm_vga_hide(0);
    ok = rm_call(0, r, RM_INT, 0x10, 0, 0) == 0 && (r->eax & 0xFFFF) == 0x004F;
    mm_vga_hide(hidden());
    return ok;
}

/* The monitor's EDID (VBE/DDC, 4F15h BL=01h: block 0) into the block at 100h (1 KB filled with A5h first: how far
   the BIOS wrote is logged if past the 128 bytes). Asked once (a monitor plugged in later is seen at GLOS's next
   start). */
static void edid_read(u32 seg)
{
    struct rmregs r;
    u8 e[128], *b = vm_ptr((seg << 4) + 0x100);
    u32 i, wrote = 0;
    int ok;
    if (d.edid_asked)
        return;
    d.edid_asked = 1;
    memset(b, 0xA5, 1024);
    memset(&r, 0, sizeof r);
    r.eax = 0x4F15;
    r.ebx = 0x01;
    r.es = (u16)seg;
    r.edi = 0x100;
    vm.bios_slow = 1;                           /* (the BIOS's I2C keeps interrupts off: 86Box's G400, over 50 ms) */
    ok = vbe_big(&r, seg);
    vm.bios_slow = 0;
    if (ok)
        memcpy(e, b, 128);
    for (i = 1024; i > 128; i--)
        if (b[i - 1] != 0xA5) {
            wrote = i;
            break;
        }
    if (wrote)
        kprintf("GLOS-DISPLAY edid bios-wrote=%u\n", wrote);
    if (!ok || edid_parse(e, 128, &d.edid) != 0) {
        kprintf("GLOS-DISPLAY edid none%s\n", ok ? " (not a good block)" : "");
        return;
    }
    if (d.edid.range)
        kprintf("GLOS-DISPLAY edid name=\"%s\" %s v=%u-%u h=%u-%u pclk=%u\n", d.edid.name,
                d.edid.digital ? "digital" : "analogue", d.edid.vmin, d.edid.vmax, d.edid.hmin, d.edid.hmax,
                d.edid.pclk_max);
    else
        kprintf("GLOS-DISPLAY edid name=\"%s\" %s no-range\n", d.edid.name, d.edid.digital ? "digital" : "analogue");
}

/* [display] refresh: the rate asked for in Hz, 0 for the BIOS's own (auto). */
static u32 refresh_wanted(void)
{
    char v[8];
    u32 hz = 0, i;
    if (kcfg_get("display", "refresh", v, sizeof v) < 0)
        return 0;
    for (i = 0; v[i] >= '0' && v[i] <= '9'; i++)
        hz = hz * 10 + (u32)(v[i] - '0');
    return v[i] ? 0 : hz;
}

/* 4F02h (r: its EAX and EBX as the caller has them), at the refresh asked for where VBE 3.0 and the monitor allow:
   the GTF timings for w x h at that rate, the clock made the card's nearest (4F0Bh), checked against the EDID's
   limits, given as a CRTCInfoBlock (BX bit 11, ES:DI: the block's start), all on the block's stack. Else, or if
   the BIOS refuses them, the BIOS's own timings, said why. 1 if the BIOS answers 004Fh. */
static int vbe_set(struct rmregs *r, u32 w, u32 h)
{
    u32 hz = refresh_wanted(), mode = r->ebx & 0x1FF, bx = r->ebx, seg = 0;
    const char *why = 0;
    struct timing t;
    struct rmregs c;
    int ok;
    d.hz = 0;
    if (!hz)
        return vbe(r);
    if (d.vbe_ver < 0x300)
        why = "the card's BIOS is not VBE 3.0";
    else if (timing_gtf(w, h, hz, &t) != 0)
        why = "no timing for that size and rate";
    else if (!(seg = big_get()))
        why = "no DOS memory for the BIOS's stack";
    else {
        edid_read(seg);
        memset(&c, 0, sizeof c);
        c.eax = 0x4F0B;
        c.ecx = t.pclk;
        c.edx = mode;
        if (vbe_big(&c, seg) && c.ecx >= t.pclk / 2 && c.ecx <= t.pclk * 2)
            timing_clock(&t, c.ecx);            /* (the clock the card can make) */
        why = timing_check(&t, &d.edid);
    }
    if (why) {
        if (seg)
            big_put(seg);
        kprintf("GLOS-DISPLAY refresh=bios want=%u reason=\"%s\"\n", hz, why);
        return vbe(r);
    }
    timing_block(&t, vm_ptr(seg << 4));
    r->ebx = bx | 0x0800;
    r->es = (u16)seg;
    r->edi = 0;
    ok = vbe_big(r, seg);
    big_put(seg);
    if (ok) {
        d.hz = t.refresh;
        kprintf("GLOS-DISPLAY refresh=%u.%02u want=%u total=%ux%u pclk=%u h=%ukHz\n", t.refresh / 100u,
                t.refresh % 100u, hz, t.htotal, t.vtotal, t.pclk, (t.pclk / t.htotal + 500) / 1000);
        return 1;
    }
    kprintf("GLOS-DISPLAY refresh=bios want=%u reason=\"the BIOS refused the timings\" ax=%04x\n", hz, r->eax & 0xFFFF);
    memset(r, 0, sizeof *r);
    r->eax = 0x4F02;
    r->ebx = bx;
    return vbe(r);
}

/* n / d by the CPU's own divide, for a quotient that fits in 32 bits (0 if it wouldn't). */
static u32 div64_32(u64 n, u32 d)
{
    u32 q, r;
    if (!d || (u32)(n >> 32) >= d)
        return 0;
    __asm__("divl %4" : "=a"(q), "=d"(r) : "a"((u32)n), "d"((u32)(n >> 32)), "rm"(d));
    return q;
}

/* The start of the next vertical retrace (the VGA's input status, 3DAh bit 3, set), waited for at most `limit`
   of the TSC's cycles: its TSC, or 0. */
static u64 retrace(u64 limit)
{
    u64 t0 = rdtsc(), t;
    u32 was = inb(0x3DA) & 8, now;
    while ((t = rdtsc()) - t0 < limit) {
        now = inb(0x3DA) & 8;
        if (now && !was)
            return t;
        was = now;
    }
    return 0;
}

/* The refresh the card makes, measured (glos display hz): the TSC's rate against the PIT (channel 2's count of
   FFFFh, 54.925 ms, with interrupts off: the kernel's tick count was no good for it, its interrupts held up
   around a mode change and the rate read 86 Hz for 70), then five frames, each from one retrace's start to the
   next's with interrupts off (a retrace's pulse is a few lines long: a thread switched out meanwhile would miss
   it), the middle one taken. Hundredths of a Hz, or 0 (no TSC: a 486; no retrace seen). Interrupts are off for
   up to 60 ms at a time. (A read of 3DAh also resets the attribute controller's flip-flop: a diagnostic, not for
   while a session programs the VGA.) */
u32 display_measure_hz(void)
{
    u32 f, i, k, c, p[5];
    u64 a, b;
    if (!(vm.bi->cpuid_edx & (1u << 4)))
        return 0;
    f = irq_save();
    a = rdtsc();
    pit2_wait();
    c = (u32)(rdtsc() - a);                     /* (the TSC's cycles in 65535 / 1193182 s) */
    irq_restore(f);
    for (i = 0; i < 5; i++) {
        f = irq_save();
        a = retrace(c);                         /* (55 ms: a frame at 18 Hz) */
        b = a ? retrace(c) : 0;
        irq_restore(f);
        if (!a || !b)
            return 0;
        p[i] = (u32)(b - a);
        for (k = i; k && p[k - 1] > p[k]; k--) {
            u32 t = p[k];
            p[k] = p[k - 1], p[k - 1] = t;
        }
    }
    /* 100 x 1193182 / 65535 = 1820.6943: in ten-thousandths, then the hundredths of a Hz */
    return div64_32((u64)c * 18206943u, p[2]) / 10000u;
}

/* [display] refresh changed (config.c): the desktop's mode set again at the new rate, if the desktop has the
   screen (a session's: at its end). */
void display_refresh_changed(void)
{
    if (d.on && !d.held)
        display_ask(DISPLAY_ON, 0, 0, 0);
}

/* How well a mode's description fits what is wanted: 0 not at all, 2 as
   asked, 1 the 5-5-5 mode for 16 bits. */
static u16 fit_w, fit_h;                        /* (0: the desktop's size) */

static int fits(const u8 *m)
{
    u32 attr = le16(m), bpp = m[0x19], model = m[0x1B];
    if ((attr & 0x91) != 0x91 || le16(m + 0x12) != (fit_w ? fit_w : d.w) || le16(m + 0x14) != (fit_h ? fit_h : d.h)
        || m[0x18] != 1 || !le32(m + 0x28))
        return 0;                               /* (supported, graphics, with a linear framebuffer; one plane) */
    if (d.bpp == 8)
        return bpp == 8 && model == 4 ? 2 : 0;
    if (model != 6 && model != 4)               /* direct colour (some BIOSes say packed for it) */
        return 0;
    if (d.bpp == 16)
        return bpp == 16 && m[0x21] == 6 ? 2 : (bpp == 15 || (bpp == 16 && m[0x21] == 5)) ? 1 : 0;
    return bpp == d.bpp ? 2 : 0;
}

/* The BIOS's list of its modes' numbers (4F00h wants 512 bytes, DOS's for a moment): how many, 0 if it gives
   none (the VESA numbers 100h to 1FFh are then tried). */
static u32 bios_modes(u16 *list, u32 max)
{
    struct rmregs r;
    u32 n = 0, seg;
    memset(&r, 0, sizeof r);
    r.eax = 0x4800;
    r.ebx = 0x20;
    r.ss = vm.loader_cs;
    r.sp = (u16)vm.bi->kill_sp;
    r.flags = 2;
    if (rm_call(0, &r, RM_INT, 0x21, 0, 0) == 0 && !(r.flags & FL_CF)) {
        u32 lin;
        seg = r.eax & 0xFFFF;
        lin = seg << 4;
        memset(vm_ptr(lin), 0, 512);
        memcpy(vm_ptr(lin), "VBE2", 4);
        memset(&r, 0, sizeof r);
        r.eax = 0x4F00;
        r.es = (u16)seg;
        if (vbe(&r)) {
            u32 p = vm_lin(le16(vm_ptr(lin + 0x10)), le16(vm_ptr(lin + 0x0E)));
            d.vbe_ver = (u16)le16(vm_ptr(lin + 4));
            for (; n < max && p < 0x10FFFE && vm_rd16(p) != 0xFFFF; p += 2)
                list[n++] = vm_rd16(p);
        }
        memset(&r, 0, sizeof r);
        r.eax = 0x4900;
        r.es = (u16)seg;
        r.ss = vm.loader_cs;
        r.sp = (u16)vm.bi->kill_sp;
        r.flags = 2;
        rm_call(0, &r, RM_INT, 0x21, 0, 0);
    }
    return n;
}

/* The mode for d.w x d.h x d.bpp, its description in info: its number, or 0. */
static u32 find(u8 *info)
{
    u16 list[128];
    u8 m[64];
    u32 n = bios_modes(list, ARRAY_SIZE(list)), i, best = 0;
    int score = 0, s;
    for (i = 0; i < (n ? n : 0x100); i++) {
        u32 mode = n ? list[i] : 0x100 + i;
        if (describe(mode, m) && (s = fits(m)) > score) {
            score = s;
            best = mode;
            memcpy(info, m, sizeof m);
        }
    }
    return best;
}

/* Every mode the desktop could have (the VM thread, once): graphics with a linear framebuffer, 8, 16 or 32
   bits (5-5-5 counts as 16, as fits() takes it), within what [display] mode allows; by size, then depth. */
static void list_modes(void)
{
    u16 list[128];
    u8 m[64];
    u32 n = bios_modes(list, ARRAY_SIZE(list)), i, k;
    d.nmodes = 0;
    for (i = 0; i < (n ? n : 0x100); i++) {
        struct display_mode v, t;
        u32 attr, bpp, model;
        if (!describe(n ? list[i] : 0x100 + i, m))
            continue;
        attr = le16(m), bpp = m[0x19], model = m[0x1B];
        if ((attr & 0x91) != 0x91 || m[0x18] != 1 || !le32(m + 0x28))
            continue;
        if (!(bpp == 8 && model == 4) && !((model == 6 || model == 4) && (bpp == 15 || bpp == 16 || bpp == 32)))
            continue;
        v.w = (u16)le16(m + 0x12), v.h = (u16)le16(m + 0x14), v.bits = bpp == 15 ? 16 : (u16)bpp;
        if (v.w < 320 || v.w > 1600 || v.h < 200 || v.h > 1200)
            continue;
        for (k = 0; k < d.nmodes && (d.modes[k].w != v.w || d.modes[k].h != v.h || d.modes[k].bits != v.bits); k++) ;
        if (k < d.nmodes || d.nmodes == DISPLAY_MODES)
            continue;                           /* (16 bits twice: 5-5-5 and 5-6-5) */
        for (k = d.nmodes++; k && (d.modes[k - 1].w > v.w || (d.modes[k - 1].w == v.w && (d.modes[k - 1].h > v.h
             || (d.modes[k - 1].h == v.h && d.modes[k - 1].bits > v.bits)))); k--) {
            t = d.modes[k - 1];
            d.modes[k] = t;
        }
        d.modes[k] = v;
    }
    d.listed = 1;
    kprintf("GLOS-DISPLAY modes=%u\n", d.nmodes);
}

/* The screen's power (VBE/PM, 4F10h; the VM thread): what the card can do, asked once; then the state. */
static void power_set(int state)
{
    static const u8 dpms[4] = { 0x00, 0x01, 0x02, 0x04 };       /* on, stand-by, suspend, off */
    struct rmregs r;
    if (!d.power_known) {
        d.power_known = 1;
        memset(&r, 0, sizeof r);
        r.eax = 0x4F10;
        if (vbe(&r)) {
            u32 bh = (r.ebx >> 8) & 0xFF;
            d.power_can = (u8)(1u << DISPLAY_POWER_ON | (bh & 1 ? 1u << DISPLAY_POWER_STANDBY : 0)
                               | (bh & 2 ? 1u << DISPLAY_POWER_SUSPEND : 0) | (bh & 4 ? 1u << DISPLAY_POWER_OFF : 0));
        }
        kprintf("GLOS-DISPLAY power can=%x\n", d.power_can);
    }
    if (state < 0 || !(d.power_can >> state & 1) || state == d.power)
        return;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F10;
    r.ebx = 0x01 | (u32)dpms[state] << 8;
    if (vbe(&r)) {
        d.power = (u8)state;
        kprintf("GLOS-DISPLAY power=%u\n", (u32)state);
    }
}

static int radeon_settle(int surfaces);

static void text_mode(void)
{
    struct rmregs r;
    d.power = DISPLAY_POWER_ON;                 /* (a mode set turns the screen on) */
    if (radeon_settle(0) == RADEON_DEAD)
        return;                                 /* (the BIOS's writes would hold the bus: the screen as it is) */
    memset(&r, 0, sizeof r);
    r.eax = 0x0003;
    vbe(&r);
}

/* The DAC for an 8-bit mode: the GLOS palette. */
static void palette(void)
{
    struct rmregs r;
    u32 i, k;
    memset(&r, 0, sizeof r);
    r.eax = 0x4F08;
    r.ebx = 0x0800;                             /* set: 8 bits a colour, if it can */
    d.dac = vbe(&r) && ((r.ebx >> 8) & 0xFF) >= 8 ? 8 : 6;
    outb(0x3C6, 0xFF);
    outb(0x3C8, 0);
    for (i = 0; i < 256; i++)
        for (k = 0; k < 3; k++)
            outb(0x3C9, (u8)(gfx_palette[i][k] >> (8 - d.dac)));
}

/* An ATI Radeon of the R100 family (the HAL's: hal/src/radeon/chip.c), settled before every mode set the BIOS
   makes for GLOS (gfx/radeon.c: a stuck engine reset, so that the BIOS's writes to it can't hold the bus), and its
   surfaces cleared before the desktop's. Its register BAR (BAR 2, memory) mapped once. Nothing done without one. */
static struct pci_dev *radeon_pd;

static void radeon_bm(int on)                  /* its bus mastering, at the PCI command register */
{
    u32 c = pci_read32(radeon_pd->bus, radeon_pd->dev, radeon_pd->fn, 0x04);
    pci_write32(radeon_pd->bus, radeon_pd->dev, radeon_pd->fn, 0x04, on ? (c & 0xFFFFu) | 4u : (c & 0xFFFFu) & ~4u);
}

static int radeon_settle(int surfaces)
{
    static const u16 ids[] = { 0x5144, 0x5145, 0x5146, 0x5147, 0x5157, 0x5158, 0x5159, 0x515A };
    static volatile u32 *regs;
    static int looked;
    if (!looked) {
        struct pci_dev *pd = 0;
        u32 k;
        looked = 1;
        for (k = 0; k < sizeof ids / sizeof ids[0]; k++)
            if ((pd = pci_find(0x1002, ids[k], 0)) != 0 && pd->class == 0x03)
                break;
        if (k < sizeof ids / sizeof ids[0] && !(pd->bar[2] & 1) && (pd->bar[2] & ~15u)
            && (regs = mm_dev_map(pd->bar[2] & ~15u, 0x4000)) != 0)
            radeon_pd = pd;
    }
    return regs ? radeon_settle_regs(regs, pit2_wait, radeon_bm, surfaces) : RADEON_IDLE;
}

/* Before a mode set the BIOS makes for GLOS outside this file (a session's end puts its first mode back:
   vm/session.c), which on a PC with no desktop is the only one: the P3's freeze at a kill was there. 0, or -1:
   the card is stuck past two resets, and no mode set may be made. */
int display_card_settle(void) { return radeon_settle(0) == RADEON_DEAD ? -1 : 0; }

/* The desktop's mode set: 0, or -1 with why in the log. */
static int mode_set(void)
{
    d.power = DISPLAY_POWER_ON;                 /* (a mode set turns the screen on) */
    struct gfx_fmt f;
    struct rmregs r;
    u8 m[64];
    u32 pitch;
    void *fb;

    if (!d.mode && !(d.mode = (u16)find(m))) {
        kprintf("GLOS-DISPLAY error=no-mode want=%ux%ux%u\n", d.w, d.h, d.bpp);
        return -1;
    }
    if (!describe(d.mode, m))
        return -1;
    if (radeon_settle(1) == RADEON_DEAD) {
        kprintf("GLOS-DISPLAY error=card-stuck mode=%x\n", d.mode);
        return -1;
    }
    memset(&r, 0, sizeof r);
    r.eax = 0x4F02;
    r.ebx = d.mode | 0x4000u;                   /* with its linear framebuffer */
    if (!vbe_set(&r, d.w, d.h)) {
        kprintf("GLOS-DISPLAY error=mode-set mode=%x ax=%04x\n", d.mode, r.eax & 0xFFFF);
        return -1;
    }
    f.bpp = (u8)(d.bpp == 8 ? 8 : d.bpp == 16 ? 16 : 32);
    f.rsize = m[0x1F], f.rpos = m[0x20], f.gsize = m[0x21], f.gpos = m[0x22], f.bsize = m[0x23], f.bpos = m[0x24];
    pitch = le16(m + 0x10);
    if (le16(m + 0x32) >= (u32)d.w * (f.bpp / 8u)) {    /* VBE 3: the linear framebuffer's own layout */
        pitch = le16(m + 0x32);
        if (f.bpp != 8)
            f.rsize = m[0x36], f.rpos = m[0x37], f.gsize = m[0x38], f.gpos = m[0x39], f.bsize = m[0x3A], f.bpos = m[0x3B];
    }
    d.phys = le32(m + 0x28);
    if (!(fb = mm_fb_map(d.phys, pitch * d.h))) {
        text_mode();
        kprintf("GLOS-DISPLAY error=too-big bytes=%u\n", pitch * d.h);
        return -1;
    }
    gfx_init(&d.fb, fb, d.w, d.h, pitch, &f);
    if (f.bpp == 8)
        palette();
    if (mga_display_up(d.fb.pix, d.phys, pitch, f.bpp, d.h))
        d.fb.dev = &mga_dev;                    /* (the engine draws on it: M7 step D) */
    d.on = 1;
    mm_vga_hide(hidden());
    vkbc_desk(!d.held);
    kprintf("GLOS-DISPLAY on mode=%x %ux%ux%u pitch=%u lfb=%08x rgb=%u.%u.%u dac=%u\n", d.mode, d.w, d.h, d.bpp, pitch,
            d.phys, f.rsize, f.gsize, f.bsize, f.bpp == 8 ? d.dac : 0);
    if (!d.held)
        mixer_desktop(1);                       /* (the mixer is the desktop's: M8) */
    return 0;
}

static void blank(struct gfx_surface *fb) { gfx_fill(fb, 0, 0, fb->w, fb->h, 0x808080); }

void display_painter(void (*paint)(struct gfx_surface *fb)) { d.paint = d.home = paint ? paint : blank; }

/* The screen, while the desktop draws on it: not a session's, not the test card's. */
struct gfx_surface *display_fb(void) { return d.on && !d.held && d.paint != gfx_testcard ? &d.fb : 0; }

void display_sync(void) { mga_display_sync(); }

void display_cursor_image(const u8 *and_mask, const u8 *xor_mask) { mga_cursor_image(and_mask, xor_mask); }

void display_evicter(void (*evict)(void)) { d.evict = evict; }

static void leaving(void)                       /* the card's memory is about to be another's, or laid out again */
{
    if (d.evict && mga_display_on())
        d.evict();
}

void *display_vram_alloc(u32 bytes) { return mga_store_alloc(bytes); }
int display_vram_free(void *p) { return mga_store_free(p); }
const struct gfx_dev *display_vram_dev(const void *p) { return mga_store_dev(p); }
void display_vram_limit(u32 bytes) { mga_store_limit(bytes); }

int display_cursor(int on, s32 x, s32 y) { return d.on && !d.held && mga_cursor(on, x, y); }

/* The display on a surface in the card's memory that is not the screen's own (a full-screen window's pixels,
   an app's: docs/v1/fullscreen-apps.md), or with NULL on the screen again. 1 if it is there now. */
int display_scanout(const struct gfx_surface *s)
{
    u32 off;
    if (!d.on || d.held || !mga_display_on())
        return 0;
    if (!s)
        return mga_scanout(MGA_SCAN_SCREEN, 0);
    off = (u32)(s->pix - (const u8 *)mga_vram_ptr(0));
    return s->pix >= (const u8 *)mga_vram_ptr(0) && off < mga_vram_size() && mga_scanout(off, s->pitch);
}

u32 display_scanout_phys(void) { return d.on ? mga_scanout_phys() : 0; }

/* A mode of its own for a full-screen window whose pixels are not the desktop's size (docs/v1/fullscreen-apps.md,
   F3): the card set to the mode of w by h at the desktop's depth, its memory kept, so that the display can be
   pointed at the window's pixels (display_scanout); the desktop's own pixels stay as they are, unseen. w 0: the
   desktop's mode again. (x, y): where the window's pixels are on the desktop (the cursor is placed from there).
   The mode is the BIOS's to set, by the system VM's thread: 1 once the card is so, 0 while that is asked for
   (done() is called when it is, or cannot be), -1 if the card has no such mode or there is no desktop on a card
   whose engine GLOS drives. Nothing may be drawn on the screen while the card is in such a mode, or while a
   change is asked for. */
static s32 app_x, app_y;

int display_app_mode(u32 w, u32 h, s32 x, s32 y, void (*done)(void))
{
    u32 f;
    int r;
    if (!d.on || d.held || !mga_display_on())
        return w ? -1 : 1;
    if (w == d.w && h == d.h)
        w = h = 0;
    f = irq_save();
    if (w && w == d.bad_w && h == d.bad_h) {
        r = -1;
    } else if (!d.app_ask && d.app_w == w && d.app_h == h) {
        r = 1;
    } else {
        d.want_w = (u16)w, d.want_h = (u16)h;
        app_x = x, app_y = y;
        d.app_done = done;
        d.app_ask = 1;
        vm_kick();
        r = 0;
    }
    irq_restore(f);
    return r;
}

/* The mode the card is in for a window is of no use after all (the window's rows are not the mode's length):
   that size is not asked for again. */
void display_app_refuse(void)
{
    u32 f = irq_save();
    if (d.app_w)
        d.bad_w = d.app_w, d.bad_h = d.app_h;
    irq_restore(f);
}

/* 1 while the card is in such a mode, or on its way into or out of one: the screen's own pixels are not what is
   seen, and the engine's state is not the kernel's to draw with. */
int display_app_busy(void) { return d.on && (d.app_w || d.app_ask); }

static void app_turn(void)                      /* (the VM thread) */
{
    u32 w = d.want_w, h = d.want_h, mode = d.mode, pitch = d.fb.pitch;
    int dead;
    struct rmregs r;
    u8 m[64];
    d.app_ask = 0;
    if (d.on && !d.held && (w != d.app_w || h != d.app_h)) {
        if (w) {
            fit_w = (u16)w, fit_h = (u16)h;
            mode = find(m);
            fit_w = fit_h = 0;
            pitch = mode ? (le16(m + 0x32) >= w * (d.fb.fmt.bpp / 8u) ? le16(m + 0x32) : le16(m + 0x10)) : 0;
        }
        if (!mode) {
            d.bad_w = (u16)w, d.bad_h = (u16)h;
            kprintf("GLOS-DISPLAY app error=no-mode want=%ux%ux%u\n", w, h, d.bpp);
        } else {
            mga_mode_begin();
            dead = radeon_settle(0) == RADEON_DEAD;
            memset(&r, 0, sizeof r);
            r.eax = 0x4F02;
            r.ebx = mode | 0x4000u | 0x8000u;   /* its linear framebuffer; the card's memory kept */
            if (dead || !vbe_set(&r, w ? w : d.w, w ? h : d.h)) {
                if (dead)
                    kprintf("GLOS-DISPLAY app error=card-stuck mode=%x\n", mode);
                else
                    kprintf("GLOS-DISPLAY app error=mode-set mode=%x ax=%04x\n", mode, r.eax & 0xFFFF);
                d.bad_w = (u16)w, d.bad_h = (u16)h;
                if (w && !dead) {               /* (the desktop's again, whatever it left) */
                    memset(&r, 0, sizeof r);
                    r.eax = 0x4F02;
                    r.ebx = d.mode | 0x4000u | 0x8000u;
                    vbe(&r);
                }
                w = h = 0;
                pitch = d.fb.pitch;
            }
            d.app_w = (u16)w, d.app_h = (u16)h;
            mga_mode_end(pitch, w ? app_x : 0, w ? app_y : 0);
            if (w)
                kprintf("GLOS-DISPLAY app mode=%x %ux%ux%u pitch=%u\n", mode, w, h, d.bpp, pitch);
            else
                kprintf("GLOS-DISPLAY app off\n");
        }
    }
    if (d.app_done)
        d.app_done();
}

void display_init(const struct bootinfo *bi)
{
    if (!d.paint)
        d.paint = d.home = blank;
    mga_display_init(bi);
    gpu_init(!bi->gpu_watchdog_ms ? 500 : bi->gpu_watchdog_ms == 0xFFFFFFFFu ? 0 : bi->gpu_watchdog_ms);
    if (bi->display_w) {
        d.w = (u16)bi->display_w;
        d.h = (u16)bi->display_h;
        d.bpp = (u16)bi->display_bpp;
        d.ask = 1 + DISPLAY_ON;
    }
}

/* (Interrupts off.) A change taken for the VM thread to make. */
static void take(int what, u32 w, u32 h, u32 bpp)
{
    if (w && (w != d.w || h != d.h || bpp != d.bpp)) {
        d.w = (u16)w, d.h = (u16)h, d.bpp = (u16)bpp;
        d.mode = 0;                             /* (to be found) */
    }
    d.ask = (u8)(1 + what);
    vm_kick();
}

/* A change asked for: 0, or -1 (one is asked for already and not yet begun). One asked for while another is
   being made waits for that one's end: its numbers taken at once would be the mode's under the VM thread's
   hands (gate 29: a mode logged as "mode=0 640x480x32 pitch=1280 rgb=5.6.5"). */
int display_ask(int what, u32 w, u32 h, u32 bpp)
{
    u32 f = irq_save();
    int r = -1;
    if (d.turning && !d.ask) {
        d.next = (u8)(1 + what), d.next_w = (u16)w, d.next_h = (u16)h, d.next_bpp = (u16)bpp;
        r = 0;
    } else if (!d.ask) {
        take(what, w, h, bpp);
        r = 0;
    }
    irq_restore(f);
    return r;
}

int display_vm_pending(void) { return d.ask || d.list_ask || d.power_ask || d.app_ask; }

/* The modes the card has, for DRAW's MODES (any thread that may wait): listed by the VM thread the first time,
   which is waited for (ten seconds at most: a session may have the VM). How many there are (out: the first max
   of them), or -1 if they aren't known yet. */
int display_modes(struct display_mode *out, u32 max)
{
    u32 f = irq_save(), until = timer_ticks() + 10 * 1024u, i;
    int n = -1;
    if (!d.listed) {
        d.list_ask = 1;
        vm_kick();
        while (!d.listed && thread_wait(&d.asked, until))
            ;
    }
    if (d.listed) {
        for (i = 0; i < d.nmodes && i < max; i++)
            out[i] = d.modes[i];
        n = (int)d.nmodes;
    }
    irq_restore(f);
    return n;
}

/* The screen's power: asked of the VM thread, and waited for (two seconds at most). state -1: only ask what the
   card can. The state now; *can the states the card has (a bit each), 0 if it has no such thing. */
int display_power(int state, u32 *can)
{
    u32 f = irq_save(), until = timer_ticks() + 2 * 1024u;
    if (!d.power_known || (state >= 0 && state != d.power && (d.power_can >> state & 1))) {
        d.power_ask = (u8)(2 + state);
        vm_kick();
        while (d.power_ask && thread_wait(&d.asked, until))
            ;
    }
    if (can)
        *can = d.power_can;
    irq_restore(f);
    return d.power;
}

int display_power_now(void) { return d.power; }

/* ... asked for without waiting (the desktop's thread, for the saver). */
void display_power_post(int state)
{
    u32 f = irq_save();
    d.power_ask = (u8)(2 + state);
    vm_kick();
    irq_restore(f);
}

static void turn(int what);

void display_vm_turn(void)
{
    int what = d.ask - 1;
    if (d.list_ask || d.power_ask) {
        u32 f;
        if (d.list_ask)
            list_modes();
        if (d.power_ask)
            power_set((int)d.power_ask - 2);
        f = irq_save();
        d.list_ask = d.power_ask = 0;
        thread_wake(&d.asked);
        irq_restore(f);
    }
    if (d.app_ask && !d.ask)
        app_turn();
    if (!d.ask)
        return;
    d.turning = 1;                              /* (set before the asking is taken: never neither) */
    d.ask = 0;
    turn(what);
    {
        u32 f = irq_save();
        d.turning = 0;
        if (d.next) {                           /* (what was asked for meanwhile: the next turn's) */
            take(d.next - 1, d.next_w, d.next_h, d.next_bpp);
            d.next = 0;
        }
        irq_restore(f);
    }
}

static void turn(int what)
{
    /* (A mode of a window's own goes with the desktop's.) */
    d.app_ask = 0;
    d.app_w = d.app_h = d.want_w = d.want_h = 0;
    leaving();
    if (what == DISPLAY_OFF) {
        mixer_desktop(0);
        mga_display_down();
        if (d.on) {
            d.on = 0;
            text_mode();
            kprintf("GLOS-DISPLAY off\n");
        }
        d.on = d.held = 0;
        mm_vga_hide(0);
        vkbc_desk(0);
        return;
    }
    if (!d.w) {
        kprintf("GLOS-DISPLAY error=no-mode-given\n");
        return;
    }
    if (d.on && d.mode && what == DISPLAY_CARD) {       /* (the mode stays) */
        gfx_testcard(&d.fb);
        d.paint = gfx_testcard;
        return;
    }
    {
        int was = d.on;
        d.on = 0;
        if (mode_set() != 0)
            return;
        d.paint = what == DISPLAY_CARD ? gfx_testcard : d.home;
        d.paint(&d.fb);
        if (!was && what == DISPLAY_ON)
            kcfg_desktop_started();             /* ([desktop] start: the shell, M9) */
    }
}

int display_desktop_bpp(void) { return d.on ? d.bpp : 0; }

int display_dac_bits(void) { return d.dac == 8 ? 8 : 6; }

void display_session(int begin)
{
    if (!d.on)
        return;
    if (begin && !d.held) {
        /* (A mode of a window's own goes with the screen: the desktop's own mode is set when the session ends.) */
        d.app_ask = 0;
        d.app_w = d.app_h = d.want_w = d.want_h = 0;
        mixer_desktop(0);                       /* (the session's: the Sound Blaster too) */
        leaving();
        mga_display_down();
        d.held = 1;
        mm_vga_hide(0);
        vkbc_desk(0);
        text_mode();
    } else if (!begin && d.held) {
        d.held = 0;
        d.on = 0;
        if (mode_set() == 0)
            d.paint(&d.fb);
    }
}

/* A console job's program runs in its window (M9; vm/session.c): the VGA's memory not there, its ports
   trapped, so that a touch of either makes it full-screen (display_vga_fault, display_vga_port). With no
   desktop (a login's program) the ports alone: the memory is the PC's text screen, which GLOS reads and writes
   itself (glos shot, the idle screen's notice), so a program that only writes there is not seen to. */
void display_console_trap(int on)
{
    u32 p;
    if (d.trap == (on != 0))
        return;
    d.trap = (u8)(on != 0);
    if (!on)
        vm_int10_hook(0);                       /* (the INT 10h vector as it was: v86.c) */
    if (d.on && !d.held)
        mm_vga_hide(hidden());
    for (p = 0x3B0; p < 0x3E0; p++)
        cpu_io_trap(p, on);
}

/* A page fault at linear address a: 1 if it was the VGA's memory a console job touched, now in reach. */
int display_vga_fault(u32 a)
{
    if (a < 0xA0000 || a >= 0xC0000 || !d.trap)
        return 0;
    if (session_console()) {
        kprintf("GLOS-SESSION wants-screen vga=%05x\n", a);
        session_console_full();                 /* (the session's now: text mode, the memory the VGA's again) */
    }
    else
        display_console_trap(0);
    return 1;
}

/* A trapped VGA port touched (vdev.c): 1 if it was a console job's, which is full-screen now; the access is then
   made for real. */
int display_vga_port(u32 port)
{
    if (!d.trap || port < 0x3B0 || port >= 0x3E0)
        return 0;
    if (session_console()) {
        kprintf("GLOS-SESSION wants-screen port=%03x at=%04x:%08x %s\n", port, vdev_from ? vdev_from->cs & 0xFFFF : 0,
                vdev_from ? vdev_from->eip : 0, vdev_from && (vdev_from->eflags & FL_VM) ? "v86" : "pm");
        session_console_full();
    } else {
        display_console_trap(0);
    }
    return 1;
}

void display_leave(void)
{
    int was = d.on && !d.held;
    mixer_desktop(0);
    leaving();
    mga_display_down();
    d.on = 0;
    if (was)
        text_mode();
    mm_vga_hide(0);
    vkbc_desk(0);
}

/* A program's INT 10h (its registers at eax to edx) while the desktop has
   the screen, or a console job's wherever it runs: 1 if it is answered here, and must not reach the BIOS. app:
   the caller is a GLOS app; otherwise it is a program still starting, which
   a mode set shows to be a session: the screen is its from there. */
int display_quiet(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx, int app)
{
    u32 ax = *eax & 0xFFFF, ah = ax >> 8;
    if (d.on ? d.held : app || !session_console())
        return 0;                               /* (with no desktop, a console job's alone: a login's program, whose
                                                   console is its terminal and not the PC's screen. MS-DOS 7.1's
                                                   console driver writes with the teletype, and at a login on
                                                   bench-g400, which has no desktop, nothing a program wrote
                                                   reached the terminal: 2026-11-13) */
    if (!app && session_console()) {            /* a console job's program (M9): what a line-by-line one needs */
        switch (ah) {
        case 0x0E:                              /* the teletype: into its window */
            agent_vm_console_char((u8)ax);
            return 1;
        case 0x06:
        case 0x07:                              /* the whole screen scrolled away: CLS */
            if (!(ax & 0xFF) && !(*ecx & 0xFFFF) && (*edx & 0xFF) >= 79 && ((*edx >> 8) & 0xFF) >= 24) {
                agent_vm_console_char('\f');
                return 1;
            }
            break;
        case 0x02: case 0x01: case 0x03: case 0x05: case 0x08: case 0x0F: case 0x11: case 0x12: case 0x1A: case 0x1B:
            goto quiet;                         /* (the cursor, the mode, what the card is: answered as below) */
        }
        kprintf("GLOS-SESSION wants-screen int10=%04x\n", ax);
        session_console_full();                 /* anything else draws: the screen is the program's from here */
        return 0;
    }
quiet:
    if (!ah || ax == 0x4F02) {                  /* a mode set */
        if (!app) {
            display_session(1);
            return 0;
        }
        if (ah)
            SET16(*eax, 0x014F);                /* VBE: it failed */
        return 1;
    }
    switch (ah) {
    case 0x0F:                                  /* the mode: 3, 80 columns, page 0 */
        SET16(*eax, 0x5003);
        SETHI(*ebx, 0);
        return 1;
    case 0x03:                                  /* the cursor: at the top, its usual shape */
        SET16(*ecx, 0x0607);
        SET16(*edx, 0);
        return 1;
    case 0x08:                                  /* the character there: a space */
        SET16(*eax, 0x0720);
        return 1;
    case 0x11: case 0x12: case 0x1A: case 0x1B: /* what the card is and has: the BIOS's to say; nothing is drawn */
        return (ah == 0x11 && (ax & 0xFF) != 0x30) ? 1 : 0;     /* (but no font is loaded) */
    case 0x4F:                                  /* VBE: its descriptions too; the rest fails */
        if (ax == 0x4F00 || ax == 0x4F01 || ax == 0x4F03)
            return 0;
        SET16(*eax, 0x014F);
        return 1;
    default:                                    /* the cursor, scrolling, characters, pixels, the palette */
        return 1;
    }
}

/* The display's state in a line (glos display). While a change asked for waits for the VM thread or is being
   made, it says so and no more: the mode's numbers are already the new ones, and "on" with them would be a
   mode that is not on the screen yet (gate 29: two suites took their pictures by it, a mode too early, when
   the VM thread was a second late: it was still starting the console). */
u32 display_stat(char *buf, u32 max)
{
    u32 n;
    if (d.ask || d.turning)
        return (u32)ksnprintf(buf, max, "display changing\n");
    char hz[16];
    if (d.hz)
        ksnprintf(hz, sizeof hz, "%u.%02u", d.hz / 100u, d.hz % 100u);
    else
        ksnprintf(hz, sizeof hz, "bios");
    if (!d.on)
        n = (u32)ksnprintf(buf, max, "display off%s\n", d.w ? "" : " (no mode given)");
    else
        n = (u32)ksnprintf(buf, max, "display %s mode=%x %ux%ux%u pitch=%u lfb=%08x rgb=%u.%u.%u dac=%u refresh=%s\n",
                           d.held ? "held" : "on", d.mode, d.w, d.h, d.bpp, d.fb.pitch, d.phys, d.fb.fmt.rsize,
                           d.fb.fmt.gsize, d.fb.fmt.bsize, d.fb.fmt.bpp == 8 ? d.dac : 0, hz);
    if (d.on && d.app_w && n < max)             /* (a full-screen window's own mode is what the card is in) */
        n += (u32)ksnprintf(buf + n, max - n, "app mode %ux%u\n", d.app_w, d.app_h);
    if (n < max)
        n += mga_display_stat(buf + n, max - n);
    if (n < max && mga_display_on())
        n += gpu_stat(buf + n, max - n);
    return n;
}

/* For GLOS_FN_STAT (M9): the desktop's mode (0 without one), whether a card's engine draws it, its memory. */
void display_counts(u32 *w, u32 *h, u32 *bpp, u32 *accel, u32 *vram_kb)
{
    int on = d.on && !d.held;
    *w = on ? d.w : 0;
    *h = on ? d.h : 0;
    *bpp = on ? d.bpp : 0;
    *accel = on && mga_desktop_card();
    *vram_kb = mga_desktop_card() ? mga_vram_size() >> 10 : 0;
}
