/* CONTROL: how things are now, under a page's settings (phase D, docs/v1/phase-d.md D5).
 *
 * The Network page: each interface's card and hardware address; its address, mask and where it came from (DHCP,
 * with what is left of its lease); the gateway and the name servers; GLOS's name; the frames received and sent.
 * Read with NET's STATE once a second while the Control Panel is open.
 *
 * The Joystick page: the pads as they are (PADS, ten times a second): for each of the two joysticks a DOS program
 * sees, its stick's place in a square (and the right stick's, if the pad has one) and its first ten buttons,
 * lit while they are down. A pad's test before a game is started.
 *
 * The Input page: a field to try the keys' repeat in, and a place that counts the double clicks made on it (by
 * the double-click time this program started with: the setting is an app's at its start).
 *
 * (The USB page's tree of controllers and devices is the Devices page's since phase F: devices.c.)
 *
 * The Saver page: the saver its list has chosen, running small in the page: its own program, started with /P and
 * the picture's size, its window taken into the page (the toolkit's gui_preview). Only while the page is the one
 * shown; the blank saver has nothing to show. Where the program can't draw (DOS-GL has no window on this card or
 * at this depth) it ends at once, and the picture says "Ended".
 *
 * The Date and Time page: a list of places whose choice writes the Time zone setting (the setting stays the POSIX
 * string it is: the list is a way to write it, and shows the place whose string the field has); the clock as it
 * is now (local time, and how it was last set), a date and a time to set
 * it to by hand, and the time server asked at once. Both are the shell's CLOCK, run for the page.
 *
 * The Services page: the programs of its one setting ([services] run: command lines with ; between them) as a
 * list, one a row; Add... chooses a program with the file dialog and puts it at the line's end, Remove takes the
 * chosen row out of the line. The line stays the setting: the list is what it says, read again when it changes.
 *
 * The Display page: which card it is, its memory, whether its engine draws the desktop, and the screen's mode now
 * (SYS's STAT, every two seconds).
 *
 * The Sound page: Test sound plays two notes through the mixer at the volume that is set, and says so, or that
 * there is no card GLOS can play through. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"

#define LINES 4

static struct gui_widget *net[LINES];
static char net_seen[LINES][96];

static const char *ip(unsigned a, char *buf)
{
    sprintf(buf, "%u.%u.%u.%u", a & 255, a >> 8 & 255, a >> 16 & 255, a >> 24);
    return buf;
}

static void put(int i, const char *text)
{
    if (strcmp(net_seen[i], text)) {            /* (set only when it is another: nothing drawn for the same) */
        snprintf(net_seen[i], sizeof net_seen[i], "%s", text);
        gui_set_text(net[i], text);
    }
}

/* ---- the pads */

#define PAD_W 220
#define PAD_H 44

static struct gui_widget *pads_view;
static struct glos_pads pads;

static void stick(struct gfx_surface *s, int x, int y, int ax, int ay)
{
    gfx_fill(s, x, y, 40, 40, GUI_ROLE(GLOS_ROLE_FIELD));
    gfx_frame(s, x, y, 40, 40, GUI_ROLE(GLOS_ROLE_LINE));
    gfx_fill(s, x + 18 + ax * 17 / 127, y + 18 + ay * 17 / 127, 4, 4, GUI_ROLE(GLOS_ROLE_MARK));
}

