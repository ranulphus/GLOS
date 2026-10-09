/* The system VM's trapped ports (supervisor.md §10) other than the 8042:
 *   20h/21h, A0h/A1h   the virtual PIC (vpic.c); the physical IMR follows it
 *   70h/71h            the RTC: a virtual index and NMI bit, virtual registers
 *                      A, B and C; the time and CMOS bytes from the chip
 *   92h                A20 (virtual) and the fast reset (a reset request)
 *   CF8h-CFFh          PCI mechanism 1 with a shadow address latch; CF9h's
 *                      reset bit is a reset request
 * Wider accesses to byte devices are split into bytes. Each kernel tick also
 * drives the virtual RTC's flags, the 8042's queue, INT 15h's waits and the
 * vif-stuck watchdog. */
#include "agent.h"
#include "io.h"
#include "kprintf.h"
#include "pci.h"
#include "timer.h"
#include "vm.h"

static struct { u16 base, len; } hidden[4];
static int nhidden;

/* COM1's data port, trapped: whether a program is part-way through a line,
   so the kernel's lines wait for it (lib/kprintf.c). */
static u8 com1_mid;
static u32 com1_since;

static int com1_hold(void) { return com1_mid; }

static void com1_out(u8 v)
{
    if (!(inb(0x3FB) & 0x80)) {                 /* not the divisor latch: a character */
        if (!com1_mid && v != '\n')
            com1_since = timer_ticks();
        com1_mid = v != '\n';
        klog_put((const char *)&v, 1);
    }
    outb(0x3F8, v);
    if (!com1_mid)
        kprintf_flush(0);
}

void vdev_hide(u16 base, u16 len)
{
    u32 p;
    if (nhidden == ARRAY_SIZE(hidden))
        panic("vdev-hide", NULL);
    hidden[nhidden].base = base;
    hidden[nhidden++].len = len;
    for (p = base; p < (u32)base + len; p++)
        cpu_io_trap(p, 1);
}

static int is_hidden(u16 port)
{
    int i;
    for (i = 0; i < nhidden; i++)
        if (port >= hidden[i].base && port < hidden[i].base + hidden[i].len)
            return 1;
    return 0;
}

void vdev_init(void)
{
    vm.rtc_a = timer_found_a();
    vm.rtc_b = timer_found_b();
    vm.rtc_c = 0;
    vm.rtc_index = 0x0D;
    cpu_io_trap(0x3F8, 1);
    kprintf_hold = com1_hold;
    outb(0x43, 0x34);                           /* PIT channel 0: mode 2, count FFFFh (§8.2) */
    outb(0x40, 0xFF);
    outb(0x40, 0xFF);
    timer_set_hook(vdev_tick);
}

void vdev_leave(void)
{
    kprintf_hold = NULL;
    kprintf_flush(1);
    cpu_io_trap(0x3F8, 0);
    timer_set_hook(NULL);
    timer_stop_to(vm.rtc_a, vm.rtc_b);          /* the chip as the program set it */
    outb(0x43, 0x36);                           /* PIT channel 0 as the BIOS sets it at POST: */
    outb(0x40, 0x00);                           /* mode 3, count 0 (supervisor.md §2) */
    outb(0x40, 0x00);
}

/* ---- RTC */

static u8 rtc_in(void)
{
    u8 v;
    switch (vm.rtc_index) {
    case 0x0A:
        return (u8)((rtc_read(0x0A) & 0x80) | (vm.rtc_a & 0x7F));   /* UIP from the chip */
    case 0x0B:
        return vm.rtc_b;
    case 0x0C:
        v = vm.rtc_c;
        vm.rtc_c = 0;
        return v;
    default:
        return rtc_read(vm.rtc_index);
    }
}

static void rtc_out(u8 v)
{
    switch (vm.rtc_index) {
    case 0x0A:
        vm.rtc_a = (u8)(v & 0x7F);
        vm.rtc_acc = 0;
        return;
    case 0x0B:
        vm.rtc_b = v;
        timer_write_b(v);
        return;
    case 0x0C:
    case 0x0D:
        return;
    default:
        rtc_write(vm.rtc_index, v);
    }
}

/* ---- PCI */

static u32 pci_in(u16 port, int w)
{
    if (port == 0xCF8 && w == 4)
        return vm.pci_addr;
    if (port == 0xCF9 && w == 1)
        return vm.cf9;
    if (port >= 0xCFC) {
        if (!(vm.pci_addr & 0x80000000u) || pci_owned_addr(vm.pci_addr))
            return w == 4 ? 0xFFFFFFFFu : w == 2 ? 0xFFFFu : 0xFFu;   /* nothing, or GLOS's card */
        outl(0xCF8, vm.pci_addr);
    }
    return w == 4 ? inl(port) : w == 2 ? inw(port) : inb(port);
}

