/* DOSCALLS: what a DOS call costs a DJGPP program, as a plain DOS program (or a session under GLOS, on the
 * system VM's own thread) and then, after glos_init, as a GLOS app (each call of its goes to the system VM's
 * thread and back: docs/milestones-m6.md). Found wanting in phase E: a small job of Files' took a second on
 * the 486's profile, most of it in the files' own calls.
 *
 *     DOSCALLS [app] [FILE]      FILE: a file to find, open and read (itself, if none is named)
 *
 * Each kind of call is made a number of times, three times over, and the least time taken: microseconds a
 * call, by the timer chip (uclock).
 *     DOSCALLS as=dos|app ver=N find=N open=N read=N stat=N fopen=N
 * ver: INT 21h 30h, DOS's version (the call alone); find: findfirst of the file; open: _dos_open and
 * _dos_close; read: 512 bytes of it by _dos_read after a seek to its start; stat and fopen: the C library's own
 * (stat; fopen and fclose). */
#include <dir.h>
#include <dos.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <glos/glos.h>

static const char *file;
static int handle;
static char buf[512];

static void c_ver(void)
{
    union REGS r;
    r.h.ah = 0x30;
    int86(0x21, &r, &r);
}

static void c_find(void)
{
    struct ffblk f;
    findfirst(file, &f, 0x37);
}

static void c_open(void)
{
    int h;
    if (_dos_open(file, O_RDONLY, &h) == 0)
        _dos_close(h);
}

static void c_read(void)
{
    unsigned got;
    lseek(handle, 0, SEEK_SET);
    _dos_read(handle, buf, sizeof buf, &got);
}

static void c_stat(void)
{
    struct stat st;
    stat(file, &st);
}

static void c_fopen(void)
{
    FILE *f = fopen(file, "rb");
    if (f)
        fclose(f);
}

static unsigned long per(void (*fn)(void), int n)
{
    unsigned long best = 0xFFFFFFFFul;
    int round, i;
    for (round = 0; round < 3; round++) {
        uclock_t t0 = uclock(), t;
        for (i = 0; i < n; i++)
            fn();
        t = uclock() - t0;
        if (t > 0 && (unsigned long)(t * 1000000 / UCLOCKS_PER_SEC / n) < best)
            best = (unsigned long)(t * 1000000 / UCLOCKS_PER_SEC / n);
    }
    return best;
}

static void measure(const char *as)
{
    unsigned long ver = per(c_ver, 400), find = per(c_find, 100), open_ = per(c_open, 100), rd = per(c_read, 100);
    unsigned long st = per(c_stat, 30), fo = per(c_fopen, 30);
    printf("DOSCALLS as=%s ver=%lu find=%lu open=%lu read=%lu stat=%lu fopen=%lu\n", as, ver, find, open_, rd, st, fo);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int app = argc > 1 && !strcmp(argv[1], "app");
    if (argc > 2 && !strcmp(argv[1], "ver")) {  /* DOSCALLS ver N: that many of the plainest call, and no more (for
                                                   a count of GLOS's traps a call: glos stat's "gp" before and after) */
        long n = atol(argv[2]);
        while (n-- > 0)
            c_ver();
        return 0;
    }
    file = argc > (app ? 2 : 1) ? argv[app ? 2 : 1] : argv[0];
    if (_dos_open(file, O_RDONLY, &handle) != 0) {
        printf("DOSCALLS: no %s\n", file);
        return 1;
    }
    measure("dos");
    if (app) {
        if (glos_init("DOSCALLS", "What a DOS call costs", "test") != 0) {
            printf("DOSCALLS: no GLOS to be an app of\n");
            return 2;
        }
        measure("app");
    }
    _dos_close(handle);
    return 0;
}
