/* A session's log (sesslog.h). Plain C and stdio: the session programs', and the host's test's
   (tests/host/sesslog_test.c). */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sesslog.h"

/* Where the chooser is in what it reads: plain text; after ESC; in a control sequence (ESC [ ... to its final
   byte); in a string (ESC ] or P, X, ^, _ ... to BEL or ESC \); after ESC in a string; after ESC and a byte
   that takes one more (a character set's choice: ESC ( B). */
enum { S_TEXT, S_ESC, S_CSI, S_STR, S_STR_ESC, S_ONE };

int sesslog_text(unsigned *state, const unsigned char *in, int n, char *out)
{
    unsigned st = *state;
    int i, k = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = in[i];
        switch (st) {
        case S_ESC:
            if (c == '[')
                st = S_CSI;
            else if (c == ']' || c == 'P' || c == 'X' || c == '^' || c == '_')
                st = S_STR;
            else if (c == '(' || c == ')' || c == '*' || c == '+' || c == '#' || c == '%' || c == ' ')
                st = S_ONE;
            else if (c == 27)
                st = S_ESC;                     /* (another begins) */
            else
                st = S_TEXT;                    /* (a sequence of two: ESC 7, ESC =, ESC M) */
            continue;
        case S_CSI:
            if (c >= 0x40 && c <= 0x7E)
                st = S_TEXT;
            else if (c == 27)
                st = S_ESC;                     /* (cut short by another) */
            else if (c == '\n' || c == '\r')
                st = S_TEXT, i--;               /* (never ended: the line's end is kept all the same) */
            continue;
        case S_STR:
            if (c == 7)
                st = S_TEXT;
            else if (c == 27)
                st = S_STR_ESC;
            continue;
        case S_STR_ESC:
            st = c == '\\' ? S_TEXT : c == 27 ? S_STR_ESC : S_STR;
            continue;
        case S_ONE:
            st = S_TEXT;
            continue;
        default:
            break;
        }
        if (c == 27)
            st = S_ESC;
        else if (c == '\n' || c == '\t' || c >= 32)     /* (a line's end is its line feed: the carriage return goes,
                                                           and with it the bell, the backspace and their like) */
            if (c != 127)
                out[k++] = (char)c;
    }
    *state = st;
    return k;
}

static FILE *file;
static unsigned state;

int sesslog_on(void) { return file != NULL; }

int sesslog_open(const char *path, const char *what)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char when[40] = "";
    long size;
    sesslog_close();
    if (!(file = fopen(path, "a")))             /* (text: a line's end is written as the system's own) */
        return -1;
    if (tm)
        strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", tm);
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fprintf(file, "%s=== %s, %s ===\n", size > 0 ? "\n" : "", what, when);
    fflush(file);
    state = 0;
    return 0;
}

void sesslog_write(const void *data, int n)
{
    const unsigned char *d = data;
    char out[512];
    if (!file)
        return;
    while (n > 0) {
        int part = n < (int)sizeof out ? n : (int)sizeof out, k = sesslog_text(&state, d, part, out);
        if (k > 0 && fwrite(out, 1, (size_t)k, file) != (size_t)k) {
            sesslog_close();                    /* (the disk is full, or gone: no more of it) */
            return;
        }
        d += part, n -= part;
    }
    fflush(file);                               /* (what has come is in the file if the PC stops) */
}

void sesslog_close(void)
{
    if (file)
        fclose(file);
    file = NULL;
}
