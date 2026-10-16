/* CALC: GLOS's calculator (PRD §10.6; docs/milestones-m9.md step D). The
 * four operations, as a pocket calculator does them (each as it comes:
 * 2 + 3 * 4 = 20), square root, per cent, 1/x, the sign, the memory, the
 * clipboard. By the mouse, or the keyboard:
 *
 *   0-9 .            digits                  Enter =   the result
 *   + - * /          an operation            Esc       clear (C)
 *   %                per cent                Del       the entry (CE)
 *   @  r  F9         square root, 1/x, ±     Backspace a digit
 *   Ctrl+M R P L     memory store, recall, add, clear
 *   Ctrl+C  Ctrl+V   copy the display, paste a number
 *
 * "CALC ..." lines on stdout give each result (the suite's). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#define DIGITS 15

static struct gui_window *win;
static struct gui_widget *display, *mem_label;
static char entry[40];                  /* what is being typed ("" : none: the display shows `value`) */
static double value, operand, memory;
static int op, has_mem, error, fresh;   /* op: the pending operation; fresh: an operand is wanted */

enum { K_C = 100, K_CE, K_BACK, K_SIGN, K_DOT, K_EQ, K_SQRT, K_PCT, K_INV, K_MC, K_MR, K_MS, K_MP };
enum { C_COPY = 1, C_PASTE, C_ABOUT };

/* A double as the display has it: up to 15 significant digits, no trailing zeros, exponent when needed. */
static void show_number(double v, char *out, size_t room)
{
    char *e;
    if (v == 0)
        v = 0;                                  /* (no "-0") */
    if (fabs(v) >= 1e15 || (fabs(v) < 1e-9 && v != 0))
        snprintf(out, room, "%.*g", DIGITS - 5, v);
    else
        snprintf(out, room, "%.*g", DIGITS, v);
    if (strchr(out, '.') && !strchr(out, 'e')) {
        e = out + strlen(out);
        while (e > out && e[-1] == '0')
            *--e = 0;
        if (e > out && e[-1] == '.')
            e[-1] = 0;
    }
}

static void refresh(void)
{
    char t[48];
    if (error)
        snprintf(t, sizeof t, "%s", error == 1 ? "Cannot divide by zero" : "Invalid input");
    else if (entry[0])
        snprintf(t, sizeof t, "%s", entry);
    else
        show_number(value, t, sizeof t);
    gui_set_text(display, t);
    gui_set_text(mem_label, has_mem ? "M" : "");
}

static double current(void) { return entry[0] ? atof(entry) : value; }

static void said(const char *what)
{
    char t[48];
    show_number(value, t, sizeof t);
    printf("CALC %s %s\n", what, error ? "error" : t);
    fflush(stdout);
}

static void apply(void)
{
    double b = current();
    switch (op) {
    case '+': value = operand + b; break;
    case '-': value = operand - b; break;
    case '*': value = operand * b; break;
    case '/':
        if (b == 0) {
            error = 1;
            break;
        }
        value = operand / b;
        break;
    default: value = b;
    }
    op = 0;
    entry[0] = 0;
}

static void press(int k)
{
    size_t n = strlen(entry);
    if (error && k != K_C && k != K_CE)
        return;
    if (k >= '0' && k <= '9') {
        if (fresh)
            entry[0] = 0, n = 0, fresh = 0;
        if (n < DIGITS + 2 && !(n == 1 && entry[0] == '0'))
            entry[n] = (char)k, entry[n + 1] = 0;
        else if (n == 1 && entry[0] == '0')
            entry[0] = (char)k;
    } else if (k == K_DOT) {
        if (fresh || !n)
            strcpy(entry, "0"), fresh = 0, n = 1;
        if (!strchr(entry, '.') && n < DIGITS + 1)
            strcat(entry, ".");
    } else if (k == '+' || k == '-' || k == '*' || k == '/') {
        if (op && !fresh)
            apply();
        else if (!op)
            value = current(), entry[0] = 0;
        if (!error) {
            operand = value;
            op = k;
            fresh = 1;
        }
    } else if (k == K_EQ) {
        if (op)
            apply();
        else
            value = current(), entry[0] = 0;
        fresh = 1;
        said("=");
    } else if (k == K_C) {
        entry[0] = 0;
        value = operand = 0;
        op = error = 0;
        fresh = 0;
    } else if (k == K_CE) {
        entry[0] = 0;
        error = 0;
        if (!op)
            value = 0;
    } else if (k == K_BACK) {
        if (n && !fresh)
            entry[n - 1] = 0;
    } else if (k == K_SIGN) {
        if (entry[0] && !fresh) {
            if (entry[0] == '-')
                memmove(entry, entry + 1, n);
            else if (n < sizeof entry - 2)
                memmove(entry + 1, entry, n + 1), entry[0] = '-';
        } else {
            value = -current();
            entry[0] = 0;
        }
    } else if (k == K_SQRT || k == K_INV || k == K_PCT) {
        double v = current();
        if (k == K_SQRT) {
            if (v < 0)
                error = 2;
            else
                v = sqrt(v);
        } else if (k == K_INV) {
            if (v == 0)
                error = 1;
            else
                v = 1 / v;
        } else {
            v = op ? operand * v / 100 : v / 100;
        }
        if (op) {                               /* (the operand being typed becomes this) */
            show_number(v, entry, sizeof entry);
            fresh = 1;
        } else {
            value = v;
            entry[0] = 0;
            fresh = 1;
        }
        said(k == K_SQRT ? "sqrt" : k == K_INV ? "1/x" : "%");
    } else if (k == K_MC) {
        has_mem = 0, memory = 0;
    } else if (k == K_MR) {
        if (has_mem) {
            show_number(memory, entry, sizeof entry);
            fresh = 1;
        }
    } else if (k == K_MS) {
        memory = current(), has_mem = memory != 0;
    } else if (k == K_MP) {
        memory += current(), has_mem = memory != 0;
    }
    refresh();
}

