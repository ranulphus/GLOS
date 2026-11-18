/* CONTROL: the Control Panel (PRD §10.7, D53; docs/milestones-m9.md step G).
 *
 * GLOS.CFG's settings, drawn from the schema (include/glos/config.h): a page
 * for each of its sections, with a widget for each setting by its kind (a
 * check box for on and off, a list to choose from, a field, the screen's
 * modes (the ones the card has), lines for a program's environment); the programs' profiles (a list
 * of the [program NAME.EXT] sections, a form for the one chosen, New and
 * Remove); the kinds of file and what opens them ([open]). The pages' names
 * are a list down the left (phase D); over it a field that finds a setting
 * by a word of its name or description (Ctrl+F; Enter or F3: the next one).
 * Beside the Desktop page's settings, the desktop's look (look.c). Under the pages,
 * what the setting with the keyboard does and when a change to it takes
 * effect. Apply checks every change against the schema first, then sets them
 * (SETTINGS' SET, which writes the file); a new screen mode is tried for
 * fifteen seconds, and goes back unless it is kept. OK applies and closes;
 * Cancel closes.
 *
 *   CONTROL [SECTION]   at that section's page (time, sound, network ...: the
 *                       task bar's clock and icons open theirs so)
 *
 * What it changed goes to stdout as "CONTROL ..." lines (the suites'). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <glos/config.h>
#include <glos/gui.h>

#include "control.h"

#define MAXVAL 256
#define MAXCTL 96                       /* (the schema's settings: 53 with API 2.0 step 5's) */
#define MAXPROG 64
#define TRY_SECONDS 15
#define MAXCHOICE 40                    /* (a list's values: the screen's modes are the most) */

/* A setting's value: as the file has it, and as the user has made it (NULL: the default; not in the file). */
struct val {
    char sec[48], key[32];
    const struct cfg_key *k;
    char *was, *now;
};
static struct val vals[MAXVAL];
static int nvals;

/* A setting's widget. */
enum { W_FIELD, W_CHECK, W_CHOICE, W_MODE, W_ENV };
struct ctl {
    const struct cfg_key *k;
    struct gui_widget *w, *label;
    int kind, page;
    const char *choice[MAXCHOICE];
    int nchoice;
};
static struct ctl ctls[MAXCTL];
static int nctl;

static struct gui_window *win;
static struct gui_widget *tabs, *help, *when_label, *status, *find;
static int find_at = -1;                        /* the setting last found (ctls' index) */
static int field_chars = 30;                    /* a text setting's field: as wide as this at most */
static struct gui_widget *prog_list, *types_list, *prog_remove;
static int prog_page, types_page;
static char progs[MAXPROG][16];
static int nprogs, prog_at = -1;
static int changes_made;

static const char *const modes[] = { "none", "640x480x8", "640x480x16", "640x480x32", "800x600x8", "800x600x16",
                                     "800x600x32", "1024x768x8", "1024x768x16", "1024x768x32", "1280x1024x8",
                                     "1280x1024x16", "1280x1024x32" };
static char mode_extra[24];                     /* the file's mode, if it isn't one of these */
/* The modes the card has (DRAW's MODES; phase D), 640 by 480 and up as the desktop needs: the Screen list is
   these when the card says, the ones above when it doesn't. */
static char card_modes[MAXCHOICE - 2][16];
static int ncard;

static void modes_read(void)
{
    static struct glos_mode m[64];
    unsigned count = 0, i;
    int r = glos_modes(m, 64, &count), tries;
    for (tries = 0; r == GLOS_E_REFUSED && tries < 10; tries++) {      /* (DOS is busy: a moment) */
        glos_sleep(200);
        r = glos_modes(m, 64, &count);
    }
    if (r != 0 && r != GLOS_E_ROOM)
        return;
    for (i = 0; i < count && i < 64 && ncard < MAXCHOICE - 2; i++)
        if (m[i].w >= 640 && m[i].h >= 480)
            snprintf(card_modes[ncard++], sizeof card_modes[0], "%dx%dx%u", m[i].w, m[i].h, m[i].bits);
}

static const char *const titles[] = { GUI_N("Display"), GUI_N("Input"), GUI_N("Joystick"), GUI_N("USB"),
    GUI_N("Sound"), GUI_N("Network"), GUI_N("Date and Time"), GUI_N("Region"), GUI_N("Disk Cache"), GUI_N("Memory"),
    GUI_N("Remote Desktop"), GUI_N("Desktop"), GUI_N("Saver"), GUI_N("Services"), GUI_N("Shell"), GUI_N("File Types"),
    GUI_N("Programs") };
                                                /* (cfg_sections' order) */

