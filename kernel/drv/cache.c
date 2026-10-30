/* The disk cache (docs/disk-cache.md): what DOS reads from the hard disks,
 * kept in memory nobody else is using.
 *
 * Every INT 13h is trapped while the cache is on: from V86 code (soft_int)
 * and from protected mode (rm_call). The cache makes the BIOS calls itself,
 * through rm_call on the caller's stack, as the INT would have, so GLOS
 * knows when the BIOS is busy (cache_busy: no kill and no posted work then;
 * supervisor.md §17.2). A read of sectors it holds is copied straight into
 * the caller's buffer and the BIOS never runs. A read of fewer than 8
 * sectors with any missing reads each 4 KB block it touches whole, into the
 * block buffer the stub keeps (4 KB below 1 MB), so a cold walk of a FAT
 * chain takes an eighth of the calls; a bigger read reads its missing runs
 * straight into the caller's buffer. A write goes to the disk first: its
 * range is dropped before the call and kept from the caller's buffer once
 * it has succeeded, so nothing is ever written late and a call that never
 * comes back (rm_nest_end) leaves nothing stale.
 *
 * Nothing else may write a cached disk behind the cache's back: the IDE
 * command registers are trapped outside its own calls, and a command that
 * may write drops everything; a direct-mode session (its protected-mode
 * code at IOPL 3 reaches the ports untrapped) bypasses the cache and drops
 * everything when it ends; with SMARTDRV there at start the cache stays off.
 *
 * Memory: frames from the top (pmm_alloc_high), given back the moment
 * anything else needs one (cache_reclaim, from kernel/mm/vmm.c). Its tables
 * are changed, and its blocks copied, with interrupts off, so the reclaims
 * (also with interrupts off) never see a block half done. */
#include <string.h>

#include "glos/bootinfo.h"
#include "arch.h"
#include "cache.h"
#include "cstore.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "pci.h"
#include "sched.h"
#include "vm.h"

#define KEEP_FREE   512u                        /* frames the cache leaves free (2 MB) */
#define HIST_BITS   18                          /* count mode: up to 256K sectors */
#define VERIFY_MAX  (127u * 512u)

struct drive {
    u8 dl, idx, edd, nofill;
    u8 al_zero;                                 /* its BIOS answers a CHS read with AL 0, not the count (86Box's) */
    u8 al_known;                                /* learnt: until then a CHS read goes to the BIOS */
    struct cgeo g;
    u32 sectors;
};

struct call {                                   /* how the caller's INT was made: the cache's calls are the same */
    struct trapframe *from;
    int kind;
    u16 cs, ip, ss, sp, flags;
};

struct req {                                    /* a decoded read or write */
    struct drive *d;
    u32 ah, lba, n, buf, dap;                   /* dap: the caller's (42h/43h), 0 for CHS */
};

struct hent { u32 key, reads; };

static struct {
    u8 mode, trapped, direct, inside, ready, ports_on;
    u8 want;                                    /* a mode asked for while the BIOS was busy (0xFF: none) */
    int busy_at;                                /* rm_nesting() of the cache's BIOS call in progress; 0 none */
    u32 gen;                                    /* a drop of everything: a request started before keeps nothing */
    u32 buf;                                    /* the block buffer (linear), its DAP at buf + 4096; 0 none */
    u32 cap, frames;                            /* frames at most, held */
    struct drive d[BI_DISKS];
    u32 nd;
    u16 ports[4];
    u32 nports;
    struct cstore s;
    struct hent *hist;                          /* count mode */
    u8 *vbuf;                                   /* verify mode: the cache's answer, to compare */
    u32 hits, misses, hit_sectors, miss_sectors, write_sectors, fills, drops, reclaims, mismatches, calls;
    u32 hist_sectors, hist_distinct, hist_lost;
} c;

static const char *const mode_name[] = { "off", "on", "verify", "count" };

static void set_mode(u32 m);

/* ---- the tables */

static int tables(void)
{
    u32 n = pmm_frames(), hb = 10, dbytes, hbytes;
    if (c.ready)
        return 0;
    while ((1u << hb) < n / 2 && hb < 20)
        hb++;
    dbytes = (n * sizeof(struct cblk) + 0xFFF) & ~0xFFFu;
    hbytes = (4u << hb) + 0xFFF;
    if (!mm_cache_map(0, dbytes + hbytes)) {
        kprintf("GLOS-CACHE off reason=memory frames=%u\n", n);
        return -1;
    }
    cs_init(&c.s, (struct cblk *)CACHE_BASE, n, (u32 *)(CACHE_BASE + dbytes), hb);
    c.ready = 1;
    return 0;
}