static void clicked(struct gui_widget *w, int what)
{
    (void)what;
    press(w->id);
    if (win->focus) {                           /* (the keys are the keyboard's own: no button keeps Enter) */
        gui_damage(win->focus);
        win->focus = NULL;
    }
}

static int key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    if (e->mods & GLOS_MOD_CTRL) {
        switch (e->ch) {
        case 13: press(K_MS); return 1;         /* Ctrl+M */
        case 18: press(K_MR); return 1;         /* Ctrl+R */
        case 16: press(K_MP); return 1;         /* Ctrl+P */
        case 12: press(K_MC); return 1;         /* Ctrl+L */
        }
        return 0;
    }
    if (e->ch >= '0' && e->ch <= '9') {
        press((int)e->ch);
        return 1;
    }
    switch (e->ch) {
    case '+': case '-': case '*': case '/': press((int)e->ch); return 1;
    case '.': case ',': press(K_DOT); return 1;
    case '=': case 13: press(K_EQ); return 1;
    case '%': press(K_PCT); return 1;
    case '@': press(K_SQRT); return 1;
    case 'r': case 'R': press(K_INV); return 1;
    case 27: press(K_C); return 1;
    case 8: press(K_BACK); return 1;
    }
    if (e->key == GUI_K_DEL) {
        press(K_CE);
        return 1;
    }
    if (e->key == 0x43) {                       /* F9 */
        press(K_SIGN);
        return 1;
    }
    return 0;
}

static void command(struct gui_window *w, int id)
{
    char t[48];
    (void)w;
    if (id == C_COPY) {
        snprintf(t, sizeof t, "%s", gui_get_text(display));
        gui_clip_set(t, strlen(t));
    } else if (id == C_PASTE) {
        size_t n;
        char *c = gui_clip_get(&n);
        if (c) {
            double v = atof(c);
            show_number(v, entry, sizeof entry);
            fresh = 1;
            free(c);
            refresh();
        }
    } else if (id == C_ABOUT) {
        gui_message(win, "About Calculator", "Calculator: GLOS's pocket calculator.", GUI_MB_OK);
    }
}

static const struct { const char *t; int k; } keys[6][5] = {
    { { "MC", K_MC }, { "MR", K_MR }, { "MS", K_MS }, { "M+", K_MP }, { "sqrt", K_SQRT } },
    { { "C", K_C }, { "CE", K_CE }, { "<-", K_BACK }, { "/", '/' }, { "%", K_PCT } },
    { { "7", '7' }, { "8", '8' }, { "9", '9' }, { "*", '*' }, { "1/x", K_INV } },
    { { "4", '4' }, { "5", '5' }, { "6", '6' }, { "-", '-' }, { "\xC2\xB1", K_SIGN } },
    { { "1", '1' }, { "2", '2' }, { "3", '3' }, { "+", '+' }, { "", 0 } },
    { { "0", '0' }, { "", 0 }, { ".", K_DOT }, { "=", K_EQ }, { "", 0 } },
};

int main(int argc, char **argv)
{
    struct gui_widget *box, *row, *b;
    struct gui_menubar *bar;
    struct gui_menu *m;
    int r, c;
    if (gui_init(argc, argv) != 0) {
        printf("CALC no GLOS\n");
        return 1;
    }
    win = gui_window("Calculator", 0, 0, GUI_W_FIXED);
    win->on_command = command;
    win->on_key = key;
    bar = gui_menubar(win);
    m = gui_menu(bar, "&Edit");
    gui_menu_item(m, "&Copy", C_COPY, "Ctrl+C");
    gui_menu_item(m, "&Paste", C_PASTE, "Ctrl+V");
    m = gui_menu(bar, "&Help");
    gui_menu_item(m, "&About Calculator", C_ABOUT, NULL);
    box = gui_vbox(win->root, 4, 8);
    row = gui_hbox(box, 4, 0);
    mem_label = gui_label(row, "");
    gui_size(mem_label, 16, 0);
    display = gui_label(row, "0");
    gui_label_style(display, gui_bold, GUI_LINE, 1);
    gui_label_box(display, 1);
    gui_grow(display, 1);
    gui_size(display, 0, 26);
    for (r = 0; r < 6; r++) {
        row = gui_hbox(box, 4, 0);
        for (c = 0; c < 5; c++) {
            if (!keys[r][c].t[0]) {
                gui_space(row, 44, 26);
                continue;
            }
            b = gui_button(row, keys[r][c].t, clicked, keys[r][c].k);
            b->flags |= GUI_NOTAB;              /* (the keyboard's are the keys themselves) */
            gui_size(b, 44, 26);
        }
    }
    gui_fit(win);
    gui_show(win, 1);
    refresh();
    gui_run();
    printf("CALC end\n");
    return 0;
}
