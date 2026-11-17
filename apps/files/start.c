/* FILES: the Start menu (phase D, docs/v1/phase-d.md D3; docs/v1/desktop.md).
 *
 * A pop-up of the shell's own at the task bar's edge (the task bar's mark, Ctrl+Esc, the Windows key:
 * GLOS_SHELL_LAUNCH). On the left a search field over one list: with nothing typed, the entries started most,
 * then every entry under its first letter; typing filters it to the programs, the Control Panel's pages and the
 * recent documents that match, and "Run" for what was typed as it stands. On the right the pinned entries as
 * tiles. Under them: Settings, Files, More (the current row's or tile's own page: open, pin or unpin, move, its
 * tasks, its recent documents) and Power (exit GLOS, restart GLOS, restart the PC).
 *
 * The entries are SHELL's ENTRY list (the programs whose profiles ask to be listed) and GLOS's own apps beside
 * FILES.EXE. The pins in their order and how often each entry was started are kept in START.CFG there.
 * Everything is by keys: type to search, Down into the list, Enter, Tab to the tiles, Esc (back a page, then
 * away); a press elsewhere takes the menu away. "FILES start ..." lines on stdout say what it did. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "files.h"

#define ENTS 64
#define ROWS 160
#define DOCS 32
#define START_W 440
#define START_H 356

struct ent {
    char name[48];                      /* a profile's program name, or one of GLOS's own apps' command */
    char title[40], dir[100], args[100], tasks[192];
    int kind;                           /* IC_ */
    int own;                            /* one of GLOS's own apps (run from FILES.EXE's folder) */
    int uses;
};

enum { R_HEAD, R_ENT, R_PAGE, R_DOC, R_RUN, R_ACT, R_TASK };
enum { A_OPEN = 1, A_PIN, A_EARLIER, A_LATER, A_BACK, A_EXIT, A_RESTART, A_REBOOT };
#define P_MAIN  -1                      /* the list's page: the entries (or what the search found) ... */
#define P_POWER -2                      /* ... Power's ... (0 and up: that entry's own page) */

struct row {
    int kind, idx;                      /* R_; an entry, a page, a document, an action */
    char text[80];
};

/* The Control Panel's pages (apps/control: its sections and their titles). */
static const struct { const char *section, *title; } pages[] = {
    { "display", "Display" }, { "input", "Input" }, { "joystick", "Joystick" }, { "usb", "USB" }, { "sound", "Sound" },
    { "net", "Network" }, { "time", "Date and Time" }, { "region", "Region" }, { "cache", "Disk Cache" },
    { "memory", "Memory" }, { "vnc", "Remote Desktop" }, { "desktop", "Desktop" }, { "saver", "Saver" },
    { "shell", "Shell" }, { "open", "File Types" }, { "program", "Programs" } };
#define NPAGES ((int)(sizeof pages / sizeof *pages))

static struct ent ents[ENTS];
static int nents, pins[ENTS], npins;
static struct row rows[ROWS];
static int nrows, page = P_MAIN;
static char docs[DOCS][140], doc_prog[DOCS][20];
static int ndocs;
static struct gui_window *start;         /* made once (and again if the task bar's edge moves), hidden between */
static struct gui_widget *search, *list, *tiles;
static int up, start_x, start_y;        /* on the screen now; where it was made */
static unsigned down_at;                /* when a press elsewhere took it away (glos_ticks; 0: not so) */

static void say(const char *what, const char *a)
{
    printf("FILES start %s %s\n", what, a);
    fflush(stdout);
}

/* ---- the entries, and what is kept of them */

static int ent_by_name(const char *name)
{
    int i;
    for (i = 0; i < nents; i++)
        if (!strcmp(ents[i].name, name))
            return i;
    return -1;
}

static int pin_at(int e)
{
    int i;
    for (i = 0; i < npins; i++)
        if (pins[i] == e)
            return i;
    return -1;
}

static void state_save(void)
{
    char p[300];
    FILE *f;
    int i;
    snprintf(p, sizeof p, "%s\\START.CFG", files_home);
    if (!(f = gui_fopen(p, "w")))
        return;
    for (i = 0; i < npins; i++)
        fprintf(f, "pin\t%s\n", ents[pins[i]].name);
    for (i = 0; i < nents; i++)
        if (ents[i].uses)
            fprintf(f, "use\t%d\t%s\n", ents[i].uses, ents[i].name);
    fclose(f);
}

