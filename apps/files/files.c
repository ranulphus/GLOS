/* FILES: GLOS's file manager and the desktop's shell (PRD D33, §10.6;
 * docs/milestones-m9.md step F; folder.c has the folders' windows).
 *
 *   FILES              the desktop's shell: the icons on the desktop (the
 *                      drives, the apps, programs whose profile says
 *                      desktop = 1), the launcher (the task bar's mark,
 *                      Ctrl+Esc, the Windows key), Ctrl+Alt+Del's task list
 *                      (the monitor); folders open in windows of their own
 *   FILES -browse [DIR]  a browser: a tree of folders and a list
 *   FILES -folder DIR  a folder's window
 *
 * A file is opened by the program [open] names for its kind (.TXT = ...),
 * or, without one: text by Notepad, pictures by Images, a program run as
 * itself, a batch file in a terminal. The apps are beside FILES.EXE (and
 * GLOS.EXE). Where the desktop's icons are kept in DESKTOP.CFG there.
 * "FILES ..." lines on stdout say what it did (the suite's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"

char files_home[260];

static struct gui_window *desk;
static struct gui_widget *area;
static struct gui_menu *launcher;

struct item {
    char name[40];                      /* shown under it */
    char what[260];                     /* a path (drive, folder, file) or "app NAME.EXE" */
    int kind;                           /* IC_ */
    int x, y;                           /* where it is on the desktop */
};

#define ITEMS 48
#define CELL_W 76
#define CELL_H 64
static struct item items[ITEMS];
static int nitems, chosen = -1, dragging = -1, drag_dx, drag_dy, moved;

static const struct { const char *title, *exe; int kind; } apps[] = {
    { "Files", "FILES.EXE -browse", IC_FILES }, { "Terminal", "TERM.EXE", IC_TERMINAL },
    { "Notepad", "NOTEPAD.EXE", IC_NOTEPAD }, { "Calculator", "CALC.EXE", IC_CALC }, { "Images", "IMAGES.EXE", IC_IMAGES },
    { "Monitor", "MONITOR.EXE", IC_MONITOR }, { "Control Panel", "CONTROL.EXE", IC_CONTROL } };
#define NAPPS ((int)(sizeof apps / sizeof *apps))

static void say(const char *what, const char *a)
{
    printf("FILES %s %s\n", what, a);
    fflush(stdout);
}

/* ---- running and opening */

int files_run(const char *command, const char *dir)
{
    struct glos_run r;
    memset(&r, 0, sizeof r);
    r.flags = GLOS_RUN_DETACH;
    snprintf(r.command, sizeof r.command, "%s", command);
    snprintf(r.dir, sizeof r.dir, "%s", dir && dir[0] ? dir : files_home);
    if (glos_run(&r) < 0) {
        gui_message(desk, "Files", "GLOS couldn't start it.", GUI_MB_OK);
        return -1;
    }
    say("run", command);
    return 0;
}

static void run_app(const char *exe_and_args, const char *arg)
{
    char cmd[300];
    snprintf(cmd, sizeof cmd, "%s\\%s%s%s", files_home, exe_and_args, arg ? " " : "", arg ? arg : "");
    files_run(cmd, NULL);
}

static int ext_in(const char *path, const char *list)
{
    const char *d = strrchr(gui_path_name(path), '.'), *p;
    size_t n;
    if (!d)
        return 0;
    d++;
    n = strlen(d);
    for (p = list; *p; ) {
        const char *e = strchr(p, ' ');
        size_t k = e ? (size_t)(e - p) : strlen(p);
        size_t i;
        for (i = 0; i < k && i < n && (d[i] & ~0x20) == p[i]; i++)
            ;
        if (i == k && k == n)
            return 1;
        p += k + (e ? 1 : 0);
    }
    return 0;
}

int files_open(const char *path)
{
    char ext[8] = ".", prog[120], dir[260];
    const char *d = strrchr(gui_path_name(path), '.');
    if (gui_is_dir(path)) {
        folder_open(path);
        return 0;
    }
    snprintf(dir, sizeof dir, "%s", path);
    gui_path_up(dir);
    if (d) {
        snprintf(ext + 1, sizeof ext - 1, "%s", d + 1);
        if (glos_config_get("open", ext, prog, sizeof prog) > 0 && prog[0]) {      /* [open] says */
            char cmd[400];
            if (strchr(prog, '\\') || strchr(prog, ':'))
                snprintf(cmd, sizeof cmd, "%s %s", prog, path);
            else
                snprintf(cmd, sizeof cmd, "%s\\%s %s", files_home, prog, path);
            return files_run(cmd, dir);
        }
    }
    if (ext_in(path, "EXE COM"))
        return files_run(path, dir);
    if (ext_in(path, "BAT")) {
        run_app("TERM.EXE", path);
        return 0;
    }
    if (ext_in(path, "PNG JPG JPEG BMP GIF TGA PCX PPM PGM PNM")) {
        run_app("IMAGES.EXE", path);
        return 0;
    }
    run_app("NOTEPAD.EXE", path);               /* (anything else: as text) */
    return 0;
}

