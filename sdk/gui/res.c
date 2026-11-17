/* Resource files and the widgets' property descriptions (toolkit 2, step C9; docs/v1/toolkit-2.md 8): a window or
 * a dialog as text, loaded by any app (gui_load), written back (gui_save), and the properties each class has, for
 * the property grid and the IDE's form designer.
 *
 * The text is nested blocks, as Delphi's DFM text form:
 *
 *     object Settings: Dialog
 *       Title = "Settings"
 *       object Name: Row
 *         Label = "&Name:"
 *         object NameField: Field
 *           Chars = 20
 *         end
 *       end
 *       object Ok: Button
 *         Text = "&OK"
 *         Default = true
 *         Command = 1
 *       end
 *     end
 *
 * The outer object is the window (Window or Dialog: Title, Width, Height, and Gap and Pad, 0 unless set); the objects in it are widgets
 * in the window's root, each with the properties that are not its class's defaults. A form's rows are Row objects
 * (Label, and what the row holds); a group's widgets and a tab page's are the Group's and the Page's own. Radio
 * buttons that follow each other in one box are a group. Values: "text" (\" \\ \n), numbers, true and false. Every
 * string a user sees goes through GUI_T (in v1 itself; later a language's catalogue). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "guiint.h"

/* ---- the properties every widget has */
static int g_name(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%s", w->name ? w->name : ""); }
static int s_name(struct gui_widget *w, const char *v) { gui_set_name(w, v); return 0; }
static int g_width(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%d", w->pw); }
static int s_width(struct gui_widget *w, const char *v) { gui_size(w, atoi(v), w->ph); return 0; }
static int g_height(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%d", w->ph); }
static int s_height(struct gui_widget *w, const char *v) { gui_size(w, w->pw, atoi(v)); return 0; }
static int g_grow(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%d", w->grow); }
static int s_grow(struct gui_widget *w, const char *v) { gui_grow(w, atoi(v)); return 0; }
static int g_enabled(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, w->flags & GUI_DISABLED ? "false" : "true"); }
static int s_enabled(struct gui_widget *w, const char *v) { gui_enable(w, !strcmp(v, "true")); return 0; }
static int g_visible(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, w->flags & GUI_HIDDEN ? "false" : "true"); }
static int s_visible(struct gui_widget *w, const char *v) { gui_visible(w, !strcmp(v, "true")); return 0; }
static int g_tip(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%s", w->tip ? w->tip : ""); }
static int s_tip(struct gui_widget *w, const char *v) { gui_tip(w, v[0] ? GUI_T(v) : NULL); return 0; }
static int g_id(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%d", w->id); }
static int s_id(struct gui_widget *w, const char *v) { w->id = atoi(v); return 0; }
static int g_cmd(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%d", w->cmd); }
static int s_cmd(struct gui_widget *w, const char *v) { w->cmd = atoi(v); return 0; }

static const struct gui_wprop common[] = {
    { "Name", GUI_PK_TEXT, "", g_name, s_name },
    { "Width", GUI_PK_NUMBER, "0", g_width, s_width },
    { "Height", GUI_PK_NUMBER, "0", g_height, s_height },
    { "Grow", GUI_PK_NUMBER, "0", g_grow, s_grow },
    { "Enabled", GUI_PK_BOOL, "true", g_enabled, s_enabled },
    { "Visible", GUI_PK_BOOL, "true", g_visible, s_visible },
    { "Tip", GUI_PK_TEXT, "", g_tip, s_tip },
    { "Id", GUI_PK_NUMBER, "0", g_id, s_id },
    { "Command", GUI_PK_COMMAND, "0", g_cmd, s_cmd },
};
#define NCOMMON ((int)(sizeof common / sizeof common[0]))

/* ---- each class's own */
static int g_text(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%s", gui_get_text(w)); }
static int s_text(struct gui_widget *w, const char *v) { gui_set_text(w, GUI_T(v)); return 0; }
static int g_gap(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%d", ((struct gui_box *)w)->gap); }
static int s_gap(struct gui_widget *w, const char *v) { ((struct gui_box *)w)->gap = atoi(v) < 0 ? GUI_GAP : atoi(v); return 0; }
static int g_pad(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%d", ((struct gui_box *)w)->pad); }
static int s_pad(struct gui_widget *w, const char *v) { ((struct gui_box *)w)->pad = atoi(v) < 0 ? GUI_PAD : atoi(v); return 0; }
static int g_default(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, w->flags & GUI_DEFAULT ? "true" : "false"); }
static int s_default(struct gui_widget *w, const char *v) { w->flags = !strcmp(v, "true") ? w->flags | GUI_DEFAULT : w->flags & ~GUI_DEFAULT; return 0; }
static int g_cancel(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, w->flags & GUI_CANCEL ? "true" : "false"); }
static int s_cancel(struct gui_widget *w, const char *v) { w->flags = !strcmp(v, "true") ? w->flags | GUI_CANCEL : w->flags & ~GUI_CANCEL; return 0; }
static int g_checked(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, gui_checked(w) ? "true" : "false"); }
static int s_checked(struct gui_widget *w, const char *v) { gui_set_checked(w, !strcmp(v, "true")); return 0; }
static int g_chars(struct gui_widget *w, char *o, int n) { int m; return snprintf(o, (size_t)n, "%d", gui_res_field(w, &m)); }
static int s_chars(struct gui_widget *w, const char *v) { gui_res_field_chars(w, atoi(v)); return 0; }
static int g_mask(struct gui_widget *w, char *o, int n) { int m; gui_res_field(w, &m); return snprintf(o, (size_t)n, m ? "true" : "false"); }
static int s_mask(struct gui_widget *w, const char *v) { gui_field_mask(w, !strcmp(v, "true")); return 0; }
static int g_min(struct gui_widget *w, char *o, int n) { int s, a, b; gui_res_number(w, &s, &a, &b); return snprintf(o, (size_t)n, "%d", a); }
static int s_min(struct gui_widget *w, const char *v) { int s, a, b; gui_res_number(w, &s, &a, &b); gui_res_number_range(w, atoi(v), b); return 0; }
static int g_max(struct gui_widget *w, char *o, int n) { int s, a, b; gui_res_number(w, &s, &a, &b); return snprintf(o, (size_t)n, "%d", b); }
static int s_max(struct gui_widget *w, const char *v) { int s, a, b; gui_res_number(w, &s, &a, &b); gui_res_number_range(w, a, atoi(v)); return 0; }
static int g_value(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%d", gui_value(w)); }
static int s_value(struct gui_widget *w, const char *v) { gui_set_value(w, atoi(v)); return 0; }
static int g_pmax(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%d", gui_res_progress_max(w)); }
static int s_pmax(struct gui_widget *w, const char *v) { gui_progress_set(w, gui_value(w), atoi(v)); return 0; }
static int g_items(struct gui_widget *w, char *o, int n)      /* a combo box's or a choice's items, "|" between */
{
    const char *it[64];
    char **ci;
    int i, k = 0, cnt, chars;
    if (!strcmp(w->cls->name, "combo")) {
        cnt = gui_res_combo_items(w, &ci);
        for (i = 0; i < cnt && i < 64; i++)
            it[i] = ci[i];
    } else {
        cnt = gui_res_choice(w, &chars, it, 64);
    }
    o[0] = 0;
    for (i = 0; i < cnt && i < 64 && k < n - 1; i++)
        k += snprintf(o + k, (size_t)(n - k), "%s%s", i ? "|" : "", it[i]);
    return k;
}
static int s_items(struct gui_widget *w, const char *v)
{
    int combo = !strcmp(w->cls->name, "combo");
    char item[128];
    const char *p = v, *e;
    if (combo)
        gui_combo_clear(w);
    else
        gui_choice_clear(w);
    while (*p) {
        size_t k;
        e = strchr(p, '|');
        k = e ? (size_t)(e - p) : strlen(p);
        if (k >= sizeof item)
            k = sizeof item - 1;
        memcpy(item, p, k), item[k] = 0;
        if (combo)
            gui_combo_add(w, GUI_T(item));
        else
            gui_choice_add(w, GUI_T(item));
        if (!e)
            break;
        p = e + 1;
    }
    return 0;
}
static int g_cchars(struct gui_widget *w, char *o, int n) { const char *it[1]; int c; gui_res_choice(w, &c, it, 0); return snprintf(o, (size_t)n, "%d", c); }
static int s_cchars(struct gui_widget *w, const char *v) { gui_res_choice_chars(w, atoi(v)); return 0; }
static int g_label(struct gui_widget *w, char *o, int n) { return snprintf(o, (size_t)n, "%s", w->prev ? gui_get_text(w->prev) : ""); }
static int s_label(struct gui_widget *w, const char *v) { if (w->prev) gui_set_text(w->prev, GUI_T(v)); return 0; }

static const struct gui_wprop p_box[] = { { "Gap", GUI_PK_NUMBER, "6", g_gap, s_gap }, { "Pad", GUI_PK_NUMBER, "6", g_pad, s_pad } };  /* (GUI_GAP, GUI_PAD) */
static const struct gui_wprop p_text[] = { { "Text", GUI_PK_TEXT, "", g_text, s_text } };
static const struct gui_wprop p_title[] = { { "Title", GUI_PK_TEXT, "", g_text, s_text } };
static const struct gui_wprop p_label[] = { { "Label", GUI_PK_TEXT, "", g_label, s_label } };
static const struct gui_wprop p_button[] = { { "Text", GUI_PK_TEXT, "", g_text, s_text },
    { "Default", GUI_PK_BOOL, "false", g_default, s_default }, { "Cancel", GUI_PK_BOOL, "false", g_cancel, s_cancel } };
static const struct gui_wprop p_check[] = { { "Text", GUI_PK_TEXT, "", g_text, s_text },
    { "Checked", GUI_PK_BOOL, "false", g_checked, s_checked } };
static const struct gui_wprop p_field[] = { { "Chars", GUI_PK_NUMBER, "20", g_chars, s_chars },
    { "Text", GUI_PK_TEXT, "", g_text, s_text }, { "Mask", GUI_PK_BOOL, "false", g_mask, s_mask } };
static const struct gui_wprop p_combo[] = { { "Chars", GUI_PK_NUMBER, "20", g_chars, s_chars },
    { "Items", GUI_PK_TEXT, "", g_items, s_items }, { "Text", GUI_PK_TEXT, "", g_text, s_text } };
static const struct gui_wprop p_number[] = { { "Min", GUI_PK_NUMBER, "0", g_min, s_min },
    { "Max", GUI_PK_NUMBER, "100", g_max, s_max }, { "Value", GUI_PK_NUMBER, "0", g_value, s_value } };
static const struct gui_wprop p_choice[] = { { "Chars", GUI_PK_NUMBER, "10", g_cchars, s_cchars },
    { "Items", GUI_PK_TEXT, "", g_items, s_items }, { "Value", GUI_PK_NUMBER, "-1", g_value, s_value } };
static const struct gui_wprop p_progress[] = { { "Max", GUI_PK_NUMBER, "100", g_pmax, s_pmax },
    { "Value", GUI_PK_NUMBER, "0", g_value, s_value } };

/* Making each: the widget, and where its children go (the same, or a box inside it). */
static struct gui_widget *m_vbox(struct gui_widget *p, struct gui_widget **in) { return *in = gui_vbox(p, -1, -1); }
static struct gui_widget *m_hbox(struct gui_widget *p, struct gui_widget **in) { return *in = gui_hbox(p, -1, -1); }
static struct gui_widget *m_form(struct gui_widget *p, struct gui_widget **in) { return *in = gui_form(p); }
static struct gui_widget *m_row(struct gui_widget *p, struct gui_widget **in) { return *in = gui_row(p, "", NULL); }
static struct gui_widget *m_group(struct gui_widget *p, struct gui_widget **in)
{
    *in = gui_group(p, "");
    return (*in)->parent;
}
static struct gui_widget *m_tabs(struct gui_widget *p, struct gui_widget **in) { return *in = gui_tabs(p); }
static struct gui_widget *m_page(struct gui_widget *p, struct gui_widget **in) { return *in = gui_tab(p, ""); }
static struct gui_widget *m_label(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_label(p, ""); }
static struct gui_widget *m_button(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_button(p, "", NULL, 0); }
static struct gui_widget *m_check(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_check(p, "", 0); }
static struct gui_widget *m_radio(struct gui_widget *p, struct gui_widget **in)
{
    struct gui_widget *first = NULL;            /* (one group with the radio buttons just before it) */
    *in = NULL;
    if (p->last && !strcmp(p->last->cls->name, "check")) {
        int radio;
        struct gui_widget *g;
        gui_res_check(p->last, &radio, &g);
        if (radio)
            first = g ? g : p->last;
    }
    return gui_radio(p, "", first);
}
static struct gui_widget *m_field(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_field(p, 20); }
static struct gui_widget *m_combo(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_combo(p, 20); }
static struct gui_widget *m_spin(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_spin(p, 0, 100, 0); }
static struct gui_widget *m_slider(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_slider(p, 0, 100, 0); }
static struct gui_widget *m_choice(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_choice(p, 10); }
static struct gui_widget *m_progress(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_progress(p, 100); }
static struct gui_widget *m_space(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_space(p, 0, 0); }
static struct gui_widget *m_sep(struct gui_widget *p, struct gui_widget **in) { *in = NULL; return gui_sep(p); }

#define P(a) a, (int)(sizeof a / sizeof a[0])
static const struct gui_wclass classes[] = {
    { "VBox", P(p_box), m_vbox }, { "HBox", P(p_box), m_hbox }, { "Form", NULL, 0, m_form },
    { "Row", P(p_label), m_row }, { "Group", P(p_title), m_group }, { "Tabs", NULL, 0, m_tabs },
    { "Page", P(p_title), m_page }, { "Label", P(p_text), m_label }, { "Button", P(p_button), m_button },
    { "Check", P(p_check), m_check }, { "Radio", P(p_check), m_radio }, { "Field", P(p_field), m_field },
    { "Combo", P(p_combo), m_combo }, { "Spin", P(p_number), m_spin }, { "Slider", P(p_number), m_slider },
    { "Choice", P(p_choice), m_choice }, { "Progress", P(p_progress), m_progress }, { "Space", NULL, 0, m_space },
    { "Separator", NULL, 0, m_sep },
};
#define NCLASSES ((int)(sizeof classes / sizeof classes[0]))

int gui_wclass_count(void) { return NCLASSES; }
const struct gui_wclass *gui_wclass_at(int i) { return i >= 0 && i < NCLASSES ? &classes[i] : NULL; }

const struct gui_wclass *gui_wclass_find(const char *name)
{
    int i;
    for (i = 0; i < NCLASSES; i++)
        if (!strcmp(classes[i].name, name))
            return &classes[i];
    return NULL;
}

/* A widget's class as a resource file has it (a group's inner box and a row's label are their group's and row's:
   NULL for those, as for widgets a file can't describe). */
const struct gui_wclass *gui_wclass_of(struct gui_widget *w)
{
    const char *k = w->cls->name;
    if (!strcmp(k, "box")) {
        if (w->parent && !strcmp(w->parent->cls->name, "group"))
            return NULL;
        if (w->parent && !strcmp(w->parent->cls->name, "form"))
            return gui_wclass_find("Row");
        if (w->parent && !strcmp(w->parent->cls->name, "tabs"))
            return gui_wclass_find("Page");
        return gui_wclass_find(((struct gui_box *)w)->vertical ? "VBox" : "HBox");
    }
    if (!strcmp(k, "label"))
        return w->parent && !strcmp(w->parent->cls->name, "form") ? NULL : gui_wclass_find("Label");
    if (!strcmp(k, "check")) {
        int radio;
        struct gui_widget *g;
        gui_res_check(w, &radio, &g);
        return gui_wclass_find(radio ? "Radio" : "Check");
    }
    if (!strcmp(k, "number")) {
        int slider, a, b;
        gui_res_number(w, &slider, &a, &b);
        return gui_wclass_find(slider ? "Slider" : "Spin");
    }
    if (!strcmp(k, "form")) return gui_wclass_find("Form");
    if (!strcmp(k, "group")) return gui_wclass_find("Group");
    if (!strcmp(k, "tabs")) return gui_wclass_find("Tabs");
    if (!strcmp(k, "button")) return gui_wclass_find("Button");
    if (!strcmp(k, "field")) return gui_wclass_find("Field");
    if (!strcmp(k, "combo")) return gui_wclass_find("Combo");
    if (!strcmp(k, "choice")) return gui_wclass_find("Choice");
    if (!strcmp(k, "progress")) return gui_wclass_find("Progress");
    if (!strcmp(k, "space")) return gui_wclass_find("Space");
    if (!strcmp(k, "sep")) return gui_wclass_find("Separator");
    return NULL;
}

static const struct gui_wprop *prop_of(const struct gui_wclass *c, const char *name)
{
    int i;
    for (i = 0; c && i < c->nprops; i++)
        if (!strcmp(c->props[i].name, name))
            return &c->props[i];
    for (i = 0; i < NCOMMON; i++)
        if (!strcmp(common[i].name, name))
            return &common[i];
    return NULL;
}

int gui_wprop_count(const struct gui_wclass *c) { return NCOMMON + (c ? c->nprops : 0); }

const struct gui_wprop *gui_wprop_at(const struct gui_wclass *c, int i)       /* the class's own first */
{
    if (c && i < c->nprops)
        return &c->props[i];
    i -= c ? c->nprops : 0;
    return i >= 0 && i < NCOMMON ? &common[i] : NULL;
}

int gui_wprop_get(struct gui_widget *w, const char *name, char *out, int room)
{
    const struct gui_wprop *p = prop_of(gui_wclass_of(w), name);
    return p ? p->get(w, out, room) : -1;
}

int gui_wprop_set(struct gui_widget *w, const char *name, const char *value)
{
    const struct gui_wprop *p = prop_of(gui_wclass_of(w), name);
    int r = p ? p->set(w, value) : -1;
    if (r == 0) {
        gui_damage(w);
        if (w->win)
            gui_relayout(w->win);
    }
    return r;
}

/* ---- names */
void gui_set_name(struct gui_widget *w, const char *name)
{
    free(w->name);
    w->name = name && name[0] ? strdup(name) : NULL;
}

const char *gui_name(struct gui_widget *w) { return w->name ? w->name : ""; }

static struct gui_widget *named(struct gui_widget *w, const char *name)
{
    struct gui_widget *c, *f;
    if (w->name && !strcmp(w->name, name))
        return w;
    for (c = w->first; c; c = c->next)
        if ((f = named(c, name)) != NULL)
            return f;
    return NULL;
}

struct gui_widget *gui_named(struct gui_window *w, const char *name) { return w && w->root ? named(w->root, name) : NULL; }

/* ---- reading */
struct rd {
    const char *p;
    int line;
    char *err;
    int errn;
};

static int fail(struct rd *r, const char *what)
{
    if (r->err && r->errn > 0 && !r->err[0])
        snprintf(r->err, (size_t)r->errn, "line %d: %s", r->line, what);
    return -1;
}

static void skip_space(struct rd *r)
{
    for (;;) {
        while (*r->p == ' ' || *r->p == '\t' || *r->p == '\r' || *r->p == '\n')
            r->line += *r->p++ == '\n';
        if (*r->p == '#' || (r->p[0] == '/' && r->p[1] == '/'))
            while (*r->p && *r->p != '\n')
                r->p++;
        else
            return;
    }
}

static int word(struct rd *r, char *out, int room)     /* a name: letters, digits, _ and . */
{
    int n = 0;
    skip_space(r);
    while ((*r->p == '_' || *r->p == '.' || (*r->p >= '0' && *r->p <= '9') || (*r->p >= 'a' && *r->p <= 'z')
            || (*r->p >= 'A' && *r->p <= 'Z') || *r->p == '-') && n < room - 1)
        out[n++] = *r->p++;
    out[n] = 0;
    return n;
}

static int value(struct rd *r, char *out, int room)    /* "text", a number, true, false */
{
    int n = 0;
    skip_space(r);
    if (*r->p != '"')
        return word(r, out, room) ? 0 : fail(r, "a value was wanted");
    r->p++;
    while (*r->p && *r->p != '"' && *r->p != '\n') {
        char c = *r->p++;
        if (c == '\\' && *r->p)
            c = *r->p == 'n' ? '\n' : *r->p, r->p++;
        if (n < room - 1)
            out[n++] = c;
    }
    out[n] = 0;
    if (*r->p != '"')
        return fail(r, "a string has no end");
    r->p++;
    return 0;
}

/* An object's body (after "object Name: Class"): its properties, its objects, its "end". */
static int body(struct rd *r, struct gui_window *win, struct gui_widget *w, struct gui_widget *in,
                const struct gui_wclass *c)
{
    char key[64], val[512];
    for (;;) {
        if (!word(r, key, sizeof key))
            return fail(r, *r->p ? "a property, an object or end was wanted" : "the text ended inside an object");
        if (!strcmp(key, "end"))
            return 0;
        if (!strcmp(key, "object")) {           /* a widget in this one */
            char name[64], cls[64];
            const struct gui_wclass *k;
            struct gui_widget *nw, *nin;
            if (!in)
                return fail(r, "this object holds no others");
            if (!word(r, name, sizeof name))
                return fail(r, "an object's name was wanted");
            skip_space(r);
            if (*r->p++ != ':' || !word(r, cls, sizeof cls))
                return fail(r, "an object's class was wanted (Name: Class)");
            if (!(k = gui_wclass_find(cls)))
                return fail(r, "no such class");
            nw = k->make(in, &nin);
            gui_set_name(nw, name);
            if (body(r, win, nw, nin, k) != 0)
                return -1;
            continue;
        }
        skip_space(r);
        if (*r->p++ != '=')
            return fail(r, "= was wanted");
        if (value(r, val, sizeof val) != 0)
            return -1;
        if (!w) {                               /* the window's own */
            if (!strcmp(key, "Title"))
                gui_title(win, GUI_T(val));
            else if (!strcmp(key, "Width"))
                win->w = atoi(val);
            else if (!strcmp(key, "Height"))
                win->h = atoi(val);
            else if (!strcmp(key, "Gap"))
                ((struct gui_box *)win->root)->gap = atoi(val);
            else if (!strcmp(key, "Pad"))
                ((struct gui_box *)win->root)->pad = atoi(val);
            else
                return fail(r, "a window has no such property");
            continue;
        }
        {
            const struct gui_wprop *p = prop_of(c, key);
            if (!p)
                return fail(r, "no such property");
            p->set(w, val);
        }
    }
}

struct gui_window *gui_load_text(const char *text, char *err, int errroom)
{
    struct rd r;
    char name[64], cls[64];
    struct gui_window *win;
    if (err && errroom > 0)
        err[0] = 0;
    r.p = text, r.line = 1, r.err = err, r.errn = errroom;
    if (!word(&r, name, sizeof name) || strcmp(name, "object") || !word(&r, name, sizeof name)) {
        fail(&r, "\"object Name: Window\" (or Dialog) was wanted");
        return NULL;
    }
    skip_space(&r);
    if (*r.p++ != ':' || !word(&r, cls, sizeof cls) || (strcmp(cls, "Window") && strcmp(cls, "Dialog"))) {
        fail(&r, "the outer object is a Window or a Dialog");
        return NULL;
    }
    win = gui_window("", 300, 200, !strcmp(cls, "Dialog") ? GUI_W_DIALOG : 0);
    if (!win)
        return NULL;
    gui_set_name(win->root, name);
    if (body(&r, win, NULL, win->root, NULL) != 0) {
        gui_close(win);
        return NULL;
    }
    return win;
}

struct gui_window *gui_load(const char *path, char *err, int errroom)
{
    void *f = gui_fopen(path, "rb");
    char *t;
    long n;
    struct gui_window *w;
    if (!f) {
        if (err && errroom > 0)
            snprintf(err, (size_t)errroom, "%s: not found", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    t = n >= 0 ? malloc((size_t)n + 1) : NULL;
    if (!t || fread(t, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(t);
        if (err && errroom > 0)
            snprintf(err, (size_t)errroom, "%s: not read", path);
        return NULL;
    }
    fclose(f);
    t[n] = 0;
    w = gui_load_text(t, err, errroom);
    free(t);
    return w;
}

/* ---- writing: each widget's class and the properties that are not its class's defaults */
struct wr {
    char *o;
    int n, room;
};

static void out(struct wr *x, const char *fmt, const char *a, const char *b, int depth)
{
    int k;
    for (k = 0; k < depth * 2 && x->n < x->room - 1; k++)
        x->o[x->n++] = ' ';
    if (x->n < x->room)
        x->n += snprintf(x->o + x->n, (size_t)(x->room - x->n), fmt, a, b);
    if (x->n > x->room - 1)
        x->n = x->room - 1;
}

static void quoted(char *dst, int room, const char *v, int kind)
{
    int n = 0;
    if (kind != GUI_PK_TEXT) {
        snprintf(dst, (size_t)room, "%s", v);
        return;
    }
    dst[n++] = '"';
    for (; *v && n < room - 3; v++) {
        if (*v == '"' || *v == '\\')
            dst[n++] = '\\';
        if (*v == '\n') {
            dst[n++] = '\\', dst[n++] = 'n';
            continue;
        }
        dst[n++] = *v;
    }
    dst[n++] = '"';
    dst[n] = 0;
}

static void props(struct wr *x, struct gui_widget *w, const struct gui_wclass *c, int depth)
{
    int i;
    char v[512], q[1100];
    for (i = 0; i < gui_wprop_count(c); i++) {
        const struct gui_wprop *p = gui_wprop_at(c, i);
        if (!strcmp(p->name, "Name"))
            continue;                           /* (it is the object's name) */
        p->get(w, v, sizeof v);
        if (!strcmp(v, p->def))
            continue;
        quoted(q, sizeof q, v, p->kind);
        out(x, "%s = %s\n", p->name, q, depth);
    }
}

static void object(struct wr *x, struct gui_widget *w, int depth, int *anon)
{
    const struct gui_wclass *c = gui_wclass_of(w);
    struct gui_widget *k, *in = w;
    char name[32];
    if (!c)
        return;
    if (!w->name)
        snprintf(name, sizeof name, "%s%d", c->name, ++*anon);
    out(x, "object %s: %s\n", w->name ? w->name : name, c->name, depth);
    props(x, w, c, depth + 1);
    if (!strcmp(c->name, "Group"))              /* (its widgets are its inner box's) */
        in = w->first;
    for (k = in ? in->first : NULL; k; k = k->next)
        object(x, k, depth + 1, anon);
    out(x, "end\n%s%s", "", "", depth);
}

int gui_save(struct gui_window *win, char *o, int room)
{
    struct wr x;
    struct gui_widget *k;
    struct gui_box *rb = (struct gui_box *)win->root;
    char q[200];
    int anon = 0;
    x.o = o, x.n = 0, x.room = room;
    if (room > 0)
        o[0] = 0;
    out(&x, "object %s: %s\n", win->root->name ? win->root->name : "Window1", win->flags & GUI_W_DIALOG ? "Dialog" : "Window", 0);
    quoted(q, sizeof q, win->gw.title, GUI_PK_TEXT);
    out(&x, "Title = %s\n%s", q, "", 1);
    if (!(win->flags & GUI_W_DIALOG)) {
        char a[16], b[16];
        snprintf(a, sizeof a, "%d", win->w), snprintf(b, sizeof b, "%d", win->h);
        out(&x, "Width = %s\n%s", a, "", 1);
        out(&x, "Height = %s\n%s", b, "", 1);
    }
    if (rb->gap || rb->pad) {                   /* (a window's root box has none of its own) */
        char a[16], b[16];
        snprintf(a, sizeof a, "%d", rb->gap), snprintf(b, sizeof b, "%d", rb->pad);
        out(&x, "Gap = %s\n%s", a, "", 1);
        out(&x, "Pad = %s\n%s", b, "", 1);
    }
    for (k = win->root->first; k; k = k->next)
        object(&x, k, 1, &anon);
    out(&x, "end\n%s%s", "", "", 0);
    return x.n;
}

/* ---- a widget's properties in a property grid (props.c): the form designer's and the property grid's one
   description. Filled under two groups, the class's own and the common ones; the app hands each GUI_ON_CHANGE of
   the grid to gui_props_apply, which sets the widget's property to the grid's value. */
static int pk_to_grid(int kind)
{
    return kind == GUI_PK_BOOL ? GUI_PROP_BOOL : kind == GUI_PK_NUMBER || kind == GUI_PK_COMMAND ? GUI_PROP_NUMBER
           : GUI_PROP_TEXT;
}

void gui_props_fill(struct gui_widget *grid, struct gui_widget *w)
{
    const struct gui_wclass *c = gui_wclass_of(w);
    int i;
    char v[512];
    if (!c)
        return;
    for (i = 0; i < gui_wprop_count(c); i++) {
        const struct gui_wprop *p = gui_wprop_at(c, i);
        struct gui_prop q;
        if (i == 0)
            gui_prop_group(grid, c->name);
        if (i == c->nprops)
            gui_prop_group(grid, "Common");
        memset(&q, 0, sizeof q);
        p->get(w, v, sizeof v);
        q.kind = pk_to_grid(p->kind);
        q.text = v;
        q.value = q.kind == GUI_PROP_BOOL ? !strcmp(v, "true") : atoi(v);
        q.min = -100000, q.max = 100000;
        gui_prop_add(grid, p->name, NULL, &q);
    }
}

int gui_props_apply(struct gui_widget *grid, struct gui_widget *w)    /* (on the grid's GUI_ON_CHANGE) */
{
    const struct gui_wclass *c = gui_wclass_of(w);
    int id = gui_prop_changed(grid);
    const struct gui_wprop *p = c ? gui_wprop_at(c, id) : NULL;
    const struct gui_prop *q = gui_prop_get(grid, id);
    char v[32];
    if (!p || !q)
        return -1;
    if (q->kind == GUI_PROP_TEXT)
        return gui_wprop_set(w, p->name, q->text ? q->text : "");
    snprintf(v, sizeof v, q->kind == GUI_PROP_BOOL ? (q->value ? "true" : "false") : "%d", q->value);
    return gui_wprop_set(w, p->name, v);
}
