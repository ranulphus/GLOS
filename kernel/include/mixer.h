/* GLOS's mixer and the cards under it (kernel/drv/mixer.c; sb16.c, the Sound
 * Blaster 16; crystal.c, a Crystal CS423x's codec; PRD §10.5;
 * docs/milestones-m8.md step E, docs/crystal-audio.md). */
#ifndef K_MIXER_H
#define K_MIXER_H
#include "types.h"

struct app;
struct bootinfo;

void mixer_counts(u32 *rate, u32 *streams, u32 *underruns);     /* GLOS_FN_STAT's (M9) */
void mixer_enable(int on);              /* [sound] mixer (M9) */
void mixer_set_rate(u32 rate);          /* [sound] rate (M9) */
int mixer_init(const struct bootinfo *bi);      /* 0 when there is an SB16 to mix into (and [sound] mixer isn't off) */
int mixer_on(void);                             /* it plays now (no session has the card) */
int mixer_found(void);                          /* it has a card (a session may have it now) */
void mixer_desktop(int on);                     /* the desktop has the screen (1: the mixer plays), or a session
                                                   or nobody has (0: the card stopped, its line the system VM's) */
int mixer_open(struct app *a, u32 rate, u32 channels, u32 bytes, u32 *id, u32 *lin);   /* GLOS_FN_AUDIO: 0, GLOS_E_ */
int mixer_close(struct app *a, u32 id);
void mixer_app_gone(struct app *a);
u32 mixer_stat(char *buf, u32 max);             /* glos audio */

struct trapframe;
typedef void (*mixer_fill)(s16 *out, u32 frames, const struct trapframe *tf);  /* (tf: what the card's interrupt
                                                                                     broke into) */
typedef void (*mixer_got)(const s16 *in, u32 frames);     /* (a half recorded: 16-bit stereo at the mixer's rate) */
struct sound_card {                             /* what the mixer plays through */
    const char *name;
    int (*start)(u32 rate, u32 frames, mixer_fill fill);        /* two halves of `frames`: 0 */
    void (*stop)(void);
    int (*running)(void);
    u32 (*irqs)(void);
    u32 (*errors)(void);                        /* periods the card reported an underrun in (0: it can't tell) */
    /* The chip's own mixer (API 2.0's AUDIO MIXER): a channel's levels (0 to 255) and mute read or set; the
       input that is recorded. 0, or -1: the chip has no such channel. NULL: none at all. */
    int (*chan)(int chan, int set, u8 *left, u8 *right, u8 *mute);
    int (*input)(int set, int *input);
    /* Recording (API 2.0's AUDIO IN): the card records at the rate it plays at, into two halves of `frames`; at
       the end of each, `got` has that half as 16-bit stereo. 0, or -1: it can't. NULL: no recording. */
    int (*rec_start)(u32 rate, u32 frames, mixer_got got);
    void (*rec_stop)(void);
    /* The card has stopped interrupting while it plays (mixer_tick): its handler run by hand, so that what it
       has pending is answered and its next interrupt is a new edge. NULL: never done (an ISA card's line is its
       own). */
    void (*kick)(void);
};
void mixer_tick(void);                          /* every kernel tick (vdev.c): the card's watchdog */
struct pci_dev;
int mixer_pci_line(struct pci_dev *pd, const char *name, const char *tag);     /* its line taken (devices.md D1): 0 */
enum { MX_CH_MASTER, MX_CH_WAVE, MX_CH_FM, MX_CH_CD, MX_CH_LINE, MX_CH_MIC, MX_CHANNELS };
enum { MX_IN_MIC, MX_IN_LINE, MX_IN_CD, MX_IN_MIX };
enum { MX_VOLUME, MX_BALANCE, MX_MUTE, MX_PAUSE, MX_THRESHOLD, MX_POSITION, MX_QUEUED };
struct mixer_stream {
    u32 id, app, rate, channels, volume;
    s32 balance;
    u8 mute, paused;
    u16 level[2];
};
int mixer_open_in(struct app *a, u32 rate, u32 channels, u32 bytes, u32 *id, u32 *lin);        /* a stream in: 0, GLOS_E_ */
u32 mixer_streams_in(void);                                     /* how many streams in are open */
int mixer_records(void);                                        /* the card can record */
int mixer_get(struct app *a, u32 id, int what, s32 *v);         /* a stream's (a NULL: any app's): 0; -1 no such
                                                                   stream; -2 not a thing a stream in has */
