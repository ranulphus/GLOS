/* MONITOR: what GLOS is doing (PRD §10.6; docs/milestones-m9.md step G),
 * from GLOS_FN_STAT, again every second:
 *
 *   Programs      the apps, the jobs and the session in front: the processor's
 *                 time each has had, its memory and windows; End ends one
 *                 (GLOS_FN_KILL). Ctrl+Alt+Del's task list (MONITOR -tasks).
 *   Performance   the processor's use over the last minute, memory, the
 *                 network's frames
 *   System        GLOS, the machine, the screen, the sound, the network
 *   Log           GLOS's log's last lines
 *
 * "MONITOR ..." lines on stdout (the suite's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#define SAMPLES 60

static struct gui_window *win;
static struct gui_widget *tabs, *list, *graph, *mem_bar, *mem_text, *net_text, *log_ed, *status;
static struct gui_widget *sysval[12];
static struct glos_stat_system sys, last;
static int have_last, busy[SAMPLES], nbusy;
static unsigned char appbuf[4096];
static int napps;
static char seen[32][16];                       /* the programs it has said it saw (stdout, for the suite) */
static int nseen;

static const char *const kinds[] = { "App", "Job", "Session" };
static const char *const job_states[] = { "Waiting", "Running", "Full-screen", "App", "Done" };

static void line_text(char *b, size_t room, unsigned kb)
{
    if (kb >= 10240)
        snprintf(b, room, "%u MB", kb / 1024);
    else
        snprintf(b, room, "%u KB", kb);
}

static void fill_programs(void)
{
    int n, o, keep = gui_list_get(list);
    n = glos_stat(GLOS_STAT_APPS, appbuf, sizeof appbuf);
    gui_list_clear(list);
    napps = 0;
    for (o = 0; n > 0 && o + (int)sizeof(struct glos_stat_app) <= n; o += (int)sizeof(struct glos_stat_app)) {
        const struct glos_stat_app *a = (const struct glos_stat_app *)(appbuf + o);
        char id[12], cpu[16], mem[16], wins[8], state[16];
        const char *cells[7];
        if (a->kind == 1 && a->state == GLOS_JOB_APP)
            continue;                           /* (a job whose program is an app: the app's row is it) */
        snprintf(id, sizeof id, "%u", a->id);
        snprintf(cpu, sizeof cpu, "%u.%u s", a->busy_ms / 1000, a->busy_ms % 1000 / 100);
        line_text(mem, sizeof mem, a->mem_kb);
        snprintf(wins, sizeof wins, "%u", a->windows);
        snprintf(state, sizeof state, "%s", a->kind == 1 ? job_states[a->state < 5 ? a->state : 4]
                                            : a->state ? "Paused" : "Running");
        cells[0] = a->name, cells[1] = kinds[a->kind < 3 ? a->kind : 0], cells[2] = id, cells[3] = cpu;
        cells[4] = mem, cells[5] = wins, cells[6] = state;
        gui_list_add(list, cells, NULL, (void *)(size_t)o);
        napps++;
        {
            int k;
            for (k = 0; k < nseen && strcmp(seen[k], a->name); k++)
                ;
            if (k == nseen && nseen < 32) {
                snprintf(seen[nseen++], sizeof *seen, "%s", a->name);
                printf("MONITOR has %s\n", a->name);
                fflush(stdout);
            }
        }
    }
    if (keep >= 0 && keep < napps)
        gui_list_set(list, keep);
}

