/* Pictures read and written: the viewer's and the editor's (picfile.h). Reading: PNG, JPEG, BMP, GIF (its first
 * frame), TGA and PNM through stb_image (third_party/stb: a parser, in ring 3, as PRD D19 has it), and PCX, the DOS
 * era's own, here. Writing: PNG, BMP and JPEG through stb_image_write, into a file opened by the toolkit (its
 * names are DOS's; the host's stand-in has them elsewhere). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_GIF
#define STBI_ONLY_TGA
#define STBI_ONLY_PNM
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#include <glos/gui.h>

#include "picfile.h"

/* ---- PCX (ZSoft): 8 bits with a 256-colour palette at the end, 24 bits as three planes, 4 bits as four planes
   (the header's 16 colours), 1 bit (black and white) */

static unsigned *pcx(const unsigned char *d, size_t n, int *pw, int *ph)
{
    int bpp, planes, bpl, w, h, x, y, p;
    unsigned *out;
    unsigned char *line;
    size_t at = 128;
    if (n < 128 || d[0] != 0x0A || d[2] != 1)
        return NULL;
    bpp = d[3];
    w = (d[8] | d[9] << 8) - (d[4] | d[5] << 8) + 1;
    h = (d[10] | d[11] << 8) - (d[6] | d[7] << 8) + 1;
    planes = d[65];
    bpl = d[66] | d[67] << 8;
    if (w < 1 || h < 1 || w > 8192 || h > 8192 || bpl < 1 || !((bpp == 8 && (planes == 1 || planes == 3))
                                                              || (bpp == 1 && (planes == 1 || planes == 4))))
        return NULL;
    out = malloc((size_t)w * (size_t)h * sizeof *out);
    line = malloc((size_t)bpl * (size_t)planes);
    if (!out || !line) {
        free(out);
        free(line);
        return NULL;
    }
    for (y = 0; y < h; y++) {
        int k = 0, total = bpl * planes;
        while (k < total && at < n) {           /* a scanline's bytes, run-length coded */
            unsigned char c = d[at++], run = 1;
            if ((c & 0xC0) == 0xC0 && at < n) {
                run = c & 0x3F;
                c = d[at++];
            }
            while (run-- && k < total)
                line[k++] = c;
        }
        for (x = 0; x < w; x++) {
            unsigned rgb = 0;
            if (bpp == 8 && planes == 1) {
                const unsigned char *pal = d + n - 768;
                unsigned i = line[x];
                if (n >= 128 + 769 && d[n - 769] == 0x0C)
                    rgb = (unsigned)pal[i * 3] << 16 | (unsigned)pal[i * 3 + 1] << 8 | pal[i * 3 + 2];
                else
                    rgb = i * 0x010101u;
            } else if (bpp == 8) {
                rgb = (unsigned)line[x] << 16 | (unsigned)line[bpl + x] << 8 | line[2 * bpl + x];
            } else if (planes == 4) {
                unsigned i = 0;
                for (p = 0; p < 4; p++)
                    i |= (unsigned)((line[p * bpl + x / 8] >> (7 - x % 8)) & 1) << p;
                rgb = (unsigned)d[16 + i * 3] << 16 | (unsigned)d[17 + i * 3] << 8 | d[18 + i * 3];
            } else {
                rgb = (line[x / 8] >> (7 - x % 8)) & 1 ? 0xFFFFFF : 0;
            }
            out[(size_t)y * (size_t)w + (size_t)x] = rgb;
        }
    }
    free(line);
    *pw = w, *ph = h;
    return out;
}

/* ---- loading */

const char *pic_ext(const char *path)
{
    const char *d = strrchr(path, '.');
    return d && !strchr(d, '\\') ? d + 1 : "";
}

int pic_is_picture(const char *name)
{
    static const char *const known[] = { "PNG", "JPG", "JPEG", "BMP", "GIF", "TGA", "PNM", "PPM", "PGM", "PCX", 0 };
    const char *e = pic_ext(name);
    int i, k;
    for (i = 0; known[i]; i++) {
        for (k = 0; e[k] && known[i][k] && (e[k] & ~0x20) == known[i][k]; k++)
            ;
        if (!e[k] && !known[i][k])
            return 1;
    }
    return 0;
}

