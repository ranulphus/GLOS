/* CONTROL: what its source files share. */
#ifndef CONTROL_H
#define CONTROL_H
#include <glos/gui.h>

/* look.c: the desktop's look beside the Desktop page (the wallpaper's picture, Choose..., Pick colour...) */
void look_add(struct gui_widget *side, struct gui_window *win, struct gui_widget *path, struct gui_widget *style,
              const char *const *values, struct gui_widget *start, struct gui_widget *colour, const char *argv0);
void look_poll(void);                   /* the picture follows the field and the list */

/* status.c: how things are now, under a page's settings */
void status_add_net(struct gui_widget *page);   /* the Network page's lines */
void status_add_pads(struct gui_widget *page);  /* the Joystick page's picture of the pads */
void status_add_input(struct gui_widget *page); /* the Input page's places to try the keys and the mouse */
void status_add_sound(struct gui_widget *page); /* the Sound page's Test sound */
void status_add_display(struct gui_widget *page);       /* the Display page's card and mode */
void status_add_services(struct gui_window *win, struct gui_widget *page, struct gui_widget *field);   /* its list */
void status_add_time(struct gui_widget *page);  /* the Date and Time page's clock */
void status_add_zone(struct gui_widget *page, struct gui_widget *field);        /* ... and its list of places */
void status_add_saver(struct gui_widget *tabs, int page_n, struct gui_widget *page, struct gui_widget *choice,
                      const char *const *values, const char *argv0);    /* the Saver page's saver, running small */
void status_add_secrets(struct gui_widget *page);        /* the Secrets page: the store's state, Unlock, the master password */
void status_poll(void);                 /* read again (the network once a second, the pads each time) */

/* devices.c: the Devices page (phase F): every device, what serves it, Turn off and Turn on */
void devices_add(struct gui_window *win, struct gui_widget *page, struct gui_widget *field, const char *argv0);
/* (2026-11-23) The Devices page's Settings: control.c's, the page of a section shown ("display", "sound" ...). */
void control_show_section(const char *section);
void devices_poll(void);                /* read again every two seconds; the chosen one's lines each time */

#endif
