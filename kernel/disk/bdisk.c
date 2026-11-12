/* The BIOS's disks by their sectors (bdisk.h; docs/v1/shell.md, step S0): what the shell's DISK and DD stand
 * on, so that GLOS partitions, formats and copies disks with no tool of the DOS's.
 *
 * A kernel thread asks; the VM thread makes the BIOS's calls (INT 13h, nested real-mode calls) when the stub
 * next asks what to do: nothing else is in DOS or in the BIOS then, as for the display's mode sets and the
 * apps' calls. The calls go through rm_call(), so the disk cache sees them (kernel/drv/cache.c): what it holds
 * of the sectors read is given from memory, and what is written is written first and then kept.
 *
 * The sectors pass through a buffer in DOS's memory (the BIOS wants its data below 1 MB): 32 KB and a little,
 * taken from DOS at the top of its memory when first wanted and given back when the last user ends
 * (bdisk_close). A run is what the buffer holds, 64 sectors at most; the part of the buffer used never
 * crosses a 64 KB boundary (a floppy's DMA can't). The BIOS's LBA calls where a disk has them, else cylinder,
 * head and sector, a track's rest at a time. A call that fails is made again after a reset, three times
 * (a floppy's motor). */
#include <string.h>

#include "glos/bootinfo.h"
#include "bdisk.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "sched.h"
#include "timer.h"
#include "vm.h"
#include "disk.h"

#define HEAD_PARAS 8u                   /* the block's first 128 bytes: the BIOS's packets */
#define WANT_PARAS (HEAD_PARAS + 64u * 512u / 16u)
#define TAKE_TICKS (30u * 1024u)        /* how long the VM thread has to come round */

enum { OP_GEO = 1, OP_READ, OP_WRITE };

static struct {
    struct waitq users, waitq;
    u8 busy, pending, taken, done, free_ask;
    u8 op, drive;
    u32 lba, count, opens;
    u8 *data;
    struct bdisk_geo *geo;
    int result;
    u16 seg, paras;                     /* the block in DOS's memory, 0: none */
    u32 data_lin, run;                  /* the part of it the sectors use, and how many it holds */
    struct bdisk_geo known[6];          /* 00h, 01h, 80h-83h: as last asked (sectors 0: not yet) */
    u8 no_lba;                          /* glos disktest lba off: as on a BIOS without the LBA calls */
} b;

static int slot(u8 drive) { return drive < 2 ? drive : drive >= 0x80 && drive < 0x84 ? 2 + (drive - 0x80) : -1; }

/* ---- the VM thread's side */

static int rm(struct rmregs *r, u8 vec)
{
    r->ss = vm.loader_cs;
    r->sp = (u16)vm.bi->kill_sp;
    r->flags = 2;
    return rm_call(0, r, RM_INT, vec, 0, 0) == 0 && !(r->flags & 1);
}

/* INT 13h: 0, or the BIOS's error. */
static int bios(struct rmregs *r)
{
    if (rm(r, 0x13))
        return 0;
    return (r->eax >> 8) & 0xFF ? (int)((r->eax >> 8) & 0xFF) : 0xFF;
}

static void buffer_free(void)
{
    struct rmregs r;
    if (!b.seg)
        return;
    memset(&r, 0, sizeof r);
    r.eax = 0x4900;
    r.es = b.seg;
    rm(&r, 0x21);
    b.seg = 0;
}

/* The block, from the top of DOS's memory (last fit: a program loaded meanwhile is below it, and nothing
   resident is left above a hole), DOS's own strategy put back. */
static int buffer_take(void)
{
    struct rmregs r;
    u32 strategy = 0, have, lin, end, edge, paras = WANT_PARAS;
    memset(&r, 0, sizeof r);
    r.eax = 0x5800;
    have = (u32)rm(&r, 0x21);
    if (have) {
        strategy = r.eax & 0xFFFF;
        memset(&r, 0, sizeof r);
        r.eax = 0x5801, r.ebx = 0x0002;
        rm(&r, 0x21);
    }
    memset(&r, 0, sizeof r);
    r.eax = 0x4800, r.ebx = paras;
    if (!rm(&r, 0x21)) {                        /* (BX: the largest block there is) */
        paras = r.ebx & 0xFFFF;
        if (paras > WANT_PARAS)
            paras = WANT_PARAS;
        memset(&r, 0, sizeof r);
        r.eax = 0x4800, r.ebx = paras;
        if (paras < HEAD_PARAS + 2 * 32 || !rm(&r, 0x21))
            paras = 0;
    }
    if (paras)
        b.seg = (u16)r.eax, b.paras = (u16)paras;
    if (have) {
        memset(&r, 0, sizeof r);
        r.eax = 0x5801, r.ebx = strategy;
        rm(&r, 0x21);
    }
    if (!paras)
        return -1;
    lin = ((u32)b.seg << 4) + HEAD_PARAS * 16;
    end = ((u32)b.seg << 4) + paras * 16;
    edge = (lin + 0xFFFF) & ~0xFFFFu;
    if (edge > lin && edge < end) {             /* the larger side of the 64 KB boundary */
        if (edge - lin >= end - edge)
            end = edge;
        else
            lin = edge;
    }
    b.data_lin = lin;
    b.run = (end - lin) / 512;
    return 0;
}