static void pads_draw(struct gui_widget *w, struct gfx_surface *s)
{
    int n, b;
    for (n = 0; n < GLOS_PADS; n++) {
        const struct glos_pad *p = &pads.pad[n];
        int x = w->r.x + n * (PAD_W + 10), y = w->r.y;
        gui_draw_text(s, x, y + 14, gui_font, n ? GUI_T("B:") : GUI_T("A:"), GUI_ROLE(GLOS_ROLE_FACE_TEXT));
        if (!p->present) {
            gui_draw_text(s, x + 20, y + 14, gui_font, GUI_T("no pad"), GUI_ROLE(GLOS_ROLE_DIM));
            continue;
        }
        stick(s, x + 20, y, p->lx, p->ly);
        if (p->flags & GLOS_PAD_RIGHT)
            stick(s, x + 66, y, p->rx, p->ry);
        for (b = 0; b < 10; b++) {              /* (two rows of five) */
            int bx = x + 114 + b % 5 * 16, by = y + 4 + b / 5 * 18;
            gfx_fill(s, bx, by, 12, 12, GUI_ROLE(p->buttons >> b & 1 ? GLOS_ROLE_MARK : GLOS_ROLE_FIELD));
            gfx_frame(s, bx, by, 12, 12, GUI_ROLE(GLOS_ROLE_LINE));
        }
    }
}

static void pads_poll(void)
{
    struct glos_pads now;
    if (!pads_view)
        return;
    memset(&now, 0, sizeof now);
    now.size = sizeof now;
    if (glos_pads(&now, 0) != 0)
        memset(&now, 0, sizeof now);
    now.size = pads.size, now.flags = pads.flags;       /* (what is compared: the pads alone) */
    if (memcmp(&now, &pads, sizeof now)) {
        pads = now;
        gui_damage(pads_view);
    }
}

/* The Joystick page's picture of the pads, under its settings. */
void status_add_pads(struct gui_widget *page)
{
    gui_label(page, GUI_T("The pads now (move a stick, press a button):"));
    pads_view = gui_canvas(page, 2 * PAD_W + 10, PAD_H, pads_draw, NULL);
}

/* ---- the keys and the mouse, tried */

static int doubles;

static void try_draw(struct gui_widget *w, struct gfx_surface *s)
{
    char t[40];
    if (doubles)
        snprintf(t, sizeof t, GUI_T("Double clicks: %d"), doubles);
    else
        snprintf(t, sizeof t, "%s", GUI_T("Double-click here"));
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_ROLE(GLOS_ROLE_FIELD));
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_ROLE(GLOS_ROLE_LINE));
    gui_draw_text(s, w->r.x + 8, w->r.y + (w->r.h - gui_font->height) / 2, gui_font, t, GUI_ROLE(GLOS_ROLE_FIELD_TEXT));
}

static int try_event(struct gui_widget *w, struct gui_ev *e)
{
    if (e->type != GUI_EV_PRESS)
        return 0;
    if (e->clicks == 2) {
        doubles++;
        gui_damage(w);
    }
    return 1;
}

/* The Input page's places to try the keys and the mouse, under its settings. */
void status_add_input(struct gui_widget *page)
{
    struct gui_widget *row;
    gui_label(page, GUI_T("Try the keys and the mouse as they are now:"));
    row = gui_hbox(page, 10, 0);
    gui_field(row, 22);
    gui_canvas(row, 170, gui_font->height + 10, try_draw, try_event);
}

/* ---- a sound, to hear that there is one */

static struct gui_widget *sound_says;
static glos_handle sound;
static unsigned sound_until;

/* A sine's value (-32767 to 32767) at phase p of 65536, by a parabola a side: near enough for two notes. */
static int wave(unsigned p)
{
    int x = (int)(p & 32767) - 16384;           /* -16384 to 16383 over half a turn */
    int y = 32767 - (int)((long)x * x / 8192);
    return p & 32768 ? -y : y;
}

