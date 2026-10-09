/* DIRTEST (GLOS M4e): a program at IOPL 3 (a direct-mode session,
 * supervisor.md §9.7) and what GLOS takes back from it. DJGPP. On COM1:
 *     HX-DIRECT iopl=N
 *         EFLAGS' IOPL as the program runs (3 in direct mode, 0 otherwise);
 *     HX-DIRECT irq0 n=N
 *         IRQ 0 through its own handler, which EOIs the PIC itself and
 *         doesn't chain: interrupts counted up to 10 (more than one: GLOS's
 *         virtual PIC doesn't wait for an EOI it can't see);
 *     HX-DIRECT imr irq5=N irq1=N
 *         IRQ 5's and IRQ 1's bits at port 21h after the program masks both
 *         there and makes a DOS call (direct mode: 1 0, GLOS keeps the
 *         keyboard's line);
 *     HX-DIRECT rtc a=NN
 *         RTC register A after the program sets 2Fh (2 Hz) there and makes
 *         32 DOS calls (direct mode: GLOS's 26h again);
 *     HX-DIRECT armed-key, then HX-DIRECT keys=XXXX
 *         the bytes its own IRQ 1 handler, which reads the 8042 itself as
 *         GTA's does (status, then data), gets for a key the harness taps
 *         (Enter: 1c9c; GLOS reads each byte first, for the kill hotkey,
 *         and in direct mode puts it back into the chip). */
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

static volatile int n0, nk;
static volatile unsigned char keys[4];

static void irq1(void)
{
    if ((inportb(0x64) & 1) && nk < 4)
        keys[nk++] = inportb(0x60);
    outportb(0x20, 0x20);
}
static void irq1_end(void) { }

static void irq0(void)
{
    n0++;
    outportb(0x20, 0x20);
}
static void irq0_end(void) { }

static void dos_call(void)
{
    __dpmi_regs r;
    r.x.ax = 0x3000;
    __dpmi_int(0x21, &r);
}

int main(void)
{
    _go32_dpmi_seginfo old, info;
    volatile unsigned long i;
    unsigned fl, imr0, imr, a0, a;
    char line[96];

    __asm__ volatile("pushfl; popl %0" : "=r"(fl));
    snprintf(line, sizeof line, "HX-DIRECT iopl=%u\r\n", (fl >> 12) & 3);
    ser(line);

    _go32_dpmi_lock_code(irq0, (char *)irq0_end - (char *)irq0);
    _go32_dpmi_lock_data((void *)&n0, sizeof n0);
    _go32_dpmi_get_protected_mode_interrupt_vector(8, &old);
    info.pm_offset = (unsigned long)irq0;
    info.pm_selector = _go32_my_cs();
    _go32_dpmi_allocate_iret_wrapper(&info);
    _go32_dpmi_set_protected_mode_interrupt_vector(8, &info);
    for (i = 0; i < 100000000ul && n0 < 10; i++) ;
    _go32_dpmi_set_protected_mode_interrupt_vector(8, &old);
    _go32_dpmi_free_iret_wrapper(&info);
    snprintf(line, sizeof line, "HX-DIRECT irq0 n=%d\r\n", n0);
    ser(line);

    imr0 = inportb(0x21);
    outportb(0x21, imr0 | 0x22);
    dos_call();
    imr = inportb(0x21);
    outportb(0x21, imr0);
    dos_call();
    snprintf(line, sizeof line, "HX-DIRECT imr irq5=%u irq1=%u\r\n", (imr >> 5) & 1, (imr >> 1) & 1);
    ser(line);

    __asm__ volatile("cli");
    outportb(0x70, 0x0A);
    a0 = inportb(0x71);
    outportb(0x70, 0x0A);
    outportb(0x71, 0x2F);
    __asm__ volatile("sti");
    for (i = 0; i < 32; i++)
        dos_call();
    __asm__ volatile("cli");
    outportb(0x70, 0x0A);
    a = inportb(0x71);
    outportb(0x70, 0x0A);
    outportb(0x71, a0 & 0x7F);
    __asm__ volatile("sti");
    snprintf(line, sizeof line, "HX-DIRECT rtc a=%02x\r\n", a & 0x7F);
    ser(line);

    _go32_dpmi_lock_code(irq1, (char *)irq1_end - (char *)irq1);
    _go32_dpmi_lock_data((void *)&nk, sizeof nk);
    _go32_dpmi_lock_data((void *)keys, sizeof keys);
    _go32_dpmi_get_protected_mode_interrupt_vector(9, &old);
    info.pm_offset = (unsigned long)irq1;
    info.pm_selector = _go32_my_cs();
    _go32_dpmi_allocate_iret_wrapper(&info);
    _go32_dpmi_set_protected_mode_interrupt_vector(9, &info);
    ser("HX-DIRECT armed-key\r\n");
    {
        unsigned long t0 = _farpeekl(_dos_ds, 0x46C);
        while (nk < 2 && _farpeekl(_dos_ds, 0x46C) - t0 < 18 * 20) ;
    }
    _go32_dpmi_set_protected_mode_interrupt_vector(9, &old);
    _go32_dpmi_free_iret_wrapper(&info);
    snprintf(line, sizeof line, "HX-DIRECT keys=%02x%02x\r\n", nk > 0 ? keys[0] : 0, nk > 1 ? keys[1] : 0);
    ser(line);
    return 0;
}
