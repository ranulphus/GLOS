/* CONIN (GLOS M9 step C): a console job's program, for RUNTEST and the
 * console suite (tests/loopa/jobs.py console). Real mode, Open Watcom.
 *
 *   CONIN line          a line through DOS (INT 21h 0Ah): "LINE <text>"
 *   CONIN bios N        N keys through INT 16h 00h: "BIOS <ascii codes>"
 *   CONIN char N        N characters through INT 21h 08h: "CHAR <text>"
 *   CONIN tty TEXT      TEXT through the BIOS's teletype (INT 10h 0Eh): "TTY TEXT"
 *   CONIN cls           INT 10h 06h over the whole screen, then "CLS done"
 *   CONIN b800          "GLOS" written into the text screen at B800:0000, then "B800 done"
 *   CONIN mode          INT 10h 0003h (a mode set), then "MODE done"
 *   CONIN port          the CRTC's cursor registers read (3D4h, 3D5h), then "PORT done"
 *   CONIN wait N        N seconds, a dot a second (DOS output): "WAIT done"
 *   CONIN code N        ends with exit code N
 *
 * Lines end CR LF; the exit code is 0 unless said. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void say(const char *s)
{
    union REGS r;
    for (; *s; s++) {
        r.h.ah = 0x02;
        r.h.dl = (unsigned char)*s;
        intdos(&r, &r);
    }
}

int main(int argc, char **argv)
{
    union REGS r;
    char out[200];
    int i, n = argc > 2 ? atoi(argv[2]) : 1;
    if (argc < 2)
        return 2;
    if (!strcmp(argv[1], "line")) {
        unsigned char buf[130];
        buf[0] = 120;
        buf[1] = 0;
        r.h.ah = 0x0A;
        r.x.dx = (unsigned)buf;
        intdos(&r, &r);
        buf[2 + buf[1]] = 0;
        sprintf(out, "\r\nLINE %s\r\n", buf + 2);
        say(out);
    } else if (!strcmp(argv[1], "bios")) {
        int o = sprintf(out, "BIOS");
        for (i = 0; i < n && o < 180; i++) {
            r.h.ah = 0x00;
            int86(0x16, &r, &r);
            o += sprintf(out + o, " %02x", r.h.al);
        }
        strcpy(out + o, "\r\n");
        say(out);
    } else if (!strcmp(argv[1], "char")) {
        int o = sprintf(out, "CHAR ");
        for (i = 0; i < n && o < 180; i++) {
            r.h.ah = 0x08;
            intdos(&r, &r);
            out[o++] = (char)r.h.al;
        }
        strcpy(out + o, "\r\n");
        say(out);
    } else if (!strcmp(argv[1], "tty")) {
        const char *t = argc > 2 ? argv[2] : "";
        for (r.h.ah = 0x0E; *t; t++) {
            r.h.ah = 0x0E;
            r.h.al = (unsigned char)*t;
            r.x.bx = 0x0007;
            int86(0x10, &r, &r);
        }
        say("\r\n");
    } else if (!strcmp(argv[1], "cls")) {
        r.x.ax = 0x0600;
        r.h.bh = 0x07;
        r.x.cx = 0;
        r.x.dx = 0x184F;
        int86(0x10, &r, &r);
        say("CLS done\r\n");
    } else if (!strcmp(argv[1], "b800")) {
        unsigned char far *v = (unsigned char far *)MK_FP(0xB800, 0);
        v[0] = 'G', v[2] = 'L', v[4] = 'O', v[6] = 'S';
        say("B800 done\r\n");
    } else if (!strcmp(argv[1], "mode")) {
        r.x.ax = 0x0003;
        int86(0x10, &r, &r);
        say("MODE done\r\n");
    } else if (!strcmp(argv[1], "port")) {
        unsigned char hi;
        outp(0x3D4, 0x0E);
        hi = (unsigned char)inp(0x3D5);
        sprintf(out, "PORT done %u\r\n", hi < 26 ? 0 : 1);
        say(out);
        return 0;
    } else if (!strcmp(argv[1], "wait")) {
        volatile unsigned long far *tick = (volatile unsigned long far *)MK_FP(0x40, 0x6C);
        for (i = 0; i < n; i++) {
            unsigned long t0 = *tick, now;
            do {
                now = *tick;
            } while (now - t0 < 18);
            say(".");
        }
        say("\r\nWAIT done\r\n");
    } else if (!strcmp(argv[1], "code")) {
        return n;
    }
    return 0;
}
