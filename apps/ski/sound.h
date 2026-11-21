/* The skiing game's sounds (apps/ski/sound.c): made by rule as they are played, no recordings: the skis' hiss,
   which rises with the speed and changes with the snow, and short sounds for what happens in a run. One stream
   into GLOS's mixer; silent, and no harm, where there is no card GLOS plays through. */
#ifndef SKI_SOUND_H
#define SKI_SOUND_H

int sound_start(void);                  /* 1: there is sound */
void sound_stop(void);
void sound_events(unsigned ev);         /* SKI_EV_ of a step: their sounds begin */
/* A picture's worth more of sound: the skis at this speed (pixels a second) on this snow (SKI_SNOW_; -1: off the
   snow, in the air or down or still). */
void sound_frame(int speed, int snow);

#endif
