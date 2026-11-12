/* SCRTEXT: the text screen as text, for the idle screen's test (tests/loopa/jobs.py ssh, idle_case).
 * stdout: "SCR rr text" for each row of the active page that has anything on it, trailing blanks removed;
 * then "SCR-END rows=R cols=C". Run with its output captured (an agent job), it leaves the screen as it was. */
#include <i86.h>
#include <stdio.h>

int main(void)
{
    unsigned char far *bda = (unsigned char far *)MK_FP(0x40, 0);
    unsigned mode = bda[0x49], cols = bda[0x4A] | bda[0x4B] << 8, rows = bda[0x84] + 1u;
    unsigned start = bda[0x4E] | bda[0x4F] << 8, r, c, last;
    unsigned char far *scr = (unsigned char far *)MK_FP(mode == 7 ? 0xB000 : 0xB800, start);
    static char line[134];
    if ((mode != 2 && mode != 3 && mode != 7) || cols < 40 || cols > 132 || rows < 25 || rows > 60) {
        printf("SCR-END mode=%u\r\n", mode);
        return 1;
    }
    for (r = 0; r < rows; r++) {
        for (c = 0, last = 0; c < cols; c++) {
            unsigned char ch = scr[(r * cols + c) * 2];
            line[c] = (char)(ch >= 32 && ch < 127 ? ch : ch ? '?' : ' ');
            if (line[c] != ' ')
                last = c + 1;
        }
        line[last] = 0;
        if (last)
            printf("SCR %02u %s\r\n", r, line);
    }
    printf("SCR-END rows=%u cols=%u\r\n", rows, cols);
    return 0;
}
