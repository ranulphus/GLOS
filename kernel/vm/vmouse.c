/* vmouse.c - INT 33h of GLOS's own for DOS programs on a native boot (docs/v1/native-dos.md, N4, "the mouse": no
 * CuteMouse). The Microsoft mouse driver's interface as programs use it: reset, show and hide, the position and
 * the buttons, presses and releases counted, the ranges, the motion counters, the mickey ratio, sensitivity,
 * the version, the virtual screen's size. Fed by the PS/2 mouse's packets as the virtual 8042 reads them
 * (vkbc.c: the AUX bytes, while the desktop hasn't the screen) and by the moves GLOS injects (glos mouse, a USB
 * mouse through GLOS's HID): from a program's first reset on, those are this driver's, not the VM's AUX queue.
 *
 * First step (2026-11-25): the state and the functions. The event handler (0Ch, 14h) is kept but not yet called,
 * and no cursor is drawn: the next two steps.
 *
 * Positions are in the virtual screen a Microsoft driver gives each BIOS mode: 640 wide in every text and CGA/EGA
 * mode (a text cell 8 or 16 wide, 8 high, positions in its multiples), 200, 350 or 480 high; mode 13h's 320 too is
 * 640 wide. The mode is looked at again on every call, as a driver that hooks INT 10h sees its sets. */
#include <string.h>

#include "arch.h"
#include "io.h"
#include "kprintf.h"
#include "vm.h"
#include "../ndos/dos21.h"

static struct {
    u8 on;                              /* a program has reset the driver: the mouse is its */
    u8 mode;                            /* the BIOS's mode the geometry was made for */
    int x, y, fx, fy;                   /* the position; the remainders of mickeys not yet a pixel */
    int minx, maxx, miny, maxy, cellx, celly;
    u8 buttons;
    u16 press[3], release[3];
    int px[3], py[3], rx[3], ry[3];     /* where each button last went down, and up */
    int mickx, micky;                   /* 0Bh's counters */
    int ratio_x, ratio_y;               /* mickeys per 8 pixels (8 and 16) */
    int shown;                          /* the cursor's counter: 0 shown, below hidden */
    u16 hmask;                          /* 0Ch: the handler's events, and the handler (seg << 16 | off) */
    u32 handler;
    u16 sens_x, sens_y, dspeed, page;
    u8 pkt[4], npkt;
} m;
static u32 said;                        /* (functions seen, first time each: what programs ask of it) */