static void state_load(void)
{
    static const char *const first[] = { "FILES.EXE -browse", "NOTEPAD.EXE", "TERM.EXE", "CONTROL.EXE" };
    char p[300], line[200];
    FILE *f;
    int i, e;
    npins = 0;
    snprintf(p, sizeof p, "%s\\START.CFG", files_home);
    if (!(f = gui_fopen(p, "r"))) {             /* (never kept: the first four of GLOS's own, pinned) */
        for (i = 0; i < 4; i++)
            if ((e = ent_by_name(first[i])) >= 0)
                pins[npins++] = e;
        return;
    }
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "pin\t", 4)) {
            if ((e = ent_by_name(line + 4)) >= 0 && pin_at(e) < 0 && npins < ENTS)
                pins[npins++] = e;
        } else if (!strncmp(line, "use\t", 4)) {
            char *t = strchr(line + 4, '\t');
            if (t && (e = ent_by_name(t + 1)) >= 0)
                ents[e].uses = atoi(line + 4);
        }
    }
    fclose(f);
}

/* A line's next part, to its tab (or its end). */
static char *part(char **at)
{
    char *s = *at, *t = strchr(s, '\t');
    if (t) {
        *t = 0;
        *at = t + 1;
    } else {
        *at = s + strlen(s);
    }
    return s;
}

static void entries_load(void)
{
    static char buf[8192];
    const char *title, *exe;
    unsigned len = 0;
    char *line, *next;
    int i, kind;
    nents = 0;
    for (i = 0; files_app(i, &title, &exe, &kind) && nents < ENTS; i++) {
        struct ent *e = &ents[nents++];
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", exe);
        snprintf(e->title, sizeof e->title, "%s", title);
        e->kind = kind;
        e->own = 1;
    }
    buf[0] = 0;
    if (glos_entry_list(buf, sizeof buf - 1, &len) == 0)
        buf[len < sizeof buf - 1 ? len : sizeof buf - 1] = 0;
    for (line = buf; *line && nents < ENTS; line = next) {     /* name, title, group, icon, folder, arguments, tasks */
        struct ent *e = &ents[nents];
        char *at = line, *name, *t, *icon_name;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        else
            next = line + strlen(line);
        name = part(&at);
        if (!name[0] || ent_by_name(name) >= 0)
            continue;
        memset(e, 0, sizeof *e);
        snprintf(e->name, sizeof e->name, "%s", name);
        t = part(&at);
        snprintf(e->title, sizeof e->title, "%s", t[0] ? t : name);
        part(&at);                              /* (its group: tiles' groups are a later step) */
        icon_name = part(&at);
        e->kind = icon_name[0] ? icon_by_name(icon_name) : IC_PROGRAM;
        snprintf(e->dir, sizeof e->dir, "%s", part(&at));
        snprintf(e->args, sizeof e->args, "%s", part(&at));
        snprintf(e->tasks, sizeof e->tasks, "%s", part(&at));
        nents++;
    }
    state_load();
}

static void docs_load(void)
{
    static char buf[6000];
    unsigned len = 0;
    char *line, *next;
    ndocs = 0;
    buf[0] = 0;
    if (glos_recent_list(NULL, buf, sizeof buf - 1, &len) == 0)
        buf[len < sizeof buf - 1 ? len : sizeof buf - 1] = 0;
    for (line = buf; *line && ndocs < DOCS; line = next) {     /* "PATH\tPROGRAM" */
        char *at = line;
        next = strchr(line, '\n');
        if (next)
            *next++ = 0;
        else
            next = line + strlen(line);
        snprintf(docs[ndocs], sizeof docs[ndocs], "%s", part(&at));
        snprintf(doc_prog[ndocs], sizeof doc_prog[ndocs], "%s", part(&at));
        if (docs[ndocs][0])
            ndocs++;
    }
}

/* The k-th of an entry's tasks ("Label=ARGUMENTS;..."): its label and its arguments. 1; 0 if that part isn't
   one; -1 past the last. */