static char *dup(const char *s) { return s ? strcpy(malloc(strlen(s) + 1), s) : NULL; }

static int same(const char *a, const char *b)
{
    for (; *a && *b && ((*a | 0x20) == (*b | 0x20) || *a == *b); a++, b++)
        ;
    return !*a && !*b;
}

/* ---- the values */

static struct val *val_of(const char *sec, const char *key)
{
    char buf[1024];
    struct val *v;
    int i, set = 0;
    for (i = 0; i < nvals; i++)
        if (same(vals[i].sec, sec) && same(vals[i].key, key))
            return &vals[i];
    if (nvals == MAXVAL || !cfg_find(sec, key))
        return NULL;
    v = &vals[nvals++];
    snprintf(v->sec, sizeof v->sec, "%s", sec);
    snprintf(v->key, sizeof v->key, "%s", key);
    v->k = cfg_find(sec, key);
    if (glos_setting_get(sec, key, buf, sizeof buf, &set) == 0 && set) {
        v->was = dup(buf);
        v->now = dup(buf);
    }
    return v;
}

static const char *val_text(const struct val *v) { return v->now ? v->now : v->k->def; }

static void val_put(struct val *v, const char *text)
{
    if (!strcmp(val_text(v), text))
        return;
    free(v->now);
    v->now = strcmp(text, v->k->def) ? dup(text) : NULL;
}

static int val_changed(const struct val *v)
{
    return (v->was == NULL) != (v->now == NULL) || (v->was && strcmp(v->was, v->now));
}

/* ---- the widgets */

static const char *ctl_sec(const struct ctl *c, char *buf, size_t room)
{
    if (strcmp(c->k->section, "program"))
        return c->k->section;
    if (prog_at < 0)
        return NULL;
    snprintf(buf, room, "program %s", progs[prog_at]);
    return buf;
}

static void ctl_load(struct ctl *c)
{
    char sec[48];
    const char *s = ctl_sec(c, sec, sizeof sec), *t;
    struct val *v = s ? val_of(s, c->k->key) : NULL;
    int i;
    gui_enable(c->w, v != NULL);
    t = v ? val_text(v) : c->k->def;
    switch (c->kind) {
    case W_CHECK:
        gui_set_checked(c->w, !strcmp(t, "on") || !strcmp(t, "1"));
        break;
    case W_CHOICE:
    case W_MODE:
        for (i = 0; i < c->nchoice && !same(c->choice[i], t); i++)
            ;
        if (i == c->nchoice && c->kind == W_MODE && c->nchoice < MAXCHOICE) {
            snprintf(mode_extra, sizeof mode_extra, "%s", t);
            gui_choice_add(c->w, mode_extra);
            c->choice[c->nchoice++] = mode_extra;
        }
        gui_set_value(c->w, i < c->nchoice ? i : 0);
        break;
    case W_ENV:
        gui_editor_set(c->w, t, strlen(t));
        break;
    default:
        gui_set_text(c->w, t);
    }
}

static void ctl_save(struct ctl *c)
{
    char sec[48], text[1024];
    const char *s = ctl_sec(c, sec, sizeof sec);
    struct val *v = s ? val_of(s, c->k->key) : NULL;
    if (!v)
        return;
    switch (c->kind) {
    case W_CHECK: {
        int on = gui_checked(c->w);
        snprintf(text, sizeof text, "%s", !strcmp(c->choice[0], "on") || !strcmp(c->choice[1], "on")
                                          ? (on ? "on" : "off") : (on ? "1" : "0"));
        break;
    }
    case W_CHOICE:
    case W_MODE:
        snprintf(text, sizeof text, "%s", c->choice[gui_value(c->w)]);
        break;
    case W_ENV: {                               /* lines, without CRs or empty ones */
        size_t n = gui_editor_get(c->w, text, sizeof text - 1), i, j;
        if (n > sizeof text - 1)
            n = sizeof text - 1;
        text[n] = 0;
        for (i = j = 0; i < n; i++)
            if (text[i] != '\r' && !(text[i] == '\n' && (j == 0 || text[j - 1] == '\n')))
                text[j++] = text[i];
        while (j && text[j - 1] == '\n')
            j--;
        text[j] = 0;
        break;
    }
    default:
        snprintf(text, sizeof text, "%s", gui_get_text(c->w));
    }
    val_put(v, text);
}

