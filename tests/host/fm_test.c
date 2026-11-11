/* The FM driver (kernel/drv/fm.c): MIDI messages as register writes. */
#include <stdio.h>
#include <string.h>

#include "fm.h"
#include "../../kernel/drv/fmbank.c"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("fm_test: line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static u8 reg[0x200];
static u32 writes;
static void wr(u16 r, u8 v) { reg[r & 0x1FF] = v; writes++; }

static u32 fnum(u32 voice) { u32 s = voice >= 9 ? 0x100 : 0, c = voice % 9; return reg[s + 0xA0 + c] | (reg[s + 0xB0 + c] & 3u) << 8; }
static u32 block(u32 voice) { return (reg[(voice >= 9 ? 0x100 : 0) + 0xB0 + voice % 9] >> 2) & 7; }
static int keyed(u32 voice) { return (reg[(voice >= 9 ? 0x100 : 0) + 0xB0 + voice % 9] & 0x20) != 0; }

int main(void)
{
    u32 i, n;

    /* The tables: A (the bank's notes are an octave up: key 57 with offset 0 is A 440) and the levels. */
    CHECK(fm_fnum[96] == 580 && fm_fnum[0] == 488 && fm_fnum[383] < 1024);
    CHECK(fm_atten[127] == 0 && fm_atten[0] == 63 && fm_atten[64] == 16 && fm_atten[100] == 6);

    /* Reset: an OPL3's registers on, every voice off and quiet. An OPL2's: no second set touched. */
    memset(reg, 0xEE, sizeof reg);
    fm_reset(wr, 1);
    CHECK(reg[0x105] == 1 && reg[0x104] == 0 && reg[0x01] == 0x20 && reg[0xBD] == 0);
    CHECK(reg[0xB0] == 0 && reg[0x1B8] == 0 && reg[0x40] == 0x3F && reg[0x43] == 0x3F && reg[0x155] == 0x3F);
    CHECK(fm_sounding() == 0);
    memset(reg, 0xEE, sizeof reg);
    fm_reset(wr, 0);
    CHECK(reg[0x105] == 0xEE && reg[0x1B0] == 0xEE && reg[0xB8] == 0);

    /* A flute's A (patch 73: one voice, offset -12): 440 Hz is F-number 580 in block 4; its carrier at the
       channel's volume (100: 6 steps down), the modulator as the patch has it. */
    fm_reset(wr, 1);
    fm_message(0xC0, 73, 0);
    fm_message(0x90, 69, 127);
    CHECK(fm_sounding() == 1 && keyed(0) && fnum(0) == 580 && block(0) == 4);
    CHECK(reg[0x20] == 0xA1 && reg[0x60] == 0x60 && reg[0x80] == 0x88 && reg[0x23] == 0x61 && reg[0x63] == 0x65);
    CHECK(reg[0x40] == (0x80 | 0x26) && reg[0x43] == 6);
    CHECK(reg[0xC0] == (0x08 | 0x30));
    fm_message(0xB0, 7, 127);                   /* the volume up: the carrier at the patch's own */
    CHECK(reg[0x43] == 0);
    fm_message(0xB0, 11, 64);                   /* expression halved: 16 steps (12 dB) */
    CHECK(reg[0x43] == 16);
    fm_message(0xB0, 7, 0);
    CHECK(reg[0x43] == 63);
    fm_message(0xB0, 10, 0);                    /* all left, all right, the middle */
    CHECK(reg[0xC0] == (0x08 | 0x10));
    fm_message(0xB0, 10, 127);
    CHECK(reg[0xC0] == (0x08 | 0x20));
    fm_message(0xB0, 10, 64);
    CHECK(reg[0xC0] == (0x08 | 0x30));
    fm_message(0xE0, 0x7F, 0x7F);               /* the bend all the way up: two semitones less a 32nd */
    CHECK(keyed(0) && fnum(0) == fm_fnum[96 + 63] && block(0) == 4);
    fm_message(0xB0, 101, 0), fm_message(0xB0, 100, 0), fm_message(0xB0, 6, 12);        /* its range an octave */
    fm_message(0xE0, 0, 0);                     /* all the way down: an octave below */
    CHECK(fnum(0) == 580 && block(0) == 3);
    fm_message(0xE0, 0, 0x40);
    CHECK(fnum(0) == 580 && block(0) == 4);
    fm_message(0x80, 69, 0);
    CHECK(fm_sounding() == 0 && !keyed(0) && fnum(0) == 580);
    fm_message(0x90, 69, 100), fm_message(0x90, 69, 0);         /* (velocity 0: a note off; the voice free longest) */
    CHECK(fm_sounding() == 0 && !keyed(1) && fnum(1) == 580);

    /* The pedal holds what is released; the same key again takes its voice's place. */
    fm_reset(wr, 1);
    fm_message(0xC1, 73, 0);
    fm_message(0xB1, 64, 127);
    fm_message(0x91, 60, 90), fm_message(0x81, 60, 0);
    CHECK(fm_sounding() == 1 && keyed(0));
    fm_message(0x91, 60, 90);
    CHECK(fm_sounding() == 1 && !keyed(0) && keyed(1));
    fm_message(0x81, 60, 0);
    fm_message(0xB1, 64, 0);
    CHECK(fm_sounding() == 0 && !keyed(1));

    /* A piano (patch 0): two voices on an OPL3, the second a 32nd of a semitone sharp; one on an OPL2. */
    fm_reset(wr, 1);
    fm_message(0x90, 69, 127);
    CHECK(fm_sounding() == 2 && fnum(0) == 580 && fnum(1) == fm_fnum[97] && block(1) == 4);
    fm_message(0x80, 69, 0);
    CHECK(fm_sounding() == 0);
    fm_reset(wr, 0);
    fm_message(0x90, 69, 127);
    CHECK(fm_sounding() == 1 && reg[0xC0] == 0x0E && (reg[0xE0] & ~3u) == 0);

    /* Percussion: channel 10's key 35 is patch 128 at its fixed note (25: C sharp 2 here), whatever the key;
       keys outside 35 to 81 are nothing. */
    fm_reset(wr, 1);
    fm_message(0x99, 35, 127);
    CHECK(fm_sounding() == 1 && reg[0x60] == 0xFB && block(0) == 1 && fnum(0) == fm_fnum[(25 + 12 - 6) * 32 % 384]);
    fm_message(0x99, 34, 127), fm_message(0x99, 82, 127);
    CHECK(fm_sounding() == 1);

    /* Low and high: a glockenspiel's lowest key (offset -24) and the highest key stay in range. */
    fm_reset(wr, 1);
    fm_message(0xC0, 9, 0);
    fm_message(0x90, 0, 127);
    CHECK(fm_sounding() == 2 && block(0) == 0 && fnum(0) < 1024);
    fm_message(0x90, 127, 127);
    CHECK(block(2) == 7);

    /* Eighteen voices: the nineteenth note takes the oldest's; all notes off frees them all. */
    fm_reset(wr, 1);
    fm_message(0xC0, 73, 0);
    for (i = 0; i < 18; i++)
        fm_message(0x90, (u8)(40 + i), 100);
    CHECK(fm_sounding() == 18 && keyed(17));
    n = fnum(0);
    fm_message(0x90, 80, 100);
    CHECK(fm_sounding() == 18 && keyed(0) && (fnum(0) != n || block(0) != 2));
    fm_message(0x80, 40, 0);                    /* (its voice was taken: nothing of that key is left) */
    CHECK(fm_sounding() == 18);
    fm_message(0xB0, 123, 0);
    CHECK(fm_sounding() == 0);
    fm_message(0x90, 60, 100), fm_message(0x91, 62, 100);
    fm_silence();
    CHECK(fm_sounding() == 0 && !keyed(1) && !keyed(2));

    /* Leaving: quiet, an OPL2's registers again; messages meanwhile are nothing. Back: the OPL3's registers,
       the channels as they were (the flute still channel 0's). Ended: nothing more. */
    fm_message(0x90, 60, 100);
    fm_leave();
    CHECK(reg[0x105] == 0 && reg[0x01] == 0 && reg[0x43] == 0x3F && !keyed(0) && !keyed(1) && !keyed(2));
    writes = 0;
    fm_message(0x90, 60, 100);
    CHECK(writes == 0 && fm_sounding() == 0);
    fm_back();
    CHECK(reg[0x105] == 1 && reg[0x01] == 0x20 && fm_sounding() == 0);
    fm_message(0x90, 69, 127);
    CHECK(fm_sounding() == 1 && keyed(0) && fnum(0) == 580 && reg[0x20] == 0xA1);
    fm_end();
    CHECK(reg[0x105] == 0 && !keyed(0));
    writes = 0;
    fm_back();
    fm_message(0x90, 60, 100);
    CHECK(writes == 0 && fm_sounding() == 0);

    if (fails)
        return 1;
    printf("fm_test: ok\n");
    return 0;
}
