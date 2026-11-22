/* The system VM's own DOS on a native boot (docs/v1/native-dos.md, native boot, NB2): GLOSLDR.SYS started GLOS
 * with no DOS under it, so INT 20h and 21h in the system VM are GLOS's own DOS's (kernel/ndos/dos21.c), over the
 * VM's memory as DOS's own structures in DOS's layouts (the SDA, the List of Lists, the file table, the MCB chain,
 * PSPs), so that the kernel's readers of them, the stub, the agent, sessions, apps and the DPMI host work as they
 * do over a DOS.
 *
 * The loader (its PSP from bootinfo's int2f_entry, its memory to the word at PSP:2 the prelude wrote) is adopted
 * as the running program, its own parent; DOS's tables go in a block of DOS's own at the top of conventional
 * memory; INT 20h and 21h's vectors point at ARPLs there (a far call to "DOS's handler", as rm_call and the DPMI
 * host's reflection make, arrives as a #UD: vdos_arpl). A call that would wait for a key comes back to the program
 * at its INT, the VM idling in between as at DOS's own console wait (no wait in a trap: the VM thread is also the
 * apps' and the DOS server's). C: is the volume GLOS started from, mounted at the first call (fsv.c over GLOS's
 * IDE driver, in the trap, as the cache's own INT 13h is). */
#include <string.h>

#include "glos/bootinfo.h"
#include "clock.h"
#include "kprintf.h"
#include "timer.h"
#include "vm.h"
#include "../drv/ide.h"
#include "../ndos/dos21.h"
#include "../ndos/dtime.h"
#include "vdos.h"

static void vm_wr16(u32 lin, u32 v)
{
    vm_wr8(lin, (u8)v);
    vm_wr8(lin + 1, (u8)(v >> 8));
}

static struct dos_m vd;
static struct fsv_vol cvol;
static struct ata_dev *cdev;
static unsigned long cbase;
static int ready, mounted, unanswered;
static u32 arpl21, arpl20;                      /* (seg << 16 | off) */

/* ---- C: */

static int c_read(void *ctx, unsigned long lba, unsigned char *buf, unsigned count)
{
    (void)ctx;
    return ata_read(cdev, cbase + lba, 0, count, buf, 0) != 0;
}

static int c_write(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count)
{
    (void)ctx;
    return ata_write(cdev, cbase + lba, 0, count, buf, 0) != 0;
}

static unsigned long c_now(void *ctx)
{
    struct dos_date d;
    struct dos_time t;
    u32 local = clock_local();

    (void)ctx;
    if (local < 315532800u)
        return 0;
    dos_from_seconds(local - 315532800u, &d, &t);
    return (unsigned long)dos_pack_date(&d) << 16 | dos_pack_time(&t);
}

static int is_fat(unsigned t)
{
    return t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E;
}

/* The disk the BIOS calls 80h: the ATA disk of its size (bootinfo's disks), else the first; its active FAT
   partition, else the whole disk. */
static void mount_c(void)
{
    static unsigned char s[512];
    unsigned long want = vm.bi->n_disks ? vm.bi->disks[0].sectors_lo : 0;
    struct ata_dev *d;
    int i, r = -1;

    mounted = 1;
    ide_probe(ATA_PROBE_OWN);
    for (i = 0; (d = ide_dev(i)) != 0; i++) {
        if (!d->present || d->atapi)
            continue;
        if (want && d->sectors_lo == want) {
            cdev = d;
            break;
        }
        if (!cdev)
            cdev = d;
    }
    if (cdev && ata_read(cdev, 0, 0, 1, s, 0) == 0) {
        cbase = 0;
        if (s[510] == 0x55 && s[511] == 0xAA)
            for (i = 0; i < 4; i++)
                if ((s[446 + i * 16] & 0x80) && is_fat(s[446 + i * 16 + 4])) {
                    cbase = (unsigned long)s[446 + i * 16 + 8] | (unsigned long)s[446 + i * 16 + 9] << 8
                            | (unsigned long)s[446 + i * 16 + 10] << 16 | (unsigned long)s[446 + i * 16 + 11] << 24;
                    break;
                }
        r = fsv_mount(&cvol, c_read, c_write, c_now, 0, 437, 0);
    }
    vd.vol[2] = r == 0 ? &cvol : 0;
    vd.cds.valid = r == 0 ? 1u << 2 : 0;
    kprintf("GLOS-NATIVE step=c disk=%s base=%u mount=%d\n", cdev ? cdev->model : "none", (u32)cbase, r);
}

/* ---- the machine */

static void vd_out(void *ctx, const char *s, unsigned n)
{
    (void)ctx, (void)s, (void)n;                /* (a program's CON output: the agent's capture sees its calls
                                                   already; the screen is NB3's) */
}

static unsigned long vd_now(void *ctx)
{
    u32 local = clock_local();

    (void)ctx;
    return local > 315532800u ? local - 315532800u : 0;
}

static int vd_wait(void *ctx)
{
    (void)ctx;
    return -1;                                  /* (no waiting in a trap: the call comes back to its INT) */
}