static u32 hist_off(void) { return (((pmm_frames() * sizeof(struct cblk) + 0xFFF) & ~0xFFFu) + (4u << 20)) & ~0xFFFu; }

static void release(u32 f)                      /* interrupts off */
{
    cs_remove(&c.s, f);
    pmm_free(f << 12);
    c.frames--;
}

/* Both walk the segments' lists, not the tables: with interrupts off, as long
   as the blocks held, not the frames there are. */
static void drop_all(void)
{
    u32 fl = irq_save(), seg;
    c.gen++;
    if (c.ready)
        for (seg = 0; seg < 2; seg++)
            while (c.s.tail[seg] != CS_NIL)
                release(c.s.tail[seg]);
    irq_restore(fl);
}

void cache_drop(const char *why)
{
    u32 had = c.frames, f;
    drop_all();
    if (c.hist && !strcmp(why, "command")) {    /* glos cache drop: count mode starts again too */
        f = irq_save();
        memset(c.hist, 0, sizeof(struct hent) << HIST_BITS);
        c.hist_sectors = c.hist_distinct = c.hist_lost = 0;
        irq_restore(f);
    }
    c.drops++;
    kprintf("GLOS-CACHE drop reason=%s blocks=%u\n", why, had);
}

/* A frame for a new block: a spare one while under the cap, else the coldest block's. Interrupts off. */
static u32 new_block(u32 key)
{
    u32 f = CS_NIL, p = 0;
    if (c.frames < c.cap && (p = pmm_alloc_high(KEEP_FREE)) != 0 && (p >> 12) < c.s.nframes) {
        f = p >> 12;
        c.frames++;
    } else if (p && (p >> 12) >= c.s.nframes) {
        pmm_free(p);                            /* (past the RAM's end the tables cover: never, as frames go) */
    } else if ((f = cs_victim(&c.s)) != CS_NIL) {
        cs_remove(&c.s, f);
    }
    if (f != CS_NIL)
        cs_insert(&c.s, key, f);
    return f;
}

static u8 bits(u32 first, u32 n) { return (u8)(((1u << n) - 1) << first); }

/* Sectors [lba, lba + n) of drive di from linear src into the cache, unless everything was dropped since gen. */
static void keep(u32 di, u32 lba, u32 n, u32 src, u32 gen)
{
    while (n) {
        u32 first = lba & 7, k = 8 - first, f, fl;
        u8 *p;
        if (k > n)
            k = n;
        fl = irq_save();
        if (gen == c.gen && (c.mode == BI_CACHE_ON || c.mode == BI_CACHE_VERIFY) && !c.direct) {
            if ((f = cs_find(&c.s, CS_KEY(di, lba))) == CS_NIL)
                f = new_block(CS_KEY(di, lba));
            if (f != CS_NIL) {
                p = kmap(f << 12);
                memcpy(p + first * 512, vm_ptr(src), k * 512);
                kunmap(p);
                c.s.b[f].valid |= bits(first, k);
            }
        }
        irq_restore(fl);
        lba += k;
        n -= k;
        src += k * 512;
    }
}

/* Sectors [lba, lba + n) of drive di dropped (before a write). */
static void forget(u32 di, u32 lba, u32 n)
{
    while (n) {
        u32 first = lba & 7, k = 8 - first, f, fl;
        if (k > n)
            k = n;
        fl = irq_save();
        if (c.ready && (f = cs_find(&c.s, CS_KEY(di, lba))) != CS_NIL) {
            c.s.b[f].valid &= (u8)~bits(first, k);
            if (!c.s.b[f].valid)
                release(f);
        }
        irq_restore(fl);
        lba += k;
        n -= k;
    }
}

static void forget_drive(u32 di)
{
    u32 f, prev, fl = irq_save(), seg;
    if (c.ready)
        for (seg = 0; seg < 2; seg++)
            for (f = c.s.tail[seg]; f != CS_NIL; f = prev) {
                prev = c.s.b[f].prev;
                if (CS_DRIVE(c.s.b[f].key) == di)
                    release(f);
            }
    irq_restore(fl);
}

/* Is sector lba of drive di here? (Only a hint: another thread wanting memory may take its block back before
   give() copies it, which give() then says.) */
static int have(u32 di, u32 lba)
{
    u32 fl = irq_save(), f = c.ready ? cs_find(&c.s, CS_KEY(di, lba)) : CS_NIL;
    int r = f != CS_NIL && (c.s.b[f].valid >> (lba & 7) & 1);
    irq_restore(fl);
    return r;
}

/* Sectors [lba, lba + n) to linear dst; promote: they were here before this request. 0, or -1 if one of them
   has gone meanwhile (reclaimed): the caller reads them from the disk instead. */