static void sound_test(struct gui_widget *b, int what)
{
    static short note[2 * 5512];                /* half a second at 11025 Hz: two notes */
    struct glos_audio_ring *ring = NULL;
    unsigned i, phase = 0;
    (void)b, (void)what;
    if (sound)
        return;                                 /* (the last one is still playing) */
    if (!(sound = glos_audio_out(11025, 1, 32768, &ring, NULL)) || !ring) {
        gui_set_text(sound_says, glos_error() == GLOS_E_NO_AUDIO ? GUI_T("No sound card that GLOS can play through.")
                                                                 : GUI_T("GLOS couldn't play it."));
        if (sound)
            glos_close(sound);
        sound = 0;
        return;
    }
    for (i = 0; i < 2 * 5512; i++) {            /* 660 Hz, then 880 Hz, each fading out */
        unsigned k = i % 5512;
        phase += (i < 5512 ? 660u : 880u) * 65536u / 11025u;
        note[i] = (short)(wave(phase) / 4 * (int)(5512 - k) / 5512);
    }
    glos_audio_write(ring, note, sizeof note);
    sound_until = glos_ticks() + 1500;
    gui_set_text(sound_says, GUI_T("Two notes, at the volume that is set."));
}

static void sound_poll(void)
{
    if (sound && (int)(glos_ticks() - sound_until) >= 0) {      /* played out: its stream closed */
        glos_close(sound);
        sound = 0;
    }
}

/* The Sound page's test, under its settings. */
void status_add_sound(struct gui_widget *page)
{
    struct gui_widget *row = gui_hbox(page, 10, 0);
    gui_button(row, GUI_T("&Test sound"), sound_test, 0);
    sound_says = gui_label(row, "");
    gui_grow(sound_says, 1);
}

/* ---- the saver, running small */

#define SV_W 192
#define SV_H 144

static struct gui_widget *sv_view, *sv_choice, *sv_tabs;
static const char *const *sv_names;
static char sv_home[80], sv_seen[16];
static int sv_page;

static void saver_poll(void)
{
    const char *name;
    char cmd[160], up[16];
    int v, i;
    if (!sv_view)
        return;
    v = gui_value(sv_choice);
    name = gui_tabs_get(sv_tabs) == sv_page && v >= 0 && sv_names[v] ? sv_names[v] : "";    /* (none on another page) */
    if (!strcmp(name, sv_seen))
        return;
    snprintf(sv_seen, sizeof sv_seen, "%s", name);
    gui_preview_stop(sv_view);
    if (!name[0] || !strcmp(name, "blank"))
        return;
    for (i = 0; name[i] && i < (int)sizeof up - 1; i++)
        up[i] = (char)(name[i] >= 'a' && name[i] <= 'z' ? name[i] - 32 : name[i]);
    up[i] = 0;
    snprintf(cmd, sizeof cmd, "%s%s.SCR /P %d %d", sv_home, up, SV_W, SV_H);
    gui_preview_run(sv_view, cmd);
}

/* The Saver page's picture, under its settings: for the saver's list (whose values are `values`, NULL-ended); the
   savers' programs are beside this one (argv0). */
void status_add_saver(struct gui_widget *tabs, int page_n, struct gui_widget *page, struct gui_widget *choice,
                      const char *const *values, const char *argv0)
{
    struct gui_widget *row;
    const char *p, *slash = NULL;
    int i;
    for (p = argv0; *p; p++)
        if (*p == '\\' || *p == '/' || *p == ':')
            slash = p;
    snprintf(sv_home, sizeof sv_home, "%.*s", slash ? (int)(slash - argv0 + 1) : 0, argv0);
    for (i = 0; sv_home[i]; i++)                /* (as DOS writes a path: DJGPP's argv[0] has / for \, and a / in a
                                                   command is where its switches begin) */
        if (sv_home[i] == '/')
            sv_home[i] = '\\';
    sv_tabs = tabs;
    sv_page = page_n;
    sv_choice = choice;
    sv_names = values;
    row = gui_hbox(page, 0, 0);                 /* (its own size and no wider: the saver's picture is that size) */
    sv_view = gui_preview(row);
    gui_size(sv_view, SV_W + 2, SV_H + 2);
    gui_space(row, 0, 0);
}

/* ---- the time zone, by place */

