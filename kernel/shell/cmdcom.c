/* GLOS's COMMAND.COM, the kernel's side (docs/v1/shell.md, step S4c; apps/command/command.asm is the program).
 *
 * A DOS program that shells out (COMMAND /C line, the C library's system(), a "DOS shell" in its menu) starts
 * COMSPEC, which under GLOS is a small real-mode program of GLOS's own. It has no interpreter in it: it tells
 * the kernel it has begun and then asks, again and again, what to do next, as the resident stub does:
 *
 *   BEGIN  AX 1, BX its block's offset in its segment, DX:CX a block of memory for its children's environment
 *          (DX its segment, CX its paragraphs; 0: none), SI:DI one for the DOS calls' data (paths, a file's
 *          bytes: at least 1 KB). -> AX 0, or 1 (no session can be had)
 *   NEXT   AX 2, EBX how the last thing ended (FFFFFFFFh nothing, FFFFFFFEh a DOS call was made, else a
 *          program's: the word INT 21h 4Dh gave, or 10000h and DOS's error if it could not be started)
 *          -> AX 1 start the program in the block (EXEC, its child), 2 nothing yet (HLT, then ask again),
 *             3 make the DOS call in the block, 0 leave with the block's exit code
 *
 * Both by the stub's own instruction (ARPL, an invalid opcode in V86 mode) with "GCMD" after it, which the
 * kernel takes from any real-mode program (vm/v86.c).
 *
 * Each COMMAND.COM that runs has a shell session of the ordinary kind (shsess.c) whose DOS calls it makes
 * (dos.c: requests say whose they are), whose programs it starts, and whose output goes through its own
 * handles 1 and 2. They nest, a program started by one shelling out in its turn: a stack. One that has gone
 * without leaving through here (killed) is found out when the one below it, or the resident stub, next asks. */
#include <string.h>

#include "cmdcom.h"
#include "glos/bootinfo.h"
#include "dos.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mm.h"
#include "shsess.h"
#include "vm.h"

#define CMD_MAX 8

/* The program's block (command.asm's `blk`). */
#define B_REGS  0                       /* nine words: AX BX CX DX SI DI DS ES, FLAGS back */
#define B_PATH  18                      /* EXEC: the program, ASCIIZ (80) */
#define B_TAIL  98                      /* ... its command tail: length, text, CR (130) */
#define B_ENV   228                     /* ... its environment's segment (0: a copy of COMMAND.COM's) */
#define B_CODE  230                     /* the exit code to leave with */
#define B_BREAK 231                     /* set by its Ctrl+C handler: a batch file goes no further */

struct cmdcom {
    u16 psp;                            /* the program's PSP, which is its segment */
    u16 blk;
    u16 env_seg, env_paras;
    u16 xfer_seg, xfer_paras;           /* its own buffer for the DOS calls (dos.c) */
    struct shsess *sess;
    volatile u8 exec;                   /* 1: a program waits to be started; 2: it runs; 3: it has ended */
    volatile u8 ended;                  /* the session has ended: leave when its last DOS call is made */
    u32 result;
    char path[80], tail[128];
    char *env;                          /* the program's environment (NAME=VALUE strings, a NUL after the last) */
    u32 env_len;
};

static struct cmdcom cmds[CMD_MAX];
static int ncmd;

static void drop(struct cmdcom *c)
{
    if (c->sess)
        shsess_free(c->sess);                   /* (first: nothing more is asked of DOS for it) */
    dos_vm_gone(c);
    if (c->env)
        kfree(c->env);
    memset(c, 0, sizeof *c);
}

/* Those on top that are not the program asking now (they went without leaving), dropped: the one that asks,
   or 0. psp 0: all of them (the resident stub asks: nothing of DOS's runs). */
static struct cmdcom *top_for(u16 psp)
{
    while (ncmd && (!psp || cmds[ncmd - 1].psp != psp)) {
        kprintf("GLOS-COMMAND gone psp=%04x\n", cmds[ncmd - 1].psp);
        drop(&cmds[--ncmd]);
    }
    return ncmd ? &cmds[ncmd - 1] : 0;
}

void cmdcom_stub_asks(void)
{
    if (ncmd)
        top_for(0);
}

static void ended(void *arg)            /* the session's kick: something to tell, or its end (its thread) */
{
    struct cmdcom *c = arg;
    int code;
    u32 f = irq_save();
    if (c->sess && shsess_ended(c->sess, &code))
        c->ended = 1;
    vm_kick();
    irq_restore(f);
}