static int give(u32 di, u32 lba, u32 n, u32 dst, int promote)
{
    u32 last = CS_NIL;
    while (n) {
        u32 first = lba & 7, k = 8 - first, f, fl;
        u8 *p;
        if (k > n)
            k = n;
        fl = irq_save();
        f = cs_find(&c.s, CS_KEY(di, lba));
        if (f == CS_NIL || (c.s.b[f].valid & bits(first, k)) != bits(first, k)) {
            irq_restore(fl);
            return -1;
        }
        p = kmap(f << 12);
        memcpy(vm_ptr(dst), p + first * 512, k * 512);
        kunmap(p);
        if (promote && f != last)
            cs_hit(&c.s, f);
        last = f;
        irq_restore(fl);
        lba += k;
        n -= k;
        dst += k * 512;
    }
    return 0;
}

/* ---- the BIOS calls */

static void ports(int on)
{
    u32 i;
    if (!c.ports_on)
        return;
    for (i = 0; i < c.nports; i++)
        cpu_io_trap(c.ports[i], on);
}

/* One BIOS call made as the caller's INT was. 0, the BIOS's error (AH) + 100h, or 1FFh if it can't be made. */
static u32 bios(const struct call *k, struct rmregs *r)
{
    int rc;
    r->ss = k->ss;
    r->sp = k->sp;
    r->flags = k->flags;
    if (k->kind == RM_IRET) {
        r->cs = k->cs;
        r->ip = k->ip;
    }
    ports(0);
    c.inside = 1;
    c.busy_at = rm_nesting() + 1;
    rc = rm_call(k->from, r, k->kind, 0x13, 0, 0);
    c.busy_at = 0;
    c.inside = 0;
    ports(1);
    c.calls++;
    if (rc != 0)
        return 0x1FF;
    return (r->flags & FL_CF) ? 0x100 | ((r->eax >> 8) & 0xFF) : 0;
}

/* The caller's own call, unchanged; its registers back into cr. */
static u32 original(const struct call *k, struct rmregs *cr)
{
    struct rmregs r = *cr;
    u32 e = bios(k, &r);
    if (e != 0x1FF) {
        cr->eax = r.eax, cr->ebx = r.ebx, cr->ecx = r.ecx, cr->edx = r.edx;
        cr->esi = r.esi, cr->edi = r.edi, cr->ebp = r.ebp;
        cr->ds = r.ds, cr->es = r.es, cr->flags = (u16)((cr->flags & ~FL_ARITH) | (r.flags & FL_ARITH));
    }
    return e;
}

static void put16(u32 lin, u32 v) { vm_wr8(lin, (u8)v); vm_wr8(lin + 1, (u8)(v >> 8)); }
static void put32(u32 lin, u32 v) { put16(lin, v); put16(lin + 2, v >> 16); }

static void seg_off(u32 lin, u32 *seg, u32 *off)       /* a far pointer to lin (FFFF:xxxx in the HMA) */
{
    if (lin >= 0xFFFF0) {
        *seg = 0xFFFF;
        *off = lin - 0xFFFF0;
    } else {
        *seg = lin >> 4;
        *off = lin & 15;
    }
}

/* What the drive's BIOS puts in AL after a CHS read that worked: the count (as specified) or 0 (some do).
   Learnt from its own answers, so that the cache's are the same. */
static void learn(struct drive *d, u32 al, u32 n)
{
    if (n && !al)
        d->al_zero = 1, d->al_known = 1;
    else if (al == n)
        d->al_zero = 0, d->al_known = 1;
}

/* Sectors [lba, lba + n) of q's drive to linear dst, by one BIOS call; *got: how many it did. */
static u32 read_to(const struct call *k, const struct req *q, const struct rmregs *cr, u32 lba, u32 n, u32 dst, u32 *got)
{
    struct rmregs r;
    u32 e, seg, off, cx, dh;
    memset(&r, 0, sizeof r);
    seg_off(dst, &seg, &off);
    if (q->dap) {                               /* the caller's DAP, for this run */
        put16(q->dap + 2, n);
        put16(q->dap + 4, off);
        put16(q->dap + 6, seg);
        put32(q->dap + 8, lba);
        put32(q->dap + 12, 0);
        r.eax = 0x4200;
        r.ds = cr->ds;
        r.esi = cr->esi;
    } else {
        cs_lba_chs(&q->d->g, lba, &cx, &dh);
        r.eax = 0x0200 | n;
        r.ecx = cx;
        r.es = (u16)seg;
        r.ebx = off;
        dh <<= 8;
        r.edx = dh;
    }
    r.edx |= q->d->dl;
    e = bios(k, &r);
    if (!e && !q->dap)
        learn(q->d, r.eax & 0xFF, n);
    *got = e ? (q->dap ? vm_rd16(q->dap + 2) : (r.eax & 0xFF)) : n;
    if (*got > n)
        *got = 0;
    return e;
}