/* ---- the desktop's icons */

static void place_default(int i)
{
    int per = (desk->h - 8) / CELL_H;
    if (per < 1)
        per = 1;
    items[i].x = 8 + (i / per) * CELL_W;
    items[i].y = 8 + (i % per) * CELL_H;
}

static void add_item(const char *name, const char *what, int kind)
{
    if (nitems == ITEMS)
        return;
    snprintf(items[nitems].name, sizeof items[nitems].name, "%s", name);
    snprintf(items[nitems].what, sizeof items[nitems].what, "%s", what);
    items[nitems].kind = kind;
    place_default(nitems);
    nitems++;
}

static void positions_load(void)
{
    char p[300], line[400];
    FILE *f;
    snprintf(p, sizeof p, "%s\\DESKTOP.CFG", files_home);
    if (!(f = gui_fopen(p, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        int x, y, i, n = 0;
        char name[64];
        if (sscanf(line, "%d %d %n", &x, &y, &n) != 2)
            continue;
        snprintf(name, sizeof name, "%s", line + n);
        name[strcspn(name, "\r\n")] = 0;
        for (i = 0; i < nitems; i++)
            if (!strcmp(items[i].name, name))
                items[i].x = x, items[i].y = y;
    }
    fclose(f);
}

static void positions_save(void)
{
    char p[300];
    FILE *f;
    int i;
    snprintf(p, sizeof p, "%s\\DESKTOP.CFG", files_home);
    if (!(f = gui_fopen(p, "w")))
        return;
    for (i = 0; i < nitems; i++)
        fprintf(f, "%d %d %s\n", items[i].x, items[i].y, items[i].name);
    fclose(f);
}

/* The programs whose profiles say so (desktop = 1, or menu = 1 for the launcher): their sections' names. */
static int programs(const char *key, char names[][40], char titles[][40], int *kinds, int room)
{
    char secs[2048], v[64];
    char *s;
    int n = 0;
    if (glos_config_sections(secs, sizeof secs - 1) <= 0)
        return 0;
    secs[sizeof secs - 1] = 0;
    for (s = strtok(secs, "\n"); s && n < room; s = strtok(NULL, "\n")) {
        if (strncmp(s, "program ", 8))
            continue;
        if (glos_config_get(s, key, v, sizeof v) <= 0 || strcmp(v, "1"))
            continue;
        snprintf(names[n], 40, "%s", s + 8);
        if (glos_config_get(s, "title", v, sizeof v) > 0 && v[0])
            snprintf(titles[n], 40, "%s", v);
        else
            snprintf(titles[n], 40, "%s", s + 8);
        kinds[n] = glos_config_get(s, "icon", v, sizeof v) >= 0 ? icon_by_name(v) : IC_PROGRAM;
        n++;
    }
    return n;
}

static void desktop_items(void)
{
    char letters[27], what[300], name[8];
    char pn[16][40], pt[16][40];
    int pk[16], i, n;
    nitems = 0;
    gui_drives(letters);
    for (i = 0; letters[i]; i++)
        if (letters[i] >= 'C') {                /* (floppies: from the browser) */
            snprintf(name, sizeof name, "%c:", letters[i]);
            snprintf(what, sizeof what, "%c:\\", letters[i]);
            add_item(name, what, IC_DRIVE);
        }
    for (i = 0; i < NAPPS; i++) {
        snprintf(what, sizeof what, "app %s", apps[i].exe);
        add_item(apps[i].title, what, apps[i].kind);
    }
    n = programs("desktop", pn, pt, pk, 16);
    for (i = 0; i < n; i++) {
        snprintf(what, sizeof what, "program %s", pn[i]);
        add_item(pt[i], what, pk[i]);
    }
    positions_load();
}

static void open_item(int i)
{
    if (i < 0 || i >= nitems)
        return;
    if (!strncmp(items[i].what, "app ", 4)) {
        run_app(items[i].what + 4, NULL);
    } else if (!strncmp(items[i].what, "program ", 8)) {
        char sec[48], args[200], dir[100], cmd[300];
        snprintf(sec, sizeof sec, "program %s", items[i].what + 8);
        if (glos_config_get(sec, "args", args, sizeof args) < 0)
            args[0] = 0;
        if (glos_config_get(sec, "dir", dir, sizeof dir) < 0)
            dir[0] = 0;
        snprintf(cmd, sizeof cmd, "%s%s%s", items[i].what + 8, args[0] ? " " : "", args);
        files_run(cmd, dir);
    } else {
        files_open(items[i].what);
    }
}

static void area_draw(struct gui_widget *w, struct gfx_surface *s)
{
    int i;
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, 0x666666);     /* (the desktop's grey: Slate's) */
    for (i = 0; i < nitems; i++) {
        const struct item *it = &items[i];
        int tw = gfx_text_width(gui_font, it->name, -1), lw = tw < CELL_W - 4 ? tw : CELL_W - 4;
        int lx = it->x + (CELL_W - lw) / 2, ly = it->y + 36;
        gui_draw_icon(s, it->x + (CELL_W - 32) / 2, it->y, icon(it->kind, 1), 32);
        gfx_fill(s, lx - 2, ly - 1, lw + 4, gui_font->height + 2, i == chosen ? 0x333333 : 0xFFFFFF);
        gui_text_fit(s, lx, ly, CELL_W - 4, gui_font, it->name, i == chosen ? 0xFFFFFF : 0x000000);
        if (i == chosen && gui_is_focus(w)) {
            struct gfx_rect f = { lx - 2, ly - 1, lw + 4, gui_font->height + 2 };
            gui_draw_focus(s, &f);
        }
    }
}

static int item_at(int x, int y)
{
    int i;
    for (i = nitems - 1; i >= 0; i--)
        if (x >= items[i].x && x < items[i].x + CELL_W && y >= items[i].y && y < items[i].y + CELL_H - 8)
            return i;
    return -1;
}

static void choose_item(int i)
{
    if (i != chosen) {
        chosen = i;
        gui_damage(area);
    }
}

static int area_event(struct gui_widget *w, struct gui_ev *e)
{
    int i;
    switch (e->type) {
    case GUI_EV_PRESS:
        i = item_at(e->x - w->r.x, e->y - w->r.y);
        choose_item(i);
        if (i >= 0 && e->clicks == 2) {
            open_item(i);
            return 1;
        }
        if (i >= 0 && e->button == 1) {
            dragging = i;
            drag_dx = e->x - items[i].x, drag_dy = e->y - items[i].y;
            moved = 0;
        }
        return 1;
    case GUI_EV_MOVE:
        if (dragging >= 0 && (abs(e->x - drag_dx - items[dragging].x) > 3 || abs(e->y - drag_dy - items[dragging].y) > 3
                              || moved)) {
            gui_damage(w);
            items[dragging].x = e->x - drag_dx;
            items[dragging].y = e->y - drag_dy;
            moved = 1;
        }
        return 1;
    case GUI_EV_RELEASE:
        if (dragging >= 0 && moved) {
            items[dragging].x = (items[dragging].x + 4) / 8 * 8;      /* (on a grid of 8) */
            items[dragging].y = (items[dragging].y + 4) / 8 * 8;
            positions_save();
            say("moved", items[dragging].name);
        }
        dragging = -1;
        gui_damage(w);
        return 1;
    case GUI_EV_FOCUS:
    case GUI_EV_BLUR:
        gui_damage(w);
        return 1;
    case GUI_EV_KEY:
        break;
    default:
        return 0;
    }
    if (e->mods & (GLOS_MOD_ALT | GLOS_MOD_CTRL))
        return 0;
    switch (e->key) {
    case GUI_K_UP: case GUI_K_LEFT: choose_item(chosen > 0 ? chosen - 1 : nitems - 1); return 1;
    case GUI_K_DOWN: case GUI_K_RIGHT: choose_item(chosen + 1 < nitems ? chosen + 1 : 0); return 1;
    case GUI_K_HOME: choose_item(0); return 1;
    case GUI_K_END: choose_item(nitems - 1); return 1;
    case GUI_K_ENTER: case GUI_K_KPENTER: open_item(chosen); return 1;
    }
    if (e->ch > 0x20) {                         /* a name's first letter */
        int k;
        for (k = 1; k <= nitems; k++) {
            int j = (chosen + k) % nitems;
            if ((items[j].name[0] & ~0x20) == (int)(e->ch & ~0x20u)) {
                choose_item(j);
                break;
            }
        }
        return 1;
    }
    return 0;
}

/* ---- the launcher */

enum { L_APP = 100, L_PROGRAM = 200, L_RUN = 300 };
static char lnames[16][40];

static void build_launcher(void)
{
    char pt[16][40];
    int pk[16], n, i;
    launcher = gui_menu_new();
    for (i = 0; i < NAPPS; i++) {
        char t[48];
        snprintf(t, sizeof t, "&%s", apps[i].title);
        gui_menu_item(launcher, t, L_APP + i, NULL);
    }
    n = programs("menu", lnames, pt, pk, 16);
    if (n)
        gui_menu_sep(launcher);
    for (i = 0; i < n; i++)
        gui_menu_item(launcher, pt[i], L_PROGRAM + i, NULL);
    gui_menu_sep(launcher);
    gui_menu_item(launcher, "&Run...", L_RUN, NULL);
}

static void command(struct gui_window *w, int id)
{
    (void)w;
    if (id >= L_APP && id < L_APP + NAPPS) {
        run_app(apps[id - L_APP].exe, NULL);
    } else if (id >= L_PROGRAM && id < L_PROGRAM + 16) {
        char sec[48], args[200], dir[100], cmd[300];
        snprintf(sec, sizeof sec, "program %s", lnames[id - L_PROGRAM]);
        if (glos_config_get(sec, "args", args, sizeof args) < 0)
            args[0] = 0;
        if (glos_config_get(sec, "dir", dir, sizeof dir) < 0)
            dir[0] = 0;
        snprintf(cmd, sizeof cmd, "%s%s%s", lnames[id - L_PROGRAM], args[0] ? " " : "", args);
        files_run(cmd, dir);
    } else if (id == L_RUN) {
        char cmd[200] = "";
        if (gui_ask(desk, "Run", "A program, a folder or a file to open:", cmd, sizeof cmd) && cmd[0]) {
            struct gui_dirent e;
            if (gui_is_dir(cmd) || (!strchr(cmd, ' ') && gui_stat(cmd, &e) == 0))
                files_open(cmd);                /* (a folder or a file there: opened) */
            else
                files_run(cmd, NULL);
        }
    }
}

static void app_event(const struct glos_event *ev)
{
    switch (ev->type) {
    case GLOS_EV_LAUNCH:
        say("launcher", "");
        gui_popup_menu(desk, launcher, ev->x, ev->y);     /* (above the task bar: it goes up to fit) */
        break;
    case GLOS_EV_SYSKEY:
        say("tasks", "");
        run_app("MONITOR.EXE -tasks", NULL);
        break;
    case GLOS_EV_FILES:
        folder_changed(ev->key);
        break;
    }
}

static void desk_resized(struct gui_window *w)
{
    (void)w;
    gui_damage(area);
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    char *p;
    if (gui_init(argc, argv) != 0) {
        printf("FILES no GLOS\n");
        return 1;
    }
    snprintf(files_home, sizeof files_home, "%s", argv[0]);     /* (DJGPP gives it with / and its case) */
    for (p = files_home; *p; p++)
        *p = *p == '/' ? '\\' : (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
    if (strrchr(files_home, '\\'))
        *strrchr(files_home, '\\') = 0;
    else
        snprintf(files_home, sizeof files_home, "C:\\GLOS");
    gui_on_event(app_event);
    folders_load();
    if (!strcmp(mode, "-browse")) {
        browser_open(argc > 2 ? argv[2] : "C:\\");
    } else if (!strcmp(mode, "-folder") && argc > 2) {
        folder_open(argv[2]);
    } else {                                    /* the desktop's shell */
        desk = gui_window_at("Desktop", 0, 0, 1, 1, GUI_W_DESKTOP | GUI_W_BARE);
        desk->on_command = command;
        desk->on_resize = desk_resized;
        area = gui_canvas(desk->root, 0, 0, area_draw, area_event);
        gui_grow(area, 1);
        {
            struct glos_window g;
            memset(&g, 0, sizeof g);
            glos_screen(&g);
            desk->w = g.w, desk->h = g.h - 20;
        }
        gui_show(desk, 1);
        desktop_items();
        build_launcher();
        gui_focus(area);
        glos_shell(1);
        say("shell", files_home);
    }
    gui_run();
    folders_save();
    say("end", "");
    return 0;
}
