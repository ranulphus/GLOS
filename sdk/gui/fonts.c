/* The UI's fonts in three sizes (toolkit 2, step C1b; docs/v1/toolkit-2.md 6) and their smoothing (M10 step G;
 * include/glos/gfx.h struct gfx_aa).
 *
 * The normal size is built into the program (kernel/gfx/fonts: DejaVu at 11 pixels). The small and the large one
 * (9 and 14 pixels) are in FONTS.DAT beside the program (tools/mkfont.py --chunk, `make fonts`), read when the
 * theme's text size asks for them; without the file the normal size stays. gui_font, gui_bold and gui_mono
 * point at the same three objects from gui_init on, whose contents are the size in force: a widget that keeps
 * one of them keeps "the UI's font", whatever its size comes to be.
 *
 * With [display] font_smoothing on, SMOOTH.DAT (--smooth) gives the fonts in force their glyphs' coverage, which
 * the 2D library blends at 16 and 32 bits. A file that doesn't match the fonts the program has (another
 * build's) is left alone: text stays 1-bit. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

static struct gfx_font cur[3];          /* ui, bold, mono: the fonts in force */
static struct gfx_font other[3][3];     /* [size][which], from FONTS.DAT (sizes 0 and 2); name NULL: not read */
static const char *const which[3] = { "ui", "bold", "mono" };
static char folder[280];                /* the program's, to its last backslash */
static struct gfx_aa aa[3];
static void *aa_mem[3];

static unsigned le32(const unsigned char *p)
{
    return p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }

static FILE *open_beside(const char *name)
{
    char path[320];
    snprintf(path, sizeof path, "%s%s", folder[0] ? folder : "C:\\GLOS\\", name);
    return gui_fopen(path, "rb");
}

void *gui_open_beside(const char *name) { return open_beside(name); }

/* FONTS.DAT's fonts of one size: 1 if all three are there. */
static int load_size(int size)
{
    FILE *f = open_beside("FONTS.DAT");
    unsigned char h[64];
    if (!f)
        return 0;
    if (fread(h, 1, 8, f) != 8 || memcmp(h, "GLOSFN1\n", 8)) {
        fclose(f);
        return 0;
    }
    while (fread(h, 1, sizeof h, f) == sizeof h) {
        unsigned nmore = le16(h + 52), nglyphs = le32(h + 56), nbits = le32(h + 60), i;
        unsigned long rest = (unsigned long)nglyphs * 8 + (unsigned long)nmore * 8 + ((nbits + 3) & ~3u);
        struct gfx_font *fn = NULL;
        unsigned char *raw;
        struct gfx_glyph *g;
        struct gfx_range *r;
        char *name;
        h[31] = h[39] = 0;
        if (nglyphs > 65536 || nmore > 256 || nbits > 0x100000)
            break;
        for (i = 0; i < 3; i++)
            if ((int)le32(h + 40) == size && !strcmp((char *)h + 32, which[i]) && !other[size][i].name)
                fn = &other[size][i];
        if (!fn) {
            if (fseek(f, (long)rest, SEEK_CUR) != 0)
                break;
            continue;
        }
        raw = malloc(rest);
        g = malloc(nglyphs * sizeof *g);
        r = malloc((nmore ? nmore : 1) * sizeof *r);
        name = malloc(32);
        if (!raw || !g || !r || !name || fread(raw, 1, rest, f) != rest) {
            free(raw), free(g), free(r), free(name);
            break;
        }
        for (i = 0; i < nglyphs; i++) {
            g[i].at = le32(raw + i * 8), g[i].width = raw[i * 8 + 4], g[i].advance = raw[i * 8 + 5];
            g[i].left = (signed char)raw[i * 8 + 6];
        }
        for (i = 0; i < nmore; i++) {
            const unsigned char *p = raw + nglyphs * 8 + i * 8;
            r[i].first = (unsigned short)le16(p), r[i].count = (unsigned short)le16(p + 2);
            r[i].at = (unsigned short)le16(p + 4);
        }
        memcpy(name, h, 32);
        fn->height = h[44], fn->ascent = h[45];
        fn->first = (unsigned short)le16(h + 46), fn->count = (unsigned short)le16(h + 48);
        fn->missing = (unsigned short)le16(h + 50);
        fn->glyph = g;
        fn->bits = raw + nglyphs * 8 + nmore * 8;       /* (raw is kept: the bitmaps, from now on) */
        fn->more = nmore ? r : NULL, fn->nmore = (unsigned short)nmore;
        if (!nmore)
            free(r);
        fn->name = name;
    }
    fclose(f);
    return other[size][0].name && other[size][1].name && other[size][2].name;
}

