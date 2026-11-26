/* CD audio played digitally (docs/v1/native-dos.md, N4's C6): a disc's (or an image's) audio sectors, read by a
 * reader thread straight into a ring, played into GLOS's mixer from the sound card's interrupt. Plain C over no
 * kernel service, so tests/host/cdda_test.c runs it; kernel/drv/cdrom.c is the reader and the controls,
 * kernel/drv/mixer.c the player.
 *
 * A play is `count` sectors from `start`. The reader asks where the next ones go (cdda_want: a run of whole
 * sectors in the ring's free space, before its end) and reads them there, then says so (cdda_commit) with the
 * play's generation, which every PLAY and STOP changes: what a reader read for a play that has since changed is
 * dropped, never played. The mixer takes frames (a stereo pair of 16-bit samples, 588 a sector) at 44.1 kHz,
 * to its own rate by linear interpolation; `played` is what has been played, which is the Q channel's position,
 * not what has been read. With nothing read yet the mixer waits (silence, the position held): the reader is late,
 * or the drive is seeking. cdda_mix and cdda_commit are each other's only concurrency besides the controls; the
 * caller makes cdda_play, cdda_stop, cdda_pause and cdda_commit atomic against cdda_mix (interrupts off). */
#ifndef K_DRV_CDDA_H
#define K_DRV_CDDA_H

#define CDDA_SECTOR 2352u               /* a CD-DA sector's bytes */
#define CDDA_FRAMES 588u                /* ... its frames: 44,100 a second, 75 sectors */
#define CDDA_RATE   44100u

enum { CDDA_IDLE, CDDA_PLAYING, CDDA_PAUSED, CDDA_DONE };

struct cdda {
    unsigned char *ring;
    unsigned long size;                 /* the ring's bytes: whole sectors */
    volatile unsigned long rd, wr;      /* bytes played and read since the play began (each mod size in the ring) */
    unsigned long start, count;         /* the play: its first sector, how many */
    unsigned long done;                 /* sectors read into the ring so far */
    volatile unsigned long played;      /* frames played */
    unsigned long phase;                /* the resampler's place after the frame at rd, 16.16 */
    volatile int state;
    unsigned long gen;                  /* the play's generation */
    unsigned vol[2];                    /* each channel's gain, 0 to 256 */
};

void cdda_init(struct cdda *c, unsigned char *ring, unsigned long size);
void cdda_play(struct cdda *c, unsigned long start, unsigned long count);
void cdda_stop(struct cdda *c);                         /* IDLE: the play forgotten */
void cdda_pause(struct cdda *c, int resume);            /* PAUSED, or PLAYING again (a paused play alone) */
void cdda_volume(struct cdda *c, unsigned left, unsigned right);        /* 0 to 256 */

/* The reader's: the sectors it may read now (at most max; 0: none, the ring full or the play all read), the
   first's number, the play's generation and where in the ring they go. */
unsigned long cdda_want(struct cdda *c, unsigned long max, unsigned long *lba, unsigned long *gen,
                        unsigned char **into);
/* ... n of them read there: 1, or 0 when the play has changed since (they are dropped). */
int cdda_commit(struct cdda *c, unsigned long gen, unsigned long n);

/* The mixer's (the card's interrupt): frames frames of 16-bit stereo at rate added into acc (two a frame). */
void cdda_mix(struct cdda *c, int *acc, unsigned long frames, unsigned long rate);

/* The sector playing now and the frame in it (for the Q channel); the state. */
unsigned long cdda_position(const struct cdda *c, unsigned *frame);
int cdda_state(const struct cdda *c);

#endif
