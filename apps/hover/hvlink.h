/* The hovercraft game on GLOS's network (apps/hover/hvlink.c): the datagram ports under the game's messages
   (apps/shared/hvnet.h). One PC opens a game; the others find it by a broadcast and join it; no addresses are
   typed. The opener keeps the game's truth, as hvnet.h says. */
#ifndef HVLINK_H
#define HVLINK_H

#include "../shared/hvnet.h"

#define HV_PORT 18518                   /* the port games are looked for on ('H', 'V') */

enum { HVL_ALONE, HVL_OPEN, HVL_LOOKING, HVL_JOINED };

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

#endif
