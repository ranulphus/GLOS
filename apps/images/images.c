/* IMAGES: GLOS's image viewer (PRD §10.6; docs/milestones-m9.md step D).
 * PNG, JPEG, BMP, GIF (its first frame), TGA and PNM through stb_image
 * (third_party/stb: a parser, in ring 3, as PRD D19 has it), and PCX, the
 * DOS era's own, here, shown in the toolkit's image view (toolkit 2, C10).
 * Fitted to the window (never enlarged) or at a zoom,
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


#include <glos/gui.h>

#include "../shared/picfile.h"

enum { C_OPEN = 1, C_NEXT, C_PREV, C_EXIT, C_FIT, C_ACTUAL, C_IN, C_OUT, C_ABOUT, C_LEFT, C_RIGHT, C_ACROSS, C_OVER,
       C_DETAILS, C_SLIDES, C_FULL, C_EDIT };

static const int zooms[] = { 125, 250, 500, 1000, 2000, 4000, 8000 };   /* tenths of a per cent */

static struct gui_window *win;
static struct gui_widget *view, *status;
static struct {
    unsigned *px;                       /* 00RRGGBBh */
    int w, h;
    int turned;                         /* turned or flipped since it was read (turn) */
    char path[260], kind[8];
} pic;
static int fit = 1, zoom = 3;           /* zoom: an index into zooms when not fitted */
static int sliding;                     /* the slideshow runs (slideshow) */
static struct gui_window *full;         /* the picture alone on the whole screen (fullscreen), or NULL */
static struct gui_widget *full_view;
static char home[260];                  /* where IMAGES.EXE is: PAINT.EXE is beside it */

static void status_line(void);

static int load(const char *path)
{
    unsigned char *d;
    size_t n = 0;
    int w = 0, h = 0;
    unsigned *px = NULL;
    if (!(d = pic_read_all(path, &n))) {
        gui_message(win, GUI_T("Images"), GUI_T("The file isn't there."), GUI_MB_OK);
        return -1;
    }
    px = pic_decode(d, n, path, &w, &h, pic.kind, sizeof pic.kind, GUI_DARK);
    free(d);
    if (!px) {
        char t[300], name[128];
        snprintf(t, sizeof t, GUI_T("%s isn't a picture Images can show."),
                 gui_dos_text(gui_path_name(path), name, sizeof name));
        gui_message(win, GUI_T("Images"), t, GUI_MB_OK);
        return -1;
    }
    free(pic.px);
    pic.px = px, pic.w = w, pic.h = h;
    pic.turned = 0;
    snprintf(pic.path, sizeof pic.path, "%s", path);
    if (view) {                         /* (the view shows the new one from its corner) */
        gui_image_view_set(view, pic.px, pic.w, pic.h, pic.w);
        gui_cview_scroll(view, 0, 0);
    }
    if (full_view) {
        gui_image_view_set(full_view, pic.px, pic.w, pic.h, pic.w);
        gui_cview_zoom(full_view, 0);
        gui_damage(full_view);
    }
    {
        char t[300], name[128];
        snprintf(t, sizeof t, GUI_T("%s - Images"), gui_dos_text(gui_path_name(path), name, sizeof name));
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
        if (!e[i].dir && pic_is_picture(e[i].name)) {
            gui_path_join(next, sizeof next, dir, e[i].name);
            free(e);
            load(next);
            return;
        }
    }
    free(e);
}

/* ---- Open's preview: the chosen picture made small, its size and type under it. A picture of more than 512 K
   pixels is only described (decoding one at each step down the list would keep a 486 waiting). */

#define THUMB_MAX (512L * 1024)

static struct {
    char path[260];
    unsigned *px;                       /* the small copy (NULL: none) */
    int w, h;                           /* its size */
    int iw, ih;                         /* the picture's (0: not a picture) */
    char kind[8];
} thumb;

