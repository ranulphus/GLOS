/* Host test for glos shot's PNG writer (kernel/lib/png.c): every chunk's
 * CRC, the zlib stream's stored blocks across the IDAT chunks and its
 * Adler-32, and every pixel, read back by an independent parser, in each
 * depth and with the scanlines given in groups of several sizes;
 * build/host/png_test.png is left for an outside decoder to check too. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "png.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static u32 crc_bitwise(const u8 *p, u32 n)
{
    u32 c = 0xFFFFFFFFu, k;
    while (n--)
        for (c ^= *p++, k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & -(c & 1));
    return ~c;
}

static u8 pixel(u32 x, u32 y, u32 c) { return (u8)(x * 3 + y * 5 + (x ^ y) + c * 77); }

/* A w x h picture in `depth`, its scanlines given `group` at a time. */
static void check(u32 w, u32 h, int depth, u32 group, const char *file)
{
    u32 colours = depth == 4 ? 16 : 256, row = 1 + (depth == 24 ? w * 3 : depth == 8 ? w : (w + 1) / 2);
    u8 rgb[256][3], *png = malloc(PNG_HEAD_MAX + h * (row + 40) + 64), *lines = malloc(row * h + 1);
    u8 *zs = malloc(row * h + h * 16 + 64), *raw = malloc(row * h + 16);
    u32 len, x, y, p = 8, zlen = 0, rawlen = 0, i, a = 1, b = 0, n_idat = 0;
    int seen_ihdr = 0, seen_iend = 0, seen_plte = 0;
    struct png st;

    for (i = 0; i < 256; i++)
        rgb[i][0] = (u8)(i * 17), rgb[i][1] = (u8)(255 - i), rgb[i][2] = (u8)i;
    memset(lines, 0, row * h);
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            u8 *l = lines + y * row + 1;
            if (depth == 24)
                l[x * 3] = pixel(x, y, 0), l[x * 3 + 1] = pixel(x, y, 1), l[x * 3 + 2] = pixel(x, y, 2);
            else if (depth == 8)
                l[x] = pixel(x, y, 0);
            else
                l[x / 2] |= (u8)((pixel(x, y, 0) & 15) << (x & 1 ? 0 : 4));
        }
    len = png_begin(&st, w, h, depth, (const u8 (*)[3])rgb, colours, png);
    CHECK(len <= PNG_HEAD_MAX && st.row == row);
    for (y = 0; y < h; y += group) {
        u32 n = h - y < group ? h - y : group, k = png_rows(&st, lines + y * row, n, png + len);
        CHECK(n * row <= PNG_BLOCK && k <= PNG_ROWS_MAX(n * row));
        len += k;
    }
    CHECK(!memcmp(png, "\x89PNG\r\n\x1a\n", 8));
    while (p + 12 <= len) {
        u32 n = be32(png + p);
        CHECK(be32(png + p + 8 + n) == crc_bitwise(png + p + 4, n + 4));
        if (!memcmp(png + p + 4, "IHDR", 4)) {
            seen_ihdr = 1;
            CHECK(n == 13 && be32(png + p + 8) == w && be32(png + p + 12) == h);
            CHECK(png[p + 16] == (depth == 24 ? 8 : depth) && png[p + 17] == (depth == 24 ? 2 : 3));
        } else if (!memcmp(png + p + 4, "PLTE", 4)) {
            seen_plte = 1;
            CHECK(n == colours * 3 && !memcmp(png + p + 8, rgb, colours * 3) && !n_idat);
        } else if (!memcmp(png + p + 4, "IDAT", 4)) {
            CHECK(!seen_iend);
            memcpy(zs + zlen, png + p + 8, n);  /* the zlib stream runs through them all */
            zlen += n;
            n_idat++;
        } else if (!memcmp(png + p + 4, "IEND", 4)) {
            seen_iend = 1;
            CHECK(n == 0);
        } else {
            CHECK(0);
        }
        p += 12 + n;
    }
    CHECK(seen_ihdr && seen_iend && p == len && n_idat == (h + group - 1) / group && seen_plte == (depth != 24));
    CHECK(zs[0] == 0x78 && (zs[0] * 256 + zs[1]) % 31 == 0);
    for (p = 2; p + 5 <= zlen;) {               /* the stored blocks */
        u32 last = zs[p] & 1, bl = zs[p + 1] | zs[p + 2] << 8, nl = zs[p + 3] | zs[p + 4] << 8;
        CHECK((zs[p] & 6) == 0 && (bl ^ 0xFFFF) == nl && rawlen + bl <= row * h);
        memcpy(raw + rawlen, zs + p + 5, bl);
        rawlen += bl;
        p += 5 + bl;
        if (last)
            break;
    }
    CHECK(rawlen == row * h && !memcmp(raw, lines, rawlen));
    for (i = 0; i < rawlen; i++)
        a = (a + raw[i]) % 65521, b = (b + a) % 65521;
    CHECK(p + 4 == zlen && be32(zs + p) == (b << 16 | a));
    if (file) {
        FILE *f = fopen(file, "wb");
        if (f) {
            fwrite(png, 1, len, f);
            fclose(f);
        }
    }
    free(png);
    free(lines);
    free(zs);
    free(raw);
}

int main(void)
{
    check(1, 1, 4, 1, NULL);
    check(7, 3, 4, 2, NULL);
    check(640, 400, 4, 49, "build/host/png_test.png");  /* 80x25 with the 8x16 font */
    check(1056, 480, 4, 100, NULL);
    check(320, 200, 8, 51, NULL);
    check(641, 37, 8, 37, NULL);
    check(13, 9, 24, 4, NULL);
    check(1024, 96, 24, 21, NULL);              /* the largest block: 21 scanlines of 3073 bytes */
    check(8000, 8, 8, 8, NULL);                 /* more than one Adler-32 reduction in a block */
    printf("png_test: %d failures\n", fails);
    return fails != 0;
}
