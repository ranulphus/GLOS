/* PNG files for glos shot (PRD §7.5), written a piece at a time so that a
 * picture of any size goes out through a small buffer: the head, then an
 * IDAT chunk for each group of scanlines the caller gives, each chunk one
 * of deflate's stored blocks (no compressor is needed; a 640x480 picture
 * in 256 colours comes to about 310 KB). CRC-32 for the chunks, Adler-32
 * for the zlib stream. */
#include "kprintf.h"
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

u32 png_begin(struct png *p, u32 w, u32 h, int depth, const u8 (*rgb)[3], u32 colours, u8 *out)
{
    static const u8 sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    u8 *q = out + 8;
    u32 i;

    p->w = w;
    p->h = h;
    p->y = 0;
    p->a = 1;
    p->b = 0;
    p->row = 1 + (depth == 24 ? w * 3 : depth == 8 ? w : (w + 1) / 2);
    memcpy(out, sig, 8);
    be32(q + 8, w);                             /* IHDR */
    be32(q + 12, h);
    q[16] = (u8)(depth == 24 ? 8 : depth);      /* bits per sample */
    q[17] = depth == 24 ? 2 : 3;                /* RGB, or indexed colour */
    q[18] = q[19] = q[20] = 0;
    q = chunk(q, "IHDR", 13);
    if (depth != 24) {
        for (i = 0; i < colours; i++)
            memcpy(q + 8 + i * 3, rgb[i], 3);
        q = chunk(q, "PLTE", colours * 3);
    }
    return (u32)(q - out);
}

u32 png_rows(struct png *p, const u8 *rows, u32 n, u8 *out)
{
    u32 len = n * p->row, i, a = p->a, b = p->b, last;
    u8 *z = out + 8, *q;

    if (!p->y) {                                /* the zlib stream's head */
        *z++ = 0x78;
        *z++ = 0x01;
    }
    p->y += n;
    last = p->y >= p->h;
    *z++ = (u8)last;                            /* BFINAL; BTYPE 00, stored */
    *z++ = (u8)len;
    *z++ = (u8)(len >> 8);
    *z++ = (u8)~len;
    *z++ = (u8)(~len >> 8);
    memcpy(z, rows, len);
    z += len;
    for (i = 0; i < len;) {                     /* 5552 bytes are the most before the sums must be reduced */
        u32 end = len - i > 5552 ? i + 5552 : len;
        for (; i < end; i++) {
            a += rows[i];
            b += a;
        }
        a %= 65521;
        b %= 65521;
    }
    p->a = a;
    p->b = b;
    if (last)
        z = be32(z, (b << 16) | a);
    q = chunk(out, "IDAT", (u32)(z - (out + 8)));
    if (last)
        q = chunk(q, "IEND", 0);
    return (u32)(q - out);
}