static int begin(struct trapframe *tf)
{
    u16 psp = vm_current_psp();
    u32 tail = (u32)psp * 16 + 0x80, env = (u32)vm_rd16((u32)psp * 16 + 0x2C) << 4, n = vm_rd8(tail), i = 0, e = 0;
    char line[130], *block;
    struct cmdcom *c;
    int mode = 0;
    while (ncmd) {                      /* those on top that are not its ancestors have gone */
        u16 up = psp;
        int depth, found = 0;
        for (depth = 0; depth < 32 && !found; depth++) {
            u16 parent = vm_rd16((u32)up * 16 + 0x16);
            if (!parent || parent == up)
                break;
            up = parent;
            found = up == cmds[ncmd - 1].psp;
        }
        if (found)
            break;
        kprintf("GLOS-COMMAND gone psp=%04x\n", cmds[ncmd - 1].psp);
        drop(&cmds[--ncmd]);
    }
    if (ncmd == CMD_MAX || n > 127 || (tf->edi & 0xFFFF) < 64 || !(tf->esi & 0xFFFF))
        return 1;
    while (i < n && (vm_rd8(tail + 1 + i) == ' ' || vm_rd8(tail + 1 + i) == '\t'))
        i++;
    while (i + 1 < n && vm_rd8(tail + 1 + i) == '/') {     /* its switches: /C and /K say a line follows */
        u8 sw = vm_rd8(tail + 2 + i) | 32;
        if (sw == 'c' || sw == 'k') {
            mode = sw == 'c' ? 1 : 2;
            i += 2;
            break;
        }
        while (i < n && vm_rd8(tail + 1 + i) != ' ' && vm_rd8(tail + 1 + i) != '\t')
            i++;
        while (i < n && (vm_rd8(tail + 1 + i) == ' ' || vm_rd8(tail + 1 + i) == '\t'))
            i++;
    }
    for (e = 0; i < n; i++)
        line[e++] = (char)vm_rd8(tail + 1 + i);
    line[e] = 0;
    for (e = 0; env && e < 8192 && (vm_rd8(env + e) || (e && vm_rd8(env + e - 1))); e++)
        ;                               /* its environment: strings to a second NUL */
    if (!(block = kmalloc(e + 2)))
        return 1;
    for (i = 0; i < e; i++)
        block[i] = (char)vm_rd8(env + i);
    block[e] = block[e + 1] = 0;
    c = &cmds[ncmd];
    memset(c, 0, sizeof *c);
    c->psp = psp;
    c->blk = (u16)tf->ebx;
    c->env_seg = (u16)tf->edx;
    c->env_paras = (u16)tf->ecx;
    c->xfer_seg = (u16)tf->esi;
    c->xfer_paras = (u16)tf->edi;
    c->sess = shsess_cmd(c, mode ? line : 0, mode != 1, block, (u32)c->xfer_paras * 16, ended, c);
    kfree(block);
    if (!c->sess)
        return 1;
    ncmd++;
    kprintf("GLOS-COMMAND begin psp=%04x depth=%d mode=%s\n", psp, ncmd, mode == 1 ? "/C" : mode == 2 ? "/K" : "shell");
    return 0;
}

static u32 next(struct cmdcom *c, u32 result)
{
    u32 base = (u32)c->psp * 16 + c->blk, i;
    int ran = result == 0xFFFFFFFEu, code = 0;
    if (vm_rd8(base + B_BREAK)) {               /* Ctrl+C while the calls were its own */
        vm_wr8(base + B_BREAK, 0);
        shsess_cmd_stop(c->sess);
    }
    if (c->exec == 2 && !ran && result != 0xFFFFFFFFu) {    /* its program has ended */
        if (!(result & 0x10000) && (result & 0xFF00) == 0x0100)
            shsess_cmd_stop(c->sess);           /* (ended by Ctrl+C: INT 21h 4Dh's AH 1) */
        c->result = result;
        c->exec = 3;
        shsess_cmd_wake(c->sess);
    }
    if (dos_vm_next_for(c, ran, base + B_REGS, c->xfer_seg, c->xfer_paras))
        return 3;
    if (c->exec == 1) {
        for (i = 0; i < 80; i++)
            vm_wr8(base + B_PATH + i, (u8)c->path[i]);
        i = (u32)strlen(c->tail);
        vm_wr8(base + B_TAIL, (u8)i);
        memcpy(vm_ptr(base + B_TAIL + 1), c->tail, i);
        vm_wr8(base + B_TAIL + 1 + i, 13);
        vm_wr8(base + B_ENV, 0);
        vm_wr8(base + B_ENV + 1, 0);
        if (c->env && c->env_seg && c->env_len + 1 <= (u32)c->env_paras * 16) {
            memcpy(vm_ptr((u32)c->env_seg * 16), c->env, c->env_len);
            vm_wr8((u32)c->env_seg * 16 + c->env_len, 0);
            vm_wr8(base + B_ENV, (u8)c->env_seg);
            vm_wr8(base + B_ENV + 1, (u8)(c->env_seg >> 8));
        }
        c->exec = 2;
        return 1;
    }
    if (c->ended && !dos_vm_pending_for(c)) {
        shsess_ended(c->sess, &code);
        vm_wr8(base + B_CODE, (u8)code);
        kprintf("GLOS-COMMAND end psp=%04x code=%d\n", c->psp, code);
        drop(c);
        ncmd--;
        return 0;
    }
    return 2;
}