static void save_all(void)
{
    int i;
    for (i = 0; i < nctl; i++)
        ctl_save(&ctls[i]);
}

static void load_page(int page)
{
    int i;
    for (i = 0; i < nctl; i++)
        if (ctls[i].page == page)
            ctl_load(&ctls[i]);
}

static struct ctl *ctl_add(struct gui_widget *form, const struct cfg_key *k, int page)
{
    struct ctl *c = &ctls[nctl++];
    struct gui_widget *row;
    static char choices[MAXCTL][96];
    memset(c, 0, sizeof *c);
    c->k = k;
    c->page = page;
    if (k->type == CFG_CHOICE) {                /* "a|b|c" into its parts */
        char *p = choices[nctl - 1], *e;
        snprintf(p, sizeof choices[0], "%s", k->choices);
        for (; p && c->nchoice < MAXCHOICE; p = e ? e + 1 : NULL) {
            if ((e = strchr(p, '|')) != NULL)
                *e = 0;
            c->choice[c->nchoice++] = p;
        }
    }
    if (k->type == CFG_CHOICE && c->nchoice == 2 && ((!strcmp(c->choice[0], "on") && !strcmp(c->choice[1], "off"))
                                                     || (!strcmp(c->choice[0], "0") && !strcmp(c->choice[1], "1")))) {
        c->kind = W_CHECK;
        row = gui_row(form, "", &c->label);
        c->w = gui_check(row, k->label, 0);
    } else if (k->type == CFG_CHOICE || k->type == CFG_MODE) {
        int i;
        c->kind = k->type == CFG_MODE ? W_MODE : W_CHOICE;
        row = gui_row(form, k->label, &c->label);
        c->w = gui_choice(row, k->type == CFG_MODE ? 14 : 10);
        if (c->kind == W_MODE && ncard) {       /* none, and what the card has */
            c->choice[0] = modes[0];
            for (c->nchoice = 1; c->nchoice <= ncard; c->nchoice++)
                c->choice[c->nchoice] = card_modes[c->nchoice - 1];
        } else if (c->kind == W_MODE) {
            for (c->nchoice = 0; c->nchoice < (int)(sizeof modes / sizeof *modes); c->nchoice++)
                c->choice[c->nchoice] = modes[c->nchoice];
        }
        for (i = 0; i < c->nchoice; i++)
            gui_choice_add(c->w, c->choice[i]);
    } else if (k->type == CFG_ENV) {
        c->kind = W_ENV;
        row = gui_row(form, k->label, &c->label);
        c->w = gui_editor(row);
        gui_editor_font(c->w, gui_mono);
        gui_editor_wrap(c->w, 1);
        gui_size(c->w, 260, 56);
        gui_grow(c->w, 1);
    } else {
        c->kind = W_FIELD;
        row = gui_row(form, k->label, &c->label);
        c->w = gui_field(row, k->type == CFG_NUMBER ? 8 : k->max >= field_chars ? field_chars : (int)k->max + 1);
    }
    return c;
}

/* ---- what the setting with the keyboard does */

static void help_poll(void *arg)
{
    static const struct cfg_key *shown = (const struct cfg_key *)1;
    static const char *const when[] = { GUI_N("Takes effect at once."), GUI_N("Takes effect when GLOS next starts."),
                                        GUI_N("Takes effect when the program next starts."),
                                        GUI_N("Takes effect when the desktop next starts."),
                                        GUI_N("Takes effect when an app next starts.") };
    const struct cfg_key *k = NULL;
    int i;
    (void)arg;
    look_poll();                                /* (the desktop's look follows its page: look.c) */
    status_poll();                              /* (and the network's state is read: status.c) */
    for (i = 0; i < nctl; i++)
        if (win->focus == ctls[i].w)
            k = ctls[i].k;
    if (win->focus == types_list)
        k = cfg_find("open", ".TXT");
    if (k == shown)
        return;
    shown = k;
    gui_set_text(help, k ? k->desc : "");
    gui_set_text(when_label, k ? GUI_T(when[k->when < 5 ? k->when : 0]) : "");
}

/* ---- a setting found by a word of its name or description */