int mixer_set(struct app *a, u32 id, int what, s32 v);
u32 mixer_streams(struct mixer_stream *out, u32 max);
void mixer_master_get(u32 *volume, int *mute);                  /* 0 to 256 */
void mixer_master_set(u32 volume, int mute);
int mixer_levels(u32 id, u16 *left, u16 *right);                /* the output's (id 0) or a stream's peaks */
int mixer_wants(struct app *a, u32 *id);                        /* a stream of a's is below its threshold: once */
const char *mixer_card(void);                                   /* the card the mixer plays through, or NULL */
int mixer_chan(int chan, int set, u8 *left, u8 *right, u8 *mute);
int mixer_input(int set, int *input);
extern const struct sound_card sb16_card, crystal_card, cmi8738_card, es1371_card, ich97_card, via686_card, ess_card,
    opti_card;
extern const struct sound_card via8233_card;    /* (via686.c's, for the VT8233 family) */
int via686_is8233(void);
int via686_init(void);                          /* (phase H) VIA's VT82C686 AC'97 found and its codec ready: 0 */
void via686_leave(void);
int ich97_init(void);                           /* (phase H) Intel's ICH AC'97 found and its codec ready: 0 */
void ich97_leave(void);
int es1371_init(void);                          /* (phase H) a PCI ES1371, ES1373 or CT5880 found and set up: 0 */
void es1371_leave(void);                        /* glos exit: its channels stopped */
int cmi8738_init(void);                         /* (phase H) a PCI CMI8738 found and set up: 0 */
int ess_init(const struct bootinfo *bi);        /* (phase H) an ESS AudioDrive at BLASTER's A, I and D: 0 */
int ess_init_pnp(void);                         /* ... no BLASTER: a Plug and Play AudioDrive set up: 0 */
int sb16_init_pnp(void);                        /* (phase H) no BLASTER: a Plug and Play SB16 or ALS100 set up: 0 */
int sb16_init_legacy(void);                     /* ... a native boot's: a jumpered SB16 at 220h-280h: 0 */
int sb16_legacy(void);                          /* ... it was found so (its MPU-401 looked for: midi.c) */
void ess_leave(void);                           /* glos exit: stopped, a Sound Blaster Pro again */
int crystal_codec_at(u16 wss, u8 irq, u8 dma, u8 cap, u16 sb, const char *blaster);    /* another chip's CS4231: 0 */
void crystal_codec_wr(u8 idx, u8 v);           /* ... one of its indexed registers written */
int opti_init(const struct bootinfo *bi);       /* (phase H) an OPTi 82C929 (MAD16 Pro), its codec through crystal.c */
void opti_leave(void);                          /* glos exit: a Sound Blaster Pro again, as found */
void cmi8738_leave(void);                       /* glos exit: its channels stopped, its legacy parts as they were */
void mixer_refresh(void);                       /* the card started or stopped as things stand now */
const char *mixer_blaster(void);                /* BLASTER for programs: the virtual Sound Blaster's, a Crystal chip's, 0 */
void mixer_late(void);                          /* GLOS.CFG read: a Crystal chip set up when there's no SB16 */
int crystal_init(const char *cfg);              /* [sound] crystal (auto, off, or resources): the part set up: 0 */
u16 crystal_sb(void);                           /* its SB Pro's port, 0 if none set up */
const char *crystal_blaster(void);              /* BLASTER for programs (A220 I5 D1 T4), 0 if none set up */
int sb16_init(const struct bootinfo *bi);       /* the DSP found, version 4 or later: 0 */
const char *sb16_res(void);
void sb16_left(const struct bootinfo *bi);      /* [devices] off names it: noted as DOS's, not looked at */             /* its ports, line and channels, as the Devices list writes them */

#endif