static void make_thumb(const char *path, int room_w, int room_h)
{
    unsigned char *d;
    size_t n = 0;
    int w = 0, h = 0, x, y;
    unsigned *px;
    free(thumb.px);
    memset(&thumb, 0, sizeof thumb);
    snprintf(thumb.path, sizeof thumb.path, "%s", path);
    if (!(d = pic_read_all(path, &n)))
        return;
    if (strcmp(pic_ext(path), "PCX") && pic_info(d, n, &w, &h)) {
        thumb.iw = w, thumb.ih = h;
        snprintf(thumb.kind, sizeof thumb.kind, "%s", pic_ext(path));
        if ((long)w * h > THUMB_MAX) {
            free(d);
            return;
        }
    }
    px = pic_decode(d, n, path, &w, &h, thumb.kind, sizeof thumb.kind, GUI_DARK);
    free(d);
    if (!px)
        return;
    thumb.iw = w, thumb.ih = h;
    thumb.w = w, thumb.h = h;                   /* (made smaller to fit, never larger) */
    if (thumb.w > room_w)
        thumb.h = (int)((long)thumb.h * room_w / thumb.w), thumb.w = room_w;
    if (thumb.h > room_h)
        thumb.w = (int)((long)thumb.w * room_h / thumb.h), thumb.h = room_h;
    if (thumb.w < 1)
        thumb.w = 1;
    if (thumb.h < 1)
        thumb.h = 1;
    thumb.px = malloc((size_t)thumb.w * (size_t)thumb.h * sizeof *thumb.px);
    for (y = 0; thumb.px && y < thumb.h; y++)   /* (each pixel the mean of the ones it covers) */
        for (x = 0; x < thumb.w; x++) {
            int y0 = (int)((long)y * h / thumb.h), y1 = (int)((long)(y + 1) * h / thumb.h);
            int x0 = (int)((long)x * w / thumb.w), x1 = (int)((long)(x + 1) * w / thumb.w), i, j;
            unsigned long r = 0, g = 0, b = 0, k;
            if (y1 <= y0)
                y1 = y0 + 1;
            if (x1 <= x0)
                x1 = x0 + 1;
            for (j = y0; j < y1; j++)
                for (i = x0; i < x1; i++) {
                    unsigned c = px[(size_t)j * (size_t)w + (size_t)i];
                    r += c >> 16 & 255, g += c >> 8 & 255, b += c & 255;
                }
            k = (unsigned long)(y1 - y0) * (unsigned long)(x1 - x0);
            thumb.px[(size_t)y * (size_t)thumb.w + (size_t)x] = (unsigned)(r / k << 16 | g / k << 8 | b / k);
        }
    free(px);
}

static void preview(struct gui_widget *pane, struct gfx_surface *s, const char *path, void *ctx)
{
    int room_h = pane->r.h - 30;
    char t[48];
    (void)ctx;
    if (!pic_is_picture(gui_path_name(path)))
        return;
    if (strcmp(thumb.path, path) != 0)
        make_thumb(path, pane->r.w - 8, room_h);
    if (thumb.px)
        gfx_image(s, pane->r.x + (pane->r.w - thumb.w) / 2, pane->r.y + 4 + (room_h - thumb.h) / 2, thumb.px, thumb.w,
                  thumb.h, (unsigned)thumb.w);
    if (thumb.iw) {
        snprintf(t, sizeof t, "%d x %d %s", thumb.iw, thumb.ih, thumb.kind);
        gui_draw_text(s, pane->r.x + 6, pane->r.y + pane->r.h - 20, gui_font, t, GUI_FIELD_TEXT);
    }
}

static const char *const filters[] = { GUI_N("Pictures|*.PNG;*.JPG;*.BMP;*.GIF;*.TGA;*.PNM;*.PPM;*.PGM;*.PCX"),
                                        GUI_N("All files|*.*"), NULL };
static struct gui_file_opts file_opts = { filters, 0, NULL, 0, NULL, 0, preview, NULL };