/* The virtual screen of the BIOS's mode now (0040:0049h; the text rows at 0040:0084h). */
static void geometry(void)
{
    u8 mode = vm_rd8(0x449) & 0x7F;
    int cols = vm_rd16(0x44A), rows = vm_rd8(0x484) + 1, h = 200;

    m.mode = mode;
    m.cellx = m.celly = 1;
    if (mode <= 3 || mode == 7) {
        if (cols < 40 || cols > 132)
            cols = 80;
        if (rows < 25 || rows > 60)
            rows = 25;
        m.cellx = 640 / cols;
        m.celly = 8;
        h = rows * 8;
    } else if (mode == 0x0F || mode == 0x10) {
        h = 350;
    } else if (mode == 0x11 || mode == 0x12) {
        h = 480;
    }
    m.minx = 0, m.maxx = 639, m.miny = 0, m.maxy = h - 1;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The mouse moved by dx, dy mickeys (down and right positive), its buttons now b (bit 0 left, 1 right, 2 middle;
   -1: as they were). Interrupt context or the VM's: with interrupts off. */
void vmouse_move(int dx, int dy, int b)
{
    u32 f = irq_save();
    int i;

    m.mickx += dx, m.micky += dy;
    m.fx += dx * 8, m.fy += dy * 8;
    if (m.ratio_x > 0) {
        m.x += m.fx / m.ratio_x;
        m.fx %= m.ratio_x;
    }
    if (m.ratio_y > 0) {
        m.y += m.fy / m.ratio_y;
        m.fy %= m.ratio_y;
    }
    m.x = clampi(m.x, m.minx, m.maxx);
    m.y = clampi(m.y, m.miny, m.maxy);
    if (b >= 0) {
        for (i = 0; i < 3; i++) {
            u8 bit = (u8)(1u << i);
            if ((b & bit) && !(m.buttons & bit))
                m.press[i]++, m.px[i] = m.x, m.py[i] = m.y;
            else if (!(b & bit) && (m.buttons & bit))
                m.release[i]++, m.rx[i] = m.x, m.ry[i] = m.y;
        }
        m.buttons = (u8)(b & 7);
    }
    irq_restore(f);
}

/* A byte from the PS/2 mouse (vkbc.c's AUX): packets of three, four with the wheel on, the first with bit 3 set
   (a byte without it where a packet should begin is dropped: back in step). */
void vmouse_aux(u8 b, int wheel)
{
    int dx, dy;

    if (!m.npkt && !(b & 0x08))
        return;
    m.pkt[m.npkt++] = b;
    if (m.npkt < (wheel ? 4 : 3))
        return;
    m.npkt = 0;
    dx = m.pkt[1] - ((m.pkt[0] & 0x10) ? 256 : 0);
    dy = m.pkt[2] - ((m.pkt[0] & 0x20) ? 256 : 0);
    vmouse_move(dx, -dy, m.pkt[0] & 7);
}

/* Whether GLOS's INT 33h has the mouse (a program reset it). */
int vmouse_on(void) { return m.on; }

static int px_x(void) { return m.x - m.x % m.cellx; }
static int px_y(void) { return m.y - m.y % m.celly; }

static void reset(void)
{
    u32 f = irq_save();

    memset(&m.press, 0, sizeof m.press), memset(&m.release, 0, sizeof m.release);
    geometry();
    m.x = (m.maxx + 1) / 2, m.y = (m.maxy + 1) / 2, m.fx = m.fy = 0;
    m.mickx = m.micky = 0;
    m.ratio_x = 8, m.ratio_y = 16;
    m.shown = -1;
    m.hmask = 0, m.handler = 0;
    m.sens_x = m.sens_y = m.dspeed = 50;
    m.page = 0;
    m.npkt = 0;
    if (!m.on) {
        m.on = 1;
        irq_restore(f);
        vkbc_vmouse(1);                 /* (the PS/2 mouse turned on, its bytes ours) */
        kprintf("GLOS-MOUSE int33 on mode=%02x screen=%dx%d\n", m.mode, m.maxx + 1, m.maxy + 1);
        return;
    }
    irq_restore(f);
}

/* INT 33h from the system VM (vdos.c's ARPL): the registers in, the answer in them. */
void vmouse_int33(struct dos_regs86 *r)
{
    unsigned ax = r->ax & 0xFFFF, i;

    if (ax < 32 && !(said & (1u << ax)) && ax != 3 && ax != 0x0B) {
        said |= 1u << ax;
        kprintf("GLOS-MOUSE int33 ax=%02x first\n", ax);
    }
    if (m.on && (vm_rd8(0x449) & 0x7F) != m.mode && ax != 0 && ax != 0x21) {
        u32 f = irq_save();             /* (a mode set since: the virtual screen of the new one) */
        geometry();
        m.x = clampi(m.x, m.minx, m.maxx), m.y = clampi(m.y, m.miny, m.maxy);
        irq_restore(f);
    }
    switch (ax) {
    case 0x00:                          /* reset: the driver is there, two buttons */
    case 0x21:
        reset();
        r->ax = 0xFFFF, r->bx = 2;
        break;
    case 0x01:
        if (m.shown < 0)
            m.shown++;
        break;
    case 0x02:
        m.shown--;
        break;
    case 0x03:
        r->bx = m.buttons, r->cx = (unsigned)px_x(), r->dx = (unsigned)px_y();
        break;
    case 0x04: {
        u32 f = irq_save();
        m.x = clampi((short)r->cx, m.minx, m.maxx), m.y = clampi((short)r->dx, m.miny, m.maxy), m.fx = m.fy = 0;
        irq_restore(f);
        break;
    }
    case 0x05:                          /* a button's presses since the last ask, where the last was */
    case 0x06:
        i = r->bx < 3 ? r->bx : 0;
        r->ax = m.buttons;
        if (ax == 5)
            r->bx = m.press[i], r->cx = (unsigned)m.px[i], r->dx = (unsigned)m.py[i], m.press[i] = 0;
        else
            r->bx = m.release[i], r->cx = (unsigned)m.rx[i], r->dx = (unsigned)m.ry[i], m.release[i] = 0;
        break;
    case 0x07:
    case 0x08: {
        int a = (short)r->cx, b = (short)r->dx, lo = a < b ? a : b, hi = a < b ? b : a;
        u32 f = irq_save();
        if (ax == 7)
            m.minx = lo, m.maxx = hi, m.x = clampi(m.x, lo, hi);
        else
            m.miny = lo, m.maxy = hi, m.y = clampi(m.y, lo, hi);
        irq_restore(f);
        break;
    }
    case 0x0B: {                        /* the mickeys since the last ask */
        u32 f = irq_save();
        r->cx = (unsigned)(m.mickx & 0xFFFF), r->dx = (unsigned)(m.micky & 0xFFFF);
        m.mickx = m.micky = 0;
        irq_restore(f);
        break;
    }
    case 0x0C:
        m.hmask = (u16)r->cx, m.handler = (u32)r->es << 16 | (r->dx & 0xFFFF);
        break;
    case 0x0F:
        if (r->cx && r->dx)
            m.ratio_x = (int)(r->cx & 0x7FFF), m.ratio_y = (int)(r->dx & 0x7FFF);
        break;
    case 0x13:
        m.dspeed = (u16)r->dx;
        break;
    case 0x14: {                        /* swap handlers */
        u16 om = m.hmask;
        u32 oh = m.handler;
        m.hmask = (u16)r->cx, m.handler = (u32)r->es << 16 | (r->dx & 0xFFFF);
        r->cx = om, r->es = oh >> 16, r->dx = oh & 0xFFFF;
        break;
    }
    case 0x15:                          /* the state's size: what 16h and 17h move */
        r->bx = sizeof m;
        break;
    case 0x16:
    case 0x17: {
        u32 lin = ((r->es & 0xFFFF) << 4) + (r->dx & 0xFFFF);
        u8 *p = (u8 *)&m, on = m.on;
        for (i = 0; i < sizeof m; i++)
            if (ax == 0x16)
                vm_wr8(lin + i, p[i]);
            else
                p[i] = vm_rd8(lin + i);
        m.on = on;
        break;
    }
    case 0x1A:
        m.sens_x = (u16)r->bx, m.sens_y = (u16)r->cx, m.dspeed = (u16)r->dx;
        break;
    case 0x1B:
        r->bx = m.sens_x, r->cx = m.sens_y, r->dx = m.dspeed;
        break;
    case 0x1D:
        m.page = (u16)r->bx;
        break;
    case 0x1E:
        r->bx = m.page;
        break;
    case 0x1F:                          /* "disable": no vector of another's to give back */
        r->ax = 0x001F, r->es = 0, r->bx = 0;
        break;
    case 0x24:                          /* version 8.05, a PS/2 mouse */
        r->bx = 0x0805, r->cx = 0x0400;
        break;
    case 0x26:                          /* the virtual screen's largest coordinates */
        r->bx = 0, r->cx = (unsigned)m.maxx, r->dx = (unsigned)m.maxy;
        break;
    default:                            /* (the cursor's shapes, areas, rates, ...: nothing to do yet) */
        break;
    }
}
