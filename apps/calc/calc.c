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
 * View chooses the mode (phase E, docs/v1/apps/calculator.md). Scientific: the keys build an expression, shown
 * over the display and worked out by = with the usual order of operations and brackets (calcmath.c); a function
 * key works on the number in the display at once, or on the bracket just closed; Inv turns sin, cos, tan into
 * their inverses, ln into exp, log into 10^x and x^2 into the square root; Deg goes round degrees, radians and
 * grads. Its keys:
 *
 *   ( ) ^            brackets, x^y            s c t     sin cos tan (after i: their inverses)
 *   l n              log, ln                  p  !      pi, n!
 *   i                Inv                      m         mod
 *
 * "CALC ..." lines on stdout give each result (the suite's). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#include "calcmath.h"

#define DIGITS 15

static struct gui_window *win;
static struct gui_widget *display, *mem_label;
static char entry[40];                  /* what is being typed ("" : none: the display shows `value`) */
static double value, operand, memory;
static int op, has_mem, error, fresh;   /* op: the pending operation; fresh: an operand is wanted */

enum { K_C = 100, K_CE, K_BACK, K_SIGN, K_DOT, K_EQ, K_SQRT, K_PCT, K_INV, K_MC, K_MR, K_MS, K_MP,
       K_LP, K_RP, K_POW, K_MOD, K_PI, K_E, K_EXP, K_INVERSE, K_ANGLE, K_FN };   /* (K_FN + n: fns[n]) */
enum { C_COPY = 1, C_PASTE, C_ABOUT, C_STANDARD, C_SCIENTIFIC };
enum { M_STANDARD, M_SCIENTIFIC };

/* ---- the scientific mode's state: the expression so far (it ends with an operator, an open bracket, or, when
   `have`, a bracket just closed, which is an operand already there), how many brackets are open, whether an
   operand has come since the last operator (typed, worked out or recalled), the angles' unit, Inv */
static int mode;
static char sx[256];
static int have, depth, pending, angle, inverse;
static struct gui_widget *expr_label, *angle_key, *inverse_key;
static const char *const fns[] = { "sin", "cos", "tan", "sinh", "cosh", "tanh", "ln", "log", "exp", "sqr", "fact",
                                   "cbrt", "abs" };

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
    gui_set_text(mem_label, has_mem ? GUI_T("M") : "");
    if (expr_label)
        gui_set_text(expr_label, sx);
    if (angle_key)
        gui_set_text(angle_key, angle == CALC_DEG ? GUI_T("Deg") : angle == CALC_RAD ? GUI_T("Rad") : GUI_T("Grad"));
    if (inverse_key)
        gui_set_text(inverse_key, inverse ? GUI_T("INV") : GUI_T("Inv"));
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

/* ---- the scientific mode */

static void sx_add(const char *t)
{
    if (strlen(sx) + strlen(t) < sizeof sx)
        strcat(sx, t);
}

/* The operand in the display joins the expression: as it was typed, or the value in full (a negative one in
   brackets: "-2 ^ 2" would be read as -(2 ^ 2)). */
static void sx_operand(void)
{
    char t[40];
    if (entry[0] && entry[0] != '-')
        snprintf(t, sizeof t, "%s", entry);
    else if (entry[0])
        snprintf(t, sizeof t, "(%s)", entry);
    else
        snprintf(t, sizeof t, value < 0 ? "(%.17g)" : "%.17g", value);
    sx_add(t);
}

/* Where the last bracket of the expression, just closed, began (its function's name with it), or -1. */
static int sx_group(void)
{
    int i = (int)strlen(sx) - 1, d = 0;
    for (; i >= 0; i--)
        if (sx[i] == ')') {
            d++;
        } else if (sx[i] == '(' && --d == 0) {
            while (i > 0 && sx[i - 1] >= 'a' && sx[i - 1] <= 'z')
                i--;
            return i;
        }
    return -1;
}

static void sci_error(int e) { error = e == CALC_E_DIV0 ? 1 : 2; }

static void sci_fn(const char *name)
{
    double v = 0;
    int e, at;
    if (have && (at = sx_group()) >= 0) {       /* of the bracket just closed: its name goes before it */
        size_t n = strlen(name);
        if (strlen(sx) + n < sizeof sx) {
            memmove(sx + at + n, sx + at, strlen(sx + at) + 1);
            memcpy(sx + at, name, n);
        }
        if ((e = calc_eval(sx + at, angle, &v)) != CALC_OK)
            sci_error(e);
        else
            value = v;
        return;
    }
    if ((e = calc_fn(name, current(), angle, &v)) != CALC_OK) {
        sci_error(e);
        return;
    }
    value = v;
    entry[0] = 0;
    fresh = pending = 1;
}

/* A key in the scientific mode: 1 if it was this mode's to do, 0 if it is done as in the standard one. */
static int sci_press(int k)
{
    size_t n = strlen(sx);
    double v = 0;
    int e;
    if (k == K_INVERSE) {
        inverse = !inverse;
        return 1;
    }
    if (k == K_ANGLE) {
        angle = (angle + 1) % 3;
        return 1;
    }
    if ((k >= '0' && k <= '9') || k == K_DOT) {
        if (have)                               /* (a number after a closed bracket: another sum) */
            sx[0] = 0, have = depth = 0;
        pending = 1;
        return 0;
    }
    if (k == K_MR) {
        pending = pending || has_mem;
        return 0;
    }
    if (k == K_EXP) {                           /* the exponent's place in the number being typed */
        size_t m = strlen(entry);
        if (m && !fresh && !strchr(entry, 'e') && m < DIGITS)
            strcat(entry, "e");
        return 1;
    }
    if (k == K_SIGN && entry[0] && !fresh && strchr(entry, 'e')) {     /* (the exponent's sign) */
        char *x = strchr(entry, 'e') + 1;
        if (*x == '-')
            memmove(x, x + 1, strlen(x));
        else if (strlen(entry) < sizeof entry - 2)
            memmove(x + 1, x, strlen(x) + 1), *x = '-';
        return 1;
    }
    if (k == '+' || k == '-' || k == '*' || k == '/' || k == K_POW || k == K_MOD) {
        char op[4] = { ' ', (char)(k == K_POW ? '^' : k == K_MOD ? '%' : k), ' ', 0 };
        if (have || pending || !n || sx[n - 1] == '(') {
            if (!have)
                sx_operand();
            sx_add(op);
        } else if (n >= 3) {                    /* (another operator at once: instead of the last) */
            strcpy(sx + n - 3, op);
        }
        have = pending = 0;
        entry[0] = 0;
        fresh = 1;
        return 1;
    }
    if (k == K_LP) {
        if (!have && !pending && depth < 20) {  /* (where an operand is due) */
            sx_add("(");
            depth++;
        }
        return 1;
    }
    if (k == K_RP) {
        int at;
        if (!depth)
            return 1;
        if (!have)
            sx_operand();
        sx_add(")");
        depth--;
        have = 1, pending = 0;
        entry[0] = 0;
        fresh = 1;
        if ((at = sx_group()) >= 0 && (e = calc_eval(sx + at, angle, &v)) == CALC_OK)
            value = v;                          /* (the bracket's value, to look at) */
        return 1;
    }
    if (k == K_EQ) {
        if (!have)
            sx_operand();
        for (; depth; depth--)
            sx_add(")");
        if ((e = calc_eval(sx, angle, &v)) != CALC_OK)
            sci_error(e);
        else
            value = v;
        sx[0] = 0;
        have = pending = 0;                     /* (the result is there for an operator; a bracket may begin too) */
        entry[0] = 0;
        fresh = 1;
        said("=");
        return 1;
    }
    if (k == K_C) {
        sx[0] = 0;
        have = depth = pending = inverse = 0;
        return 0;                               /* (and the rest as ever) */
    }
    if (k == K_PI || k == K_E) {
        if (have)
            sx[0] = 0, have = depth = 0;
        value = k == K_PI ? 3.14159265358979323846 : 2.71828182845904523536;
        entry[0] = 0;
        fresh = pending = 1;
        return 1;
    }
    if (k >= K_FN || k == K_SQRT || k == K_INV) {
        const char *name = k == K_SQRT ? "sqrt" : k == K_INV ? "inv" : fns[k - K_FN];
        if (inverse) {                          /* Inv: the other way */
            if (!strcmp(name, "sin") || !strcmp(name, "cos") || !strcmp(name, "tan")) {
                static char inv_name[8];
                snprintf(inv_name, sizeof inv_name, "a%s", name);
                name = inv_name;
            } else if (!strcmp(name, "ln")) {
                name = "exp";
            } else if (!strcmp(name, "sqr")) {
                name = "sqrt";
            } else if (!strcmp(name, "log")) {  /* 10^x: no name for it */
                if (!have) {
                    value = pow(10, current());
                    entry[0] = 0;
                    fresh = pending = 1;
                    inverse = 0;
                    return 1;
                }
            }
            inverse = 0;
        }
        sci_fn(name);
        said(name);
        return 1;
    }
    if (k == K_PCT) {                           /* (a hundredth of it: no "per cent of" here) */
        value = current() / 100;
        entry[0] = 0;
        fresh = pending = 1;
        return 1;
    }
    return 0;
}

static void press(int k)
{
    size_t n = strlen(entry);
    if (error && k != K_C && k != K_CE)
        return;
    if (mode == M_SCIENTIFIC && sci_press(k)) {
        refresh();
        return;
    }
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
    if (mode == M_SCIENTIFIC) {
        static const char keys_of[] = "sctln";  /* sin cos tan log ln, as fns has them */
        static const int fn_of[] = { 0, 1, 2, 7, 6 };
        const char *q = e->ch && e->ch < 128 ? strchr(keys_of, (int)e->ch) : NULL;
        if (q) {
            press(K_FN + fn_of[q - keys_of]);
            return 1;
        }
        switch (e->ch) {
        case '(': press(K_LP); return 1;
        case ')': press(K_RP); return 1;
        case '^': press(K_POW); return 1;
        case 'm': press(K_MOD); return 1;
        case 'p': press(K_PI); return 1;
        case '!': press(K_FN + 10); return 1;
        case 'i': press(K_INVERSE); return 1;
        }
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

static void build(void);

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
            if (mode == M_SCIENTIFIC && calc_eval(c, angle, &v) != CALC_OK)    /* (an expression, here) */
                v = atof(c);
            show_number(v, entry, sizeof entry);
            fresh = pending = 1;
            free(c);
            refresh();
        }
    } else if (id == C_ABOUT) {
        gui_message(win, GUI_T("About Calculator"), GUI_T("Calculator: GLOS's pocket calculator."), GUI_MB_OK);
    } else if ((id == C_STANDARD || id == C_SCIENTIFIC) && id - C_STANDARD != mode) {
        struct gui_window *old = win;           /* another mode: its window made, this one gone after */
        mode = id - C_STANDARD;
        sx[0] = 0;
        have = depth = inverse = pending = 0;
        build();
        gui_show(win, 1);
        refresh();
        gui_close_later(old);
        printf("CALC mode %s\n", mode == M_SCIENTIFIC ? "scientific" : "standard");
        fflush(stdout);
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

/* The scientific mode's keys, left of the others: six rows of four, as those have six. */
static const struct { const char *t; int k; } sci_keys[6][4] = {
    { { "Deg", K_ANGLE }, { "Inv", K_INVERSE }, { "(", K_LP }, { ")", K_RP } },
    { { "sin", K_FN + 0 }, { "cos", K_FN + 1 }, { "tan", K_FN + 2 }, { "pi", K_PI } },
    { { "sinh", K_FN + 3 }, { "cosh", K_FN + 4 }, { "tanh", K_FN + 5 }, { "e", K_E } },
    { { "ln", K_FN + 6 }, { "log", K_FN + 7 }, { "exp", K_FN + 8 }, { "Exp", K_EXP } },
    { { "x^2", K_FN + 9 }, { "x^y", K_POW }, { "n!", K_FN + 10 }, { "Mod", K_MOD } },
    { { "cbrt", K_FN + 11 }, { "abs", K_FN + 12 }, { "", 0 }, { "", 0 } },
};

/* The mode's window (the standard one's is as it always was: the tests press its keys by their places). */
static void build(void)
{
    struct gui_widget *box, *row, *b, *pads, *pad;
    struct gui_menubar *bar;
    struct gui_menu *m;
    int r, c;
    win = gui_window(GUI_T("Calculator"), 0, 0, GUI_W_FIXED);
    gui_window_icon(win, "app-calc");
    win->on_command = command;
    win->on_key = key;
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&Edit"));
    gui_menu_item(m, GUI_T("&Copy"), C_COPY, GUI_T("Ctrl+C"));
    gui_menu_item(m, GUI_T("&Paste"), C_PASTE, GUI_T("Ctrl+V"));
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_item(m, GUI_T("&Standard"), C_STANDARD, NULL);
    gui_menu_item(m, GUI_T("S&cientific"), C_SCIENTIFIC, NULL);
    gui_menu_check(m, C_STANDARD + mode, 1);
    m = gui_menu(bar, GUI_T("&Help"));
    gui_menu_item(m, GUI_T("&About Calculator"), C_ABOUT, NULL);
    box = gui_vbox(win->root, 4, 8);
    expr_label = angle_key = inverse_key = NULL;
    if (mode == M_SCIENTIFIC) {
        expr_label = gui_label(box, "");
        gui_size(expr_label, 0, 16);
    }
    row = gui_hbox(box, 4, 0);
    mem_label = gui_label(row, "");
    gui_size(mem_label, 16, 0);
    display = gui_label(row, "0");
    gui_label_style(display, gui_bold, GFX_NONE, 1);
    gui_label_box(display, 1);
    gui_grow(display, 1);
    gui_size(display, 0, 26);
    pads = gui_hbox(box, 8, 0);
    if (mode == M_SCIENTIFIC) {
        pad = gui_vbox(pads, 4, 0);
        for (r = 0; r < 6; r++) {
            row = gui_hbox(pad, 4, 0);
            for (c = 0; c < 4; c++) {
                if (!sci_keys[r][c].t[0]) {
                    gui_space(row, 44, 26);
                    continue;
                }
                b = gui_button(row, sci_keys[r][c].t, clicked, sci_keys[r][c].k);
                b->flags |= GUI_NOTAB;
                gui_size(b, 44, 26);
                if (sci_keys[r][c].k == K_ANGLE)
                    angle_key = b;
                if (sci_keys[r][c].k == K_INVERSE)
                    inverse_key = b;
            }
        }
    }
    pad = gui_vbox(pads, 4, 0);
    for (r = 0; r < 6; r++) {
        row = gui_hbox(pad, 4, 0);
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
}

int main(int argc, char **argv)
{
    if (gui_init(argc, argv) != 0) {
        printf("CALC no GLOS\n");
        return 1;
    }
    if (argc > 1 && !strcmp(argv[1], "-s"))     /* CALC -s: scientific from the start */
        mode = M_SCIENTIFIC;
    build();
    gui_show(win, 1);
    refresh();
    gui_run();
    printf("CALC end\n");
    return 0;
}
