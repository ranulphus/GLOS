/* The DOS server (supervisor.md §17.1, §17.2; M3 item 9): DOS calls for
 * the kernel's own users (SFTP), made by the resident stub while it waits
 * for work (NEXT's answer 3), so they never run while a program does. In
 * M3 that is headless mode's idle stub; a running job holds them back.
 *
 * A request is up to four INT 21h calls, made in order until one returns
 * with CF set, with optional bytes copied into a transfer buffer before the
 * first call and out of it after the last. The buffer is a DOS memory block
 * (up to 32 KB) that the stub allocates on the first request, so it belongs
 * to GLOS.EXE and costs conventional memory only while someone (an SFTP
 * session) holds the server; it is freed when the last one lets go.
 *
 * The ssh thread posts requests and polls their state; the VM thread runs
 * them (trap context, interrupts off). Posting and letting go happen with
 * interrupts off. */
#include "glos/bootinfo.h"
#include "dos.h"
#include "io.h"
#include "kprintf.h"
#include "vm.h"

#define XFER_PARAS 2048u                        /* 32 KB */

enum { ST_NONE, ST_ALLOC, ST_CALLS, ST_FREE };

static struct dos_req *head, *tail, *cur;
static u32 step, users, xfer_paras;
static u16 xfer_seg;
static u8 state, alloc_failed;
static void (*kick)(void);

static u32 irq_save(void)
{
    u32 f;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(f) :: "memory");
    return f;
}

static void irq_restore(u32 f)
{
    if (f & 0x200)
        sti();
}

/* ---- the ssh thread's side */

void dos_init(void (*k)(void)) { kick = k; }

void dos_hold(void)
{
    u32 f = irq_save();
    users++;
    irq_restore(f);
}

void dos_release(void)
{
    u32 f = irq_save();
    if (users)
        users--;
    if (!users)
        vm_kick();                              /* the stub frees the buffer when next idle */
    irq_restore(f);
}

u32 dos_xfer_size(void) { return xfer_paras ? xfer_paras * 16u : XFER_PARAS * 16u; }

int dos_post(struct dos_req *q)
{
    u32 f;
    if (!(vm.bi->flags & BI_F_AGENT) || !q->n || q->n > DOS_MAX_CALLS)
        return -1;
    q->ran = 0;
    q->error = 0;
    q->next = 0;
    f = irq_save();
    q->state = DR_QUEUED;
    if (tail)
        tail->next = q;
    else
        head = q;
    tail = q;
    vm_kick();
    irq_restore(f);
    return 0;
}

/* ---- the VM thread's side */

int dos_vm_pending(void) { return head || (!users && xfer_seg); }

static void put_regs(u32 lin, const struct dos_regs *r)
{
    u16 ds = r->ds == DOS_XFER ? xfer_seg : r->ds, es = r->es == DOS_XFER ? xfer_seg : r->es;
    u16 v[8] = { r->ax, r->bx, r->cx, r->dx, r->si, r->di, ds, es };
    u32 i;
    for (i = 0; i < 8; i++) {
        vm_wr8(lin + i * 2, (u8)v[i]);
        vm_wr8(lin + i * 2 + 1, (u8)(v[i] >> 8));
    }
}

static void get_regs(u32 lin, struct dos_regs *r)
{
    r->ax = vm_rd16(lin);
    r->bx = vm_rd16(lin + 2);
    r->cx = vm_rd16(lin + 4);
    r->dx = vm_rd16(lin + 6);
    r->si = vm_rd16(lin + 8);
    r->di = vm_rd16(lin + 10);
    r->flags = vm_rd16(lin + 16);
}

static void finish(struct dos_req *q)
{
    u32 x = (u32)xfer_seg << 4;
    if (q->out && q->out_len && xfer_seg && q->out_at + q->out_len <= xfer_paras * 16u)
        memcpy(q->out, vm_ptr(x + q->out_at), q->out_len);
    q->state = DR_DONE;
    if (kick)
        kick();
}

int dos_vm_next(int ran, u32 regs_lin)
{
    struct dos_regs r;
    if (ran) {                                  /* the call made last time came back */
        get_regs(regs_lin, &r);
        if (state == ST_ALLOC) {
            if (!(r.flags & 1)) {
                xfer_seg = r.ax;
                kprintf("GLOS-DOS xfer seg=%04x bytes=%u\n", xfer_seg, xfer_paras * 16u);
            } else if (r.bx >= 256 && xfer_paras == XFER_PARAS) {
                xfer_paras = r.bx;              /* less than asked: what there is, then */
                goto alloc;
            } else {
                alloc_failed = 1;
                kprintf("GLOS-DOS error=no-low-memory\n");
            }
        } else if (state == ST_FREE) {
            xfer_seg = 0;
            xfer_paras = 0;
        } else if (state == ST_CALLS && cur) {
            r.ds = cur->r[step].ds;
            r.es = cur->r[step].es;
            cur->r[step] = r;
            cur->ran = step + 1;
            if ((r.flags & 1) || ++step == cur->n) {
                cur->error = (r.flags & 1) ? r.ax : 0;
                finish(cur);
                cur = 0;
            }
        }
        state = ST_NONE;
    }
    if (!cur && head) {                         /* the next request */
        if (!xfer_seg && !alloc_failed) {
            xfer_paras = XFER_PARAS;
            goto alloc;
        }
        cur = head;
        head = head->next;
        if (!head)
            tail = 0;
        step = 0;
        cur->state = DR_RUNNING;
        if (!xfer_seg) {                        /* no buffer: the request fails */
            cur->error = 8;                     /* insufficient memory */
            finish(cur);
            cur = 0;
            return dos_vm_next(0, regs_lin);
        }
        if (cur->in && cur->in_len && cur->in_at + cur->in_len <= xfer_paras * 16u)
            memcpy(vm_ptr(((u32)xfer_seg << 4) + cur->in_at), cur->in, cur->in_len);
    }
    if (cur) {
        put_regs(regs_lin, &cur->r[step]);
        state = ST_CALLS;
        return 1;
    }
    if (!users && xfer_seg) {                   /* nobody holds it: give it back */
        r.ax = 0x4900;
        r.bx = r.cx = r.dx = r.si = r.di = 0;
        r.ds = 0;
        r.es = xfer_seg;
        put_regs(regs_lin, &r);
        state = ST_FREE;
        alloc_failed = 0;
        return 1;
    }
    if (!users)
        alloc_failed = 0;
    return 0;
alloc:
    r.ax = 0x4800;
    r.bx = (u16)xfer_paras;
    r.cx = r.dx = r.si = r.di = 0;
    r.ds = r.es = 0;
    put_regs(regs_lin, &r);
    state = ST_ALLOC;
    return 1;
}
