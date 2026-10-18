/* CONTROL: the Control Panel (PRD §10.7, D53; docs/milestones-m9.md step G).
 *
 * GLOS.CFG's settings, drawn from the schema (include/glos/config.h): a page
 * for each of its sections, with a widget for each setting by its kind (a
 * check box for on and off, a list to choose from, a field, the screen's
 * modes, lines for a program's environment); the programs' profiles (a list
 * of the [program NAME.EXT] sections, a form for the one chosen, New and
 * Remove); the kinds of file and what opens them ([open]). Under the pages,
 * what the setting with the keyboard does and when a change to it takes
 * effect. Apply checks every change against the schema first, then sets them
 * (GLOS_FN_CONFIG, which writes the file); a new screen mode is tried for
 * fifteen seconds, and goes back unless it is kept. OK applies and closes;
 * Cancel closes.
 *
 * What it changed goes to stdout as "CONTROL ..." lines (the suites'). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/config.h>
#include <glos/gui.h>

#define MAXVAL 256
#define MAXCTL 48
#define MAXPROG 64
#define TRY_SECONDS 15

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
    const char *choice[16];
    int nchoice;
};
static struct ctl ctls[MAXCTL];
static int nctl;

static struct gui_window *win;
static struct gui_widget *tabs, *help, *when_label, *status;
static struct gui_widget *prog_list, *types_list, *prog_remove;
static int prog_page, types_page;
static char progs[MAXPROG][16];
static int nprogs, prog_at = -1;
static int changes_made;

static const char *const modes[] = { "none", "640x480x8", "640x480x16", "640x480x32", "800x600x8", "800x600x16",
                                     "800x600x32", "1024x768x8", "1024x768x16", "1024x768x32", "1280x1024x8",
                                     "1280x1024x16", "1280x1024x32" };
static char mode_extra[24];                     /* the file's mode, if it isn't one of these */

