/* edit_test: the shell's line editor (sdk/shell/edit.c) on the host, against a small terminal of its own: what
 * the editor writes is played on a grid as an xterm would (carriage return, new line, ESC [ K, A, B, C, H and
 * 2J, and the cursor kept at the last column until the next character), and after each step the rows that
 * hold anything and the cursor's place are printed. The Makefile compares the transcript with tests/shell/
 * edit.out. In a step's keys, \e is ESC. */
#include <stdio.h>
#include <string.h>

#include "../../sdk/shell/edit.h"

#define ROWS 8
#define COLS 40

static char grid[ROWS][COLS + 1];
static int cols = 20, row, col, pending, esc, arg;

static void scroll(void)
{
    memmove(grid[0], grid[1], sizeof grid[0] * (ROWS - 1));
    memset(grid[ROWS - 1], ' ', COLS);
    row = ROWS - 1;
}

static void term(void *ctx, const char *s, int n)
{
    (void)ctx;
    for (; n; n--, s++) {
        unsigned char c = (unsigned char)*s;
        if (esc == 1) {
            esc = c == '[' ? 2 : 0;
            arg = 0;
        } else if (esc == 2) {
            if (c >= '0' && c <= '9') {
                arg = arg * 10 + (c - '0');
                continue;
            }
            esc = 0;
            pending = 0;
            if (c == 'K')
                memset(grid[row] + col, ' ', (size_t)(cols - col));
            else if (c == 'A')
                row = row - (arg ? arg : 1) < 0 ? 0 : row - (arg ? arg : 1);
            else if (c == 'B')
                row = row + (arg ? arg : 1) >= ROWS ? ROWS - 1 : row + (arg ? arg : 1);
            else if (c == 'C')
                col = col + (arg ? arg : 1) >= cols ? cols - 1 : col + (arg ? arg : 1);
            else if (c == 'H')
                row = col = 0;
            else if (c == 'J')
                memset(grid, ' ', sizeof grid);
        } else if (c == 27) {
            esc = 1;
        } else if (c == '\r') {
            col = 0, pending = 0;
        } else if (c == '\n') {
            pending = 0;
            if (++row == ROWS)
                scroll();
        } else {
            if (pending) {
                col = 0, pending = 0;
                if (++row == ROWS)
                    scroll();
            }
            grid[row][col] = (char)c;
            if (col == cols - 1)
                pending = 1;
            else
                col++;
        }
    }
}

static void show(const char *what, int event, const struct ed *e)
{
    static const char *const names[] = { "", " ENTER", " UP", " DOWN", " TAB", " BREAK", " END", " CLEAR" };
    int r, last;
    printf("-- %s%s%s%s%s\n", what, names[event], event == 1 || event == 4 ? " line=\"" : "",
           event == 1 || event == 4 ? e->line : "", event == 1 || event == 4 ? "\"" : "");
    for (last = ROWS - 1; last > row && strspn(grid[last], " ") >= COLS; last--)
        ;
    for (r = 0; r <= last; r++) {
        int n = COLS;
        while (n && grid[r][n - 1] == ' ')
            n--;
        printf("  |%.*s%*s|%s\n", n, grid[r], cols - n > 0 ? cols - n : 0, "", r == row ? " <" : "");
    }
    printf("  cursor %d,%d%s\n", row, col, pending ? " (at the row's end)" : "");
}

static int keys(struct ed *e, const char *k)
{
    int event = ED_NOTHING, r;
    for (; *k; k++) {
        unsigned char c = (unsigned char)*k;
        if (c == '\\' && k[1] == 'e')
            c = 27, k++;
        if ((r = ed_key(e, c)) != ED_NOTHING)
            event = r;
    }
    return event;
}

int main(void)
{
    static const struct { const char *what, *keys; } steps[] = {
        { "typed", "dir /w" },
        { "left three times, X", "\\e[D\\e[D\\e[DX" },
        { "Home, Delete, End, Backspace", "\\e[H\\e[3~\\e[F\177" },
        { "Ctrl+U at the end, then a line longer than the row", "\025copy longname.txt c:\\backup" },
        { "Home, then a character put in before it", "\001>" },
        { "Ctrl+E, Ctrl+W, Ctrl+W", "\005\027\027" },
        { "Ctrl+U, then typed to the row's last column", "\025abcdefghijklmnop" },
        { "one more", "q" },
        { "Backspace twice", "\177\177" },
        { "right with Ctrl held (a modifier in the sequence), left, Ctrl+K", "\001\\e[1;5C\\e[C\\e[D\013" },
        { "Enter", "\r\n" },
    };
    struct ed e;
    unsigned i;
    int ev;
    memset(grid, ' ', sizeof grid);
    ed_init(&e, term, 0, cols);
    ed_begin(&e, "C:\\>", 4);
    show("the prompt", 0, &e);
    for (i = 0; i < sizeof steps / sizeof *steps; i++) {
        ev = keys(&e, steps[i].keys);
        show(steps[i].what, ev, &e);
    }
    ed_begin(&e, "\033[1mC:\\WORK>\033[0m", 16);        /* (a prompt with its colours: 8 columns) */
    show("a prompt in bold", 0, &e);
    show("Up", keys(&e, "\\e[A"), &e);
    ed_set(&e, "dir c:\\tools /b");
    show("the history's line", 0, &e);
    show("Tab", keys(&e, " ty\t"), &e);
    ed_set(&e, "dir c:\\tools /b TYPE ");
    show("completed, over the row's end", 0, &e);
    ed_away(&e);
    term(0, "\r\nNOTES.TXT  README.MD\r\n", 24);
    ed_begin(&e, "C:\\WORK>", 8);
    ed_set(&e, "type ");
    show("names listed, the prompt and the line again", 0, &e);
    show("Ctrl+C", keys(&e, "\003"), &e);
    ed_begin(&e, "first row\r\nB>", 13);
    show("a prompt of two rows", 0, &e);
    show("typed, and Home", keys(&e, "xy\001"), &e);
    show("Ctrl+D with something typed: Delete", keys(&e, "\004"), &e);
    show("Ctrl+U, Ctrl+D: the end", keys(&e, "\005\025\004"), &e);
    ed_size(&e, 12);
    cols = 12;
    ed_set(&e, "narrower now, redrawn");
    show("the terminal 12 wide", 0, &e);
    show("Ctrl+L", keys(&e, "\014"), &e);
    return 0;
}
