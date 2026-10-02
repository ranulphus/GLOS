/* IFTEST - how does the DPMI host treat a client's interrupt flag?
 * Times a busy loop by the BIOS tick (0040:006C, advanced by IRQ 0 through
 * the BIOS) after CLI, after PUSHF/CLI/POPF (the pattern that can't restore
 * IF at IOPL 0), and after STI, and reads DPMI 0902h's virtual IF.
 * On an IOPL-3 host CLI really stops IRQ 0 and POPF restores it; an IOPL-0
 * host can see CLI and STI (they trap) but not POPF. Reports on COM1:
 *     HX-TEST if-<case> INFO ticks=<n of ~9> vif=<0|1>
 * GLOS M0 survey (docs/survey-iopl0.md). DJGPP. */
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdio.h>
#include <sys/farptr.h>

static void ser(const char *s)
{
    for (; *s; s++) {
        int n = 0;
        while (!(inportb(0x3FD) & 0x20) && ++n < 100000) ;
        outportb(0x3F8, *s);
    }
}

static unsigned long tick(void) { return _farpeekl(_dos_ds, 0x46C); }

static volatile unsigned long sink;

static void spin(unsigned long n)
{
    unsigned long i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 1000; j++)
            sink++;
}

static void report(const char *name, unsigned long ticks)
{
    char b[96];
    int vif = __dpmi_get_virtual_interrupt_state();
    sprintf(b, "HX-TEST if-%s INFO ticks=%lu vif=%d\r\n", name, ticks, vif);
    ser(b);
}

int main(void)
{
    unsigned long t0, n = 0, d;
    outportb(0x3FB, 0x80); outportb(0x3F8, 1); outportb(0x3F9, 0); outportb(0x3FB, 0x03);
    outportb(0x3FA, 0xC7); outportb(0x3FC, 0x03);
    ser("HX-START iftest\r\n");
    /* calibrate: iterations for 9 ticks (about half a second) */
    t0 = tick();
    while (tick() == t0) ;
    t0 = tick();
    while (tick() - t0 < 9) { spin(1); n++; }
    __asm__ volatile("cli");
    t0 = tick(); spin(n); d = tick() - t0;
    report("after-cli", d);
    __asm__ volatile("sti");
    __asm__ volatile("pushfl\n cli\n popfl");
    t0 = tick(); spin(n); d = tick() - t0;
    report("after-pushf-cli-popf", d);
    __asm__ volatile("sti");
    t0 = tick(); spin(n); d = tick() - t0;
    report("after-sti", d);
    ser("HX-DONE 0\r\n");
    return 0;
}
