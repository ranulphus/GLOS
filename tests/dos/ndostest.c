/* ndostest: a program of GLOS's own for GLOS's own DOS (docs/v1/native-dos.md, native-DOS sessions, NS2), run in a
 * native machine by NDOS RUN: files, folders, searches, memory, EXEC of itself as a child and the child's exit
 * code, date and time; through Open Watcom's C library (whose start-up asks DOS what C programs ask) and its DOS
 * calls (dos.h). A line for each failure, then "NDOSTEST: n failures"; it exits with n. Run as
 * "NDOSTEST CHILD k word" it says "child: k word" and exits with k. It passes under any DOS from its own folder
 * (the Loop A case runs it under the DOS beside GLOS too, as the reference), "NATIVE" as its first argument
 * asking for GLOS's own DOS's answers besides (7.10, C:\NDOSTEST.EXE). */
#include <direct.h>
#include <dos.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <malloc.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL %s\n", what);
        fails++;
    }
}

static char big[16384];

static void files(void)
{
    static const char text[] = "line one\r\nline two\r\n";
    char buf[64];
    FILE *f;
    unsigned i;
    int fd;

    f = fopen("NT.TXT", "wb");
    check(f != NULL, "fopen for writing");
    if (!f)
        return;
    fwrite(text, 1, sizeof text - 1, f);
    fclose(f);
    f = fopen("NT.TXT", "rb");
    check(f != NULL, "fopen for reading");
    if (!f)
        return;
    check(fgets(buf, sizeof buf, f) != NULL && !strcmp(buf, "line one\r\n"), "fgets");
    fseek(f, 0, SEEK_END);
    check(ftell(f) == 20, "ftell at the end");
    fseek(f, 5, SEEK_SET);
    check(fread(buf, 1, 3, f) == 3 && !memcmp(buf, "one", 3), "fseek and fread");
    fclose(f);
    check(rename("NT.TXT", "NT2.TXT") == 0, "rename");
    check(access("NT.TXT", 0) != 0 && access("NT2.TXT", 0) == 0, "access after rename");
    check(remove("NT2.TXT") == 0, "remove");
    check(fopen("NT2.TXT", "rb") == NULL && errno == ENOENT, "fopen of a removed file: ENOENT");
    check(fopen("\\NODIR\\X.TXT", "rb") == NULL && errno == ENOENT, "fopen in no folder");
    /* a file over several clusters, by handle */
    for (i = 0; i < sizeof big; i++)
        big[i] = (char)(i * 7 + (i >> 8));
    fd = open("BIG.DAT", O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0666);
    check(fd >= 0, "open BIG.DAT");
    if (fd < 0)
        return;
    for (i = 0; i < 3; i++)
        check(write(fd, big, sizeof big) == (int)sizeof big, "write 16 KB");
    close(fd);
    fd = open("BIG.DAT", O_RDONLY | O_BINARY);
    check(fd >= 0 && filelength(fd) == 3L * sizeof big, "filelength 48 KB");
    lseek(fd, sizeof big + 100, SEEK_SET);
    memset(buf, 0, sizeof buf);
    check(read(fd, buf, 64) == 64 && !memcmp(buf, big + 100, 64), "read in the middle");
    close(fd);
    check(unlink("BIG.DAT") == 0, "unlink BIG.DAT");
}

static int ends(const char *s, const char *end)
{
    size_t a = strlen(s), b = strlen(end);

    return a >= b && !strcmp(s + a - b, end);
}

