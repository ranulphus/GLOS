/* The preview surface (toolkit 2, step C8; docs/v1/toolkit-2.md, Widgets): another program's window shown inside
 * a widget (WIN's EMBED): the Control Panel's screen-saver previews, the IDE's running program.
 *
 * gui_preview_run starts the program (JOB's RUN, hidden), finds its window once it has one (SYS's PROGRAMS: the app
 * the job became; WIN's ENUM: its window) and has GLOS show it inside the widget's frame, as large as the widget,
 * again whenever the widget moves or changes size. Until then, and after the program has ended, the widget says
 * so. GUI_ON_CHANGE when the window is taken in and when the program ends. The program is ended
 * (gui_preview_stop) when the widget goes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

struct preview {
    struct gui_widget w;
    glos_handle job;
    unsigned app, win;
    struct gfx_rect at;                 /* where the window was put (the host's client area) */
    const char *note;
};

#define PV(w) ((struct preview *)(w))

static void pv_draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct preview *p = PV(w);
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_DARK);
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_LINE);
    if (!p->win && p->note)                     /* (the window, once it is in, is drawn by its own program) */
        gfx_text(s, w->r.x + (w->r.w - gfx_text_width(gui_font, p->note, -1)) / 2,
                 w->r.y + (w->r.h - gui_font->height) / 2, gui_font, p->note, -1, GUI_CHOSEN_TEXT);
}

static void put(struct preview *p)              /* the window at the widget's rectangle (inside its frame) */
{
    struct gfx_rect r = { p->w.r.x + 1, p->w.r.y + 1, p->w.r.w - 2, p->w.r.h - 2 };
    if (!p->win || !p->w.win || !p->w.win->id || r.w <= 0 || r.h <= 0)
        return;
    if (r.x == p->at.x && r.y == p->at.y && r.w == p->at.w && r.h == p->at.h)
        return;
    if (glos_embed(p->w.win->id, p->win, r.x, r.y, r.w, r.h) == 0)
        p->at = r;
}

static void ended(struct preview *p)
{
    if (p->job)
        glos_close(p->job);
    p->job = 0, p->app = p->win = 0;
    memset(&p->at, 0, sizeof p->at);
    p->note = "Ended";
    gui_timer(&p->w, 0);
    gui_damage(&p->w);
    gui_send(&p->w, GUI_ON_CHANGE);
}

static int pv_event(struct gui_widget *w, struct gui_ev *e)
{
    struct preview *p = PV(w);
    struct glos_job_status st;
    if (e->type != GUI_EV_TIMER || !p->job)
        return 0;
    memset(&st, 0, sizeof st);
    st.size = sizeof st;
    if (glos_job_status(p->job, &st) != 0 || st.state == GLOS_JS_DONE) {
        ended(p);
        return 1;
    }
    if (!p->win) {                              /* the window it has, once it has one */
        static struct glos_program progs[16];
        struct glos_win_info wins[4];
        unsigned n = 0, i;
        if (!p->app && st.id && glos_programs(progs, sizeof progs, &n) == 0)
            for (i = 0; i < n && i < 16; i++)
                if (progs[i].kind == GLOS_PROG_APP && progs[i].job == st.id)
                    p->app = progs[i].id;
        if (p->app && glos_windows(p->app, wins, 4, &n) == 0 && n >= 1) {
            p->win = wins[0].id;
            put(p);
            if (w->win && w->win->id)           /* (its program showed its window, which took the keyboard: the
                                                   host's again, until the user presses in the preview) */
                glos_focus(w->win->id);
            gui_damage(w);
            gui_send(w, GUI_ON_CHANGE);
            gui_timer(w, 1000);                 /* (its end watched) */
        }
    }
    return 1;
}

static void pv_layout(struct gui_widget *w) { put(PV(w)); }

static void pv_measure(struct gui_widget *w, int *pw, int *ph) { (void)w, *pw = 200, *ph = 150; }

static void pv_destroy(struct gui_widget *w) { gui_preview_stop(w); }

static const struct gui_class pv_class = { "preview", 0, pv_draw, pv_event, pv_measure, pv_layout, pv_destroy };

struct gui_widget *gui_preview(struct gui_widget *parent)
{
    struct preview *p = (struct preview *)gui_new(parent, &pv_class, sizeof *p);
    p->note = "No preview";
    return &p->w;
}

int gui_preview_run(struct gui_widget *w, const char *command)
{
    struct preview *p = PV(w);
    gui_preview_stop(w);
    p->job = glos_job_run(command, NULL, NULL, GLOS_RUN_HIDDEN, 0, NULL);
    if (!p->job) {
        p->note = "No preview";
        gui_damage(w);
        return glos_error() ? glos_error() : GLOS_E_REFUSED;
    }
    p->note = "Starting...";
    gui_timer(w, 200);
    gui_damage(w);
    return 0;
}

unsigned gui_preview_window(struct gui_widget *w) { return PV(w)->win; }

int gui_preview_info(struct gui_widget *w, struct glos_win_info *out)  /* the window taken in, as GLOS has it */
{
    struct preview *p = PV(w);
    struct glos_win_info wins[8];
    unsigned n = 0, i;
    if (p->app && p->win && glos_windows(p->app, wins, 8, &n) == 0)
        for (i = 0; i < n && i < 8; i++)
            if (wins[i].id == p->win) {
                *out = wins[i];
                return 0;
            }
    return GLOS_E_NOT_FOUND;
}

void gui_preview_stop(struct gui_widget *w)
{
    struct preview *p = PV(w);
    if (!p->job)
        return;
    if (p->win && p->w.win && p->w.win->id)     /* (let go first: its program is then ended) */
        glos_embed(p->w.win->id, p->win, 0, 0, 0, 0);
    glos_job_signal(p->job, GLOS_SIGNAL_END);
    ended(p);
}
