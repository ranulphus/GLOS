/* GLOS's MIDI ports (API 2.0's AUDIO MIDI_OUT and MIDI_IN; docs/v1/phase-b.md step 10).
 *
 * Port FM is the card's FM chip, an OPL3 or an OPL2, at the AdLib's ports (388h) or where a Crystal chip has it:
 * found by its timer (timer 1 at its shortest sets the status's top bits within 80 microseconds), an OPL3 by its
 * status's low bits being 0. GLOS's FM driver (fm.c) plays it. Register writes wait as the chips ask: an OPL2
 * 3.3 microseconds after the address and 23 after the data (reads of the status port, a microsecond or more
 * each on an ISA bus); an OPL3 needs next to nothing.
 *
 * Port MPU is an MPU-401 in UART mode (BLASTER's P, or a Crystal chip's): command FFh resets it (answered FEh,
 * but not from UART mode), 3Fh enters UART mode (answered FEh); the status port's bit 6 clear says a byte may be
 * written, bit 7 clear that one may be read. It is in UART mode only while an app has it and the desktop has the
 * machine: programs find it as it is after power-on.
 *
 * A port out has a queue of messages with their times (midiq.c). Every kernel tick (1024 a second, in the
 * interrupt) plays what is due: the FM chip's at once, at most TICK_MAX a tick; an MPU-401's go as bytes (with
 * the running status) into a ring that each tick empties as far as the UART takes them. What a port in receives
 * is read on the tick too (and in the SB16's interrupt, which the MPU-401 shares), parsed (midiq.c) and kept
 * with its time for the app's events.
 *
 * While a session has the machine the chips are the session's: what waited is dropped, the notes are ended, the
 * FM chip is an OPL2 again and the MPU-401 back in its first mode; the ports stay open and play again after. */
#include <string.h>

#include "glos/bootinfo.h"
#include "app.h"
#include "arch.h"
#include "fm.h"
#include "io.h"
#include "kprintf.h"
#include "midi.h"
#include "timer.h"

#define TICK_MAX 8
#define TX 256
#define IN 64

static struct {
    u16 fm_port;
    u8 fm;                                      /* 0 none, 2 an OPL2, 3 an OPL3 */
    u16 mpu;                                    /* its port, 0 none */
    u8 mpu_sb;                                  /* ... the SB16's: its interrupt is the card's */
    u8 uart;                                    /* it is in UART mode */
    u8 desk;
    struct out {
        struct app *a;
        struct midiq q;
        u32 played, late;                       /* messages played; the longest any was late, in milliseconds */
    } out[MIDI_PORTS];
    struct app *in_app;
    struct midi_parse parse;
    struct midi_msg in[IN];
    u32 in_h, in_n, in_got, in_lost;
    u8 tx[TX];
    u32 tx_h, tx_n;
    u8 tx_status;                               /* the running status as sent */
} md;

static u32 now_ms(void) { return (u32)(((unsigned long long)timer_ticks() * 125) >> 7); }

/* ---- the FM chip */

static void fm_wait(u32 n)
{
    while (n--)
        inb(md.fm_port);
}

static void fm_out(u16 reg, u8 v)
{
    u16 p = (u16)(md.fm_port + ((reg & 0x100) ? 2 : 0));
    outb(p, (u8)reg);
    fm_wait(md.fm == 3 ? 2 : 6);
    outb((u16)(p + 1), v);
    fm_wait(md.fm == 3 ? 2 : 36);
}

static void fm_find(u16 port)
{
    u8 s1, s2;
    u32 f = irq_save();
    md.fm_port = port;
    md.fm = 2;                                  /* (an OPL2's waits, until it is known) */
    fm_out(0x04, 0x60);                         /* the timers reset */
    fm_out(0x04, 0x80);                         /* the interrupt reset */
    s1 = inb(port);
    fm_out(0x02, 0xFF);                         /* timer 1 at its shortest */
    fm_out(0x04, 0x21);                         /* started */
    fm_wait(200);
    s2 = inb(port);
    fm_out(0x04, 0x60);
    fm_out(0x04, 0x80);
    md.fm = (s1 & 0xE0) != 0x00 || (s2 & 0xE0) != 0xC0 ? 0 : (s2 & 0x06) ? 2 : 3;
    irq_restore(f);
    if (md.fm)
        kprintf("GLOS-MIDI fm port=%x chip=opl%u\n", port, md.fm);
}