/* Whether s has t in it, whatever the capitals (s's & for its letter left out). */
static int has_text(const char *s, const char *t)
{
    for (; *s; s++) {
        const char *a = s, *b = t;
        while (*b) {
            if (*a == '&')
                a++;
            if (!*a || (*a | 0x20) != (*b | 0x20))
                break;
            a++, b++;
        }
        if (!*b)
            return 1;
    }
    return 0;
}

/* The next setting after the one last found that has the field's text: its page, and the keyboard to it. */
static void find_step(void)
{
    const char *t = gui_get_text(find);
    char say[160], label[64];
    int i, k;
    size_t a, b;
    if (!t[0])
        return;
    for (i = 1; i <= nctl; i++) {
        const struct cfg_key *key;
        k = (find_at + i) % nctl;
        key = ctls[k].k;
        if (!has_text(key->label, t) && !has_text(key->desc, t) && !has_text(GUI_T(titles[ctls[k].page]), t))
            continue;
        find_at = k;
        gui_tabs_set(tabs, ctls[k].page);
        gui_focus(ctls[k].w);
        for (a = b = 0; key->label[a] && b < sizeof label - 1; a++)
            if (key->label[a] != '&')
                label[b++] = key->label[a];
        label[b] = 0;
        snprintf(say, sizeof say, GUI_T("Found: %s, %s. F3 finds the next."), GUI_T(titles[ctls[k].page]), label);
        gui_set_text(status, say);
        return;
    }
    gui_set_text(status, GUI_T("No setting has that in its name or description."));
}

static void find_on(struct gui_widget *w, int what)
{
    (void)w;
    if (what == GUI_ON_CHANGE)
        find_at = -1;                           /* (another word: from the first setting) */
    else if (what == GUI_ON_ACTIVATE)
        find_step();
}

/* Ctrl+F: the keyboard to the search; F3: the next setting found. */
static int on_key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    if (e->type != GUI_EV_KEY)
        return 0;
    if (e->key == 0x21 && (e->mods & GLOS_MOD_CTRL)) {
        gui_focus(find);
        return 1;
    }
    if (e->key == 0x3D) {
        find_step();
        return 1;
    }
    return 0;
}

/* ---- the programs' profiles */

static void prog_fill(void)
{
    int i;
    gui_list_clear(prog_list);
    for (i = 0; i < nprogs; i++) {
        const char *cells[1];
        cells[0] = progs[i];
        gui_list_add(prog_list, cells, NULL, NULL);
    }
}

static void prog_choose(int i)
{
    int k;
    for (k = 0; k < nctl; k++)                  /* the one shown kept, then the new one shown */
        if (ctls[k].page == prog_page)
            ctl_save(&ctls[k]);
    prog_at = i < nprogs ? i : -1;
    load_page(prog_page);
    gui_enable(prog_remove, prog_at >= 0);
}

static void prog_select(struct gui_widget *w, int what)
{
    if (what == GUI_ON_SELECT)
        prog_choose(gui_list_get(w));
}

static void prog_new(struct gui_widget *w, int what)
{
    char name[32] = "", sec[48];
    int i;
    (void)w, (void)what;
    if (!gui_ask(win, GUI_T("New Program"), GUI_T("The program's file name (NAME.EXE):"), name, sizeof name))
        return;
    for (i = 0; name[i]; i++)
        if (name[i] >= 'a' && name[i] <= 'z')
            name[i] -= 32;
    snprintf(sec, sizeof sec, "program %s", name);
    if (!name[0] || strlen(name) > 12 || !cfg_section_ok(sec)) {
        gui_message(win, GUI_T("New Program"), GUI_T("That isn't a DOS program's file name."), GUI_MB_OK);
        return;
    }
    for (i = 0; i < nprogs && !same(progs[i], name); i++)
        ;
    if (i == nprogs && nprogs < MAXPROG)
        snprintf(progs[nprogs++], sizeof progs[0], "%s", name);
    prog_fill();
    gui_list_set(prog_list, i);
    prog_choose(i);
}

static void prog_del(struct gui_widget *w, int what)
{
    char q[96], sec[48];
    int i;
    (void)w, (void)what;
    if (prog_at < 0)
        return;
    snprintf(q, sizeof q, GUI_T("Remove %s's profile? It runs with GLOS's defaults then."), progs[prog_at]);
    if (gui_message(win, GUI_T("Remove"), q, GUI_MB_YESNO) != GUI_YES)
        return;
    snprintf(sec, sizeof sec, "program %s", progs[prog_at]);
    for (i = 0; i < cfg_schema_n; i++)          /* every key it has: the default */
        if (!strcmp(cfg_schema[i].section, "program")) {
            struct val *v = val_of(sec, cfg_schema[i].key);
            if (v) {
                free(v->now);
                v->now = NULL;
            }
        }
    for (i = prog_at; i + 1 < nprogs; i++)
        memcpy(progs[i], progs[i + 1], sizeof progs[0]);
    nprogs--;
    prog_at = -1;
    prog_fill();
    prog_choose(nprogs ? 0 : -1);
    if (nprogs)
        gui_list_set(prog_list, 0);
}

