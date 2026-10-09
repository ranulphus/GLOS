/* The system VM's 8042 (supervisor.md §10). The kernel reads every byte the
 * controller receives (IRQ 1, IRQ 12, each tick and each status read) into
 * one queue, keyboard and AUX bytes in order, and presents them to the
 * program one at a time through a virtual output buffer: the next byte
 * arrives a tick after the last was read, as on the real part, where a
 * second read of port 60h returns the same byte.
 *
 * Kept from the program: Ctrl-Alt-Del (a reset request) and Ctrl-Alt-Shift-
 * Esc (the kill), A20 and reset through the output port (D1h, DDh/DFh,
 * pulses), and anything that would stop the kernel hearing the keyboard:
 * the command byte's IRQ 1 enable and keyboard clock stay on in the chip,
 * whatever the program's own copy says. Other commands go to the chip;
 * those that answer are answered at once.
 *
 * In a direct-mode session (supervisor.md §9.7) the program's protected-
 * mode code reads the chip itself, so its bytes can't wait in a virtual
 * buffer: GLOS still reads each byte first (the kill hotkey) and puts it
 * back into the chip's output buffer (D2h, or D3h for AUX), and the IRQ the
 * chip raises for it is the program's. Reads from V86 code go to the chip
 * too; its writes are handled as always (A20 and resets stay GLOS's). */
#include "io.h"
#include "kprintf.h"
#include "timer.h"
#include "vm.h"

#define QSIZE 64                                /* a power of two: the indices wrap */
#define AUX   0x100

static u16 q[QSIZE];
static u32 qh, qt;
static u8 out, out_aux, obf;                    /* the virtual output buffer */
static u8 cmd;                                  /* the program's command byte */
static u8 pending;                              /* a command waiting for its byte at 60h */
static u8 a2;                                   /* the last write was to 64h */
static u8 to_aux;                               /* D4h passed: the next byte at 60h goes to the AUX device */
static u8 mods, e0, swallow;                    /* hotkey tracking */
static u8 reinjected;                           /* direct mode: the chip holds a byte GLOS put back */

enum { M_CTRL = 1, M_ALT = 2, M_LSHIFT = 4, M_RSHIFT = 8 };

static void wait_write(void)
{
    u32 n = 0;
    while ((inb(0x64) & 2) && ++n < 100000) ;
}

static void chip_cmd(u8 c)
{
    wait_write();
    outb(0x64, c);
}

static void chip_data(u8 d)
{
    wait_write();
    outb(0x60, d);
}

static void write_cmd_byte(void)
{
    chip_cmd(0x60);
    chip_data((u8)((cmd | 0x01) & ~0x10));      /* IRQ 1 on, keyboard enabled */
}

static void raise(int aux)
{
    if (aux ? (cmd & 0x02) : (cmd & 0x01)) {
        vpic_raise(&vm.pic, aux ? 12 : 1);
        vm_kick();
    }
}

/* 1: the byte is the kernel's (a hotkey). Set 1 codes, as translated. */
static int hotkey(u8 b)
{
    int ext = e0, brk = b & 0x80;
    u8 code = b & 0x7F;

    if (b == 0xE0) {
        e0 = 1;
        return 0;
    }
    e0 = 0;
    if (swallow && b == swallow) {
        swallow = 0;
        return 1;
    }
    switch (code) {
    case 0x1D: mods = (u8)(brk ? mods & ~M_CTRL : mods | M_CTRL); return 0;
    case 0x38: mods = (u8)(brk ? mods & ~M_ALT : mods | M_ALT); return 0;
    case 0x2A: if (!ext) mods = (u8)(brk ? mods & ~M_LSHIFT : mods | M_LSHIFT); return 0;
    case 0x36: if (!ext) mods = (u8)(brk ? mods & ~M_RSHIFT : mods | M_RSHIFT); return 0;
    case 0x01:
        if (!brk && (mods & M_CTRL) && (mods & M_ALT) && (mods & (M_LSHIFT | M_RSHIFT))) {
            vm.kill_req = 1;
            vm.kill_reason = "hotkey";
            vm.kill_since = timer_ticks();
            vm_kick();
            swallow = 0x81;
            return 1;
        }
        return 0;
    case 0x53:
        if (!brk && (mods & M_CTRL) && (mods & M_ALT)) {
            vm_reset_req("cad");
            if (ext && qt != qh && q[(qt - 1) % QSIZE] == 0xE0)
                qt--;                           /* the E0 before it */
            swallow = 0xD3;
            return 1;
        }
        return 0;
    }
    return 0;
}

