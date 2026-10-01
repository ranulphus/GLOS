/* GLOS.EXE - the loader. For now a stub that proves the build and Loop A:
   it reports over COM1 (115200 8N1, the harness's HX- protocol) and stdout,
   then returns to DOS. 16-bit real mode, Open Watcom small model. */
#include <conio.h>
#include <stdio.h>

#ifndef GLOS_BUILD
#define GLOS_BUILD "unknown"
#endif

static void ser_init(void)
{
    outp(0x3FB, 0x80); outp(0x3F8, 1); outp(0x3F9, 0); outp(0x3FB, 0x03);
    outp(0x3FA, 0xC7); outp(0x3FC, 0x03);
}

static void ser_put(char c)
{
    long spin = 0;
    while (!(inp(0x3FD) & 0x20) && ++spin < 100000L) ;
    outp(0x3F8, c);
}

/* One line to COM1 and stdout. */
static void say(const char *s)
{
    const char *p;
    for (p = s; *p; p++) ser_put(*p);
    ser_put('\r'); ser_put('\n');
    puts(s);
}

int main(void)
{
    ser_init();
    say("HX-START glos " GLOS_BUILD);
    say("GLOS-HELLO loader stub " GLOS_BUILD);
    say("HX-DONE 0");
    return 0;
}
