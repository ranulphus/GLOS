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
 * apps' and the DOS server's). C: is the volume GLOS started from, mounted at the first call: fsv.c over GLOS's
 * IDE driver (in the trap, as the cache's own INT 13h is) when the BIOS's disk is an IDE disk GLOS drives, else
 * over the BIOS's own calls (a USB stick, a SCSI or SATA disk: "the BIOS's disks" below). */
#include <string.h>

#include "glos/bootinfo.h"
#include "agent.h"
#include "app.h"
#include "arch.h"
#include "clock.h"
#include "dpmi.h"
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

/* ---- the BIOS's disks

   A disk GLOS has no driver for is read and written by the BIOS's INT 13h, as nested real-mode calls from the
   trap (the VM's thread: rm_call; the disk cache answers what it holds on the way, as for a program's INT 13h).
   The calls' stack and the sectors' buffer are a block of DOS's memory under its tables (dos_m_init_vm's extra;
   the BIOS wants its data below 1 MB), given back when C: turns out to be GLOS's own driver's: 2 KB for a
   request packet at its start and the stack above it, then 16 KB of sectors, the part of them used never across
   a 64 KB boundary (a BIOS's DMA). */
#define X_HEAD      0x800u
#define X_PARAS     (X_HEAD / 16 + 0x400)
static unsigned xseg;                           /* the block; 0: none */
static u32 xdata, xrun;                         /* the buffer (linear) and the sectors it holds */
static struct bi_disk *bdsk;                    /* the BIOS's disk C: is on (bootinfo's) */
static int cbios;                               /* C: is read by the BIOS's calls */

/* INT 13h: 0, or the BIOS's error (FFh: none said). */
static int bios13(struct rmregs *r)
{
    r->ss = (u16)xseg;
    r->sp = X_HEAD;
    r->flags = 2;
    if (rm_call(0, r, RM_INT, 0x13, 0, 0) != 0)
        return 0xFF;
    if (!(r->flags & 1))
        return 0;
    return (r->eax >> 8) & 0xFF ? (int)((r->eax >> 8) & 0xFF) : 0xFF;
}

/* count sectors of the disk at lba: the BIOS's LBA calls where it has them, else cylinder, head and sector, a
   track's rest at a time; a call that fails is made again after a reset, three times. 0, or the BIOS's error. */
static int bios_rw(int write, u32 lba, u8 *buf, u32 count)
{
    struct rmregs r;
    int e = 0, tries;

    if (!xseg || !bdsk)
        return 0xFF;
    while (count) {
        u32 k = count < xrun ? count : xrun, c = 0, h = 0, s = 0;

        if (!(bdsk->flags & BI_DISK_EDD)) {
            u32 track = bdsk->heads * bdsk->spt, t;

            if (!track)
                return 0x01;
            c = lba / track, t = lba % track, h = t / bdsk->spt, s = t % bdsk->spt;
            if (k > bdsk->spt - s)
                k = bdsk->spt - s;
            if (c > 1023)
                return 0x01;
        }
        if (write)
            memcpy(vm_ptr(xdata), buf, k * 512);
        for (tries = 0; tries < 3; tries++) {
            memset(&r, 0, sizeof r);
            if (bdsk->flags & BI_DISK_EDD) {
                u8 *p = vm_ptr((u32)xseg << 4);

                memset(p, 0, 16);
                p[0] = 16, p[2] = (u8)k, p[4] = (u8)(xdata & 15), p[6] = (u8)(xdata >> 4), p[7] = (u8)(xdata >> 12);
                p[8] = (u8)lba, p[9] = (u8)(lba >> 8), p[10] = (u8)(lba >> 16), p[11] = (u8)(lba >> 24);
                r.eax = write ? 0x4300 : 0x4200;
                r.ds = (u16)xseg;
            } else {
                r.eax = (write ? 0x0300u : 0x0200u) | k;
                r.ecx = ((c & 0xFF) << 8) | ((c >> 2) & 0xC0) | (s + 1);
                r.edx = h << 8;
                r.es = (u16)(xdata >> 4);
                r.ebx = xdata & 15;
            }
            r.edx |= bdsk->dl;
            if ((e = bios13(&r)) == 0)
                break;
            memset(&r, 0, sizeof r);
            r.edx = bdsk->dl;
            bios13(&r);
        }
        if (e)
            return e;
        if (!write)
            memcpy(buf, vm_ptr(xdata), k * 512);
        lba += k, buf += k * 512, count -= k;
    }
    return 0;
}

int vdos_c_bios(void) { return cbios; }

/* GLOS leaves the PC's USB controllers to the BIOS when C: is the BIOS's disk and the BIOS doesn't say that the
   disk is something other than a USB one: its calls for a USB stick stop the moment GLOS takes the controller
   the stick is on (kernel/drv/uhci.c). */
int vdos_usb_bios(void)
{
    if (ready && !mounted)
        kprintf("GLOS-WARN native: USB asked about before C: was mounted\n");
    return cbios && vm.bi->boot_if != BI_IF_ATA && vm.bi->boot_if != BI_IF_OTHER;
}

/* ---- C: */

/* (through the disk cache, which a program's own INT 13h on the same disk goes through too) */
static int c_read(void *ctx, unsigned long lba, unsigned char *buf, unsigned count)
{
    (void)ctx;
    if (cbios)
        return bios_rw(0, (u32)(cbase + lba), buf, count) != 0;
    return cache_kread(0x80, cdev, (u32)(cbase + lba), count, buf) != 0;
}

static int c_write(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count)
{
    (void)ctx;
    if (cbios)
        return bios_rw(1, (u32)(cbase + lba), (u8 *)buf, count) != 0;
    return cache_kwrite(0x80, cdev, (u32)(cbase + lba), count, buf) != 0;
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

/* A FAT volume's first sector, by its figures (512 bytes a sector, sectors a cluster a power of two, one or two
   FATs, a media byte), not a partition table's: a disk with no table, and a USB stick a BIOS starts as a floppy
   (its "disk" is then the stick's partition). */
static int fat_first(const unsigned char *s)
{
    return s[510] == 0x55 && s[511] == 0xAA && (s[0] == 0xEB || s[0] == 0xE9) && s[11] == 0 && s[12] == 2 && s[13]
           && !(s[13] & (s[13] - 1)) && (s[16] == 1 || s[16] == 2) && s[21] >= 0xF0;
}

/* The disk GLOS started from (the BIOS's number of it and its geometry from the loader: bootinfo's boot_disk):
   the one IDE disk GLOS drives whose first sector is that disk's as the BIOS reads it, and whose size is its size
   (the size the BIOS's 48h gives, or up to a cylinder more, a BIOS's rounding; at least the size of 08h's geometry
   on a BIOS without 48h), by GLOS's IDE driver; none, or two alike, and the disk is read by the BIOS's calls (so
   is every disk with /BIOSDISK, the tests', and a disk of the floppies' numbers: a USB stick a BIOS starts as
   one). If the BIOS can't be asked: the ATA disk of its size, else the first. Then the volume: the disk itself
   if its first sector is a FAT volume's, else its active FAT partition. */
static void mount_c(void)
{
    static unsigned char s[512], s0[512];
    struct bi_disk *bd = &vm.bi->boot_disk;
    int said = bd->bps == 512 && (bd->spt || (bd->flags & BI_DISK_PARAMS));    /* (the loader's word on it) */
    u32 dl = said ? bd->dl : 0x80;
    unsigned long want;
    struct ata_dev *d;
    int i, r = -1, hits = 0, asked, differ = 0, failed = 0;

    mounted = 1;
    if (said)
        bdsk = bd;
    for (i = 0; !bdsk && i < (int)vm.bi->n_disks; i++)
        if (vm.bi->disks[i].dl == dl && vm.bi->disks[i].bps == 512)
            bdsk = &vm.bi->disks[i];
    want = bdsk ? bdsk->sectors_lo : 0;
    /* The BIOS is asked first, and GLOS's IDE driver looks at the disks as it does beside a DOS: with no reset
       and no settings of its own (ATA_PROBE_OWN's). The BIOS goes on reading these disks: the one GLOS started
       from when it has no driver for it, any of them for DISK and for programs' INT 13h. After a reset a drive
       has lost what the BIOS set (the sectors a block of its READ MULTIPLE, its timing): 86Box's BIOS then read
       16 sectors of a longer call right and the rest wrong, and the owner's PCChips M925G gave a first sector
       that was none (2026-11-23). */
    asked = bios_rw(0, 0, s0, 1) == 0;
    ide_probe(0);
    for (i = 0; dl >= 0x80 && (d = ide_dev(i)) != 0; i++) {
        if (!d->present || d->atapi)
            continue;
        if (asked) {
            int size = d->sectors_hi || d->sectors_lo >= want;

            if (size && (bdsk->flags & BI_DISK_PARAMS) && !bdsk->sectors_hi)
                size = !d->sectors_hi && d->sectors_lo - want <= 255u * 63;
            if (size && !(vm.bi->flags & BI_F_BIOSDISK)) {
                if (ata_read(d, 0, 0, 1, s, 0) != 0)
                    failed++;
                else if (memcmp(s, s0, 512))
                    differ++;
                else
                    hits++, cdev = d;
            }
            continue;
        }
        if (want && d->sectors_lo == want) {
            cdev = d;
            break;
        }
        if (!cdev)
            cdev = d;
    }
    if (asked && hits != 1)
        cdev = 0, cbios = 1;
    /* (what was found, for a PC's log: the BIOS's sector's first bytes and its last two, the IDE disks that
       had the same sector, another, or none to give) */
    kprintf("GLOS-NATIVE step=c-disk dl=%02x chs=%u/%u/%u flags=%x asked=%d first=%02x%02x%02x sig=%02x%02x "
            "same=%d differ=%d failed=%d\n", dl, bdsk ? (u32)bdsk->cyls : 0, bdsk ? (u32)bdsk->heads : 0,
            bdsk ? (u32)bdsk->spt : 0, bdsk ? (u32)bdsk->flags : 0, asked, s0[0], s0[1], s0[2], s0[510], s0[511],
            hits, differ, failed);
    if (!cbios && xseg) {                       /* (GLOS's own driver: the BIOS's calls' block back to DOS) */
        dos_mem_free(&vd.mcb, xseg);
        xseg = 0;
    }
    if (cbios) {
        memcpy(s, s0, 512);
        r = 0;
    } else if (cdev) {
        r = ata_read(cdev, 0, 0, 1, s, 0);
    }
    if (r == 0) {
        r = -1;
        cbase = 0;
        if (!fat_first(s) && s[510] == 0x55 && s[511] == 0xAA)
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
    kprintf("GLOS-NATIVE step=c dl=%02x disk=%s if=%u base=%u mount=%d\n", dl, cbios ? "bios" : cdev ? cdev->model : "none",
            (u32)vm.bi->boot_if, (u32)cbase, r);
}

/* ---- the machine */

/* A program's CON output, a character at a time where a DOS's console driver would make INT 29h: the agent's
   hook there first (a console job's window has it, and nothing else; a job whose output is kept has it too), then
   the screen, as the BIOS's teletype writes it. */
static void vd_out(void *ctx, const char *s, unsigned n)
{
    struct trapframe f;

    (void)ctx;
    for (; n; n--, s++) {
        f.eax = (u8)*s;
        if (!agent_vm_int29(&f))
            vcon_tty(s, 1);
    }
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

    if (dos_m_init_vm(&vd, mem, (unsigned long)low_top * 16, psp, top, low_top, 0, 2, 437, X_PARAS, &xseg) != 0) {
        kprintf("GLOS-NATIVE step=dos error psp=%04x top=%04x low_top=%04x\n", psp, top, low_top);
        return -1;
    }
    {                                           /* the sectors' buffer: the larger side of a 64 KB boundary in it */
        u32 lin = ((u32)xseg << 4) + X_HEAD, end = ((u32)xseg + X_PARAS) << 4, edge = (lin + 0xFFFF) & ~0xFFFFu;

        if (edge > lin && edge < end) {
            if (edge - lin >= end - edge)
                end = edge;
            else
                lin = edge;
        }
        xdata = lin, xrun = (end - lin) / 512;
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
    /* The call, its disk's waits giving the processor to other threads (ide_patient): an app's own threads, the
       mixer and the network go on while a program is in DOS, as they do while a DOS under GLOS is in the BIOS. */
    ide_patient(1);
    vm_wr8(vm.indos, (u8)(vm_rd8(vm.indos) + 1));        /* (InDOS, as a DOS has it during a call: the agent's hooks
                                                           and the kill look at it) */
    res = n == 0x20 ? dos20(&vd, &r) : dos21(&vd, &r);
    vm_wr8(vm.indos, (u8)(vm_rd8(vm.indos) - 1));
    ide_patient(0);
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
        app_vm_idle_turn(0);                    /* (an app's own calls meanwhile, as at a DOS's wait for a key: its
                                                   console job's keys may be what it is about to type) */
        if (vm_rd16(0x41A) == vm_rd16(0x41C))
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