/* ---- showing */

static int scale_of(void)               /* the size shown, in tenths of a per cent */
{
    int num, den;
    if (!pic.px)
        return 1000;
    if (!fit)
        return zooms[zoom];
    gui_cview_get_scale(view, &num, &den);
    return den > 0 ? (int)((long)num * 1000 / den) : 1000;
}

static void status_line(void)
{
    char t[200], name[128];
    if (!pic.px) {
        gui_set_text(status, GUI_T("No picture"));
        return;
    }
    snprintf(t, sizeof t, "%s   %d x %d   %s   %d%%%s", gui_dos_text(gui_path_name(pic.path), name, sizeof name), pic.w,
             pic.h, pic.kind,
             (scale_of() + 5) / 10, fit ? " (fitted)" : "");
    if (sliding)
        snprintf(t + strlen(t), sizeof t - strlen(t), "   %s", GUI_T("Slideshow"));
    gui_set_text(status, t);
}

static void changed(void)               /* the view at the scale asked for, from its corner; the status line */
{
    if (fit)
        gui_cview_zoom(view, 0);
    else
        gui_cview_scale(view, zooms[zoom], 1000);
    gui_cview_scroll(view, 0, 0);
    status_line();
    gui_damage(view);
}

/* ---- turned and flipped (phase E): the picture in memory, a quarter turn right (1) or left (-1), flipped left to
   right (2) or top to bottom (3). What is shown, not the file: saving a picture is Paint's (docs/v1/apps/paint.md). */
static void turn(int how)
{
    int w = pic.w, h = pic.h, nw = how == 1 || how == -1 ? h : w, nh = how == 1 || how == -1 ? w : h, x, y;
    unsigned *n;
    if (!pic.px)
        return;
    if (!(n = malloc((size_t)nw * (size_t)nh * sizeof *n))) {
        gui_message(win, GUI_T("Images"), GUI_T("There isn't the memory to turn this picture."), GUI_MB_OK);
        return;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            unsigned c = pic.px[(size_t)y * w + x];
            switch (how) {
            case 1: n[(size_t)x * nw + (h - 1 - y)] = c; break;
            case -1: n[(size_t)(w - 1 - x) * nw + y] = c; break;
            case 2: n[(size_t)y * nw + (w - 1 - x)] = c; break;
            default: n[(size_t)(h - 1 - y) * nw + x] = c; break;
            }
        }
    gui_image_view_set(view, n, nw, nh, nw);
    if (full_view) {
        gui_image_view_set(full_view, n, nw, nh, nw);
        gui_cview_zoom(full_view, 0);
        gui_damage(full_view);
    }
    free(pic.px);
    pic.px = n, pic.w = nw, pic.h = nh;
    pic.turned = 1;
    printf("IMAGES turned %s %dx%d\n", how == 1 ? "right" : how == -1 ? "left" : how == 2 ? "across" : "over", nw, nh);
    fflush(stdout);
    changed();
}

/* ---- the picture's details (phase E): what it is and where, its size in pixels and in bytes, when it was
   last written, how large it is shown. */
static void details_ok(struct gui_widget *b, int what)
{
    (void)what;
    gui_end_modal(b->win, 1);
}