static void fill_system(void)
{
    char t[12][64];
    unsigned up = sys.uptime_ms / 1000;
    int i;
    snprintf(t[0], 64, "GLOS, its API %u.%u", sys.version >> 16, sys.version & 0xFFFF);
    snprintf(t[1], 64, "%u:%02u:%02u", up / 3600, up / 60 % 60, up % 60);
    snprintf(t[2], 64, "%s", sys.cpu_family == 4 ? "486" : sys.cpu_family == 5 ? "Pentium" : "Pentium Pro or later");
    snprintf(t[3], 64, "%u KB, %u KB free", sys.mem_kb, sys.mem_free_kb);
    snprintf(t[4], 64, "%u threads, %u apps, %u windows", sys.threads, sys.apps, sys.windows);
    if (sys.screen_w)
        snprintf(t[5], 64, "%ux%u, %u bits, %s%s", sys.screen_w, sys.screen_h, sys.screen_bpp, sys.card,
                 sys.accel ? " (its engine)" : "");
    else
        snprintf(t[5], 64, "text mode");
    if (sys.vram_kb)
        snprintf(t[6], 64, "%u KB", sys.vram_kb);
    else
        snprintf(t[6], 64, "-");
    if (sys.mixer_rate)
        snprintf(t[7], 64, "%u Hz, %u streams, %u underruns", sys.mixer_rate, sys.streams, sys.underruns);
    else
        snprintf(t[7], 64, "off");
    if (sys.nic[0])
        snprintf(t[8], 64, "%s, %u.%u.%u.%u", sys.nic, sys.ip[0], sys.ip[1], sys.ip[2], sys.ip[3]);
    else
        snprintf(t[8], 64, "none");
    snprintf(t[9], 64, "%s", sys.hostname);
    snprintf(t[10], 64, "%s", sys.session[0] ? sys.session : "none");
    for (i = 0; i < 11; i++)
        gui_set_text(sysval[i], t[i]);
}

static void graph_draw(struct gui_widget *w, struct gfx_surface *s)
{
    int i, gx = w->r.x, gy = w->r.y, gw = w->r.w, gh = w->r.h;
    gfx_fill(s, gx, gy, gw, gh, 0x000000);
    for (i = 1; i < 4; i++)
        gfx_fill(s, gx, gy + gh * i / 4, gw, 1, 0x333333);
    for (i = 0; i < nbusy; i++) {
        int x = gx + gw - (nbusy - i) * gw / SAMPLES, h = busy[i] * (gh - 2) / 100;
        gfx_fill(s, x, gy + gh - 1 - h, gw / SAMPLES > 1 ? gw / SAMPLES - 1 : 1, h, 0x33CC33);
    }
    gfx_frame(s, gx, gy, gw, gh, GUI_LINE);
}

static void sample(void *arg)
{
    char t[96];
    (void)arg;
    memset(&sys, 0, sizeof sys);
    if (glos_stat(GLOS_STAT_SYSTEM, &sys, sizeof sys) < 0)
        return;
    if (have_last) {
        unsigned b = sys.busy_ms - last.busy_ms, i = sys.idle_ms - last.idle_ms;
        int pct = b + i ? (int)(b * 100 / (b + i)) : 0;
        if (nbusy == SAMPLES)
            memmove(busy, busy + 1, (SAMPLES - 1) * sizeof *busy);
        else
            nbusy++;
        busy[nbusy - 1] = pct;
        gui_damage(graph);
        snprintf(t, sizeof t, "Processor %d%%   Memory %u KB of %u KB used", pct, sys.mem_kb - sys.mem_free_kb, sys.mem_kb);
        gui_set_text(status, t);
        snprintf(t, sizeof t, "%u frames in, %u out a second", sys.net_rx - last.net_rx, sys.net_tx - last.net_tx);
        gui_set_text(net_text, t);
    }
    last = sys;
    have_last = 1;
    gui_progress_set(mem_bar, (int)((sys.mem_kb - sys.mem_free_kb) / 64), (int)(sys.mem_kb / 64 ? sys.mem_kb / 64 : 1));
    {
        char used[16], all[16];
        line_text(used, sizeof used, sys.mem_kb - sys.mem_free_kb);
        line_text(all, sizeof all, sys.mem_kb);
        snprintf(t, sizeof t, "%s of %s", used, all);
        gui_set_text(mem_text, t);
    }
    fill_system();
    if (gui_tabs_get(tabs) == 0)
        fill_programs();
}

