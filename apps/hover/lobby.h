/* Cushion's lobby (apps/hover/lobby.c): a toolkit window before the game, and again after it. */
#ifndef HVLOBBY_H
#define HVLOBBY_H

enum { LOBBY_QUIT, LOBBY_ALONE, LOBBY_OPEN, LOBBY_JOIN };

struct lobby {
    int kind;                           /* in and out: the kind of game (HV_FLAGS ...) */
    int arena;                          /* ... the arena: the game's six, then the user's own */
    int strength;                       /* ... the computer's craft: 0 mild, 1 normal, 2 keen */
    char name[17];                      /* ... the player's name on the network */
    const char *const *arenas;          /* in: the arenas' names */
    int narenas;
    int offer;                          /* out (LOBBY_JOIN): the game chosen in the network's list (hvlink_offer) */
};

/* The lobby until the player chooses: LOBBY_ alone, open to the network, join a game, or quit. The network's
   open games are looked for (hvlink_browse) while it is up; a join leaves the link in hvlink_join_offer's state. */
int lobby_run(struct lobby *l);

#endif
