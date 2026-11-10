/* Notifications (API 2.0's SHELL NOTIFY; docs/v1/shell.md): which is on the screen, for how long, and how one is
 * laid out. One is shown at a time, above the task bar's right end; the rest wait their turn. The desktop draws
 * the one shown (kernel/wm/desk.c) and the API tells apps what became of theirs (kernel/app/api2shell.c). Built
 * for the host too (tests/host/notes_test.c). */
#ifndef K_NOTES_H
#define K_NOTES_H
#include "types.h"

#define NOTES_MAX    8                  /* shown and waiting, all told */
#define NOTE_TITLE   48                 /* its texts' bytes, with their NULs */
#define NOTE_TEXT    128
#define NOTE_ACTION  16
#define NOTE_W       240                /* its width on the screen */
#define NOTE_LINES   3                  /* its text's lines at most */
#define NOTE_LIFE    6                  /* seconds it is shown, unless it says (2 to 60) */

struct note {
    u32 id;                             /* 0: none */
    u32 owner;                          /* the app's id whose it is */
    char title[NOTE_TITLE], text[NOTE_TEXT], action[2][NOTE_ACTION];
    u8 nactions, has_icon;
    u32 icon[16 * 16];                  /* 0xAARRGGBB */
    u32 life;                           /* ticks it is shown for; while it is shown: those left when it was hidden */
};

enum { NOTE_ACTION1 = 1, NOTE_ACTION2, NOTE_DISMISSED, NOTE_TIMEOUT };  /* what became of one (GLOS_NOTIFY_) */

struct note_news {                      /* one is gone from the screen: its app is told */
    u32 id, owner;
    int what;                           /* NOTE_ */
};

struct notes {
    struct note q[NOTES_MAX];           /* the first is the one shown, or the next to be */
    u32 n;
    u32 next_id;
    u8 shown;                           /* q[0] is on the screen ... */
    u32 until;                          /* ... until this tick */
};

u32 notes_add(struct notes *s, const struct note *n, u32 owner);       /* its id, or 0: no room (life in seconds, 0
                                                                           for the usual) */
/* The app closed it (or has ended: id 0, every one of the owner's): 1 if what is on the screen changed. */
int notes_remove(struct notes *s, u32 owner, u32 id);
/* Each turn of the desktop's: now, the tick; can: one may be on the screen (no full-screen program has it);
   over: the pointer is on the one shown (it stays while it is). 1 if what is on the screen changed; *news is
   filled (id not 0) when one's time ran out. */
int notes_turn(struct notes *s, u32 now, int can, int over, struct note_news *news);
/* The one shown was pressed at a part (note_part's): it is gone, and *news says how; 1 if it was. */
int notes_click(struct notes *s, int part, struct note_news *news);
const struct note *notes_shown(const struct notes *s); /* NULL: none on the screen */
u32 notes_due(const struct notes *s);                   /* the tick the one shown goes at, 0 none shown */

/* How one is laid out: sizes in pixels, from the widths of its texts (the caller's fonts). */
struct note_rect {
    s32 x, y, w, h;
};
struct note_layout {
    s32 w, h;
    s32 text_x;                         /* where the title and the text begin (past the icon, if it has one) */
    s32 title_y;
    u32 nlines;
    u32 line_at[NOTE_LINES], line_len[NOTE_LINES];      /* the text's lines: offset and length in its bytes */
    s32 line_y[NOTE_LINES];
    u8 cut;                             /* the text was longer than the lines there are: the last ends "..." */
    struct note_rect close, act[2];     /* the close box; the actions' buttons */
};
enum { NP_NONE, NP_BODY, NP_CLOSE, NP_ACT1, NP_ACT2 };
/* width(text, len, bold): a text's width in pixels. */
void note_layout(const struct note *n, int (*width)(const char *text, int len, int bold), struct note_layout *l);
int note_part(const struct note_layout *l, s32 x, s32 y);       /* NP_: what is at a point of it */

#endif
