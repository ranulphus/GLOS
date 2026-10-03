/* ECHOARGS [args...]: for the agent's capture test (tests/loopa/jobs.py ssh).
 * stdout: the arguments on one line, then a second line written through the
 * other console paths GLOS watches: INT 21h 09h ("nine "), 02h ("two "),
 * 06h ("six ") and INT 29h ("int29"). stderr: "echoargs: N arguments".
 * Exit code 7. Every line ends CR LF, as DOS text output does. */
#include <dos.h>
#include <stdio.h>

static void dos_char(unsigned char fn, char c)
{
    union REGS r;
    r.h.ah = fn;
    r.h.dl = (unsigned char)c;
    intdos(&r, &r);
}

static void int29(const char *s)
{
    union REGS r;
    for (; *s; s++) {
        r.h.al = (unsigned char)*s;
        int86(0x29, &r, &r);
    }
}

int main(int argc, char **argv)
{
    static char nine[] = "nine $";
    union REGS r;
    struct SREGS sr;
    int i;

    for (i = 1; i < argc; i++)
        printf(i > 1 ? " %s" : "%s", argv[i]);
    printf("\n");
    fflush(stdout);
    segread(&sr);
    r.h.ah = 0x09;
    r.x.dx = (unsigned)nine;
    intdosx(&r, &r, &sr);
    dos_char(0x02, 't'); dos_char(0x02, 'w'); dos_char(0x02, 'o'); dos_char(0x02, ' ');
    dos_char(0x06, 's'); dos_char(0x06, 'i'); dos_char(0x06, 'x'); dos_char(0x06, ' ');
    int29("int29\r\n");
    fprintf(stderr, "echoargs: %d arguments\n", argc - 1);
    return 7;
}
