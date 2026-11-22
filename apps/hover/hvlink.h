/* The hovercraft game on GLOS's network (apps/hover/hvlink.c): the datagram ports under the game's messages
   (apps/shared/hvnet.h). One PC opens a game; the others find it by a broadcast and join it; no addresses are
   typed. The opener keeps the game's truth, as hvnet.h says. */
#ifndef HVLINK_H
#define HVLINK_H

#include "../shared/hvnet.h"

#define HV_PORT 18518                   /* the port games are looked for on ('H', 'V') */

enum { HVL_ALONE, HVL_OPEN, HVL_LOOKING, HVL_JOINED, HVL_BROWSING };

/* What a step did (bits). */
#define HVL_SEATS  1                    /* the opener's: a PC joined, left or went quiet */
#define HVL_CRAFT  2                    /* a joiner's: the opener gave it a craft: the caller reads the game's arena
                                           (hvlink_joiner()->arena) and gives it with hvlink_arena, or leaves */
#define HVL_STATE  4                    /* a joiner's: the opener told the game anew (its `ev`: what happened) */
#define HVL_ENDED  8                    /* a joiner's: the opener has gone quiet or sent it away: alone again */
#define HVL_NOBODY 16                   /* nobody answered in ten seconds: alone again */

int hvlink_mode(void);
/* The game opened to the network, begun again: 0; -1 without a network. */
int hvlink_open(const struct hv_arena *a, int arena, int kind, int ncraft, const char *name);
/* A game looked for: 0; -1 without a network. */
int hvlink_look(const char *name);
void hvlink_arena(const struct hv_arena *a);    /* a joiner's: the arena of the game it is in */
void hvlink_leave(void);                /* alone again (a joiner tells the opener; an opener's joiners hear its silence) */
/* A step of the game: the opener's hears its joiners, steps the game for everyone and tells them every other
   step; a joiner's sends its controls and hears the opener. HVL_ bits. */
unsigned hvlink_step(unsigned controls);
struct hv_game *hvlink_game(void);      /* the game as it is (the opener's own; a joiner's view of it) */
int hvlink_craft(void);                 /* the player's craft in it */
int hvlink_seats(void);                 /* the opener's: joiners now */
const struct hv_joiner *hvlink_joiner(void);
const char *hvlink_host(void);          /* a joiner's: the name of the game it found */

/* The lobby's: the network's open games, looked for (a broadcast every second) and listed as they answer; one
   that has not answered for four seconds is gone from the list. hvlink_join_offer joins the one chosen
   (hvlink_step then goes on as after hvlink_look). */
struct hvl_offer {
    unsigned addr, port;
    char name[17];
    int kind, arena, ncraft, free;
    unsigned heard;                     /* glos_ticks() when it last answered */
};
#define HVL_OFFERS 16
int hvlink_browse(void);                /* 0; -1 without a network */
int hvlink_browse_step(void);           /* the list's length now (it changed: the caller draws it again) */
const struct hvl_offer *hvlink_offer(int i);
int hvlink_join_offer(int i, const char *name);         /* 0; -1 no such game */
int hvlink_set_strength(int strength);  /* the opener's: the computer's craft's strength, 0 to 2 */

#endif