static int task_at(const char *tasks, int k, char *label, size_t lroom, char *args, size_t aroom)
{
    const char *p = tasks;
    int i;
    for (i = 0; *p; i++) {
        const char *e = strchr(p, ';'), *eq;
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (i == k) {
            if (!(eq = memchr(p, '=', n)))
                return 0;
            snprintf(label, lroom, "%.*s", (int)(eq - p), p);
            snprintf(args, aroom, "%.*s", (int)(n - (size_t)(eq - p) - 1), eq + 1);
            return 1;
        }
        p += n + (e ? 1 : 0);
    }
    return -1;
}

/* ---- the list's rows */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int has(const char *text, const char *typed)    /* typed is in text, whatever the case */
{
    size_t n = strlen(typed), i;
    for (; *text; text++) {
        for (i = 0; i < n && lower((unsigned char)text[i]) == lower((unsigned char)typed[i]); i++)
            ;
        if (i == n)
            return 1;
    }
    return !n;
}

static void add(int kind, int idx, const char *text)
{
    if (nrows == ROWS)
        return;
    rows[nrows].kind = kind, rows[nrows].idx = idx;
    snprintf(rows[nrows].text, sizeof rows[nrows].text, "%s", text);
    nrows++;
}

static int by_title(const void *a, const void *b)
{
    const char *s = ents[*(const int *)a].title, *t = ents[*(const int *)b].title;
    for (; *s && lower((unsigned char)*s) == lower((unsigned char)*t); s++, t++)
        ;
    return lower((unsigned char)*s) - lower((unsigned char)*t);
}

static int by_uses(const void *a, const void *b)
{
    int d = ents[*(const int *)b].uses - ents[*(const int *)a].uses;
    return d ? d : by_title(a, b);
}

/* An entry's program, as the recent documents name it: its name to the first space. */
static void prog_of(const struct ent *e, char *out, size_t room)
{
    snprintf(out, room, "%s", e->name);
    out[strcspn(out, " ")] = 0;
}

static void fill(void)
{
    const char *typed = search ? gui_get_text(search) : "";
    int order[ENTS], i, n = 0;
    char t[100], name[64];
    nrows = 0;
    if (page == P_POWER) {
        add(R_ACT, A_EXIT, GUI_T("Exit GLOS"));
        add(R_ACT, A_RESTART, GUI_T("Restart GLOS"));
        add(R_ACT, A_REBOOT, GUI_T("Restart the PC"));
        add(R_ACT, A_BACK, GUI_T("Back"));
    } else if (page >= 0) {                     /* an entry's own page */
        const struct ent *e = &ents[page];
        char prog[48], label[64], args[100];
        int at = pin_at(page), r, any = 0;
        snprintf(t, sizeof t, GUI_T("Open %s"), e->title);
        add(R_ACT, A_OPEN, t);
        add(R_ACT, A_PIN, at >= 0 ? GUI_T("Unpin from Start") : GUI_T("Pin to Start"));
        if (at > 0)
            add(R_ACT, A_EARLIER, GUI_T("Move its tile earlier"));
        if (at >= 0 && at + 1 < npins)
            add(R_ACT, A_LATER, GUI_T("Move its tile later"));
        for (i = 0; (r = task_at(e->tasks, i, label, sizeof label, args, sizeof args)) >= 0; i++) {
            if (!r)
                continue;
            if (!any++)
                add(R_HEAD, 0, GUI_T("Tasks"));
            add(R_TASK, i, label);
        }
        prog_of(e, prog, sizeof prog);
        for (i = 0, n = 0; i < ndocs; i++)
            if (!strcasecmp(doc_prog[i], prog)) {
                if (!n++)
                    add(R_HEAD, 0, GUI_T("Recent"));
                add(R_DOC, i, gui_dos_text(gui_path_name(docs[i]), name, sizeof name));
            }
        add(R_ACT, A_BACK, GUI_T("Back"));
    } else if (typed[0]) {                      /* what the search finds, by kind */
        for (i = 0; i < nents; i++)
            if (has(ents[i].title, typed))
                order[n++] = i;
        qsort(order, (size_t)n, sizeof *order, by_title);
        if (n)
            add(R_HEAD, 0, GUI_T("Programs"));
        for (i = 0; i < n; i++)
            add(R_ENT, order[i], ents[order[i]].title);
        for (i = 0, n = 0; i < NPAGES; i++)
            if (has(GUI_T(pages[i].title), typed)) {
                if (!n++)
                    add(R_HEAD, 0, GUI_T("Settings"));
                add(R_PAGE, i, GUI_T(pages[i].title));
            }
        for (i = 0, n = 0; i < ndocs; i++)
            if (has(gui_path_name(docs[i]), typed)) {
                if (!n++)
                    add(R_HEAD, 0, GUI_T("Documents"));
                add(R_DOC, i, gui_dos_text(gui_path_name(docs[i]), name, sizeof name));
            }
        snprintf(t, sizeof t, GUI_T("Run: %s"), typed);
        add(R_RUN, 0, t);
    } else {                                    /* the entries started most, then all of them by their letters */
        int letter = 0;
        for (i = 0; i < nents; i++)
            if (ents[i].uses)
                order[n++] = i;
        qsort(order, (size_t)n, sizeof *order, by_uses);
        if (n)
            add(R_HEAD, 0, GUI_T("Most used"));
        for (i = 0; i < n && i < 5; i++)
            add(R_ENT, order[i], ents[order[i]].title);
        for (i = 0; i < nents; i++)
            order[i] = i;
        qsort(order, (size_t)nents, sizeof *order, by_title);
        for (i = 0; i < nents; i++) {
            int c = lower((unsigned char)ents[order[i]].title[0]);
            if (c != letter) {
                t[0] = (char)(c >= 'a' && c <= 'z' ? c - 32 : c), t[1] = 0;
                add(R_HEAD, 0, t);
                letter = c;
            }
            add(R_ENT, order[i], ents[order[i]].title);
        }
    }
    if (list) {
        gui_vlist_changed(list);
        for (i = 0; i < nrows && rows[i].kind == R_HEAD; i++)   /* (the first row that is something) */
            ;
        gui_vlist_set(list, i < nrows ? i : -1);
    }
}