/* ---- the MPU-401 */

static int mpu_writable(u32 tries)
{
    while (tries--)
        if (!(inb((u16)(md.mpu + 1)) & 0x40))
            return 1;
    return 0;
}

static int mpu_cmd(u8 cmd, u32 tries)           /* 0: answered */
{
    if (!mpu_writable(tries))
        return -1;
    outb((u16)(md.mpu + 1), cmd);
    while (tries--)
        if (!(inb((u16)(md.mpu + 1)) & 0x80) && inb(md.mpu) == 0xFE)
            return 0;
    return -1;
}

static void mpu_find(u16 port, int sb)
{
    u32 f;
    int ok;
    if (inb((u16)(port + 1)) == 0xFF)           /* (nothing there) */
        return;
    f = irq_save();
    md.mpu = port;
    ok = mpu_cmd(0xFF, 20000) == 0 || mpu_cmd(0xFF, 20000) == 0;       /* (no answer from UART mode: once more) */
    if (!ok)
        md.mpu = 0;
    md.mpu_sb = (u8)(ok && sb);
    irq_restore(f);
    if (ok)
        kprintf("GLOS-MIDI mpu port=%x\n", port);
}

static int mpu_mode(int uart)                   /* (interrupts off) 0: it is in that mode */
{
    if (uart && !md.uart) {
        if (mpu_cmd(0x3F, 20000) != 0)
            return -1;
        md.uart = 1;
        md.tx_status = 0;
        if (md.mpu_sb)
            sb16_midi(1);
    } else if (!uart && md.uart) {
        mpu_cmd(0xFF, 300);                     /* (unanswered from UART mode; an answer is read all the same) */
        md.uart = 0;
        if (md.mpu_sb)
            sb16_midi(0);
    }
    return 0;
}

static void tx_put(u8 b)
{
    md.tx[(md.tx_h + md.tx_n++) % TX] = b;
}

static void tx_message(const struct midi_msg *m)
{
    if (m->status != md.tx_status)
        tx_put(md.tx_status = m->status);
    tx_put(m->d1);
    if (midi_len(m->status) == 3)
        tx_put(m->d2);
}

static void tx_notes_off(void)                  /* every channel's notes off, and its pedal up: 80 bytes */
{
    u32 ch;
    for (ch = 0; ch < 16; ch++) {
        tx_put((u8)(0xB0 | ch));
        tx_put(123), tx_put(0);
        tx_put(64), tx_put(0);
    }
    md.tx_status = 0;
}

static void tx_pump(u32 most)
{
    while (most-- && md.tx_n && !(inb((u16)(md.mpu + 1)) & 0x40)) {
        outb(md.mpu, md.tx[md.tx_h]);
        md.tx_h = (md.tx_h + 1) % TX;
        md.tx_n--;
    }
}

void midi_mpu_drain(void)
{
    struct midi_msg m;
    u32 n, now;
    if (!md.mpu || !md.uart)
        return;
    now = now_ms();
    for (n = 0; n < 64 && !(inb((u16)(md.mpu + 1)) & 0x80); n++) {
        u8 b = inb(md.mpu);
        if (!md.in_app || !midi_parse(&md.parse, b, &m))
            continue;
        m.time = now;
        md.in_got++;
        if (md.in_n < IN)
            md.in[(md.in_h + md.in_n++) % IN] = m;
        else
            md.in_lost++;
    }
}

/* ---- the ports */

void midi_init(const struct bootinfo *bi)
{
    fm_find(0x388);
    if (bi->sb_mpu)
        mpu_find((u16)bi->sb_mpu, 1);
}

void midi_late(void)
{
    if (!md.fm && crystal_fm())
        fm_find(crystal_fm());
    if (!md.mpu && crystal_mpu())
        mpu_find(crystal_mpu(), 0);
}

static const char *port_name(u32 port) { return port == MIDI_FM ? "fm" : "mpu"; }

