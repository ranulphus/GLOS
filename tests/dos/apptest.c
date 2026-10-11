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
 *   APPTEST plain              never asks: an ordinary session
 * Without GLOS (CWSDPMI) every mode says APP-NOGLOS and ends with 2. */
#include <dir.h>
#include <dos.h>
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

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "";
    volatile unsigned n = 0;

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
    printf("usage: APPTEST run NAME SECONDS | spin | crash | exit N | twice | child PROGRAM | plain\n");
    return 2;
}
