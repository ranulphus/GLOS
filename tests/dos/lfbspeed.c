/* LFBSPEED (DJGPP): how fast whole frames are written to a VBE linear
 * framebuffer, for the write-combining trial (glos mtrr lfb on: kernel/arch/
 * mtrr.c). The mode 640x480 in 8 bits (VESA's 101h) with its linear
 * framebuffer, mapped by DPMI 0800h as a game's renderer has it; a frame of
 * 307,200 bytes copied from memory a number of times (LFBSPEED [frames], 200
 * unless said), timed by the BIOS's clock; then text mode again and one line:
 *   HX-LFBSPEED mode=lfb frames=200 ms=1234 mb_s=49.7 lfb=e0000000
 * LFBSPEED frames vga: mode 13h's 64,000 bytes through the window at A0000h
 * instead; LFBSPEED frames bank: 640x480 through the window, a bank at a time
 * (glos mtrr vga on's trial).
 * or HX-LFBSPEED error=... . Exit code 0, or 1 with an error. */
#include <dpmi.h>
#include <go32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <sys/nearptr.h>
#include <time.h>

#define W 640
#define H 480

static int text_mode(void)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0003;
    return __dpmi_int(0x10, &r);
}

static void report(const char *mode, int frames, uclock_t t0, uclock_t t1, unsigned long bytes, unsigned long lfb)
{
    unsigned long ms = (unsigned long)((t1 - t0) * 1000 / UCLOCKS_PER_SEC), kb = (unsigned long)frames * (bytes / 1024);
    printf("HX-LFBSPEED mode=%s frames=%d ms=%lu mb_s=%lu.%lu lfb=%08lx\n", mode, frames, ms,
           ms ? kb * 1000 / ms / 1024 : 0, ms ? (kb * 1000 / ms % 1024) * 10 / 1024 : 0, lfb);
}

static unsigned char frame[W * H];

/* Mode 13h: 64,000 bytes a frame through the window at A0000h. */
static int vga(int frames)
{
    __dpmi_regs r;
    uclock_t t0, t1;
    int i;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x0013;
    __dpmi_int(0x10, &r);
    t0 = uclock();
    for (i = 0; i < frames; i++) {
        frame[0] = (unsigned char)i;
        movedata(_my_ds(), (unsigned)frame, _dos_ds, 0xA0000, 64000);
    }
    t1 = uclock();
    text_mode();
    report("vga", frames, t0, t1, 64000, 0xA0000);
    return 0;
}

/* VESA's 101h without its linear framebuffer: the same frame through the window, a bank at a time (4F05h). */
static int bank(int frames, const unsigned char *info)
{
    unsigned gran = *(const unsigned short *)(info + 4), i, b;
    __dpmi_regs r;
    uclock_t t0, t1;
    if (!gran || 64 % gran || *(const unsigned short *)(info + 8) != 0xA000) {
        printf("HX-LFBSPEED error=bank granularity=%u segment=%04x\n", gran, *(const unsigned short *)(info + 8));
        return 1;
    }
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02;
    r.x.bx = 0x0101;
    __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) {
        text_mode();
        printf("HX-LFBSPEED error=mode-set ax=%04x\n", r.x.ax);
        return 1;
    }
    t0 = uclock();
    for (i = 0; i < (unsigned)frames; i++) {
        frame[0] = (unsigned char)i;
        for (b = 0; b * 65536ul < (unsigned long)W * H; b++) {
            unsigned long left = (unsigned long)W * H - b * 65536ul;
            memset(&r, 0, sizeof r);
            r.x.ax = 0x4F05;
            r.x.dx = b * (64 / gran);
            __dpmi_int(0x10, &r);
            movedata(_my_ds(), (unsigned)(frame + b * 65536ul), _dos_ds, 0xA0000, left < 65536ul ? left : 65536ul);
        }
    }
    t1 = uclock();
    text_mode();
    report("bank", frames, t0, t1, (unsigned long)W * H, 0xA0000);
    return 0;
}

int main(int argc, char **argv)
{
    int frames = argc > 1 ? atoi(argv[1]) : 200, i, sel;
    const char *how = argc > 2 ? argv[2] : "lfb";
    unsigned long lfb, pitch;
    unsigned char info[256];
    __dpmi_regs r;
    __dpmi_meminfo mi;
    uclock_t t0, t1;
    if (frames < 1 || frames > 100000)
        frames = 200;
    for (i = 0; i < W * H; i++)
        frame[i] = (unsigned char)(i ^ (i >> 8));
    if (!strcmp(how, "vga"))
        return vga(frames);
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F01;
    r.x.cx = 0x101;
    r.x.es = __tb >> 4;
    r.x.di = __tb & 15;
    __dpmi_int(0x10, &r);
    dosmemget(__tb, sizeof info, info);
    lfb = *(unsigned long *)(info + 0x28);
    pitch = *(unsigned short *)(info + 0x10);
    if (!strcmp(how, "bank") && r.x.ax == 0x004F)
        return bank(frames, info);
    if (r.x.ax != 0x004F || !(info[0] & 0x80) || !lfb || pitch < W) {
        printf("HX-LFBSPEED error=no-lfb-mode ax=%04x attr=%02x lfb=%08lx\n", r.x.ax, info[0], lfb);
        return 1;
    }
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F02;
    r.x.bx = 0x4101;
    __dpmi_int(0x10, &r);
    if (r.x.ax != 0x004F) {
        text_mode();
        printf("HX-LFBSPEED error=mode-set ax=%04x\n", r.x.ax);
        return 1;
    }
    mi.address = lfb;
    mi.size = pitch * H;
    if (__dpmi_physical_address_mapping(&mi) != 0 || (sel = __dpmi_allocate_ldt_descriptors(1)) < 0
        || __dpmi_set_segment_base_address(sel, mi.address) != 0
        || __dpmi_set_segment_limit(sel, ((pitch * H + 4095) & ~4095ul) - 1) != 0) {
        text_mode();
        printf("HX-LFBSPEED error=map lfb=%08lx\n", lfb);
        return 1;
    }
    t0 = uclock();
    for (i = 0; i < frames; i++) {
        int y;
        frame[0] = (unsigned char)i;
        for (y = 0; y < H; y++)
            movedata(_my_ds(), (unsigned)(frame + y * W), sel, y * pitch, W);
    }
    t1 = uclock();
    text_mode();
    report("lfb", frames, t0, t1, (unsigned long)W * H, lfb);
    return 0;
}
