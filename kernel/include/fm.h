/* GLOS's FM driver (kernel/drv/fm.c): MIDI channel messages played on an OPL3 or OPL2 with General MIDI
 * instruments (kernel/drv/fmbank.c: Freedoom's GENMIDI). No port I/O of its own: the caller writes the registers
 * (kernel/drv/midi.c; tests/host/fm_test.c). */
#ifndef K_FM_H
#define K_FM_H
#include "types.h"

#define FM_PATCHES     175      /* 128 melodic, then percussion for keys 35 to 81 */
#define FM_PATCH_BYTES 36
extern const u8 fm_bank[FM_PATCHES][FM_PATCH_BYTES];
extern const u16 fm_fnum[384];
extern const u8 fm_atten[128];

typedef void (*fm_write)(u16 reg, u8 v);        /* (0-FFh: the first register set; 100h-1FFh: the OPL3's second) */

void fm_reset(fm_write wr, int opl3);           /* the chip as the driver wants it: every voice silent, the channels
                                                   as General MIDI starts them */
void fm_message(u8 status, u8 d1, u8 d2);       /* a channel message (80h-EFh) */
void fm_silence(void);                          /* every note off (its release is heard) */
void fm_leave(void);                            /* the chip as a program expects to find it: silent, an OPL2's
                                                   registers. Messages are nothing until fm_back */
void fm_back(void);                             /* ... and the driver's again: the channels as they were left */
void fm_end(void);                              /* fm_leave, for good (until fm_reset) */
u32 fm_sounding(void);                          /* voices keyed on */

#endif
