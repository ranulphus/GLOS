/* GLOSLOG: GLOS's log (its last lines, GLOS_FN_STAT's GLOS_STAT_LOG) on
 * stdout, for a PC with neither a serial cable nor a network to read it by
 * (a bench PC's first boots). DJGPP, with sdk/libglos.
 *
 *   GLOSLOG              the log now
 *   GLOSLOG /WAIT N      after N seconds (AUTOEXEC.BAT: GLOSLOG /WAIT 45 >
 *                        C:\GLOS\BOOT.LOG, read from another OS after)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dpmi.h>

#include <glos/api.h>

/* N seconds by GLOS's clock (GLOS_FN_TIME, for any program), yielding meanwhile: DJGPP's sleep() waits on
   DOS's clock, which doesn't move for a program AUTOEXEC.BAT runs while GLOS starts as the shell. */
static void wait_s(unsigned n)
{
    unsigned long t0, t;
    if (glos_time(&t0, 0) < 0)
        return;
    do
        __dpmi_yield();
    while (glos_time(&t, 0) >= 0 && t - t0 < n);
}

int main(int argc, char **argv)
{
    static char buf[65536];
    struct glos_stat_system s;
    int n;
    if (argc == 3 && !strcasecmp(argv[1], "/WAIT") && atoi(argv[2]) > 0)
        wait_s((unsigned)atoi(argv[2]));
    else if (argc != 1) {
        fprintf(stderr, "usage: GLOSLOG [/WAIT SECONDS]\n");
        return 2;
    }
    /* GLOS_FN_STAT is for any program: no glos_init (a program a batch file started can't be an app) */
    memset(&s, 0, sizeof s);
    s.size = sizeof s;
    if (glos_stat(GLOS_STAT_SYSTEM, &s, sizeof s) > 0)
        printf("GLOSLOG uptime=%us nic=%s ip=%u.%u.%u.%u rx=%u tx=%u host=%s card=%s mem_kb=%u\n", s.uptime_ms / 1000,
               s.nic, s.ip[0], s.ip[1], s.ip[2], s.ip[3], s.net_rx, s.net_tx, s.hostname, s.card, s.mem_kb);
    if ((n = glos_stat(GLOS_STAT_LOG, buf, sizeof buf - 1)) < 0) {
        printf("GLOSLOG: no log from GLOS (error %d)\n", glos_error());
        return 1;
    }
    buf[n] = 0;
    fputs(buf, stdout);
    return 0;
}