/* ---- the kinds of file */

static void types_fill(void)
{
    int i, keep = gui_list_get(types_list);
    gui_list_clear(types_list);
    for (i = 0; i < nvals; i++)
        if (!strcmp(vals[i].sec, "open") && vals[i].now) {
            const char *cells[2];
            cells[0] = vals[i].key, cells[1] = vals[i].now;
            gui_list_add(types_list, cells, NULL, &vals[i]);
        }
    if (gui_list_count(types_list))
        gui_list_set(types_list, keep >= 0 && keep < gui_list_count(types_list) ? keep : 0);
}

static void types_add(struct gui_widget *w, int what)
{
    char ext[16] = ".", prog[80] = "", q[96];
    struct val *v;
    int i;
    (void)what;
    if (w->id == 2) {                           /* Change: the one chosen */
        int r = gui_list_get(types_list);
        if (r < 0)
            return;
        v = gui_list_user(types_list, r);
        snprintf(prog, sizeof prog, "%s", val_text(v));
        snprintf(q, sizeof q, GUI_T("The program that opens %s files:"), v->key);
        if (!gui_ask(win, GUI_T("Change"), q, prog, sizeof prog))
            return;
        val_put(v, prog);
        types_fill();
        return;
    }
    if (!gui_ask(win, GUI_T("Add"), GUI_T("The kind of file (.EXT):"), ext, sizeof ext))
        return;
    for (i = 0; ext[i]; i++)
        if (ext[i] >= 'a' && ext[i] <= 'z')
            ext[i] -= 32;
    if (ext[0] != '.' || !ext[1] || strlen(ext) > 4 || !(v = val_of("open", ext))) {
        gui_message(win, GUI_T("Add"), GUI_T("A kind of file is a dot and up to three letters: .TXT"), GUI_MB_OK);
        return;
    }
    snprintf(q, sizeof q, GUI_T("The program that opens %s files:"), ext);
    snprintf(prog, sizeof prog, "%s", val_text(v));
    if (!gui_ask(win, GUI_T("Add"), q, prog, sizeof prog))
        return;
    val_put(v, prog);
    types_fill();
}

static void types_del(struct gui_widget *w, int what)
{
    int r = gui_list_get(types_list);
    struct val *v;
    (void)w, (void)what;
    if (r < 0)
        return;
    v = gui_list_user(types_list, r);
    free(v->now);
    v->now = NULL;
    types_fill();
}

/* ---- Apply */

static struct gui_window *try_w;
static struct gui_widget *try_text;
static int try_left;

/* The question, from a resource file's text (toolkit 2, C10: its words through GUI_T as it is loaded). */
static const char try_res[] =
    "object Screen: Dialog\n"
    "  Title = \"Screen\"\n"
    "  object Box: VBox\n"
    "    Gap = 10\n"
    "    Pad = 12\n"
    "    object Says: Label\n"
    "    end\n"
    "    object Buttons: HBox\n"
    "      Pad = 0\n"
    "      object Fill: Space\n"
    "      end\n"
    "      object Keep: Button\n"
    "        Text = \"&Keep\"\n"
    "        Default = true\n"
    "        Command = 1\n"
    "      end\n"
    "      object Back: Button\n"
    "        Text = \"&Back\"\n"
    "        Cancel = true\n"
    "        Command = 2\n"
    "      end\n"
    "    end\n"
    "  end\n"
    "end\n";

static void try_say(void)
{
    char t[80];
    snprintf(t, sizeof t, GUI_T("Keep this screen? It goes back in %d second%s."), try_left, try_left == 1 ? "" : "s");
    gui_set_text(try_text, t);
}

static void try_tick(void *arg)                 /* a second gone */
{
    (void)arg;
    if (--try_left <= 0)
        gui_end_modal(try_w, GUI_CANCEL_R);
    else
        try_say();
}

static void try_command(struct gui_window *w, int id)  /* Keep (1) or Back (2) */
{
    gui_end_modal(w, id == 1 ? GUI_OK : GUI_CANCEL_R);
}

