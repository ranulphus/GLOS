/* Commands (toolkit 2, step C2; docs/v1/toolkit-2.md 1): what an app can be asked to do, each set once for its
 * window: an id, its text ("&Save"), its shortcut ("Ctrl+S"), its icon, its tooltip, and whether it can be used
 * now and whether it is checked. Menus, toolbars and context menus show commands, so every place a command is
 * shown follows when it changes; the window's on_command is told the id, wherever it came from.
 *
 * The window's on_update, if it has one, is asked before a menu opens, before a shortcut is looked up and when a
 * toolbar is about to be drawn after the app has handled events: there the app says which commands can be used
 * now, instead of keeping every control's state by hand as things change. */
#include "guiint.h"

struct gui_cmd *gui_cmd_find(struct gui_window *w, int id)
{
    int i;
    for (i = 0; w && i < w->ncmds; i++)
        if (w->cmds[i].id == id)
            return &w->cmds[i];
    return NULL;
}

static void changed(struct gui_window *w)
{
    struct gui_widget *c;
    for (c = w->root ? w->root->first : NULL; c; c = c->next)   /* its bars show commands: drawn again */
        if (c->cls == &gui_menubar_class || c->cls == &gui_toolbar_class)
            gui_damage(c);
}

void gui_command(struct gui_window *w, int id, const char *text, const char *shortcut, const unsigned char *icon,
                 const char *tip)
{
    struct gui_cmd *c = gui_cmd_find(w, id);
    if (!c) {
        struct gui_cmd *n = realloc(w->cmds, (size_t)(w->ncmds + 1) * sizeof *n);
        if (!n)
            abort();
        w->cmds = n;
        c = &w->cmds[w->ncmds++];
        memset(c, 0, sizeof *c);
        c->id = id;
    }
    free(c->text), free(c->shortcut), free(c->tip);
    c->text = strdup(text ? text : "");
    c->shortcut = shortcut ? strdup(shortcut) : NULL;
    c->tip = tip ? strdup(tip) : NULL;
    c->icon = icon;
    c->key = c->mods = 0;
    if (shortcut && gui_shortcut_parse(shortcut, &c->key, &c->mods) != 0)
        c->key = 0;
    changed(w);
}

static void set(struct gui_window *w, int id, unsigned bit, int on)
{
    struct gui_cmd *c = gui_cmd_find(w, id);
    unsigned was;
    if (!c)
        return;
    was = c->state;
    c->state = on ? c->state | bit : c->state & ~bit;
    if (c->state != was)
        changed(w);
}

void gui_command_enable(struct gui_window *w, int id, int on) { set(w, id, GUI_CS_OFF, !on); }
void gui_command_check(struct gui_window *w, int id, int on) { set(w, id, GUI_CS_CHECKED, on); }

void gui_command_radio(struct gui_window *w, int id, int on)
{
    set(w, id, GUI_CS_RADIO, 1);
    set(w, id, GUI_CS_CHECKED, on);
}

unsigned gui_command_state(struct gui_window *w, int id)
{
    struct gui_cmd *c = gui_cmd_find(w, id);
    return c ? c->state : GUI_CS_OFF;
}

/* The app says which of its commands can be used now. */
void gui_cmd_update(struct gui_window *w)
{
    if (w && w->on_update && !w->closing)
        w->on_update(w);
}

/* The command a key is the shortcut of (one that can be used now), or NULL. */
struct gui_cmd *gui_cmd_key(struct gui_window *w, unsigned key, unsigned mods)
{
    int i;
    for (i = 0; i < w->ncmds; i++)
        if (w->cmds[i].key && w->cmds[i].key == key && w->cmds[i].mods == mods && !(w->cmds[i].state & GUI_CS_OFF))
            return &w->cmds[i];
    return NULL;
}

void gui_cmd_free(struct gui_window *w)
{
    int i;
    for (i = 0; i < w->ncmds; i++)
        free(w->cmds[i].text), free(w->cmds[i].shortcut), free(w->cmds[i].tip);
    free(w->cmds);
    w->cmds = NULL;
    w->ncmds = 0;
}
