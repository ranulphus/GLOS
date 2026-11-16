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
 * session) holds the server; it is freed when the last one lets go. It is
 * allocated last fit, at the top of conventional memory (DOS's strategy 02h
 * for the one call, then DOS's own again), so a TSR started while it is
 * held loads below it and no hole is left when it goes. Not in an upper
 * memory block: under GLOS those are UMBPCI's shadow RAM, which the 440BX's
 * ISA DMA (the floppy's) can't reach (docs/upper-memory.md B1).
 *
 * The ssh thread posts requests and polls their state; the VM thread runs
 * them (trap context, interrupts off). Posting and letting go happen with
 * interrupts off. */
#include "glos/bootinfo.h"
#include "dos.h"
#include "io.h"
#include "kprintf.h"
#include "sched.h"
#include "vm.h"

#define XFER_PARAS 2048u                        /* 32 KB */

enum { ST_NONE, ST_ALLOC, ST_CALLS, ST_FREE, ST_STRAT_GET, ST_STRAT_SET, ST_STRAT_BACK };

static struct dos_req *head, *tail, *cur;
static u32 step, users, xfer_paras;
static u16 xfer_seg;
static u16 cur_seg;                     /* the buffer the request under way uses: xfer_seg, or its COMMAND.COM's own */
static u32 cur_paras;
static u8 state, alloc_failed;
static u16 strategy;                            /* DOS's allocation strategy, put back after the buffer's allocation */
static u8 strategy_saved;
static void (*kick)(void);

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
    if (q->via && q->gone && *q->gone) {        /* (its COMMAND.COM has gone: nobody would make the calls) */
        irq_restore(f);
        return -1;
    }
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

int dos_call(struct dos_req *q)
{
    struct waitq wq;
    u32 f;
    memset(&wq, 0, sizeof wq);
    q->waiter = &wq;
    if (dos_post(q) != 0)
        return -1;
    f = irq_save();
    while (q->state != DR_DONE)
        thread_block(&wq);
    irq_restore(f);
    return 0;
}

/* ---- the VM thread's side */

/* A request waits whose calls are via's to make (interrupts off, or the VM's thread). */
static int waits_for(void *via)
{
    struct dos_req *q;
    for (q = head; q; q = q->next)
        if (q->via == via)
            return 1;
    return 0;
}

/* ... the first of them, taken out of the queue. */
static struct dos_req *take(void *via)
{
    struct dos_req *q, *before = 0;
    for (q = head; q && q->via != via; before = q, q = q->next)
        ;
    if (!q)
        return 0;
    if (before)
        before->next = q->next;
    else
        head = q->next;
    if (tail == q)
        tail = before;
    q->next = 0;
    return q;
}

int dos_vm_pending(void) { return waits_for(0) || (!users && xfer_seg); }
int dos_vm_pending_for(void *via) { return (cur && cur->via == via) || waits_for(via); }

static void put_regs(u32 lin, const struct dos_regs *r)
{
    u16 seg = cur ? cur_seg : xfer_seg;
    u16 ds = r->ds == DOS_XFER ? seg : r->ds, es = r->es == DOS_XFER ? seg : r->es;
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
    u32 x = (u32)cur_seg << 4;
    if (q->out && q->out_len && cur_seg && q->out_at + q->out_len <= cur_paras * 16u)
        memcpy(q->out, vm_ptr(x + q->out_at), q->out_len);
    q->state = DR_DONE;
    if (q->waiter)
        thread_wake(q->waiter);
    else if (kick)
        kick();
}

int dos_vm_next(int ran, u32 regs_lin) { return dos_vm_next_for(0, ran, regs_lin, 0, 0); }

/* The program that made via's calls has gone without finishing them (DOS ended it, or it was killed): the
   request under way and those that wait end with an error. */
void dos_vm_gone(void *via)
{
    struct dos_req *q;
    u32 f = irq_save();
    if (cur && cur->via == via) {
        cur->error = 0xFFFF;
        q = cur;
        cur = 0;
        q->out = 0;
        finish(q);
        state = ST_NONE;
    }
    while ((q = take(via)) != 0) {
        q->error = 0xFFFF;
        q->out = 0;
        finish(q);
    }
    irq_restore(f);
}

/* The requests whose calls are via's to make (0: the resident stub's), a call at a time: 1, with the call in
   regs_lin; 0 when there is none. The stub allocates the server's transfer buffer and gives it back when that
   is due; a COMMAND.COM's requests use a buffer of its own (seg, paras), in its own memory: a program that
   shells out may have little memory left, and the server's 32 KB would be taken from what its child needs. */
int dos_vm_next_for(void *via, int ran, u32 regs_lin, u16 seg, u32 paras)
{
    struct dos_regs r;
    if (ran) {                                  /* the call made last time came back */
        get_regs(regs_lin, &r);
        if (state == ST_STRAT_GET) {            /* DOS's strategy: kept, and last fit (low memory) for ours */
            if (r.flags & 1)
                goto alloc;                     /* (a DOS without strategies: first fit, as before) */
            strategy = r.ax;
            strategy_saved = 1;
            r.ax = 0x5801;
            r.bx = 0x0002;
            r.cx = r.dx = r.si = r.di = 0;
            r.ds = r.es = 0;
            put_regs(regs_lin, &r);
            state = ST_STRAT_SET;
            return 1;
        }
        if (state == ST_STRAT_SET)
            goto alloc;
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
            if (strategy_saved) {               /* DOS's strategy back */
                strategy_saved = 0;
                r.ax = 0x5801;
                r.bx = strategy;
                r.cx = r.dx = r.si = r.di = 0;
                r.ds = r.es = 0;
                put_regs(regs_lin, &r);
                state = ST_STRAT_BACK;
                return 1;
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
    if (!cur && waits_for(via)) {               /* the next request */
        if (!via && !xfer_seg && !alloc_failed) {
            xfer_paras = XFER_PARAS;
            r.ax = 0x5800;                      /* DOS's strategy first (ST_STRAT_GET) */
            r.bx = r.cx = r.dx = r.si = r.di = 0;
            r.ds = r.es = 0;
            put_regs(regs_lin, &r);
            state = ST_STRAT_GET;
            return 1;
        }
        cur = take(via);
        step = 0;
        cur->state = DR_RUNNING;
        cur_seg = via ? seg : xfer_seg;
        cur_paras = via ? paras : xfer_paras;
        if (!cur_seg) {                         /* no buffer: the request fails */
            cur->error = 8;                     /* insufficient memory */
            finish(cur);
            cur = 0;
            return dos_vm_next_for(via, 0, regs_lin, seg, paras);
        }
        if (cur->in && cur->in_len && cur->in_at + cur->in_len <= cur_paras * 16u)
            memcpy(vm_ptr(((u32)cur_seg << 4) + cur->in_at), cur->in, cur->in_len);
    }
    if (cur) {
        put_regs(regs_lin, &cur->r[step]);
        state = ST_CALLS;
        return 1;
    }
    if (via)                                    /* (the server's buffer is the stub's to give back) */
        return 0;
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