static void folders(void)
{
    char cwd[80];
    FILE *f;

    check(mkdir("NDIR") == 0, "mkdir");
    check(mkdir("NDIR") != 0, "mkdir twice");
    check(chdir("NDIR") == 0, "chdir");
    check(getcwd(cwd, sizeof cwd) != NULL && ends(cwd, "\\NDIR") && cwd[1] == ':', "getcwd ...\\NDIR");
    f = fopen("IN.TXT", "w");
    check(f != NULL, "a file in the folder");
    if (f) {
        fputs("x", f);
        fclose(f);
    }
    check(chdir("..") == 0, "chdir ..");
    check(rmdir("NDIR") != 0, "rmdir of a folder with a file");
    check(remove("NDIR\\IN.TXT") == 0, "remove NDIR\\IN.TXT");
    check(rmdir("NDIR") == 0, "rmdir");
    check(chdir("NDIR") != 0, "chdir to a removed folder");
}

/* (in its own folder, argv[0]'s: a shell's folder is its session's, and a run may start anywhere) */
static void searches(const char *self_path)
{
    struct find_t ft;
    int n = 0, self = 0;
    char spec[80];
    const char *slash = strrchr(self_path, '\\');
    size_t k = slash ? (size_t)(slash - self_path + 1) : 0;

    if (k > sizeof spec - 4)
        k = 0;
    memcpy(spec, self_path, k);
    strcpy(spec + k, "*.*");
    if (_dos_findfirst(spec, _A_NORMAL, &ft) == 0) {
        do {
            n++;
            if (!strcmp(ft.name, "NDOSTEST.EXE"))
                self = ft.size > 1000;
        } while (_dos_findnext(&ft) == 0);
    }
    check(n >= 1 && self, "find NDOSTEST.EXE among *.*");
    check(_dos_findfirst("*.ZZZ", _A_NORMAL, &ft) != 0, "find *.ZZZ: none");
}

static void memory(void)
{
    unsigned seg, max;
    void *p;

    check(_dos_allocmem(0x1000, &seg) == 0, "48h 64 KB");
    check(_dos_freemem(seg) == 0, "49h");
    check(_dos_allocmem(0xFFFF, &max) != 0 && max > 0x4000, "48h FFFFh: the largest block");
    p = malloc(20000);
    check(p != NULL, "malloc 20000");
    free(p);
}

static void programs(char *self)
{
    int r;

    r = spawnl(P_WAIT, self, self, "CHILD", "7", "word", NULL);
    check(r == 7, "spawnl: the child's 7");
    r = spawnl(P_WAIT, self, self, "CHILD", "0", "again", NULL);
    check(r == 0, "spawnl again: 0");
    r = spawnl(P_WAIT, "C:\\NONE.EXE", "C:\\NONE.EXE", NULL);
    check(r == -1 && errno == ENOENT, "spawnl of nothing");
}

static void clock_(void)
{
    struct dosdate_t d;
    struct dostime_t t;
    time_t now = time(NULL);

    _dos_getdate(&d);
    _dos_gettime(&t);
    check(d.year >= 2026 && d.month >= 1 && d.month <= 12 && d.day >= 1 && d.day <= 31, "the date");
    check(t.hour < 24 && t.minute < 60, "the time");
    check(now > 1700000000L, "time()");
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "CHILD")) {
        printf("child: %s %s\n", argv[2], argc > 3 ? argv[3] : "");
        return atoi(argv[2]);
    }
    printf("NDOSTEST %s, %d arguments, DOS %d.%02d\n", argv[0], argc - 1, _osmajor, _osminor);
    check(ends(argv[0], "\\NDOSTEST.EXE"), "argv[0]");
    check(argc == 3 && !strcmp(argv[2], "two"), "the arguments");
    if (argc > 1 && !strcmp(argv[1], "NATIVE")) {
        check(_osmajor == 7 && _osminor == 10, "the version 7.10");
        check(!strcmp(argv[0], "C:\\NDOSTEST.EXE"), "argv[0] C:\\NDOSTEST.EXE");
    }
    check(getenv("PATH") != NULL, "PATH in the environment");
    files();
    folders();
    searches(argv[0]);
    memory();
    programs(argv[0]);
    clock_();
    printf("NDOSTEST: %d failures\n", fails);
    return fails;
}
