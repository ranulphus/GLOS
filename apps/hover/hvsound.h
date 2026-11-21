/* The hovercraft game's sounds (apps/hover/hvsound.c): made by rule as they are played, no recordings: the fans,
   whose pitch goes with their work, and short sounds for what happens. One stream into GLOS's mixer; silent,
   and no harm, where there is no card GLOS plays through. */
#ifndef HVSOUND_H
#define HVSOUND_H

int hvsound_start(void);                /* 1: there is sound */
void hvsound_stop(void);
void hvsound_events(unsigned ev);       /* HV_EV_ of a step: their sounds begin */
/* A picture's worth more of sound: the fans at this work (0 idling to 3 boosting), the craft at this speed
   (1/65536 square a step), set down or not, in the water or not. */
void hvsound_frame(int work, int speed, int down, int wet);

#endif
