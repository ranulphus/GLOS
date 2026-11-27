/* PCIPLACE: can GLOS place a PCI display card's ranges where the BIOS couldn't? (docs/v1/native-dos.md, "a
 * display card the BIOS couldn't start"; bench C's 486, 2026-11-26: a PCI G450 behind its HiNT bridge, its 32 MB
 * frame buffer left at 0). DJGPP; PCI configuration by CF8h/CFCh, the ROM read through DPMI 0800h.
 *
 *   PCIPLACE [VVVV:DDDD] [ADDR]      the card (default 102B:0525, the G450 PCI) and the free address to try
 *                                    (default F4000000)
 *
 * Read only, but for its own steps, each put back before the next: the card's BARs and its ROM BAR sized (the
 * card's memory and I/O decode off meanwhile); its ROM placed in the bridge's own window and its signature (55AA,
 * then "PCIR" with the card's vendor and device) read; then the bridge's prefetchable window widened to the free
 * address and the ROM placed there, read again: whether the chipset passes that address to the PCI bus and the
 * bridge on to the card. The card's memory is never read (nothing has set it up before its BIOS runs). Every
 * register it changed is written back as found; the last line says so. Each step a line "PCIPLACE name ...". */
#include <dpmi.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>

static unsigned long cfg_addr(int b, int d, int f, int r)
{
    return 0x80000000UL | (unsigned long)b << 16 | (unsigned long)d << 11 | (unsigned long)f << 8 | (r & 0xFC);
}

static unsigned long rd(int b, int d, int f, int r)
{
    outportl(0xCF8, cfg_addr(b, d, f, r));
    return inportl(0xCFC);
}

static void wr(int b, int d, int f, int r, unsigned long v)
{
    outportl(0xCF8, cfg_addr(b, d, f, r));
    outportl(0xCFC, v);
}

/* A memory BAR's size (decode off around it): the bytes, or 0 for none. */
static unsigned long bar_size(int b, int d, int f, int r, int rom)
{
    unsigned long old = rd(b, d, f, r), got;
    wr(b, d, f, r, rom ? 0xFFFFF800UL : 0xFFFFFFFFUL);
    got = rd(b, d, f, r);
    wr(b, d, f, r, old);
    got &= rom ? 0xFFFFF800UL : 0xFFFFFFF0UL;
    return got ? (~got + 1) & 0xFFFFFFFFUL : 0;
}

/* The ROM at phys: 0 if it shows 55AA and a "PCIR" block naming vendor and device; else what it found. */
static int rom_read(unsigned long phys, unsigned vendor, unsigned device, char *what, int room)
{
    __dpmi_meminfo mi;
    int sel, ok = 0;
    unsigned sig, pcir;
    mi.address = phys, mi.size = 65536;
    if (__dpmi_physical_address_mapping(&mi) != 0) {
        snprintf(what, room, "no mapping (DPMI 0800h)");
        return -1;
    }
    if ((sel = __dpmi_allocate_ldt_descriptors(1)) < 0) {
        __dpmi_free_physical_address_mapping(&mi);
        snprintf(what, room, "no selector");
        return -1;
    }
    __dpmi_set_segment_base_address(sel, mi.address);
    __dpmi_set_segment_limit(sel, 65535);
    sig = _farpeekw(sel, 0);
    pcir = _farpeekw(sel, 0x18);
    if (sig == 0xAA55 && pcir < 0xFFF0 && _farpeekl(sel, pcir) == 0x52494350UL) {         /* "PCIR" */
        unsigned v = _farpeekw(sel, pcir + 4), dv = _farpeekw(sel, pcir + 6);
        ok = v == vendor && dv == device;
        snprintf(what, room, "55AA, PCIR %04x:%04x, %u KB of image", v, dv, _farpeekb(sel, 2) * 512 / 1024);
    } else {
        snprintf(what, room, "found %04x at 0 (not a ROM: %s)", sig, sig == 0xFFFF ? "nothing answered" : "something else");
    }
    __dpmi_free_ldt_descriptor(sel);
    __dpmi_free_physical_address_mapping(&mi);
    return ok ? 0 : -1;
}

