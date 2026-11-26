/* kernel/drv/cdda.c on the host (N4's C6: CD audio played digitally): the reader's runs in the ring (never across
 * its end, never more than the room or the play), what the mixer takes at 44.1 kHz exactly, at half the rate
 * every other frame, at 48 kHz between frames; the play's end; a late reader (silence, the position held); a
 * read for a play that has changed dropped; pause, resume, stop; the volume. Frames are numbered by their left
 * sample (k * 10), their right its negative, so what is played says which frame it was. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/drv/cdda.h"

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("cdda_test: FAIL %s\n", what);
        fails++;
    }
}

/* n sectors of the play's frames from frame `first` on, into p */
static void fill(unsigned char *p, unsigned long first, unsigned long n)
{
    unsigned long f;
    for (f = 0; f < n * CDDA_FRAMES; f++) {
        short l = (short)((first + f) * 10 % 30000), r = (short)-l;
        p[4 * f] = (unsigned char)l, p[4 * f + 1] = (unsigned char)((unsigned short)l >> 8);
        p[4 * f + 2] = (unsigned char)r, p[4 * f + 3] = (unsigned char)((unsigned short)r >> 8);
    }
}

static int frame_l(unsigned long k) { return (short)(k * 10 % 30000); }

/* The reader: every run it is given, filled with the frames due there. */
static unsigned long read_all(struct cdda *c, unsigned long max)
{
    unsigned long lba, gen, n, total = 0;
    unsigned char *into;
    while ((n = cdda_want(c, max, &lba, &gen, &into)) != 0) {
        fill(into, (lba - c->start) * CDDA_FRAMES, n);
        if (!cdda_commit(c, gen, n))
            break;
        total += n;
    }
    return total;
}