/* ---- the models */

static int l_count(void *ctx) { (void)ctx; return nrows; }

static void l_row(void *ctx, int r, struct gui_vrow *out)
{
    const struct row *w;
    (void)ctx;
    if (r < 0 || r >= nrows)
        return;
    w = &rows[r];
    out->cell[0] = w->text;
    out->id = (unsigned long)r + 1;
    out->icon = w->kind == R_ENT ? icon(ents[w->idx].kind, 0) : w->kind == R_PAGE ? icon(IC_CONTROL, 0)
              : w->kind == R_DOC ? icon(icon_for_file(docs[w->idx], 0), 0) : w->kind == R_RUN ? icon(IC_PROGRAM, 0) : NULL;
}

static int t_count(void *ctx) { (void)ctx; return npins; }

static void t_row(void *ctx, int r, struct gui_vrow *out)
{
    (void)ctx;
    if (r < 0 || r >= npins)
        return;
    out->cell[0] = ents[pins[r]].title;
    out->id = (unsigned long)pins[r] + 1;
    out->icon = icon(ents[pins[r]].kind, 0);
    out->big = icon(ents[pins[r]].kind, 1);
}

static int t_find(void *ctx, unsigned long id)
{
    (void)ctx;
    return id ? pin_at((int)id - 1) : -1;
}

/* ---- what a row does */

static void hide(void)
{
    if (!up)
        return;
    up = 0;
    gui_show(start, 0);
}

static void run_ent(int i, const char *args)
{
    struct ent *e = &ents[i];
    char cmd[320];
    if (e->own)
        snprintf(cmd, sizeof cmd, "%s\\%s%s%s", files_home, e->name, args && args[0] ? " " : "", args ? args : "");
    else
        snprintf(cmd, sizeof cmd, "%s%s%s", e->name, (args ? args : e->args)[0] ? " " : "", args ? args : e->args);
    e->uses++;
    state_save();
    say("run", e->title);
    hide();
    files_run(cmd, e->own ? NULL : e->dir);
}