/* The new screen mode, tried: 1 kept; 0 the old one again. */
static int try_mode(void)
{
    char err[96];
    int r;
    if (!(try_w = gui_load_text(try_res, err, sizeof err)))
        return 0;                               /* (no question to ask: the old screen again, to be safe) */
    try_w->owner = win;
    try_w->on_command = try_command;
    try_text = gui_named(try_w, "Says");
    try_left = TRY_SECONDS;
    try_say();
    gui_idle(try_tick, NULL, 1000);
    r = gui_modal(try_w);
    gui_idle(try_tick, NULL, 0);
    gui_close(try_w);
    return r == GUI_OK;
}

static void say_value(const char *what, const struct val *v)
{
    char t[1024];
    size_t i;
    snprintf(t, sizeof t, "%s", v->now ? v->now : "");
    for (i = 0; t[i]; i++)
        if (t[i] == '\n')
            t[i] = '|';
    if (v->now)
        printf("CONTROL %s [%s] %s = %s\n", what, v->sec, v->key, t);
    else
        printf("CONTROL %s [%s] %s\n", what, v->sec, v->key);
    fflush(stdout);
}

static void show_val(const struct val *v)
{
    int i, p;
    for (i = 0; i < nctl && !(ctls[i].k == v->k); i++)
        ;
    if (!strcmp(v->k->section, "program")) {
        for (p = 0; p < nprogs && !same(v->sec + 8, progs[p]); p++)
            ;
        if (p < nprogs) {
            gui_list_set(prog_list, p);
            prog_choose(p);
        }
    }
    if (i < nctl) {
        gui_tabs_set(tabs, ctls[i].page);
        gui_focus(ctls[i].w);
    } else if (!strcmp(v->k->section, "open")) {
        gui_tabs_set(tabs, types_page);
    }
}

/* Every change checked, then set: 1 if they all were. */
static int apply(void)
{
    char why[200], one[200], msg[300];
    int i, pass, later = 0, n = 0;
    struct val *mode = NULL;
    char *old_mode = NULL;                      /* (the mode before, NULL the default: for going back) */
    save_all();
    for (i = 0; i < nvals; i++) {              /* checked first: nothing is set if one is wrong */
        struct val *v = &vals[i];
        const char *p;
        if (!val_changed(v) || !v->now)
            continue;
        for (p = v->now;;) {
            size_t k = 0;
            while (*p && (v->k->type != CFG_ENV || *p != '\n') && k < sizeof one - 1)
                one[k++] = *p++;
            one[k] = 0;
            if (cfg_check(v->k, one, why, sizeof why) != 0) {
                char label[64];
                size_t a, b;
                for (a = b = 0; v->k->label[a] && b < sizeof label - 1; a++)
                    if (v->k->label[a] != '&')
                        label[b++] = v->k->label[a];
                label[b] = 0;
                printf("CONTROL refused [%s] %s: %s\n", v->sec, v->key, why);
                fflush(stdout);
                snprintf(msg, sizeof msg, "%s%s%s: %s", strcmp(v->sec, "open") ? label : v->key,
                         strncmp(v->sec, "program ", 8) ? "" : " of ", strncmp(v->sec, "program ", 8) ? "" : v->sec + 8,
                         why);
                show_val(v);
                gui_message(win, GUI_T("Not Applied"), msg, GUI_MB_OK);
                return 0;
            }
            if (!*p++ || v->k->type != CFG_ENV)
                break;
        }
    }
    for (pass = 0; pass < 2; pass++)            /* the screen's mode last, to be tried */
        for (i = 0; i < nvals; i++) {
            struct val *v = &vals[i];
            unsigned when = 0;
            int r;
            if (!val_changed(v) || (v->k->type == CFG_MODE) != (pass == 1))
                continue;
            why[0] = 0;
            r = glos_setting_set(v->sec, v->key, v->now, &when, why, sizeof why) != 0 ? -1
                : v->now ? (int)when : v->k->when;      /* (v->now NULL: taken out of the file) */
            if (r < 0) {
                snprintf(msg, sizeof msg, GUI_T("[%s] %s couldn't be set: %s"), v->sec, v->key,
                         why[0] ? why : GUI_T("GLOS refused it"));
                show_val(v);
                gui_message(win, GUI_T("Not Applied"), msg, GUI_MB_OK);
                return 0;
            }
            say_value(v->now ? "set" : "unset", v);
            if (v->k->type == CFG_MODE && r == CFG_NOW) {
                mode = v;
                old_mode = dup(v->was);
            } else if (r > CFG_NOW)
                later = 1;
            free(v->was);
            v->was = dup(v->now);
            n++;
        }
    if (mode && strcmp(val_text(mode), "none")) {
        if (try_mode()) {
            printf("CONTROL mode kept %s\n", val_text(mode));
        } else {                                /* the mode before, again */
            glos_setting_set(mode->sec, mode->key, old_mode, 0, why, sizeof why);       /* (NULL: out of the file) */
            free(mode->now);
            free(mode->was);
            mode->now = dup(old_mode);
            mode->was = dup(old_mode);
            for (i = 0; i < nctl; i++)
                if (ctls[i].k == mode->k)
                    ctl_load(&ctls[i]);
            printf("CONTROL mode back %s\n", val_text(mode));
        }
        fflush(stdout);
    }
    free(old_mode);
    changes_made += n;
    gui_set_text(status, !n       ? GUI_T("Nothing was changed.")
                         : later ? GUI_T("Applied. Some changes take effect later (see each one).")
                                 : GUI_T("Applied."));
    return 1;
}

