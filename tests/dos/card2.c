/* CARD2: a PCI display card the BIOS couldn't start, started from DOS (docs/v1/native-dos.md, N4: the first try on
 * the real PC before GLOS does it itself; bench C's 486, its PCI G450 beside the ViRGE, 2026-11-26). DJGPP.
 *
 *   CARD2 [VVVV:DDDD]                (default 102B:0525, the G450 PCI)
 *
 * Steps, each a line "CARD2 name PASS|FAIL ...", stopping at the first that fails before anything is switched:
 *   find      the card, the bridge above it, the BIOS's display (another class 03 function decoding I/O);
 *   size      the card's BARs and ROM (its decode off meanwhile); the other display card's BARs;
 *   place     each BAR that reads 0 given a place (kernel/drv/pcialloc.c), the bridge's windows widened to it;
 *   rom       the ROM placed in the bridge's memory window, read whole into DOS memory (its x86 image: 55AA, PCIR
 *             naming the card, its checksum), then switched off again;
 *   vga       the VGA ranges switched to the card: the other card's I/O and memory decode off, the bridge's VGA
 *             Enable on, the card's own decode and bus mastering on;
 *   post      its image's start entry (offset 3) called in real mode, AX its bus and device, a stack of 8 KB of
 *             ours; INT 10h's vector then into the image;
 *   mode      INT 10h mode 3; a line written to its text screen;
 *   vbe       4F00h: "VESA", its version, its maker's string, its memory.
 * The image stays in a DOS block owned by DOS (INT 10h is the card's from here on); restart the PC to have the
 * BIOS's display back. */
#include <dpmi.h>
#include <stdarg.h>
#include <go32.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/farptr.h>

#include "../../kernel/drv/pcialloc.h"

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

static unsigned long bar_size(int b, int d, int f, int r, int rom)
{
    unsigned long old = rd(b, d, f, r), got;
    wr(b, d, f, r, rom ? 0xFFFFF800UL : 0xFFFFFFFFUL);
    got = rd(b, d, f, r);
    wr(b, d, f, r, old);
    got &= rom ? 0xFFFFF800UL : 0xFFFFFFF0UL;
    return got ? (~got + 1) & 0xFFFFFFFFUL : 0;
}

