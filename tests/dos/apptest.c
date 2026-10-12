/* APPTEST: a GLOS app for the apps suite (tests/loopa/jobs.py apps; M6 step A).
 *   APPTEST run NAME SECONDS   becomes an app; in C:\APPT\NAME, its own
 *                              directory, for SECONDS: sleeps 50 ms at a time,
 *                              and between sleeps checks that the directory
 *                              is still its own, that a directory search it
 *                              has under way goes on where it was (its DTA),
 *                              that the file it writes holds its own bytes,
 *                              that the FPU still has its sum, and that the
 *                              clock moved as far as it slept. Ends with
 *                              APP-OK and exit code 40 + NAME's first letter
 *                              - 'A', or APP-FAIL and 1.
 *   APPTEST spin               becomes an app and loops for ever, calling
 *                              nothing
 *   APPTEST crash              becomes an app and writes through a null
 *                              pointer, with no handler of its own (as
 *                              CRASHME)
 *   APPTEST exit N             becomes an app and ends with code N
 *   APPTEST twice              the second glos_init() is refused
 *   APPTEST child PROGRAM      becomes an app and runs PROGRAM (spawn) with
 *                              the arguments "from an app"; ends with its
 *                              exit code
 *   APPTEST key                becomes an app and waits for a line from the
 *                              keyboard, in DOS
 *   APPTEST sleep SECONDS      becomes an app and sleeps
 *   APPTEST uclock SECONDS     becomes an app and reads DJGPP's uclock() as
 *                              fast as it can: it must never go back, and
 *                              must have moved as far as GLOS's clock
 *   APPTEST timer HZ SECONDS [SLACK]
 *                              becomes an app, sets the timer to HZ and
 *                              counts its interrupts in a handler of its
 *                              own, while it sleeps (the API's, the BIOS's
 *                              wait), halts and spins in turn: HZ a second,
 *                              the counter always below what it set, and the
 *                              BIOS's own tick (the system's) at 18.2. SLACK:
 *                              the interrupts in a hundred that may be
 *                              missing (3)
 *   APPTEST drain              never asks: takes the keys that wait
 *   APPTEST kb                 never asks: says whether a key waits (kbhit(),
 *                              three times), for the keyboard's state as a
 *                              program made an app without asking sees it
 *   APPTEST plain              never asks: an ordinary session
 * Without GLOS (CWSDPMI) every mode says APP-NOGLOS and ends with 2. */
#include <dir.h>
#include <dos.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <sys/farptr.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/exceptn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glos/api.h>

static int fails;
static volatile int *volatile null_ptr;

static void bad(const char *what, long a, long b)
{
    printf("APP-BAD %s got=%ld want=%ld\n", what, a, b);
    fails++;
}

