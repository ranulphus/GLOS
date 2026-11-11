/* GLOS's MIDI ports (kernel/drv/midi.c; API 2.0's AUDIO MIDI_OUT and MIDI_IN): the FM chip, played by GLOS's own
 * driver (fm.c), and an MPU-401's UART. */
#ifndef K_MIDI_H
#define K_MIDI_H
#include "types.h"
#include "midiq.h"

struct app;
struct bootinfo;

enum { MIDI_FM, MIDI_MPU, MIDI_PORTS };         /* (API 2.0's GLOS_MIDI_PORT_) */
enum { MIDI_E_NONE = -1, MIDI_E_BUSY = -2, MIDI_E_NOT_OPEN = -3, MIDI_E_MESSAGE = -4, MIDI_E_ROOM = -5,
       MIDI_E_AWAY = -6 };

void midi_init(const struct bootinfo *bi);      /* the FM chip at 388h; an MPU-401 where BLASTER's P says */
void midi_late(void);                           /* a Crystal chip set up since: its FM chip and its MPU-401 */
void midi_desktop(int on);                      /* the desktop has the machine (1), or a session or nobody: the
                                                   chips are left as programs expect them, what waited is dropped */
int midi_open(struct app *a, u32 port, int in); /* 0, or MIDI_E_: no such port, another app has it, no desktop */
void midi_close(struct app *a, u32 port, int in);
int midi_send(struct app *a, u32 port, const struct midi_msg *m, u32 n, u32 *taken);   /* 0 (*taken fewer than
                                                   n: the queue is full), MIDI_E_NOT_OPEN, MIDI_E_MESSAGE (the one
                                                   after *taken isn't a channel message) */
int midi_raw(struct app *a, u32 port, const u8 *b, u32 n);     /* an MPU-401's: bytes as they are, after what is
                                                                   being sent: 0, MIDI_E_ROOM, MIDI_E_NOT_OPEN */
int midi_flush(struct app *a, u32 port);        /* what waits dropped, every note off */
u32 midi_queued(u32 port);
int midi_in_next(struct app *a, struct midi_msg *m);            /* 1: a message received for a (interrupts off) */
void midi_tick(void);                           /* every kernel tick (interrupts off): what is due is played */
void midi_mpu_drain(void);                      /* the MPU-401 has bytes (its interrupt, or the tick) */
void midi_app_gone(struct app *a);
int midi_port(u32 port, const char **name, u32 *outs, u32 *ins);       /* 0: there is such a port (ins: -1 as u32
                                                                           where it has no input) */
u32 midi_stat(char *buf, u32 max);              /* glos audio's lines */

void sb16_midi(int on);                         /* (sb16.c) the card's MPU-401 is GLOS's: its interrupts too */
u16 crystal_fm(void);                           /* (crystal.c) its FM chip's port, 0 if none set up */
u16 crystal_mpu(void);                          /* ... and its MPU-401's */

#endif