/* A whole block into the block buffer and kept: 0, or the BIOS's error. */
static u32 fill(const struct call *k, struct drive *d, u32 blk, u32 gen)
{
    struct rmregs r;
    u32 n = d->sectors - blk < 8 ? d->sectors - blk : 8, e, cx, dh, dap = c.buf + 4096;
    memset(&r, 0, sizeof r);
    if (d->edd) {
        vm_wr8(dap, 16);
        vm_wr8(dap + 1, 0);
        put16(dap + 2, n);
        put16(dap + 4, c.buf & 15);
        put16(dap + 6, c.buf >> 4);
        put32(dap + 8, blk);
        put32(dap + 12, 0);
        r.eax = 0x4200;
        r.ds = (u16)(dap >> 4);
        r.esi = dap & 15;
    } else {
        cs_lba_chs(&d->g, blk, &cx, &dh);
        r.eax = 0x0200 | n;
        r.ecx = cx;
        r.edx = dh << 8;
        r.es = (u16)(c.buf >> 4);
        r.ebx = c.buf & 15;
    }
    r.edx |= d->dl;
    e = bios(k, &r);
    if (e) {
        if ((e & 0xFF) == 0x09 && !d->nofill) {
            d->nofill = 1;
            kprintf("GLOS-CACHE nofill dl=%02x why=dma-boundary\n", d->dl);
        }
        return e;
    }
    if (!d->edd)
        learn(d, r.eax & 0xFF, n);
    keep(d->idx, blk, n, c.buf, gen);
    c.fills++;
    return 0;
}

/* ---- requests */

static struct drive *drive(u32 dl)
{
    u32 i;
    for (i = 0; i < c.nd; i++)
        if (c.d[i].dl == dl)
            return &c.d[i];
    return 0;
}

/* A read or write the cache understands exactly: 1, else 0 (passed on as it is). */
static int decode(const struct rmregs *r, struct req *q)
{
    u32 ah = (r->eax >> 8) & 0xFF, lba, n, seg, off;
    struct drive *d = drive(r->edx & 0xFF);
    if (!d || (ah != 0x02 && ah != 0x03 && ah != 0x42 && ah != 0x43))
        return 0;
    q->d = d;
    q->ah = ah;
    if (ah == 0x02 || ah == 0x03) {
        n = r->eax & 0xFF;
        if (cs_chs_lba(&d->g, r->ecx & 0xFFFF, (r->edx >> 8) & 0xFF, &lba) != 0)
            return 0;
        seg = r->es, off = r->ebx & 0xFFFF;
        q->dap = 0;
    } else {
        u32 dap = vm_lin(r->ds, r->esi), size = vm_rd8(dap);
        if (!d->edd || size < 16 || vm_rd32(dap + 12) != 0)
            return 0;
        n = vm_rd16(dap + 2);
        off = vm_rd16(dap + 4), seg = vm_rd16(dap + 6);
        if (seg == 0xFFFF && off == 0xFFFF)     /* (EDD 3.0's 64-bit flat buffer) */
            return 0;
        lba = vm_rd32(dap + 8);
        q->dap = dap;
    }
    if (n < 1 || n > 127 || lba >= d->sectors || n > d->sectors - lba || off + n * 512 > 0x10000)
        return 0;
    q->lba = lba;
    q->n = n;
    q->buf = (seg << 4) + off;
    return 1;
}

static void result(const struct req *q, struct rmregs *cr, u32 e, u32 done)
{
    cr->eax = (cr->eax & 0xFFFF0000u) | ((e & 0xFF) << 8)
              | (q->dap ? (cr->eax & 0xFF) : !e && q->d->al_zero ? 0 : (done & 0xFF));
    if (e)
        cr->flags |= FL_CF;
    else
        cr->flags &= (u16)~FL_CF;
    if (q->dap)
        put16(q->dap + 2, done);
    vm_wr8(0x474, (u8)e);                       /* the BIOS's last status (0040:0074) */
}