/* The invalid opcode at tf (V86 code) is a COMMAND.COM's call: 1, answered; 0: not one. */
int cmdcom_call(struct trapframe *tf)
{
    u32 at = ((u32)(tf->cs & 0xFFFF) << 4) + (tf->eip & 0xFFFF);
    struct cmdcom *c;
    if (vm_rd8(at) != 0x63 || vm_rd8(at + 1) != 0xC0 || vm_rd8(at + 2) != 'G' || vm_rd8(at + 3) != 'C'
        || vm_rd8(at + 4) != 'M' || vm_rd8(at + 5) != 'D')
        return 0;
    tf->eip = (tf->eip + 6) & 0xFFFF;
    switch (tf->eax & 0xFFFF) {
    case 1:
        tf->eax = (u32)begin(tf);
        return 1;
    case 2:
        if (!(c = top_for(vm_current_psp()))) { /* (none of ours: it leaves) */
            tf->eax = 0;
            return 1;
        }
        tf->eax = next(c, tf->ebx);
        return 1;
    }
    tf->eax = 0xFFFF;
    return 1;
}

/* A HLT in V86 code at this segment is a COMMAND.COM's wait: 1 if it has something to do already. */
int cmdcom_waits(u16 cs)
{
    struct cmdcom *c = ncmd ? &cmds[ncmd - 1] : 0;
    return c && c->psp == cs;
}

int cmdcom_pending(void)
{
    struct cmdcom *c = ncmd ? &cmds[ncmd - 1] : 0;
    return c && (c->exec == 1 || dos_vm_pending_for(c) || c->ended);
}

/* The program the VM runs was killed: a batch file the COMMAND.COM on top was running is not gone on with. */
void cmdcom_killed(void)
{
    if (ncmd && cmds[ncmd - 1].sess)
        shsess_cmd_stop(cmds[ncmd - 1].sess);
}

/* GLOS's own COMMAND.COM for the programs GLOS starts: its path (beside GLOS.EXE), or 0: the DOS's own is
   theirs (there is none of GLOS's there, or [shell] command = dos). */
const char *cmdcom_path(void)
{
    static char p[96];
    char v[8], *slash;
    if (!(vm.bi->flags & BI_F_OWNCMD))
        return 0;
    v[0] = 0;
    if (kcfg_loaded())
        kcfg_get("shell", "command", v, sizeof v);
    if (!strcmp(v, "dos"))
        return 0;
    if (!p[0]) {
        strncpy(p, vm.bi->cfg_path, sizeof p - 16);
        slash = strrchr(p, '\\');
        strcpy(slash ? slash + 1 : p, "COMMAND.COM");
    }
    return p;
}

/* ---- the session's side (its thread) */

/* Its COMMAND.COM is to start this program, with these variables as its environment: 0, or -1. */
int cmdcom_exec(void *via, struct shsess *sess, const char *path, const char *tail, char *const *vars, int nvars)
{
    struct cmdcom *c = via;
    u32 len = 0, f;
    int i;
    char *env;
    if (strlen(path) >= sizeof c->path || strlen(tail) >= sizeof c->tail - 2)
        return -1;
    for (i = 0; i < nvars; i++)
        len += (u32)strlen(vars[i]) + 1;
    if (!(env = kmalloc(len + 1)))
        return -1;
    for (i = 0, len = 0; i < nvars; i++) {
        strcpy(env + len, vars[i]);
        len += (u32)strlen(vars[i]) + 1;
    }
    f = irq_save();
    if (c->sess != sess || c->exec) {           /* (its COMMAND.COM has gone) */
        irq_restore(f);
        kfree(env);
        return -1;
    }
    if (c->env)
        kfree(c->env);
    c->env = env;
    c->env_len = len;
    strcpy(c->path, path);
    strcpy(c->tail, tail);
    c->exec = 1;
    vm_kick();
    irq_restore(f);
    return 0;
}

/* Its program has ended: 1 with its exit code (1 if DOS could not start it), once. */
int cmdcom_done(void *via, struct shsess *sess, int *code)
{
    struct cmdcom *c = via;
    u32 f = irq_save();
    int yes = c->sess == sess && c->exec == 3;
    if (yes) {
        *code = c->result & 0x10000 ? 1 : (int)(c->result & 0xFF);
        c->exec = 0;
    }
    irq_restore(f);
    return yes;
}