static void end_task(struct gui_widget *w, int what)
{
    int r = gui_list_get(list);
    struct glos_stat_app a;                     /* (a copy: the list is filled again while the question is up) */
    char q[120];
    (void)w, (void)what;
    if (r < 0)
        return;
    memcpy(&a, appbuf + (size_t)gui_list_user(list, r), sizeof a);
    snprintf(q, sizeof q, "End %s? What it hasn't saved is lost.", a.name);
    if (gui_message(win, "End Program", q, GUI_MB_YESNO) != GUI_YES)
        return;
    if (glos_kill(a.kind == 1 ? 1 : a.kind == 2 ? 2 : 0, a.id) != 0)
        gui_message(win, "End Program", "It couldn't be ended.", GUI_MB_OK);
    else
        printf("MONITOR ended %s\n", a.name);
    fflush(stdout);
    fill_programs();
}

static void refresh_log(struct gui_widget *w, int what)
{
    static char buf[2048];
    int n = glos_stat(GLOS_STAT_LOG, buf, sizeof buf);
    (void)w, (void)what;
    gui_editor_set(log_ed, buf, n > 0 ? (size_t)n : 0);
    gui_editor_command(log_ed, GUI_CMD_END);
}

static void tab_changed(struct gui_widget *w, int what)
{
    (void)what;
    if (gui_tabs_get(w) == 0)
        fill_programs();
    if (gui_tabs_get(w) == 3)
        refresh_log(NULL, 0);
}

int main(int argc, char **argv)
{
    static const char *const heads[7] = { "Program", "Kind", "Id", "Processor", "Memory", "Windows", "State" };
    static const int widths[7] = { 100, 44, 30, 70, 64, 62, 80 };
    static const char *const labels[11] = { "Version:", "Running for:", "Processor:", "Memory:", "Work:", "Screen:",
                                            "Card's memory:", "Sound:", "Network:", "Host name:", "In front:" };
    struct gui_widget *page, *row, *form, *box;
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("MONITOR no GLOS\n");
        return 1;
    }
    win = gui_window("Monitor", 500, 330, 0);
    tabs = gui_tabs(gui_vbox(win->root, -1, 6));
    gui_grow(tabs, 1);
    gui_grow(tabs->parent, 1);
    gui_on(tabs, tab_changed, 0);

    page = gui_tab(tabs, "Programs");
    list = gui_list(page, 7, heads, widths);
    row = gui_hbox(page, -1, 0);
    gui_space(row, 0, 0);
    gui_button(row, "&End Program", end_task, 0);

    page = gui_tab(tabs, "Performance");
    gui_label(page, "Processor's use, the last minute:");
    graph = gui_canvas(page, 0, 100, graph_draw, NULL);
    gui_grow(graph, 1);
    form = gui_form(page);
    row = gui_row(form, "Memory:", NULL);
    mem_bar = gui_progress(row, 100);
    mem_text = gui_label(row, "");
    gui_grow(mem_text, 1);
    net_text = gui_label(gui_row(form, "Network:", NULL), "");

    page = gui_tab(tabs, "System");
    form = gui_form(page);
    for (i = 0; i < 11; i++)
        sysval[i] = gui_label(gui_row(form, labels[i], NULL), "");

    page = gui_tab(tabs, "Log");
    log_ed = gui_editor(page);
    gui_editor_font(log_ed, gui_mono);
    box = gui_hbox(page, -1, 0);
    gui_space(box, 0, 0);
    gui_button(box, "&Refresh", refresh_log, 0);

    status = gui_status(win);
    if (argc > 1 && !strcmp(argv[1], "-tasks"))
        gui_title(win, "Monitor: Programs");
    sample(NULL);
    gui_show(win, 1);
    gui_idle(sample, NULL, 1000);
    gui_run();
    printf("MONITOR end\n");
    return 0;
}
