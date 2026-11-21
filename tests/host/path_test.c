/* path_test: kernel/ndos/path.c, paths as DOS's short-name calls take them: drives, current folders, '.' and '..',
 * both separators, upper case, 8.3 cuts, lengths, 60h's and 47h's forms. "path_test: 0 failures" when all held. */
#include <stdio.h>
#include <string.h>

#include "../../kernel/ndos/path.h"

static int fails;
static struct dos_cds c;

static void res(const char *path, int want_r, int want_d, const char *want)
{
    char out[160];
    int d = -1, r = dos_resolve(&c, 437, path, &d, out, sizeof out);

    if (r != want_r || (!r && (d != want_d || strcmp(out, want)))) {
        printf("FAIL \"%s\": %d %c: \"%s\", want %d %c: \"%s\"\n", path, r, 'A' + d, r ? "" : out, want_r,
               'A' + want_d, want ? want : "");
        fails++;
    }
}

int main(void)
{
    char out[160], longp[300];
    int i;

    memset(&c, 0, sizeof c);
    c.valid = 1 << 0 | 1 << 2 | 1 << 3;         /* A:, C:, D: */
    c.current = 2;
    for (i = 0; i < DOS_DRIVES; i++)
        strcpy(c.cwd[i], "\\");
    strcpy(c.cwd[2], "\\GAMES\\DOOM");

    res("FILE.TXT", 0, 2, "\\GAMES\\DOOM\\FILE.TXT");
    res("file.txt", 0, 2, "\\GAMES\\DOOM\\FILE.TXT");
    res("\\autoexec.bat", 0, 2, "\\AUTOEXEC.BAT");
    res("/dos/edit.com", 0, 2, "\\DOS\\EDIT.COM");
    res("..\\HEXEN\\HEXEN.EXE", 0, 2, "\\GAMES\\HEXEN\\HEXEN.EXE");
    res("..\\..\\..\\..\\x", 0, 2, "\\X");               /* (above the root: stays at it) */
    res(".\\a\\.\\b", 0, 2, "\\GAMES\\DOOM\\A\\B");
    res("C:", 0, 2, "\\GAMES\\DOOM");
    res("C:\\", 0, 2, "\\");
    res("c:sub", 0, 2, "\\GAMES\\DOOM\\SUB");
    res("D:FILE", 0, 3, "\\FILE");
    res("a:\\x.y", 0, 0, "\\X.Y");
    res("C:\\games\\", 0, 2, "\\GAMES");                /* (a separator at the end: dropped) */
    res("C:\\\\games\\\\doom", 0, 2, "\\GAMES\\DOOM");
    res("C:\\LONGFILENAME.TEXT", 0, 2, "\\LONGFILE.TEX"); /* (cut to 8.3: to check against MS-DOS 7.1) */
    res("C:\\caf\x82.txt", 0, 2, "\\CAF\x90.TXT");
    res("C:\\FOO.", 0, 2, "\\FOO");
    res("B:\\X", 0x0F, 0, 0);
    res("Q:", 0x0F, 0, 0);
    res("1:\\X", 0x0F, 0, 0);
    res("C:\\a.b.c", 0x02, 0, 0);
    res("C:\\a+b", 0x02, 0, 0);
    res("C:\\.txt", 0x02, 0, 0);
    longp[0] = 0;
    strcat(longp, "C:");
    for (i = 0; i < 20; i++)
        strcat(longp, "\\DIRNAME");
    res(longp, 0x03, 0, 0);

    if (dos_truename(&c, 437, "doom.exe", out, sizeof out) || strcmp(out, "C:\\GAMES\\DOOM\\DOOM.EXE")) {
        printf("FAIL truename: \"%s\"\n", out);
        fails++;
    }
    if (dos_getcwd(&c, 0, out, sizeof out) || strcmp(out, "GAMES\\DOOM")) {
        printf("FAIL getcwd: \"%s\"\n", out);
        fails++;
    }
    if (dos_getcwd(&c, 1, out, sizeof out) || strcmp(out, "")) {
        printf("FAIL getcwd A:: \"%s\"\n", out);
        fails++;
    }
    if (dos_getcwd(&c, 2, out, sizeof out) != 0x0F) {
        printf("FAIL getcwd B: not 0Fh\n");
        fails++;
    }
    printf("path_test: %d failures\n", fails);
    return fails != 0;
}