static void details(void)
{
    struct gui_window *d;
    struct gui_widget *box, *form, *row;
    struct gui_dirent e;
    char t[160], name[128], dir[260];
    if (!pic.px)
        return;
    d = gui_window(GUI_T("Details"), 300, 0, GUI_W_DIALOG);
    d->owner = win;
    box = gui_vbox(d->root, 6, 8);
    form = gui_form(box);
    gui_label(gui_row(form, GUI_T("Name:"), NULL), gui_dos_text(gui_path_name(pic.path), name, sizeof name));
    snprintf(dir, sizeof dir, "%s", pic.path);
    gui_path_up(dir);
    gui_label(gui_row(form, GUI_T("Where:"), NULL), dir);
    gui_label(gui_row(form, GUI_T("Kind:"), NULL), pic.kind);
    snprintf(t, sizeof t, pic.turned ? GUI_T("%d x %d pixels (turned here)") : GUI_T("%d x %d pixels"), pic.w, pic.h);
    gui_label(gui_row(form, GUI_T("Size:"), NULL), t);
    if (gui_stat(pic.path, &e) == 0) {
        snprintf(t, sizeof t, GUI_T("%lu bytes"), e.size);
        gui_label(gui_row(form, GUI_T("File:"), NULL), t);
        snprintf(t, sizeof t, "%04u-%02u-%02u %02u:%02u", (e.date >> 9) + 1980, (e.date >> 5) & 15, e.date & 31,
                 e.time >> 11, (e.time >> 5) & 63);
        gui_label(gui_row(form, GUI_T("Modified:"), NULL), t);
    }
    snprintf(t, sizeof t, fit ? GUI_T("%d%% (fitted)") : "%d%%", (scale_of() + 5) / 10);
    gui_label(gui_row(form, GUI_T("Shown at:"), NULL), t);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), details_ok, 1)->flags |= GUI_DEFAULT | GUI_CANCEL;
    printf("IMAGES details %s\n", pic.path);
    fflush(stdout);
    gui_modal(d);                               /* (which closes it) */
}

/* ---- the slideshow (phase E): the folder's next picture every few seconds, round and round, until it is
   turned off (S again, Esc) or another picture is asked for by hand. */
#define SLIDE_MS 4000

static void slide_tick(void *arg)
{
    (void)arg;
    step(1);
}

static void slideshow(int on)
{
    if (on == sliding)
        return;
    sliding = on;
    gui_idle(slide_tick, NULL, on ? SLIDE_MS : 0);
    gui_command_check(win, C_SLIDES, on);
    printf("IMAGES slideshow %s\n", on ? "on" : "off");
    fflush(stdout);
    status_line();
}

/* ---- full screen (phase E): the picture alone on the whole screen, fitted: a second window, bare, shown full
   (GLOS takes its task bar away while that window has the keyboard). Esc, F11 or Enter comes back; the keys
   that go to the next picture, turn it and run the slideshow are the same there. */
#define K_F11 0x57                      /* (the menu's own shortcut in the main window: sdk/gui/menu.c) */

static int key(struct gui_window *w, struct gui_ev *e);
static int full_event(struct gui_widget *w, struct gui_ev *e, int x, int y);
static void full_closed(struct gui_window *w);

static void fullscreen(int on)
{
    struct gui_window *f = full;
    int sw = 640, sh = 480;
    if (on == (full != NULL) || (on && !pic.px))
        return;
    printf("IMAGES full %s\n", on ? "on" : "off");
    fflush(stdout);
    if (!on) {
        full = NULL, full_view = NULL;
        gui_close_later(f);                     /* (its own key may be what closes it) */
        return;
    }
    gui_screen_size(&sw, &sh);
    full = gui_window(GUI_T("Images"), sw, sh, GUI_W_BARE);
    full_view = gui_image_view(gui_vbox(full->root, 0, 0));
    gui_grow(full_view->parent, 1);
    gui_grow(full_view, 1);
    full->focus = full_view;
    full->on_close = full_closed;
    gui_cview_on_event(full_view, full_event);
    gui_image_view_set(full_view, pic.px, pic.w, pic.h, pic.w);
    gui_show(full, 1);
    glos_show(full->id, GLOS_WS_FULL);
}

static int full_event(struct gui_widget *w, struct gui_ev *e, int x, int y)
{
    (void)w, (void)x, (void)y;
    if (e->type != GUI_EV_KEY)
        return 0;
    if ((e->key == GUI_K_ESC && !sliding) || e->key == K_F11 || e->key == GUI_K_ENTER) {
        fullscreen(0);
        return 1;
    }
    return key(win, e);
}

