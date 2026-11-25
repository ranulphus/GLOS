/* vmouse.c - INT 33h of GLOS's own for DOS programs on a native boot (docs/v1/native-dos.md, N4, "the mouse": no
 * CuteMouse). The Microsoft mouse driver's interface as programs use it: reset, show and hide, the position and
 * the buttons, presses and releases counted, the ranges, the motion counters, the mickey ratio, sensitivity,
 * the version, the virtual screen's size. Fed by the PS/2 mouse's packets as the virtual 8042 reads them
 * (vkbc.c: the AUX bytes, while the desktop hasn't the screen) and by the moves GLOS injects (glos mouse, a USB
 * mouse through GLOS's HID): from a program's first reset on, those are this driver's, not the VM's AUX queue.
 *
 * The event handler (0Ch, 14h; step 2): an event the program's mask asks for raises the VM's virtual IRQ 12, whose
 * vector (INT 74h, set at the first reset, the line unmasked as a driver unmasks it) is an ARPL of GLOS's (vdos.c):
 * there the program's routine is called far with the events in AX, the buttons in BX, the position in CX and DX,
 * the mickeys' running totals in SI and DI, interrupts off; its RETF comes back to another ARPL, which puts the
 * interrupted code's registers back. Events that come meanwhile wait for its return.
 *
 * The cursor (step 3): in a text mode, the software cursor a Microsoft driver draws by default: the cell under the
 * mouse, on the BIOS's active page, written as (its word AND the screen mask) XOR the cursor mask (77FFh, 7700h:
 * the character kept, its colours turned round; 0Ah sets them); put back as it was when the mouse leaves it or the
 * cursor is hidden, if the cell still holds what the driver wrote (a program that wrote there since keeps its
 * own). Taken away at a session's end. Not drawn in graphics modes yet.
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
    u16 pend;                           /* events not yet given to the handler */
    u8 in_handler;
    int rawx, rawy;                     /* the mickeys' running totals (the handler's SI and DI) */
    u16 smask, cmask;                   /* the text cursor's screen and cursor masks (0Ah) */
    u8 tc_drawn;                        /* the text cursor is on the screen: at tc_off, tc_word over tc_save */
    u32 tc_off;
    u16 tc_save, tc_word;
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

/* IRQ 12 open in the VM's PIC (its own bit on the slave, the slave's on the master). A session's end puts the
   system VM's masks back (session.c), so the line is opened again at each reset and each routine set; while it is
   shut no request is raised, so none is left waiting behind a mask (a CLI loop then read as vif-stuck). */
static int line_open(void) { return !(vpic_imr(&vm.pic) & ((1u << 12) | (1u << 2))); }
static int px_x(void) { return m.x - m.x % m.cellx; }
static int px_y(void) { return m.y - m.y % m.celly; }

/* The text cursor where it should be now: taken off its old cell (put back if that still holds the driver's
   word), drawn on the new one; nothing in a graphics mode or while hidden. Interrupts off. */
static void cursor(void)
{
    u8 mode = vm_rd8(0x449) & 0x7F;
    int text = mode <= 3 || mode == 7, want = m.on && m.shown >= 0 && text && mode == m.mode;
    u32 off = 0;

    if (want) {
        int cols = vm_rd16(0x44A) ? vm_rd16(0x44A) : 80, col = m.x / m.cellx, row = m.y / m.celly;
        off = (mode == 7 ? 0xB0000u : 0xB8000u) + vm_rd16(0x44E) + (u32)(row * cols + col) * 2;
    }
    if (m.tc_drawn && (!want || off != m.tc_off)) {
        if (m.mode == mode && vm_rd16(m.tc_off) == m.tc_word)
            vm_wr16(m.tc_off, m.tc_save);
        m.tc_drawn = 0;
    }
    if (want && !m.tc_drawn) {
        m.tc_save = vm_rd16(off);
        m.tc_word = (u16)((m.tc_save & m.smask) ^ m.cmask);
        vm_wr16(off, m.tc_word);
        m.tc_off = off;
        m.tc_drawn = 1;
    }
}

/* A session ended (session.c): the cursor taken off the screen and hidden, and the program's routine forgotten
   (its code is gone: a program killed, or one that never took its routine away, would be called there next). */
void vmouse_session_end(void)
{
    u32 f = irq_save();

    if (m.on) {
        m.shown = -1;
        cursor();
    }
    m.handler = 0, m.hmask = 0, m.pend = 0, m.in_handler = 0;
    irq_restore(f);
}

/* The mouse moved by dx, dy mickeys (down and right positive), its buttons now b (bit 0 left, 1 right, 2 middle;
   -1: as they were). Interrupt context or the VM's: with interrupts off. */