static void enqueue(u8 b, int aux)
{
    if (qt - qh < QSIZE)
        q[qt++ % QSIZE] = (u16)(b | (aux ? AUX : 0));
}

/* Everything the chip holds, into the queue. */
static void drain(void)
{
    u32 n = 0;
    u8 s, b;
    while (((s = inb(0x64)) & 1) && ++n < 32) {
        b = inb(0x60);
        if (s & 0x20)
            enqueue(b, 1);
        else if (!hotkey(b))
            enqueue(b, 0);
    }
}

/* The next byte into the empty output buffer: not a keyboard byte while the
   program has the keyboard disabled (it waits, as in the keyboard). */
static void refill(void)
{
    u16 e;
    if (obf || qh == qt)
        return;
    e = q[qh % QSIZE];
    if (!(e & AUX) && (cmd & 0x10))
        return;
    qh++;
    out = (u8)e;
    out_aux = (e & AUX) != 0;
    obf = 1;
    raise(out_aux);
}

/* Direct mode: a byte into the chip's output buffer for the program (D2h,
   or D3h for AUX), as if the device had sent it; the chip raises its IRQ.
   Back once the chip holds it, so that nothing gets in ahead of it. */
static void direct_put(u8 b, int aux)
{
    u32 n = 0;
    wait_write();
    outb(0x64, aux ? 0xD3 : 0xD2);
    wait_write();
    outb(0x60, b);
    reinjected = 1;
    while (!(inb(0x64) & 1) && ++n < 100000) ;
}

/* A controller's answer goes ahead of anything queued. */
static void respond(u8 b, int aux)
{
    if (vm.direct) {
        direct_put(b, aux);
        return;
    }
    if (obf) {
        qh--;
        q[qh % QSIZE] = (u16)(out | (out_aux ? AUX : 0));
    }
    out = b;
    out_aux = (u8)aux;
    obf = 1;
    raise(aux);
}

static void chip_answer(u8 c)
{
    u32 n = 0;
    drain();
    chip_cmd(c);
    while (!(inb(0x64) & 1))
        if (++n > 200000)
            return;
    respond(inb(0x60), 0);
}

static void command(u8 c)
{
    to_aux = 0;
    switch (c) {
    case 0x20:
        respond(cmd, 0);
        return;
    case 0x60: case 0xD1: case 0xD2: case 0xD3:
        pending = c;
        return;
    case 0xAD:
        cmd |= 0x10;
        return;
    case 0xAE:
        cmd &= (u8)~0x10;
        refill();
        return;
    case 0xA7:
        cmd |= 0x20;
        break;
    case 0xA8:
        cmd &= (u8)~0x20;
        break;
    case 0xD0:
        respond((u8)(0xDD | (vm.a20 << 1)), 0);
        return;
    case 0xDD:
        vm_set_a20(0);
        return;
    case 0xDF:
        vm_set_a20(1);
        return;
    case 0xA9: case 0xAA: case 0xAB: case 0xC0: case 0xE0:
        chip_answer(c);
        if (c == 0xAA)
            write_cmd_byte();
        return;
    default:
        if (c >= 0xF0) {                        /* pulse output lines: bit 0 is reset */
            if (!(c & 1))
                vm_reset_req("kbc");
            return;
        }
    }
    to_aux = c == 0xD4;
    chip_cmd(c);
}