static void full_closed(struct gui_window *w)   /* (closed by GLOS: Alt+F4, the task list) */
{
    (void)w;
    fullscreen(0);
}

static void command(struct gui_window *w, int id)
{
    char p[260];
    (void)w;
    switch (id) {
    case C_OPEN:
        snprintf(p, sizeof p, "%s", pic.path[0] ? pic.path : glos_boot_root());
        if (gui_file_dialog_ex(win, GUI_T("Open"), 0, p, sizeof p, &file_opts))
            load(p);
        free(thumb.px);                         /* (the preview's copy: not wanted until the next Open) */
        memset(&thumb, 0, sizeof thumb);
        break;
    case C_NEXT: slideshow(0); step(1); break;
    case C_PREV: slideshow(0); step(-1); break;
    case C_EXIT: gui_quit(0); break;
    case C_LEFT: turn(-1); break;
    case C_RIGHT: turn(1); break;
    case C_ACROSS: turn(2); break;
    case C_OVER: turn(3); break;
    case C_DETAILS: details(); break;
    case C_SLIDES: slideshow(!sliding); break;
    case C_FULL: fullscreen(!full); break;
    case C_EDIT:                                /* the picture's file opened in Paint (the editor: paint.md) */
        if (pic.path[0]) {
            char cmd[600];
            snprintf(cmd, sizeof cmd, "%s\\PAINT.EXE %s", home, pic.path);
            glos_job_run(cmd, home, 0, GLOS_RUN_HIDDEN, GLOS_RUNF_DETACH, 0);
            if (glos_error() != 0)
                gui_message(win, GUI_T("Images"), GUI_T("Paint couldn't be started."), GUI_MB_OK);
            printf("IMAGES edit %s\n", pic.path);
            fflush(stdout);
        }
        break;
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
        gui_message(win, GUI_T("About Images"),
                    GUI_T("Images: GLOS's picture viewer. PNG, JPEG, BMP, GIF, TGA, PNM and PCX."),
                    GUI_MB_OK);
        break;
    }
}

