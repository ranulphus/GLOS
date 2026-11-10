/* Host test for notifications (kernel/wm/notes.c; API 2.0 step 8): the queue and its times, the layout. */
#include <stdio.h>
#include <string.h>
#include "notes.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

#define S 1024u

static int width(const char *t, int len, int bold) { (void)t; return len * (bold ? 7 : 6); }   /* 6 pixels a character */

static struct note mk(const char *title, const char *text, u32 life)
{
    struct note n;
    memset(&n, 0, sizeof n);
    strcpy(n.title, title);
    strcpy(n.text, text);
    n.life = life;
    return n;
}

int main(void)
{
    static struct notes s;
    struct note_news news;
    struct note_layout l;
    struct note n;
    u32 a, b, c, i, now = 100000;

    /* One: shown at the desktop's next turn, for its six seconds, then its app is told it timed out. */
    n = mk("Title", "Text", 0);
    a = notes_add(&s, &n, 7);
    CHECK(a && !notes_shown(&s) && notes_due(&s) == 0);
    CHECK(notes_turn(&s, now, 1, 0, &news) == 1 && !news.id && notes_shown(&s) && notes_shown(&s)->id == a);
    CHECK(notes_due(&s) == now + 6 * S);
    CHECK(notes_turn(&s, now + 3 * S, 1, 0, &news) == 0 && notes_shown(&s));
    CHECK(notes_turn(&s, now + 6 * S, 1, 0, &news) == 1 && news.id == a && news.owner == 7 && news.what == NOTE_TIMEOUT);
    CHECK(!notes_shown(&s) && s.n == 0);

    /* Several: one at a time, in their order; the next comes as the one before goes. Lives of 1 and 90 seconds are 2 and 60. */
    n = mk("A", "", 1);
    a = notes_add(&s, &n, 1);
    n = mk("B", "", 90);
    b = notes_add(&s, &n, 2);
    n = mk("C", "", 5);
    c = notes_add(&s, &n, 1);
    CHECK(a != b && b != c && s.n == 3);
    now += 100 * S;
    notes_turn(&s, now, 1, 0, &news);
    CHECK(notes_shown(&s)->id == a && notes_due(&s) == now + 2 * S);
    CHECK(notes_turn(&s, now + 2 * S, 1, 0, &news) == 1 && news.id == a && notes_shown(&s)->id == b);
    CHECK(notes_due(&s) == now + 2 * S + 60 * S);

    /* The pointer on it: it stays, a second past the pointer's leaving. */
    now += 2 * S + 60 * S - 10;
    CHECK(notes_turn(&s, now, 1, 1, &news) == 0 && notes_due(&s) == now + S);
    now += S + 500;
    CHECK(notes_turn(&s, now, 1, 1, &news) == 0 && notes_shown(&s)->id == b);        /* (still on it) */
    CHECK(notes_turn(&s, now + S - 1, 1, 0, &news) == 0);
    CHECK(notes_turn(&s, now + S, 1, 0, &news) == 1 && news.id == b && news.owner == 2 && notes_shown(&s)->id == c);

    /* A full-screen program takes the screen: the one shown waits, and has what time it had left when it is back. */
    now += S + 2 * S;                                   /* (c has been shown two of its five seconds) */
    CHECK(notes_turn(&s, now, 0, 0, &news) == 1 && !news.id && !notes_shown(&s) && s.n == 1 && notes_due(&s) == 0);
    CHECK(notes_turn(&s, now + 50 * S, 0, 0, &news) == 0 && !notes_shown(&s));
    now += 100 * S;
    CHECK(notes_turn(&s, now, 1, 0, &news) == 1 && notes_shown(&s)->id == c && notes_due(&s) == now + 3 * S);

    /* Pressed: an action, the close box, its body; an action it hasn't got is its body. */
    n = mk("D", "", 0);
    strcpy(n.action[0], "Open");
    n.nactions = 1;
    a = notes_add(&s, &n, 3);
    CHECK(notes_click(&s, NP_NONE, &news) == 0 && notes_shown(&s)->id == c);
    CHECK(notes_click(&s, NP_CLOSE, &news) == 1 && news.id == c && news.what == NOTE_DISMISSED && !notes_shown(&s));
    CHECK(notes_click(&s, NP_BODY, &news) == 0);        /* (none shown) */
    notes_turn(&s, now, 1, 0, &news);
    CHECK(notes_click(&s, NP_ACT2, &news) == 1 && news.id == a && news.what == NOTE_DISMISSED);
    a = notes_add(&s, &n, 3);
    notes_turn(&s, now, 1, 0, &news);
    CHECK(notes_click(&s, NP_ACT1, &news) == 1 && news.id == a && news.owner == 3 && news.what == NOTE_ACTION1 && s.n == 0);

    /* Closed by its app: one waiting, the one shown; an app's end takes all of its. No more than there is room for. */
    for (i = 0; i < NOTES_MAX; i++) {
        n = mk("E", "", 0);
        CHECK(notes_add(&s, &n, 10 + i % 2) != 0);
    }
    CHECK(notes_add(&s, &n, 10) == 0 && s.n == NOTES_MAX);
    notes_turn(&s, now, 1, 0, &news);
    a = s.q[0].id, b = s.q[3].id;
    CHECK(notes_remove(&s, 11, b) == 0 && s.n == NOTES_MAX - 1 && notes_shown(&s)->id == a);
    CHECK(notes_remove(&s, 11, a) == 0 && s.n == NOTES_MAX - 1);        /* (not that app's) */
    CHECK(notes_remove(&s, 10, a) == 1 && !notes_shown(&s) && s.n == NOTES_MAX - 2);
    CHECK(notes_remove(&s, 11, 0) == 0 && s.n == 3);    /* (app 11's three that were left; 10's stay) */
    CHECK(notes_remove(&s, 10, 0) == 0 && s.n == 0);
    notes_turn(&s, now, 1, 0, &news);
    CHECK(!notes_shown(&s) && !news.id);

    /* Layout: a title alone; with text on one line; text wrapped at spaces; too long (cut); a word too long for a
       line; lines of its own; an icon; actions from the right. */
    n = mk("Title", "", 0);
    note_layout(&n, width, &l);
    CHECK(l.w == NOTE_W && l.nlines == 0 && l.text_x == 8 && l.h == 8 + 16 + 2 + 8 && !l.cut);
    CHECK(note_part(&l, 5, 5) == NP_BODY && note_part(&l, NOTE_W - 10, 10) == NP_CLOSE && note_part(&l, -1, 5) == NP_NONE
          && note_part(&l, 5, l.h) == NP_NONE);
    n = mk("Title", "Short text", 0);
    note_layout(&n, width, &l);
    CHECK(l.nlines == 1 && l.line_at[0] == 0 && l.line_len[0] == 10 && l.h == 8 + 14 + 2 + 14 + 8);
    n = mk("T", "one two three four five six seven eight nine ten eleven twelve thirteen", 0);
    note_layout(&n, width, &l);                         /* (224 pixels: 37 characters a line) */
    CHECK(l.nlines == 2 && l.line_at[0] == 0 && l.line_len[0] == 33 && !strncmp(n.text + l.line_at[1], "eight", 5) && !l.cut);
    CHECK(l.line_at[1] + l.line_len[1] == strlen(n.text));
    n = mk("T", "aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll mmmm nnnn oooo pppp qqqq rrrr ssss tttt uuuu "
           "vvvv wwww xxxx", 0);
    note_layout(&n, width, &l);
    CHECK(l.nlines == NOTE_LINES && l.cut && l.line_at[2] + l.line_len[2] < strlen(n.text));
    n = mk("T", "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz", 0);
    note_layout(&n, width, &l);
    CHECK(l.nlines == 2 && l.line_len[0] == 37 && l.line_at[1] == 37 && l.line_len[1] == 15);
    n = mk("T", "first\nsecond", 0);
    note_layout(&n, width, &l);
    CHECK(l.nlines == 2 && l.line_len[0] == 5 && l.line_at[1] == 6 && l.line_len[1] == 6);
    n = mk("T", "x", 0);
    n.has_icon = 1;
    strcpy(n.action[0], "Open");
    strcpy(n.action[1], "Later");
    n.nactions = 2;
    note_layout(&n, width, &l);
    CHECK(l.text_x == 8 + 16 + 6 && l.act[0].w == 4 * 6 + 16 && l.act[1].w == 5 * 6 + 16 && l.act[0].x < l.act[1].x);
    CHECK(l.act[1].x + l.act[1].w == NOTE_W - 8 && l.act[0].x + l.act[0].w + 6 == l.act[1].x && l.h == l.act[0].y + 18 + 8);
    CHECK(note_part(&l, l.act[0].x + 2, l.act[0].y + 2) == NP_ACT1 && note_part(&l, l.act[1].x + 2, l.act[1].y + 17) == NP_ACT2
          && note_part(&l, l.act[0].x - 2, l.act[0].y + 2) == NP_BODY);

    printf("notes_test: %d failures\n", fails);
    return fails != 0;
}
