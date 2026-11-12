/* The verbose boot (/VERBOSE, BI_F_VERBOSE; GLOS.CFG's [shell] options): the
 * kernel's log lines on the text screen as well as COM1, as Linux's console
 * has its messages, for a PC with no serial cable (a bench PC's first
 * boots). Written where the BIOS's cursor is and the cursor moved on, so
 * that DOS's own output goes on below; only while the screen is in a text
 * mode and the desktop hasn't got it (a program's graphics are never
 * touched). The lines are those written until the threads run (the boot's):
 * after that the screen is DOS's and its programs'.
 *
 * The idle screen's notice (vcon_notice), verbose or not: a PC that runs
 * GLOS with no desktop and nothing running shows nothing of its own and
 * takes no keys, and was taken for one that had stopped (the 486 bench,
 * 2026-11-12). So the screen says that GLOS runs, how the PC is reached
 * (a PC with no serial cable is found by its address) and that
 * Ctrl-Alt-Del restarts it (v86.c vm_idle_headless). A temporary measure:
 * GLOS's shell has that screen from its step S2 (docs/v1/shell.md). */
#include <string.h>

#include "display.h"
#include "io.h"
#include "kprintf.h"
#include "nic.h"
#include "sshd.h"
#include "vm.h"

/* Up a line, as the BIOS's teletype scrolls. */
static void scroll_up(u16 *scr, u32 cols, u32 rows)
{
    u32 i;
    for (i = 0; i < (rows - 1) * cols; i++)
        scr[i] = scr[i + cols];
    for (i = 0; i < cols; i++)
        scr[(rows - 1) * cols + i] = 0x0720;
}

static int vcon_write(const char *s, u32 n, int fresh)
{
    u8 mode = vm_rd8(0x449), page = vm_rd8(0x462);
    u32 cols = vm_rd8(0x44A) | (u32)vm_rd8(0x44B) << 8, rows = vm_rd8(0x484) + 1u;
    u32 col, row, base;
    u16 *scr;
    if ((mode != 2 && mode != 3 && mode != 7) || display_fb() || cols < 40 || cols > 132 || rows < 25 || rows > 60
        || page > 7)
        return 0;
    base = (mode == 7 ? 0xB0000u : 0xB8000u) + (u32)page * (vm_rd8(0x44C) | (u32)vm_rd8(0x44D) << 8);
    scr = (u16 *)vm_ptr(base);
    col = vm_rd8(0x450 + page * 2u), row = vm_rd8(0x451 + page * 2u);
    if (col >= cols || row >= rows)
        col = 0, row = rows - 1;
    if (fresh && col) {                         /* (a notice starts its own line) */
        col = 0;
        if (++row == rows)
            scroll_up(scr, cols, rows), row = rows - 1;
    }
    for (; n; n--, s++) {
        if (*s == '\r')
            continue;
        if (*s == '\n') {
            col = 0, row++;
        } else {
            scr[row * cols + col] = (u16)(0x0700 | (u8)*s);
            if (++col == cols)
                col = 0, row++;
        }
        if (row == rows)
            scroll_up(scr, cols, rows), row = rows - 1;
    }
    vm_wr8(0x450 + page * 2u, (u8)col);
    vm_wr8(0x451 + page * 2u, (u8)row);
    return 1;
}

static void vcon_put(const char *s, u32 n) { (void)vcon_write(s, n, 0); }

/* The VM thread, about to wait at the idle screen: what the screen hasn't said yet. */
void vcon_notice(void)
{
    static u8 said;
    static char said_ip[16];
    char line[160], ip[16];
    const char *name;
    u32 f = irq_save();
    if (!said && vcon_write(line, (u32)ksnprintf(line, sizeof line,
                                                 "GLOS is running on this PC (no desktop). Ctrl-Alt-Del restarts the PC.\n"), 1))
        said = 1;
    if (said && net_address(ip, &name) && strcmp(ip, said_ip)
        && vcon_write(line, (u32)ksnprintf(line, sizeof line, sshd_on() ? "Its name is %s.local, its address %s: reach it with ssh.\n"
                                           : "Its name is %s.local, its address %s. SSH is off: it has no host key.\n", name, ip), 1))
        strcpy(said_ip, ip);
    irq_restore(f);
}

/* From now on (vm_start: low memory is the VM's from there): the lines so far first. */
void vcon_start(void)
{
    static char past[4096];
    u32 n = klog_read(past, sizeof past);
    vcon_put(past, n);
    kprintf_screen = vcon_put;
}