int vdos_init(struct bootinfo *bi)
{
    unsigned psp = bi->int2f_entry >> 16;
    unsigned top = vm_rd16((u32)psp * 16 + 2), low_top = vm_rd16(0x413) * 64u;
    unsigned long a;
    u8 *mem = vm_ptr(0);

    if (dos_m_init_vm(&vd, mem, (unsigned long)low_top * 16, psp, top, low_top, 0, 2, 437) != 0) {
        kprintf("GLOS-NATIVE step=dos error psp=%04x top=%04x low_top=%04x\n", psp, top, low_top);
        return -1;
    }
    vd.out = vd_out, vd.now = vd_now, vd.wait = vd_wait;
    bi->indos = vd.lay.sda + 1;
    bi->sda = vd.lay.sda;
    bi->lol = vd.lay.lol;
    /* INT 21h's and 20h's vectors at ARPLs in DOS's own bytes */
    a = vd.lay.misc + 4;
    mem[a] = 0x63, mem[a + 1] = 0xC0, mem[a + 2] = 0x63, mem[a + 3] = 0xC0;
    arpl21 = (u32)(a >> 4) << 16 | (u32)(a & 0xF);
    arpl20 = arpl21 + 2;
    vm_wr16(0x21 * 4, arpl21 & 0xFFFF), vm_wr16(0x21 * 4 + 2, arpl21 >> 16);
    vm_wr16(0x20 * 4, arpl20 & 0xFFFF), vm_wr16(0x20 * 4 + 2, arpl20 >> 16);
    ready = 1;
    kprintf("GLOS-NATIVE step=dos psp=%04x top=%04x tables=%05x sda=%05x\n", psp, top, (u32)(vd.lay.lol - 0x12),
            (u32)vd.lay.sda);
    return 0;
}

int vdos_on(void) { return ready; }

#define LO16(x, v) ((x) = ((x) & 0xFFFF0000u) | ((v) & 0xFFFF))

/* INT n (20h, 21h) for the program at the trap frame: ret its return address (CS:IP after the INT), at where the
   INT was (for a call made again after a wait), via_arpl when it came through the vector (a far call). */
void vdos_int(struct trapframe *tf, u8 n, u32 ret, u32 at, int via_arpl)
{
    struct dos_regs86 r;
    int res;

    if (!mounted)
        mount_c();
    r.ax = tf->eax & 0xFFFF, r.bx = tf->ebx & 0xFFFF, r.cx = tf->ecx & 0xFFFF, r.dx = tf->edx & 0xFFFF;
    r.si = tf->esi & 0xFFFF, r.di = tf->edi & 0xFFFF, r.bp = tf->ebp & 0xFFFF;
    r.ds = tf->v86_ds & 0xFFFF, r.es = tf->v86_es & 0xFFFF;
    r.cs = tf->cs & 0xFFFF, r.ip = ret & 0xFFFF, r.ss = tf->ss & 0xFFFF, r.sp = tf->esp & 0xFFFF;
    r.flags = tf->eflags & 0xFFFF;
    res = n == 0x20 ? dos20(&vd, &r) : dos21(&vd, &r);
    if (res == DOS21_GONE) {                    /* a key waited for: back to the INT, the VM idling meanwhile */
        if (via_arpl) {
            u32 sp = (tf->esp - 6) & 0xFFFF, lin = (tf->ss & 0xFFFF) * 16 + sp;

            vm_wr16(lin, ret & 0xFFFF), vm_wr16(lin + 2, tf->cs & 0xFFFF), vm_wr16(lin + 4, tf->eflags & 0xFFFF);
            LO16(tf->esp, sp);
            tf->cs = (n == 0x20 ? arpl20 : arpl21) >> 16;
            tf->eip = (n == 0x20 ? arpl20 : arpl21) & 0xFFFF;
        } else {
            tf->eip = at;
        }
        vm_idle(timer_ticks() + 8);
        return;
    }
    if (res == DOS21_END || res == DOS21_CTRLC) {
        kprintf("GLOS-NATIVE dos %s ax=%04x psp=%04x\n", res == DOS21_END ? "end-of-root" : "ctrl-c", r.ax,
                dos_cur_psp(&vd));             /* (NB3: Ctrl-C; the root never ends) */
    }
    if (res == DOS21_UNANSWERED && unanswered++ < 40)
        kprintf("GLOS-NATIVE unanswered int=%02x ax=%04x psp=%04x\n", n, tf->eax & 0xFFFF, dos_cur_psp(&vd));
    LO16(tf->eax, r.ax), LO16(tf->ebx, r.bx), LO16(tf->ecx, r.cx), LO16(tf->edx, r.dx);
    LO16(tf->esi, r.si), LO16(tf->edi, r.di), LO16(tf->ebp, r.bp);
    tf->v86_ds = r.ds, tf->v86_es = r.es;
    tf->cs = r.cs, tf->eip = r.ip, tf->ss = r.ss;
    LO16(tf->esp, r.sp);
    tf->eflags = (tf->eflags & ~0x0FD5u) | (r.flags & 0x0FD5u);
}

/* A #UD at INT 21h's or 20h's ARPL: the call made through the vector (its INT frame on the stack). 1 if it was. */
int vdos_arpl(struct trapframe *tf)
{
    u32 at = (tf->cs & 0xFFFF) << 16 | (tf->eip & 0xFFFF), lin, ip, cs, f;
    u8 n;

    if (!ready || (at != arpl21 && at != arpl20))
        return 0;
    n = at == arpl21 ? 0x21 : 0x20;
    lin = (tf->ss & 0xFFFF) * 16 + (tf->esp & 0xFFFF);
    ip = vm_rd16(lin), cs = vm_rd16(lin + 2), f = vm_rd16(lin + 4);
    LO16(tf->esp, (tf->esp + 6) & 0xFFFF);
    tf->cs = cs;
    tf->eflags = (tf->eflags & ~0x0FD5u) | (f & 0x0FD5u);
    vdos_int(tf, n, ip, ip, 1);
    return 1;
}
