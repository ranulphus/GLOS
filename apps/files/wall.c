/* FILES: the desktop's wallpaper (phase D, docs/v1/phase-d.md D4).
 *
 * [desktop] wallpaper names a picture; [desktop] wallpaper_style says how it is put on the desktop (wallmake.h:
 * fill, stretch, centre or tile). The shell makes it once into a picture the desktop's size, draws that under the
 * icons, and makes it again when either setting or the screen changes. With none, or one that can't be read, the
 * desktop is its theme's colour. The 2D library dithers it on an 8-bit desktop. */
#define WALLMAKE_STB_ELSEWHERE          /* (the decoders' code is picfile.c's here: thumbs.c) */
#include "wallmake.h"

#include "files.h"

static unsigned *wall;                  /* the desktop's picture, 00RRGGBBh, wall_w by wall_h (NULL: none) */
static int wall_w, wall_h;

/* The wallpaper made for a desktop w by h, from the settings as they are now. 1 if there is one. */
int wall_load(int w, int h)
{
    char path[200], style[16];
    int set = 0;
    free(wall);
    wall = NULL;
    path[0] = style[0] = 0;
    if (glos_setting_get("desktop", "wallpaper", path, sizeof path, &set) != 0 || !path[0])
        return 0;
    glos_setting_get("desktop", "wallpaper_style", style, sizeof style, &set);
    wall = wallmake(path, style, w, h, GUI_ROLE(GLOS_ROLE_DESK));
    wall_w = w, wall_h = h;
    return wall != NULL;
}

/* The desktop's background in a part of the surface w by h at ox, oy (the desktop's window): the wallpaper, or
   the theme's colour. */
void wall_draw(struct gfx_surface *s, int ox, int oy, int w, int h)
{
    if (wall && wall_w == w && wall_h == h)
        gfx_image(s, ox, oy, wall, wall_w, wall_h, (unsigned)wall_w);
    else
        gfx_fill(s, ox, oy, w, h, GUI_ROLE(GLOS_ROLE_DESK));
}
