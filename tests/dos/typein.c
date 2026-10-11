/* TYPEIN (GLOS M5 A4): a line typed at the keyboard, read through DOS, said
 * on COM1: "HX-TYPE ready" when it waits for one, then "HX-TYPE got=..."
 * with the line. For glos type (tests/loopa/jobs.py input). */
#include <bios.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>

static void ser(const char *s)
{
    for (; *s; s++) {
        long n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 100000L) ;
        outp(0x3F8, *s);
    }
}

int main(void)
{
    char line[160];
    while (_bios_keybrd(_KEYBRD_READY))         /* (keys left from the boot) */
        _bios_keybrd(_KEYBRD_READ);
    ser("HX-TYPE ready\r\n");
    if (!fgets(line, sizeof line, stdin))
        line[0] = 0;
    line[strcspn(line, "\r\n")] = 0;
    ser("HX-TYPE got=");
    ser(line);
    ser("\r\n");
    return 0;
}
