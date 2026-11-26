/* GLOS's FM synthesiser for DOS programs (sdk/fm; build/sdk/libfmsynth.a): MIDI channel messages played on an
 * OPL3 or OPL2 with General MIDI's instruments, by the same driver and instruments GLOS plays its MIDI ports' FM
 * with (kernel/drv/fm.c; kernel/drv/fmbank.c, Freedoom's GENMIDI). So a program sounds the same on plain DOS and
 * under GLOS.
 *
 * For a program that reaches the chip's ports itself: on plain DOS, and in a GLOS session (a program run full
 * screen, not a GLOS app: while a session has the machine GLOS's own driver lets the chips go, and the ports
 * are the program's). A GLOS app reaches no port: it uses the MIDI API (glos_midi_out, GLOS_MIDI_PORT_FM).
 *
 * One caller at a time: from the program's main loop, or from its timer's handler, not both (nothing here is
 * locked). Times are the caller's milliseconds, on any clock that counts up (it may wrap: compared by
 * difference).
 *
 *   if (fmsynth_open(0x388)) {                 2 an OPL2, 3 an OPL3 (0: none at that port)
 *       fmsynth_send(t + 500, 0x90, 60, 100);  a message for later (the queue holds FMSYNTH_QUEUE)
 *       ... each frame: fmsynth_play_due(now);
 *       fmsynth_close();                       silent, the chip an OPL2's again, as programs find it
 *   }
 *
 * Building it into another project (no copies: the sources are GLOS's, compiled where they are; DJGPP):
 *   the driver's half, freestanding with the kernel's headers: kernel/drv/fm.c kernel/drv/fmbank.c
 *     kernel/drv/midiq.c sdk/fm/fmq.c, each with
 *     -ffreestanding -IGLOS/include -IGLOS/kernel/include -IGLOS/kernel/include/libc
 *   the program's half, as any DJGPP file: sdk/fm/fmsynth.c with -IGLOS/include
 * (GLOS's Makefile does it: make build/sdk/libfmsynth.a.) */
#ifndef GLOS_FMSYNTH_H
#define GLOS_FMSYNTH_H

#ifdef __cplusplus
extern "C" {
#endif

#define FMSYNTH_QUEUE 512       /* messages waiting at once */

/* The chip at `port` (388h, the AdLib's; or a Sound Blaster's base, 220h ...: its first four ports), found by
   its timer as an AdLib's driver finds it, then set up for General MIDI (every voice silent, the channels as GM
   starts them; an OPL3 in its OPL3 mode). 2: an OPL2, 3: an OPL3, 0: nothing there (then every call below does
   nothing). A second open closes the first. */
int fmsynth_open(unsigned port);
int fmsynth_chip(void);         /* what was found: 2, 3, or 0 */

/* A channel message (80h-EFh: note off, note on, aftertouch, control change, program change, channel pressure,
   pitch bend; channel 10 the drums), played now. Anything else is ignored. */
void fmsynth_message(unsigned char status, unsigned char d1, unsigned char d2);

/* A channel message for `at_ms` (or now: a time gone by, or 0), kept in order of time; played by
   fmsynth_play_due. 0, or -1 (the queue is full, or not a channel message). */
int fmsynth_send(unsigned long at_ms, unsigned char status, unsigned char d1, unsigned char d2);
void fmsynth_play_due(unsigned long now_ms);    /* every message due by now_ms, played */
unsigned fmsynth_queued(void);                  /* how many wait */
void fmsynth_flush(void);                       /* what waits dropped, every note off */

void fmsynth_close(void);       /* silent, the chip as a program expects to find it (an OPL2's registers) */

#ifdef __cplusplus
}
#endif
#endif
