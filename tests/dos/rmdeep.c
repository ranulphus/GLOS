/* RMDEEP (supervisor.md §12.1a): real-mode calls nested eight deep on the DPMI host's own real-mode stack (0301h
 * with SS:SP 0), each going back into protected mode through a real-mode callback. Level L's real-mode routine
 * records SS:SP, then calls level L's callback; its protected-mode handler calls level L+1's routine. DJGPP.
 * On COM1:
 *     HX-RMDEEP levels=N ss=S0,S1,... sp=P0,P1,...
 * levels is how deep it went (8 when every call ran). Under GLOS the stack is the block 1687h asked for: one SS,
 * each SP a slice below the last. Until 2026-11-03 the fifth wrapped to FFxxh, 62 KB past a 2 KB block. */
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <stdio.h>
#include <string.h>
#include <sys/movedata.h>

#define LEVELS 8
#define REC    0x100                            /* in the DOS block: SP, SS for each level */
#define TARGET 0x140                            /* the callbacks' real-mode addresses (offset, segment) */

static int seg;
static _go32_dpmi_seginfo cb[LEVELS];
static _go32_dpmi_registers cbregs[LEVELS];
static int reached;

static void ser(const char *s)
{
    for (; *s; s++) {
        int n = 0;
        while (!(inportb(0x3FD) & 0x20) && ++n < 100000) ;
        outportb(0x3F8, *s);
    }
}

static void call_level(int l)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.cs = seg;                               /* (SS:SP 0: the host's own stack) */
    r.x.ip = l * 16;
    r.x.ds = seg;
    __dpmi_simulate_real_mode_procedure_retf(&r);
}

static void handler(_go32_dpmi_registers *r)
{
    int l = r->x.ax;
    if (l + 1 > reached)
        reached = l + 1;
    if (l + 1 < LEVELS)
        call_level(l + 1);
}

int main(void)
{
    unsigned char code[LEVELS * 16];
    unsigned short rec[LEVELS * 2], target[LEVELS * 2];
    char line[400];
    int sel, l, n;
    if ((seg = __dpmi_allocate_dos_memory(0x20, &sel)) < 0) {
        ser("HX-RMDEEP no-dos-memory\r\n");
        return 1;
    }
    for (l = 0; l < LEVELS; l++) {
        unsigned char *c = code + l * 16;
        unsigned rec_at = REC + l * 4, tgt = TARGET + l * 4;
        cb[l].pm_offset = (unsigned long)handler;
        if (_go32_dpmi_allocate_real_mode_callback_retf(&cb[l], &cbregs[l]) != 0) {
            ser("HX-RMDEEP no-callback\r\n");
            return 1;
        }
        target[l * 2] = cb[l].rm_offset;
        target[l * 2 + 1] = cb[l].rm_segment;
        c[0] = 0x89, c[1] = 0x26, c[2] = rec_at & 0xFF, c[3] = rec_at >> 8;            /* mov [rec], sp */
        c[4] = 0x8C, c[5] = 0x16, c[6] = (rec_at + 2) & 0xFF, c[7] = (rec_at + 2) >> 8; /* mov [rec+2], ss */
        c[8] = 0xB8, c[9] = l, c[10] = 0;                                                /* mov ax, l */
        c[11] = 0xFF, c[12] = 0x1E, c[13] = tgt & 0xFF, c[14] = tgt >> 8;               /* call far [tgt] */
        c[15] = 0xCB;                                                                    /* retf */
    }
    memset(rec, 0, sizeof rec);
    dosmemput(code, sizeof code, seg * 16);
    dosmemput(rec, sizeof rec, seg * 16 + REC);
    dosmemput(target, sizeof target, seg * 16 + TARGET);
    call_level(0);
    dosmemget(seg * 16 + REC, sizeof rec, rec);
    n = snprintf(line, sizeof line, "HX-RMDEEP levels=%d ss=", reached);
    for (l = 0; l < LEVELS; l++)
        n += snprintf(line + n, sizeof line - n, "%s%04x", l ? "," : "", rec[l * 2 + 1]);
    n += snprintf(line + n, sizeof line - n, " sp=");
    for (l = 0; l < LEVELS; l++)
        n += snprintf(line + n, sizeof line - n, "%s%04x", l ? "," : "", rec[l * 2]);
    snprintf(line + n, sizeof line - n, "\r\n");
    ser(line);
    for (l = 0; l < LEVELS; l++)
        _go32_dpmi_free_real_mode_callback(&cb[l]);
    __dpmi_free_dos_memory(sel);
    return 0;
}
