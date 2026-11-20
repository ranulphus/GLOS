/* The Player's playing (apps/player/engine.c): one file at a time, its samples fed to a stream out by a thread of
   the engine's own, so that the music goes on whatever the window is doing (a menu open, a dialog up, a folder
   being listed). Every call here is the window's thread's. */
#ifndef ENGINE_H
#define ENGINE_H

#include "../shared/source.h"

enum { ENG_STOPPED, ENG_PLAYING, ENG_PAUSED };

struct eng_now {
    int state;
    unsigned long at, frames;           /* the frame heard now; the file's length */
    unsigned rate;
    int ended;                          /* the file played to its end since this was asked last */
    int slow;                           /* ... or was stopped: the PC can't unpack it as fast as it plays */
    int left, right;                    /* its level now, 0 to 1000 */
};

int eng_begin(int demo);                /* 0, or -1 (no thread to be had). demo: no card is asked for and no time
                                           passes by itself: eng_step says how much was "heard" (the tests') */
void eng_end(void);
int eng_open(const char *path, struct source_info *info);       /* SOURCE_OK and the file ready at its start, or a
                                           SOURCE_E_; whatever was open is closed either way */
int eng_play(void);                     /* 0, or -1: there is no card to play through */
void eng_pause(void);                   /* paused, or on again */
void eng_stop(void);                    /* silent, at the file's start again */
void eng_close(void);
void eng_seek(unsigned long frame);
void eng_volume(int v);                 /* 0 to 256 */
void eng_now(struct eng_now *n);
void eng_step(unsigned long frames);    /* (demo) */

#endif