/* A read: hits, block fills, runs. 0, or the error (and *done) it ends with. */
static u32 do_read(const struct call *k, const struct req *q, struct rmregs *cr, u32 gen, u32 *done)
{
    u32 i, j, e, got, di = q->d->idx, blk, all = 1, save[4] = { 0 };
    if (!q->dap && !q->d->al_known) {           /* a CHS read before the BIOS's AL is known: its own answer */
        *done = 0;
        return 0x10000;
    }
    for (i = 0; i < q->n && all; i++)
        all = have(di, q->lba + i);
    if (all && give(di, q->lba, q->n, q->buf, 1) == 0) {
        c.hits++;
        c.hit_sectors += q->n;
        *done = q->n;
        return 0;
    }
    c.misses++;
    if (q->n < 8 && c.buf && !q->d->nofill) {   /* small: whole blocks through the block buffer */
        for (blk = q->lba & ~7u; blk < q->lba + q->n; blk += 8) {
            for (i = blk, all = 1; i < blk + 8 && i < q->d->sectors && all; i++)
                all = have(di, i);
            if (!all && (e = fill(k, q->d, blk, gen)) != 0) {
                *done = 0;
                return e | 0x10000;             /* (the caller's own call next, unchanged) */
            }
            c.miss_sectors += all ? 0 : 8;
        }
        for (i = 0; i < q->n; i++)
            if (!have(di, q->lba + i)) {        /* (dropped meanwhile: everything was) */
                *done = 0;
                return 0x10000;
            }
        if (give(di, q->lba, q->n, q->buf, 0) != 0) {
            *done = 0;
            return 0x10000;                     /* (taken back meanwhile: the caller's own call) */
        }
        *done = q->n;
        return 0;
    }
    if (q->dap)                                 /* the caller's DAP, put back after the runs */
        save[0] = vm_rd16(q->dap + 4), save[1] = vm_rd16(q->dap + 6), save[2] = vm_rd32(q->dap + 8),
        save[3] = vm_rd16(q->dap + 2);
    for (i = 0, e = 0; i < q->n && !e;) {
        if (have(di, q->lba + i)) {
            for (j = i; j < q->n && have(di, q->lba + j); j++) ;
            if (give(di, q->lba + i, j - i, q->buf + i * 512, 1) == 0) {
                i = j;
                continue;
            }                                   /* (some taken back meanwhile: these from the disk) */
        } else {
            for (j = i; j < q->n && !have(di, q->lba + j); j++) ;
        }
        e = read_to(k, q, cr, q->lba + i, j - i, q->buf + i * 512, &got);
        if (!e) {
            keep(di, q->lba + i, j - i, q->buf + i * 512, gen);
            c.miss_sectors += j - i;
        }
        i += e ? got : j - i;
    }
    if (q->dap) {
        put16(q->dap + 2, save[3]);
        put16(q->dap + 4, save[0]);
        put16(q->dap + 6, save[1]);
        put32(q->dap + 8, save[2]);
    }
    *done = i;
    return e;
}

static void hist_add(u32 di, u32 lba, u32 n)
{
    u32 mask = (1u << HIST_BITS) - 1, key, h;
    for (; n; n--, lba++) {
        key = ((di << 30) | (lba & 0x3FFFFFFFu)) + 1;
        for (h = (key * 2654435761u) >> (32 - HIST_BITS);; h = (h + 1) & mask) {
            if (c.hist[h].key == key) {
                c.hist[h].reads++;
                break;
            }
            if (!c.hist[h].key) {
                if (c.hist_distinct >= mask - mask / 8) {
                    c.hist_lost++;
                    break;
                }
                c.hist[h].key = key;
                c.hist[h].reads = 1;
                c.hist_distinct++;
                break;
            }
        }
        c.hist_sectors++;
    }
}

