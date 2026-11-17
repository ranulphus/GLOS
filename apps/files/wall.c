/* FILES: the desktop's wallpaper (phase D, docs/v1/phase-d.md D4).
 *
 * [desktop] wallpaper names a picture (PNG, JPEG, BMP, GIF or TGA: stb_image, third_party/stb, as Images has
 * it); [desktop] wallpaper_style says how it is put on the desktop: in its middle at its own size (centre), made
 * the desktop's size (stretch), made as big as covers it with what is over cut away (fill), or side by side
 * (tile). The shell reads it once into a picture the desktop's size, draws that under the icons, and makes it
 * again when either setting or the screen changes. With none, or one that can't be read, the desktop is its
 * theme's colour. The 2D library dithers it on an 8-bit desktop. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "stb_image.h"

#include "files.h"

static unsigned *wall;                  /* the desktop's picture, 00RRGGBBh, wall_w by wall_h (NULL: none) */
static int wall_w, wall_h;

/* The picture's bytes, as the file has them (NULL: not read). */
static unsigned char *read_all(const char *path, size_t *pn)
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

/* The source's pixel for a place in a picture dw by dh made from it (sw by sh): the mean of the ones it covers
   when it is made smaller, the nearest when larger. */
static unsigned sample(const unsigned char *src, int sw, int sh, int x, int y, int dw, int dh)
{
    int x0 = (int)((long)x * sw / dw), x1 = (int)((long)(x + 1) * sw / dw);
    int y0 = (int)((long)y * sh / dh), y1 = (int)((long)(y + 1) * sh / dh), i, j;
    unsigned long r = 0, g = 0, b = 0, k;
    if (x1 <= x0)
        x1 = x0 + 1;
    if (y1 <= y0)
        y1 = y0 + 1;
    if (x1 > sw)
        x1 = sw;
    if (y1 > sh)
        y1 = sh;
    for (j = y0; j < y1; j++)
        for (i = x0; i < x1; i++) {
            const unsigned char *p = src + ((size_t)j * (size_t)sw + (size_t)i) * 4;
            r += p[0], g += p[1], b += p[2];
        }
    k = (unsigned long)(x1 - x0) * (unsigned long)(y1 - y0);
    return k ? (unsigned)(r / k << 16 | g / k << 8 | b / k) : 0;
}

/* The wallpaper made for a desktop w by h, from the settings as they are now. 1 if there is one. */
int wall_load(int w, int h)
{
    char path[200], style[16], dos[200];
    unsigned char *d, *src;
    size_t n = 0;
    int sw = 0, sh = 0, comp, x, y, set = 0;
    unsigned back = GUI_ROLE(GLOS_ROLE_DESK);
    free(wall);
    wall = NULL;
    path[0] = style[0] = 0;
    if (glos_setting_get("desktop", "wallpaper", path, sizeof path, &set) != 0 || !path[0] || w < 1 || h < 1)
        return 0;
    glos_setting_get("desktop", "wallpaper_style", style, sizeof style, &set);
    if (gui_text_dos(path, dos, sizeof dos) != 0 || !(d = read_all(dos, &n)))
        return 0;
    src = stbi_load_from_memory(d, (int)n, &sw, &sh, &comp, 4);
    free(d);
    if (!src || sw < 1 || sh < 1 || !(wall = malloc((size_t)w * (size_t)h * sizeof *wall))) {
        stbi_image_free(src);
        return 0;
    }
    wall_w = w, wall_h = h;
    if (!strcmp(style, "stretch")) {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                wall[(size_t)y * (size_t)w + (size_t)x] = sample(src, sw, sh, x, y, w, h);
    } else if (!strcmp(style, "tile")) {
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                const unsigned char *p = src + ((size_t)(y % sh) * (size_t)sw + (size_t)(x % sw)) * 4;
                wall[(size_t)y * (size_t)w + (size_t)x] = (unsigned)p[0] << 16 | (unsigned)p[1] << 8 | p[2];
            }
    } else if (!strcmp(style, "centre")) {
        int ox = (w - sw) / 2, oy = (h - sh) / 2;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                int sx = x - ox, sy = y - oy;
                const unsigned char *p = src + ((size_t)(sy < 0 ? 0 : sy) * (size_t)sw + (size_t)(sx < 0 ? 0 : sx)) * 4;
                wall[(size_t)y * (size_t)w + (size_t)x] = sx < 0 || sy < 0 || sx >= sw || sy >= sh ? back
                    : (unsigned)p[0] << 16 | (unsigned)p[1] << 8 | p[2];
            }
    } else {                                    /* fill: as big as covers the desktop, its middle kept */
        int dw = w, dh = (int)((long)sh * w / sw), ox, oy;
        if (dh < h)
            dh = h, dw = (int)((long)sw * h / sh);
        ox = (dw - w) / 2, oy = (dh - h) / 2;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                wall[(size_t)y * (size_t)w + (size_t)x] = sample(src, sw, sh, x + ox, y + oy, dw, dh);
    }
    stbi_image_free(src);
    return 1;
}

/* The desktop's background in r of the surface (the desktop's window is at ox, oy of it): the wallpaper, or the
   theme's colour. */
void wall_draw(struct gfx_surface *s, int ox, int oy, int w, int h)
{
    if (wall && wall_w == w && wall_h == h)
        gfx_image(s, ox, oy, wall, wall_w, wall_h, (unsigned)wall_w);
    else
        gfx_fill(s, ox, oy, w, h, GUI_ROLE(GLOS_ROLE_DESK));
}
