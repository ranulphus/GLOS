/* PNG files for glos shot (PRD §7.2): 4-bit indexed colour, with the image
 * data in deflate's stored blocks, so no compressor is needed (a text
 * screen comes to about 130 KB). CRC-32 for the chunks, Adler-32 for the
 * zlib stream. */
#include "kprintf.h"
#include "mm.h"
#include "png.h"

static u32 crc_table[256];

static u32 crc(u32 c, const u8 *p, u32 n)
{
    u32 i, k, t;
    if (!crc_table[1])
        for (i = 0; i < 256; i++) {
            for (t = i, k = 0; k < 8; k++)
                t = (t & 1) ? 0xEDB88320u ^ (t >> 1) : t >> 1;
            crc_table[i] = t;
        }
    c = ~c;
    while (n--)
        c = crc_table[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return ~c;
}

static u8 *be32(u8 *p, u32 v)
{
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
    return p + 4;
}

/* A chunk of n data bytes already at p + 8: its length, type and CRC. */
static u8 *chunk(u8 *p, const char *type, u32 n)
{
    be32(p, n);
    memcpy(p + 4, type, 4);
    be32(p + 8 + n, crc(0, p + 4, n + 4));
    return p + 12 + n;
}

u8 *png_indexed4(u32 w, u32 h, const u8 rgb[16][3], const u8 *pix, u32 *len)
{
    u32 row = 1 + (w + 1) / 2, raw = row * h, blocks = (raw + 65534) / 65535;
    u32 size = 8 + 25 + 60 + 12 + 2 + blocks * 5 + raw + 4 + 12, a = 1, b = 0, x, y, i;
    u8 *out = kmalloc(size), *p, *z;
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

    if (!out)
        return 0;
    memcpy(out, sig, 8);
    p = out + 8;
    be32(p + 8, w);                             /* IHDR */
    be32(p + 12, h);
    p[16] = 4;                                  /* 4 bits per pixel, */
    p[17] = 3;                                  /* indexed colour */
    p[18] = p[19] = p[20] = 0;
    p = chunk(p, "IHDR", 13);
    for (i = 0; i < 16; i++)                    /* PLTE */
        memcpy(p + 8 + i * 3, rgb[i], 3);
    p = chunk(p, "PLTE", 48);
    z = p + 8;                                  /* IDAT: zlib, stored blocks */
    *z++ = 0x78;
    *z++ = 0x01;
    for (y = 0, i = 0; y < h; y++)
        for (x = 0; x < row; x++, i++) {
            u8 v;
            if (i % 65535 == 0) {
                u32 n = raw - i < 65535 ? raw - i : 65535;
                *z++ = n == raw - i;            /* BFINAL on the last; BTYPE 00 */
                *z++ = (u8)n;
                *z++ = (u8)(n >> 8);
                *z++ = (u8)~n;
                *z++ = (u8)(~n >> 8);
            }
            if (x == 0)
                v = 0;                          /* filter: none */
            else {
                u32 px = (x - 1) * 2;
                v = (u8)(pix[y * w + px] << 4);
                if (px + 1 < w)
                    v |= pix[y * w + px + 1] & 15;
            }
            *z++ = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
    z = be32(z, (b << 16) | a);
    p = chunk(p, "IDAT", (u32)(z - (p + 8)));
    p = chunk(p, "IEND", 0);
    *len = (u32)(p - out);
    return out;
}
