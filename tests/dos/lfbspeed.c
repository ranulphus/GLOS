/* LFBSPEED (DJGPP): how fast whole frames are written to a VBE linear
 * framebuffer, for the write-combining trial (glos mtrr lfb on: kernel/arch/
 * mtrr.c). The mode 640x480 in 8 bits (VESA's 101h) with its linear
 * framebuffer, mapped by DPMI 0800h as a game's renderer has it; a frame of
 * 307,200 bytes copied from memory a number of times (LFBSPEED [frames], 200
 * unless said), timed by the BIOS's clock; then text mode again and one line:
 *   HX-LFBSPEED frames=200 ms=1234 mb_s=49.7 lfb=e0000000
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

int main(int argc, char **argv)
{
    static unsigned char frame[W * H];
    int frames = argc > 1 ? atoi(argv[1]) : 200, i, sel;
    unsigned long lfb, pitch;
    unsigned char info[256];
    __dpmi_regs r;
    __dpmi_meminfo mi;
    uclock_t t0, t1;
    unsigned long ms, kb;
    if (frames < 1 || frames > 100000)
        frames = 200;
    memset(&r, 0, sizeof r);
    r.x.ax = 0x4F01;
    r.x.cx = 0x101;
    r.x.es = __tb >> 4;
    r.x.di = __tb & 15;
    __dpmi_int(0x10, &r);
    dosmemget(__tb, sizeof info, info);
    lfb = *(unsigned long *)(info + 0x28);
    pitch = *(unsigned short *)(info + 0x10);
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
    for (i = 0; i < W * H; i++)
        frame[i] = (unsigned char)(i ^ (i >> 8));
    t0 = uclock();
    for (i = 0; i < frames; i++) {
        int y;
        frame[0] = (unsigned char)i;
        for (y = 0; y < H; y++)
            movedata(_my_ds(), (unsigned)(frame + y * W), sel, y * pitch, W);
    }
    t1 = uclock();
    text_mode();
    ms = (unsigned long)((t1 - t0) * 1000 / UCLOCKS_PER_SEC);
    kb = (unsigned long)frames * (W * H / 1024);
    printf("HX-LFBSPEED frames=%d ms=%lu mb_s=%lu.%lu lfb=%08lx\n", frames, ms, ms ? kb * 1000 / ms / 1024 : 0,
           ms ? (kb * 1000 / ms % 1024) * 10 / 1024 : 0, lfb);
    return 0;
}
