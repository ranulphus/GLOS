/* MODELOOP (DJGPP): a VBE mode set and text mode again, over and over, the way a game's renderer starts and
 * ends, for a PC that stood dead at a mode set (the 486 bench, 2026-11-18 and 11-19: docs/v1-route.md).
 *
 *   MODELOOP [count] [mode] [lfb]      count times (10 unless said): VBE mode `mode` (hex, 110 unless said:
 *                                      640x480 in 15 bits) set by INT 10h AX=4F02h, text mode 3 again
 *                                      lfb: the mode with its linear framebuffer, mapped by DPMI 0800h and
 *                                      written to (a row), as a renderer has it
 *
 * A line before each step, flushed, so that what was last said tells where it stopped:
 *   MODELOOP i=1 set mode=110
 *   MODELOOP i=1 text
 *   MODELOOP done count=10 failed=0
 * A mode the BIOS refuses is counted (failed=) and the loop goes on. Exit code 0, or 1 with any refused. */
#include <dpmi.h>
#include <go32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <sys/movedata.h>

int main(int argc, char **argv)
{
    int count = argc > 1 ? atoi(argv[1]) : 10, i, failed = 0;
    unsigned mode = argc > 2 ? (unsigned)strtoul(argv[2], 0, 16) : 0x110;
    int lfb = argc > 3 && !strcmp(argv[3], "lfb");
    unsigned long base = 0, pitch = 0;
    unsigned char info[256];
    __dpmi_regs r;
    if (count < 1 || count > 100000)
        count = 10;
    memset(&r, 0, sizeof r);                    /* the mode's own description: its framebuffer, its row */
    r.x.ax = 0x4F01;
    r.x.cx = (unsigned short)mode;
    r.x.es = (unsigned short)(__tb >> 4);
    r.x.di = (unsigned short)(__tb & 15);
    __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) {
        printf("MODELOOP error=no-mode mode=%x ax=%04x\n", mode, r.x.ax);
        return 1;
    }
    dosmemget(__tb, sizeof info, info);
    pitch = *(const unsigned short *)(info + 16);
    base = *(const unsigned long *)(info + 40);
    printf("MODELOOP mode=%x %ux%u bits=%u pitch=%lu lfb=%08lx count=%d%s\n", mode, *(const unsigned short *)(info + 18),
           *(const unsigned short *)(info + 20), info[25], pitch, base, count, lfb ? " lfb" : "");
    fflush(stdout);
    for (i = 1; i <= count; i++) {
        printf("MODELOOP i=%d set mode=%x\n", i, mode);
        fflush(stdout);
        memset(&r, 0, sizeof r);
        r.x.ax = 0x4F02;
        r.x.bx = (unsigned short)(mode | (lfb ? 0x4000 : 0));
        __dpmi_int(0x10, &r);
        if (r.x.ax != 0x004F) {
            failed++;
        } else if (lfb && base) {
            __dpmi_meminfo mi;
            int sel;
            mi.address = base;
            mi.size = 0x100000;
            if (__dpmi_physical_address_mapping(&mi) == 0 && (sel = __dpmi_allocate_ldt_descriptors(1)) >= 0) {
                unsigned long x;
                __dpmi_set_segment_base_address(sel, mi.address);
                __dpmi_set_segment_limit(sel, 0xFFFFF);
                for (x = 0; x < pitch; x++)
                    _farpokeb(sel, pitch * 8 + x, (unsigned char)(x + i));
                __dpmi_free_ldt_descriptor(sel);
                __dpmi_free_physical_address_mapping(&mi);
            }
        }
        memset(&r, 0, sizeof r);
        r.x.ax = 0x0003;
        __dpmi_int(0x10, &r);
        printf("MODELOOP i=%d text%s\n", i, failed ? " (a mode was refused)" : "");
        fflush(stdout);
    }
    printf("MODELOOP done count=%d failed=%d\n", count, failed);
    return failed != 0;
}
