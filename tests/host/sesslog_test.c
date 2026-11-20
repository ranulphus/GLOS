/* A session's log (apps/shared/sesslog.c): what is kept of what a terminal is sent, whole and cut at every
 * place; the file begun, written, added to; rubbish, which must do no harm.
 *
 *   sesslog_test WORKDIR      (prints "sesslog_test: N failures (M checks)") */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/sesslog.h"

static int fails, checks;

#define CHECK(c, ...) do { checks++; if (!(c)) { printf("sesslog_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* in, as a terminal is sent it, gives want: at once, and cut in two at every place. */
static void gives(const char *in, int n, const char *want)
{
    char out[600], two[600];
    unsigned st = 0;
    int k, cut;
    if (n < 0)
        n = (int)strlen(in);
    k = sesslog_text(&st, (const unsigned char *)in, n, out);
    out[k] = 0;
    CHECK(!strcmp(out, want), "%s: \"%s\", not \"%s\"", in, out, want);
    for (cut = 0; cut <= n; cut++) {
        st = 0;
        k = sesslog_text(&st, (const unsigned char *)in, cut, two);
        k += sesslog_text(&st, (const unsigned char *)in + cut, n - cut, two + k);
        two[k] = 0;
        CHECK(!strcmp(two, want), "cut at %d: \"%s\", not \"%s\"", cut, two, want);
    }
}

static int slurp(const char *path, char *out, int room)
{
    FILE *f = fopen(path, "rb");
    int n;
    if (!f)
        return -1;
    n = (int)fread(out, 1, (size_t)room - 1, f);
    out[n] = 0;
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    char path[400], text[4000], *p;
    unsigned seed = 4242;
    int i, j;
    if (argc < 2)
        return 2;

    gives("plain text\r\n", -1, "plain text\n");
    gives("tabs\tstay, \a bells \b and backspaces go\r\n", -1, "tabs\tstay,  bells  and backspaces go\n");
    gives("\033[1;32mgreen\033[0m and \033[38;5;196mred\033[m\r\n", -1, "green and red\n");
    gives("\033[2J\033[H\033[?25l$ ls\033[K\r\n", -1, "$ ls\n");
    gives("\033]0;a title\007after it", -1, "after it");
    gives("\033]2;ended the other way\033\\after it", -1, "after it");
    gives("\033P1$r0m\033\\x\033_app\033\\y\033^pm\033\\z\033Xsos\033\\.", -1, "xyz.");
    gives("\033(B\033)0line\033=\0337\0338\033M drawing", -1, "line drawing");
    gives("caf\303\251 \342\224\200\342\224\200 \360\237\230\200", -1, "caf\303\251 \342\224\200\342\224\200 \360\237\230\200");
    gives("a\177b\0c", 5, "abc");
    gives("\033[31", -1, "");
    gives("\033[31\r\nnext", -1, "\nnext");                      /* (a sequence never ended: the line's end stays) */
    gives("\033[3\033[1mbold", -1, "bold");                      /* (cut short by another) */
    gives("\033\033[1mx", -1, "x");
    gives("\033]0;never ended", -1, "");
    gives("\033]0;an ESC \033 in it\007out", -1, "out");
    gives("progress 10%\rprogress 90%\rdone\r\n", -1, "progress 10%progress 90%done\n");

    /* the file: begun under a line that says what and when; what comes is in it as it comes; closed, opened
       again: added to under another such line */
    snprintf(path, sizeof path, "%s/SESSION.LOG", argv[1]);
    remove(path);
    CHECK(!sesslog_on(), "on before it is open");
    sesslog_write("nothing open", 12);
    CHECK(sesslog_open(path, "ssh office") == 0 && sesslog_on(), "opened");
    sesslog_write("\033[1m$ \033[0mls\r\n", 14);
    sesslog_write("a.txt  b", 8);
    CHECK(slurp(path, text, sizeof text) > 0 && strstr(text, "$ ls\na.txt  b"), "in the file as it comes: %s", text);
    sesslog_write(".txt\r\n\033[3", 9);
    sesslog_write("2mgreen\033[m\r\n", 12);
    sesslog_close();
    CHECK(!sesslog_on(), "on after its close");
    slurp(path, text, sizeof text);
    CHECK(!strncmp(text, "=== ssh office, 20", 18) && (p = strstr(text, " ===\n")) != NULL
          && !strcmp(p + 5, "$ ls\na.txt  b.txt\ngreen\n"), "the first part: %s", text);
    CHECK(sesslog_open(path, "ssh office") == 0, "opened again");
    sesslog_write("more\r\n", 6);
    sesslog_close();
    slurp(path, text, sizeof text);
    CHECK((p = strstr(text, "green\n\n=== ssh office, 20")) != NULL && (p = strstr(p + 8, " ===\n")) != NULL
          && !strcmp(p + 5, "more\n"), "added to: %s", text);
    snprintf(path, sizeof path, "%s/no/such/folder/SESSION.LOG", argv[1]);
    CHECK(sesslog_open(path, "ssh office") == -1 && !sesslog_on(), "a file that can't be written");

    /* rubbish, in pieces: no more comes out than went in, and nothing that is no text */
    for (j = 0; j < 2000; j++) {
        unsigned char in[300];
        char out[300];
        unsigned st = 0;
        int n, k, at = 0, total = 0;
        seed = seed * 1103515245u + 12345u;
        n = (int)((seed >> 16) % sizeof in);
        for (i = 0; i < n; i++) {
            seed = seed * 1103515245u + 12345u;
            in[i] = (seed >> 24) % 5 ? (unsigned char)(seed >> 16) : (unsigned char)"\033[]\\\007P;\r\n"[(seed >> 16) % 10];
        }
        while (at < n) {
            int part;
            seed = seed * 1103515245u + 12345u;
            part = 1 + (int)((seed >> 16) % 40);
            if (part > n - at)
                part = n - at;
            k = sesslog_text(&st, in + at, part, out + total);
            CHECK(k >= 0 && k <= part, "rubbish %d: %d bytes out of %d", j, k, part);
            total += k, at += part;
        }
        for (i = 0; i < total; i++)
            if ((unsigned char)out[i] < 32 && out[i] != '\n' && out[i] != '\t')
                break;
        CHECK(i == total && st <= 5, "rubbish %d: a control character kept, or a state that is none (%u)", j, st);
    }
    printf("sesslog_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