static int run(const char *name, int seconds)
{
    char dir[64], cwd[128], want[64], path[32], buf[256];
    struct ffblk ff;
    unsigned t0, t, slept = 0, loops = 0;
    volatile double sum = 0.0;
    double check = 0.0;
    int i, fd, more, seen = 0;

    mkdir("C:\\APPT", 0777);
    sprintf(dir, "C:\\APPT\\%s", name);
    mkdir(dir, 0777);
    if (chdir(dir) != 0) {
        printf("APP-FAIL chdir %s errno=%d\n", dir, errno);
        return 1;
    }
    sprintf(want, "c:/appt/%s", name);
    for (i = 0; want[i]; i++)
        if (want[i] >= 'A' && want[i] <= 'Z')
            want[i] += 32;
    for (i = 0; i < 12; i++) {                  /* files to search through, a few each turn */
        sprintf(path, "F%02d.%s", i, name);
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666);
        if (fd < 0 || write(fd, name, strlen(name)) != (int)strlen(name))
            bad("create", errno, 0);
        close(fd);
    }
    more = findfirst("F*.*", &ff, 0) == 0;
    t0 = glos_ticks();
    printf("APP-RUN name=%s id=%u\n", name, glos_id());
    fflush(stdout);
    while (glos_ticks() - t0 < (unsigned)seconds * 1000) {
        t = glos_ticks();
        glos_sleep(50);
        t = glos_ticks() - t;
        if (t < 45)
            bad("sleep", t, 50);
        slept += t;
        loops++;
        sum += 1.0 / (double)(loops + name[0]);
        check += 1.0 / (double)(loops + name[0]);
        if (!getcwd(cwd, sizeof cwd) || strcmp(cwd, want) != 0) {
            printf("APP-BAD cwd got=%s want=%s\n", cwd, want);
            fails++;
        }
        if (more) {                             /* one more of the search: it must be one of ours */
            if (strcmp(strrchr(ff.ff_name, '.') + 1, name) != 0) {
                printf("APP-BAD find got=%s\n", ff.ff_name);
                fails++;
            }
            seen++;
            more = findnext(&ff) == 0;
        }
        sprintf(path, "DATA.%s", name);
        memset(buf, name[0], sizeof buf);
        fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_BINARY, 0666);
        if (fd < 0 || write(fd, buf, sizeof buf) != sizeof buf)
            bad("write", errno, 0);
        glos_yield();
        memset(buf, 0, sizeof buf);
        if (lseek(fd, 0, SEEK_SET) != 0 || read(fd, buf, sizeof buf) != sizeof buf)
            bad("read", errno, 0);
        for (i = 0; i < (int)sizeof buf; i++)
            if (buf[i] != name[0]) {
                bad("data", buf[i], name[0]);
                break;
            }
        close(fd);
        if (fails > 8)
            break;
    }
    if (sum != check)
        bad("fpu", (long)(sum * 1e6), (long)(check * 1e6));
    if (seen != 12 && loops >= 12)
        bad("found", seen, 12);
    for (i = 0; i < 12; i++) {
        sprintf(path, "F%02d.%s", i, name);
        unlink(path);
    }
    sprintf(path, "DATA.%s", name);
    unlink(path);
    chdir("C:\\");
    rmdir(dir);
    printf("%s name=%s loops=%u slept=%u found=%d sum=%ld\n", fails ? "APP-FAIL" : "APP-OK", name, loops, slept, seen,
           (long)(sum * 1e6));
    return fails ? 1 : 40 + name[0] - 'A';
}

static int uclk(int seconds)
{
    uclock_t u0, u, last;
    unsigned t0, t, n = 0, back = 0, ms, ms2;

    last = u0 = uclock();
    t0 = glos_ticks();
    while (glos_ticks() - t0 < (unsigned)seconds * 1000) {
        u = uclock();
        if (u < last)
            back++;
        last = u;
        if (!(++n & 255))
            glos_yield();
    }
    u = uclock();                               /* GLOS's clock between two readings: other apps may run anywhere */
    t = glos_ticks() - t0;
    ms = (unsigned)((u - u0) * 1000 / UCLOCKS_PER_SEC);
    ms2 = (unsigned)((uclock() - u0) * 1000 / UCLOCKS_PER_SEC);
    if (back || u < last || t + 60 < ms || t > ms2 + 60)       /* (60 ms: a BIOS tick, and the ms's rounding) */
        fails++;
    printf("%s ms=%u..%u clock=%u reads=%u back=%u\n", fails ? "APP-UCLOCK-FAIL" : "APP-UCLOCK-OK", ms, ms2, t, n, back);
    return fails ? 1 : 0;
}

static volatile unsigned tics;

static void tick(void)
{
    tics++;
    outportb(0x20, 0x20);
}