int midi_open(struct app *a, u32 port, int in)
{
    u32 f;
    if (port >= MIDI_PORTS || (port == MIDI_FM ? in || !md.fm : !md.mpu))
        return MIDI_E_NONE;
    f = irq_save();
    if (!md.desk) {
        irq_restore(f);
        return MIDI_E_AWAY;
    }
    if (in ? md.in_app != 0 : md.out[port].a != 0) {
        irq_restore(f);
        return MIDI_E_BUSY;
    }
    if (port == MIDI_MPU && mpu_mode(1) != 0) {
        irq_restore(f);
        return MIDI_E_NONE;
    }
    if (in) {
        md.in_app = a;
        memset(&md.parse, 0, sizeof md.parse);
        md.in_h = md.in_n = md.in_got = md.in_lost = 0;
    } else {
        md.out[port].a = a;
        md.out[port].played = md.out[port].late = 0;
        midiq_clear(&md.out[port].q);
        if (port == MIDI_FM)
            fm_reset(fm_out, md.fm == 3);
    }
    irq_restore(f);
    kprintf("GLOS-MIDI open app=%u port=%s dir=%s\n", app_id(a), port_name(port), in ? "in" : "out");
    return 0;
}

void midi_close(struct app *a, u32 port, int in)
{
    u32 f, played = 0, late = 0;
    if (port >= MIDI_PORTS)
        return;
    f = irq_save();
    if (in ? port != MIDI_MPU || md.in_app != a : md.out[port].a != a) {
        irq_restore(f);
        return;
    }
    if (in) {
        md.in_app = 0;
        played = md.in_got, late = md.in_lost;
    } else {
        played = md.out[port].played, late = md.out[port].late;
        md.out[port].a = 0;
        midiq_clear(&md.out[port].q);
        if (port == MIDI_FM) {
            fm_end();
        } else if (md.uart) {                   /* (the tick sends these, then leaves UART mode) */
            md.tx_h = md.tx_n = 0;
            tx_notes_off();
        }
    }
    irq_restore(f);
    kprintf("GLOS-MIDI close app=%u port=%s dir=%s %s=%u %s=%u\n", app_id(a), port_name(port), in ? "in" : "out",
            in ? "received" : "played", played, in ? "lost" : "late", late);
}

int midi_send(struct app *a, u32 port, const struct midi_msg *m, u32 n, u32 *taken)
{
    u32 i = 0;
    int r = 0;
    *taken = 0;
    if (port >= MIDI_PORTS)
        return MIDI_E_NOT_OPEN;
    while (i < n && r == 0) {                   /* (sixteen at a time: a message put in among others moves them) */
        u32 f = irq_save(), now = now_ms(), k;
        if (md.out[port].a != a) {
            irq_restore(f);
            return MIDI_E_NOT_OPEN;
        }
        for (k = 0; k < 16 && i < n && md.desk; k++, i++)
            if ((r = midiq_put(&md.out[port].q, &m[i], now)) != 0)
                break;
        if (!md.desk)                           /* (a session has the machine: nothing waits) */
            i = n;
        irq_restore(f);
    }
    *taken = i;
    return r == -2 ? MIDI_E_MESSAGE : 0;
}

int midi_raw(struct app *a, u32 port, const u8 *b, u32 n)
{
    u32 f, i;
    if (port != MIDI_MPU)
        return MIDI_E_NOT_OPEN;
    f = irq_save();
    if (md.out[port].a != a) {
        irq_restore(f);
        return MIDI_E_NOT_OPEN;
    }
    if (md.tx_n + n > TX) {
        irq_restore(f);
        return MIDI_E_ROOM;
    }
    for (i = 0; i < n && md.uart; i++)
        tx_put(b[i]);
    md.tx_status = 0;
    irq_restore(f);
    return 0;
}

int midi_flush(struct app *a, u32 port)
{
    u32 f;
    if (port >= MIDI_PORTS)
        return MIDI_E_NOT_OPEN;
    f = irq_save();
    if (md.out[port].a != a) {
        irq_restore(f);
        return MIDI_E_NOT_OPEN;
    }
    midiq_clear(&md.out[port].q);
    if (port == MIDI_FM) {
        fm_silence();
    } else if (md.uart) {
        md.tx_h = md.tx_n = 0;
        tx_notes_off();
    }
    irq_restore(f);
    return 0;
}

u32 midi_queued(u32 port) { return port < MIDI_PORTS ? md.out[port].q.n : 0; }

int midi_in_next(struct app *a, struct midi_msg *m)
{
    if (md.in_app != a || !md.in_n)
        return 0;
    *m = md.in[md.in_h];
    md.in_h = (md.in_h + 1) % IN;
    md.in_n--;
    return 1;
}