static void on_button(struct gui_widget *w, int what)
{
    (void)what;
    if (w->id == GUI_OK) {
        if (apply())
            gui_quit(0);
    } else if (w->id == GUI_CANCEL_R) {
        gui_quit(0);
    } else {
        apply();
    }
}

/* ---- the window */

static void read_file(void)
{
    char buf[2048], *p, *e;
    int n, i;
    buf[0] = 0;
    n = glos_setting_sections(buf, sizeof buf);
    if (n != 0 && n != GLOS_E_ROOM)
        buf[0] = 0;
    for (p = buf; *p; p = e) {
        e = strchr(p, '\n');
        if (e)
            *e++ = 0;
        else
            e = p + strlen(p);
        if (!strncmp(p, "program ", 8) && nprogs < MAXPROG) {
            for (i = 0; i < nprogs && !same(progs[i], p + 8); i++)
                ;
            if (i == nprogs)
                snprintf(progs[nprogs++], sizeof progs[0], "%s", p + 8);
        }
    }
    buf[0] = 0;
    n = glos_setting_keys("open", buf, sizeof buf);
    if (n != 0 && n != GLOS_E_ROOM)
        buf[0] = 0;
    for (p = buf; *p; p = e) {
        char *eq;
        e = strchr(p, '\n');
        if (e)
            *e++ = 0;
        else
            e = p + strlen(p);
        if ((eq = strstr(p, " = ")) != NULL) {
            *eq = 0;
            val_of("open", p);
        }
    }
}