/* The file's bytes (NULL: not read). */
unsigned char *pic_read_all(const char *path, size_t *pn)
{
    FILE *f = gui_fopen(path, "rb");
    unsigned char *d = NULL;
    size_t n = 0, room = 0, k;
    if (!f)
        return NULL;
    for (;;) {
        if (n == room) {
            unsigned char *nd = realloc(d, room = room * 2 + 65536);
            if (!nd)
                break;
            d = nd;
        }
        if (!(k = fread(d + n, 1, room - n, f)))
            break;
        n += k;
    }
    fclose(f);
    *pn = n;
    return d;
}

/* A picture's pixels from its file's bytes (00RRGGBBh; NULL: not one Images can show); kind: its type. */
unsigned *pic_decode(const unsigned char *d, size_t n, const char *path, int *pw, int *ph, char *kind, size_t room,
                     unsigned back)
{
    unsigned *px = NULL;
    int w = 0, h = 0, comp;
    if (!strcmp(pic_ext(path), "PCX")) {
        px = pcx(d, n, &w, &h);
        snprintf(kind, room, "PCX");
    } else {
        unsigned char *rgba = stbi_load_from_memory(d, (int)n, &w, &h, &comp, 4);
        if (rgba) {
            size_t i, all = (size_t)w * (size_t)h;
            unsigned br = back >> 16 & 255, bg = back >> 8 & 255, bb = back & 255;
            px = malloc(all * sizeof *px);
            for (i = 0; px && i < all; i++) {   /* (alpha: over the view's backdrop) */
                unsigned a = rgba[i * 4 + 3], r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
                r = (r * a + br * (255 - a)) / 255, g = (g * a + bg * (255 - a)) / 255, b = (b * a + bb * (255 - a)) / 255;
                px[i] = r << 16 | g << 8 | b;
            }
            stbi_image_free(rgba);
        }
        snprintf(kind, room, "%s", pic_ext(path));
    }
    *pw = w, *ph = h;
    return px;
}

int pic_info(const unsigned char *d, size_t n, int *pw, int *ph)
{
    int comp;
    return stbi_info_from_memory(d, (int)n, pw, ph, &comp);
}

/* ---- writing */

static void put(void *ctx, void *data, int size)
{
    FILE **f = ctx;
    if (*f && fwrite(data, 1, (size_t)size, *f) != (size_t)size) {
        fclose(*f);                             /* (a full disk: no more is written, and the file is no picture) */
        *f = NULL;
    }
}

static int kind_of(const char *name)            /* 1 PNG, 2 BMP, 3 JPEG, 0 none of them */
{
    static const char *const known[] = { "PNG", "BMP", "JPG", "JPEG", 0 };
    const char *e = pic_ext(name);
    int i, k;
    for (i = 0; known[i]; i++) {
        for (k = 0; e[k] && known[i][k] && (e[k] & ~0x20) == known[i][k]; k++)
            ;
        if (!e[k] && !known[i][k])
            return i < 3 ? i + 1 : 3;
    }
    return 0;
}

int pic_can_write(const char *name) { return kind_of(name) != 0; }

int pic_write(const char *path, const unsigned *px, int w, int h, int quality)
{
    int kind = kind_of(path), ok = 0;
    size_t i, all = (size_t)w * (size_t)h;
    unsigned char *rgb;
    FILE *f;
    if (!kind)
        return -2;
    if (!(rgb = malloc(all * 3)))
        return -3;
    for (i = 0; i < all; i++)
        rgb[i * 3] = (unsigned char)(px[i] >> 16), rgb[i * 3 + 1] = (unsigned char)(px[i] >> 8),
        rgb[i * 3 + 2] = (unsigned char)px[i];
    if (!(f = gui_fopen(path, "wb"))) {
        free(rgb);
        return -1;
    }
    if (kind == 1)
        ok = stbi_write_png_to_func(put, &f, w, h, 3, rgb, w * 3);
    else if (kind == 2)
        ok = stbi_write_bmp_to_func(put, &f, w, h, 3, rgb);
    else
        ok = stbi_write_jpg_to_func(put, &f, w, h, 3, rgb, quality > 0 && quality <= 100 ? quality : 90);
    free(rgb);
    if (!f) {                                   /* (cut short: nothing of it is left) */
        gui_unlink(path);
        return -1;
    }
    if (fclose(f) != 0 || !ok) {
        gui_unlink(path);
        return -1;
    }
    return 0;
}
