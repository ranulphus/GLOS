/* IMAGES: GLOS's image viewer (PRD §10.6; docs/milestones-m9.md step D).
 * PNG, JPEG, BMP, GIF (its first frame), TGA and PNM through stb_image
 * (third_party/stb: a parser, in ring 3, as PRD D19 has it), and PCX, the
 * DOS era's own, here. Fitted to the window (never enlarged) or at a zoom,
 * the next and the previous picture in the folder a key away; dithered to
 * the palette on an 8-bit desktop by the 2D library.
 *
 *   IMAGES [FILE]
 *
 * Keys: PgDn, PgUp the next, previous; F fitted; 1 actual size; + and -
 * zoom; the arrows move about a picture bigger than the window.
 * "IMAGES ..." lines on stdout (the suite's). */
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

#include <glos/gui.h>

enum { C_OPEN = 1, C_NEXT, C_PREV, C_EXIT, C_FIT, C_ACTUAL, C_IN, C_OUT, C_ABOUT };

static const int zooms[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };   /* tenths of a per cent */

static struct gui_window *win;
static struct gui_widget *view, *status;
static struct {
    unsigned *px;                       /* 00RRGGBBh */
    int w, h;
    char path[260], kind[8];
} pic;
static int fit = 1, zoom = 3, sx, sy;   /* zoom: an index into zooms when not fitted; the corner shown */
static unsigned *scaled;                /* the picture at the size shown */
static int scaled_w, scaled_h;

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

static const char *ext_of(const char *path)
{
    const char *d = strrchr(path, '.');
    return d && !strchr(d, '\\') ? d + 1 : "";
}

static int is_picture(const char *name)
{
    static const char *const known[] = { "PNG", "JPG", "JPEG", "BMP", "GIF", "TGA", "PNM", "PPM", "PGM", "PCX", 0 };
    const char *e = ext_of(name);
    int i, k;
    for (i = 0; known[i]; i++) {
        for (k = 0; e[k] && known[i][k] && (e[k] & ~0x20) == known[i][k]; k++)
            ;
        if (!e[k] && !known[i][k])
            return 1;
    }
    return 0;
}

static void status_line(void);

static int load(const char *path)
{
    FILE *f = gui_fopen(path, "rb");
    unsigned char *d = NULL;
    size_t n = 0, room = 0, k;
    int w = 0, h = 0, comp;
    unsigned *px = NULL;
    if (!f) {
        gui_message(win, "Images", "The file isn't there.", GUI_MB_OK);
        return -1;
    }
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
    if (d && !strcmp(ext_of(path), "PCX")) {
        px = pcx(d, n, &w, &h);
        strcpy(pic.kind, "PCX");
    } else if (d) {
        unsigned char *rgba = stbi_load_from_memory(d, (int)n, &w, &h, &comp, 4);
        if (rgba) {
            size_t i, all = (size_t)w * (size_t)h;
            px = malloc(all * sizeof *px);
            for (i = 0; px && i < all; i++) {   /* (alpha: over the window's grey) */
                unsigned a = rgba[i * 4 + 3], r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2];
                r = (r * a + 0x66 * (255 - a)) / 255, g = (g * a + 0x66 * (255 - a)) / 255, b = (b * a + 0x66 * (255 - a)) / 255;
                px[i] = r << 16 | g << 8 | b;
            }
            stbi_image_free(rgba);
        }
        snprintf(pic.kind, sizeof pic.kind, "%s", ext_of(path));
    }
    free(d);
    if (!px) {
        char t[300];
        snprintf(t, sizeof t, "%s isn't a picture Images can show.", gui_path_name(path));
        gui_message(win, "Images", t, GUI_MB_OK);
        return -1;
    }
    free(pic.px);
    pic.px = px, pic.w = w, pic.h = h;
    snprintf(pic.path, sizeof pic.path, "%s", path);
    free(scaled);
    scaled = NULL;
    sx = sy = 0;
    {
        char t[300];
        snprintf(t, sizeof t, "%s - Images", gui_path_name(path));
        gui_title(win, t);
    }
    printf("IMAGES open %s %dx%d %s\n", path, w, h, pic.kind);
    fflush(stdout);
    status_line();
    gui_damage(view);
    return 0;
}

/* The next (1) or previous (-1) picture in the folder, by name. */
static void step(int by)
{
    char dir[260], next[260];
    struct gui_dirent *e;
    int n, i, at = -1, k;
    if (!pic.path[0])
        return;
    snprintf(dir, sizeof dir, "%s", pic.path);
    gui_path_up(dir);
    if ((n = gui_dir_read(dir, &e)) <= 0)
        return;
    for (i = 0; i < n; i++)
        if (!e[i].dir && !strcmp(e[i].name, gui_path_name(pic.path)))
            at = i;
    for (k = 1; k <= n; k++) {
        i = ((at < 0 ? 0 : at) + by * k + n * 2) % n;
        if (!e[i].dir && is_picture(e[i].name)) {
            gui_path_join(next, sizeof next, dir, e[i].name);
            free(e);
            load(next);
            return;
        }
    }
    free(e);
}

/* ---- showing */

static int scale_of(void)               /* the size shown, in tenths of a per cent */
{
    int cw = view->r.w, ch = view->r.h, s;
    if (!pic.px)
        return 1000;
    if (!fit)
        return zooms[zoom];
    s = 1000;
    if (pic.w * s / 1000 > cw)
        s = cw * 1000 / pic.w;
    if (pic.h * s / 1000 > ch)
        s = ch * 1000 / pic.h;
    return s > 0 ? s : 1;
}

