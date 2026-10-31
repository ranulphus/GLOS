/* umaprobe.c - what is in the upper memory area, read-only (docs/upper-memory.md U0), under plain DOS or under
 * GLOS (the same answer, but for a page GLOS maps itself). One line per 4 KB page from C0000h to EFFFFh,
 *   UMAPROBE page=SEG kind=open|zero|changing|data|rom|bar [rom=SEG] [pci=BB:DD.F]
 * (open: every byte FFh, read twice; changing: the two reads differ), then the option ROMs (55AAh at a 2 KB
 * boundary), the PCI memory and expansion-ROM BARs below 1 MB and the E820 ranges below 1 MB, and
 *   UMAPROBE free=N first=SEG auto=SEG|none
 * free counts the open pages no ROM or BAR covers; auto is upper-memory.md's rule (the first of them from
 * C8000h, E0000h-EFFFFh only by name). Nothing is written anywhere: no BAR is sized (that takes a write), so a
 * BAR covers only the page it starts in. */
#include <dpmi.h>
#include <go32.h>
#include <stdio.h>
#include <string.h>
#include <sys/movedata.h>

#define UMA_LO   0xC0000UL
#define UMA_HI   0xF0000UL
#define NPAGES   ((UMA_HI - UMA_LO) >> 12)

static unsigned char a[4096], b[4096];
static unsigned long rom_at[NPAGES];            /* each page: the ROM covering it (its address), or 0 */
static char bar_on[NPAGES][12];                 /* each page: the PCI function whose BAR starts in it, or "" */

static int all(const unsigned char *p, unsigned char v)
{
    int i;
    for (i = 0; i < 4096; i++)
        if (p[i] != v)
            return 0;
    return 1;
}

static void roms(void)
{
    unsigned long at = UMA_LO, len, end, i;
    unsigned char h[3], sum, x;
    while (at < UMA_HI) {
        dosmemget(at, 3, h);
        if (h[0] != 0x55 || h[1] != 0xAA || !h[2]) {
            at += 2048;
            continue;
        }
        len = h[2] * 512UL;
        for (sum = 0, i = 0; i < len && at + i < UMA_HI; i++) {
            dosmemget(at + i, 1, &x);
            sum += x;
        }
        printf("UMAPROBE rom=%04lX kb=%lu sum=%s\n", at >> 4, len >> 10, sum ? "bad" : "ok");
        end = at + len < UMA_HI ? at + len : UMA_HI;
        for (i = at & ~4095UL; i < end; i += 4096)      /* every page it touches, the first and last too */
            rom_at[(i - UMA_LO) >> 12] = at;
        at += (len + 2047) & ~2047UL;
    }
}

static unsigned long cfg(int bus, int dev, int fn, int reg)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB10A;
    r.h.bh = bus;
    r.h.bl = dev << 3 | fn;
    r.x.di = reg;
    __dpmi_int(0x1A, &r);
    return r.h.ah ? 0xFFFFFFFFUL : r.d.ecx;
}

static void bar(int bus, int dev, int fn, const char *what, unsigned long base)
{
    printf("UMAPROBE pci=%02X:%02X.%d %s=%05lX\n", bus, dev, fn, what, base);
    if (base >= UMA_LO && base < UMA_HI)
        sprintf(bar_on[(base - UMA_LO) >> 12], "%02X:%02X.%d", bus, dev, fn);
}

static void pci(void)
{
    __dpmi_regs r;
    int bus, dev, fn, last, type, reg, nbar;
    unsigned long v;
    memset(&r, 0, sizeof r);
    r.x.ax = 0xB101;
    __dpmi_int(0x1A, &r);
    if (r.h.ah || r.d.edx != 0x20494350UL) {    /* "PCI " */
        printf("UMAPROBE pci=none\n");
        return;
    }
    last = r.h.cl;
    for (bus = 0; bus <= last; bus++)
        for (dev = 0; dev < 32; dev++)
            for (fn = 0; fn < 8; fn++) {
                if ((cfg(bus, dev, fn, 0) & 0xFFFF) == 0xFFFF) {
                    if (!fn)
                        break;
                    continue;
                }
                type = cfg(bus, dev, fn, 0x0C) >> 16 & 0xFF;
                nbar = (type & 0x7F) == 0 ? 6 : (type & 0x7F) == 1 ? 2 : 0;
                for (reg = 0x10; reg < 0x10 + nbar * 4; reg += 4) {
                    v = cfg(bus, dev, fn, reg);
                    if (v & 1)                  /* I/O */
                        continue;
                    if ((v & ~0xFUL) && (v & ~0xFUL) < 0x100000UL)
                        bar(bus, dev, fn, "bar", v & ~0xFUL);
                    if ((v & 6) == 4)           /* 64-bit: the next is its upper half */
                        reg += 4;
                }
                if (nbar) {
                    v = cfg(bus, dev, fn, (type & 0x7F) == 0 ? 0x30 : 0x38);
                    if ((v & 1) && (v & ~0x7FFUL) < 0x100000UL)
                        bar(bus, dev, fn, "rombar", v & ~0x7FFUL);
                }
                if (!fn && !(type & 0x80))      /* one function only */
                    break;
            }
}

static void e820(void)
{
    __dpmi_regs r;
    unsigned long e[5];
    int n = 0;
    memset(&r, 0, sizeof r);
    do {
        r.d.eax = 0xE820;
        r.d.edx = 0x534D4150UL;                 /* "SMAP" */
        r.d.ecx = 20;
        r.x.es = __tb >> 4;
        r.x.di = __tb & 15;
        __dpmi_int(0x15, &r);
        if ((r.x.flags & 1) || r.d.eax != 0x534D4150UL)
            break;
        dosmemget(__tb, 20, e);
        if (!e[1] && e[0] < 0x100000UL)
            printf("UMAPROBE e820 base=%05lX kb=%lu type=%lu\n", e[0], e[2] >> 10, e[4]);
    } while (r.d.ebx && ++n < 64);
    if (!n && (r.x.flags & 1))
        printf("UMAPROBE e820=none\n");
}

static const char *seg(unsigned long at)
{
    static char s[2][8];
    static int k;
    if (!at)
        return "none";
    k ^= 1;
    sprintf(s[k], "%04lX", at >> 4);
    return s[k];
}

int main(void)
{
    unsigned long i, at, first = 0, pick = 0;
    int nfree = 0;
    const char *kind;
    roms();
    pci();
    e820();
    for (i = 0; i < NPAGES; i++) {
        at = UMA_LO + (i << 12);
        dosmemget(at, 4096, a);
        dosmemget(at, 4096, b);
        kind = memcmp(a, b, 4096) ? "changing" : all(a, 0xFF) ? "open" : all(a, 0) ? "zero" : "data";
        if (rom_at[i])
            kind = "rom";
        else if (bar_on[i][0])
            kind = "bar";
        printf("UMAPROBE page=%04lX kind=%s", at >> 4, kind);
        if (rom_at[i])
            printf(" rom=%04lX", rom_at[i] >> 4);
        if (bar_on[i][0])
            printf(" pci=%s", bar_on[i]);
        printf("\n");
        if (!strcmp(kind, "open")) {
            nfree++;
            if (!first)
                first = at;
            if (!pick && at >= 0xC8000UL && at < 0xE0000UL)
                pick = at;
        }
    }
    printf("UMAPROBE free=%d first=%s auto=%s\n", nfree, seg(first), seg(pick));
    return 0;
}
