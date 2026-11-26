/* FMSYNTH (tests/dos): GLOS's FM synthesiser for DOS programs (sdk/fm, include/glos/fmsynth.h) through its calls
 * alone: the chip found at a port, a General MIDI flute (program 74) chosen, a short middle C first (86Box's
 * first FM notes after power-on are faint), then its F sharp above middle C (key 66, 370 Hz) for MS
 * milliseconds from the queue, a drum (channel 10's closed hi-hat) after it, the chip closed.
 *
 *   FMSYNTH [PORT [MS]]        PORT in hex (388 by default), MS 1500
 *   FMSYNTH open port=388 chip=3
 *   FMSYNTH played queued=0
 * Exit status 0, or 1 (no chip at that port). */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <glos/fmsynth.h>

static unsigned long now_ms(void) { return (unsigned long)(uclock() * 1000 / UCLOCKS_PER_SEC); }

int main(int argc, char **argv)
{
    unsigned port = argc > 1 ? (unsigned)strtoul(argv[1], 0, 16) : 0x388;
    unsigned long ms = argc > 2 ? strtoul(argv[2], 0, 10) : 1500, t0;
    int chip = fmsynth_open(port);
    printf("FMSYNTH open port=%x chip=%d\n", port, chip);
    if (!chip)
        return 1;
    t0 = now_ms();
    fmsynth_play_due(t0);
    fmsynth_send(0, 0xC0, 73, 0);               /* (now) the flute, on channel 1 */
    fmsynth_send(t0 + 50, 0x90, 60, 100);       /* a short middle C, */
    fmsynth_send(t0 + 350, 0x80, 60, 0);
    fmsynth_send(t0 + 500, 0x90, 66, 120);      /* then its F sharp ... */
    fmsynth_send(t0 + 500 + ms, 0x80, 66, 0);   /* ... for ms */
    fmsynth_send(t0 + 600 + ms, 0x99, 42, 100); /* a closed hi-hat on channel 10, the drums */
    fmsynth_send(t0 + 700 + ms, 0x89, 42, 0);
    while (now_ms() - t0 < ms + 1000)
        fmsynth_play_due(now_ms());
    printf("FMSYNTH played queued=%u\n", fmsynth_queued());
    fmsynth_close();
    return 0;
}