int cache_int13(struct trapframe *from, struct rmregs *cr, int kind)
{
    struct call k;
    struct req q;
    u32 e, done, gen = c.gen, mode = c.direct ? BI_CACHE_OFF : c.mode;

    if (!c.trapped || c.inside || current != vm.thread)
        return 0;
    if (c.want != 0xFF) {
        set_mode(c.want);
        c.want = 0xFF;
        kprintf("GLOS-CACHE mode=%s\n", mode_name[c.mode]);
        if (!c.trapped)
            return 0;
        gen = c.gen, mode = c.direct ? BI_CACHE_OFF : c.mode;
    }
    k.from = from, k.kind = kind, k.cs = cr->cs, k.ip = cr->ip, k.ss = cr->ss, k.sp = cr->sp, k.flags = cr->flags;
    if (!decode(cr, &q) || mode == BI_CACHE_OFF) {
        if (mode != BI_CACHE_OFF && drive(cr->edx & 0xFF)) {
            u32 ah = (cr->eax >> 8) & 0xFF;     /* writes the cache can't follow: their drive's blocks go */
            if (ah == 0x03 || ah == 0x43 || ah == 0x05 || ah == 0x07 || ah == 0x0B || ah == 0x0F)
                forget_drive(drive(cr->edx & 0xFF)->idx);
        }
        if (original(&k, cr) == 0x1FF)
            return 0;
        return 1;
    }
    if (mode == BI_CACHE_COUNT) {
        if (q.ah == 0x02 || q.ah == 0x42)
            hist_add(q.d->idx, q.lba, q.n);
        if (original(&k, cr) == 0x1FF)
            return 0;
        return 1;
    }
    if (q.ah == 0x03 || q.ah == 0x43) {         /* a write: the range goes first, comes back once written */
        forget(q.d->idx, q.lba, q.n);
        e = original(&k, cr);
        if (e == 0x1FF)
            return 0;
        if (!e) {
            keep(q.d->idx, q.lba, q.n, q.buf, gen);
            c.write_sectors += q.n;
        }
        return 1;
    }
    e = do_read(&k, &q, cr, gen, &done);
    if (e & 0x10000) {                          /* the caller's call as it was (a block fill failed, a block was
                                                   taken back, or a CHS read before AL is learnt): kept if it worked */
        e = original(&k, cr);
        if (e == 0x1FF)
            return 0;
        if (!e) {
            if (q.ah == 0x02)
                learn(q.d, cr->eax & 0xFF, q.n);
            keep(q.d->idx, q.lba, q.n, q.buf, gen);
        }
        return 1;
    }
    if (e == 0x1FF)
        return 0;
    if (mode == BI_CACHE_VERIFY && !e && c.vbuf) {      /* the disk's answer as well, compared */
        u32 i, len = q.n * 512, f;
        memcpy(c.vbuf, vm_ptr(q.buf), len);
        e = original(&k, cr);
        if (!e && q.ah == 0x02)
            learn(q.d, cr->eax & 0xFF, q.n);
        if (!e) {
            for (i = 0; i < q.n; i++)
                if (memcmp(c.vbuf + i * 512, vm_ptr(q.buf + i * 512), 512)) {
                    if (c.mismatches++ < 32)
                        kprintf("GLOS-CACHE mismatch dl=%02x lba=%u\n", q.d->dl, q.lba + i);
                    f = irq_save();
                    forget(q.d->idx, q.lba + i, 1);
                    irq_restore(f);
                }
        }
        return 1;
    }
    result(&q, cr, e & 0xFF, done);
    return 1;
}

int cache_busy(void) { return c.busy_at && rm_nesting() >= c.busy_at; }

void cache_nest_ended(int depth)
{
    if (c.busy_at && c.busy_at >= depth) {      /* the cache's call is gone with them: what it left is dropped */
        c.busy_at = 0;
        c.inside = 0;
        ports(1);
    }
}

/* ---- the port watch */

static int harmless(u8 cmd)                     /* ATA commands that never change what a disk holds */
{
    switch (cmd) {
    case 0x20: case 0x21: case 0x24: case 0x25: case 0x27: case 0x29: case 0x40: case 0x41: case 0x42: case 0x70:
    case 0x90: case 0x91: case 0xA0: case 0xA1: case 0x08: case 0xC4: case 0xC6: case 0xC8: case 0xC9:
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: case 0xE7: case 0xEA: case 0xEC:
    case 0xEF: case 0xF8:
        return 1;
    }
    return cmd >= 0x10 && cmd <= 0x1F;         /* recalibrate */
}

void cache_port_out(u16 port, u8 v)
{
    static u32 logged;
    u32 i;
    if (!c.ports_on || c.inside)
        return;
    for (i = 0; i < c.nports; i++)
        if (c.ports[i] == port && c.mode == BI_CACHE_COUNT) {      /* (C0: who else talks to the disks) */
            if (logged++ < 20)
                kprintf("GLOS-CACHE port port=%03x cmd=%02x%s\n", port, v, harmless(v) ? "" : " writes=1");
            return;
        } else if (c.ports[i] == port && !harmless(v)) {
            kprintf("GLOS-CACHE drop reason=port port=%03x cmd=%02x blocks=%u\n", port, v, c.frames);
            drop_all();
            c.drops++;
            return;
        }
}

static void watch(int on)
{
    u32 i;
    if (on == c.ports_on)
        return;
    for (i = 0; i < c.nports; i++)
        cpu_io_trap(c.ports[i], on);
    c.ports_on = (u8)on;
}

/* ---- modes */

static void trap13(int on)
{
    if (vm.vme)
        cpu_int_redirect(0x13, !on);            /* (without VME every INT traps; c.trapped decides) */
    c.trapped = (u8)on;
}

static void set_mode(u32 m)
{
    u32 f = irq_save();
    if (m != BI_CACHE_OFF && m != BI_CACHE_COUNT && tables() != 0)
        m = BI_CACHE_OFF;
    if (m == BI_CACHE_VERIFY && !c.vbuf && !(c.vbuf = kmalloc(VERIFY_MAX)))
        m = BI_CACHE_ON;
    if (m == BI_CACHE_COUNT && !c.hist) {
        c.hist = mm_cache_map(hist_off(), sizeof(struct hent) << HIST_BITS);
        if (!c.hist)
            m = BI_CACHE_OFF;
    }
    if (m != c.mode)
        drop_all();                             /* (writes went by unseen while it was off) */
    c.mode = (u8)m;
    trap13(m != BI_CACHE_OFF);
    watch(m != BI_CACHE_OFF);                   /* (count mode: logged, not dropped) */
    irq_restore(f);
}

