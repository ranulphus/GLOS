/* RUNOUT NAME PROG [ARGS...]: runs PROG with its stdout and stderr in
 * C:\TEST\RUNOUT.TXT, then sends what it wrote to COM1, a line each, as
 *     HX-OUT NAME <line>
 * (control characters as '.', lines cut at LINELEN bytes; of a long output
 * the first and the last KEEP lines) and ends with
 *     HX-RUN NAME code=<exit code>
 * Before the run it drops what is in the BIOS's key buffer (the harness's
 * F1 taps at boot would end a program's kbhit() loop at once) and says
 * HX-RUN-START NAME, which Loop A's --keys can time from. Loop A's way to see what a DOS program prints, DJGPP's fault
 * messages included, to compare hosts (tests/loopa/jobs.py djtst). A real-
 * mode program, so no DPMI client itself. Open Watcom, small model. */
#include <bios.h>
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>

#define OUTFILE  "C:\\TEST\\RUNOUT.TXT"
#define KEEP     40
#define LINELEN  160

static void ser(const char *s)
{
    for (; *s; s++) {
        unsigned n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u) ;
        outp(0x3F8, *s);
    }
}

int main(int argc, char **argv)
{
    static char line[LINELEN + 1], msg[LINELEN + 40];
    int fd, save1, save2, code, c, n, pass;
    long lines, total = 0;
    FILE *f;

    if (argc < 3) {
        ser("HX-RUN ? code=-1\r\n");
        return 1;
    }
    while (_bios_keybrd(_KEYBRD_READY))
        _bios_keybrd(_KEYBRD_READ);
    sprintf(msg, "HX-RUN-START %s\r\n", argv[1]);
    ser(msg);
    fd = open(OUTFILE, O_CREAT | O_TRUNC | O_WRONLY | O_BINARY, 0666);
    if (fd < 0) {
        sprintf(msg, "HX-RUN %s code=-2\r\n", argv[1]);
        ser(msg);
        return 1;
    }
    save1 = dup(1);
    save2 = dup(2);
    dup2(fd, 1);
    dup2(fd, 2);
    close(fd);
    code = spawnv(P_WAIT, argv[2], (const char * const *)argv + 2);
    dup2(save1, 1);
    dup2(save2, 2);
    close(save1);
    close(save2);

    for (pass = 0; pass < 2; pass++) {         /* count the lines, then send the ends */
        f = fopen(OUTFILE, "rb");
        lines = n = 0;
        while (f && ((c = getc(f)) != EOF || n)) {
            if (c == '\n' || c == EOF || n == LINELEN) {
                line[n] = 0;
                if (pass && (lines < KEEP || lines >= total - KEEP)) {
                    sprintf(msg, "HX-OUT %s %s\r\n", argv[1], line);
                    ser(msg);
                } else if (pass && lines == KEEP) {
                    sprintf(msg, "HX-OUT %s (%ld lines left out)\r\n", argv[1], total - 2L * KEEP);
                    ser(msg);
                }
                lines++;
                n = 0;
                if (c == EOF)
                    break;
                if (c == '\n')
                    continue;
            }
            if (c != '\r')
                line[n++] = (char)(c < ' ' || c > '~' ? '.' : c);
        }
        if (f)
            fclose(f);
        total = lines;
    }
    sprintf(msg, "HX-RUN %s code=%d\r\n", argv[1], code);
    ser(msg);
    return 0;
}
