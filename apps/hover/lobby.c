/* Cushion's lobby (lobby.h; the plan's "toolkit window": docs/v1/apps/hovercraft.md): the kind of game, the
 * arena, the computer's craft's strength, the player's name, and the network's open games as a list that
 * keeps itself up to date (hvlink_browse, a look a second). Play alone, Open to the network, Join the game
 * chosen, or Quit. The game itself is DOS-GL's window, made once the lobby has gone (hvcraft.c), and the lobby
 * comes back when the game ends.
 *
 * "HVCRAFT lobby ..." lines on the output (the suite's). */
#include <stdio.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/gui.h>

#include "hvlink.h"
#include "lobby.h"

static struct gui_window *win;
static struct gui_widget *kind_w, *strength_w, *arenas_w, *games_w, *name_w, *join_w, *says;
static struct lobby *L;
static int chosen, shown = -1;
static char shown_sig[HVL_OFFERS * 48];

static const char *const kinds[4] = { GUI_N("Flags"), GUI_N("Beacons"), GUI_N("The lamp"), GUI_N("Laps") };
static const char *const strengths[3] = { GUI_N("Mild"), GUI_N("Normal"), GUI_N("Keen") };

static void say(const char *what)
{
    printf("HVCRAFT lobby %s\n", what);
    fflush(stdout);
}

/* The network's list, made again only when the games in it change (their free craft too). */
static void games_fill(int n)
{
    char sig[sizeof shown_sig], kindt[16], arenat[12], freet[12];
    size_t k = 0;
    int i, keep = gui_list_get(games_w);
    for (i = 0; i < n; i++) {
        const struct hvl_offer *o = hvlink_offer(i);
        k += (size_t)snprintf(sig + k, sizeof sig - k, "%s/%d/%d/%d;", o->name, o->kind, o->arena, o->free);
        if (k >= sizeof sig)
            k = sizeof sig - 1;
    }
    sig[k] = 0;
    if (n == shown && !strcmp(sig, shown_sig))
        return;
    shown = n;
    strcpy(shown_sig, sig);
    gui_list_clear(games_w);
    for (i = 0; i < n; i++) {
        const struct hvl_offer *o = hvlink_offer(i);
        const char *cells[4];
        snprintf(kindt, sizeof kindt, "%s", GUI_T(kinds[o->kind & 3]));
        snprintf(arenat, sizeof arenat, "%d", o->arena + 1);
        snprintf(freet, sizeof freet, "%d of %d", o->free, o->ncraft);
        cells[0] = o->name, cells[1] = kindt, cells[2] = arenat, cells[3] = freet;
        gui_list_add(games_w, cells, NULL, NULL);
    }
    if (n)
        gui_list_set(games_w, keep >= 0 && keep < n ? keep : 0);
    gui_enable(join_w, n > 0);
    gui_set_text(says, n ? GUI_T("Choose a game and Join, or open your own.")
                         : GUI_T("No game is open on the network now."));
    {
        char t[32];
        snprintf(t, sizeof t, "games=%d", n);
        say(t);
    }
}

static void tick(void *arg)
{
    (void)arg;
    games_fill(hvlink_browse_step());
}

/* The choices as they are now, into the caller's. */
static void take(void)
{
    L->kind = gui_value(kind_w);
    L->strength = gui_value(strength_w);
    L->arena = gui_list_get(arenas_w) >= 0 ? gui_list_get(arenas_w) : 0;
    snprintf(L->name, sizeof L->name, "%s", gui_get_text(name_w));
    L->offer = gui_list_get(games_w);
}

static void button(struct gui_widget *b, int what)
{
    (void)what;
    take();
    if (b->id == LOBBY_JOIN && (L->offer < 0 || !hvlink_offer(L->offer)))
        return;
    chosen = b->id;
    gui_close(win);
}

int lobby_run(struct lobby *l)
{
    static const char *const heads[] = { GUI_N("Game"), GUI_N("Kind"), GUI_N("Arena"), GUI_N("Free craft") };
    static const int widths[] = { 110, 70, 46, 70 };
    struct gui_widget *box, *row, *left, *right, *b;
    int i;
    L = l, chosen = LOBBY_QUIT, shown = -1;
    shown_sig[0] = 0;
    win = gui_window(GUI_T("Cushion"), 560, 320, 0);
    gui_window_icon(win, "app-games");
    box = gui_vbox(win->root, 8, 10);
    gui_grow(box, 1);
    row = gui_hbox(box, 8, 0);
    gui_label(row, GUI_T("&Game:"));
    kind_w = gui_choice(row, 10);
    for (i = 0; i < 4; i++)
        gui_choice_add(kind_w, GUI_T(kinds[i]));
    gui_set_value(kind_w, l->kind & 3);
    gui_label(row, GUI_T("The &computer's craft:"));
    strength_w = gui_choice(row, 8);
    for (i = 0; i < 3; i++)
        gui_choice_add(strength_w, GUI_T(strengths[i]));
    gui_set_value(strength_w, l->strength < 0 || l->strength > 2 ? 1 : l->strength);
    row = gui_hbox(box, 10, 0);
    gui_grow(row, 1);
    left = gui_vbox(row, 4, 0);
    gui_label(left, GUI_T("&Arena:"));
    arenas_w = gui_list(left, 1, NULL, NULL);
    gui_size(arenas_w, 170, 0);
    gui_grow(arenas_w, 1);
    for (i = 0; i < l->narenas; i++) {
        const char *cells[1] = { l->arenas[i] };
        gui_list_add(arenas_w, cells, NULL, NULL);
    }
    gui_list_set(arenas_w, l->arena >= 0 && l->arena < l->narenas ? l->arena : 0);
    right = gui_vbox(row, 4, 0);
    gui_grow(right, 1);
    gui_label(right, GUI_T("Games &on the network:"));
    games_w = gui_list(right, 4, heads, widths);
    gui_grow(games_w, 1);
    says = gui_label(right, GUI_T("Looking for games on the network..."));
    row = gui_hbox(box, 8, 0);
    gui_label(row, GUI_T("Your &name:"));
    name_w = gui_field(row, 16);
    gui_set_text(name_w, l->name);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    b = gui_button(row, GUI_T("&Play Alone"), button, LOBBY_ALONE);
    b->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Open to the Ne&twork"), button, LOBBY_OPEN);
    join_w = gui_button(row, GUI_T("&Join"), button, LOBBY_JOIN);
    gui_enable(join_w, 0);
    b = gui_button(row, GUI_T("&Quit"), button, LOBBY_QUIT);
    b->flags |= GUI_CANCEL;
    if (hvlink_browse() != 0) {
        gui_set_text(says, GUI_T("There is no network: the game can be played alone."));
        say("no network");
    } else {
        gui_idle(tick, NULL, 250);
    }
    gui_show(win, 1);
    say("shown");
    gui_run();
    gui_idle(NULL, NULL, 0);
    {
        static const char *const what[] = { "quit", "alone", "open", "join" };
        char t[64];
        snprintf(t, sizeof t, "chose %s kind=%d arena=%d strength=%d", what[chosen & 3], l->kind, l->arena, l->strength);
        say(t);
    }
    return chosen;
}
