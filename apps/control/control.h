/* CONTROL: what its source files share. */
#ifndef CONTROL_H
#define CONTROL_H
#include <glos/gui.h>

/* look.c: the desktop's look beside the Desktop page (the wallpaper's picture, and Choose...) */
void look_add(struct gui_widget *side, struct gui_window *win, struct gui_widget *path, struct gui_widget *style,
              const char *const *values);
void look_poll(void);                   /* the picture follows the field and the list */

#endif
