/* GLOS.DAT's list (kernel/core/kdata.c). */
#include <stdio.h>
#include <string.h>

#include "kdata.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("kdata_test: line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static struct kdata d, e;

int main(void)
{
    char t[KDATA_TEXT + 8], big[KDATA_TEXT + 8], file[8192];
    u32 n, i;

    CHECK(kd_count(&d, "recent") == 0 && kd_get(&d, "recent", 0, t, sizeof t) == -1);
    CHECK(kd_add(&d, "recent", "NOTEPAD.EXE\tC:\\A.TXT", 1) == 0);
    CHECK(kd_add(&d, "sched", "backup\tdaily 03:00\t5\t0\tC:\\B.BAT", 0) == 0);
    CHECK(kd_add(&d, "recent", "NOTEPAD.EXE\tC:\\B.TXT", 1) == 0);       /* the newest first, among its kind */
    CHECK(kd_add(&d, "sched", "tidy\tstart\t5\t0\tC:\\T.BAT", 0) == 0);
    CHECK(kd_count(&d, "recent") == 2 && kd_count(&d, "sched") == 2 && kd_count(&d, "rec") == 0 && d.n == 4);
    CHECK(kd_get(&d, "recent", 0, t, sizeof t) == 20 && !strcmp(t, "NOTEPAD.EXE\tC:\\B.TXT"));
    CHECK(kd_get(&d, "recent", 1, t, sizeof t) == 20 && !strcmp(t, "NOTEPAD.EXE\tC:\\A.TXT"));
    CHECK(kd_get(&d, "recent", 2, t, sizeof t) == -1 && kd_get(&d, "recent", 0, t, 20) == -1);
    CHECK(kd_get(&d, "sched", 1, t, sizeof t) > 0 && !strncmp(t, "tidy\t", 5));
    CHECK(kd_set(&d, "sched", 0, "backup\tdaily 03:00\t9\t1\tC:\\B.BAT") == 0 && kd_get(&d, "sched", 0, t, sizeof t) > 0
          && strstr(t, "\t9\t1\t"));
    CHECK(kd_set(&d, "sched", 2, "x") == -1 && kd_set(&d, "sched", 0, "two\nlines") == -1);

    /* What a record may not be. */
    memset(big, 'x', sizeof big);
    big[KDATA_TEXT + 1] = 0;
    CHECK(kd_add(&d, "recent", big, 0) == -1);
    big[KDATA_TEXT] = 0;
    CHECK(kd_add(&d, "recent", big, 0) == 0 && kd_remove(&d, "recent", 2) == 0);
    CHECK(kd_add(&d, "", "x", 0) == -1 && kd_add(&d, "two words", "x", 0) == -1 && kd_add(&d, "eightlong", "x", 0) == -1
          && kd_add(&d, "recent", "a\rb", 0) == -1 && d.n == 4);

    /* The file: written and read back the same; lines that are no records dropped. */
    n = kd_write(&d, file, sizeof file);
    CHECK(n > 0 && n < sizeof file && file[n - 1] == '\n');
    kd_read(&e, file, n);
    CHECK(e.n == 4 && !memcmp(d.line, e.line, sizeof d.line[0] * 4));
    strcpy(file, "recent A\tB\r\n\r\nnokindhere\r\n toolongakindname x\r\nsched s\tstart\t1\t0\tX\n\x1Arecent after the end\r\n");
    kd_read(&e, file, (u32)strlen(file));
    CHECK(e.n == 2 && kd_count(&e, "recent") == 1 && kd_count(&e, "sched") == 1);
    CHECK(kd_get(&e, "recent", 0, t, sizeof t) == 3 && !strcmp(t, "A\tB"));

    /* Removing; filling. */
    CHECK(kd_remove(&d, "recent", 0) == 0 && kd_get(&d, "recent", 0, t, sizeof t) == 20 && strstr(t, "A.TXT"));
    CHECK(kd_remove(&d, "recent", 1) == -1 && kd_remove(&d, "recent", 0) == 0 && kd_count(&d, "recent") == 0 && d.n == 2);
    for (i = 0; i < KDATA_MAX - 2; i++)
        CHECK(kd_add(&d, "recent", "P\tX", i & 1) == 0);
    CHECK(kd_add(&d, "recent", "P\tX", 0) == -1 && kd_add(&d, "recent", "P\tX", 1) == -1 && d.n == KDATA_MAX);
    CHECK(kd_count(&d, "sched") == 2 && kd_get(&d, "sched", 1, t, sizeof t) > 0 && !strncmp(t, "tidy", 4));
    n = kd_write(&d, file, 100);                        /* (a buffer too small: whole lines only) */
    CHECK(n <= 100 && (!n || file[n - 1] == '\n'));

    printf("kdata_test: %d failures\n", fails);
    return fails != 0;
}