int main(int argc, char **argv)
{
    unsigned vendor = 0x102B, device = 0x0525;
    unsigned long free_at = 0xF4000000UL;
    int b, d, f, cb = -1, cd = -1, cf = -1, bb = -1, bd = -1, bf = -1, fails = 0, k;
    unsigned long cmd, rom, rsize, bar[6], bsize[6], win_np, win_p, win_pu = 0, win_pl = 0, bcmd;
    char what[128];

    if (argc > 1 && sscanf(argv[1], "%x:%x", &vendor, &device) != 2)
        return printf("PCIPLACE usage: PCIPLACE [VVVV:DDDD] [ADDR]\n"), 2;
    if (argc > 2)
        free_at = strtoul(argv[2], 0, 16);
    for (b = 0; b < 4 && cb < 0; b++)
        for (d = 0; d < 32 && cb < 0; d++)
            for (f = 0; f < 8; f++) {
                unsigned long id = rd(b, d, f, 0);
                if (id == 0xFFFFFFFFUL || !id) {
                    if (!f)
                        break;
                    continue;
                }
                if ((id & 0xFFFF) == vendor && id >> 16 == device) {
                    cb = b, cd = d, cf = f;
                    break;
                }
            }
    if (cb < 0)
        return printf("PCIPLACE card FAIL %04x:%04x not found\n", vendor, device), 1;
    printf("PCIPLACE card PASS %04x:%04x at %02x:%02x.%u\n", vendor, device, cb, cd, cf);
    for (d = 0; cb > 0 && d < 32 && bb < 0; d++)       /* (its bridge: the one whose secondary bus it's on) */
        for (f = 0; f < 8; f++) {
            unsigned long id = rd(0, d, f, 0), cls = rd(0, d, f, 8) >> 16, bus = rd(0, d, f, 0x18);
            if (id == 0xFFFFFFFFUL)
                break;
            if (cls == 0x0604 && ((bus >> 8) & 0xFF) == (unsigned long)cb) {
                bb = 0, bd = d, bf = f;
                break;
            }
        }
    if (bb < 0)
        return printf("PCIPLACE bridge FAIL none above bus %u\n", cb), 1;
    win_np = rd(bb, bd, bf, 0x20), win_p = rd(bb, bd, bf, 0x24), bcmd = rd(bb, bd, bf, 4);
    if ((win_p & 0xF) == 1)
        win_pu = rd(bb, bd, bf, 0x28), win_pl = rd(bb, bd, bf, 0x2C);
    printf("PCIPLACE bridge PASS %04lx:%04lx at %02x:%02x.%u buses %02lx-%02lx mem %08lx-%08lx prefetch %08lx-%08lx%s\n",
           rd(bb, bd, bf, 0) & 0xFFFF, rd(bb, bd, bf, 0) >> 16, bb, bd, bf, rd(bb, bd, bf, 0x18) >> 8 & 0xFF,
           rd(bb, bd, bf, 0x18) >> 16 & 0xFF, (win_np & 0xFFF0) << 16, (win_np & 0xFFF00000UL) | 0xFFFFF,
           (win_p & 0xFFF0) << 16, (win_p & 0xFFF00000UL) | 0xFFFFF, (win_p & 0xF) == 1 ? " (64-bit)" : "");

    /* the card's BARs and ROM sized, its decode off meanwhile */
    cmd = rd(cb, cd, cf, 4);
    wr(cb, cd, cf, 4, cmd & ~3UL);
    for (k = 0; k < 6; k++) {
        bar[k] = rd(cb, cd, cf, 0x10 + 4 * k);
        bsize[k] = (bar[k] & 1) ? 0 : bar_size(cb, cd, cf, 0x10 + 4 * k, 0);
    }
    rom = rd(cb, cd, cf, 0x30);
    rsize = bar_size(cb, cd, cf, 0x30, 1);
    wr(cb, cd, cf, 4, cmd);
    for (k = 0; k < 6; k++)
        if (bsize[k])
            printf("PCIPLACE bar%d %08lx size %lu KB%s%s\n", k, bar[k] & 0xFFFFFFF0UL, bsize[k] >> 10,
                   bar[k] & 8 ? " prefetchable" : "", (bar[k] & 0xFFFFFFF0UL) ? "" : " UNASSIGNED");
    printf("PCIPLACE rom %08lx size %lu KB command %04lx\n", rom, rsize >> 10, cmd & 0xFFFF);
    if (!rsize)
        return printf("PCIPLACE done failures=1 (no ROM)\n"), 1;

    /* 1: the ROM in the bridge's own memory window, at its top end (aligned to its size) */
    {
        unsigned long top = (win_np & 0xFFF00000UL) | 0xFFFFF, base = (win_np & 0xFFF0) << 16;
        unsigned long at = (top + 1 - rsize) & ~(rsize - 1);
        int ok, j, clash = 1;
        while (clash && at >= base && at > rsize) {     /* (below the top, clear of the card's own ranges) */
            clash = 0;
            for (j = 0; j < 6; j++) {
                unsigned long s = bar[j] & 0xFFFFFFF0UL;
                if (bsize[j] && s && at < s + bsize[j] && s < at + rsize)
                    clash = 1;
            }
            if (clash)
                at -= rsize;
        }
        if (!(win_np & 0xFFF0) || clash || at < base) {
            printf("PCIPLACE rom-window SKIP no room in the bridge's memory window\n");
        } else {
            wr(cb, cd, cf, 0x30, at | 1);
            wr(cb, cd, cf, 4, cmd | 2);
            ok = rom_read(at, vendor, device, what, sizeof what) == 0;
            wr(cb, cd, cf, 0x30, rom);
            wr(cb, cd, cf, 4, cmd);
            printf("PCIPLACE rom-window %s at %08lx: %s\n", ok ? "PASS" : "FAIL", at, what);
            fails += !ok;
        }
    }
    /* 2: the bridge's prefetchable window widened to free_at (32 MB), the ROM placed there */
    {
        unsigned long nw = ((free_at + 0x2000000UL - 1) & 0xFFF00000UL) | (free_at >> 16 & 0xFFF0) | (win_p & 0xF);
        int ok;
        wr(bb, bd, bf, 0x24, nw);
        if ((win_p & 0xF) == 1)
            wr(bb, bd, bf, 0x28, 0), wr(bb, bd, bf, 0x2C, 0);
        wr(bb, bd, bf, 4, bcmd | 2);
        wr(cb, cd, cf, 0x30, free_at | 1);
        wr(cb, cd, cf, 4, cmd | 2);
        ok = rom_read(free_at, vendor, device, what, sizeof what) == 0;
        wr(cb, cd, cf, 0x30, rom);
        wr(cb, cd, cf, 4, cmd);
        wr(bb, bd, bf, 0x24, win_p);
        if ((win_p & 0xF) == 1)
            wr(bb, bd, bf, 0x28, win_pu), wr(bb, bd, bf, 0x2C, win_pl);
        wr(bb, bd, bf, 4, bcmd);
        printf("PCIPLACE rom-free %s at %08lx (the bridge's prefetchable window widened): %s\n", ok ? "PASS" : "FAIL",
               free_at, what);
        fails += !ok;
    }
    k = rd(cb, cd, cf, 0x30) == rom && rd(cb, cd, cf, 4) == cmd && rd(bb, bd, bf, 0x24) == win_p
        && rd(bb, bd, bf, 4) == bcmd;
    printf("PCIPLACE restored %s\n", k ? "PASS" : "FAIL");
    printf("PCIPLACE done failures=%d\n", fails + !k);
    return fails + !k;
}
