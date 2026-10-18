/* PKG's window (M10 step E): what can be installed and what is, with
 * Install, Remove, Upgrade and Refresh; each message on the status line (and
 * on stdout, as the command has them). */
#include <stdio.h>
#include <string.h>

#include <glos/gui.h>

#include "pkg.h"

static struct gui_window *win;
static struct gui_widget *list, *status;
static const char *const heads[] = { "Name", "Version", "Installed", "From", "Summary" };
static const int widths[] = { 70, 70, 70, 70, 260 };

void pkg_status(const char *text)
{
    if (status) {
        gui_set_text(status, text);
        gui_step(0);                            /* (shown before the work, which waits on the network) */
    }
}

static void fill(void)
{
    int i, n = pkg_count();
    gui_list_clear(list);
    for (i = 0; i < n; i++) {
        const char *cells[5];
        char inst[32];
        pkg_row(i, cells, inst, sizeof inst);
        gui_list_add(list, cells, NULL, (void *)(size_t)i);
    }
}

static void act(struct gui_widget *w, int what)
{
    int r = gui_list_get(list), id = w->id;    /* (what: GUI_ON_CLICK; which button: its id) */
    char name[16] = "";
    if (what != GUI_ON_CLICK)
        return;
    if (r >= 0)
        snprintf(name, sizeof name, "%s", gui_list_cell(list, r, 0));
    switch (id) {
    case 0:
        if (name[0])
            pkg_install(name);
        break;
    case 1:
        if (name[0])
            pkg_remove(name);
        break;
    case 2:
        pkg_upgrade_all();
        break;
    case 3:
        pkg_status("Reading the sources...");
        pkg_reload();
        pkg_status("Ready");
        break;
    }
    fill();
    if (r >= 0 && r < gui_list_count(list))
        gui_list_set(list, r);
}

int pkg_window(int argc, char **argv)
{
    struct gui_widget *box, *row;
    if (gui_init(argc, argv) != 0) {
        printf("PKG no GLOS\n");
        return 1;
    }
    win = gui_window("Packages", 580, 300, 0);
    box = gui_vbox(win->root, -1, 6);
    gui_grow(box, 1);
    list = gui_list(box, 5, heads, widths);
    gui_grow(list, 1);
    row = gui_hbox(box, -1, 0);
    gui_button(row, "&Install", act, 0);
    gui_button(row, "&Remove", act, 1);
    gui_button(row, "&Upgrade All", act, 2);
    gui_space(row, 0, 0);
    gui_button(row, "Re&fresh", act, 3);
    status = gui_status(win);
    gui_show(win, 1);
    pkg_status("Reading the sources...");
    pkg_reload();
    fill();
    pkg_status("Ready");
    win->focus = list;
    gui_run();
    printf("PKG end\n");
    return 0;
}