static int geo_ask(u8 drive, struct bdisk_geo *g)
{
    struct rmregs r;
    u32 cyls, heads, spt;
    memset(g, 0, sizeof *g);
    memset(&r, 0, sizeof r);
    r.eax = 0x0800, r.edx = drive;
    if (bios(&r) != 0 || !(r.ecx & 0x3F))
        return BDISK_NONE;
    if (drive >= 0x80 ? (r.edx & 0xFF) <= (u32)(drive - 0x80) : !(r.ebx & 0xFF) || (r.edx & 0xFF) <= drive)
        return BDISK_NONE;                      /* (DL: how many there are; a floppy drive of no kind) */
    cyls = (((r.ecx >> 8) & 0xFF) | ((r.ecx & 0xC0) << 2)) + 1;
    heads = ((r.edx >> 8) & 0xFF) + 1;
    spt = r.ecx & 0x3F;
    g->cyls = (u16)cyls, g->heads = (u16)heads, g->spt = (u16)spt;
    g->sectors = cyls * heads * spt;
    if (drive < 0x80)
        return 0;
    memset(&r, 0, sizeof r);
    r.eax = 0x4100, r.ebx = 0x55AA, r.edx = drive;
    if (!b.no_lba && bios(&r) == 0 && (r.ebx & 0xFFFF) == 0xAA55 && (r.ecx & 1)) {
        u8 *p = vm_ptr((u32)b.seg << 4);
        memset(p, 0, 0x42);
        p[0] = 0x1A;                            /* the size of the answer wanted: the sectors, as EDD 1.x has them */
        memset(&r, 0, sizeof r);
        r.eax = 0x4800, r.edx = drive, r.ds = b.seg;
        if (bios(&r) == 0) {
            u32 lo = vm_rd32(((u32)b.seg << 4) + 0x10), hi = vm_rd32(((u32)b.seg << 4) + 0x14);
            g->edd = 1;
            if (hi)
                lo = 0xFFFFFFFFu;
            if (lo > g->sectors)
                g->sectors = lo;
        }
    }
    return 0;
}

/* A BIOS read or write, again after a reset if it fails. */
static int tried(struct rmregs *want, u8 drive)
{
    struct rmregs r;
    int e = 0, n;
    for (n = 0; n < 3; n++) {
        r = *want;
        if ((e = bios(&r)) == 0)
            return 0;
        memset(&r, 0, sizeof r);
        r.edx = drive;
        bios(&r);
    }
    return e;
}

static int rw(int write, u8 drive, u32 lba, u32 count, u8 *data)
{
    struct bdisk_geo *g = &b.known[slot(drive)];
    struct rmregs r;
    u16 dseg = (u16)(b.data_lin >> 4);
    int e;
    if (!g->sectors && (e = geo_ask(drive, g)) != 0)
        return e;
    if (!count || count > b.run || lba >= g->sectors || count > g->sectors - lba)
        return 0x01;                            /* (the BIOS's own word for a bad request) */
    if (write)
        memcpy(vm_ptr(b.data_lin), data, count * 512);
    if (g->edd) {
        u8 *p = vm_ptr((u32)b.seg << 4);
        memset(p, 0, 16);
        p[0] = 16, p[2] = (u8)count, p[6] = (u8)dseg, p[7] = (u8)(dseg >> 8);
        p[8] = (u8)lba, p[9] = (u8)(lba >> 8), p[10] = (u8)(lba >> 16), p[11] = (u8)(lba >> 24);
        memset(&r, 0, sizeof r);
        r.eax = write ? 0x4300 : 0x4200, r.edx = drive, r.ds = b.seg;
        if ((e = tried(&r, drive)) != 0)
            return e;
    } else {
        u32 off = 0, left = count, track = (u32)g->heads * g->spt;
        while (left) {                          /* a track's rest at a time */
            u32 c = lba / track, t = lba % track, h = t / g->spt, s = t % g->spt, k = g->spt - s;
            if (k > left)
                k = left;
            memset(&r, 0, sizeof r);
            r.eax = (write ? 0x0300u : 0x0200u) | k;
            r.ecx = ((c & 0xFF) << 8) | ((c >> 2) & 0xC0) | (s + 1);
            r.edx = (h << 8) | drive;
            r.es = dseg, r.ebx = off;
            if ((e = tried(&r, drive)) != 0)
                return e;
            lba += k, left -= k, off += k * 512;
        }
    }
    if (!write)
        memcpy(data, vm_ptr(b.data_lin), count * 512);
    return 0;
}

