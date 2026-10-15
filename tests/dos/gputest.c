/* GPUTEST: a GLOS app that draws with the Matrox card's engine itself, from
 * its own copy of MGA-Glide's HAL, as DOS-GL does (M8 steps B and C; the
 * "gpu" suite of tests/loopa/jobs.py). DJGPP, with sdk/libglos and the
 * HAL's DJGPP port.
 *
 *   GPUTEST info               what GLOS says of the card (GLOS_FN_VERSION's
 *                              capabilities, GLOS_FN_GPU)
 *   GPUTEST draw NAME SECONDS [X Y W H]
 *                              a window that shows two buffers of the app's
 *                              own VRAM in turn: in each the engine fills
 *                              the background (a colour by NAME's first
 *                              letter), a white square that moves along and
 *                              a copy of it below; after each frame the
 *                              pixels are read back through the app's view
 *                              of VRAM and checked, and the window is told
 *                              to show that buffer (GLOS_FN_WINVRAM). 50
 *                              frames a second at most; ends when the
 *                              window is asked to close or SECONDS have
 *                              passed (41)
 *   GPUTEST spin NAME SECONDS [X Y W H]
 *                              the same, never sleeping or asking for events
 *   GPUTEST raw                the registers touched without asking for the
 *                              lock: the fault takes it
 *   GPUTEST hang NAME          a window, the lock, and then a loop for ever
 *                              that reads the engine's status and calls
 *                              nothing: GLOS's watchdog is to end it
 *   GPUTEST vram KB            a block of VRAM of that size, held until the
 *                              program ends (exit code 0), for the memory's
 *                              coming back
 *
 * Before each piece of work with the engine it does what DOS-GL does: the
 * lock if the shared page says it hasn't got it, engine_resync() if someone
 * else had the engine since (`lost` moved).
 *
 * Lines: "GPU-INFO ...", "WIN name=...", "GPU-OK NAME frames=N bad=B
 * lost=L ...", "GPU-ERR ...". */
#include <crt0.h>
#include <dpmi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glos/api.h>

#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"

int _crt0_startup_flags = _CRT0_FLAG_UNIX_SBRK; /* (glos_ptr()'s pointers stay where they are) */

static struct glos_gpu gg;
static volatile struct glos_gpu_share *share;
static unsigned seen, resyncs;
static struct glos_window win;
static const char *name = "?";
static unsigned buf_off[2], pitch_b;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

static void die(int code, const char *what)
{
    say("GPU-ERR %s %s error=%d reason=%d\n", name, what, glos_error(), glos_reason());
    exit(code);
}

static unsigned rgb565(unsigned c) { return ((c >> 8) & 0xF800) | ((c >> 5) & 0x07E0) | ((c >> 3) & 0x001F); }

static unsigned colour(void)
{
    switch (name[0]) {
    case 'A': return 0xCC3333;
    case 'B': return 0x3333CC;
    case 'C': return 0x33CC33;
    default: return 0x999999;
    }
}

/* The engine the app's own, and its HAL told if it wasn't the last owner. */
static void enter(void)
{
    if (!share->held && glos_gpu_lock() != 0)
        die(43, "lock");
    if (share->lost != seen) {
        seen = share->lost;
        engine_resync();
        resyncs++;
    }
}

/* The card as GLOS describes it, in the HAL's own terms. */
static void card(void)
{
    if (glos_gpu(&gg) != 0)
        die(44, "gpu");
    memset(&mga, 0, sizeof mga);
    mga.device_id = (uint16_t)gg.device;
    mga.revision = (uint8_t)gg.revision;
    mga.family = gg.device == 0x0525 ? MGA_FAMILY_G400 : gg.device == 0x0520 || gg.device == 0x0521 ? MGA_FAMILY_G200
               : MGA_FAMILY_G100;
    mga.fb_phys = gg.fb_phys;
    mga.mmio_phys = gg.regs_phys;
    mga.option = gg.option;
    mga.subsys = gg.subsys;
    mga.vram_bytes = gg.vram;
    mga_chip_caps(&mga);
    mga_mmio = glos_ptr(gg.regs);
    mga_fb = glos_ptr(gg.base);
    share = glos_ptr(gg.share);
    if (!mga_mmio || !mga_fb || !share)
        die(44, "pointers");
}

static void info(void)
{
    say("GPU-INFO version=%x caps=%x\n", glos_version(), glos_caps());
    if (glos_init() != 0)
        die(44, "init");
    card();
    say("GPU-INFO device=%04x revision=%02x vram=%u regs=%x base=%x share=%x bpp=%u family=%d\n", gg.device,
        gg.revision, gg.vram, gg.regs, gg.base, gg.share, gg.bpp, mga.family);
}

static void window(int x, int y, int w, int h)
{
    unsigned bytes;
    int i;
    char title[48];
    if (glos_init() != 0)
        die(44, "init");
    card();
    memset(&win, 0, sizeof win);
    win.x = x, win.y = y, win.w = w, win.h = h;
    win.flags = GLOS_WIN_FIXED;
    sprintf(title, "GPUTEST %s", name);
    strcpy(win.title, title);
    if (glos_window(&win) != 0)
        die(44, "window");
    pitch_b = ((unsigned)w + 31) / 32 * 32 * 2;
    for (i = 0; i < 2; i++)
        if (glos_vram(pitch_b * (unsigned)h, 0, 4096, &buf_off[i], &bytes) != 0)
            die(44, "vram");
    glos_show(win.id, 1);
    say("WIN name=%s id=%u bufs=%x,%x pitch=%u\n", name, win.id, buf_off[0], buf_off[1], pitch_b);
}