u8 vkbc_in(u16 port)
{
    if (vm.direct)
        return inb(port);                       /* (the chip holds what the program reads) */
    drain();
    if (port == 0x60) {
        obf = 0;                                /* the next byte comes on the next tick */
        return out;
    }
    return (u8)((inb(0x64) & 0x14) | obf | (obf && out_aux ? 0x20 : 0) | (a2 ? 0x08 : 0));
}

void vkbc_out(u16 port, u8 v)
{
    u8 p = pending;
    if (port == 0x64) {
        a2 = 1;
        pending = 0;
        command(v);
        return;
    }
    a2 = 0;
    pending = 0;
    switch (p) {
    case 0x60:
        cmd = v;
        write_cmd_byte();
        refill();
        return;
    case 0xD1:
        vm_set_a20((v >> 1) & 1);
        if (!(v & 1))
            vm_reset_req("kbc");
        return;
    case 0xD2:
    case 0xD3:
        respond(v, p == 0xD3);
        return;
    default:
        if (!to_aux) {                          /* to the keyboard: the 8042 enables its interface (the BIOS */
            cmd &= (u8)~0x10;                   /* sends LED commands between ADh and AEh and waits for */
            refill();                           /* the ACK; 86Box's kbc_at does the same) */
        }
        to_aux = 0;
        chip_data(v);                           /* to the keyboard, or a passed command's byte */
    }
}

/* Direct mode's IRQ 1 and 12: a byte GLOS put back is the program's; any
   other is read (the hotkey) and put back. */
static void direct_irq(void)
{
    u8 s = inb(0x64), b;
    if (!(s & 1)) {                             /* (the program read it before the IRQ came) */
        reinjected = 0;
        return;
    }
    if (reinjected) {
        reinjected = 0;
        vpic_raise(&vm.pic, (s & 0x20) ? 12 : 1);
        vm_kick();
        return;
    }
    chip_cmd(0xAD);                             /* the keyboard held off: its next byte can't get in between */
    b = inb(0x60);
    if ((s & 0x20) || !hotkey(b))
        direct_put(b, (s & 0x20) != 0);
    chip_cmd(0xAE);                             /* (and while the byte waits in the chip, the chip holds it off) */
}

void vkbc_irq(struct trapframe *tf)
{
    (void)tf;
    if (vm.direct) {
        direct_irq();
        return;
    }
    drain();
    refill();
}

void vkbc_tick(void)
{
    if (vm.direct)                              /* (a byte is the program's: no stealing it) */
        return;
    drain();
    refill();
}

/* A direct-mode session begins (1) or ends (0): the byte the program would
   have read next goes into the chip; at the end what the chip still holds
   goes into the queue. */
void vkbc_direct(int on)
{
    if (on) {
        if (obf)
            direct_put(out, out_aux);
        obf = 0;
    } else {
        reinjected = 0;
        drain();
        refill();
    }
}

void vkbc_init(void)
{
    u32 n = 0;
    drain();
    chip_cmd(0x20);
    while (!(inb(0x64) & 1) && ++n < 200000) ;
    cmd = (inb(0x64) & 1) ? inb(0x60) : 0x45;
    write_cmd_byte();
}

u8 vkbc_cmd(void) { return cmd; }

/* After a kill: a byte the killed program never read (its handler took the
   IRQ without reading 60h, as PICREMAP's does) is raised again for the
   handler that is back in charge; otherwise nothing more would come. */
void vkbc_restore(u8 c)
{
    cmd = c;
    pending = 0;
    write_cmd_byte();
    if (obf)
        raise(out_aux);
    refill();
}

/* Back to real mode: bytes still queued are dropped, and the chip is left
   empty. A byte left in it would hold IRQ 1 high, and the PIC, initialised
   again, waits for a rising edge that would never come. */
void vkbc_leave(void)
{
    drain();
    qh = qt;
    obf = 0;
    chip_cmd(0x60);
    chip_data(cmd);
    drain();
    qh = qt;
}