static const char *const titles[] = { "Display", "Input", "Sound", "Network", "Date and Time", "Desktop", "Shell",
                                      "File Types", "Programs" };          /* (cfg_sections' order) */

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
    int i;
    for (i = 0; i < nvals; i++)
        if (same(vals[i].sec, sec) && same(vals[i].key, key))
            return &vals[i];
    if (nvals == MAXVAL || !cfg_find(sec, key))
        return NULL;
    v = &vals[nvals++];
    snprintf(v->sec, sizeof v->sec, "%s", sec);
    snprintf(v->key, sizeof v->key, "%s", key);
    v->k = cfg_find(sec, key);
    if (glos_config_get(sec, key, buf, sizeof buf) == 1) {
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
        if (i == c->nchoice && c->kind == W_MODE && c->nchoice < 16) {
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
        for (; p && c->nchoice < 16; p = e ? e + 1 : NULL) {
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
        if (c->kind == W_MODE)
            for (c->nchoice = 0; c->nchoice < (int)(sizeof modes / sizeof *modes); c->nchoice++)
                c->choice[c->nchoice] = modes[c->nchoice];
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
        c->w = gui_field(row, k->type == CFG_NUMBER ? 8 : k->max > 30 ? 30 : (int)k->max + 1);
    }
    return c;
}

/* ---- what the setting with the keyboard does */

static void help_poll(void *arg)
{
    static const struct cfg_key *shown = (const struct cfg_key *)1;
    static const char *const when[] = { "Takes effect at once.", "Takes effect when GLOS next starts.",
                                        "Takes effect when the program next starts.",
                                        "Takes effect when the desktop next starts.",
                                        "Takes effect when an app next starts." };
    const struct cfg_key *k = NULL;
    int i;
    (void)arg;
    for (i = 0; i < nctl; i++)
        if (win->focus == ctls[i].w)
            k = ctls[i].k;
    if (win->focus == types_list)
        k = cfg_find("open", ".TXT");
    if (k == shown)
        return;
    shown = k;
    gui_set_text(help, k ? k->desc : "");
    gui_set_text(when_label, k ? when[k->when < 5 ? k->when : 0] : "");
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
    if (!gui_ask(win, "New Program", "The program's file name (NAME.EXE):", name, sizeof name))
        return;
    for (i = 0; name[i]; i++)
        if (name[i] >= 'a' && name[i] <= 'z')
            name[i] -= 32;
    snprintf(sec, sizeof sec, "program %s", name);
    if (!name[0] || strlen(name) > 12 || !cfg_section_ok(sec)) {
        gui_message(win, "New Program", "That isn't a DOS program's file name.", GUI_MB_OK);
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
    snprintf(q, sizeof q, "Remove %s's profile? It runs with GLOS's defaults then.", progs[prog_at]);
    if (gui_message(win, "Remove", q, GUI_MB_YESNO) != GUI_YES)
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
        snprintf(q, sizeof q, "The program that opens %s files:", v->key);
        if (!gui_ask(win, "Change", q, prog, sizeof prog))
            return;
        val_put(v, prog);
        types_fill();
        return;
    }
    if (!gui_ask(win, "Add", "The kind of file (.EXT):", ext, sizeof ext))
        return;
    for (i = 0; ext[i]; i++)
        if (ext[i] >= 'a' && ext[i] <= 'z')
            ext[i] -= 32;
    if (ext[0] != '.' || !ext[1] || strlen(ext) > 4 || !(v = val_of("open", ext))) {
        gui_message(win, "Add", "A kind of file is a dot and up to three letters: .TXT", GUI_MB_OK);
        return;
    }
    snprintf(q, sizeof q, "The program that opens %s files:", ext);
    snprintf(prog, sizeof prog, "%s", val_text(v));
    if (!gui_ask(win, "Add", q, prog, sizeof prog))
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

static void try_draw(struct gui_widget *w, struct gfx_surface *s) { (void)w, (void)s; }

static int try_tick(struct gui_widget *w, struct gui_ev *e)
{
    char t[80];
    (void)w;
    if (e->type != GUI_EV_TIMER)
        return 0;
    if (--try_left <= 0) {
        gui_end_modal(try_w, GUI_CANCEL_R);
        return 1;
    }
    snprintf(t, sizeof t, "Keep this screen? It goes back in %d second%s.", try_left, try_left == 1 ? "" : "s");
    gui_set_text(try_text, t);
    return 1;
}

static void try_button(struct gui_widget *w, int what)
{
    (void)what;
    gui_end_modal(try_w, w->id);
}

/* The new screen mode, tried: 1 kept; 0 the old one again. */
static int try_mode(void)
{
    struct gui_widget *box, *row, *tick;
    int r;
    try_w = gui_window("Screen", 0, 0, GUI_W_DIALOG);
    try_w->owner = win;
    box = gui_vbox(try_w->root, 10, 12);
    try_left = TRY_SECONDS;
    try_text = gui_label(box, "Keep this screen? It goes back in 15 seconds.");
    tick = gui_canvas(box, 1, 1, try_draw, try_tick);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, "&Keep", try_button, GUI_OK)->flags |= GUI_DEFAULT;
    gui_button(row, "&Back", try_button, GUI_CANCEL_R)->flags |= GUI_CANCEL;
    gui_timer(tick, 1000);
    r = gui_modal(try_w);
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
                gui_message(win, "Not Applied", msg, GUI_MB_OK);
                return 0;
            }
            if (!*p++ || v->k->type != CFG_ENV)
                break;
        }
    }
    for (pass = 0; pass < 2; pass++)            /* the screen's mode last, to be tried */
        for (i = 0; i < nvals; i++) {
            struct val *v = &vals[i];
            int r;
            if (!val_changed(v) || (v->k->type == CFG_MODE) != (pass == 1))
                continue;
            why[0] = 0;
            r = v->now ? glos_config_set(v->sec, v->key, v->now, why, sizeof why)
                       : glos_config_unset(v->sec, v->key) == 0 ? v->k->when : -1;
            if (r < 0) {
                snprintf(msg, sizeof msg, "[%s] %s couldn't be set: %s", v->sec, v->key, why[0] ? why : "GLOS refused it");
                show_val(v);
                gui_message(win, "Not Applied", msg, GUI_MB_OK);
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
            if (old_mode)
                glos_config_set(mode->sec, mode->key, old_mode, why, sizeof why);
            else
                glos_config_unset(mode->sec, mode->key);
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
    gui_set_text(status, !n ? "Nothing was changed." : later ? "Applied. Some changes take effect later (see each one)."
                                                              : "Applied.");
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
    n = glos_config_sections(buf, sizeof buf - 1);
    buf[n > 0 ? n : 0] = 0;
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
    n = glos_config_keys("open", buf, sizeof buf - 1);
    buf[n > 0 ? n : 0] = 0;
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
    static const char *const heads[2] = { "Kind", "Opened by" };
    static const int widths[2] = { 70, 200 };
    struct gui_widget *box, *page, *form, *row, *left, *b;
    int s, i;
    if (gui_init(argc, argv) != 0) {
        printf("CONTROL no GLOS\n");
        return 1;
    }
    win = gui_window("Control Panel", 600, 420, 0);
    box = gui_vbox(win->root, 6, 8);
    gui_grow(box, 1);
    tabs = gui_tabs(box);
    gui_grow(tabs, 1);
    for (s = 0; cfg_sections[s]; s++) {
        const char *sec = cfg_sections[s];
        page = gui_tab(tabs, titles[s]);
        if (!strcmp(sec, "open")) {
            types_page = s;
            gui_label(page, "Kinds of file, and the program the file manager opens each with:");
            types_list = gui_list(page, 2, heads, widths);
            gui_grow(types_list, 1);
            row = gui_hbox(page, 6, 0);
            gui_space(row, 0, 0);
            gui_button(row, "&Add...", types_add, 1);
            gui_button(row, "&Change...", types_add, 2);
            gui_button(row, "&Remove", types_del, 3);
            continue;
        }
        if (!strcmp(sec, "program")) {
            prog_page = s;
            row = gui_hbox(page, 10, 0);
            gui_grow(row, 1);
            left = gui_vbox(row, 4, 0);
            gui_label(left, "Pro&grams:");
            prog_list = gui_list(left, 1, NULL, NULL);
            gui_size(prog_list, 130, 0);
            gui_grow(prog_list, 1);
            gui_on(prog_list, prog_select, 0);
            b = gui_hbox(left, 6, 0);
            gui_button(b, "&New...", prog_new, 0);
            prog_remove = gui_button(b, "&Remove", prog_del, 0);
            form = gui_form(row);
            gui_grow(form, 1);
        } else {
            form = gui_form(page);
        }
        for (i = 0; i < cfg_schema_n; i++)
            if (!strcmp(cfg_schema[i].section, sec) && nctl < MAXCTL)
                ctl_add(form, &cfg_schema[i], s);
    }
    help = gui_label(box, "");
    when_label = gui_label(box, "");
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, "OK", on_button, GUI_OK)->flags |= GUI_DEFAULT;
    gui_button(row, "Cancel", on_button, GUI_CANCEL_R)->flags |= GUI_CANCEL;
    gui_button(row, "A&pply", on_button, 3);
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
    gui_show(win, 1);
    gui_idle(help_poll, NULL, 100);
    printf("CONTROL start\n");
    fflush(stdout);
    gui_run();
    printf("CONTROL end\n");
    return 0;
}