static void target(unsigned off)
{
    mga_target t;
    memset(&t, 0, sizeof t);
    t.color_off = off;
    t.pitch_px = (int)(pitch_b / 2);
    t.bpp = 16;
    engine_set_target(&t);
    engine_set_clip(0, 0, win.w, win.h);
}

static unsigned px(unsigned off, int x, int y) { return *(volatile uint16_t *)(mga_fb + off + (unsigned)y * pitch_b + (unsigned)x * 2); }

/* A frame in buffer b: the background, the square at sx, its copy below;
   then the pixels checked. The number of wrong ones. */
static int frame(int b, int sx)
{
    unsigned bg = rgb565(colour()), fg = 0xFFFF, off = buf_off[b];
    int bad = 0, q = win.h / 4, x, y;
    enter();
    target(off);
    engine_fill(0, 0, win.w, win.h, bg);
    engine_fill(sx, 4, q, q, fg);
    engine_copy(sx, 4, sx, 8 + q, q, q);
    if (engine_sync(1000000) != 0)
        return -1;
    for (y = 0; y < win.h; y += 7)
        for (x = 0; x < win.w; x += 13) {
            int in = x >= sx && x < sx + q && ((y >= 4 && y < 4 + q) || (y >= 8 + q && y < 8 + 2 * q));
            if (px(off, x, y) != (in ? fg : bg))
                bad++;
        }
    return bad;
}

static int run(int argc, char **argv, int spin)
{
    int secs, x = 40, y = 40, w = 200, h = 120, b = 0, sx = 0, n = 0, bad = 0, worst = 0;
    clock_t end;
    struct glos_event ev;
    if (argc < 4)
        return 2;
    name = argv[2];
    secs = atoi(argv[3]);
    if (argc >= 8)
        x = atoi(argv[4]), y = atoi(argv[5]), w = atoi(argv[6]), h = atoi(argv[7]);
    window(x, y, w, h);
    end = clock() + (clock_t)secs * CLOCKS_PER_SEC;
    while (clock() < end) {
        int r = frame(b, sx);
        if (r < 0) {
            say("GPU-ERR %s engine-timeout frame=%d\n", name, n);
            return 45;
        }
        if (r) {
            bad++;
            if (r > worst)
                worst = r;
        }
        if (glos_winvram(win.id, buf_off[b], pitch_b) != 0)
            die(45, "winvram");
        b ^= 1;
        sx = (sx + 3) % (win.w - win.h / 4);
        n++;
        if (spin)
            continue;
        while (glos_event(&ev, 0))
            if (ev.type == GLOS_EV_CLOSE) {
                say("GPU-OK %s frames=%d bad=%d worst=%d lost=%u resyncs=%u closed\n", name, n, bad, worst,
                    share->lost, resyncs);
                return 40;
            }
        glos_sleep(20);
    }
    say("GPU-OK %s frames=%d bad=%d worst=%d lost=%u resyncs=%u\n", name, n, bad, worst, share->lost, resyncs);
    return 41;
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    if (!strcmp(argv[1], "info")) {
        info();
        return 0;
    }
    if (!strcmp(argv[1], "draw"))
        return run(argc, argv, 0);
    if (!strcmp(argv[1], "spin"))
        return run(argc, argv, 1);
    if (!strcmp(argv[1], "raw")) {             /* no lock asked for: the first touch faults and takes it */
        uint32_t st;
        name = "raw";
        if (glos_init() != 0)
            die(44, "init");
        card();
        say("GPU-RAW held=%u\n", share->held);
        st = MGA_RD32(MGAREG_STATUS);
        say("GPU-RAW status=%08lx held=%u lost=%u\n", (unsigned long)st, share->held, share->lost);
        return share->held ? 0 : 46;
    }
    if (!strcmp(argv[1], "hang") && argc >= 3) {
        name = argv[2];
        window(40, 200, 160, 100);
        enter();
        target(buf_off[0]);
        engine_fill(0, 0, win.w, win.h, rgb565(colour()));
        say("GPU-HANG %s held=%u\n", name, share->held);
        for (;;)
            (void)MGA_RD32(MGAREG_STATUS);
    }
    if (!strcmp(argv[1], "vram") && argc >= 3) {
        unsigned off, bytes, kb = (unsigned)atoi(argv[2]);
        name = "vram";
        if (glos_init() != 0)
            die(44, "init");
        card();
        if (glos_vram(kb * 1024, 0, 4096, &off, &bytes) != 0)
            die(44, "vram");
        *(volatile uint32_t *)(mga_fb + off) = 0x12345678;     /* (its view: the block is the app's to write) */
        say("GPU-VRAM off=%x bytes=%u read=%08lx\n", off, bytes, (unsigned long)*(volatile uint32_t *)(mga_fb + off));
        return 0;
    }
    return 2;
}
