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
#include "cache.h"
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

/* ---- C: */

static int c_read(void *ctx, unsigned long lba, unsigned char *buf, unsigned count)
{
    (void)ctx;
    return ata_read(cdev, cbase + lba, 0, count, buf, 0) != 0;
}

/* (past the block cache, which then forgets those sectors: a program's INT 13h through it reads them again) */
static int c_write(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count)
{
    int r = ata_write(cdev, cbase + lba, 0, count, buf, 0);

    (void)ctx;
    cache_forget(0x80, (u32)(cbase + lba), count);
    return r != 0;
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

/* A program's CON output: on the screen, as the BIOS's teletype writes it (the agent's capture has seen the call). */
static void vd_out(void *ctx, const char *s, unsigned n)
{
    (void)ctx;
    vcon_tty(s, n);
}

static unsigned long vd_now(void *ctx)
{
    u32 local = clock_local();

    (void)ctx;
    return local > 315532800u ? local - 315532800u : 0;
}

/* A key the BIOS's INT 09h has put in its buffer (the VM's keyboard: the PC's, lcon's and the shell's typing, glos
   type's) into GLOS's DOS's ring when it is empty, one at a time, as DOS takes them through INT 16h: so a program
   that reads a line through DOS and then a key through INT 16h finds that key still in the BIOS's buffer. For a
   call that isn't input, only Ctrl-C is taken. */
static void vd_fill(void *ctx, int input)
{
    u32 head = vm_rd16(0x41A), tail = vm_rd16(0x41C), start = vm_rd16(0x480), end = vm_rd16(0x482), key;

    (void)ctx;
    if (head == tail || dos_key_peek(&vd) >= 0)
        return;
    if (start < 0x1E || end > 0x100 || start >= end)
        start = 0x1E, end = 0x3E;
    key = vm_rd16(0x400 + head);
    if (!input && (key & 0xFF) != 0x03)
        return;
    dos_key_push(&vd, key);
    head += 2;
    if (head >= end)
        head = start;
    vm_wr16(0x41A, head);
}

static int vd_wait(void *ctx)
{
    vd_fill(ctx, 1);
    return dos_key_peek(&vd) >= 0 ? 0 : -1;     /* (none yet: the call comes back to its INT, no waiting in a trap) */
}

/* DOS's bytes for the vectors (the layout's misc + 4 on): ARPLs for INT 21h, 20h, 29h and 23h (DOS's own Ctrl-C
   handler), one a program's INT 23h returns to, and an IRET that ends INT 2Fh's chain. */
enum { V21, V20, V29, V23, VC23, NV };
static u32 arpl[NV];                            /* (seg << 16 | off) */
static struct dos_regs86 c23;                   /* the call Ctrl-C stopped: its registers, where its INT was (a */
static u32 c23_sp, c23_at;                      /* call made again may wait), whether it came through the vector */
static int c23_via;

static void set_vec(u32 n, u32 v)
{
    vm_wr16(n * 4, v & 0xFFFF);
    vm_wr16(n * 4 + 2, v >> 16);
}

static u32 vec(u32 n) { return (u32)vm_rd16(n * 4 + 2) << 16 | vm_rd16(n * 4); }

int vdos_init(struct bootinfo *bi)
{
    unsigned psp = bi->int2f_entry >> 16;
    unsigned top = vm_rd16((u32)psp * 16 + 2), low_top = vm_rd16(0x413) * 64u;
    unsigned long a;
    u8 *mem = vm_ptr(0);
    int i;

    if (dos_m_init_vm(&vd, mem, (unsigned long)low_top * 16, psp, top, low_top, 0, 2, 437) != 0) {
        kprintf("GLOS-NATIVE step=dos error psp=%04x top=%04x low_top=%04x\n", psp, top, low_top);
        return -1;
    }
    vd.out = vd_out, vd.now = vd_now, vd.wait = vd_wait, vd.fill = vd_fill;
    bi->indos = vd.lay.sda + 1;
    bi->sda = vd.lay.sda;
    bi->lol = vd.lay.lol;
    a = vd.lay.misc + 4;
    for (i = 0; i < NV; i++) {
        mem[a + 2 * i] = 0x63, mem[a + 2 * i + 1] = 0xC0;
        arpl[i] = (u32)(a >> 4) << 16 | (u32)((a & 0xF) + 2 * i);
    }
    mem[a + 2 * NV] = 0xCF;                     /* (IRET) */
    set_vec(0x21, arpl[V21]), set_vec(0x20, arpl[V20]), set_vec(0x29, arpl[V29]), set_vec(0x23, arpl[V23]);
    set_vec(0x2F, arpl[V21] + 2 * NV);          /* (INT 2Fh's chain ends here: GLOS's int2f_hook answers before it) */
    ready = 1;
    kprintf("GLOS-NATIVE step=dos psp=%04x top=%04x tables=%05x sda=%05x\n", psp, top, (u32)(vd.lay.lol - 0x12),
            (u32)vd.lay.sda);
    return 0;
}

int vdos_on(void) { return ready; }

#define LO16(x, v) ((x) = ((x) & 0xFFFF0000u) | ((v) & 0xFFFF))

static void to_regs(const struct trapframe *tf, struct dos_regs86 *r, u32 ret)
{
    r->ax = tf->eax & 0xFFFF, r->bx = tf->ebx & 0xFFFF, r->cx = tf->ecx & 0xFFFF, r->dx = tf->edx & 0xFFFF;
    r->si = tf->esi & 0xFFFF, r->di = tf->edi & 0xFFFF, r->bp = tf->ebp & 0xFFFF;
    r->ds = tf->v86_ds & 0xFFFF, r->es = tf->v86_es & 0xFFFF;
    r->cs = tf->cs & 0xFFFF, r->ip = ret & 0xFFFF, r->ss = tf->ss & 0xFFFF, r->sp = tf->esp & 0xFFFF;
    r->flags = tf->eflags & 0xFFFF;
}

static void from_regs(struct trapframe *tf, const struct dos_regs86 *r)
{
    LO16(tf->eax, r->ax), LO16(tf->ebx, r->bx), LO16(tf->ecx, r->cx), LO16(tf->edx, r->dx);
    LO16(tf->esi, r->si), LO16(tf->edi, r->di), LO16(tf->ebp, r->bp);
    tf->v86_ds = r->ds, tf->v86_es = r->es;
    tf->cs = r->cs, tf->eip = r->ip, tf->ss = r->ss;
    LO16(tf->esp, r->sp);
    tf->eflags = (tf->eflags & ~0x0FD5u) | (r->flags & 0x0FD5u);
}

static void push16(struct trapframe *tf, u32 v)
{
    LO16(tf->esp, (tf->esp - 2) & 0xFFFF);
    vm_wr16((tf->ss & 0xFFFF) * 16 + (tf->esp & 0xFFFF), v);
}

/* The program ends as DOS's own Ctrl-C handler ends it (4Dh's AH 01h): its parent runs on. */
static void ctrl_c_end(struct trapframe *tf, struct dos_regs86 *r)
{
    if (dos_ctrl_c_end(&vd, r) == DOS21_END)
        kprintf("GLOS-NATIVE dos end-of-root ctrl-c psp=%04x\n", dos_cur_psp(&vd));
    from_regs(tf, r);
}

/* INT n (20h, 21h) for the program at the trap frame: ret its return address (CS:IP after the INT), at where the
   INT was (for a call made again after a wait), via_arpl when it came through the vector (a far call). */
void vdos_int(struct trapframe *tf, u8 n, u32 ret, u32 at, int via_arpl)
{
    struct dos_regs86 r;
    int res;

    if (!mounted)
        mount_c();
    to_regs(tf, &r, ret);
    res = n == 0x20 ? dos20(&vd, &r) : dos21(&vd, &r);
    if (res == DOS21_GONE) {                    /* a key waited for: back to the INT, the VM idling meanwhile */
        if (via_arpl) {
            push16(tf, tf->eflags & 0xFFFF);
            push16(tf, tf->cs & 0xFFFF);
            push16(tf, ret & 0xFFFF);
            tf->cs = arpl[n == 0x20 ? V20 : V21] >> 16;
            tf->eip = arpl[n == 0x20 ? V20 : V21] & 0xFFFF;
        } else {
            tf->eip = at;
        }
        vm_idle(timer_ticks() + 8);
        return;
    }
    if (res == DOS21_CTRLC) {
        if (vec(0x23) == arpl[V23]) {           /* (no handler of the program's: DOS's own ends it) */
            ctrl_c_end(tf, &r);
            return;
        }
        c23 = r;                                /* its INT 23h, the call's registers, back by an ARPL of GLOS's */
        c23_at = at, c23_via = via_arpl;
        from_regs(tf, &r);
        c23_sp = tf->esp & 0xFFFF;
        push16(tf, tf->eflags & 0xFFFF);
        push16(tf, arpl[VC23] >> 16);
        push16(tf, arpl[VC23] & 0xFFFF);
        tf->cs = vec(0x23) >> 16;
        tf->eip = vec(0x23) & 0xFFFF;
        tf->eflags &= ~0x0300u;                 /* (IF and TF, as an interrupt clears them) */
        return;
    }
    if (res == DOS21_END)
        kprintf("GLOS-NATIVE dos end-of-root ax=%04x psp=%04x\n", r.ax, dos_cur_psp(&vd));     /* (the stub never ends) */
    if (res == DOS21_UNANSWERED && unanswered++ < 40)
        kprintf("GLOS-NATIVE unanswered int=%02x ax=%04x psp=%04x\n", n, tf->eax & 0xFFFF, dos_cur_psp(&vd));
    from_regs(tf, &r);
}

/* A #UD at one of DOS's ARPLs (its INT frame on the stack, but VC23's: a RETF or an IRET to it). 1 if it was. */
int vdos_arpl(struct trapframe *tf)
{
    u32 at = (tf->cs & 0xFFFF) << 16 | (tf->eip & 0xFFFF), lin, ip, cs, f;
    struct dos_regs86 r;
    int i;
    char c;

    if (!ready)
        return 0;
    for (i = 0; i < NV && arpl[i] != at; i++)
        ;
    if (i == NV)
        return 0;
    lin = (tf->ss & 0xFFFF) * 16 + (tf->esp & 0xFFFF);
    if (i == VC23) {                            /* back from the program's INT 23h */
        if ((tf->esp & 0xFFFF) == ((c23_sp - 2) & 0xFFFF) && (vm_rd16(lin) & 1)) {
            LO16(tf->esp, (tf->esp + 2) & 0xFFFF);      /* (RETF with CF set: the program ends) */
            to_regs(tf, &r, tf->eip);
            ctrl_c_end(tf, &r);
            return 1;
        }
        r = c23;                                /* (IRET, or RETF with CF clear: the call again) */
        from_regs(tf, &r);
        vdos_int(tf, 0x21, r.ip, c23_at, c23_via);
        return 1;
    }
    ip = vm_rd16(lin), cs = vm_rd16(lin + 2), f = vm_rd16(lin + 4);
    LO16(tf->esp, (tf->esp + 6) & 0xFFFF);
    tf->cs = cs;
    tf->eip = ip;
    tf->eflags = (tf->eflags & ~0x0FD5u) | (f & 0x0FD5u);
    switch (i) {
    case V21:
    case V20:
        vdos_int(tf, i == V21 ? 0x21 : 0x20, ip, ip, 1);
        break;
    case V29:                                   /* DOS's fast console output */
        c = (char)(tf->eax & 0xFF);
        vcon_tty(&c, 1);
        break;
    case V23:                                   /* DOS's own Ctrl-C handler, called or chained to: the end */
        to_regs(tf, &r, ip);
        ctrl_c_end(tf, &r);
        break;
    }
    return 1;
}
