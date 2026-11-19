/* Host test for the virtual 8237A channels (kernel/vm/vdma.c), as the port sequences a game's sound code sends:
 * channel 1 set up for an auto-initialised block and taken in pieces, its count read back, its terminal count
 * in the status; a single block that masks itself at its end; an address that goes round within its page;
 * channel 5 in words; and what reaches the real chip: other channels' writes whole, shared registers with the
 * virtual channels' part taken out. Channel 1 and 5 are virtual here; the real channel 1 is nobody's (it stays
 * masked) and the real channel 5 is GLOS's own sound driver's (it is never masked by a program). */
#include <stdio.h>
#include "vdma.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static struct vdma d;
static struct vdma_real real;

static void out(u16 port, u8 val) { vdma_out(&d, port, val, &real); }

static int in(u16 port)                 /* a virtual register's byte; -1: the real chip's; -2: a merged status */
{
    u8 v = 0, m = 0;
    int r = vdma_in(&d, port, &v, &m);
    return r == 1 ? v : r == 2 ? -2 : -1;
}

static u16 in16(u16 port)
{
    int lo, hi;
    out(0x0C, 0);
    lo = in(port);
    hi = in(port);
    return (u16)(lo | hi << 8);
}

/* A game's set-up of channel 1: masked, the flip-flop cleared, its mode, address, page and count, unmasked. */
static void setup1(u8 mode, u32 phys, u16 bytes)
{
    out(0x0A, 0x05);
    CHECK(real.n == 0);
    out(0x0C, 0x00);
    CHECK(real.n == 1 && real.port[0] == 0x0C);
    out(0x0B, mode);
    CHECK(real.n == 0);
    out(0x02, (u8)phys);
    out(0x02, (u8)(phys >> 8));
    CHECK(real.n == 0);
    out(0x83, (u8)(phys >> 16));
    CHECK(real.n == 0);
    out(0x03, (u8)(bytes - 1));
    out(0x03, (u8)((bytes - 1) >> 8));
    out(0x0A, 0x01);
    CHECK(real.n == 0);
}

