/* The terminal's xterm screen (vt.c; M10 step D): what an SSH session's
 * programs draw with escape sequences, as a grid of cells. Plain C, no GUI:
 * the terminal draws it (term.c), the host tests it (tests/host/vt_test.c). */
#ifndef VT_H
#define VT_H

#define VT_COLS_MAX 200
#define VT_ROWS_MAX 100
#define VT_DEFAULT  0xFFFF              /* a cell's colour: the default foreground or background */

enum { VT_BOLD = 1, VT_DIM = 2, VT_ITALIC = 4, VT_UNDER = 8, VT_BLINK = 16, VT_REVERSE = 32, VT_HIDDEN = 64,
       VT_STRIKE = 128 };

struct vt_cell {
    unsigned short ch;                  /* Unicode (the basic plane), ' ' when empty */
    unsigned short fg, bg;              /* 0-255 the xterm palette, 0x1000000 | RGB doesn't fit: direct colours are
                                           taken to the nearest of the 256; VT_DEFAULT */
    unsigned char attr;                 /* VT_ */
};

struct vt {
    int cols, rows;
    struct vt_cell *cell;               /* the screen in use: main or alt */
    struct vt_cell main[VT_COLS_MAX * VT_ROWS_MAX], alt[VT_COLS_MAX * VT_ROWS_MAX];
    int on_alt;
    int x, y, wrap_next;                /* the cursor; a character written in the last column waits to wrap */
    int top, bottom;                    /* the scroll region, rows (inclusive) */
    unsigned short fg, bg;
    unsigned char attr;
    int origin, autowrap, insert, cursor_on, app_cursor, app_keypad, bracketed, reverse_screen, lnm;
    int g0_lines, g1_lines, shift_out;  /* the DEC line-drawing set in G0, G1; SO in force */
    struct { int x, y, fg, bg, attr, origin, wrap_next, g0, g1, so; } saved, saved_alt;
    unsigned char tabs[VT_COLS_MAX];
    /* the parser */
    int state, priv, inter, np, param[16], have_param;
    char osc[128];
    int osc_n;
    unsigned u8, u8_left;               /* a UTF-8 sequence being read */
    unsigned last_ch;                   /* REP's */
    /* for the terminal */
    char title[128];
    int title_set;                      /* the title changed (cleared by the terminal) */
    unsigned char dirty[VT_ROWS_MAX];   /* rows changed since the terminal last drew */
    int bell;
    char reply[64];                     /* bytes to send back (DSR, DA): the terminal sends and clears them */
    int reply_n;
    long scrolled;                      /* lines that went off the top of the main screen, in all */
};

void vt_init(struct vt *t, int cols, int rows);
void vt_resize(struct vt *t, int cols, int rows);       /* what fits is kept, from the top left */
void vt_feed(struct vt *t, const unsigned char *data, int n);
/* A key as the program wants it (xterm's encoding), in out: its length, 0 if the key sends nothing. key is a
   scan code (100h added for E0 keys) as glos_event has it, ch its character, mods GLOS_MOD_. */
int vt_key(const struct vt *t, unsigned key, unsigned ch, unsigned mods, char *out, int room);
unsigned vt_rgb(unsigned short colour, int fg);         /* a cell colour as 0xRRGGBB (fg: the default's) */
int vt_dump(const struct vt *t, char *out, int room);   /* the screen as text, a line a row, trailing blanks off
                                           (the tests'); its length */

#endif