int bdisk_vm_pending(void) { return b.pending || (b.free_ask && b.seg); }

void bdisk_vm_turn(void)
{
    u32 f;
    int r;
    if (b.free_ask) {
        buffer_free();
        b.free_ask = 0;
    }
    f = irq_save();
    if (!b.pending) {
        irq_restore(f);
        return;
    }
    b.pending = 0, b.taken = 1;
    irq_restore(f);
    if (!b.seg && buffer_take() != 0) {
        r = BDISK_MEMORY;
    } else if (b.op == OP_GEO) {
        r = geo_ask(b.drive, b.geo);
        b.known[slot(b.drive)] = *b.geo;
    } else {
        r = rw(b.op == OP_WRITE, b.drive, b.lba, b.count, b.data);
    }
    f = irq_save();
    b.result = r;
    b.done = 1;
    thread_wake(&b.waitq);
    irq_restore(f);
}

/* ---- the asking thread's side */

static int ask(u8 op, u8 drive, u32 lba, u32 count, u8 *data, struct bdisk_geo *geo)
{
    u32 f, until;
    int r;
    if (slot(drive) < 0)
        return BDISK_NONE;
    f = irq_save();
    while (b.busy)
        thread_block(&b.users);
    b.busy = 1;
    b.op = op, b.drive = drive, b.lba = lba, b.count = count, b.data = data, b.geo = geo;
    b.taken = b.done = 0;
    b.pending = 1;
    vm_kick();
    until = (timer_ticks() + TAKE_TICKS) | 1;
    while (!b.done) {
        if (!thread_wait(&b.waitq, b.taken ? 0 : until) && !b.taken && !b.done) {
            b.pending = 0;                      /* a program has the system VM: not now */
            b.result = BDISK_BUSY;
            break;
        }
    }
    r = b.result;
    b.busy = 0;
    thread_wake(&b.users);
    irq_restore(f);
    return r;
}

void bdisk_open(void)
{
    u32 f = irq_save();
    b.opens++;
    irq_restore(f);
}

void bdisk_close(void)
{
    u32 f = irq_save();
    if (b.opens && !--b.opens) {
        b.free_ask = 1;
        vm_kick();
    }
    irq_restore(f);
}

int bdisk_geo(u8 drive, struct bdisk_geo *g) { return ask(OP_GEO, drive, 0, 0, 0, g); }

u32 bdisk_run(void) { return b.run ? b.run : 64; }

int bdisk_read(u8 drive, u32 lba, u32 count, void *data) { return ask(OP_READ, drive, lba, count, data, 0); }

int bdisk_write(u8 drive, u32 lba, u32 count, const void *data)
{
    return ask(OP_WRITE, drive, lba, count, (u8 *)data, 0);
}

/* ---- glos disktest (a stopgap for the tests until the shell's DISK and DD: S3)
 *   list                         the hard disks, and each one's partitions
 *   lba off | lba on             the BIOS's LBA calls left alone (cylinder, head and sector, as on a BIOS
 *                                that has none), or used again
 *   sum DISK LBA COUNT           Adler-32 of those sectors
 *   copy DISK LBA COUNT TO LBA   those sectors to another disk: only to a second disk (81h on) that has no
 *                                partition on it, so nothing of anybody's is ever written over by a test
 * DISK and TO in hexadecimal, the rest decimal. The lines are the log's. */

struct job {
    char what;
    u8 from, to;
    u32 lba, count, to_lba;
};

static u32 adler(u32 a, const u8 *p, u32 n)
{
    u32 lo = a & 0xFFFF, hi = a >> 16;
    while (n--) {
        lo = (lo + *p++) % 65521;
        hi = (hi + lo) % 65521;
    }
    return hi << 16 | lo;
}

/* The disk's partitions in use: -1 without a table, as mbr_parse has it; BIOS errors below -1. */
static int table(u8 drive, u8 *sector, struct mbr_part *part)
{
    int e = bdisk_read(drive, 0, 1, sector);
    return e ? -100 - (e < 0 ? -e : e) : mbr_parse(sector, part);
}

