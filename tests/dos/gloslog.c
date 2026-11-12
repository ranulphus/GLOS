/* GLOSLOG: GLOS's log (what it has kept: SYS's LOG) on stdout, for a PC
 * with neither a serial cable nor a network to read it by (a bench PC's
 * first boots). DJGPP, with libglos.
 *
 *   GLOSLOG              the log now
 *   GLOSLOG /WAIT N      after N seconds (AUTOEXEC.BAT: GLOSLOG /WAIT 45 >
 *                        C:\GLOS\BOOT.LOG, read from another OS after)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dpmi.h>

#include <glos/glos2.h>

/* N seconds by GLOS's clock (TIME, for any program), yielding meanwhile: DJGPP's sleep() waits on
   DOS's clock, which doesn't move for a program AUTOEXEC.BAT runs while GLOS starts as the shell. */
static void wait_s(unsigned n)
{
    struct glos_time t0 = { sizeof t0, 0, 0, 0 }, t = { sizeof t, 0, 0, 0 };
    if (glos_time(&t0) != 0)
        return;
    do
        __dpmi_yield();
    while (glos_time(&t) == 0 && t.utc - t0.utc < n);
}

int main(int argc, char **argv)
{
    static char buf[4096];
    struct glos_sys_system s;
    struct glos_sys_memory m;
    unsigned pos = 0, next = 0;
    int n, any = 0;
    if (argc == 3 && !strcasecmp(argv[1], "/WAIT") && atoi(argv[2]) > 0)
        wait_s((unsigned)atoi(argv[2]));
    else if (argc != 1) {
        fprintf(stderr, "usage: GLOSLOG [/WAIT SECONDS]\n");
        return 2;
    }
    /* SYS's STAT and LOG are for any program: no glos_init (a program a batch file started can't be an app) */
    memset(&s, 0, sizeof s);
    memset(&m, 0, sizeof m);
    if (glos_sys_stat(GLOS_STAT_SYSTEM, &s, sizeof s, 0, 0) == 0 && glos_sys_stat(GLOS_STAT_MEMORY, &m, sizeof m, 0, 0) == 0)
        printf("GLOSLOG uptime=%us nic=%s ip=%u.%u.%u.%u rx=%u tx=%u host=%s card=%s mem_kb=%u\n", s.uptime_ms / 1000,
               s.nic, s.ip[0], s.ip[1], s.ip[2], s.ip[3], s.net_rx, s.net_tx, s.hostname, s.card, m.total_kb);
    while ((n = glos_sys_log(pos, buf, sizeof buf, &next, 0)) > 0) {    /* from the oldest line kept to the newest */
        fwrite(buf, 1, (size_t)n, stdout);
        pos = next;
        any = 1;
    }
    if (n < 0 && !any) {
        printf("GLOSLOG: no log from GLOS (error %d)\n", glos_error());
        return 1;
    }
    return 0;
}