static void pci_out(u16 port, u32 v, int w)
{
    if (port == 0xCF8 && w == 4) {
        vm.pci_addr = v;
        return;
    }
    if (port == 0xCF9 && w == 1) {
        vm.cf9 = (u8)(v & ~4u);
        if (v & 4)
            vm_reset_req("cf9");
        return;
    }
    if (port >= 0xCFC) {
        if (!(vm.pci_addr & 0x80000000u) || pci_owned_addr(vm.pci_addr))
            return;
        outl(0xCF8, vm.pci_addr);
    }
    if (w == 4)
        outl(port, v);
    else if (w == 2)
        outw(port, (u16)v);
    else
        outb(port, (u8)v);
}

/* ---- byte devices */

static u8 dev_inb(u16 port)
{
    u8 v;
    switch (port) {
    case 0x20: case 0x21: case 0xA0: case 0xA1:
        v = vpic_read(&vm.pic, port);
        vm_sync_mask();                         /* a poll acknowledges */
        return v;
    case 0x60: case 0x64:
        return vkbc_in(port);
    case 0x70:
        return (u8)(vm.rtc_index | vm.rtc_nmi);
    case 0x71:
        return rtc_in();
    case 0x92:
        return (u8)((inb(0x92) & ~3u) | (vm.a20 << 1));
    default:
        return is_hidden(port) ? 0xFF : inb(port);
    }
}

static void dev_outb(u16 port, u8 v)
{
    switch (port) {
    case 0x20: case 0x21: case 0xA0: case 0xA1:
        vpic_write(&vm.pic, port, v);
        vm_sync_mask();
        return;
    case 0x60: case 0x64:
        vkbc_out(port, v);
        return;
    case 0x70:
        vm.rtc_index = v & 0x7F;
        vm.rtc_nmi = v & 0x80;
        return;
    case 0x71:
        rtc_out(v);
        return;
    case 0x92:
        if (v & 1)
            vm_reset_req("port92");
        vm_set_a20((v >> 1) & 1);
        return;
    case 0x3F8:
        com1_out(v);
        return;
    default:
        if (!is_hidden(port))
            outb(port, v);
    }
}

u32 vdev_in(u16 port, int width)
{
    u32 v = 0;
    int i;
    if (port >= 0xCF8 && port <= 0xCFF)
        return pci_in(port, width);
    for (i = 0; i < width; i++)
        v |= (u32)dev_inb((u16)(port + i)) << (8 * i);
    return v;
}

void vdev_out(u16 port, u32 val, int width)
{
    int i;
    if (port >= 0xCF8 && port <= 0xCFF) {
        pci_out(port, val, width);
        return;
    }
    for (i = 0; i < width; i++)
        dev_outb((u16)(port + i), (u8)(val >> (8 * i)));
}

/* ---- every kernel tick (1024 Hz) */

/* The virtual RTC's periodic flag at the rate in its register A (at most
   once per tick), AF and UF from the chip's register C; IRQ 8 when IRQF
   rises, as the chip holds its line until C is read. */
static void rtc_tick(u8 c)
{
    u8 rs = vm.rtc_a & 0x0F, flags = c & 0x30, before = vm.rtc_c;
    if (rs) {
        vm.rtc_acc += rs <= 2 ? (rs == 1 ? 256u : 128u) : (32768u >> (rs - 1));
        if (vm.rtc_acc >= 1024) {
            vm.rtc_acc = vm.rtc_acc >= 2048 ? 0 : vm.rtc_acc - 1024;
            flags |= 0x40;
        }
    }
    if (!flags)
        return;
    vm.rtc_c |= flags;
    if ((vm.rtc_c & vm.rtc_b & 0x70) && !(before & 0x80)) {
        vm.rtc_c |= 0x80;
        vpic_raise(&vm.pic, 8);
        vm_kick();
    }
}

/* An IRQ waiting longer than 50 ms behind a cleared virtual IF. */
static void watchdog(struct trapframe *tf, u32 now)
{
    if (vm.vif || !vpic_pending(&vm.pic) || vm.direct) {     /* (direct mode: the real IF, the program's own) */
        vm.stuck_since = 0;
        vm.stuck_warned = 0;
        return;
    }
    if (!vm.stuck_since) {
        vm.stuck_since = now | 1;
        return;
    }
    if (!vm.stuck_warned && now - vm.stuck_since > 51) {
        struct trapframe *f = vm_frame();       /* the same frame as tf when the tick hit V86 code */
        (void)tf;
        vm.stuck_warned = 1;
        kprintf("GLOS-WARN vif-stuck at=%04x:%04x%s\n", f->cs, f->eip & 0xFFFF, vm.waiting ? " hlt" : "");
    }
}

void vdev_tick(struct trapframe *tf, u8 c)
{
    u32 now = timer_ticks();
    rtc_tick(c);
    vkbc_tick();
    vm_int15_tick(now);
    agent_vm_tick(now);
    if (vm.waiting && vm.wait_deadline && (s32)(now - vm.wait_deadline) >= 0)
        vm_kick();
    if (com1_mid && now - com1_since > 100) {   /* a line that never ends: write ours anyway */
        com1_mid = 0;
        kprintf_flush(1);
    }
    watchdog(tf, now);
}