static void worker(void *arg)
{
    struct job *j = arg;
    struct bdisk_geo g;
    struct mbr_part part[4];
    u8 *buf = kmalloc(64 * 512);
    u32 run, done = 0, runs = 0, sum = 1, k;
    int e = 0, n, i;
    u8 d;
    bdisk_open();
    if (!buf) {
        kprintf("GLOS-DISKTEST error=memory\n");
    } else if (j->what == 'l') {
        for (d = 0x80; d < 0x84 && bdisk_geo(d, &g) == 0; d++) {
            n = table(d, buf, part);
            kprintf("GLOS-DISKTEST disk=%02x sectors=%u chs=%u/%u/%u edd=%u parts=%d\n", d, g.sectors, g.cyls, g.heads,
                    g.spt, g.edd, n);
            for (i = 0; n > 0 && i < 4; i++)
                if (part[i].type)
                    kprintf("GLOS-DISKTEST part disk=%02x n=%d type=%02x active=%u start=%u sectors=%u\n", d, i + 1,
                            part[i].type, part[i].active, (u32)part[i].start, (u32)part[i].sectors);
        }
        kprintf("GLOS-DISKTEST list done disks=%u\n", (u32)(d - 0x80));
    } else if (j->what == 'c' && (j->to < 0x81 || j->to == j->from || table(j->to, buf, part) > 0)) {
        kprintf("GLOS-DISKTEST refused to=%02x why=%s\n", j->to,
                j->to < 0x81 || j->to == j->from ? "not-a-second-disk" : "has-partitions");
    } else {
        if ((e = bdisk_geo(j->from, &g)) == 0 && j->what == 'c')
            e = bdisk_geo(j->to, &g);
        run = bdisk_run();
        while (!e && done < j->count) {
            k = j->count - done < run ? j->count - done : run;
            if ((e = bdisk_read(j->from, j->lba + done, k, buf)) != 0)
                break;
            sum = adler(sum, buf, k * 512);
            if (j->what == 'c' && (e = bdisk_write(j->to, j->to_lba + done, k, buf)) != 0)
                break;
            done += k, runs++;
        }
        if (e)
            kprintf("GLOS-DISKTEST error=%d at=%u\n", e, done);
        else if (j->what == 'c')
            kprintf("GLOS-DISKTEST copy from=%02x lba=%u to=%02x lba=%u sectors=%u runs=%u adler=%08x\n", j->from,
                    j->lba, j->to, j->to_lba, done, runs, sum);
        else
            kprintf("GLOS-DISKTEST sum disk=%02x lba=%u sectors=%u runs=%u adler=%08x\n", j->from, j->lba, done, runs, sum);
    }
    bdisk_close();
    kfree(buf);
    kfree(j);
    thread_exit();
}

static int number(const char **s, u32 *v, u32 base)
{
    const char *p = *s;
    u32 n = 0, any = 0;
    while (*p == ' ')
        p++;
    for (;; p++, any = 1) {
        u32 c = (u8)*p, digit = c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : 99;
        if (digit >= base)
            break;
        n = n * base + digit;
    }
    *s = p, *v = n;
    return any ? 0 : -1;
}

int bdisk_test(const char *args)
{
    struct job *j = kmalloc(sizeof *j);
    const char *p = args;
    u32 from = 0, to = 0;
    int ok;
    if (!j)
        return -1;
    memset(j, 0, sizeof *j);
    if (!strcmp(args, "lba off") || !strcmp(args, "lba on")) {
        u32 f = irq_save();
        b.no_lba = args[5] == 'f';
        memset(b.known, 0, sizeof b.known);
        irq_restore(f);
        kprintf("GLOS-DISKTEST lba=%s\n", b.no_lba ? "off" : "on");
        kfree(j);
        return 0;
    }
    if (!strcmp(args, "list")) {
        j->what = 'l';
        ok = 1;
    } else if (!strncmp(args, "sum ", 4)) {
        p = args + 4;
        j->what = 's';
        ok = number(&p, &from, 16) == 0 && number(&p, &j->lba, 10) == 0 && number(&p, &j->count, 10) == 0 && !*p;
    } else if (!strncmp(args, "copy ", 5)) {
        p = args + 5;
        j->what = 'c';
        ok = number(&p, &from, 16) == 0 && number(&p, &j->lba, 10) == 0 && number(&p, &j->count, 10) == 0
             && number(&p, &to, 16) == 0 && number(&p, &j->to_lba, 10) == 0 && !*p;
    } else {
        ok = 0;
    }
    j->from = (u8)from, j->to = (u8)to;
    if (!ok || from > 0xFF || to > 0xFF || !thread_create("disktest", PRIO_NORMAL, worker, j)) {
        kfree(j);
        return -1;
    }
    return 0;
}