static void to_page(int p)
{
    page = p;
    if (search && p != P_MAIN)
        gui_set_text(search, "");
    fill();
    if (list)
        gui_focus(list);
}

static void act(int r)
{
    const struct row *w;
    char t[160];
    int e = page, at;
    if (r < 0 || r >= nrows)
        return;
    w = &rows[r];
    switch (w->kind) {
    case R_ENT:
        run_ent(w->idx, NULL);
        break;
    case R_PAGE:
        say("page", pages[w->idx].section);
        hide();
        files_run_app("CONTROL.EXE", pages[w->idx].section);
        break;
    case R_DOC:
        snprintf(t, sizeof t, "%s", docs[w->idx]);
        say("document", t);
        hide();
        files_open(t);
        break;
    case R_RUN:
        snprintf(t, sizeof t, "%s", gui_get_text(search));
        say("typed", t);
        hide();
        files_run_typed(t);
        break;
    case R_TASK: {                              /* the entry's task: its arguments */
        char label[64], args[100];
        if (task_at(ents[e].tasks, w->idx, label, sizeof label, args, sizeof args) == 1)
            run_ent(e, args);
        break;
    }
    case R_ACT:
        at = e >= 0 ? pin_at(e) : -1;
        switch (w->idx) {
        case A_OPEN:
            run_ent(e, NULL);
            return;
        case A_PIN:
            if (at >= 0) {
                memmove(&pins[at], &pins[at + 1], (size_t)(npins - at - 1) * sizeof *pins);
                npins--;
            } else if (npins < ENTS) {
                pins[npins++] = e;
            }
            say(at >= 0 ? "unpinned" : "pinned", ents[e].title);
            break;
        case A_EARLIER:
        case A_LATER: {
            int to = at + (w->idx == A_EARLIER ? -1 : 1), was = pins[to];
            pins[to] = pins[at], pins[at] = was;
            say("moved", ents[e].title);
            break;
        }
        case A_BACK:
            to_page(P_MAIN);
            return;
        case A_EXIT:
        case A_RESTART:
        case A_REBOOT:
            say("power", w->idx == A_EXIT ? "exit" : w->idx == A_RESTART ? "restart" : "reboot");
            hide();
            files_shell(w->idx == A_EXIT ? "glos exit" : w->idx == A_RESTART ? "glos restart" : "glos reboot");
            return;
        }
        state_save();
        if (tiles)
            gui_vlist_changed(tiles);
        fill();
        break;
    }
}

/* ---- the widgets' functions */

static void search_on(struct gui_widget *w, int what)
{
    (void)w;
    if (what == GUI_ON_CHANGE) {
        page = P_MAIN;
        fill();
    } else if (what == GUI_ON_ACTIVATE) {
        act(gui_vlist_get(list));
    }
}

static void list_on(struct gui_widget *w, int what)
{
    if (what == GUI_ON_ACTIVATE)
        act(gui_vlist_get(w));
}

static void tiles_on(struct gui_widget *w, int what)
{
    int r = gui_vlist_get(w);
    if (what == GUI_ON_ACTIVATE && r >= 0 && r < npins)
        run_ent(pins[r], NULL);
}

static void more(void)                          /* the current row's entry, or the current tile's: its own page */
{
    int r;
    if (!up)
        return;
    if (start->focus == tiles) {
        if ((r = gui_vlist_get(tiles)) >= 0 && r < npins)
            to_page(pins[r]);
    } else if ((r = gui_vlist_get(list)) >= 0 && r < nrows && rows[r].kind == R_ENT) {
        to_page(rows[r].idx);
    }
}

static void button(struct gui_widget *w, int what)
{
    (void)what;
    switch (w->id) {
    case 1:
        say("page", "");
        hide();
        files_run_app("CONTROL.EXE", NULL);
        break;
    case 2:
        say("files", "");
        hide();
        files_run_app("FILES.EXE -browse", NULL);
        break;
    case 3:
        more();
        break;
    case 4:
        to_page(P_POWER);
        break;
    }
}