int main(void)
{
    u32 phys = 0;
    u8 v = 0, m = 0;

    vdma_reset(&d, 1 << 1 | 1 << 5, 1 << 1);
    CHECK(vdma_port(0x02) && vdma_port(0x83) && vdma_port(0xD4) && vdma_port(0x8B) && !vdma_port(0x10) && !vdma_port(0xC1)
          && !vdma_port(0x80) && !vdma_port(0x220));
    CHECK(!vdma_running(&d, 1) && !vdma_running(&d, 2));
    CHECK(vdma_take(&d, 1, 100, &phys) == 0);

    /* An auto-initialised block of 4096 bytes at 24000h, taken in two pieces. */
    setup1(0x59, 0x24000, 4096);
    CHECK(vdma_running(&d, 1));
    CHECK(in(0x83) == 0x02);
    CHECK(in16(0x02) == 0x4000 && in16(0x03) == 4095);
    CHECK(vdma_take(&d, 1, 1000, &phys) == 1000 && phys == 0x24000);
    CHECK(in16(0x02) == 0x4000 + 1000 && in16(0x03) == 4095 - 1000);
    CHECK(vdma_in(&d, 0x08, &v, &m) == 2 && v == 0 && m == 0x22);
    CHECK(vdma_take(&d, 1, 5000, &phys) == 3096 && phys == 0x24000 + 1000);
    CHECK(vdma_in(&d, 0x08, &v, &m) == 2 && v == 0x02 && m == 0x22);    /* its terminal count, once */
    CHECK(vdma_in(&d, 0x08, &v, &m) == 2 && v == 0);
    CHECK(vdma_running(&d, 1) && in16(0x02) == 0x4000 && in16(0x03) == 4095);  /* from the start again */
    CHECK(vdma_take(&d, 1, 16, &phys) == 16 && phys == 0x24000);

    /* A single block: masked at its end, and nothing more comes. */
    setup1(0x49, 0x31000, 512);
    CHECK(vdma_take(&d, 1, 512, &phys) == 512 && phys == 0x31000);
    CHECK(!vdma_running(&d, 1) && vdma_take(&d, 1, 16, &phys) == 0);
    CHECK(in16(0x03) == 0xFFFF);
    CHECK(vdma_in(&d, 0x08, &v, &m) == 2 && v == 0x02);

    /* An address that goes round within its page: a piece to the page's end, then from its start. */
    setup1(0x49, 0x5FF00, 512);
    CHECK(vdma_take(&d, 1, 1000, &phys) == 256 && phys == 0x5FF00);
    CHECK(vdma_take(&d, 1, 1000, &phys) == 256 && phys == 0x50000);
    CHECK(!vdma_running(&d, 1));

    /* Channel 5, in words: 4096 bytes at 30000h. */
    out(0xD4, 0x05);
    CHECK(real.n == 0);
    out(0xD8, 0x00);
    CHECK(real.n == 1 && real.port[0] == 0xD8);
    out(0xD6, 0x59);
    out(0xC4, 0x00);
    out(0xC4, 0x80);
    out(0x8B, 0x02);
    out(0xC6, 0xFF);
    out(0xC6, 0x07);
    out(0xD4, 0x01);
    CHECK(real.n == 0 && vdma_running(&d, 5));
    CHECK(vdma_take(&d, 5, 1001, &phys) == 1000 && phys == 0x30000);    /* (whole words) */
    out(0xD8, 0);
    CHECK(in(0xC6) == ((2047 - 500) & 0xFF) && in(0xC6) == (2047 - 500) >> 8);
    CHECK(vdma_take(&d, 5, 8192, &phys) == 3096 && phys == 0x30000 + 1000);
    CHECK(vdma_in(&d, 0xD0, &v, &m) == 2 && v == 0x02 && m == 0x22);

    /* The real chip's own channels: a program's writes for them go through whole. */
    out(0x0A, 0x06);
    CHECK(real.n == 1 && real.port[0] == 0x0A && real.val[0] == 0x06);
    out(0x0B, 0x46);
    CHECK(real.n == 1 && real.port[0] == 0x0B && real.val[0] == 0x46);
    out(0x0C, 0);
    out(0x04, 0x34);
    CHECK(real.n == 1 && real.port[0] == 0x04 && real.val[0] == 0x34);
    out(0x02, 0x12);                            /* (the flip-flop is one: this is channel 1's high byte) */
    CHECK(real.n == 0 && in16(0x02) == 0x1200 + (0x5FF00 & 0xFF));
    out(0x81, 0x07);
    CHECK(real.n == 1 && real.port[0] == 0x81);
    CHECK(in(0x81) == -1 && in(0x05) == -1 && in(0x0D) == -1);
    out(0x08, 0x00);
    CHECK(real.n == 1 && real.port[0] == 0x08);

    /* Registers every channel shares: the virtual channels' part is kept here, and the real chip gets the
       rest, its channel 1 masked still and its channel 5 never masked. */
    out(0x0A, 0x01);
    out(0x0F, 0x0D);                            /* all masked but channel 1 */
    CHECK(vdma_running(&d, 1) && real.n == 1 && real.port[0] == 0x0F && real.val[0] == 0x0F);
    out(0x0F, 0x02);                            /* channel 1 alone masked */
    CHECK(!vdma_running(&d, 1) && real.n == 1 && real.val[0] == 0x02);
    out(0xDE, 0x0F);
    CHECK(!vdma_running(&d, 5) && real.n == 1 && real.port[0] == 0xDE && real.val[0] == 0x0D);
    out(0x0E, 0x00);                            /* every mask cleared: the real channel 1 masked again after */
    CHECK(vdma_running(&d, 1) && real.n == 2 && real.port[0] == 0x0E && real.port[1] == 0x0A && real.val[1] == 0x05);
    out(0xDC, 0x00);
    CHECK(vdma_running(&d, 5) && real.n == 1 && real.port[0] == 0xDC);
    out(0xDA, 0x00);                            /* master clear: the real chip's flip-flop and masks, channel 5 left */
    CHECK(!vdma_running(&d, 5) && real.n == 2 && real.port[0] == 0xD8 && real.port[1] == 0xDE && real.val[1] == 0x0D);
    out(0x0D, 0x00);
    CHECK(!vdma_running(&d, 1) && real.n == 2 && real.port[0] == 0x0C && real.port[1] == 0x0F && real.val[1] == 0x0F);
    out(0x09, 0x05);                            /* a request by software for a virtual channel: none is made */
    CHECK(real.n == 0);
    out(0x09, 0x06);
    CHECK(real.n == 1);

    /* With no virtual channel on a controller, everything of it is the real chip's. */
    vdma_reset(&d, 1 << 1, 1 << 1);
    out(0xDA, 0x00);
    CHECK(real.n == 1 && real.port[0] == 0xDA);
    out(0xD4, 0x05);
    CHECK(real.n == 1);
    CHECK(vdma_in(&d, 0xD0, &v, &m) == 0 && in(0x8B) == -1);

    printf("vdma_test: %d failures\n", fails);
    return fails != 0;
}
