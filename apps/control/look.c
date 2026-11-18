/* CONTROL: the desktop's look (phase D, docs/v1/phase-d.md D5): beside the Desktop page's settings, a small
 * picture of the desktop as the wallpaper and its style in the page would make it (not yet applied: what the field
 * and the list say), and a button that chooses the picture with the file dialog.
 *
 * The picture is made as FILES makes the real one (apps/files/wallmake.h), for the screen's own work area, then
 * made small (80 by 60); the task bar and a window are drawn on it in the theme's colours. It follows the field and the list:
 * looked at a few times a second (look_poll), made again when either has changed and the user has stopped typing. */
#include "../files/wallmake.h"

#include "control.h"

#define PW 80                           /* (what the Desktop page has room for beside its settings at 640x480) */
#define PH 60

static struct gui_window *owner;
static struct gui_widget *field, *choice, *view;
static const char *const *styles;       /* the choice's values, as the schema has them */
static unsigned small[PW * PH];
static int have;                        /* small[] is a wallpaper's (else the desktop's colour) */
static char seen_path[200], seen_style[16];
static int still;                       /* polls since the field last changed */

static const char *style_now(void)
{
    int v = gui_value(choice);
    return v >= 0 && styles[v] ? styles[v] : "fill";
}

/* The picture again, from what the page says now. */
static void make(void)
{
    struct glos_screen scr;
    unsigned *full;
    int w = 640, h = 460, x, y;
    memset(&scr, 0, sizeof scr);
    scr.size = sizeof scr;
    if (glos_screen(&scr) == 0 && scr.work_w > 0 && scr.work_h > 0)
        w = scr.work_w, h = scr.work_h;
    have = 0;
    if ((full = wallmake(seen_path, seen_style, w, h, GUI_ROLE(GLOS_ROLE_DESK))) != NULL) {
        for (y = 0; y < PH; y++)                /* (each place the mean of the pixels it stands for) */
            for (x = 0; x < PW; x++) {
                int x0 = x * w / PW, x1 = (x + 1) * w / PW, y0 = y * h / PH, y1 = (y + 1) * h / PH, i, j;
                unsigned long r = 0, g = 0, b = 0, k = 0;
                for (j = y0; j < y1; j++)
                    for (i = x0; i < x1; i++, k++) {
                        unsigned c = full[(size_t)j * (size_t)w + (size_t)i];
                        r += c >> 16 & 255, g += c >> 8 & 255, b += c & 255;
                    }
                small[y * PW + x] = k ? (unsigned)(r / k << 16 | g / k << 8 | b / k) : 0;
            }
        free(full);
        have = 1;
    }
    gui_damage(view);
}

static void draw(struct gui_widget *w, struct gfx_surface *s)
{
    int x = w->r.x + 1, y = w->r.y + 1;         /* (inside the line round it) */
    if (have)
        gfx_image(s, x, y, small, PW, PH, PW);
    else
        gfx_fill(s, x, y, PW, PH, GUI_ROLE(GLOS_ROLE_DESK));
    gfx_fill(s, x + 14, y + 10, 36, 24, GUI_ROLE(GLOS_ROLE_FACE));              /* a window, */
    gfx_fill(s, x + 14, y + 10, 36, 4, GUI_ROLE(GLOS_ROLE_TITLE));
    gfx_frame(s, x + 14, y + 10, 36, 24, GUI_ROLE(GLOS_ROLE_FRAME));
    gfx_fill(s, x, y + PH - 5, PW, 5, GUI_ROLE(GLOS_ROLE_BAR));                 /* the task bar, */
    gfx_frame(s, x - 1, y - 1, PW + 2, PH + 2, GUI_ROLE(GLOS_ROLE_LINE));       /* and the screen's edge */
}

/* Choose a picture...: the file dialog, from the folder of the one the field has. */
static void choose(struct gui_widget *b, int what)
{
    static const char *const filters[] = { "Pictures|*.PNG;*.JPG;*.JPEG;*.BMP;*.GIF;*.TGA", "Every file|*.*", NULL };
    struct gui_file_opts o;
    char path[200];
    (void)b, (void)what;
    memset(&o, 0, sizeof o);
    o.filters = filters;
    snprintf(path, sizeof path, "%s", gui_get_text(field));
    if (gui_file_dialog_ex(owner, GUI_T("Wallpaper"), 0, path, sizeof path, &o) == 1) {
        gui_set_text(field, path);
        still = 99;                             /* (chosen, not typed: shown at once) */
        look_poll();
    }
}

/* The picture follows the page: made again once the field has been the same for two looks. */
void look_poll(void)
{
    const char *p, *st;
    if (!field)
        return;
    p = gui_get_text(field), st = style_now();
    if (!strcmp(p, seen_path) && !strcmp(st, seen_style))
        return;
    if (strcmp(st, seen_style) || ++still >= 2) {
        snprintf(seen_path, sizeof seen_path, "%s", p);
        snprintf(seen_style, sizeof seen_style, "%s", st);
        still = 0;
        make();
    }
}

/* The picture and its button in `side`, for the wallpaper's field and its style's list (whose values are
   `values`, NULL-ended). */
void look_add(struct gui_widget *side, struct gui_window *win, struct gui_widget *path, struct gui_widget *style,
              const char *const *values)
{
    owner = win;
    field = path;
    choice = style;
    styles = values;
    view = gui_canvas(side, PW + 2, PH + 2, draw, NULL);
    gui_button(side, GUI_T("&Choose..."), choose, 0);
    seen_path[0] = 1, seen_path[1] = 0;         /* (nothing seen: the first look makes it) */
    still = 99;
}