static int key(struct gui_window *w, struct gui_ev *e)      /* (the arrows are the view's own) */
{
    (void)w;
    if (e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
        return 0;
    switch (e->key) {
    case GUI_K_PGDN: command(win, C_NEXT); return 1;
    case GUI_K_PGUP: command(win, C_PREV); return 1;
    case GUI_K_ESC:
        if (!sliding)
            return 0;
        slideshow(0);
        return 1;
    }
    switch (e->ch) {
    case 'l': case 'L': command(win, C_LEFT); return 1;
    case 'r': case 'R': command(win, C_RIGHT); return 1;
    case 's': case 'S': command(win, C_SLIDES); return 1;
    case 'e': case 'E': command(win, C_EDIT); return 1;
    case 'f': case 'F': command(win, C_FIT); return 1;
    case '1': command(win, C_ACTUAL); return 1;
    case '+': case '=': command(win, C_IN); return 1;
    case '-': command(win, C_OUT); return 1;
    }
    return 0;
}

static int view_event(struct gui_widget *w, struct gui_ev *e, int x, int y)   /* Images' keys, before the view's */
{
    (void)w, (void)x, (void)y;
    return e->type == GUI_EV_KEY && key(win, e);
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
    {                                           /* (DJGPP gives the program's name with / and its case) */
        char *p;
        snprintf(home, sizeof home, "%s", argv[0]);
        for (p = home; *p; p++)
            *p = *p == '/' ? '\\' : (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
        if (strrchr(home, '\\'))
            *strrchr(home, '\\') = 0;
        else
            snprintf(home, sizeof home, "%s", glos_home());
    }
    win = gui_window(GUI_T("Images"), 480, 360, 0);
    gui_window_icon(win, "app-images");
    win->on_command = command;
    win->on_key = key;
    win->on_resize = resized;
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&File"));
    gui_menu_item(m, GUI_T("&Open..."), C_OPEN, GUI_T("Ctrl+O"));
    gui_menu_item(m, GUI_T("&Next"), C_NEXT, GUI_T("PgDn"));
    gui_menu_item(m, GUI_T("&Previous"), C_PREV, GUI_T("PgUp"));
    gui_menu_item(m, GUI_T("&Details"), C_DETAILS, GUI_T("Alt+Enter"));
    gui_menu_item(m, GUI_T("&Edit in Paint"), C_EDIT, "E");
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("E&xit"), C_EXIT, NULL);
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_item(m, GUI_T("&Fit to window"), C_FIT, GUI_T("F"));
    gui_menu_item(m, GUI_T("&Actual size"), C_ACTUAL, "1");
    gui_menu_item(m, GUI_T("Zoom &in"), C_IN, NULL);
    gui_menu_item(m, GUI_T("Zoom &out"), C_OUT, NULL);
    gui_menu_sep(m);
    gui_menu_item(m, GUI_T("&Slideshow"), C_SLIDES, "S");
    gui_menu_item(m, GUI_T("F&ull Screen"), C_FULL, "F11");
    m = gui_menu(bar, GUI_T("&Image"));
    gui_menu_item(m, GUI_T("Turn &Left"), C_LEFT, "L");
    gui_menu_item(m, GUI_T("Turn &Right"), C_RIGHT, "R");
    gui_menu_item(m, GUI_T("Flip &Across"), C_ACROSS, NULL);
    gui_menu_item(m, GUI_T("Flip &Over"), C_OVER, NULL);
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_item(m, GUI_T("&About Images"), C_ABOUT, NULL);
    {                                           /* the toolbar (phase E) */
        static const struct {
            int id;
            const char *text, *glyph, *tip;
        } tools[] = {
            { C_OPEN, GUI_N("&Open..."), "open", GUI_N("Open (Ctrl+O)") },
            { C_PREV, GUI_N("&Previous"), "back", GUI_N("Previous picture (PgUp)") },
            { C_NEXT, GUI_N("&Next"), "forward", GUI_N("Next picture (PgDn)") },
            { 0, NULL, NULL, NULL },
            { C_IN, GUI_N("Zoom &in"), "zoom-in", GUI_N("Zoom in (+)") },
            { C_OUT, GUI_N("Zoom &out"), "zoom-out", GUI_N("Zoom out (-)") },
            { C_FIT, GUI_N("&Fit to window"), "view", GUI_N("Fit to window (F)") },
            { 0, NULL, NULL, NULL },
            { C_LEFT, GUI_N("Turn &Left"), "undo", GUI_N("Turn left (L)") },
            { C_RIGHT, GUI_N("Turn &Right"), "redo", GUI_N("Turn right (R)") },
            { 0, NULL, NULL, NULL },
            { C_SLIDES, GUI_N("&Slideshow"), "play", GUI_N("Slideshow (S)") },
            { C_DETAILS, GUI_N("&Details"), "info", GUI_N("Details (Alt+Enter)") },
            { C_EDIT, GUI_N("&Edit in Paint"), "edit", GUI_N("Edit in Paint (E)") },
        };
        struct gui_widget *tb = gui_toolbar(win);
        size_t k;
        for (k = 0; k < sizeof tools / sizeof tools[0]; k++)
            if (!tools[k].id) {
                gui_toolbar_sep(tb);
            } else {
                gui_command(win, tools[k].id, GUI_T(tools[k].text), NULL, gui_glyph(tools[k].glyph), GUI_T(tools[k].tip));
                gui_toolbar_command(tb, tools[k].id);
            }
    }
    view = gui_image_view(gui_vbox(win->root, 0, 0));
    gui_cview_on_event(view, view_event);
    gui_grow(view->parent, 1);
    gui_grow(view, 1);
    win->focus = view;
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
