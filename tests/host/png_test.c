/* Host test for glos shot's PNG writer (kernel/lib/png.c): every chunk's
 * CRC, the zlib stream's stored blocks and Adler-32, and every pixel, read
 * back by an independent parser; build/host/png_test.png is left for an
 * outside decoder to check too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

void *kmalloc(size_t n) { return malloc(n); }

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static u32 crc_bitwise(const u8 *p, u32 n)
{
    u32 c = 0xFFFFFFFFu, k;
    while (n--)
        for (c ^= *p++, k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    return ~c;
}

static u8 pixel(u32 x, u32 y) { return (u8)((x * 3 + y * 5 + (x ^ y)) & 15); }

static void check(u32 w, u32 h, const char *file)
{
    u8 rgb[16][3], *pix = malloc(w * h), *png, *raw = malloc((1 + (w + 1) / 2) * h + 16), *idat = 0;
    u32 len, x, y, p = 8, n_idat = 0, rawlen = 0, i, a = 1, b = 0, row = 1 + (w + 1) / 2;
    int seen_ihdr = 0, seen_iend = 0;
    for (i = 0; i < 16; i++)
        rgb[i][0] = (u8)(i * 17), rgb[i][1] = (u8)(255 - i), rgb[i][2] = (u8)i;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            pix[y * w + x] = pixel(x, y);
    png = png_indexed4(w, h, (const u8 (*)[3])rgb, pix, &len);
    CHECK(png && !memcmp(png, "\x89PNG\r\n\x1a\n", 8));
    while (p + 12 <= len) {
        u32 n = be32(png + p);
        CHECK(be32(png + p + 8 + n) == crc_bitwise(png + p + 4, n + 4));
        if (!memcmp(png + p + 4, "IHDR", 4)) {
            seen_ihdr = 1;
            CHECK(n == 13 && be32(png + p + 8) == w && be32(png + p + 12) == h);
            CHECK(png[p + 16] == 4 && png[p + 17] == 3);
        } else if (!memcmp(png + p + 4, "PLTE", 4)) {
            CHECK(n == 48 && !memcmp(png + p + 8, rgb, 48));
        } else if (!memcmp(png + p + 4, "IDAT", 4)) {
            idat = png + p + 8;
            n_idat = n;
        } else if (!memcmp(png + p + 4, "IEND", 4)) {
            seen_iend = 1;
        }
        p += 12 + n;
    }
    CHECK(seen_ihdr && seen_iend && p == len && idat);
    if (!idat)
        return;
    CHECK(idat[0] == 0x78 && (idat[0] * 256 + idat[1]) % 31 == 0);
    for (p = 2;;) {                             /* the stored blocks */
        u32 last = idat[p] & 1, bl = idat[p + 1] | idat[p + 2] << 8, nl = idat[p + 3] | idat[p + 4] << 8;
        CHECK((idat[p] & 6) == 0 && (bl ^ 0xFFFF) == nl);
        memcpy(raw + rawlen, idat + p + 5, bl);
        rawlen += bl;
        p += 5 + bl;
        if (last)
            break;
    }
    CHECK(rawlen == row * h);
    for (i = 0; i < rawlen; i++)
        a = (a + raw[i]) % 65521, b = (b + a) % 65521;
    CHECK(p + 4 == n_idat && be32(idat + p) == (b << 16 | a));
    for (y = 0; y < h; y++) {
        CHECK(raw[y * row] == 0);
        for (x = 0; x < w; x++)
            if (((raw[y * row + 1 + x / 2] >> (x & 1 ? 0 : 4)) & 15) != pixel(x, y)) {
                CHECK(0);
                y = h;
                break;
            }
    }
    if (file) {
        FILE *f = fopen(file, "wb");
        if (f) {
            fwrite(png, 1, len, f);
            fclose(f);
        }
    }
    free(png);
    free(pix);
    free(raw);
}

int main(void)
{
    check(1, 1, NULL);
    check(7, 3, NULL);
    check(640, 400, "build/host/png_test.png");     /* 80x25 with the 8x16 font: two stored blocks */
    check(1056, 480, NULL);
    printf("png_test: %d failures\n", fails);
    return fails != 0;
}
