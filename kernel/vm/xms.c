/* GLOS's XMS 3.0 server (supervisor.md §16), reached through GLOS.EXE's
 * entry point (INT 2Fh 4310h under GLOS; an ARPL there traps here). Blocks
 * are contiguous runs of the kernel's frames, so a lock can hand out a
 * physical address. In XMS mode the driver's version and HMA state carry
 * over, and its existing handles stay its own: programs that kept its entry
 * point from before GLOS still reach it, and its moves (INT 15h 87h in V86
 * mode) are kept out of GLOS's memory. UMBs are not provided. */
#include "glos/bootinfo.h"
#include "kprintf.h"
#include "mm.h"
#include "vm.h"

#define NHANDLES 64
#define RESERVE  256                            /* frames XMS leaves the kernel: 1 MB */

struct emb {
    u32 phys, kb;
    u8 used, locks;
};

static struct emb emb[NHANDLES];
static struct bootinfo *bi;
static u16 ver = 0x0300, rev = 0x0001, hma_exists = 1;
static u8 hma_taken;                            /* 1 before GLOS (DOS=HIGH), 2 through GLOS */
static u8 a20_global;
static u32 a20_local;
static u8 bounce[PAGE_SIZE];

void xms_init(struct bootinfo *b)
{
    bi = b;
    if (bi->mode == BI_MODE_XMS && bi->xms_ver) {
        ver = (u16)bi->xms_ver;
        rev = (u16)bi->xms_rev;
        hma_exists = (u16)bi->xms_hma;
    }
    hma_taken = bi->hma_used ? 1 : 0;
    a20_global = bi->a20_initial ? 1 : 0;
}

/* ---- physical memory */

int phys_owned(u32 base, u32 len)
{
    u32 end = base + len, i;
    if (!len)
        return 0;
    if (end < base)
        return 1;
    if (base < bi->kernel_phys + bi->kernel_total && end > bi->kernel_phys)
        return 1;
    for (i = 0; i < bi->n_ranges; i++) {
        u32 b = bi->range[i].base, e = b + bi->range[i].length;
        if (b < 0x110000)
            b = 0x110000;                       /* DOS's below there, and the HMA */
        if (bi->range[i].type == BI_MEM_FREE && base < e && end > b)
            return 1;
    }
    return 0;
}

static void phys_rw(u32 phys, u8 *buf, u32 len, int write)
{
    while (len) {
        u32 n = PAGE_SIZE - (phys & 0xFFF);
        u8 *p;
        if (n > len)
            n = len;
        p = kmap(phys);
        if (write)
            memcpy(p, buf, n);
        else
            memcpy(buf, p, n);
        kunmap(p);
        phys += n;
        buf += n;
        len -= n;
    }
}

int phys_copy(u32 dst, u32 src, u32 len)
{
    u32 n, off;
    if (dst + len < dst || src + len < src)
        return -1;
    if (dst > src && dst < src + len) {         /* overlapping upwards: from the end */
        while (len) {
            n = len > PAGE_SIZE ? PAGE_SIZE : len;
            len -= n;
            phys_rw(src + len, bounce, n, 0);
            phys_rw(dst + len, bounce, n, 1);
        }
        return 0;
    }
    for (off = 0; off < len; off += n) {
        n = len - off > PAGE_SIZE ? PAGE_SIZE : len - off;
        phys_rw(src + off, bounce, n, 0);
        phys_rw(dst + off, bounce, n, 1);
    }
    return 0;
}

/* ---- blocks */

static u32 free_frames(void)
{
    u32 f = pmm_free_frames();
    return f > RESERVE ? f - RESERVE : 0;
}

static u32 largest_frames(void)
{
    u32 l = pmm_largest(), f = free_frames();
    return l < f ? l : f;
}

static int handle(u32 h)
{
    return (h >= 1 && h <= NHANDLES && emb[h - 1].used) ? (int)h - 1 : -1;
}

static int alloc(u32 kb, u32 *h)
{
    u32 n = (kb + 3) / 4, phys = 0;
    int i;
    for (i = 0; i < NHANDLES && emb[i].used; i++) ;
    if (i == NHANDLES)
        return 0xA1;
    if (n) {
        if (n > free_frames() || !(phys = pmm_alloc_run(n)))
            return 0xA0;
    }
    emb[i].phys = phys;
    emb[i].kb = kb;
    emb[i].used = 1;
    emb[i].locks = 0;
    *h = (u32)i + 1;
    return 0;
}

static int resize(struct emb *e, u32 kb)
{
    u32 n_old = (e->kb + 3) / 4, n_new = (kb + 3) / 4, phys;
    if (e->locks)
        return 0xAB;
    if (n_new <= n_old) {
        if (n_old > n_new)
            pmm_free_run(e->phys + n_new * PAGE_SIZE, n_old - n_new);
        if (!n_new)
            e->phys = 0;
        e->kb = kb;
        return 0;
    }
    if (n_new - n_old > free_frames())
        return 0xA0;
    if (n_old && pmm_claim(e->phys + n_old * PAGE_SIZE, n_new - n_old) == 0) {
        e->kb = kb;
        return 0;
    }
    if (!(phys = pmm_alloc_run(n_new)))
        return 0xA0;
    if (n_old) {
        phys_copy(phys, e->phys, n_old * PAGE_SIZE);
        pmm_free_run(e->phys, n_old);
    }
    e->phys = phys;
    e->kb = kb;
    return 0;
}

