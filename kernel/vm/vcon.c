/* The verbose boot (/VERBOSE, BI_F_VERBOSE; GLOS.CFG's [shell] options): the
 * kernel's log lines on the text screen as well as COM1, as Linux's console
 * has its messages, for a PC with no serial cable (a bench PC's first
 * boots). Written where the BIOS's cursor is and the cursor moved on, so
 * that DOS's own output goes on below; only while the screen is in a text
 * mode and the desktop hasn't got it (a program's graphics are never
 * touched). Lines from the threads come with interrupts on, a character at
 * a time (kprintf_pump): DOS writing at once may mix with them, as on any
 * console. */
#include "display.h"
#include "kprintf.h"
#include "vm.h"

static void vcon_put(const char *s, u32 n)
{
    u8 mode = vm_rd8(0x449), page = vm_rd8(0x462);
    u32 cols = vm_rd8(0x44A) | (u32)vm_rd8(0x44B) << 8, rows = vm_rd8(0x484) + 1u;
    u32 col, row, base;
    u16 *scr;
    if ((mode != 2 && mode != 3 && mode != 7) || display_fb() || cols < 40 || cols > 132 || rows < 25 || rows > 60
        || page > 7)
        return;
    base = (mode == 7 ? 0xB0000u : 0xB8000u) + (u32)page * (vm_rd8(0x44C) | (u32)vm_rd8(0x44D) << 8);
    scr = (u16 *)vm_ptr(base);
    col = vm_rd8(0x450 + page * 2u), row = vm_rd8(0x451 + page * 2u);
    if (col >= cols || row >= rows)
        col = 0, row = rows - 1;
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
        if (row == rows) {                      /* up a line, as the BIOS's teletype scrolls */
            u32 i;
            for (i = 0; i < (rows - 1) * cols; i++)
                scr[i] = scr[i + cols];
            for (i = 0; i < cols; i++)
                scr[(rows - 1) * cols + i] = 0x0720;
            row = rows - 1;
        }
    }
    vm_wr8(0x450 + page * 2u, (u8)col);
    vm_wr8(0x451 + page * 2u, (u8)row);
}

/* From now on (vm_start: low memory is the VM's from there): the lines so far first. */
void vcon_start(void)
{
    static char past[4096];
    u32 n = klog_read(past, sizeof past);
    vcon_put(past, n);
    kprintf_screen = vcon_put;
}
