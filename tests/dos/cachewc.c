/* CACHEWC: DRAW's CACHE (API 1.4's GLOS_FN_CACHE), for any program. Conventional memory
 * from DOS (DPMI 0100h), a 64 KB unit of it made write-combining, written
 * and read back, and the ranges GLOS must refuse:
 *     HX-CACHE wc=E unit16=E part=E foreign=E type=E kept=K
 * E is 0 for success or the GLOS_E_ code; K 1 when what was written through
 * the write-combining unit reads back. On a CPU without MTRRs every call is
 * refused with GLOS_E_FUNCTION or GLOS_E_ARGUMENT. GLOS gives the unit back
 * when the program ends (its log: GLOS-MTRR restored).
 *
 * Since API 2.11, memory of a video card's own that the program has mapped
 * (DPMI 0800h): a page of it write-combining, and the ranges refused:
 *     HX-CACHE-CARD bar=B page=E odd=E ram=E again=E next=E uc=E left=E read=1
 * B is the card's memory range used (its second, if it has two: the first
 * is the framebuffer, which GLOS may have made write-combining whole); odd
 * is three pages (no power of two), ram a page of the program's own
 * memory, again the same page once more, next the page after it (which
 * replaces it), uc that one uncached again, left a page left
 * write-combining at the end, for GLOS to take back (its log: GLOS-MTRR
 * page off). read: the card's memory read through the page afterwards.
 * "HX-CACHE-CARD none" where PCI shows no video card with memory.
 * DJGPP, with sdk/libglos. */
#include <stdio.h>
#include <string.h>
#include <dpmi.h>
#include <pc.h>
#include <sys/movedata.h>
#include <sys/farptr.h>
#include <go32.h>

#include <glos/glos.h>

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
    return glos_cache(lin, bytes, type);        /* (0, or GLOS_E_) */
}

static unsigned pci_read(unsigned bus, unsigned dev, unsigned fn, unsigned reg)
{
    outportl(0xCF8, 0x80000000u | bus << 16 | dev << 11 | fn << 8 | reg);
    return inportl(0xCFC);
}

/* A memory range of the first video card PCI shows: its second if it has two, else its first; 0: none. */
static unsigned card_memory(void)
{
    unsigned bus, dev, k, found = 0, n = 0;
    for (bus = 0; bus < 4 && !n; bus++)
        for (dev = 0; dev < 32 && !n; dev++) {
            if (pci_read(bus, dev, 0, 0) == 0xFFFFFFFFu || pci_read(bus, dev, 0, 8) >> 24 != 0x03)
                continue;
            for (k = 0; k < 6 && n < 2; k++) {
                unsigned b = pci_read(bus, dev, 0, 0x10 + 4 * k);
                if (b && !(b & 7) && (b & ~15u) >= 0x100000u) {
                    found = b & ~15u;
                    n++;
                }
            }
        }
    return found;
}

static void card(void)
{
    static char own[3 * 4096];
    char line[160];
    __dpmi_meminfo mi;
    unsigned bar = card_memory(), lin, mine;
    unsigned long base = 0;
    int page, odd, ram, again, next, uc, left, sel, got = 0;
    if (!bar) {
        ser("HX-CACHE-CARD none\r\n");
        return;
    }
    mi.address = bar;
    mi.size = 0x4000;
    if (__dpmi_physical_address_mapping(&mi) != 0) {
        ser("HX-CACHE-CARD no mapping\r\n");
        return;
    }
    lin = mi.address;
    __dpmi_get_segment_base_address(_my_ds(), &base);
    mine = ((unsigned)base + (unsigned)own + 0xFFF) & ~0xFFFu;
    own[4096] = 1;                              /* (the page is there) */
    page = try(lin, 0x1000, GLOS_CACHE_WC);
    odd = try(lin, 0x3000, GLOS_CACHE_WC);
    ram = try(mine, 0x1000, GLOS_CACHE_WC);
    again = try(lin, 0x1000, GLOS_CACHE_WC);
    next = try(lin + 0x1000, 0x1000, GLOS_CACHE_WC);
    uc = try(lin + 0x1000, 0x1000, GLOS_CACHE_UC);
    left = try(lin, 0x1000, GLOS_CACHE_WC);
    sel = __dpmi_allocate_ldt_descriptors(1);
    if (sel > 0 && __dpmi_set_segment_base_address(sel, lin) == 0 && __dpmi_set_segment_limit(sel, 0x1FFF) == 0) {
        (void)_farpeekl(sel, 0);                /* (through the write-combining page, and the one made uncached again) */
        (void)_farpeekl(sel, 0x1000);
        got = 1;
    }
    snprintf(line, sizeof line, "HX-CACHE-CARD bar=%08x page=%d odd=%d ram=%d again=%d next=%d uc=%d left=%d read=%d\r\n",
             bar, page, odd, ram, again, next, uc, left, got);
    ser(line);
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
    card();
    return 0;
}
