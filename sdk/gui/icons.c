/* GLOS's icons by name (toolkit 2; docs/gui.md 1.11).
 *
 * Colour icons (gui_icon): pictures of 16 and of 32 pixels in the 8-bit palette's cube, from ICONS.DAT beside
 * the program (tools/mkicons.py, `make icons`: Farm-Fresh's, by the names in sdk/gui/icons/colour.list). An icon
 * is read from the file when it is first asked for and kept; without the file, or the name, there is none
 * (NULL: nothing is drawn).
 *
 * One-colour glyphs (gui_glyph): masks of 16 pixels in the toolkit itself (glyphs.c: Font Awesome Free's solid
 * icons, by the names in sdk/gui/icons/glyphs.list). Wherever an icon is drawn a glyph may be given instead: it
 * is drawn in the text's colour there (the theme's, the chosen row's, the greyed one's), so a toolbar's and a
 * menu's glyphs are right in every theme. A glyph says what it is by its first four bytes (GUI_MASK_MAGIC). */
#include <stdio.h>

#include "guiint.h"

#define REC (24 + GUI_ICON_W * GUI_ICON_W + GUI_BIG_W * GUI_BIG_W)
#define KEPT 96

static struct {
    char name[24];
    unsigned char *px[2];               /* 16, 32: read, or NULL */
    int none;                           /* the file has no such icon */
} kept[KEPT];
static int nkept;

const unsigned char *gui_glyph(const char *name)
{
    int i;
    for (i = 0; i < gui_glyphs_n; i++)
        if (!strcmp(gui_glyphs[i].name, name))
            return gui_glyphs[i].mask;
    return NULL;
}

const unsigned char *gui_icon(const char *name, int big)
{
    unsigned char head[12], rec[24];
    FILE *f;
    long lo, hi, n;
    int i, k = -1;
    big = big != 0;
    for (i = 0; i < nkept && k < 0; i++)
        if (!strcmp(kept[i].name, name))
            k = i;
    if (k >= 0 && (kept[k].none || kept[k].px[big]))
        return kept[k].px[big];
    if (k < 0) {
        if (nkept == KEPT || strlen(name) > 23)
            return NULL;
        k = nkept++;
        strcpy(kept[k].name, name);
    }
    if (!(f = gui_open_beside("ICONS.DAT")) || fread(head, 1, 12, f) != 12 || memcmp(head, "GLOSIC1\n", 8)) {
        if (f)
            fclose(f);
        kept[k].none = 1;
        return NULL;
    }
    n = (long)(head[8] | (unsigned)head[9] << 8 | (unsigned)head[10] << 16 | (unsigned)head[11] << 24);
    for (lo = 0, hi = n - 1; lo <= hi;) {       /* the icons are in their names' order */
        long mid = (lo + hi) / 2;
        int c;
        if (fseek(f, 12 + mid * REC, SEEK_SET) != 0 || fread(rec, 1, 24, f) != 24)
            break;
        rec[23] = 0;
        c = strcmp(name, (char *)rec);
        if (c == 0) {
            size_t size = big ? GUI_BIG_W * GUI_BIG_W : GUI_ICON_W * GUI_ICON_W;
            unsigned char *px = malloc(size);
            if (px && fseek(f, 12 + mid * REC + 24 + (big ? GUI_ICON_W * GUI_ICON_W : 0), SEEK_SET) == 0
                && fread(px, 1, size, f) == size)
                kept[k].px[big] = px;
            else
                free(px);
            fclose(f);
            return kept[k].px[big];
        }
        if (c < 0)
            hi = mid - 1;
        else
            lo = mid + 1;
    }
    fclose(f);
    kept[k].none = 1;
    return NULL;
}