static int key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    if (e->type != GUI_EV_KEY)
        return 0;
    if (e->key == GUI_K_ESC) {                  /* back a page, then away */
        if (page != P_MAIN)
            to_page(P_MAIN);
        else
            hide();
        return 1;
    }
    if (e->key == GUI_K_MENU || (e->key == GUI_K_F10 && (e->mods & GLOS_MOD_SHIFT))) {
        more();
        return 1;
    }
    if (e->key == GUI_K_DOWN && start->focus == search) {
        gui_focus(list);
        return 1;
    }
    if (e->ch >= 0x20 && e->ch != 0x7F && !(e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT)) && start->focus != search) {
        char t[80];                             /* typing anywhere else in it: into the search */
        struct gui_ev end;
        snprintf(t, sizeof t, "%s%c", gui_get_text(search), (char)e->ch);
        gui_set_text(search, t);
        gui_focus(search);
        /* (a field given the keyboard chooses its text, and the next letter would take its place: the caret to
           the text's end instead, by the field's own End) */
        memset(&end, 0, sizeof end);
        end.type = GUI_EV_KEY, end.key = GUI_K_END;
        search->cls->event(search, &end);
        page = P_MAIN;
        fill();
        return 1;
    }
    return 0;
}

static void popdown(struct gui_window *w)
{
    (void)w;
    if (up)
        down_at = glos_ticks() | 1;
    hide();
}

/* The Start menu at x, y: the task bar's edge (above it, or below it when the bar is at the screen's top). The
   window is made the first time, and again if that edge has moved; between times it is hidden, not closed (a
   new window each time could be told of the old one's lost keyboard). */
void start_open(int x, int y)
{
    static const struct gui_model lm = { l_count, l_row, NULL, NULL, NULL }, tm = { t_count, t_row, t_find, NULL, NULL };
    struct gui_widget *box, *cols, *left, *right, *row;
    int sw, sh;
    if (up) {                                   /* (asked for again while it is up: away) */
        hide();
        return;
    }
    /* A press on the task bar's mark while it was up took it away just now (the press took the keyboard from it,
       then asked for the launcher): it stays away. */
    if (down_at && glos_ticks() - down_at < 300) {
        down_at = 0;
        return;
    }
    down_at = 0;
    gui_screen_size(&sw, &sh);
    if (y > sh / 2)
        y -= START_H;
    if (start && (x != start_x || y != start_y)) {
        gui_close(start);
        start = NULL;
    }
    if (!start) {
        start = gui_window_at(GUI_T("Start"), x, y, START_W, START_H, GUI_W_POPUP | GUI_W_BARE);
        start_x = x, start_y = y;
        start->on_key = key;
        start->on_popdown = popdown;
        box = gui_vbox(start->root, 6, 6);
        gui_grow(box, 1);
        cols = gui_hbox(box, 8, 0);
        gui_grow(cols, 1);
        left = gui_vbox(cols, 4, 0);
        gui_size(left, 190, 0);
        search = gui_field(left, 20);
        gui_on(search, search_on, 0);
        list = gui_vlist(left, 1, NULL, NULL, &lm);
        gui_grow(list, 1);
        gui_on(list, list_on, 0);
        right = gui_vbox(cols, 4, 0);
        gui_grow(right, 1);
        gui_label(right, GUI_T("Pinned"));
        tiles = gui_vlist(right, 1, NULL, NULL, &tm);
        gui_vlist_view(tiles, GUI_VIEW_ICONS);
        gui_grow(tiles, 1);
        gui_on(tiles, tiles_on, 0);
        row = gui_hbox(box, 6, 0);
        gui_button(row, GUI_T("&Settings"), button, 1);
        gui_button(row, GUI_T("&Files"), button, 2);
        gui_space(row, 0, 0);
        gui_button(row, GUI_T("&More"), button, 3);
        gui_button(row, GUI_T("&Power"), button, 4);
    }
    entries_load();
    docs_load();
    page = P_MAIN;
    gui_set_text(search, "");
    fill();
    gui_vlist_changed(tiles);
    gui_vlist_set(tiles, npins ? 0 : -1);
    gui_focus(search);
    gui_show(start, 1);
    up = 1;
    say("up", "");
}

/* The shell is ending (its desktop window closed): the menu's window goes too, hidden as it may be. */
void start_end(void)
{
    if (start)
        gui_close(start);
    start = NULL;
    up = 0;
}
