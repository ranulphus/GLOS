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
 * too; its writes are handled as always (A20 and resets stay GLOS's).
 *
 * Injected input (glos key, type and mouse; PRD §7.6) takes the keyboard's
 * and the mouse's way in: bytes into the same queue, the hotkeys watched
 * as for a real key; in a direct-mode session they wait in a queue of
 * their own and go into the chip when its output buffer is free. A mouse
 * packet has the form the program asked the mouse for: GLOS reads the
 * commands that pass to the AUX device (enable, disable, reset, and the
 * sample rates 200, 100, 80 that turn a wheel mouse's fourth byte on).
 *
 * While the desktop has the screen (M6; vkbc_desk()), the keyboard and the
 * mouse are the desktop's: every byte from either, real or injected, goes
 * to kernel/wm/desk.c and not to the queue, so DOS and the BIOS see no
 * keys meanwhile; the kill hotkey is the desktop's to carry out. A mouse no
 * DOS driver has turned on is turned on for the desktop (the AUX port, IRQ
 * 12, the mouse's reporting) and off again after, where the controller has
 * an AUX port at all: an AT keyboard controller takes the byte after D4h
 * for the keyboard's own, and F5h there turns the keyboard off. */
#include "desk.h"
#include "io.h"
#include "kprintf.h"
#include "timer.h"
#include "vm.h"

#define QSIZE 64                                /* a power of two: the indices wrap */
#define AUX   0x100
#define REPLY 0x200                             /* the keyboard's answer to a byte the program sent it */

static u16 q[QSIZE];
static u32 qh, qt;
static u8 out, out_aux, obf;                    /* the virtual output buffer */
static u8 cmd;                                  /* the program's command byte */
static u8 pending;                              /* a command waiting for its byte at 60h */
static u8 a2;                                   /* the last write was to 64h */
static u8 to_aux;                               /* D4h passed: the next byte at 60h goes to the AUX device */
static u8 mods, e0, swallow;                    /* hotkey tracking */
static u8 reinjected;                           /* direct mode: the chip holds a byte GLOS put back */
static u8 replies;                              /* bytes the program sent the keyboard, still to be answered */
static u32 reply_since;
static u32 acks_made;                           /* ACKs GLOS gave for a keyboard that didn't */
static u16 inj[QSIZE];                          /* direct mode: injected bytes waiting for the chip */
static u32 injh, injt;
static u8 aux_on, aux_wheel, aux_rate[3], aux_arg, aux_buttons;        /* the mouse, as the program set it */
static u8 desk_on;                              /* the desktop has the keyboard and the mouse */
static u8 aux_port;                             /* the controller has an AUX port */
static u8 desk_acks;                            /* the mouse's answers to the desktop's own commands, to drop */
static u8 kbd_acks;                             /* ... and the keyboard's (the desktop's key repeat, M9) */
static u8 typematic = 0x2C;                     /* [input] repeat_delay and repeat_rate as F3h's byte: 500 ms, 10.0 */

enum { M_CTRL = 1, M_ALT = 2, M_LSHIFT = 4, M_RSHIFT = 8 };

/* The last things that passed through here, for the log when the program
   sits with its interrupts off (GLOS-WARN vif-stuck: the BIOS waiting for
   the keyboard, as a rule). k: a byte from the chip (and its status); f: a
   byte put before the program; r: its read of 60h; w, c: its writes to 60h
   and 64h; C, D: GLOS's own writes to the chip's 64h and 60h (and the status
   before each); h: a hotkey's byte kept back; x: an answer given up on; p:
   the chip's status as the program polls it, when it changes. */
#define NTRACE 64
static struct { u32 t; char what; u8 a, b; } tr[NTRACE];
static u32 trn;

static void trace(char what, u8 a, u8 b)
{
    tr[trn % NTRACE].t = timer_ticks();
    tr[trn % NTRACE].what = what;
    tr[trn % NTRACE].a = a;
    tr[trn % NTRACE].b = b;
    trn++;
}

void vkbc_trace(void)
{
    u32 i = trn > NTRACE ? trn - NTRACE : 0;
    kprintf("GLOS-KBC cmd=%02x obf=%u out=%02x queued=%u replies=%u now=%u\n", cmd, obf, out, qt - qh, replies,
            timer_ticks());
    for (; i < trn; i += 8) {
        u32 k;
        kprintf("GLOS-KBC");
        for (k = i; k < i + 8 && k < trn; k++)
            kprintf(" %u:%c%02x/%02x", tr[k % NTRACE].t, tr[k % NTRACE].what, tr[k % NTRACE].a, tr[k % NTRACE].b);
        kprintf("\n");
    }
}

static void trace(char what, u8 a, u8 b);

static void wait_write(void)
{
    u32 n = 0;
    while ((inb(0x64) & 2) && ++n < 100000) ;
}

static void chip_cmd(u8 c)
{
    wait_write();
    trace('C', c, inb(0x64));
    outb(0x64, c);
}

static void chip_data(u8 d)
{
    wait_write();
    trace('D', d, inb(0x64));
    outb(0x60, d);
}

/* The chip's command byte: the program's, with IRQ 1 on and the keyboard
   enabled. Written only when it changes (a kill puts the killed program's
   byte back, as a rule the one the chip has). */
static u16 chip_byte = 0x100;                   /* what the chip was given last; 100h: not known */

static void write_cmd_byte(void)
{
    u8 v = (u8)((cmd | 0x01) & ~0x10);
    if (desk_on && aux_port)
        v = (u8)((v | 0x02) & ~0x20);           /* the desktop's mouse: IRQ 12, and its clock */
    if (v == chip_byte)
        return;
    chip_cmd(0x60);
    chip_data(v);
    chip_byte = v;
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
            if (desk_on) {                      /* the app in front, not DOS's program */
                desk_in(DESK_KILL, 0, 0, 0);
            } else {
                vm.kill_req = 1;
                vm.kill_reason = "hotkey";
                vm.kill_since = timer_ticks();
                vm_kick();
            }
            swallow = 0x81;
            return 1;
        }
        return 0;
    case 0x53:
        if (!brk && (mods & M_CTRL) && (mods & M_ALT)) {
            vm_reset_req("cad");
            if (desk_on)
                desk_in(DESK_SYSKEY, 0, 0, 0);  /* the task list: the shell's (M9) */
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
        trace('k', b, s);
        if ((s & 0x20) && desk_acks && b == 0xFA) {
            desk_acks--;                        /* (the mouse's answer to the desktop's own command) */
        } else if (!(s & 0x20) && kbd_acks && b == 0xFA) {
            kbd_acks--;                         /* (the keyboard's, to the desktop's key repeat) */
        } else if (s & 0x20) {
            if (desk_on)
                desk_in(DESK_AUX, b, aux_wheel, 0);
            else
                enqueue(b, 1);
        } else if (replies && (b == 0xFA || b == 0xFE)) {       /* its ACK or "resend": ahead of the keys */
            replies--;                                          /* that wait, as a keyboard sends it */
            if (qt - qh < QSIZE)
                q[--qh % QSIZE] = (u16)(b | REPLY);
        } else if (!hotkey(b)) {
            if (desk_on)
                desk_in(DESK_KEY, b, 0, 0);
            else
                enqueue(b, 0);
        } else {
            trace('h', b, mods);
        }
    }
}

/* The next byte into the empty output buffer: not a keyboard byte while the
   program has the keyboard disabled (it waits, as in the keyboard), and not
   a key while the keyboard owes the program an answer: the BIOS sends the
   LED command from its INT 09h and polls for the ACK with interrupts off,
   and takes whatever byte comes next for it. A key released at that moment
   (the kill hotkey's three at once) made it wait out its time-out, seconds
   long (M4e's open item; found in M5). */
static void refill(void)
{
    u16 e;
    if (obf || qh == qt)
        return;
    e = q[qh % QSIZE];
    if (!(e & AUX) && !(e & REPLY) && ((cmd & 0x10) || replies))
        return;
    qh++;
    out = (u8)e;
    out_aux = (e & AUX) != 0;
    obf = 1;
    trace('f', out, (u8)(e >> 8));
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
        if (c == 0xAA) {                        /* (the self-test resets the chip's byte) */
            chip_byte = 0x100;
            write_cmd_byte();
        }
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

/* A byte on its way to the AUX device: what injected packets must look like. */
static void aux_seen(u8 v)
{
    if (aux_arg == 1) {                         /* a sample rate: 200, 100, 80 in a row is the wheel's knock */
        aux_rate[0] = aux_rate[1];
        aux_rate[1] = aux_rate[2];
        aux_rate[2] = v;
        if (aux_rate[0] == 200 && aux_rate[1] == 100 && aux_rate[2] == 80)
            aux_wheel = 1;
    }
    if (aux_arg) {
        aux_arg = 0;
        return;
    }
    switch (v) {
    case 0xF3: aux_arg = 1; break;
    case 0xE8: aux_arg = 2; break;              /* (the resolution: a byte follows) */
    case 0xF4: aux_on = 1; break;
    case 0xF5: aux_on = 0; break;
    case 0xF6: aux_on = 0; break;
    case 0xFF: aux_on = aux_wheel = 0; aux_rate[2] = 0; break;
    }
}

u8 vkbc_in(u16 port)
{
    if (vm.direct)
        return inb(port);                       /* (the chip holds what the program reads) */
    drain();
    if (port == 0x60) {
        trace('r', out, obf);
        obf = 0;                                /* the next byte comes on the next tick */
        return out;
    }
    {
        static u8 last;
        u8 st = inb(0x64);
        if (st != last)
            trace('p', st, obf);
        last = st;
        return (u8)((st & 0x14) | obf | (obf && out_aux ? 0x20 : 0) | (a2 ? 0x08 : 0));
    }
}

void vkbc_out(u16 port, u8 v)
{
    u8 p = pending;
    if (port == 0x64) {
        trace('c', v, cmd);
        a2 = 1;
        pending = 0;
        command(v);
        return;
    }
    trace('w', v, p);
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
            if (replies < 4)                    /* the ACK; 86Box's kbc_at does the same), and the answer */
                replies++;                      /* comes before any key (refill) */
            reply_since = timer_ticks();
        } else {
            aux_seen(v);
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

/* Direct mode: the next injected byte into the chip, once the program has
   read what the chip holds. */
static void inject_turn(void)
{
    u16 e;
    if (injh == injt || (inb(0x64) & 1))
        return;
    e = inj[injh++ % QSIZE];
    chip_cmd(0xAD);
    if (!(inb(0x64) & 1))                       /* (no real byte got in ahead) */
        direct_put((u8)e, (e & AUX) != 0);
    else
        injh--;
    chip_cmd(0xAE);
}

/* A byte as from the keyboard (set 1) or the AUX device: 0, or -1 when
   there is no room. Interrupts off. */
static int inject(u8 b, int aux)
{
    if (!aux && hotkey(b))
        return 0;
    if (desk_on) {
        desk_in(aux ? DESK_AUX : DESK_KEY, b, aux_wheel, 0);
        return 0;
    }
    if (vm.direct) {
        if (injt - injh >= QSIZE)
            return -1;
        inj[injt++ % QSIZE] = (u16)(b | (aux ? AUX : 0));
        inject_turn();
        return 0;
    }
    if (qt - qh >= QSIZE)
        return -1;
    enqueue(b, aux);
    refill();
    return 0;
}

int vkbc_room(void) { return (int)(QSIZE - (vm.direct ? injt - injh : qt - qh)); }

/* A key down (1) or up (0): a set 1 code, 100h and above with the E0 prefix
   (the numbers 86Box's monitor takes). */
int vkbc_key(u32 code, int down)
{
    u32 f = irq_save();
    int r = -1;
    if (vkbc_room() >= 2) {
        if (code & 0x100)
            inject(0xE0, 0);
        r = inject((u8)((code & 0x7F) | (down ? 0 : 0x80)), 0);
    }
    irq_restore(f);
    return r;
}

/* The mouse moved by dx, dy (down and right positive, as on the screen),
   its buttons now `buttons` (bit 0 left, 1 right, 2 middle; -1: as they
   were): 0; -1 without room; -2 when no program has the mouse enabled. */
int vkbc_mouse(int dx, int dy, int buttons)
{
    u32 f = irq_save();
    int r = 0;
    if (buttons >= 0)
        aux_buttons = (u8)(buttons & 7);
    if (desk_on) {                              /* (no packets: the desktop takes it as it is) */
        desk_in(DESK_MOUSE, dx, dy, aux_buttons);
    } else if (!aux_on || (cmd & 0x20)) {
        r = -2;
    } else {
        do {                                    /* a packet carries 255 at most each way */
            int x = dx > 255 ? 255 : dx < -255 ? -255 : dx, y = dy > 255 ? 255 : dy < -255 ? -255 : dy;
            if (vkbc_room() < 4) {
                r = -1;
                break;
            }
            dx -= x;
            dy -= y;
            y = -y;                             /* the mouse counts up as positive */
            inject((u8)(0x08 | aux_buttons | (x < 0 ? 0x10 : 0) | (y < 0 ? 0x20 : 0)), 1);
            inject((u8)x, 1);
            inject((u8)y, 1);
            if (aux_wheel)
                inject(0, 1);
        } while (dx || dy);
    }
    irq_restore(f);
    return r;
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
    if (vm.direct) {                            /* (a byte is the program's: no stealing it) */
        inject_turn();
        return;
    }
    /* A keyboard has 20 ms to answer a command. When it hasn't, the program
       gets the ACK from here: the BIOS polls for it with interrupts off and
       would wait out its own time-out, seconds long, the clock standing.
       86Box's keyboard does this now and then when a key is on its way as
       the command comes (the kill hotkey's Esc, then the BIOS's LED update:
       one sess-kill run in 20 on a busy host). A real ACK that still comes
       is then one more byte for INT 09h, which takes it for what it is. */
    if (replies && timer_ticks() - reply_since > 20) {
        trace('x', replies, 0);
        replies--;
        reply_since = timer_ticks();
        if (qt - qh < QSIZE)
            q[--qh % QSIZE] = (u16)(0xFA | REPLY);
        kprintf("GLOS-KBC ack=made-up n=%u\n", ++acks_made);       /* (the keyboard gave none in 20 ms) */
    }
    drain();
    refill();
}

/* A direct-mode session begins (1) or ends (0): the byte the program would
   have read next goes into the chip; at the end what the chip still holds
   goes into the queue. */
/* The desktop takes the keyboard and the mouse (1), or gives them back (0:
   a session has the screen, or there is no desktop). Interrupts off. */
/* The keyboard's repeat (8042 command F3h) while the desktop has it: [input] repeat_delay (ms) and
   repeat_rate (a second) (M9). Given again whenever the desktop takes the keyboard back. */
static void send_typematic(void)
{
    if (!desk_on)
        return;
    chip_data(0xF3);
    chip_data(typematic);
    kbd_acks += 2;
}

void vkbc_typematic(u32 delay_ms, u32 rate)
{
    static const u8 cps10[32] = { 255, 255, 240, 218, 200, 185, 171, 160, 150, 133, 120, 109, 100, 92, 86, 80,
                                  75, 67, 60, 55, 50, 46, 43, 40, 37, 33, 30, 27, 25, 23, 21, 20 };
    u32 i, best = 0, bd = 1000, want = rate * 10;
    for (i = 0; i < 32; i++) {                  /* the nearest of the keyboard's rates (30.0 to 2.0 a second) */
        u32 have = i == 0 ? 300 : i == 1 ? 267 : cps10[i], d = have > want ? have - want : want - have;
        if (d < bd)
            bd = d, best = i;
    }
    typematic = (u8)(((delay_ms >= 1000 ? 3 : delay_ms >= 750 ? 2 : delay_ms >= 500 ? 1 : 0) << 5) | best);
    kprintf("GLOS-KBC typematic=%02x delay=%u rate=%u\n", typematic, delay_ms, rate);
    send_typematic();
}

void vkbc_desk(int on)
{
    if ((on != 0) == desk_on)
        return;
    if (on) {
        desk_on = 1;
        send_typematic();
        if (aux_port && !aux_on) {              /* a mouse nobody has turned on: for the desktop */
            chip_cmd(0xA8);
            write_cmd_byte();
            chip_cmd(0xD4);
            chip_data(0xF4);
            desk_acks++;
        } else {
            write_cmd_byte();
        }
    } else {
        if (aux_port && !aux_on) {
            chip_cmd(0xD4);
            chip_data(0xF5);
            desk_acks++;
        }
        desk_on = 0;
        write_cmd_byte();
        if (aux_port && !aux_on && !(cmd & 0x02) && (cmd & 0x20))
            chip_cmd(0xA7);                     /* (the port off again, as the program's byte has it) */
        mods = e0 = 0;                          /* (keys held across the change mean nothing to the other side) */
    }
    desk_in(DESK_RESET, 0, 0, 0);
}

void vkbc_direct(int on)
{
    if (on) {
        if (obf)
            direct_put(out, out_aux);
        obf = 0;
    } else {
        reinjected = 0;
        injh = injt = 0;
        drain();
        refill();
    }
}

/* The next byte from the AUX device, polled (GLOS's start: interrupts are
   off); -1 if none comes. A keyboard byte meanwhile goes into the queue. */
static int aux_byte(void)
{
    u32 n;
    for (n = 0; n < 100000; n++) {
        u8 s = inb(0x64), b;
        if (!(s & 1))
            continue;
        b = inb(0x60);
        if (s & 0x20)
            return b;
        if (!hotkey(b))
            enqueue(b, 0);
    }
    return -1;
}

void vkbc_init(void)
{
    u32 n = 0;
    drain();
    chip_cmd(0x20);
    while (!(inb(0x64) & 1) && ++n < 200000) ;
    cmd = (inb(0x64) & 1) ? inb(0x60) : 0x45;
    write_cmd_byte();
    /* A mouse driver loaded before GLOS has set the mouse up already: its
       port and IRQ 12 are on in the command byte. Injected packets need to
       know whether it is a wheel mouse by now (its ID is 3 or 4 then). */
    aux_on = (cmd & 0x22) == 0x02;
    aux_port = aux_on;
    if (!aux_on) {                              /* is there an AUX port? Its clock's bit follows A7h and A8h then */
        u8 off, on;
        chip_cmd(0xA7);
        chip_cmd(0x20);
        for (n = 0; !(inb(0x64) & 1) && ++n < 200000;) ;
        off = (inb(0x64) & 1) ? inb(0x60) : 0;
        chip_cmd(0xA8);
        chip_cmd(0x20);
        for (n = 0; !(inb(0x64) & 1) && ++n < 200000;) ;
        on = (inb(0x64) & 1) ? inb(0x60) : 0x20;
        aux_port = (off & 0x20) && !(on & 0x20);
        if (cmd & 0x20)
            chip_cmd(0xA7);                     /* off, as it was */
        chip_byte = 0x100;                      /* (whatever those did to the byte: written again below) */
        write_cmd_byte();
    }
    if (aux_on) {
        chip_cmd(0xD4);
        chip_data(0xF2);
        if (aux_byte() == 0xFA)
            aux_wheel = aux_byte() >= 3;
        /* ... and whether the mouse reports at all: a driver that was
           unloaded again (CuteMouse's /U) leaves the port and IRQ 12 on and
           the mouse quiet. Its status says (E9h: bit 5 of the first byte). */
        chip_cmd(0xD4);
        chip_data(0xE9);
        if (aux_byte() == 0xFA) {
            int st = aux_byte();
            aux_byte();
            aux_byte();
            if (st >= 0)
                aux_on = (st & 0x20) != 0;
        }
    }
}

u8 vkbc_cmd(void) { return cmd; }

/* After a kill: a byte the killed program never read (its handler took the
   IRQ without reading 60h, as PICREMAP's does) is raised again for the
   handler that is back in charge; otherwise nothing more would come. */
void vkbc_restore(u8 c)
{
    cmd = c;
    pending = 0;
    replies = 0;
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
    /* The mouse's answer to the desktop's last word to it (stop reporting): taken here, not left for real mode
       to find in the chip. */
    while (desk_acks && aux_byte() >= 0)
        desk_acks--;
    desk_acks = 0;
    drain();
    qh = qt;
    obf = 0;
    chip_cmd(0x60);
    chip_data(cmd);
    drain();
    qh = qt;
}
