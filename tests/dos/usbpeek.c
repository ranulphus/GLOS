/* USBPEEK (docs/gamepad.md J0): every UHCI USB controller's state as the BIOS left it, read-only. For each PCI
 * function of class 0C 03 00, its configuration (the command register, the interrupt line, the legacy support
 * register at C0h on Intel's) and its I/O registers (Intel's UHCI design guide rev 1.1: USBCMD 00h, USBSTS 02h,
 * USBINTR 04h, FRNUM 06h, FRBASEADD 08h, SOFMOD 0Ch, PORTSC1 10h, PORTSC2 12h). Nothing is written to the
 * controller. Under GLOS, the legacy support register's PIRQDEN (bit 13) is GLOS's: its quiet driver clears it.
 *     USBPEEK
 *     HX-USBPEEK pci=BB:DD.F id=VVVV:DDDD cmd=XXXX irq=N legsup=XXXX io=XXXX usbcmd=XXXX usbsts=XXXX
 *                usbintr=XXXX frnum=XXXX frbase=XXXXXXXX sof=XX port1=XXXX port2=XXXX     (one line a controller)
 *     HX-USBPEEK found=N
 * Then a line a controller saying what the bits mean: whether it runs (USBCMD.RS), whether the BIOS's legacy
 * support has it (SMI enabled, 60h/64h trapped) and what each root port has (connected, enabled, low speed).
 * DJGPP. */
#include <dpmi.h>
#include <pc.h>
#include <stdio.h>
#include <string.h>

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inportb(0x3FD) & 0x20) && ++n < 60000u) ;
        outportb(0x3F8, *s);
    }
}

static void say(const char *line)
{
    ser(line);
    fputs(line, stdout);
}

/* A configuration dword by the PCI BIOS (INT 1Ah B10Ah). */
static unsigned long cfg(int bus, int dev, int fn, int reg)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB10A;
    r.h.bh = bus;
    r.h.bl = dev << 3 | fn;
    r.x.di = reg;
    __dpmi_int(0x1A, &r);
    return r.h.ah ? 0xFFFFFFFFUL : r.d.ecx;
}

static void port(char *out, int n, unsigned v)
{
    sprintf(out, "port%d=%s%s%s%s", n, v & 0x0001 ? "connected" : "empty", v & 0x0004 ? ",enabled" : "",
            v & 0x0100 ? ",low-speed" : "", v & 0x0200 ? ",reset" : "");
}

int main(void)
{
    char line[300], p1[60], p2[60];
    int bus, dev, fn, found = 0, last;
    __dpmi_regs r;

    memset(&r, 0, sizeof r);
    r.x.ax = 0xB101;                            /* the PCI BIOS: present, and its last bus */
    __dpmi_int(0x1A, &r);
    last = r.h.ah == 0 && r.d.edx == 0x20494350UL ? r.h.cl : 0;
    for (bus = 0; bus <= last; bus++)
        for (dev = 0; dev < 32; dev++)
            for (fn = 0; fn < 8; fn++) {
                unsigned long id = cfg(bus, dev, fn, 0), cls, cmd, irq, bar4, leg = 0;
                unsigned io, usbcmd, usbsts, usbintr, frnum, sof, ps1, ps2;
                unsigned long frbase;
                if ((id & 0xFFFF) == 0xFFFF) {
                    if (fn == 0)
                        break;
                    continue;
                }
                cls = cfg(bus, dev, fn, 8) >> 8;
                if (fn == 0 && !(cfg(bus, dev, fn, 0x0C) & 0x00800000UL))
                    fn = 8;                     /* (one function only: the loop ends after this one) */
                if (cls != 0x0C0300UL)
                    continue;
                found++;
                cmd = cfg(bus, dev, fn & 7, 4) & 0xFFFF;
                irq = cfg(bus, dev, fn & 7, 0x3C) & 0xFF;
                bar4 = cfg(bus, dev, fn & 7, 0x20);
                if ((id & 0xFFFF) == 0x8086)
                    leg = cfg(bus, dev, fn & 7, 0xC0) & 0xFFFF;
                io = (bar4 & 1) ? (unsigned)(bar4 & 0xFFE0) : 0;
                usbcmd = io ? inportw(io) : 0;
                usbsts = io ? inportw(io + 2) : 0;
                usbintr = io ? inportw(io + 4) : 0;
                frnum = io ? inportw(io + 6) : 0;
                frbase = io ? inportl(io + 8) : 0;
                sof = io ? inportb(io + 0x0C) : 0;
                ps1 = io ? inportw(io + 0x10) : 0;
                ps2 = io ? inportw(io + 0x12) : 0;
                sprintf(line, "HX-USBPEEK pci=%02X:%02X.%d id=%04lX:%04lX cmd=%04lX irq=%lu legsup=%04lX io=%04X "
                        "usbcmd=%04X usbsts=%04X usbintr=%04X frnum=%04X frbase=%08lX sof=%02X port1=%04X "
                        "port2=%04X\r\n", bus, dev, fn & 7, id & 0xFFFF, id >> 16, cmd, irq, leg, io, usbcmd,
                        usbsts, usbintr, frnum, frbase, sof, ps1, ps2);
                say(line);
                port(p1, 1, ps1);
                port(p2, 2, ps2);
                sprintf(line, "  means: %s; legacy SMI %s, 60h/64h traps %s%s; %s %s\r\n",
                        usbcmd & 1 ? "running (RS)" : "stopped",
                        (id & 0xFFFF) != 0x8086 ? "n/a" : leg & 0x0010 ? "on" : "off",
                        (id & 0xFFFF) != 0x8086 ? "n/a" : leg & 0x000F ? "on" : "off",
                        (id & 0xFFFF) == 0x8086 && (leg & 0x2000) ? ", PIRQDEN on" : "", p1, p2);
                say(line);
            }
    sprintf(line, "HX-USBPEEK found=%d\r\n", found);
    say(line);
    return found ? 0 : 1;
}
