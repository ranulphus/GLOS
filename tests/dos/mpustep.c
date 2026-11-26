/* MPUSTEP (tests/dos): one step of an MPU-401 probe a run, for finding which access reset the P3 (2026-11-26:
 * Echoes' probe at 330h in a GLOS session reset it twice). Each run does one step and says what it read, so that
 * the step that stops the PC is the last one heard (run with `glos log sync on`).
 *
 *   MPUSTEP PORT STEP          PORT in hex (330); STEP:
 *     status   read the status port (PORT+1) 16 times
 *     data     read the data port (PORT) once
 *     reset    write FFh to the status port (the reset command)
 *     ack      wait up to 100 ms for the status's bit 7 to clear, then read the data port (FEh: acknowledged)
 *     uart     write 3Fh to the status port (UART mode)
 *     all      status, reset, ack, uart in one run, as Echoes' probe did
 *   MPUSTEP PORT=330 STEP=status s=FF,FF,... (the values read)
 * Exit status 0. */
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned port;

static void st(void)
{
    int i;
    printf("MPUSTEP status s=");
    for (i = 0; i < 16; i++)
        printf("%s%02X", i ? "," : "", inportb((unsigned short)(port + 1)));
    printf("\n");
    fflush(stdout);
}

static void ack(void)
{
    uclock_t t0 = uclock();
    unsigned char s = 0xFF;
    while (uclock() - t0 < UCLOCKS_PER_SEC / 10 && ((s = inportb((unsigned short)(port + 1))) & 0x80))
        ;
    printf("MPUSTEP ack status=%02X data=%02X\n", s, inportb((unsigned short)port));
    fflush(stdout);
}

int main(int argc, char **argv)
{
    const char *step = argc > 2 ? argv[2] : "status";
    port = argc > 1 ? (unsigned)strtoul(argv[1], 0, 16) : 0x330;
    printf("MPUSTEP port=%X step=%s\n", port, step);
    fflush(stdout);
    if (!strcmp(step, "status")) {
        st();
    } else if (!strcmp(step, "data")) {
        printf("MPUSTEP data=%02X\n", inportb((unsigned short)port));
    } else if (!strcmp(step, "reset")) {
        outportb((unsigned short)(port + 1), 0xFF);
        printf("MPUSTEP reset written\n");
    } else if (!strcmp(step, "ack")) {
        ack();
    } else if (!strcmp(step, "uart")) {
        outportb((unsigned short)(port + 1), 0x3F);
        printf("MPUSTEP uart written\n");
    } else if (!strcmp(step, "all")) {
        st();
        outportb((unsigned short)(port + 1), 0xFF);
        printf("MPUSTEP reset written\n");
        fflush(stdout);
        ack();
        outportb((unsigned short)(port + 1), 0x3F);
        printf("MPUSTEP uart written\n");
    } else {
        printf("MPUSTEP unknown step\n");
        return 1;
    }
    fflush(stdout);
    printf("MPUSTEP done\n");
    return 0;
}
