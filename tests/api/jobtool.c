/* JOBTOOL: a plain DOS program (DJGPP; not an app) for API 2.0's JOB tests (tests/api/apitest.c, the job mode):
 * what a compiler or a filter does with its arguments, its environment, its standard input and its outputs.
 *   JOBTOOL args ...       how many arguments it was given, and each
 *   JOBTOOL where NAME     its folder, and the environment's NAME
 *   JOBTOOL echo           each line of its input in capitals on its output; a line that begins '!' on its error
 *                          output too; its exit code is how many lines it read
 *   JOBTOOL stream N       N lines, one every 50 ms; Ctrl+C or Ctrl+Break ends it with "interrupted" and code 7
 *   JOBTOOL both           a line on its output, one on its error output, code 3
 *   JOBTOOL chars          its input a character at a time through DOS's console calls (08h), to a full stop */
#include <ctype.h>
#include <dos.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile int stopped;

static void on_int(int sig)
{
    (void)sig;
    stopped = 1;
}

int main(int argc, char **argv)
{
    char line[256], cwd[128];
    int i, n = 0;
    setvbuf(stdout, 0, _IONBF, 0);
    if (argc > 1 && !strcmp(argv[1], "args")) {
        printf("%d", argc - 1);
        for (i = 1; i < argc; i++)
            printf(" [%s]", argv[i]);
        printf("\n");
        return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "where")) {
        const char *v = getenv(argv[2]);
        getcwd(cwd, sizeof cwd);
        printf("cwd=%s %s=%s\n", cwd, argv[2], v ? v : "(none)");
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "echo")) {
        while (fgets(line, sizeof line, stdin)) {
            n++;
            for (i = 0; line[i]; i++)
                line[i] = (char)toupper((unsigned char)line[i]);
            fputs(line, stdout);
            if (line[0] == '!')
                fputs(line, stderr);
        }
        printf("end %d\n", n);
        return n;
    }
    if (argc > 2 && !strcmp(argv[1], "stream")) {
        signal(SIGINT, on_int);
        for (i = 0; i < atoi(argv[2]) && !stopped; i++) {
            printf("line %d\n", i);
            delay(50);
        }
        if (stopped) {
            printf("interrupted\n");
            return 7;
        }
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "both")) {
        fputs("to out\n", stdout);
        fputs("to err\n", stderr);
        return 3;
    }
    if (argc > 1 && !strcmp(argv[1], "chars")) {
        union REGS r;
        for (;;) {
            r.h.ah = 0x08;
            int86(0x21, &r, &r);
            if (r.h.al == '.' || r.h.al == 0x1A)
                break;
            printf("<%c>", r.h.al);
            n++;
        }
        printf("\n");
        return n;
    }
    return 99;
}
