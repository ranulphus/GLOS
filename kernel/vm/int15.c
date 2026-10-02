/* INT 15h for the system VM (supervisor.md §8.3, §16). Emulated before the
 * IVT, so neither the BIOS nor an XMS driver's hook sees these:
 *   24xxh   A20 (virtual)                  86h   wait CX:DX µs on the kernel clock
 *   83h     event wait (flag byte bit 7)   87h   block move, never into GLOS's memory
 *   88h, E801h   no extended memory        E820h, E881h   unsupported (CF)
 *   89h     protected mode: refused
 * Everything else goes to the BIOS. The BIOSes' own 86h would program the
 * RTC (V86TEST case R), which is the kernel's. */
#include "kprintf.h"
#include "timer.h"
#include "vm.h"

static int done(struct trapframe *tf, u32 ret_ip, int cf)
{
    if (cf)
        tf->eflags |= FL_CF;
    else
        tf->eflags &= ~FL_CF;
    tf->eip = ret_ip;
    return 1;
}

/* Microseconds to kernel ticks (1024 Hz), rounded up, in 32-bit arithmetic. */
static u32 us_ticks(u32 us)
{
    return (us / 1000000u) * 1024u + ((us % 1000000u) * 1024u + 999999u) / 1000000u;
}

static u32 desc_base(u32 d)
{
    return vm_rd8(d + 2) | ((u32)vm_rd8(d + 3) << 8) | ((u32)vm_rd8(d + 4) << 16) | ((u32)vm_rd8(d + 7) << 24);
}

int vm_int15(struct trapframe *tf, u32 ret_ip)
{
    u8 ah = (u8)(tf->eax >> 8), al = (u8)tf->eax;
    u32 now = timer_ticks(), us, key, gdt, n, src, dst;

    switch (ah) {
    case 0x24:
        switch (al) {
        case 0x00: vm_set_a20(0); break;
        case 0x01: vm_set_a20(1); break;
        case 0x02: SETLO(tf->eax, vm.a20); break;
        case 0x03: SET16(tf->ebx, 3); break;    /* the 8042 and port 92h */
        default:
            SETHI(tf->eax, 0x86);
            return done(tf, ret_ip, 1);
        }
        SETHI(tf->eax, 0);
        return done(tf, ret_ip, 0);

    case 0x83:
        if (al == 0x01) {
            vm.event_lin = 0;
            return done(tf, ret_ip, 0);
        }
        if (al != 0x00 || vm.event_lin)
            return done(tf, ret_ip, 1);
        us = ((tf->ecx & 0xFFFF) << 16) | (tf->edx & 0xFFFF);
        vm.event_lin = vm_lin(tf->v86_es, tf->ebx);
        vm.event_until = now + us_ticks(us);
        return done(tf, ret_ip, 0);

    case 0x86:
        /* The INT runs again until the deadline, so the IRQs that come in
           the meantime are delivered in front of it. */
        key = (vm_lin(tf->cs, tf->eip) << 12) ^ (tf->ss << 16) ^ (tf->esp & 0xFFFF);
        if (vm.wait_key != key) {
            us = ((tf->ecx & 0xFFFF) << 16) | (tf->edx & 0xFFFF);
            vm.wait_key = key;
            vm.wait_until = now + us_ticks(us);
        }
        if ((s32)(now - vm.wait_until) >= 0) {
            vm.wait_key = 0;
            SETHI(tf->eax, 0);
            return done(tf, ret_ip, 0);
        }
        vm_idle(vm.wait_until);
        return 1;

    case 0x87:
        gdt = vm_lin(tf->v86_es, tf->esi);
        n = (tf->ecx & 0xFFFF) * 2u;
        src = desc_base(gdt + 0x10);
        dst = desc_base(gdt + 0x18);
        if (phys_owned(src, n) || phys_owned(dst, n) || phys_copy(dst, src, n) != 0) {
            SETHI(tf->eax, 0x02);
            return done(tf, ret_ip, 1);
        }
        SETHI(tf->eax, 0);
        tf->eflags |= FL_ZF;
        return done(tf, ret_ip, 0);

    case 0x88:
        SET16(tf->eax, 0);
        return done(tf, ret_ip, 0);

    case 0x89:
        SETHI(tf->eax, 0xFF);
        return done(tf, ret_ip, 1);

    case 0xE8:
        if (al == 0x01) {
            SET16(tf->eax, 0);
            SET16(tf->ebx, 0);
            SET16(tf->ecx, 0);
            SET16(tf->edx, 0);
            return done(tf, ret_ip, 0);
        }
        if (al == 0x20 || al == 0x81) {
            SETHI(tf->eax, 0x86);
            return done(tf, ret_ip, 1);
        }
        return 0;
    }
    return 0;
}

void vm_int15_tick(u32 now)
{
    if (vm.event_lin && (s32)(now - vm.event_until) >= 0) {
        vm_wr8(vm.event_lin, (u8)(vm_rd8(vm.event_lin) | 0x80));
        vm.event_lin = 0;
    }
}
