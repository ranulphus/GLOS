/* ems_test: kernel/vm/ems.c (N4's EMS, E1) with frames of malloc's and a page frame whose mapping is recorded:
 * each INT 67h function and its errors as LIM EMS 4.0 has them, 57h's moves and exchanges checked byte for byte
 * against memmove (overlaps both ways, across pages, conventional memory either end), the frames all given back
 * at the end. "ems_test: 0 failures" when all held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/vm/ems.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; } } while (0)

#define FRAMES 40
static unsigned char *frame[FRAMES];
static int frame_used[FRAMES], frames_out;
static unsigned long shown[EMS_PHYS];
static unsigned char mem[0x110000];

static void f_map(void *c, int p, unsigned long id) { (void)c; shown[p] = id; }
static unsigned long f_alloc(void *c)
{
    int i;
    (void)c;
    for (i = 0; i < FRAMES; i++)
        if (!frame_used[i]) {
            frame_used[i] = 1, frames_out++;
            if (!frame[i])
                frame[i] = malloc(EMS_PAGE);
            memset(frame[i], 0xEE, EMS_PAGE);
            return (unsigned long)i;
        }
    return EMS_UNMAPPED;
}
static void f_free(void *c, unsigned long id) { (void)c; if (frame_used[id]) frame_used[id] = 0, frames_out--; }
static unsigned char *f_page(void *c, unsigned long id, unsigned off) { (void)c; return frame[id] + off; }
static void *f_grow(void *old, unsigned long n) { if (!n) { free(old); return 0; } return realloc(old, n); }

static struct ems e;
static struct ems_regs r;

static unsigned call(unsigned ax)
{
    r.ax = ax;
    ems_call(&e, &r);
    return r.ax >> 8;
}

static void w16(unsigned long at, unsigned v) { mem[at] = (unsigned char)v, mem[at + 1] = (unsigned char)(v >> 8); }
static unsigned g16(unsigned long at) { return mem[at] | mem[at + 1] << 8; }

/* 57h's descriptor at 2000:0000: length; source type, handle, offset, segment or page; the same for the destination. */
static unsigned move(int xch, unsigned long len, int st, unsigned sh, unsigned so, unsigned ss, int dt, unsigned dh, unsigned dofs,
                     unsigned ds)
{
    unsigned long d = 0x20000;
    w16(d, (unsigned)len), w16(d + 2, (unsigned)(len >> 16));
    mem[d + 4] = (unsigned char)st, w16(d + 5, sh), w16(d + 7, so), w16(d + 9, ss);
    mem[d + 11] = (unsigned char)dt, w16(d + 12, dh), w16(d + 14, dofs), w16(d + 16, ds);
    r.ds = 0x2000, r.si = 0;
    return call(0x5700 | (xch ? 1 : 0));
}

/* An expanded region's byte as the handle's pages hold it. */
static unsigned char eb(unsigned h, unsigned long at) { return frame[e.h[h].page[at / EMS_PAGE]][at % EMS_PAGE]; }