void cache_direct(int on)
{
    if (!c.trapped || on == c.direct)
        return;
    c.direct = (u8)on;
    if (on) {
        watch(0);
    } else {
        cache_drop("direct");
        watch(c.mode != BI_CACHE_OFF);
    }
}

void cache_apply(const char *key, const char *v)
{
    u32 m;
    if (!strcmp(key, "enabled")) {
        m = !strcmp(v, "on") ? BI_CACHE_ON : !strcmp(v, "verify") ? BI_CACHE_VERIFY
            : !strcmp(v, "count") ? BI_CACHE_COUNT : BI_CACHE_OFF;
        if (m != c.mode && c.nd && c.busy_at) {
            c.want = (u8)m;                     /* (the VM thread, when it next comes to an INT 13h) */
        } else if (m != c.mode && c.nd) {
            set_mode(m);
            kprintf("GLOS-CACHE mode=%s\n", mode_name[c.mode]);
        }
    } else if (!strcmp(key, "size")) {
        u32 mb = 0, f;
        const char *p;
        for (p = v; *p >= '0' && *p <= '9'; p++)
            mb = mb * 10 + (u32)(*p - '0');
        f = irq_save();
        c.cap = mb ? mb << 8 : 0xFFFFFFFFu;
        while (c.frames > c.cap && (mb = cs_victim(&c.s)) != CS_NIL)
            release(mb);
        irq_restore(f);
    }
}

/* ---- start, end, numbers */

void cache_init(struct bootinfo *bi)
{
    u32 i, ok = 1;
    struct pci_dev *p;
    char list[64];
    u32 n = 0;

    c.cap = bi->cache_size_mb ? bi->cache_size_mb << 8 : 0xFFFFFFFFu;
    c.want = 0xFF;
    for (i = 0; i < bi->n_disks && c.nd < BI_DISKS; i++) {
        struct bi_disk *b = &bi->disks[i];
        struct drive *d = &c.d[c.nd];
        if (b->bps != 512 || (b->flags & BI_DISK_REMOVE) || b->sectors_hi || !b->heads || !b->spt)
            continue;
        d->dl = (u8)b->dl;
        d->idx = (u8)c.nd;
        d->edd = (b->flags & BI_DISK_EDD) ? 1 : 0;
        d->g.cyls = b->cyls, d->g.heads = b->heads, d->g.spt = b->spt;
        d->sectors = b->sectors_lo;             /* (48h's, or 08h's geometry's without the extensions) */
        n += (u32)ksnprintf(list + n, sizeof list - n, "%s%02x:%s:%u", c.nd ? "," : "", d->dl, d->edd ? "lba" : "chs",
                            d->sectors);
        c.nd++;
    }
    for (i = 0; (p = pci_at((int)i)) != 0; i++) {     /* which IDE command registers to watch */
        if (p->class != 0x01)
            continue;
        if (p->subclass == 0x01) {
            if (c.nports < 3)
                c.ports[c.nports++] = (p->progif & 1) && (p->bar[0] & 1) ? (u16)((p->bar[0] & ~3u) + 7) : 0x1F7;
            if (c.nports < 4)
                c.ports[c.nports++] = (p->progif & 4) && (p->bar[2] & 1) ? (u16)((p->bar[2] & ~3u) + 7) : 0x177;
        } else if (p->subclass != 0x02) {       /* SCSI, RAID, SATA (AHCI), others: writes GLOS can't see */
            ok = 0;
        }
    }
    if (!c.nports) {                            /* no PCI IDE function: ISA IDE at the usual ports */
        c.ports[c.nports++] = 0x1F7;
        c.ports[c.nports++] = 0x177;
    }
    if (bi->cache_mode == BI_CACHE_OFF)
        return;
    if (bi->smartdrv || !c.nd || !ok) {
        kprintf("GLOS-CACHE off reason=%s\n", bi->smartdrv ? "smartdrv" : !c.nd ? "no-disk" : "controller");
        return;
    }
    set_mode(bi->cache_mode);
    if (c.mode == BI_CACHE_OFF)
        return;
    kprintf("GLOS-CACHE start mode=%s drives=%s kb_max=%u ports=%03x,%03x%s\n", mode_name[c.mode], list,
            c.cap == 0xFFFFFFFFu ? (mm_avail_frames() - KEEP_FREE) * 4 : c.cap * 4, c.ports[0], c.ports[1],
            bi->cache_paras ? " buffer=4096" : "");
}

