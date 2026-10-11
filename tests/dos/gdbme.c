/* GDBME (GLOS M5 A7): a program for glos gdb to stop, look into and change
 * (tests/loopa/jobs.py gdb). DJGPP, with its symbols, no optimising.
 *
 * It says "HX-GDBME start" on COM1, makes counter (42) into twice itself and
 * one, says "HX-GDBME counter=N" and ends with N's low seven bits: 85, when
 * nobody has touched counter. */
#include <pc.h>
#include <stdio.h>

int counter = 42;

static void ser(const char *s)
{
    for (; *s; s++) {
        int n = 0;
        while (!(inportb(0x3FD) & 0x20) && ++n < 100000) ;
        outportb(0x3F8, *s);
    }
}

int twice_and_one(int x)
{
    return x * 2 + 1;
}

int main(void)
{
    char line[64];
    ser("HX-GDBME start\r\n");
    counter = twice_and_one(counter);
    sprintf(line, "HX-GDBME counter=%d\r\n", counter);
    ser(line);
    return counter & 0x7F;
}