static void status_line(void)
{
    char t[200];
    if (!pic.px) {
        gui_set_text(status, "No picture");
        return;
    }
    snprintf(t, sizeof t, "%s   %d x %d   %s   %d%%%s", gui_path_name(pic.path), pic.w, pic.h, pic.kind,
             (scale_of() + 5) / 10, fit ? " (fitted)" : "");
    gui_set_text(status, t);
}

static void draw(struct gui_widget *w, struct gfx_surface *s)
{
    int s1000 = scale_of(), dw, dh, x0, y0, x, y;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_SHADE);
    if (!pic.px)
        return;
    dw = (int)((long)pic.w * s1000 / 1000), dh = (int)((long)pic.h * s1000 / 1000);
    if (dw < 1)
        dw = 1;
    if (dh < 1)
        dh = 1;
    if (sx > dw - w->r.w)
        sx = dw - w->r.w;
    if (sy > dh - w->r.h)
        sy = dh - w->r.h;
    if (sx < 0)
        sx = 0;
    if (sy < 0)
        sy = 0;
    {
        int vw = dw < w->r.w ? dw : w->r.w, vh = dh < w->r.h ? dh : w->r.h;     /* what of it shows */
        if (!scaled || scaled_w != vw || scaled_h != vh) {
            free(scaled);
            scaled = malloc((size_t)vw * (size_t)vh * sizeof *scaled);
            scaled_w = vw, scaled_h = vh;
        }
        if (!scaled)
            return;
        for (y = 0; y < vh; y++) {              /* nearest neighbour */
            const unsigned *row = pic.px + (size_t)((long)(y + sy) * pic.h / dh) * (size_t)pic.w;
            for (x = 0; x < vw; x++)
                scaled[(size_t)y * (size_t)vw + (size_t)x] = row[(long)(x + sx) * pic.w / dw];
        }
        x0 = w->r.x + (w->r.w - vw) / 2;
        y0 = w->r.y + (w->r.h - vh) / 2;
        gfx_image(s, x0, y0, scaled, vw, vh, (unsigned)vw);
    }
}

static void changed(void)
{
    status_line();
    gui_damage(view);
}

static void command(struct gui_window *w, int id)
{
    char p[260];
    (void)w;
    switch (id) {
    case C_OPEN:
        snprintf(p, sizeof p, "%s", pic.path[0] ? pic.path : "C:\\");
        if (gui_file_dialog(win, "Open", 0, p, sizeof p))
            load(p);
        break;
    case C_NEXT: step(1); break;
    case C_PREV: step(-1); break;
    case C_EXIT: gui_quit(0); break;
    case C_FIT: fit = 1; changed(); break;
    case C_ACTUAL: fit = 0, zoom = 3; changed(); break;
    case C_IN:
    case C_OUT: {
        int s = scale_of(), i;
        fit = 0;
        if (id == C_IN) {
            for (i = 0; i < 7 && zooms[i] <= s; i++)
                ;
            zoom = i < 7 ? i : 6;
        } else {
            for (i = 6; i > 0 && zooms[i] >= s; i--)
                ;
            zoom = i;
        }
        changed();
        break;
    }
    case C_ABOUT:
        gui_message(win, "About Images", "Images: GLOS's picture viewer. PNG, JPEG, BMP, GIF, TGA, PNM and PCX.",
                    GUI_MB_OK);
        break;
    }
}

static int key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    switch (e->key) {
    case GUI_K_LEFT: sx -= 32; changed(); return 1;
    case GUI_K_RIGHT: sx += 32; changed(); return 1;
    case GUI_K_UP: sy -= 32; changed(); return 1;
    case GUI_K_DOWN: sy += 32; changed(); return 1;
    }
    switch (e->ch) {
    case 'f': case 'F': command(win, C_FIT); return 1;
    case '1': command(win, C_ACTUAL); return 1;
    case '+': case '=': command(win, C_IN); return 1;
    case '-': command(win, C_OUT); return 1;
    }
    return 0;
}

static void resized(struct gui_window *w)
{
    (void)w;
    changed();
}

int main(int argc, char **argv)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    if (gui_init(argc, argv) != 0) {
        printf("IMAGES no GLOS\n");
        return 1;
    }
    win = gui_window("Images", 480, 360, 0);
    win->on_command = command;
    win->on_key = key;
    win->on_resize = resized;
    bar = gui_menubar(win);
    m = gui_menu(bar, "&File");
    gui_menu_item(m, "&Open...", C_OPEN, "Ctrl+O");
    gui_menu_item(m, "&Next", C_NEXT, "PgDn");
    gui_menu_item(m, "&Previous", C_PREV, "PgUp");
    gui_menu_sep(m);
    gui_menu_item(m, "E&xit", C_EXIT, NULL);
    m = gui_menu(bar, "&View");
    gui_menu_item(m, "&Fit to window", C_FIT, "F");
    gui_menu_item(m, "&Actual size", C_ACTUAL, "1");
    gui_menu_item(m, "Zoom &in", C_IN, NULL);
    gui_menu_item(m, "Zoom &out", C_OUT, NULL);
    m = gui_menu(bar, "&Help");
    gui_menu_item(m, "&About Images", C_ABOUT, NULL);
    view = gui_canvas(gui_vbox(win->root, 0, 0), 0, 0, draw, NULL);
    gui_grow(view->parent, 1);
    gui_grow(view, 1);
    status = gui_status(win);
    gui_show(win, 1);
    if (argc > 1)
        load(argv[1]);
    else
        status_line();
    gui_run();
    printf("IMAGES end\n");
    return 0;
}
