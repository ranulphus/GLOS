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
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "vm.h"

void gfx_testcard(struct gfx_surface *s);

static struct {
    u8 on;                              /* the screen is in the desktop's mode */
    u8 held;                            /* ... but for a session, which has it in text mode */
    u8 ask;                             /* 1 + DISPLAY_: what the VM thread is to do */
    u8 dac;                             /* bits a colour in the DAC, at 8 bits a pixel */
    u16 w, h, bpp;                      /* the mode wanted */
    u16 mode;                           /* its VBE number */
    u32 phys;
    struct gfx_surface fb;
    void (*paint)(struct gfx_surface *fb);      /* who draws the whole screen now ... */
    void (*home)(struct gfx_surface *fb);       /* ... and when it isn't the test card's */
} d;

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
    mm_vga_hide(d.on && !d.held);
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

/* How well a mode's description fits what is wanted: 0 not at all, 2 as
   asked, 1 the 5-5-5 mode for 16 bits. */
static int fits(const u8 *m)
{
    u32 attr = le16(m), bpp = m[0x19], model = m[0x1B];
    if ((attr & 0x91) != 0x91 || le16(m + 0x12) != d.w || le16(m + 0x14) != d.h || m[0x18] != 1 || !le32(m + 0x28))
        return 0;                               /* (supported, graphics, with a linear framebuffer; one plane) */
    if (d.bpp == 8)
        return bpp == 8 && model == 4 ? 2 : 0;
    if (model != 6 && model != 4)               /* direct colour (some BIOSes say packed for it) */
        return 0;
    if (d.bpp == 16)
        return bpp == 16 && m[0x21] == 6 ? 2 : (bpp == 15 || (bpp == 16 && m[0x21] == 5)) ? 1 : 0;
    return bpp == d.bpp ? 2 : 0;
}

/* The mode for d.w x d.h x d.bpp, its description in info: its number, or 0. */
static u32 find(u8 *info)
{
    u16 list[128];
    u8 m[64];
    struct rmregs r;
    u32 n = 0, i, best = 0, seg;
    int score = 0, s;

    memset(&r, 0, sizeof r);                    /* the BIOS's own list: 4F00h wants 512 bytes, DOS's for a moment */
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
            for (; n < ARRAY_SIZE(list) && p < 0x10FFFE && vm_rd16(p) != 0xFFFF; p += 2)
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

static void text_mode(void)
{
    struct rmregs r;
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

/* The desktop's mode set: 0, or -1 with why in the log. */
static int mode_set(void)
{
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
    memset(&r, 0, sizeof r);
    r.eax = 0x4F02;
    r.ebx = d.mode | 0x4000u;                   /* with its linear framebuffer */
    if (!vbe(&r)) {
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
    d.on = 1;
    mm_vga_hide(!d.held);
    kprintf("GLOS-DISPLAY on mode=%x %ux%ux%u pitch=%u lfb=%08x rgb=%u.%u.%u dac=%u\n", d.mode, d.w, d.h, d.bpp, pitch,
            d.phys, f.rsize, f.gsize, f.bsize, f.bpp == 8 ? d.dac : 0);
    return 0;
}

static void blank(struct gfx_surface *fb) { gfx_fill(fb, 0, 0, fb->w, fb->h, 0x808080); }

void display_painter(void (*paint)(struct gfx_surface *fb)) { d.paint = d.home = paint ? paint : blank; }

/* The screen, while the desktop draws on it: not a session's, not the test card's. */
struct gfx_surface *display_fb(void) { return d.on && !d.held && d.paint != gfx_testcard ? &d.fb : 0; }

void display_init(const struct bootinfo *bi)
{
    if (!d.paint)
        d.paint = d.home = blank;
    if (bi->display_w) {
        d.w = (u16)bi->display_w;
        d.h = (u16)bi->display_h;
        d.bpp = (u16)bi->display_bpp;
        d.ask = 1 + DISPLAY_ON;
    }
}

int display_ask(int what, u32 w, u32 h, u32 bpp)
{
    u32 f = irq_save();
    int r = -1;
    if (!d.ask) {
        if (w && (w != d.w || h != d.h || bpp != d.bpp)) {
            d.w = (u16)w, d.h = (u16)h, d.bpp = (u16)bpp;
            d.mode = 0;                         /* (to be found) */
        }
        d.ask = (u8)(1 + what);
        vm_kick();
        r = 0;
    }
    irq_restore(f);
    return r;
}

int display_vm_pending(void) { return d.ask != 0; }

void display_vm_turn(void)
{
    int what = d.ask - 1;
    if (!d.ask)
        return;
    d.ask = 0;
    if (what == DISPLAY_OFF) {
        if (d.on) {
            d.on = 0;
            text_mode();
            kprintf("GLOS-DISPLAY off\n");
        }
        d.on = d.held = 0;
        mm_vga_hide(0);
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
    d.on = 0;
    if (mode_set() != 0)
        return;
    d.paint = what == DISPLAY_CARD ? gfx_testcard : d.home;
    d.paint(&d.fb);
}

void display_session(int begin)
{
    if (!d.on)
        return;
    if (begin && !d.held) {
        d.held = 1;
        mm_vga_hide(0);
        text_mode();
    } else if (!begin && d.held) {
        d.held = 0;
        d.on = 0;
        if (mode_set() == 0)
            d.paint(&d.fb);
    }
}

void display_leave(void)
{
    int was = d.on && !d.held;
    d.on = 0;
    if (was)
        text_mode();
    mm_vga_hide(0);
}

/* A program's INT 10h (its registers at eax to edx) while the desktop has
   the screen: 1 if it is answered here, and must not reach the BIOS. app:
   the caller is a GLOS app; otherwise it is a program still starting, which
   a mode set shows to be a session: the screen is its from there. */
int display_quiet(u32 *eax, u32 *ebx, u32 *ecx, u32 *edx, int app)
{
    u32 ax = *eax & 0xFFFF, ah = ax >> 8;
    if (!d.on || d.held)
        return 0;
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

u32 display_stat(char *buf, u32 max)
{
    if (!d.on)
        return (u32)ksnprintf(buf, max, "display off%s\n", d.w ? "" : " (no mode given)");
    return (u32)ksnprintf(buf, max, "display %s mode=%x %ux%ux%u pitch=%u lfb=%08x rgb=%u.%u.%u dac=%u\n",
                          d.held ? "held" : "on", d.mode, d.w, d.h, d.bpp, d.fb.pitch, d.phys, d.fb.fmt.rsize,
                          d.fb.fmt.gsize, d.fb.fmt.bsize, d.fb.fmt.bpp == 8 ? d.dac : 0);
}