/* slack: how many of a hundred interrupts may be missing. */
static int timer(int hz, int seconds, int slack)
{
    _go32_dpmi_seginfo old, mine;
    unsigned count = 1193182 / hz, t0, t, n, c, want, phase = 0, over = 0, bios, bwant, s0;

    _go32_dpmi_get_protected_mode_interrupt_vector(8, &old);
    mine.pm_offset = (unsigned long)tick;
    mine.pm_selector = _go32_my_cs();
    _go32_dpmi_allocate_iret_wrapper(&mine);
    _go32_dpmi_set_protected_mode_interrupt_vector(8, &mine);
    outportb(0x43, 0x34);
    outportb(0x40, count & 0xFF);
    outportb(0x40, count >> 8);
    bios = _farpeekl(_dos_ds, 0x46C);
    t0 = glos_ticks();
    tics = 0;
    while ((t = glos_ticks() - t0) < (unsigned)seconds * 1000) {
        switch (phase++ % 4) {
        case 0: glos_sleep(30); break;
        case 1: delay(30); break;
        case 2: __asm__ __volatile__("hlt"); break;
        default: for (s0 = glos_ticks(); glos_ticks() - s0 < 5;) ;     /* busy, for 5 ms */
        }
        outportb(0x43, 0x00);
        c = inportb(0x40);
        c |= inportb(0x40) << 8;
        if (c > count)
            over++;
    }
    n = tics;
    t = glos_ticks() - t0;
    bios = _farpeekl(_dos_ds, 0x46C) - bios;
    outportb(0x43, 0x34);
    outportb(0x40, 0xFF);
    outportb(0x40, 0xFF);
    _go32_dpmi_set_protected_mode_interrupt_vector(8, &old);
    _go32_dpmi_free_iret_wrapper(&mine);
    want = (unsigned)hz * t / 1000;
    bwant = t * 182 / 10000;
    if (n + want * slack / 100 + 2 < want || n > want + want / 33 + 2 || over || bios + 3 < bwant || bios > bwant + 3)
        fails++;
    printf("%s hz=%d ms=%u tics=%u want=%u over=%u bios=%u of %u turns=%u\n", fails ? "APP-TIMER-FAIL" : "APP-TIMER-OK",
           hz, t, n, want, over, bios, bwant, phase);
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    volatile unsigned n = 0;

    if (!strcmp(mode, "drain")) {               /* keys left in the BIOS's buffer (the boot's) out of it */
        int n = 0;
        while (kbhit()) {
            getkey();
            n++;
        }
        printf("APP-DRAIN %d\n", n);
        return 0;
    }
    if (!strcmp(mode, "kb")) {
        int a = kbhit(), b = kbhit(), c = kbhit();
        printf("APP-KB %d %d %d head=%04x tail=%04x\n", a, b, c, _farpeekw(_dos_ds, 0x41A), _farpeekw(_dos_ds, 0x41C));
        return 0;
    }
    if (!strcmp(mode, "plain")) {
        printf("APP-PLAIN version=%x\n", glos_version());
        return 7;
    }
    if (glos_init() != 0) {
        if (!glos_error()) {
            printf("APP-NOGLOS\n");
            return 2;
        }
        printf("APP-REFUSED error=%d reason=%d\n", glos_error(), glos_reason());
        return 3;
    }
    if (!strcmp(mode, "run") && argc > 3)
        return run(argv[2], atoi(argv[3]));
    if (!strcmp(mode, "spin")) {
        printf("APP-SPIN id=%u\n", glos_id());
        fflush(stdout);
        for (;;)
            n++;
    }
    if (!strcmp(mode, "crash")) {
        printf("APP-CRASH id=%u\n", glos_id());
        fflush(stdout);
        __djgpp_exception_toggle();             /* its handlers back to the host: the fault is the host's to report */
        *null_ptr = 1;                          /* the page DJGPP's start-up uncommitted: a page fault */
        return 9;
    }
    if (!strcmp(mode, "exit") && argc > 2) {
        printf("APP-EXIT id=%u\n", glos_id());
        return atoi(argv[2]);
    }
    if (!strcmp(mode, "twice")) {
        int r = glos_init();
        printf("APP-TWICE r=%d error=%d reason=%d\n", r, glos_error(), glos_reason());
        return r == -1 && glos_error() == GLOS_E_INIT && glos_reason() == GLOS_INIT_ALREADY ? 0 : 1;
    }
    if (!strcmp(mode, "child") && argc > 2) {
        int r;
        printf("APP-CHILD id=%u\n", glos_id());
        fflush(stdout);
        r = spawnl(P_WAIT, argv[2], argv[2], "from", "an", "app", (char *)0);
        printf("APP-CHILD-BACK r=%d ticks=%u\n", r, glos_ticks() != 0);
        return r;
    }
    if (!strcmp(mode, "uclock") && argc > 2)
        return uclk(atoi(argv[2]));
    if (!strcmp(mode, "timer") && argc > 3)
        return timer(atoi(argv[2]), atoi(argv[3]), argc > 4 ? atoi(argv[4]) : 3);
    if (!strcmp(mode, "key")) {
        char line[40];
        printf("APP-KEY id=%u\n", glos_id());
        fflush(stdout);
        return read(0, line, sizeof line) > 0 ? 0 : 1;
    }
    if (!strcmp(mode, "sleep") && argc > 2) {
        printf("APP-SLEEP id=%u\n", glos_id());
        fflush(stdout);
        glos_sleep((unsigned)atoi(argv[2]) * 1000);
        printf("APP-SLEPT\n");
        return 0;
    }
    printf("usage: APPTEST run NAME SECONDS | spin | crash | exit N | twice | child PROGRAM | key | sleep SECONDS |"
           " uclock SECONDS | timer HZ SECONDS | plain\n");
    return 2;
}