void vmouse_move(int dx, int dy, int b)
{
    u32 f = irq_save();
    int i;

    m.mickx += dx, m.micky += dy;
    m.rawx += dx, m.rawy += dy;
    if (dx || dy)
        m.pend |= 1;
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
    if (m.tc_drawn || m.shown >= 0)
        cursor();
    if (b >= 0) {
        for (i = 0; i < 3; i++) {
            u8 bit = (u8)(1u << i);
            if ((b & bit) && !(m.buttons & bit))
                m.press[i]++, m.px[i] = m.x, m.py[i] = m.y, m.pend |= (u16)(2u << (2 * i));
            else if (!(b & bit) && (m.buttons & bit))
                m.release[i]++, m.rx[i] = m.x, m.ry[i] = m.y, m.pend |= (u16)(4u << (2 * i));
        }
        m.buttons = (u8)(b & 7);
    }
    if (m.handler && (m.pend & m.hmask) && !m.in_handler && line_open()) {
        vpic_raise(&vm.pic, 12);        /* (the routine is called at INT 74h's ARPL: vmouse_event) */
        vm_kick();
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

/* INT 74h's ARPL (vdos.c), the VM's thread: 1 and the routine's registers in r and its address in *handler if
   events the mask asks for wait, 0 if none (a raise that the mask or a 0Ch since has made empty). */
int vmouse_event(struct dos_regs86 *r, u32 *handler)
{
    u32 f = irq_save();
    u16 ev = m.pend & m.hmask;

    if (!m.handler || !ev || m.in_handler) {
        irq_restore(f);
        return 0;
    }
    m.pend = 0;
    m.in_handler = 1;
    r->ax = ev, r->bx = m.buttons, r->cx = (unsigned)px_x(), r->dx = (unsigned)px_y();
    r->si = (unsigned)(m.rawx & 0xFFFF), r->di = (unsigned)(m.rawy & 0xFFFF);
    *handler = m.handler;
    irq_restore(f);
    return 1;
}

/* The routine came back (vdos.c's return ARPL): events that came meanwhile raise the line again. */
void vmouse_event_done(void)
{
    u32 f = irq_save();

    m.in_handler = 0;
    if (m.handler && (m.pend & m.hmask) && line_open()) {
        vpic_raise(&vm.pic, 12);
        vm_kick();
    }
    irq_restore(f);
}

/* Whether GLOS's INT 33h has the mouse (a program reset it). */
int vmouse_on(void) { return m.on; }


static void reset(void)
{
    u32 f = irq_save();

    m.shown = -1;
    cursor();                           /* (off the screen first, while the old mode's geometry holds) */
    m.smask = 0x77FF, m.cmask = 0x7700;
    memset(&m.press, 0, sizeof m.press), memset(&m.release, 0, sizeof m.release);
    geometry();
    m.x = (m.maxx + 1) / 2, m.y = (m.maxy + 1) / 2, m.fx = m.fy = 0;
    m.mickx = m.micky = 0;
    m.ratio_x = 8, m.ratio_y = 16;
    m.shown = -1;
    m.hmask = 0, m.handler = 0, m.pend = 0;
    m.sens_x = m.sens_y = m.dspeed = 50;
    m.page = 0;
    m.npkt = 0;
    vdos_mouse_irq();                   /* (INT 74h GLOS's, IRQ 12 open: the routine's calls; each reset) */
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
        u32 f = irq_save();             /* (a mode set since: the virtual screen of the new one; the old */
        m.tc_drawn = 0;                 /* screen and the cursor on it are gone) */
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
    case 0x02: {
        u32 f = irq_save();
        if (ax == 1 && m.shown < 0)
            m.shown++;
        else if (ax == 2)
            m.shown--;
        cursor();
        irq_restore(f);
        break;
    }
    case 0x03:
        r->bx = m.buttons, r->cx = (unsigned)px_x(), r->dx = (unsigned)px_y();
        break;
    case 0x04: {
        u32 f = irq_save();
        m.x = clampi((short)r->cx, m.minx, m.maxx), m.y = clampi((short)r->dx, m.miny, m.maxy), m.fx = m.fy = 0;
        cursor();
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
        cursor();
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
    case 0x0C: {
        u32 f = irq_save();
        m.hmask = (u16)r->cx, m.handler = (u32)r->es << 16 | (r->dx & 0xFFFF), m.pend = 0;
        irq_restore(f);
        if (m.handler && m.hmask)
            vdos_mouse_irq();
        break;
    }
    case 0x0A: {                        /* the text cursor: BX 0 a software one, CX the screen mask, DX the cursor's */
        u32 f = irq_save();
        if (!r->bx) {
            u8 shown = m.tc_drawn;
            int keep = m.shown;
            m.shown = -1;
            cursor();
            m.smask = (u16)r->cx, m.cmask = (u16)r->dx;
            m.shown = keep;
            if (shown)
                cursor();
        }
        irq_restore(f);
        break;
    }
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
        if (m.handler && m.hmask)
            vdos_mouse_irq();
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
