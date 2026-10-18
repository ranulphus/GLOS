/* Font smoothing (M10 step G; include/glos/gfx.h struct gfx_aa): with
 * [display] font_smoothing on, the toolkit reads SMOOTH.DAT (tools/mkfont.py
 * --smooth, `make fonts`) from GLOS's folder when the program starts, and the
 * 2D library blends the fonts' glyphs from it from then on, at 16 and 32
 * bits. A file that doesn't match the fonts the program has (another
 * build's) is left alone: text stays 1-bit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

static struct gfx_aa aa[3];

static unsigned le32(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

/* The file's fonts (those it has of the program's): how many, or -1 if it can't be read. */
static int load(const char *path)
{
    const struct gfx_font *fonts[3] = { &gfx_font_ui, &gfx_font_bold, &gfx_font_mono };
    FILE *f = gui_fopen(path, "rb");
    unsigned char *d;
    long size;
    unsigned off = 8, k = 0;
    if (!f)
        return -1;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 8 || fseek(f, 0, SEEK_SET) != 0 ||
        !(d = malloc((size_t)size))) {
        fclose(f);
        return -1;
    }
    if (fread(d, 1, (size_t)size, f) != (size_t)size || memcmp(d, "GLOSAA1\n", 8)) {
        fclose(f);
        free(d);
        return -1;
    }
    fclose(f);
    while (off + 40 <= (unsigned)size && k < 3) {
        unsigned n = le32(d + off + 32), nbits = le32(d + off + 36), i, j;
        const unsigned char *t = d + off + 40;
        struct gfx_aa_glyph *g;
        d[off + 31] = 0;
        if (n > 65536 || nbits > (unsigned)size || off + 40 + n * 8 + nbits > (unsigned)size)
            break;
        for (i = 0; i < 3 && (strcmp(fonts[i]->name, (char *)d + off) || fonts[i]->missing + 1u != n); i++) ;
        if (i < 3 && (g = malloc(n * sizeof *g)) != NULL) {
            for (j = 0; j < n; j++) {
                g[j].at = le32(t + j * 8), g[j].width = t[j * 8 + 4], g[j].left = (signed char)t[j * 8 + 5];
                if (g[j].at + (g[j].width + 1u) / 2 * fonts[i]->height > nbits)
                    break;
            }
            if (j == n) {
                aa[k].font = fonts[i], aa[k].glyph = g, aa[k].bits = t + n * 8;
                aa[k].next = k ? &aa[k - 1] : NULL;
                k++;
            } else {
                free(g);
            }
        }
        off += 40 + n * 8 + nbits;
    }
    if (k)
        gfx_smooth(&aa[k - 1]);
    else
        free(d);
    return (int)k;                              /* (d kept: the coverage, from now on) */
}

void gui_smooth_start(const char *argv0)
{
    char v[8], path[300], *p;
    if (glos_config_get("display", "font_smoothing", v, sizeof v) < 0 || strcmp(v, "on"))
        return;
    snprintf(path, sizeof path, "%s", argv0 ? argv0 : "");
    for (p = path; *p; p++)
        if (*p == '/')
            *p = '\\';
    if ((p = strrchr(path, '\\')) != NULL)
        snprintf(p + 1, sizeof path - (size_t)(p + 1 - path), "SMOOTH.DAT");
    else
        snprintf(path, sizeof path, "C:\\GLOS\\SMOOTH.DAT");    /* (the host's: its drive C) */
    load(path);
}