void cache_resident(u32 buf)
{
    c.buf = buf;
}

u32 cache_kb(void) { return c.frames * 4; }

u32 cache_stat(char *b, u32 max)
{
    u32 n, i;
    n = (u32)ksnprintf(b, max, "{\"mode\":\"%s\",\"direct\":%u,\"kb\":%u,\"blocks\":%u,\"protected\":%u,\"hits\":%u,"
                       "\"misses\":%u,\"hit_sectors\":%u,\"miss_sectors\":%u,\"write_sectors\":%u,\"fills\":%u,"
                       "\"drops\":%u,\"reclaims\":%u,\"mismatches\":%u,\"calls\":%u,\"buffer\":%u,"
                       "\"count\":{\"sectors\":%u,\"distinct\":%u,\"lost\":%u},\"drives\":[",
                       mode_name[c.mode], c.direct, c.frames * 4, c.ready ? cs_blocks(&c.s) : 0,
                       c.ready ? c.s.n[1] : 0, c.hits, c.misses, c.hit_sectors, c.miss_sectors, c.write_sectors,
                       c.fills, c.drops, c.reclaims, c.mismatches, c.calls, c.buf ? 4096 : 0, c.hist_sectors,
                       c.hist_distinct, c.hist_lost);
    for (i = 0; i < c.nd && n < max; i++)
        n += (u32)ksnprintf(b + n, max - n, "%s{\"dl\":%u,\"sectors\":%u,\"lba\":%u,\"fill\":%u}", i ? "," : "",
                            c.d[i].dl, c.d[i].sectors, c.d[i].edd, !c.d[i].nofill);
    if (n < max)
        n += (u32)ksnprintf(b + n, max - n, "]}\n");
    return n < max ? n : max;
}

/* Count mode's sectors: a summary, then the most read (up to 200): "dl lba reads" lines. */
u32 cache_lbas(char *b, u32 max)
{
    u32 n, i, size = 1u << HIST_BITS, re = 0, cnt[33], thr = 0, shown = 0;
    if (!c.hist)
        return (u32)ksnprintf(b, max, "glos cache lbas: count mode hasn't run\n");
    memset(cnt, 0, sizeof cnt);
    for (i = 0; i < size; i++)
        if (c.hist[i].key) {
            u32 r = c.hist[i].reads, lg = 0;
            re += r - 1;
            while (r > 1 && lg < 32) {
                r >>= 1;
                lg++;
            }
            cnt[lg]++;
        }
    n = (u32)ksnprintf(b, max, "sectors=%u distinct=%u reread=%u lost=%u\n", c.hist_sectors, c.hist_distinct, re,
                       c.hist_lost);
    for (thr = 33; thr > 0 && shown + cnt[thr - 1] <= 200; thr--)
        shown += cnt[thr - 1];
    for (i = 0; i < size && n + 32 < max; i++)
        if (c.hist[i].key && c.hist[i].reads >= (thr ? (1u << thr) : 1u) && c.hist[i].reads > 1)
            n += (u32)ksnprintf(b + n, max - n, "%02x %u %u\n", c.d[(c.hist[i].key - 1) >> 30].dl,
                                (c.hist[i].key - 1) & 0x3FFFFFFFu, c.hist[i].reads);
    return n;
}

void cache_end(void)
{
    if (c.mode == BI_CACHE_OFF && !c.hits && !c.misses)
        return;
    kprintf("GLOS-CACHE end mode=%s hits=%u misses=%u hit_sectors=%u miss_sectors=%u write_sectors=%u fills=%u "
            "drops=%u reclaims=%u mismatches=%u kb=%u counted=%u distinct=%u\n", mode_name[c.mode], c.hits, c.misses,
            c.hit_sectors, c.miss_sectors, c.write_sectors, c.fills, c.drops, c.reclaims, c.mismatches, c.frames * 4,
            c.hist_sectors, c.hist_distinct);
}

/* ---- memory (kernel/mm/vmm.c, interrupts off) */

u32 cache_frames(void) { return c.ready ? c.frames : 0; }

u32 cache_reclaim(u32 n)
{
    u32 got = 0, f;
    while (c.ready && got < n && (f = cs_victim(&c.s)) != CS_NIL) {
        release(f);
        got++;
        c.reclaims++;
    }
    return got;
}

int cache_frame_evictable(u32 f, void *arg)
{
    (void)arg;
    return c.ready && f < c.s.nframes && (c.s.b[f].flags & (CB_USED | CB_PINNED)) == CB_USED;
}

void cache_evict_frames(u32 first, u32 n)
{
    u32 f;
    for (f = first; f < first + n; f++)
        if (cache_frame_evictable(f, 0)) {
            release(f);
            c.reclaims++;
        }
}