/* SMOOTH.DAT's coverage for the fonts in force, if [display] font_smoothing is on. */
static void smooth(void)
{
    char v[8];
    unsigned char h[40];
    FILE *f;
    int k = 0, i;
    gfx_smooth(NULL);
    for (i = 0; i < 3; i++) {
        free(aa_mem[i]);
        free((void *)aa[i].glyph);
        aa_mem[i] = NULL, aa[i].glyph = NULL;
    }
    if (glos_setting_get("display", "font_smoothing", v, sizeof v, 0) != 0 || strcmp(v, "on") || !(f = open_beside("SMOOTH.DAT")))
        return;
    if (fread(h, 1, 8, f) != 8 || memcmp(h, "GLOSAA1\n", 8)) {
        fclose(f);
        return;
    }
    while (k < 3 && fread(h, 1, sizeof h, f) == sizeof h) {
        unsigned n = le32(h + 32), nbits = le32(h + 36), j;
        unsigned long rest = (unsigned long)n * 8 + nbits;
        unsigned char *raw;
        struct gfx_aa_glyph *g;
        h[31] = 0;
        if (n > 65536 || nbits > 0x400000)
            break;
        for (i = 0; i < 3 && (strcmp(cur[i].name, (char *)h) || cur[i].missing + 1u != n); i++) ;
        if (i == 3) {
            if (fseek(f, (long)rest, SEEK_CUR) != 0)
                break;
            continue;
        }
        raw = malloc(rest);
        g = malloc(n * sizeof *g);
        if (!raw || !g || fread(raw, 1, rest, f) != rest) {
            free(raw), free(g);
            break;
        }
        for (j = 0; j < n; j++) {
            g[j].at = le32(raw + j * 8), g[j].width = raw[j * 8 + 4], g[j].left = (signed char)raw[j * 8 + 5];
            if (g[j].at + (g[j].width + 1u) / 2 * cur[i].height > nbits)
                break;
        }
        if (j < n) {                            /* (not this font's after all) */
            free(raw), free(g);
            continue;
        }
        aa[k].font = &cur[i], aa[k].glyph = g, aa[k].bits = raw + n * 8;
        aa[k].next = k ? &aa[k - 1] : NULL;
        aa_mem[k] = raw;
        k++;
    }
    fclose(f);
    if (k)
        gfx_smooth(&aa[k - 1]);
}

/* The fonts of the theme's text size (gui_theme.text: 0 small, 1 normal, 2 large) put in force. */
void gui_fonts_apply(void)
{
    static const struct gfx_font *const built[3] = { &gfx_font_ui, &gfx_font_bold, &gfx_font_mono };
    int size = gui_theme.text == 0 || gui_theme.text == 2 ? gui_theme.text : 1, i;
    if (size != 1 && !(other[size][0].name && other[size][1].name && other[size][2].name) && !load_size(size))
        size = 1;                               /* (no FONTS.DAT beside the program: the normal size) */
    if (cur[0].name && !strcmp(cur[0].name, size == 1 ? built[0]->name : other[size][0].name))
        return;                                 /* (as they are) */
    for (i = 0; i < 3; i++)
        cur[i] = size == 1 ? *built[i] : other[size][i];
    gui_font = &cur[0], gui_bold = &cur[1], gui_mono = &cur[2];
    smooth();
}

void gui_fonts_start(const char *argv0)
{
    char *p;
    snprintf(folder, sizeof folder, "%s", argv0 ? argv0 : "");
    for (p = folder; *p; p++)
        if (*p == '/')
            *p = '\\';
    if ((p = strrchr(folder, '\\')) != NULL)
        p[1] = 0;
    else
        snprintf(folder, sizeof folder, "C:\\GLOS\\");  /* (the host's: its drive C) */
    gui_fonts_apply();
}
