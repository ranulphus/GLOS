/* CACHEWC (API 1.4): GLOS_FN_CACHE, for any program. Conventional memory
 * from DOS (DPMI 0100h), a 64 KB unit of it made write-combining, written
 * and read back, and the ranges GLOS must refuse:
 *     HX-CACHE wc=E unit16=E part=E foreign=E type=E kept=K
 * E is 0 for success or the GLOS_E_ code; K 1 when what was written through
 * the write-combining unit reads back. On a CPU without MTRRs every call is
 * refused with GLOS_E_FUNCTION or GLOS_E_ARGUMENT. GLOS gives the unit back
 * when the program ends (its log: GLOS-MTRR restored). DJGPP, with
 * sdk/libglos. */
#include <stdio.h>
#include <string.h>
#include <dpmi.h>
#include <pc.h>
#include <sys/movedata.h>
#include <go32.h>

#include <glos/api.h>

static void ser(const char *s)
{
    for (; *s; s++) {
        int n = 0;
        while (!(inportb(0x3FD) & 0x20) && ++n < 100000) ;
        outportb(0x3F8, *s);
    }
}

static int try(unsigned lin, unsigned bytes, unsigned type)
{
    return glos_cache(lin, bytes, type) == 0 ? 0 : glos_error();
}

int main(void)
{
    static unsigned char pat[4096], got[4096];
    char line[100];
    int sel, seg, wc, u16, part, foreign, type, kept = 0;
    unsigned lin, al, i;
    seg = __dpmi_allocate_dos_memory((0x20000 + 15) >> 4, &sel);   /* 128 KB: a whole 64 KB unit in it */
    if (seg < 0) {
        ser("HX-CACHE no DOS memory\r\n");
        return 1;
    }
    lin = (unsigned)seg << 4;
    al = (lin + 0xFFFFu) & ~0xFFFFu;
    wc = try(al, 0x10000, GLOS_CACHE_WC);
    u16 = try(al, 0x4000, GLOS_CACHE_WC);               /* (a quarter of a 64 KB unit) */
    part = try(al + 0x1000, 0x10000, GLOS_CACHE_WC);    /* (not on a unit's start) */
    foreign = try(0, 0x10000, GLOS_CACHE_WC);           /* (the vectors and DOS's data: never the program's) */
    type = try(al, 0x10000, 4);                         /* (write-through: not offered) */
    for (i = 0; i < sizeof pat; i++)
        pat[i] = (unsigned char)(i * 7 + 3);
    dosmemput(pat, sizeof pat, al);
    dosmemget(al, sizeof got, got);
    kept = memcmp(pat, got, sizeof pat) == 0;
    snprintf(line, sizeof line, "HX-CACHE wc=%d unit16=%d part=%d foreign=%d type=%d kept=%d\r\n", wc, u16, part,
             foreign, type, kept);
    ser(line);
    __dpmi_free_dos_memory(sel);
    return 0;
}
