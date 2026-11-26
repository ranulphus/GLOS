/* CONTROL: the desktop's look (phase D, docs/v1/phase-d.md D5): beside the Desktop page's settings, a small
 * picture of the desktop as the wallpaper and its style in the page would make it (not yet applied: what the field
 * and the list say), and a button that chooses the picture with the file dialog.
 *
 * The picture is made as FILES makes the real one (apps/files/wallmake.h), for the screen's own work area, then
 * made small (80 by 60); the task bar and a window are drawn on it in the theme's colours. It follows the field and the list:
 * looked at a few times a second (look_poll), made again when either has changed and the user has stopped typing. */
#include "../files/wallmake.h"

#include <strings.h>

#include "control.h"

#define PW 80                           /* (what the Desktop page has room for beside its settings at 640x480) */
#define PH 60

static struct gui_window *owner;
static char su_home[100];               /* this program's folder (GLOS's own programs are beside it) */
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

/* Choose a picture...: the file dialog, from the folder of the one the field has; with none, from GLOS's own
   wallpapers' (WALLS beside the programs: tools/mkwalls.py), which is also one of its places. */
static void choose(struct gui_widget *b, int what)
{
    static const char *const filters[] = { "Pictures|*.PNG;*.JPG;*.JPEG;*.BMP;*.GIF;*.TGA", "Every file|*.*", NULL };
    static char walls[120];
    const char *places[2] = { NULL, NULL };
    struct gui_file_opts o;
    char path[200];
    (void)b, (void)what;
    memset(&o, 0, sizeof o);
    o.filters = filters;
    snprintf(walls, sizeof walls, "%s%sWALLS", su_home, su_home[0] ? "\\" : "");
    if (gui_is_dir(walls))
        places[0] = walls;
    o.recent = places;
    snprintf(path, sizeof path, "%s", gui_get_text(field));
    if (!path[0] && places[0])
        snprintf(path, sizeof path, "%s", walls);
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

/* ---- the programs started with the desktop ([desktop] start: names with spaces between them), as a list

   Startup...: a dialog with the field's programs one a row; Add... chooses one with the file dialog (one beside
   this program is written by its name alone, as the setting has GLOS's own), Remove takes the chosen row out; OK
   writes the list back in the field, Cancel leaves it. */

#define SU_MAX 12
#define SU_ADD    10                   /* (its buttons: not GUI_OK's and GUI_CANCEL_R's numbers) */
#define SU_REMOVE 11

static struct gui_widget *su_field, *su_list;
static struct gui_window *su_win;
static char su_item[SU_MAX][80];
static int su_n;

static void su_fill(void)
{
    int i, keep = gui_list_get(su_list);
    gui_list_clear(su_list);
    for (i = 0; i < su_n; i++) {
        const char *cells[1] = { su_item[i] };
        gui_list_add(su_list, cells, NULL, NULL);
    }
    if (su_n)
        gui_list_set(su_list, keep >= 0 && keep < su_n ? keep : su_n - 1);
}

static void su_button(struct gui_widget *b, int what)
{
    static const char *const filters[] = { "Programs|*.EXE;*.COM;*.BAT", "Every file|*.*", NULL };
    (void)what;
    if (b->id == SU_ADD && su_n < SU_MAX) {     /* Add... */
        struct gui_file_opts o;
        char path[160], *name;
        memset(&o, 0, sizeof o);
        o.filters = filters;
        path[0] = 0;
        if (gui_file_dialog_ex(su_win, GUI_T("Add a Program"), 0, path, sizeof path, &o) != 1 || !path[0])
            return;
        if (strchr(path, ' ')) {
            gui_message(su_win, GUI_T("Start With The Desktop"), GUI_T("A program whose path has a space can't be listed here."),
                        GUI_MB_OK);
            return;
        }
        name = strrchr(path, '\\');
        if (name && su_home[0] && (size_t)(name - path) == strlen(su_home) && !strncasecmp(path, su_home, strlen(su_home)))
            snprintf(su_item[su_n++], sizeof su_item[0], "%s", name + 1);      /* (beside GLOS: its name alone) */
        else
            snprintf(su_item[su_n++], sizeof su_item[0], "%s", path);
        su_fill();
    } else if (b->id == SU_REMOVE) {            /* Remove */
        int r = gui_list_get(su_list), i;
        if (r < 0 || r >= su_n)
            return;
        for (i = r; i + 1 < su_n; i++)
            strcpy(su_item[i], su_item[i + 1]);
        su_n--;
        su_fill();
    } else if (b->id == GUI_OK || b->id == GUI_CANCEL_R) {
        gui_end_modal(su_win, b->id);
    }
}

static void startup(struct gui_widget *b, int what)
{
    struct gui_widget *box, *row;
    const char *p = gui_get_text(su_field);
    char line[256];
    int i;
    size_t n = 0;
    (void)b, (void)what;
    for (su_n = 0; *p && su_n < SU_MAX;) {      /* the field's programs */
        size_t k = 0;
        while (*p == ' ')
            p++;
        for (; *p && *p != ' '; p++)
            if (k < sizeof su_item[0] - 1)
                su_item[su_n][k++] = *p;
        su_item[su_n][k] = 0;
        if (k)
            su_n++;
    }
    su_win = gui_window(GUI_T("Start With The Desktop"), 300, 200, GUI_W_DIALOG);
    su_win->owner = owner;
    box = gui_vbox(su_win->root, 6, 8);
    gui_grow(box, 1);
    gui_label(box, GUI_T("Started each time the desktop comes on:"));
    su_list = gui_list(box, 1, NULL, NULL);
    gui_grow(su_list, 1);
    row = gui_hbox(box, 6, 0);
    gui_button(row, GUI_T("&Add..."), su_button, SU_ADD);
    gui_button(row, GUI_T("&Remove"), su_button, SU_REMOVE);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), su_button, GUI_OK)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), su_button, GUI_CANCEL_R)->flags |= GUI_CANCEL;
    su_fill();
    if (gui_modal(su_win) == GUI_OK) {
        line[0] = 0;
        for (i = 0; i < su_n; i++)
            n += (size_t)snprintf(line + n, sizeof line - n, "%s%s", n ? " " : "", su_item[i]);
        gui_set_text(su_field, line);
    }
    gui_close(su_win);
    su_win = NULL;
}

/* The picture and its button in `side`, for the wallpaper's field and its style's list (whose values are
   `values`, NULL-ended); and Startup..., for the field of the programs started with the desktop (`start`). */
void look_add(struct gui_widget *side, struct gui_window *win, struct gui_widget *path, struct gui_widget *style,
              const char *const *values, struct gui_widget *start, const char *argv0)
{
    const char *sl = NULL, *q;
    size_t i;
    for (q = argv0; *q; q++)                    /* this program's folder, as DOS writes it (GLOS's own are there) */
        if (*q == '\\' || *q == '/')
            sl = q;
    snprintf(su_home, sizeof su_home, "%.*s", sl ? (int)(sl - argv0) : 0, argv0);
    for (i = 0; su_home[i]; i++)
        if (su_home[i] == '/')
            su_home[i] = '\\';
    su_field = start;
    owner = win;
    field = path;
    choice = style;
    styles = values;
    view = gui_canvas(side, PW + 2, PH + 2, draw, NULL);
    gui_button(side, GUI_T("&Choose..."), choose, 0);
    if (start)
        gui_button(side, GUI_T("Start&up..."), startup, 0);
    seen_path[0] = 1, seen_path[1] = 0;         /* (nothing seen: the first look makes it) */
    still = 99;
}