int main(void)
{
    static const struct ems_ops ops = { 0, f_map, f_alloc, f_free, f_page };
    unsigned h1, h2, h3, k;
    unsigned long i;
    int ok;

    ems_init(&e, &ops, 32, 0xE000, mem, sizeof mem, f_grow);
    CHECK(call(0x4000) == 0, "40h");
    CHECK(call(0x4100) == 0 && r.bx == 0xE000, "41h: %x", r.bx);
    CHECK(call(0x4200) == 0 && r.bx == 32 && r.dx == 32, "42h: %u %u", r.bx, r.dx);
    CHECK(call(0x4600) == 0 && (r.ax & 0xFF) == 0x40, "46h: %x", r.ax);
    r.bx = 0;
    CHECK(call(0x4300) == 0x89, "43h of none: 89h");
    r.bx = 33;
    CHECK(call(0x4300) == 0x87, "43h past all: 87h");
    r.bx = 4;
    CHECK(call(0x4300) == 0 && r.dx == 1, "43h of 4: handle %u", r.dx);
    h1 = r.dx;
    r.bx = 29;
    CHECK(call(0x4300) == 0x88, "43h past what's free: 88h");
    CHECK(call(0x4200) == 0 && r.bx == 28 && frames_out == 4, "42h after: %u free, %d frames out", r.bx, frames_out);
    /* 44h */
    for (k = 0; k < 4; k++) {
        r.bx = k, r.dx = h1;
        CHECK(call(0x4400 | k) == 0 && shown[k] == e.h[h1].page[k], "44h page %u", k);
    }
    r.bx = 0, r.dx = h1;
    CHECK(call(0x4404) == 0x8B, "44h physical 4: 8Bh");
    r.bx = 4, r.dx = h1;
    CHECK(call(0x4400) == 0x8A, "44h logical 4: 8Ah");
    r.bx = 0, r.dx = 9;
    CHECK(call(0x4400) == 0x83, "44h of no handle: 83h");
    r.bx = 0xFFFF, r.dx = h1;
    CHECK(call(0x4403) == 0 && shown[3] == EMS_UNMAPPED, "44h FFFFh: unmapped");
    /* 47h, 48h */
    r.dx = h1;
    CHECK(call(0x4700) == 0, "47h");
    CHECK(call(0x4700) == 0x8D, "47h again: 8Dh");
    r.bx = 3, r.dx = h1;
    call(0x4400);
    CHECK(shown[0] == e.h[h1].page[3], "44h changed page 0");
    r.dx = h1;
    CHECK(call(0x4800) == 0 && shown[0] == e.h[h1].page[0] && shown[3] == EMS_UNMAPPED, "48h: the map as saved");
    CHECK(call(0x4800) == 0x8E, "48h with none saved: 8Eh");
    /* 4Bh, 4Ch, 4Dh */
    r.bx = 2;
    call(0x4300);
    h2 = r.dx;
    CHECK(call(0x4B00) == 0 && r.bx == 3, "4Bh: %u (0, and two)", r.bx);
    r.dx = h2;
    CHECK(call(0x4C00) == 0 && r.bx == 2, "4Ch: %u", r.bx);
    r.es = 0x3000, r.di = 0;
    CHECK(call(0x4D00) == 0 && r.bx == 3 && g16(0x30004) == h1 && g16(0x30006) == 4 && g16(0x3000A) == 2, "4Dh");
    /* 4Eh */
    CHECK(call(0x4E03) == 0 && (r.ax & 0xFF) == 16, "4Eh size");
    r.es = 0x3000, r.di = 0;
    CHECK(call(0x4E00) == 0 && g16(0x30000) == h1 && g16(0x30002) == 0 && g16(0x3000C) == 0xFFFF, "4Eh get");
    r.bx = 0, r.dx = h2;
    call(0x4400);
    r.ds = 0x3000, r.si = 0;
    CHECK(call(0x4E01) == 0 && shown[0] == e.h[h1].page[0], "4Eh set");
    w16(0x30000, 7);
    CHECK(call(0x4E01) == 0xA3 && shown[0] == e.h[h1].page[0], "4Eh set of a map that isn't one: A3h");
    w16(0x30000, h1);
    /* 4Fh */
    w16(0x31000, 2), w16(0x31002, 0xE400), w16(0x31004, 0xEC00);
    r.ds = 0x3100, r.si = 0, r.es = 0x3200, r.di = 0;
    CHECK(call(0x4F00) == 0 && g16(0x32000) == 2 && g16(0x32002) == 0xE400 && g16(0x32004) == h1 && g16(0x32006) == 1
          && g16(0x32008) == 0xEC00 && g16(0x3200A) == 0xFFFF, "4Fh get");
    r.bx = 1, r.dx = h2;
    call(0x4401);
    r.ds = 0x3200, r.si = 0;
    CHECK(call(0x4F01) == 0 && shown[1] == e.h[h1].page[1], "4Fh set");
    w16(0x31002, 0xE300);
    r.ds = 0x3100, r.si = 0;
    CHECK(call(0x4F00) == 0x8B, "4Fh of a segment outside the frame: 8Bh");
    r.bx = 3;
    CHECK(call(0x4F02) == 0 && (r.ax & 0xFF) == 20, "4Fh size: %u", r.ax & 0xFF);
    /* 50h */
    w16(0x33000, 1), w16(0x33002, 2), w16(0x33004, 0), w16(0x33006, 3);
    r.ds = 0x3300, r.si = 0, r.cx = 2, r.dx = h2;
    CHECK(call(0x5000) == 0 && shown[2] == e.h[h2].page[1] && shown[3] == e.h[h2].page[0], "50h by physical page");
    w16(0x33000, 0), w16(0x33002, 0xE800);
    r.ds = 0x3300, r.si = 0, r.cx = 1, r.dx = h1;
    CHECK(call(0x5001) == 0 && shown[2] == e.h[h1].page[0], "50h by segment");
    w16(0x33000, 5);
    r.ds = 0x3300, r.si = 0, r.cx = 1, r.dx = h1;
    CHECK(call(0x5001) == 0x8A, "50h past the handle's pages: 8Ah");
    /* 51h */
    r.dx = h2, r.bx = 6;
    CHECK(call(0x5100) == 0 && r.bx == 6 && e.free == 22, "51h grown: %u, %u free", r.bx, e.free);
    r.dx = h2, r.bx = 3, r.cx = 0;
    w16(0x33000, 5), w16(0x33002, 1);
    r.ds = 0x3300, r.si = 0, r.cx = 1;
    call(0x5000);
    CHECK(shown[1] == e.h[h2].page[5], "page 5 shown");
    r.dx = h2, r.bx = 3;
    CHECK(call(0x5100) == 0 && r.bx == 3 && shown[1] == EMS_UNMAPPED && e.free == 25, "51h shrunk: its shown page gone");
    /* 52h, 53h, 54h */
    r.dx = h1;
    CHECK(call(0x5200) == 0 && (r.ax & 0xFF) == 0, "52h volatile");
    r.dx = h1, r.bx = 1;
    CHECK(call(0x5201) == 0x91, "52h nonvolatile: 91h");
    memcpy(mem + 0x34000, "GAMEDATA", 8);
    r.dx = h1, r.ds = 0x3400, r.si = 0;
    CHECK(call(0x5301) == 0, "53h set");
    r.dx = h2;
    CHECK(call(0x5301) == 0xA1, "53h the same name twice: A1h");
    r.dx = h1, r.es = 0x3500, r.di = 0;
    CHECK(call(0x5300) == 0 && !memcmp(mem + 0x35000, "GAMEDATA", 8), "53h get");
    r.ds = 0x3400, r.si = 0;
    CHECK(call(0x5401) == 0 && r.dx == h1, "54h by name");
    memcpy(mem + 0x34000, "NOSUCH..", 8);
    CHECK(call(0x5401) == 0xA0, "54h a name not there: A0h");
    r.es = 0x3600, r.di = 0;
    CHECK(call(0x5400) == 0 && (r.ax & 0xFF) == 3 && !memcmp(mem + 0x3600C, "GAMEDATA", 8), "54h directory");
    CHECK(call(0x5402) == 0 && r.bx == EMS_HANDLES, "54h handles");
    /* 57h */
    for (i = 0; i < 0x10000; i++)
        mem[0x40000 + i] = (unsigned char)(i * 7 + (i >> 9));
    CHECK(move(0, 40000, 0, 0, 0, 0x4000, 1, h1, 100, 0) == 0, "57h conventional to expanded");
    for (ok = 1, i = 0; i < 40000; i++)
        ok &= eb(h1, 100 + i) == mem[0x40000 + i];
    CHECK(ok, "... its bytes, across three pages");
    CHECK(move(0, 40000, 1, h1, 100, 0, 0, 0, 0, 0x6000) == 0, "57h expanded to conventional");
    CHECK(!memcmp(mem + 0x60000, mem + 0x40000, 40000), "... back the same");
    CHECK(move(0, 5000, 1, h1, 0, 0, 1, h1, 3000, 0) == 0x92, "57h overlapping forward: 92h");
    {
        static unsigned char want[4 * EMS_PAGE];
        for (i = 0; i < 4 * EMS_PAGE; i++)
            want[i] = eb(h1, i);
        CHECK(move(0, 9000, 1, h1, 500, 1, 1, h1, 1000, 1) == 0x92, "57h overlapping, the destination after: 92h");
        memmove(want + EMS_PAGE + 1000, want + EMS_PAGE + 500, 9000);
        for (ok = 1, i = 0; i < 4 * EMS_PAGE; i++)
            ok &= eb(h1, i) == want[i];
        CHECK(ok, "... the same as memmove's");
        CHECK(move(0, 9000, 1, h1, 1000, 1, 1, h1, 500, 1) == 0x92, "57h overlapping, the destination before");
        memmove(want + EMS_PAGE + 500, want + EMS_PAGE + 1000, 9000);
        for (ok = 1, i = 0; i < 4 * EMS_PAGE; i++)
            ok &= eb(h1, i) == want[i];
        CHECK(ok, "... the same as memmove's");
    }
    memset(mem + 0x70000, 0x11, 1000);
    CHECK(move(1, 1000, 0, 0, 0, 0x7000, 1, h2, 0, 2) == 0, "57h exchange");
    CHECK(mem[0x70000] == 0xEE && eb(h2, 2 * EMS_PAGE) == 0x11, "... both sides changed");
    CHECK(move(1, 1000, 1, h1, 0, 0, 1, h1, 500, 0) == 0x97, "57h exchange overlapping: 97h");
    CHECK(move(0, 0x100001, 0, 0, 0, 0, 0, 0, 0, 0x1000) == 0x96, "57h past 1 MB: 96h");
    CHECK(move(0, 10, 1, h1, EMS_PAGE, 0, 0, 0, 0, 0x1000) == 0x95, "57h an offset past the page: 95h");
    CHECK(move(0, 10, 1, h1, 0, 4, 0, 0, 0, 0x1000) == 0x93, "57h past the handle's pages: 93h");
    CHECK(move(0, 10, 1, 9, 0, 0, 0, 0, 0, 0x1000) == 0x83, "57h of no handle: 83h");
    /* 58h, 59h, 5Ah, 5Bh */
    r.es = 0x3700, r.di = 0;
    CHECK(call(0x5800) == 0 && r.cx == 4 && g16(0x37000) == 0xE000 && g16(0x3700C) == 0xEC00 && g16(0x3700E) == 3, "58h");
    CHECK(call(0x5901) == 0 && r.bx == e.free && r.dx == 32, "59h raw pages");
    r.bx = 0;
    CHECK(call(0x5A00) == 0, "5Ah of none (allowed)");
    h3 = r.dx;
    r.dx = h3;
    CHECK(call(0x4C00) == 0 && r.bx == 0, "... with no pages");
    r.bx = 0x1234;
    CHECK(call(0x5B03) == 0 && (r.bx & 0xFF) == 0, "5Bh: no alternate sets");
    CHECK(call(0x5500) == 0x84 && call(0x5D00) == 0x84, "55h, 5Dh: 84h");
    /* 45h */
    r.dx = h1;
    call(0x4700);
    CHECK(call(0x4500) == 0x86, "45h with a map saved: 86h");
    r.dx = h1;
    call(0x4800);
    r.dx = h1;
    CHECK(call(0x4500) == 0 && shown[0] == EMS_UNMAPPED, "45h: its shown pages gone");
    r.dx = h2;
    call(0x4500);
    r.dx = h3;
    call(0x4500);
    CHECK(call(0x4200) == 0 && r.bx == 32 && frames_out == 0, "every frame back: %u free, %d out", r.bx, frames_out);
    r.dx = 0;
    CHECK(call(0x4500) == 0 && e.h[0].used, "45h of handle 0: kept");
    r.bx = 1;
    CHECK(call(0x4300) == 0 && r.dx == 1, "a handle given back is given again: %u", r.dx);
    printf("ems_test: %d failures\n", fails);
    return fails != 0;
}
