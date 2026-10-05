/* Client levels (supervisor.md §12.1; M4c). A program that a client EXECs
 * and that switches to protected mode itself joins its parent's context,
 * as under CWSDPMI and HDPMI: one address space, one LDT, one virtual IDT
 * (DJGPP's system() and spawn*(); djtst205's MULTISPN).
 * Its mode switch pushes a level, and its end pops it, freeing exactly what
 * it made: the LDT entries, memory blocks, DOS blocks' selectors and real-
 * mode callbacks tagged with its level, the handlers it set (the parent's
 * tables come back), and entries its handlers left. Each level has its own
 * PSP, environment selector, host real-mode stack, first-block floor and
 * terminate address, kept in dctx while it runs and in its lv[] slot while a
 * child runs above it. The parent waits inside its EXEC, a nested real-mode
 * call, so the child runs in that call's frame (`depth`, `frame`), and ends
 * there. Levels share the client's bitness: a 16-bit child of a 32-bit
 * client (or the reverse) is refused. */
#include <string.h>

#include "arch.h"
#include "dpmi.h"
#include "kprintf.h"
#include "mm.h"
#include "vm.h"

static void save_fields(struct dpmi_level *l)
{
    l->psp = dctx->psp;
    l->env_seg = dctx->env_seg;
    l->env_sel = dctx->env_sel;
    l->psp_sel = dctx->psp_sel;
    l->rm_seg = dctx->rm_seg;
    l->rm_sp = dctx->rm_sp;
    l->lin_floor = dctx->lin_floor;
    l->term_vec = dctx->term_vec;
    l->bits32 = dctx->bits32;
}

static void load_fields(const struct dpmi_level *l)
{
    dctx->psp = l->psp;
    dctx->env_seg = l->env_seg;
    dctx->env_sel = l->env_sel;
    dctx->psp_sel = l->psp_sel;
    dctx->rm_seg = l->rm_seg;
    dctx->rm_sp = l->rm_sp;
    dctx->lin_floor = l->lin_floor;
    dctx->term_vec = l->term_vec;
    dctx->bits32 = l->bits32;
}

void level_init(struct trapframe *tf)
{
    struct dpmi_level *l = &dctx->lv[0];
    memset(l, 0, sizeof *l);
    l->depth = rm_nesting();
    l->frame = tf;
    dctx->nlv = 1;
}

int level_push(struct trapframe *tf)
{
    struct dpmi_level *l;
    if (dctx->nlv >= NLEVEL) {
        dpmi_unimpl(tf, "client-levels");
        return 0x8011;
    }
    if ((tf->eax & 1) != dctx->bits32) {        /* frames and stacks follow the context's bitness */
        dpmi_unimpl(tf, "mixed-bitness");
        return 0x8011;
    }
    save_fields(&dctx->lv[dctx->nlv - 1]);
    l = &dctx->lv[dctx->nlv];
    memset(l, 0, sizeof *l);
    memcpy(l->vidt, dctx->vidt, sizeof l->vidt);
    memcpy(l->exc, dctx->exc, sizeof l->exc);
    memcpy(l->exc10, dctx->exc10, sizeof l->exc10);
    l->depth = rm_nesting();
    l->frame = tf;
    l->npe = dctx->npe;
    dctx->nlv++;
    dctx->lin_floor = 0;                        /* its own blocks ascend from its own first */
    return 0;
}

void level_pop(int psp)
{
    struct dpmi_level *l = &dctx->lv[dctx->nlv - 1];
    u32 lev = dctx->nlv, i;
    struct dosblk **pp;

    if (psp) {
        vm_wr8(dctx->psp * 16u + 0x2C, (u8)dctx->env_seg);     /* PSP:2Ch as it was */
        vm_wr8(dctx->psp * 16u + 0x2D, (u8)(dctx->env_seg >> 8));
    }
    while (dctx->npe > l->npe) {                /* its handlers' entries */
        struct pmentry *x = &dctx->pe[--dctx->npe];
        if (x->switched)
            dctx->lstack_use--;
        if (x->kind == PE_EXC)
            dctx->exc_depth--;
    }
    lin_free_level(lev);
    wp_clear_level(lev);
    for (pp = &dctx->dosblks; *pp;) {           /* DOS frees the memory with the program */
        struct dosblk *d = *pp;
        if (d->level >= lev) {
            *pp = d->next;
            kfree(d);
        } else {
            pp = &d->next;
        }
    }
    for (i = 0; i < NRMCB; i++)
        if (dctx->rmcb[i].used >= lev)
            dctx->rmcb[i].used = 0;             /* (their stack selectors go with the LDT below) */
    for (i = 0; i < NSEGSEL; i++)
        if (dctx->segsel[i].sel && dctx->ldt_used[dctx->segsel[i].sel >> 3] >= lev)
            dctx->segsel[i].sel = 0;
    for (i = 0; i < LDT_ENTRIES; i++)
        if (dctx->ldt_used[i] >= lev) {
            dctx->ldt_used[i] = 0;
            dctx->ldt[i * 2] = dctx->ldt[i * 2 + 1] = 0;
        }
    memcpy(dctx->vidt, l->vidt, sizeof l->vidt);
    memcpy(dctx->exc, l->exc, sizeof l->exc);
    memcpy(dctx->exc10, l->exc10, sizeof l->exc10);
    dctx->nlv--;
    load_fields(&dctx->lv[dctx->nlv - 1]);
    dctx->ending = 0;
}