static const struct { const char *place, *tz; } zones[] = {
    { GUI_N("(as written below)"), "" },
    { GUI_N("UTC"), "UTC0" },
    { GUI_N("Hawaii"), "HST10" },
    { GUI_N("Alaska"), "AKST9AKDT,M3.2.0,M11.1.0" },
    { GUI_N("Pacific (Los Angeles, Vancouver)"), "PST8PDT,M3.2.0,M11.1.0" },
    { GUI_N("Mountain (Denver, Calgary)"), "MST7MDT,M3.2.0,M11.1.0" },
    { GUI_N("Arizona"), "MST7" },
    { GUI_N("Central (Chicago, Mexico City)"), "CST6CDT,M3.2.0,M11.1.0" },
    { GUI_N("Eastern (New York, Toronto)"), "EST5EDT,M3.2.0,M11.1.0" },
    { GUI_N("Atlantic (Halifax)"), "AST4ADT,M3.2.0,M11.1.0" },
    { GUI_N("Newfoundland"), "NST3:30NDT,M3.2.0,M11.1.0" },
    { GUI_N("Brasilia, Buenos Aires"), "<-03>3" },
    { GUI_N("Azores"), "<-01>1<+00>,M3.5.0/0,M10.5.0/1" },
    { GUI_N("Britain, Ireland, Portugal"), "GMT0BST,M3.5.0/1,M10.5.0" },
    { GUI_N("Central Europe (Berlin, Paris, Rome)"), "CET-1CEST,M3.5.0,M10.5.0/3" },
    { GUI_N("Eastern Europe (Athens, Helsinki)"), "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { GUI_N("South Africa"), "SAST-2" },
    { GUI_N("Moscow, Istanbul, Riyadh"), "<+03>-3" },
    { GUI_N("Gulf (Dubai)"), "<+04>-4" },
    { GUI_N("Pakistan"), "PKT-5" },
    { GUI_N("India"), "IST-5:30" },
    { GUI_N("Bangladesh"), "<+06>-6" },
    { GUI_N("Thailand, Vietnam, Jakarta"), "<+07>-7" },
    { GUI_N("China, Singapore, Perth"), "<+08>-8" },
    { GUI_N("Japan, Korea"), "JST-9" },
    { GUI_N("Adelaide"), "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
    { GUI_N("Brisbane"), "AEST-10" },
    { GUI_N("Sydney, Melbourne"), "AEST-10AEDT,M10.1.0,M4.1.0/3" },
    { GUI_N("New Zealand"), "NZST-12NZDT,M9.5.0,M4.1.0/3" },
};
#define NZONES ((int)(sizeof zones / sizeof zones[0]))

static struct gui_widget *zone_list, *zone_field;
static char zone_seen[64] = "\1";

/* A place chosen: its string written in the setting's field. */
static void zone_chosen(struct gui_widget *w, int what)
{
    int v = gui_value(w);
    if (what == GUI_ON_SELECT && v > 0 && v < NZONES) {
        gui_set_text(zone_field, zones[v].tz);
        snprintf(zone_seen, sizeof zone_seen, "%s", zones[v].tz);
    }
}

/* The list shows the place whose string the field has (the first row for any other). */
static void zone_poll(void)
{
    const char *t;
    int i;
    if (!zone_list || !strcmp(t = gui_get_text(zone_field), zone_seen))
        return;
    snprintf(zone_seen, sizeof zone_seen, "%s", t);
    for (i = NZONES - 1; i > 0 && strcmp(zones[i].tz, t); i--) ;
    gui_set_value(zone_list, i);
}

/* The Date and Time page's list of places, for its Time zone setting's field. */
void status_add_zone(struct gui_widget *page, struct gui_widget *field)
{
    struct gui_widget *row = gui_hbox(page, 8, 0);
    int i;
    zone_field = field;
    gui_label(row, GUI_T("Time zone of:"));
    zone_list = gui_choice(row, 30);
    for (i = 0; i < NZONES; i++)
        gui_choice_add(zone_list, GUI_T(zones[i].place));
    gui_on(zone_list, zone_chosen, 0);
}

/* ---- the clock */

static struct gui_widget *clock_now, *clock_date, *clock_time, *clock_says;
static unsigned clock_seen;

/* Seconds since 1970 as a date (yyyymmdd) and a time (hhmmss). */
static void civil(unsigned t, int *date, int *hms)
{
    unsigned days = t / 86400, s = t % 86400;
    int z = (int)days + 719468, era = z / 146097, doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, y = yoe + era * 400;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9;
    *date = (y + (m <= 2)) * 10000 + m * 100 + d;
    *hms = (int)(s / 3600) * 10000 + (int)(s / 60 % 60) * 100 + (int)(s % 60);
}

static unsigned local_now(unsigned *how)
{
    struct glos_time t;
    memset(&t, 0, sizeof t);
    t.size = sizeof t;
    if (glos_time(&t) != 0)
        return 0;
    if (how)
        *how = t.how;
    return (unsigned)((int)t.utc + t.offset);
}

static void clock_poll(void)
{
    static const char *const hows[] = { GUI_N("the PC's own clock"), GUI_N("set from the network"), GUI_N("set by hand") };
    char text[96], day[24];
    unsigned how = 0, now;
    int date, hms;
    if (!clock_now || (now = local_now(&how)) == clock_seen)
        return;
    clock_seen = now;
    civil(now, &date, &hms);
    gui_date_text(date, day, sizeof day);
    snprintf(text, sizeof text, "%s  %02d:%02d:%02d  (%s)", day, hms / 10000, hms / 100 % 100, hms % 100,
             GUI_T(hows[how < 3 ? how : 0]));
    gui_set_text(clock_now, text);
}

/* Set the clock (1): the date and time in the fields, local time; Ask the server now (2). The shell's CLOCK. */
static void clock_ask(struct gui_widget *b, int what)
{
    static glos_handle job;
    char line[64];
    int d = gui_datetime_get(clock_date), t = gui_datetime_get(clock_time);
    (void)what;
    if (job)
        glos_close(job);
    if (b->id == 1)
        snprintf(line, sizeof line, "CLOCK set %04d-%02d-%02dT%02d:%02d:%02d", d / 10000, d / 100 % 100, d % 100, t / 10000,
                 t / 100 % 100, t % 100);
    else
        snprintf(line, sizeof line, "CLOCK sync");
    job = glos_job_run(line, NULL, NULL, GLOS_RUN_SHELL, 0, NULL);
    gui_set_text(clock_says, !job ? GUI_T("GLOS couldn't do it.") : b->id == 1 ? GUI_T("The clock was set.")
                                  : GUI_T("The time server was asked."));
    printf("CONTROL clock %s\n", b->id == 1 ? line + 10 : "sync");
    fflush(stdout);
    clock_seen = 0;
}

/* The Date and Time page's clock, under its settings. */
void status_add_time(struct gui_widget *page)
{
    struct gui_widget *row;
    int date = 0, hms = 0;
    row = gui_hbox(page, 8, 0);
    gui_label(row, GUI_T("Now:"));
    clock_now = gui_label(row, "");
    gui_grow(clock_now, 1);
    civil(local_now(NULL), &date, &hms);
    row = gui_hbox(page, 8, 0);
    clock_date = gui_date_field(row, date);
    clock_time = gui_time_field(row, hms, 1);
    gui_button(row, GUI_T("S&et the clock"), clock_ask, 1);
    gui_button(row, GUI_T("As&k the server now"), clock_ask, 2);
    clock_says = gui_label(page, "");
}

/* ---- [services] run, as a list */

#define SL_MAX 8

static struct gui_window *sl_win;
static struct gui_widget *sl_field, *sl_list;
static char sl_seen[256] = "\1";                /* (nothing seen: the first look fills it) */

/* The line's entry k (between the ;, the spaces round it gone): 1 if there is one. */
static int sl_entry(const char *line, int k, char *out, size_t room)
{
    const char *p = line;
    for (;; k--) {
        size_t n = 0;
        while (*p == ' ' || *p == ';')
            p++;
        if (!*p)
            return 0;
        for (; *p && *p != ';'; p++)
            if (n < room - 1)
                out[n++] = *p;
        while (n && out[n - 1] == ' ')
            n--;
        out[n] = 0;
        if (!k)
            return 1;
    }
}

static void sl_poll(void)
{
    const char *line;
    char one[160];
    int k, keep;
    if (!sl_field || !strcmp(line = gui_get_text(sl_field), sl_seen))
        return;
    snprintf(sl_seen, sizeof sl_seen, "%s", line);
    keep = gui_list_get(sl_list);
    gui_list_clear(sl_list);
    for (k = 0; k < SL_MAX && sl_entry(sl_seen, k, one, sizeof one); k++) {
        const char *cells[1] = { one };
        gui_list_add(sl_list, cells, NULL, NULL);
    }
    if (k)
        gui_list_set(sl_list, keep >= 0 && keep < k ? keep : 0);
}

/* Add... (1): a program chosen, at the line's end. Remove (2): the chosen row out of the line. */
static void sl_ask(struct gui_widget *b, int what)
{
    static const char *const filters[] = { "Programs|*.EXE", "Every file|*.*", NULL };
    char line[256], one[160], path[160];
    int k, row = gui_list_get(sl_list);
    size_t n = 0;
    (void)what;
    line[0] = 0;
    for (k = 0; k < SL_MAX && sl_entry(gui_get_text(sl_field), k, one, sizeof one); k++)
        if (b->id == 1 || k != row)
            n += (size_t)snprintf(line + n, sizeof line - n, "%s%s", n ? "; " : "", one);
    if (b->id == 1) {
        struct gui_file_opts o;
        memset(&o, 0, sizeof o);
        o.filters = filters;
        path[0] = 0;
        if (gui_file_dialog_ex(sl_win, GUI_T("Background App"), 0, path, sizeof path, &o) != 1 || !path[0])
            return;
        if (n + strlen(path) + 3 > sizeof line || k == SL_MAX) {
            gui_message(sl_win, GUI_T("Services"), GUI_T("There is no room for another."), GUI_MB_OK);
            return;
        }
        snprintf(line + n, sizeof line - n, "%s%s", n ? "; " : "", path);
    }
    gui_set_text(sl_field, line);
    sl_poll();
}

/* The Services page's list and buttons, under its setting's field. */
void status_add_services(struct gui_window *win, struct gui_widget *page, struct gui_widget *field)
{
    struct gui_widget *row;
    sl_win = win;
    sl_field = field;
    gui_label(page, GUI_T("GLOS starts these with itself, and again if they end:"));
    sl_list = gui_list(page, 1, NULL, NULL);
    gui_grow(sl_list, 1);
    row = gui_hbox(page, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("&Add..."), sl_ask, 1);
    gui_button(row, GUI_T("&Remove"), sl_ask, 2);
}

/* ---- the card and the screen */

static struct gui_widget *card_line, *mode_line;
static char card_seen[96], mode_seen[96];

static void display_poll(void)
{
    static unsigned last;
    struct glos_sys_system sys;
    struct glos_sys_memory mem;
    char a[96], b[96], kb[24];
    unsigned now = glos_ticks();
    if (!card_line || (last && now - last < 2000))
        return;
    last = now ? now : 1;
    memset(&sys, 0, sizeof sys);
    memset(&mem, 0, sizeof mem);
    if (glos_sys_stat(GLOS_STAT_SYSTEM, &sys, sizeof sys, 0, 0) != 0 || glos_sys_stat(GLOS_STAT_MEMORY, &mem, sizeof mem, 0, 0) != 0)
        return;
    if (mem.vram_kb >= 1024)
        snprintf(kb, sizeof kb, GUI_T(", %u MB"), mem.vram_kb / 1024);
    else if (mem.vram_kb)
        snprintf(kb, sizeof kb, GUI_T(", %u KB"), mem.vram_kb);
    else
        kb[0] = 0;
    snprintf(a, sizeof a, GUI_T("Card: %s%s%s"), sys.card[0] ? sys.card : GUI_T("none GLOS can draw on"), kb,
             sys.accel ? GUI_T(" (its engine draws the desktop)") : "");
    if (sys.screen_w) {
        /* (the refresh the card was set to, from glos display's line: "refresh=75.02", or "refresh=bios" for the
           BIOS's own timings; G2, 2026-11-25) */
        char t[512], hz[24] = "";
        const char *r;
        unsigned len = 0;
        if (glos_sys_stat(GLOS_STAT_DISPLAY, t, sizeof t - 1, &len, 0) == 0 && len < sizeof t) {
            t[len] = 0;
            if ((r = strstr(t, " refresh=")) != NULL && strncmp(r + 9, "bios", 4)) {
                size_t k = strcspn(r + 9, " \r\n");
                snprintf(hz, sizeof hz, GUI_T(", %.*s Hz"), (int)(k < 8 ? k : 8), r + 9);
            }
        }
        snprintf(b, sizeof b, GUI_T("Now: %u by %u, %u bits%s"), sys.screen_w, sys.screen_h, sys.screen_bpp, hz);
    } else
        snprintf(b, sizeof b, "%s", GUI_T("Now: text mode"));
    if (strcmp(a, card_seen)) {
        strcpy(card_seen, a);
        gui_set_text(card_line, a);
    }
    if (strcmp(b, mode_seen)) {
        strcpy(mode_seen, b);
        gui_set_text(mode_line, b);
    }
}

/* The Display page's card and mode, under its settings. */
void status_add_display(struct gui_widget *page)
{
    card_line = gui_label(page, "");
    mode_line = gui_label(page, "");
}

/* ---- the network */

/* ---- the Secrets page (phase D): not GLOS.CFG's, the secrets store itself: how it is, and the two things the
   user does to it, each in a dialog of GLOS's own (SECRETS' STORE): no password passes through this program. */

static struct gui_widget *sec_line, *sec_unlock, *sec_change;
static unsigned sec_state = 99, sec_has = 99;

static void secrets_poll(void)
{
    unsigned state = GLOS_STORE_NONE, has = 0;
    const char *t;
    if (!sec_line)
        return;
    if (glos_secrets_store(GLOS_STORE_STATE, &state, &has) != 0)
        state = GLOS_STORE_NONE;
    if (state == sec_state && has == sec_has)
        return;
    sec_state = state, sec_has = has;
    t = state == GLOS_STORE_OPEN ? (has ? GUI_T("The store is open. It has a master password.")
                                        : GUI_T("The store is open. It has no master password."))
      : state == GLOS_STORE_LOCKED ? GUI_T("The store is locked: its master password hasn't been given since GLOS started.")
      : state == GLOS_STORE_BAD ? GUI_T("The store's file can't be read.")
      : GUI_T("There is no store here (GLOS runs without its agent).");
    gui_set_text(sec_line, t);
    gui_enable(sec_unlock, state == GLOS_STORE_LOCKED);
    gui_enable(sec_change, state == GLOS_STORE_OPEN || state == GLOS_STORE_LOCKED);
    gui_set_text(sec_change, has ? GUI_T("&Master password...") : GUI_T("Set a &master password..."));
    printf("CONTROL secrets state=%u password=%u\n", state, has);
    fflush(stdout);
}

static void secrets_button(struct gui_widget *b, int what)
{
    (void)what;
    glos_secrets_store(b->id == 21 ? GLOS_STORE_UNLOCK : GLOS_STORE_PASSWORD, NULL, NULL);     /* (GLOS asks) */
    sec_state = 99;
    secrets_poll();
}

void status_add_secrets(struct gui_widget *page)
{
    struct gui_widget *row;
    gui_label(page, GUI_T("Programs keep passwords and keys in GLOS's secrets store, each its own."));
    gui_label(page, GUI_T("With a master password the store is encrypted under it, and GLOS asks"));
    gui_label(page, GUI_T("for it once after each start. Without one, whoever has the disk has them."));
    sec_line = gui_label(page, "");
    row = gui_hbox(page, 6, 0);
    sec_unlock = gui_button(row, GUI_T("&Unlock..."), secrets_button, 21);
    sec_change = gui_button(row, GUI_T("Set a &master password..."), secrets_button, 22);
    gui_space(row, 0, 0);
    gui_space(page, 0, 0);
    secrets_poll();
}

void status_poll(void)
{
    static struct glos_net_if nif[4];
    static unsigned last;
    char t[LINES][96], a[16], b[16], c[16];
    unsigned count = 0, now = glos_ticks(), i;
    const struct glos_net_if *n = &nif[0];
    pads_poll();
    sound_poll();
    saver_poll();
    clock_poll();
    sl_poll();
    display_poll();
    zone_poll();
    secrets_poll();
    if (!net[0] || (last && now - last < 1000))
        return;
    last = now ? now : 1;
    memset(nif, 0, sizeof nif);
    for (i = 0; i < 4; i++)
        nif[i].size = sizeof nif[i];
    for (i = 0; i < LINES; i++)
        t[i][0] = 0;
    if (glos_net_state(nif, 4, &count) != 0 || !count) {
        snprintf(t[0], sizeof t[0], "%s", GUI_T("No network card or cable link that GLOS has a driver for."));
    } else {
        for (i = 0; i < count && i < 4; i++)    /* the one with an address, if any has */
            if (nif[i].flags & GLOS_NETIF_UP) {
                n = &nif[i];
                break;
            }
        snprintf(t[0], sizeof t[0], n->kind == GLOS_NETIF_ETHERNET ? GUI_T("Card: %s, %02X:%02X:%02X:%02X:%02X:%02X")
                 : GUI_T("Cable link: %s"), n->card, n->mac[0], n->mac[1], n->mac[2], n->mac[3], n->mac[4], n->mac[5]);
        if (!(n->flags & GLOS_NETIF_UP))
            snprintf(t[1], sizeof t[1], "%s", GUI_T("Address: none yet"));
        else if ((n->flags & GLOS_NETIF_DHCP) && n->lease)
            snprintf(t[1], sizeof t[1], GUI_T("Address: %s, mask %s (DHCP: %u h %02u min left)"), ip(n->address, a), ip(n->mask, b),
                     n->lease_left / 3600, n->lease_left / 60 % 60);
        else
            snprintf(t[1], sizeof t[1], GUI_T("Address: %s, mask %s%s"), ip(n->address, a), ip(n->mask, b),
                     n->flags & GLOS_NETIF_DHCP ? GUI_T(" (DHCP)") : "");
        if (n->dns[1])
            snprintf(t[2], sizeof t[2], GUI_T("Gateway: %s   Name servers: %s, %s"), ip(n->gateway, a), ip(n->dns[0], b), ip(n->dns[1], c));
        else
            snprintf(t[2], sizeof t[2], GUI_T("Gateway: %s   Name server: %s"), ip(n->gateway, a), n->dns[0] ? ip(n->dns[0], b) : GUI_T("none"));
        snprintf(t[3], sizeof t[3], GUI_T("Name: %s.local   Frames: %u in, %u out, %u bad"), n->host, n->rx_frames, n->tx_frames,
                 n->rx_errors + n->overruns);
    }
    for (i = 0; i < LINES; i++)
        put((int)i, t[i]);
}

/* The Network page's lines, under its settings. */
void status_add_net(struct gui_widget *page)
{
    int i;
    gui_label(page, GUI_T("Now:"));
    for (i = 0; i < LINES; i++)
        net[i] = gui_label(page, "");
}