/* Handle 0: the offset is a real-mode seg:off. */
static int where(u32 h, u32 off, u32 len, u32 *phys, int bad_h, int bad_off)
{
    int i;
    if (!h) {
        u32 lin = ((off >> 16) << 4) + (off & 0xFFFF);
        if (lin + len > 0x110000)
            return bad_off;
        *phys = lin;
        return 0;
    }
    if ((i = handle(h)) < 0)
        return bad_h;
    if (off > emb[i].kb * 1024u)
        return bad_off;
    if (len > emb[i].kb * 1024u - off)
        return 0xA7;                            /* as HIMEMX: the length runs past the end */
    *phys = emb[i].phys + off;
    return 0;
}

static int move(u32 lin)
{
    u32 len = vm_rd32(lin), src, dst;
    int err;
    if (len & 1)
        return 0xA7;
    if ((err = where(vm_rd16(lin + 4), vm_rd32(lin + 6), len, &src, 0xA3, 0xA4)) != 0)
        return err;
    if ((err = where(vm_rd16(lin + 10), vm_rd32(lin + 12), len, &dst, 0xA5, 0xA6)) != 0)
        return err;
    return phys_copy(dst, src, len) ? 0xA7 : 0;
}

static void a20_update(void) { vm_set_a20(a20_global || a20_local); }

static int free_handles(void)
{
    int i, n = 0;
    for (i = 0; i < NHANDLES; i++)
        n += !emb[i].used;
    return n;
}

/* ---- the entry point: AH = function */

void xms_call(struct trapframe *tf)
{
    u8 fn = (u8)(tf->eax >> 8);
    int err = 0, i;
    u32 h = 0, kb;

    switch (fn) {
    case 0x00:
        SET16(tf->eax, ver);
        SET16(tf->ebx, rev);
        SET16(tf->edx, hma_exists);
        return;
    case 0x01:
        err = !hma_exists ? 0x90 : hma_taken ? 0x91 : 0;
        if (!err)
            hma_taken = 2;
        break;
    case 0x02:
        if (hma_taken == 2)
            hma_taken = 0;
        else
            err = 0x93;
        break;
    case 0x03: a20_global = 1; a20_update(); break;
    case 0x04: a20_global = 0; a20_update(); err = vm.a20 ? 0x94 : 0; break;
    case 0x05: a20_local++; a20_update(); break;
    case 0x06:
        if (a20_local)
            a20_local--;
        a20_update();
        err = vm.a20 ? 0x94 : 0;
        break;
    case 0x07:
        SET16(tf->eax, vm.a20);
        SETLO(tf->ebx, 0);
        return;
    case 0x08:
        kb = largest_frames() * 4;
        SET16(tf->eax, kb > 0xFFFF ? 0xFFFF : kb);
        kb = free_frames() * 4;
        SET16(tf->edx, kb > 0xFFFF ? 0xFFFF : kb);
        SETLO(tf->ebx, kb ? 0 : 0xA0);
        return;
    case 0x88:
        tf->eax = largest_frames() * 4;
        tf->edx = free_frames() * 4;
        tf->ecx = 0xFFFFFFFFu;
        SETLO(tf->ebx, tf->edx ? 0 : 0xA0);
        return;
    case 0x09:
    case 0x89:
        err = alloc(fn == 0x09 ? (tf->edx & 0xFFFF) : tf->edx, &h);
        if (!err)
            SET16(tf->edx, h);
        break;
    case 0x0A:
        if ((i = handle(tf->edx & 0xFFFF)) < 0)
            err = 0xA2;
        else if (emb[i].locks)
            err = 0xAB;
        else {
            if (emb[i].kb)
                pmm_free_run(emb[i].phys, (emb[i].kb + 3) / 4);
            emb[i].used = 0;
        }
        break;
    case 0x0B:
        err = move(vm_lin(tf->v86_ds, tf->esi));
        break;
    case 0x0C:
        if ((i = handle(tf->edx & 0xFFFF)) < 0)
            err = 0xA2;
        else if (emb[i].locks == 0xFF)
            err = 0xAC;
        else {
            emb[i].locks++;
            SET16(tf->edx, emb[i].phys >> 16);
            SET16(tf->ebx, emb[i].phys);
            SET16(tf->eax, 1);
            return;
        }
        break;
    case 0x0D:
        if ((i = handle(tf->edx & 0xFFFF)) < 0)
            err = 0xA2;
        else if (!emb[i].locks)
            err = 0xAA;
        else
            emb[i].locks--;
        break;
    case 0x0E:
    case 0x8E:
        if ((i = handle(tf->edx & 0xFFFF)) < 0) {
            err = 0xA2;
            break;
        }
        SETHI(tf->ebx, emb[i].locks);
        if (fn == 0x0E) {
            SETLO(tf->ebx, free_handles());
            SET16(tf->edx, emb[i].kb > 0xFFFF ? 0xFFFF : emb[i].kb);
        } else {
            SET16(tf->ecx, free_handles());
            tf->edx = emb[i].kb;
        }
        break;
    case 0x0F:
    case 0x8F:
        if ((i = handle(tf->edx & 0xFFFF)) < 0)
            err = 0xA2;
        else
            err = resize(&emb[i], fn == 0x0F ? (tf->ebx & 0xFFFF) : tf->ebx);
        break;
    default:                                    /* 10h-12h (UMBs) and the rest */
        err = 0x80;
    }
    if (err) {
        SET16(tf->eax, 0);
        SETLO(tf->ebx, err);
    } else {
        SET16(tf->eax, 1);
    }
}
