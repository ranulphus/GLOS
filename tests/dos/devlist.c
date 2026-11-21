/* DEVLIST: SYS's STAT GLOS_STAT_DEVICES (API 2.12), the devices GLOS found and what serves each, to COM1, a
 * line each with "HX-DEV " before it, then
 *     HX-DEVS lines=N bytes=B
 * For the suite's runs under GLOS /RUN, where there is no SSH to ask (a network card turned off, say). DJGPP,
 * with sdk/libglos. */
#include <stdio.h>
#include <string.h>
#include <pc.h>

#include <glos/glos.h>

static void ser(const char *s)
{
    for (; *s; s++) {
        int n = 0;
        while (!(inportb(0x3FD) & 0x20) && ++n < 100000) ;
        outportb(0x3F8, *s);
    }
}

int main(void)
{
    static char text[12288], line[200];
    unsigned len = 0, count = 0, lines = 0;
    char *p, *e;
    int r = glos_sys_stat(GLOS_STAT_DEVICES, text, sizeof text - 1, &len, &count);
    if (r != 0) {
        snprintf(line, sizeof line, "HX-DEVS error=%d\r\n", r);
        ser(line);
        return 1;
    }
    text[len < sizeof text ? len : sizeof text - 1] = 0;
    for (p = text; *p; p = *e ? e + 1 : e) {
        e = strchr(p, '\n');
        if (!e)
            e = p + strlen(p);
        snprintf(line, sizeof line, "HX-DEV %.*s\r\n", (int)(e - p) < 180 ? (int)(e - p) : 180, p);
        ser(line);
        lines++;
    }
    snprintf(line, sizeof line, "HX-DEVS lines=%u bytes=%u\r\n", lines, len);
    ser(line);
    return 0;
}