static void say(const char *name, int ok, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void say(const char *name, int ok, const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    printf("CARD2 %s %s %s\n", name, ok ? "PASS" : "FAIL", b);
    fflush(stdout);
}

/* A DOS block of paras paragraphs that DOS keeps after this program ends (its owner made 0008h, DOS's own). */
static int dos_block(unsigned paras)
{
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x48, r.x.bx = paras;
    __dpmi_int(0x21, &r);
    if (r.x.flags & 1)
        return -1;
    _farpokew(_dos_ds, (unsigned long)(r.x.ax - 1) * 16 + 1, 0x0008);
    return r.x.ax;
}

int main(int argc, char **argv)
{
    unsigned vendor = 0x102B, device = 0x0525;
    int b, d, f, cb = -1, cd = -1, cf = -1, bd = -1, bf = -1, ob = -1, od = -1, of = -1, k;
    unsigned long cmd, bar[6], bsize[6], rsize, obar[6], osize[6], np, pf, ocmd, bctl;
    struct pa_range busy[16], keep_np[8], keep_pf[8], win_np, win_pf;
    int nbusy = 0, nnp = 0, npf = 0, seg, stack;
    unsigned long rom_at, len, sum;
    __dpmi_regs r;

    if (argc > 1 && sscanf(argv[1], "%x:%x", &vendor, &device) != 2)
        return printf("CARD2 usage: CARD2 [VVVV:DDDD]\n"), 2;

    /* find */
    for (b = 0; b < 4; b++)
        for (d = 0; d < 32; d++)
            for (f = 0; f < 8; f++) {
                unsigned long id = rd(b, d, f, 0), cls = rd(b, d, f, 8) >> 16;
                if (id == 0xFFFFFFFFUL || !id) {
                    if (!f)
                        break;
                    continue;
                }
                if ((id & 0xFFFF) == vendor && id >> 16 == device && cb < 0)
                    cb = b, cd = d, cf = f;
                else if (cls == 0x0300 && (rd(b, d, f, 4) & 1) && ob < 0)
                    ob = b, od = d, of = f;
            }
    for (d = 0; cb > 0 && d < 32 && bd < 0; d++)
        for (f = 0; f < 8; f++) {
            unsigned long id = rd(0, d, f, 0);
            if (id == 0xFFFFFFFFUL)
                break;
            if ((rd(0, d, f, 8) >> 16) == 0x0604 && ((rd(0, d, f, 0x18) >> 8) & 0xFF) == (unsigned long)cb) {
                bd = d, bf = f;
                break;
            }
        }
    if (cb < 0 || bd < 0) {
        say("find", 0, "card %04x:%04x %s, bridge %s", vendor, device, cb < 0 ? "not found" : "found",
            bd < 0 ? "none" : "found");
        return 1;
    }
    say("find", 1, "card at %02x:%02x.%u, bridge 00:%02x.%u, the BIOS's display %s", cb, cd, cf, bd, bf,
        ob < 0 ? "none other" : "found");
    if (ob >= 0)
        printf("CARD2 display-now %02x:%02x.%u %08lx\n", ob, od, of, rd(ob, od, of, 0));

    /* size */
    cmd = rd(cb, cd, cf, 4);
    wr(cb, cd, cf, 4, cmd & ~3UL);
    for (k = 0; k < 6; k++) {
        bar[k] = rd(cb, cd, cf, 0x10 + 4 * k);
        bsize[k] = (bar[k] & 1) ? 0 : bar_size(cb, cd, cf, 0x10 + 4 * k, 0);
    }
    rsize = bar_size(cb, cd, cf, 0x30, 1);
    wr(cb, cd, cf, 4, cmd);
    memset(osize, 0, sizeof osize);
    if (ob >= 0) {
        ocmd = rd(ob, od, of, 4);
        wr(ob, od, of, 4, ocmd & ~3UL);
        for (k = 0; k < 6; k++) {
            obar[k] = rd(ob, od, of, 0x10 + 4 * k);
            osize[k] = (obar[k] & 1) ? 0 : bar_size(ob, od, of, 0x10 + 4 * k, 0);
        }
        wr(ob, od, of, 4, ocmd);
    }
    for (k = 0; k < 6; k++)
        if (bsize[k])
            printf("CARD2 bar%d %08lx size %lu KB%s\n", k, bar[k] & 0xFFFFFFF0UL, bsize[k] >> 10,
                   (bar[k] & 0xFFFFFFF0UL) ? "" : " unplaced");
    say("size", rsize != 0, "rom %lu KB", rsize >> 10);
    if (!rsize)
        return 1;

    /* place */
    np = rd(0, bd, bf, 0x20), pf = rd(0, bd, bf, 0x24);
    win_np.base = (np & 0xFFF0) << 16, win_np.last = (np & 0xFFF00000UL) | 0xFFFFF;
    win_pf.base = (pf & 0xFFF0) << 16, win_pf.last = (pf & 0xFFF00000UL) | 0xFFFFF;
    busy[nbusy].base = 0, busy[nbusy++].last = 0x00FFFFFFUL;           /* (RAM: the 486's few MB, and ISA's) */
    busy[nbusy].base = 0xFEC00000UL, busy[nbusy++].last = 0xFFFFFFFFUL;
    for (k = 0; k < 6; k++)
        if (osize[k] && (obar[k] & 0xFFFFFFF0UL)) {
            busy[nbusy].base = obar[k] & 0xFFFFFFF0UL, busy[nbusy].last = busy[nbusy].base + osize[k] - 1;
            nbusy++;
        }
    for (k = 0; k < 6; k++)
        if (bsize[k] && (bar[k] & 0xFFFFFFF0UL)) {
            struct pa_range *kp = (bar[k] & 8) ? &keep_pf[npf++] : &keep_np[nnp++];
            kp->base = bar[k] & 0xFFFFFFF0UL, kp->last = kp->base + bsize[k] - 1;
        }
    for (k = 0; k < 6; k++) {
        unsigned long at;
        struct pa_range b2[17];
        int pre = (bar[k] & 8) != 0, n2;
        if (!bsize[k] || (bar[k] & 0xFFFFFFF0UL))
            continue;
        memcpy(b2, busy, sizeof busy[0] * nbusy);
        n2 = nbusy;
        b2[n2++] = pre ? win_np : win_pf;                               /* (the bridge's other window) */
        if (pa_place(b2, n2, pre ? keep_pf : keep_np, pre ? npf : nnp, pre ? &win_pf : &win_np, bsize[k],
                     0x01000000UL, 0xFEC00000UL, &at) != 0) {
            say("place", 0, "bar%d (%lu KB): no room", k, bsize[k] >> 10);
            return 1;
        }
        bar[k] = at | (bar[k] & 0xF);
        if (pre)
            keep_pf[npf].base = at, keep_pf[npf++].last = at + bsize[k] - 1;
        else
            keep_np[nnp].base = at, keep_np[nnp++].last = at + bsize[k] - 1;
        printf("CARD2 bar%d at %08lx\n", k, at);
    }
    wr(0, bd, bf, 0x20, (win_np.last & 0xFFF00000UL) | (win_np.base >> 16 & 0xFFF0));
    wr(0, bd, bf, 0x24, (win_pf.last & 0xFFF00000UL) | (win_pf.base >> 16 & 0xFFF0) | (pf & 0xF));
    if ((pf & 0xF) == 1)
        wr(0, bd, bf, 0x28, 0), wr(0, bd, bf, 0x2C, 0);
    for (k = 0; k < 6; k++)
        if (bsize[k])
            wr(cb, cd, cf, 0x10 + 4 * k, bar[k]);
    say("place", 1, "windows mem %08lx-%08lx prefetch %08lx-%08lx", win_np.base, win_np.last, win_pf.base,
        win_pf.last);

    /* rom */
    {
        struct pa_range b2[17];
        int n2 = nbusy;
        memcpy(b2, busy, sizeof busy[0] * nbusy);
        b2[n2++] = win_pf;
        if (pa_place(b2, n2, keep_np, nnp, &win_np, rsize, 0x01000000UL, 0xFEC00000UL, &rom_at) != 0
            || rom_at < win_np.base || rom_at + rsize - 1 > win_np.last) {
            say("rom", 0, "no place in the bridge's memory window");
            return 1;
        }
    }
    {
        __dpmi_meminfo mi;
        int sel;
        unsigned long off = 0, pcir;
        mi.address = rom_at, mi.size = rsize;
        wr(cb, cd, cf, 0x30, rom_at | 1);
        wr(cb, cd, cf, 4, cmd | 2);
        if (__dpmi_physical_address_mapping(&mi) != 0 || (sel = __dpmi_allocate_ldt_descriptors(1)) < 0) {
            wr(cb, cd, cf, 0x30, 0);
            say("rom", 0, "no mapping");
            return 1;
        }
        __dpmi_set_segment_base_address(sel, mi.address);
        __dpmi_set_segment_limit(sel, rsize - 1);
        for (;;) {                              /* (the x86 image: code type 0; else the next, while the last isn't) */
            pcir = _farpeekw(sel, off + 0x18);
            if (_farpeekw(sel, off) != 0xAA55 || _farpeekl(sel, off + pcir) != 0x52494350UL)
                break;
            if (_farpeekb(sel, off + pcir + 0x14) == 0 || (_farpeekb(sel, off + pcir + 0x15) & 0x80))
                break;
            off += (unsigned long)_farpeekw(sel, off + pcir + 0x10) * 512;
            if (off >= rsize)
                break;
        }
        len = (unsigned long)_farpeekb(sel, off + 2) * 512;
        if (_farpeekw(sel, off) != 0xAA55 || !len || off + len > rsize || (seg = dos_block((len + 15) / 16 + 0x100)) < 0) {
            wr(cb, cd, cf, 0x30, 0);
            say("rom", 0, "no image (%04x at %lx) or no DOS memory", _farpeekw(sel, off), off);
            return 1;
        }
        for (sum = 0, k = 0; (unsigned long)k < len; k++) {
            unsigned char c = _farpeekb(sel, off + k);
            _farpokeb(_dos_ds, (unsigned long)seg * 16 + k, c);
            sum += c;
        }
        __dpmi_free_ldt_descriptor(sel);
        __dpmi_free_physical_address_mapping(&mi);
        wr(cb, cd, cf, 0x30, 0);                /* (the ROM off: its range given up) */
        if (sum & 0xFF) {
            say("rom", 0, "%lu bytes, checksum %02lx", len, sum & 0xFF);
            return 1;
        }
        say("rom", 1, "%lu bytes at %04x:0000 (from %08lx, image at %lx), checksum 0", len, seg, rom_at, off);
    }
    if ((stack = dos_block(8192 / 16)) < 0) {
        say("post", 0, "no stack");
        return 1;
    }

    /* vga */
    if (ob >= 0)
        wr(ob, od, of, 4, rd(ob, od, of, 4) & ~3UL);
    bctl = rd(0, bd, bf, 0x3C);
    wr(0, bd, bf, 0x3C, bctl | (8UL << 16));    /* (the bridge's VGA Enable: bridge control bit 3) */
    wr(0, bd, bf, 4, rd(0, bd, bf, 4) | 7);
    wr(cb, cd, cf, 4, (cmd & ~0x400UL) | 7);
    say("vga", 1, "other card's decode off, the bridge's VGA Enable on, the card's I/O, memory and mastering on");

    /* post */
    {
        unsigned long v10 = _farpeekl(_dos_ds, 0x40);
        memset(&r, 0, sizeof r);
        r.x.ax = (unsigned short)(cb << 8 | cd << 3 | cf);
        r.x.cs = seg, r.x.ip = 3;
        r.x.ss = stack, r.x.sp = 8192 - 16;
        printf("CARD2 post calling %04x:0003 ax=%04x\n", seg, r.x.ax);
        fflush(stdout);
        __dpmi_simulate_real_mode_procedure_retf(&r);
        k = (_farpeekl(_dos_ds, 0x40) >> 16) == (unsigned long)seg;
        say("post", k, "returned; INT 10h %08lx (was %08lx)", _farpeekl(_dos_ds, 0x40), v10);
        if (!k)
            return 1;
    }

    /* mode */
    {
        static const char msg[] = "GLOS: this PCI G450 started by CARD2, its own BIOS run from DOS memory.";
        memset(&r, 0, sizeof r);
        r.x.ax = 0x0003;
        __dpmi_int(0x10, &r);
        for (k = 0; msg[k]; k++)
            _farpokew(_dos_ds, 0xB8000 + 160 * 2 + 2 * k, 0x1F00 | (unsigned char)msg[k]);
        say("mode", 1, "mode 3 set; the line written on its screen's third row");
    }

    /* vbe */
    {
        unsigned long tb = __tb;
        unsigned long oem;
        char s[80];
        int i;
        for (i = 0; i < 512; i++)
            _farpokeb(_dos_ds, tb + i, 0);
        memset(&r, 0, sizeof r);
        r.x.ax = 0x4F00, r.x.es = tb >> 4, r.x.di = tb & 15;
        __dpmi_int(0x10, &r);
        oem = _farpeekl(_dos_ds, tb + 6);
        for (i = 0; i < 79; i++)
            s[i] = _farpeekb(_dos_ds, (oem >> 16) * 16 + (oem & 0xFFFF) + i);
        s[79] = 0;
        for (i = 0; s[i]; i++)
            if (s[i] < 32 || s[i] > 126) {
                s[i] = 0;
                break;
            }
        k = r.x.ax == 0x004F && _farpeekl(_dos_ds, tb) == 0x41534556UL;    /* "VESA" */
        say("vbe", k, "ax=%04x version %x.%x memory %u KB oem \"%s\"", r.x.ax, _farpeekb(_dos_ds, tb + 5),
            _farpeekb(_dos_ds, tb + 4), _farpeekw(_dos_ds, tb + 18) * 64, s);
    }
    printf("CARD2 done (restart the PC for the BIOS's display again)\n");
    return 0;
}
