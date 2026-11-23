/* The hovercraft game over a network (GLOS v1, phase F; docs/v1/apps/hovercraft.md): what the PCs of a game say
   to each other, and what each does with what it hears. No sockets here: datagrams in and out as bytes, so that
   the host's test can put a network that loses, doubles and reorders them between two copies of the game.

   One PC opens a game and keeps its truth (the opener): it runs the steps for everyone. The others (the
   joiners) send their craft's controls, thirty times a second, and are sent every craft's place and speed and
   the game's score, fifteen times a second. A datagram lost is the next one's to mend: every message says the
   whole of what it is about, none depends on one before it. A PC that has not been heard for three seconds is
   taken over by the computer. Every message begins with the game's mark and version: PCs of different releases
   do not join each other.

   Messages (bytes; numbers low byte first):
     FIND    'H' 'V' ver 1                              a joiner looking for games (a broadcast)
     OFFER   'H' 'V' ver 2 kind arena ncraft free name[16]      the opener's answer: its game, and craft still free
     JOIN    'H' 'V' ver 3 name[12]                     a joiner asks for a craft
     TAKEN   'H' 'V' ver 4 craft kind arena ncraft      the opener gives it one (said again if asked again)
     FULL    'H' 'V' ver 5                              ... or has none
     KEYS    'H' 'V' ver 6 craft count:u32 controls     a joiner's controls, and how many it has sent
     STATE   'H' 'V' ver 7 step:u32 kind score0:i16 score1:i16 flags... craft...     the game as it is
     BYE     'H' 'V' ver 8 craft                        a joiner leaves
     NAMES   'H' 'V' ver 9 ncraft name[12]...           (2026-11-23) the players' names, a craft each ("": the
                                                        computer's), every second: shown over their craft. A
                                                        PC of before it passes it over, as any it doesn't know */
#ifndef HVNET_H
#define HVNET_H

#include "hvgame.h"

#define HV_NET_VERSION 1
#define HV_NET_MOST 256                 /* a message's bytes at most */
#define HV_NET_QUIET (3 * HV_STEPS)     /* steps without a word: the computer takes the craft */

enum { HV_M_FIND = 1, HV_M_OFFER, HV_M_JOIN, HV_M_TAKEN, HV_M_FULL, HV_M_KEYS, HV_M_STATE, HV_M_BYE, HV_M_NAMES };

/* ---- the opener's side */

struct hv_seat {                        /* a craft of the game: whose it is */
    int taken;                          /* a joiner has it (0: the computer's, or the opener's own) */
    unsigned long who;                  /* the joiner (an address's number, as the caller knows it) */
    char name[13];
    unsigned char controls;             /* the last heard */
    unsigned long count;                /* ... and its number: an older one that comes late is not heard */
    unsigned heard;                     /* the step it was last heard at */
};

struct hv_opener {
    struct hv_game game;
    int arena;                          /* which of the game's arenas (hvarenas.h) */
    char name[17];
    struct hv_seat seat[HV_CRAFT];
    int own;                            /* the opener's own craft */
    int strength;                       /* the computer's craft's (hv_auto's): 1 unless changed */
};

void hv_opener_start(struct hv_opener *o, const struct hv_arena *a, int arena, int kind, int ncraft, const char *name);
/* A datagram from `who`: what is said back to it (its length; 0: nothing). */
int hv_opener_hear(struct hv_opener *o, unsigned long who, const unsigned char *m, int n, unsigned char *back);
/* A step of the game: the opener's own controls, the joiners' as last heard, the computer's for the rest. */
void hv_opener_step(struct hv_opener *o, unsigned own_controls);
/* The game as it is, for every joiner (sent every other step): its length. */
int hv_opener_state(const struct hv_opener *o, unsigned char *m);
/* (2026-11-23) The players' names (the opener's own craft: the game's name; a joiner's: its own; the computer's:
   ""): its length. And one craft's. */
int hv_opener_names(const struct hv_opener *o, unsigned char *m);
const char *hv_opener_name(const struct hv_opener *o, int craft, char *out13);

/* ---- a joiner's side */

struct hv_joiner {
    int joined, craft;                  /* it has a craft of the game */
    int kind, arena, ncraft;
    unsigned long sent;                 /* controls sent so far */
    unsigned step;                      /* the newest state heard: the game's step; an older one that comes late is passed over */
    struct hv_game view;                /* the game as last told (its craft, scores, flags, beacons, lamp); its `ev`:
                                           what happened since the state before, as far as two states show it
                                           (a flag or the lamp taken, a flag dropped, a point, a beacon, a craft
                                           in the water; not the bumps) */
    int refused;                        /* the opener had no craft free, or is of another version */
    char names[HV_CRAFT][13];           /* (2026-11-23) the players' names as last told ("": the computer's) */
    int coasted;                        /* steps gone since the last state (hv_joiner_coast) */
};

void hv_joiner_start(struct hv_joiner *j);
int hv_joiner_find(unsigned char *m);                           /* the message that looks for games */
int hv_joiner_join(const char *name, unsigned char *m);         /* ... that asks for a craft */
int hv_joiner_keys(struct hv_joiner *j, unsigned controls, unsigned char *m);  /* ... that says its controls (each step) */
int hv_joiner_bye(const struct hv_joiner *j, unsigned char *m);
/* A datagram from the opener: 1 if it was a game's offer (out: its kind, arena, craft, free craft, name), 2 if
   the joiner's view changed, 0 otherwise. `a` is the arena the view's game is in (the caller reads it when told
   which, at HV_M_TAKEN). */
int hv_joiner_hear(struct hv_joiner *j, const struct hv_arena *a, const unsigned char *m, int n, int *offer, char *name17);
/* A step in which no state came: every craft goes on as it was going (three steps at most; never into a wall),
   and what is carried goes with its carrier. The next state puts everything right. */
void hv_joiner_coast(struct hv_joiner *j);

#endif