int main(int argc, char **argv)
{
    static const char *const heads[2] = { GUI_N("Kind"), GUI_N("Opened by") };
    static const int widths[2] = { 70, 200 };
    struct gui_widget *box, *page, *form, *row, *left, *b, *side = NULL;
    struct ctl *wall = NULL, *style = NULL, *saver, *run, *zone;
    int s, i;
    if (gui_init(argc, argv) != 0) {
        printf("CONTROL no GLOS\n");
        return 1;
    }
    modes_read();
    win = gui_window(GUI_T("Control Panel"), 600, 440, 0); /* (as large as 640x480 allows) */
    win->on_key = on_key;
    box = gui_vbox(win->root, 6, 8);
    gui_grow(box, 1);
    row = gui_hbox(box, 6, 0);
    gui_label(row, GUI_T("Find a setting (Ctrl+F):"));
    find = gui_field(row, 24);
    gui_on(find, find_on, 0);
    tabs = gui_tabs(box);
    gui_tabs_style(tabs, GUI_TABS_LIST);        /* (the pages' names down the left: phase D) */
    gui_grow(tabs, 1);
    for (s = 0; cfg_sections[s]; s++) {
        const char *sec = cfg_sections[s];
        page = gui_tab(tabs, GUI_T(titles[s]));
        if (!strcmp(sec, "open")) {
            types_page = s;
            gui_label(page, GUI_T("Kinds of file, and the program the file manager opens each with:"));
            types_list = gui_list(page, 2, heads, widths);
            gui_grow(types_list, 1);
            row = gui_hbox(page, 6, 0);
            gui_space(row, 0, 0);
            gui_button(row, GUI_T("&Add..."), types_add, 1);
            gui_button(row, GUI_T("&Change..."), types_add, 2);
            gui_button(row, GUI_T("&Remove"), types_del, 3);
            continue;
        }
        if (!strcmp(sec, "program")) {
            prog_page = s;
            row = gui_hbox(page, 10, 0);
            gui_grow(row, 1);
            left = gui_vbox(row, 4, 0);
            gui_label(left, GUI_T("Pro&grams:"));
            prog_list = gui_list(left, 1, NULL, NULL);
            gui_size(prog_list, 130, 0);
            gui_grow(prog_list, 1);
            gui_on(prog_list, prog_select, 0);
            b = gui_hbox(left, 6, 0);
            gui_button(b, GUI_T("&New..."), prog_new, 0);
            prog_remove = gui_button(b, GUI_T("&Remove"), prog_del, 0);
            form = gui_form(row);
            gui_grow(form, 1);
        } else if (!strcmp(sec, "desktop")) {   /* its settings, and the desktop's look beside them (look.c) */
            row = gui_hbox(page, 10, 0);
            gui_grow(row, 1);
            form = gui_form(row);
            gui_grow(form, 1);
            side = gui_vbox(row, 6, 0);
        } else {
            form = gui_form(page);
        }
        field_chars = side && !strcmp(sec, "desktop") ? 20 : 30;        /* (the look takes room beside that page's) */
        saver = run = zone = NULL;
        for (i = 0; i < cfg_schema_n; i++)
            if (!strcmp(cfg_schema[i].section, sec) && nctl < MAXCTL) {
                struct ctl *c = ctl_add(form, &cfg_schema[i], s);
                if (!strcmp(sec, "saver") && !strcmp(c->k->key, "saver"))
                    saver = c;
                if (!strcmp(sec, "services") && !strcmp(c->k->key, "run"))
                    run = c;
                if (!strcmp(sec, "time") && !strcmp(c->k->key, "zone"))
                    zone = c;
                if (!strcmp(sec, "desktop") && !strcmp(c->k->key, "wallpaper"))
                    wall = c;
                if (!strcmp(sec, "desktop") && !strcmp(c->k->key, "wallpaper_style"))
                    style = c;
            }
        if (!strcmp(sec, "net"))
            status_add_net(page);               /* (how the network is now, under its settings: status.c) */
        if (!strcmp(sec, "joystick"))
            status_add_pads(page);              /* (the pads as they are, for a test) */
        if (!strcmp(sec, "input"))
            status_add_input(page);             /* (a field and a place to try the keys and the mouse) */
        if (!strcmp(sec, "sound"))
            status_add_sound(page);             /* (two notes, to hear that there is sound) */
        if (!strcmp(sec, "usb"))
            status_add_usb(page);               /* (the controllers and what is plugged in) */
        if (!strcmp(sec, "display"))
            status_add_display(page);           /* (the card, its memory and the mode now) */
        if (zone)
            status_add_zone(page, zone->w);     /* (the time zone chosen by place) */
        if (!strcmp(sec, "time"))
            status_add_time(page);              /* (the clock now, set by hand, the server asked) */
        if (run)
            status_add_services(win, page, run->w);     /* (its programs as a list, Add... and Remove) */
        if (saver)
            status_add_saver(tabs, s, page, saver->w, saver->choice, argv[0]);  /* (the saver chosen, running small) */
    }
    if (side && wall && style)
        look_add(side, win, wall->w, style->w, style->choice);
    help = gui_label(box, "");
    when_label = gui_label(box, "");
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), on_button, GUI_OK)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), on_button, GUI_CANCEL_R)->flags |= GUI_CANCEL;
    gui_button(row, GUI_T("A&pply"), on_button, 3);
    status = gui_status(win);

    read_file();
    for (s = 0; cfg_sections[s]; s++)
        if (s != prog_page)
            load_page(s);
    prog_fill();
    if (nprogs)
        gui_list_set(prog_list, 0);
    prog_choose(nprogs ? 0 : -1);
    types_fill();
    for (s = 0; argc > 1 && cfg_sections[s]; s++)  /* CONTROL SECTION: at that page (the task bar's clock: time) */
        if (!strcasecmp(argv[1], cfg_sections[s]))
            gui_tabs_set(tabs, s);
    gui_show(win, 1);
    gui_idle(help_poll, NULL, 100);
    printf("CONTROL start\n");
    fflush(stdout);
    gui_run();
    printf("CONTROL end\n");
    return 0;
}