int main(void)
{
    static unsigned char ring[3 * CDDA_SECTOR + 100];      /* (not whole sectors: the ring is cut to three) */
    static int acc[2 * 4000];
    struct cdda c;
    unsigned long lba, gen, n, i;
    unsigned char *into;
    unsigned frame;
    int ok;

    cdda_init(&c, ring, sizeof ring);
    check(c.size == 3 * CDDA_SECTOR, "the ring cut to whole sectors");
    check(cdda_want(&c, 8, &lba, &gen, &into) == 0, "nothing to read with no play");

    /* 44.1 kHz: frame for frame, the run never past the ring's end */
    cdda_play(&c, 1000, 5);
    n = cdda_want(&c, 8, &lba, &gen, &into);
    check(n == 3 && lba == 1000 && into == ring, "the first run: the ring's three sectors from 1000");
    n = cdda_want(&c, 2, &lba, &gen, &into);
    check(n == 2, "max honoured");
    fill(into, 0, 2);
    check(cdda_commit(&c, gen, 2) == 1, "two committed");
    n = cdda_want(&c, 8, &lba, &gen, &into);
    check(n == 1 && lba == 1002 && into == ring + 2 * CDDA_SECTOR, "then the one before the ring's end");
    fill(into, 2 * CDDA_FRAMES, 1);
    cdda_commit(&c, gen, 1);
    check(cdda_want(&c, 8, &lba, &gen, &into) == 0, "the ring full: nothing");
    memset(acc, 0, sizeof acc);
    cdda_mix(&c, acc, CDDA_FRAMES, 44100);
    for (i = 0, ok = 1; i < CDDA_FRAMES; i++)
        ok &= acc[2 * i] == frame_l(i) && acc[2 * i + 1] == -frame_l(i);
    check(ok, "a sector played frame for frame at 44.1 kHz");
    check(cdda_position(&c, &frame) == 1001 && frame == 0, "the position: sector 1001, frame 0");
    n = cdda_want(&c, 8, &lba, &gen, &into);
    check(n == 1 && lba == 1003 && into == ring, "the room a sector played made: at the ring's start again");
    fill(into, 3 * CDDA_FRAMES, 1);
    cdda_commit(&c, gen, 1);
    memset(acc, 0, sizeof acc);
    cdda_mix(&c, acc, 3 * CDDA_FRAMES, 44100);
    for (i = 0, ok = 1; i < 3 * CDDA_FRAMES - 1; i++)
        ok &= acc[2 * i] == frame_l(CDDA_FRAMES + i);
    check(ok, "three more sectors, across the ring's end");
    check(acc[2 * (3 * CDDA_FRAMES - 1)] == 0 && c.played == 4 * CDDA_FRAMES - 1,
          "... their last frame waiting for the next sector's first");
    check(read_all(&c, 8) == 1, "the play's last sector read");
    check(cdda_want(&c, 8, &lba, &gen, &into) == 0, "the play all read: nothing more");
    memset(acc, 0, sizeof acc);
    cdda_mix(&c, acc, 2 * CDDA_FRAMES, 44100);
    check(acc[0] == frame_l(4 * CDDA_FRAMES - 1), "... then played");
    check(acc[2 * CDDA_FRAMES] == frame_l(5 * CDDA_FRAMES - 1), "the play's last frame played (alone)");
    check(acc[2 * (CDDA_FRAMES + 1)] == 0, "nothing after it");
    check(cdda_state(&c) == CDDA_DONE, "DONE at the end");
    check(cdda_position(&c, &frame) == 1005 && frame == 0, "the position at the end: 1005");

    /* 22.05 kHz: every other frame */
    cdda_play(&c, 50, 2);
    read_all(&c, 8);
    memset(acc, 0, sizeof acc);
    cdda_mix(&c, acc, CDDA_FRAMES, 22050);
    for (i = 0, ok = 1; i < CDDA_FRAMES; i++)
        ok &= acc[2 * i] == frame_l(2 * i);
    check(ok, "22.05 kHz: every other frame");
    cdda_mix(&c, acc, 10, 22050);
    check(cdda_state(&c) == CDDA_DONE, "... its end");

    /* 48 kHz: between frames */
    cdda_play(&c, 0, 1);
    read_all(&c, 8);
    memset(acc, 0, sizeof acc);
    cdda_mix(&c, acc, 4, 48000);
    {
        /* output 1 lies 0.919 of the way from frame 0 (0) to frame 1 (10); output 3 2.756: frame 2 (20) to 3 (30) */
        check(acc[0] == 0 && (acc[2] == 9 || acc[2] == 8) && (acc[6] == 27 || acc[6] == 28),
              "48 kHz: between frames");
    }

    /* a late reader: the frames there played, then silence with the position held */
    cdda_play(&c, 200, 4);
    n = cdda_want(&c, 1, &lba, &gen, &into);
    fill(into, 0, 1);
    cdda_commit(&c, gen, n);
    memset(acc, 0, sizeof acc);
    cdda_mix(&c, acc, 1000, 44100);
    check(acc[2 * (CDDA_FRAMES - 2)] == frame_l(CDDA_FRAMES - 2), "a late reader: what is there played");
    check(c.played == CDDA_FRAMES - 1, "... up to the frame that waits for its next");
    check(acc[2 * CDDA_FRAMES] == 0 && cdda_state(&c) == CDDA_PLAYING, "... then silence, still playing");
    check(cdda_position(&c, &frame) == 200 && frame == CDDA_FRAMES - 1, "... the position held");
    read_all(&c, 8);
    memset(acc, 0, sizeof acc);
    cdda_mix(&c, acc, 2, 44100);
    check(acc[0] == frame_l(CDDA_FRAMES - 1) && acc[2] == frame_l(CDDA_FRAMES), "... and on where it stopped");

    /* a read overtaken by a new play is dropped */
    cdda_play(&c, 300, 4);
    n = cdda_want(&c, 2, &lba, &gen, &into);
    fill(into, 0, n);
    cdda_play(&c, 400, 4);
    check(cdda_commit(&c, gen, n) == 0 && c.done == 0 && c.wr == 0, "an older play's read dropped");
    n = cdda_want(&c, 8, &lba, &gen, &into);
    check(n == 3 && lba == 400, "the new play read from its own start");

    /* pause, resume, stop */
    fill(into, 0, n);
    cdda_commit(&c, gen, n);
    cdda_pause(&c, 0);
    memset(acc, 0, sizeof acc);
    cdda_mix(&c, acc, 100, 44100);
    check(cdda_state(&c) == CDDA_PAUSED && c.played == 0 && acc[2] == 0, "paused: nothing played");
    cdda_pause(&c, 1);
    cdda_mix(&c, acc, 100, 44100);
    check(cdda_state(&c) == CDDA_PLAYING && c.played == 100, "resumed");
    cdda_stop(&c);
    check(cdda_state(&c) == CDDA_IDLE && cdda_want(&c, 8, &lba, &gen, &into) == 0, "stopped: nothing to read");
    cdda_pause(&c, 1);
    check(cdda_state(&c) == CDDA_IDLE, "resume after stop: still stopped");

    /* the volume */
    cdda_play(&c, 0, 1);
    cdda_volume(&c, 128, 0);
    read_all(&c, 8);
    memset(acc, 0, sizeof acc);
    cdda_mix(&c, acc, 20, 44100);
    check(acc[2 * 10] == frame_l(10) / 2 && acc[2 * 10 + 1] == 0, "the volume: left halved, right off");

    printf("cdda_test: %d failures\n", fails);
    return fails != 0;
}
