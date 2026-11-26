/* sdk/fm's program half (include/glos/fmsynth.h): the chip found and written, for a DJGPP program that reaches
 * its ports. The detection and the waits are GLOS's own (kernel/drv/midi.c's fm_find and fm_out): AdLib's timer
 * test (timer 1 at its shortest, 80 microseconds; its flags looked for, paced by port 80h, for 4 ms at most; an
 * OPL3 by its status's low bits being 0); an OPL2 given 3.3 microseconds after a register's address and 23 after
 * its data (reads of its status port, a microsecond or more each on an ISA bus), an OPL3 next to nothing. The
 * driver itself (kernel/drv/fm.c) and the queue (kernel/drv/midiq.c) are reached through fmq.c, in the kernel's
 * types: declared here in plain C's (u8 an unsigned char, u16 an unsigned short, u32 an unsigned int). */
#include <dos.h>
#include <pc.h>

#include <glos/fmsynth.h>

typedef void (*fm_write)(unsigned short reg, unsigned char v);
void fm_reset(fm_write wr, int opl3);
void fm_message(unsigned char status, unsigned char d1, unsigned char d2);
void fm_silence(void);
void fm_end(void);
int fmq_put(unsigned at, unsigned char status, unsigned char d1, unsigned char d2, unsigned now);
void fmq_play(unsigned now);
unsigned fmq_n(void);
void fmq_clear(void);
unsigned fmq_len(unsigned char status);

static unsigned base;
static int chip;                                /* 0 none, 2 an OPL2, 3 an OPL3 */
static unsigned long last_now;

static void reads(unsigned n)
{
    while (n--)
        inportb(base);
}

static void out(unsigned short reg, unsigned char v)
{
    unsigned short p = (unsigned short)(base + ((reg & 0x100) ? 2 : 0));
    outportb(p, (unsigned char)reg);
    reads(chip == 3 ? 2 : 6);
    outportb((unsigned short)(p + 1), v);
    reads(chip == 3 ? 2 : 36);
}

int fmsynth_open(unsigned port)
{
    unsigned char s1, s2 = 0;
    unsigned n;
    int was;
    fmsynth_close();
    base = port;
    chip = 2;                                   /* (an OPL2's waits, until it is known) */
    was = disable();
    out(0x04, 0x60);                            /* the timers reset */
    out(0x04, 0x80);                            /* the interrupt reset */
    s1 = inportb(port);
    out(0x02, 0xFF);                            /* timer 1 at its shortest: 80 microseconds */
    out(0x04, 0x21);                            /* started */
    for (n = 0; n < 4000 && (s2 & 0xE0) != 0xC0; n++) {
        inportb(0x80);
        s2 = inportb(port);
    }
    out(0x04, 0x60);
    out(0x04, 0x80);
    if (was)
        enable();
    chip = (s1 & 0xE0) != 0x00 || (s2 & 0xE0) != 0xC0 ? 0 : (s2 & 0x06) ? 2 : 3;
    if (chip) {
        fmq_clear();
        fm_reset(out, chip == 3);
    }
    return chip;
}

int fmsynth_chip(void) { return chip; }

void fmsynth_message(unsigned char status, unsigned char d1, unsigned char d2)
{
    if (chip && fmq_len(status))
        fm_message(status, d1, d2);
}

int fmsynth_send(unsigned long at_ms, unsigned char status, unsigned char d1, unsigned char d2)
{
    if (!chip || !fmq_len(status))
        return -1;
    return fmq_put((unsigned)at_ms, status, d1, d2, (unsigned)last_now);
}

void fmsynth_play_due(unsigned long now_ms)
{
    last_now = now_ms;
    if (chip)
        fmq_play((unsigned)now_ms);
}

unsigned fmsynth_queued(void) { return chip ? fmq_n() : 0; }

void fmsynth_flush(void)
{
    if (!chip)
        return;
    fmq_clear();
    fm_silence();
}

void fmsynth_close(void)
{
    if (!chip)
        return;
    fmq_clear();
    fm_end();
    chip = 0;
}