void midi_tick(void)
{
    struct midi_msg m;
    struct out *o;
    u32 now, n;
    if (!md.desk || (!md.out[MIDI_FM].a && !md.uart))
        return;
    now = now_ms();
    o = &md.out[MIDI_FM];
    for (n = 0; o->a && n < TICK_MAX && midiq_due(&o->q, now, &m); n++) {
        if (now - m.time > o->late)
            o->late = now - m.time;
        o->played++;
        fm_message(m.status, m.d1, m.d2);
    }
    if (!md.uart)
        return;
    o = &md.out[MIDI_MPU];
    for (n = 0; o->a && n < TICK_MAX && md.tx_n + 3 <= TX && midiq_due(&o->q, now, &m); n++) {
        if (now - m.time > o->late)
            o->late = now - m.time;
        o->played++;
        tx_message(&m);
    }
    tx_pump(8);
    midi_mpu_drain();
    if (!o->a && !md.in_app && !md.tx_n)        /* (closed, its last bytes sent) */
        mpu_mode(0);
}

void midi_desktop(int on)
{
    u32 f = irq_save(), i;
    if (md.desk == (u8)(on != 0)) {
        irq_restore(f);
        return;
    }
    md.desk = (u8)(on != 0);
    if (on) {
        if (md.out[MIDI_FM].a)
            fm_back();
        if (md.out[MIDI_MPU].a || md.in_app)
            mpu_mode(1);
        irq_restore(f);
        return;
    }
    for (i = 0; i < MIDI_PORTS; i++)
        midiq_clear(&md.out[i].q);
    if (md.out[MIDI_FM].a)
        fm_leave();
    md.in_n = 0;
    md.tx_h = md.tx_n = 0;
    if (md.uart && md.out[MIDI_MPU].a)
        tx_notes_off();
    irq_restore(f);
    while (md.tx_n && mpu_writable(20000)) {    /* (now, not on the tick: the session's program is next. The tick
                                                   leaves these alone: the desktop is away) */
        f = irq_save();
        tx_pump(1);
        irq_restore(f);
    }
    f = irq_save();
    md.tx_h = md.tx_n = 0;
    mpu_mode(0);
    irq_restore(f);
}

void midi_app_gone(struct app *a)
{
    midi_close(a, MIDI_FM, 0);
    midi_close(a, MIDI_MPU, 0);
    midi_close(a, MIDI_MPU, 1);
}

int midi_port(u32 port, const char **name, u32 *outs, u32 *ins)
{
    if (port == MIDI_FM && md.fm) {
        *name = md.fm == 3 ? "opl3" : "opl2";
        *outs = md.out[MIDI_FM].a != 0;
        *ins = 0xFFFFFFFFu;
        return 0;
    }
    if (port == MIDI_MPU && md.mpu) {
        *name = "mpu401";
        *outs = md.out[MIDI_MPU].a != 0;
        *ins = md.in_app != 0;
        return 0;
    }
    return -1;
}

u32 midi_stat(char *buf, u32 max)
{
    u32 n = 0, i;
    if (!md.fm && !md.mpu)
        return 0;
    n = (u32)ksnprintf(buf, max, "midi");
    if (md.fm)
        n += (u32)ksnprintf(buf + n, max - n, " fm=opl%u@%x", md.fm, md.fm_port);
    if (md.mpu)
        n += (u32)ksnprintf(buf + n, max - n, " mpu=%x%s", md.mpu, md.uart ? " uart" : "");
    n += (u32)ksnprintf(buf + n, max - n, "\n");
    for (i = 0; i < MIDI_PORTS && n < max; i++)
        if (md.out[i].a)
            n += (u32)ksnprintf(buf + n, max - n, "midi out port=%s app=%u queued=%u played=%u late=%u voices=%u\n",
                                port_name(i), app_id(md.out[i].a), md.out[i].q.n, md.out[i].played, md.out[i].late,
                                i == MIDI_FM ? fm_sounding() : 0);
    if (md.in_app && n < max)
        n += (u32)ksnprintf(buf + n, max - n, "midi in port=mpu app=%u received=%u lost=%u\n", app_id(md.in_app),
                            md.in_got, md.in_lost);
    return n;
}
