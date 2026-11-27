/* G450OUT: why a Matrox G450 started by CARD2 sends no picture (bench C, 2026-11-26: the monitor finds no signal
 * on either connector). DJGPP; read only, but for index registers (put back as found).
 *
 * Whether its CRTC runs (vertical retrace edges at 3DAh and in STATUS 1E14h over about two seconds of reads), the
 * VGA side's screen-off and sync bits (SEQ1, CRTC 17h, the misc output, CRTCEXT1's sync-offs), and the DAC's own
 * (through PALWTADD 3C00h and X_DATAREG 3C0Ah in its registers' range): the pixel clock's control and its PLL's
 * lock, misc control (the DAC's power), and the G450's output and power registers. One line each. */
#include <dpmi.h>
#include <pc.h>
#include <stdio.h>
#include <string.h>
#include <go32.h>
#include <sys/farptr.h>

static unsigned long cfg(int b, int d, int f, int r)
{
    outportl(0xCF8, 0x80000000UL | (unsigned long)b << 16 | (unsigned long)d << 11 | (unsigned long)f << 8 | (r & 0xFC));
    return inportl(0xCFC);
}

static int sel;

static unsigned m8(unsigned off) { return _farpeekb(sel, off); }
static unsigned long m32(unsigned off) { return _farpeekl(sel, off); }
static void w8(unsigned off, unsigned v) { _farpokeb(sel, off, (unsigned char)v); }

static unsigned dac(unsigned idx)
{
    w8(0x3C00, idx);
    return m8(0x3C0A);
}

int main(int argc, char **argv)
{
    int b, d, f, found = 0;
    unsigned long mmio = 0, i, edges = 0, sedges = 0;
    unsigned last, now, slast, snow, pal, k;
    __dpmi_meminfo mi;
    static const struct { unsigned idx; const char *name; } regs[] = {
        { 0x1A, "XPIXCLKCTRL" }, { 0x1E, "XMISCCTRL" }, { 0x1D, "XGENCTRL" }, { 0x2C, "XPIXPLLCM" }, { 0x2D, "XPIXPLLCN" },
        { 0x2E, "XPIXPLLCP" }, { 0x4F, "XPIXPLLSTAT" }, { 0x18, "XVREFCTRL" }, { 0x19, "XMULCTRL" },
        { 0x8A, "XOUTPUTCONN(G450)" }, { 0x8B, "XSYNCCTRL(G450)" }, { 0xA0, "XPWRCTRL(G450)" },
        { 0xA2, "XPANMODE(G450)" }, { 0x89, "XDVICLKCTRL(G450)" }, { 0x4C, "XPIXPLL2/C2?" },
    };

    for (b = 0; b < 4 && !found; b++)
        for (d = 0; d < 32 && !found; d++)
            for (f = 0; f < 8; f++) {
                unsigned long id = cfg(b, d, f, 0);
                if (id == 0x0525102BUL) {
                    mmio = cfg(b, d, f, 0x14) & 0xFFFFC000UL;
                    printf("G450OUT card %02x:%02x.%u command %04lx option %08lx bar0 %08lx bar1 %08lx\n", b, d, f,
                           cfg(b, d, f, 4) & 0xFFFF, cfg(b, d, f, 0x40), cfg(b, d, f, 0x10), cfg(b, d, f, 0x14));
                    found = 1;
                    break;
                }
                if (id == 0xFFFFFFFFUL && !f)
                    break;
            }
    if (!found || !mmio)
        return printf("G450OUT no G450 with its registers placed (run CARD2 first)\n"), 1;
    mi.address = mmio, mi.size = 16384;
    if (__dpmi_physical_address_mapping(&mi) != 0 || (sel = __dpmi_allocate_ldt_descriptors(1)) < 0)
        return printf("G450OUT no mapping\n"), 1;
    __dpmi_set_segment_base_address(sel, mi.address);
    __dpmi_set_segment_limit(sel, 16383);

    if (argc > 1 && !strcmp(argv[1], "mode3")) {  /* (G450OUT mode3: INT 10h's mode 3 first, the card's BIOS's) */
        __dpmi_regs r;
        memset(&r, 0, sizeof r);
        r.x.ax = 0x0003;
        __dpmi_int(0x10, &r);
        printf("G450OUT mode3 asked of INT 10h %08lx\n", _farpeekl(_dos_ds, 0x40));
    }
    if (argc > 1 && !strcmp(argv[1], "vga3")) {  /* (G450OUT vga3: text mode 3 by its registers, not the BIOS) */
        static const unsigned char seq[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
        static const unsigned char crtc[25] = { 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E,
                                                0x00, 0x00, 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF };
        static const unsigned char gc[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF };
        static const unsigned char ac[21] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B,
                                              0x3C, 0x3D, 0x3E, 0x3F, 0x0C, 0x00, 0x0F, 0x08, 0x00 };
        unsigned j;
        printf("G450OUT vga3 pll A %02x %02x %02x B %02x %02x %02x C %02x %02x %02x\n", dac(0x44), dac(0x45), dac(0x46),
               dac(0x48), dac(0x49), dac(0x4A), dac(0x4C), dac(0x4D), dac(0x4E));
        for (j = 0; j < 6; j++)
            w8(0x1FDE, j), w8(0x1FDF, 0);       /* (Matrox's extensions: VGA mode) */
        outportb(0x3C2, 0x67);
        outportb(0x3C4, 0), outportb(0x3C5, 1);
        for (j = 1; j < 5; j++)
            outportb(0x3C4, j), outportb(0x3C5, seq[j]);
        outportb(0x3C4, 0), outportb(0x3C5, 3);
        outportb(0x3D4, 0x11), outportb(0x3D5, inportb(0x3D5) & 0x7F);
        for (j = 0; j < 25; j++)
            outportb(0x3D4, j), outportb(0x3D5, crtc[j]);
        for (j = 0; j < 9; j++)
            outportb(0x3CE, j), outportb(0x3CF, gc[j]);
        (void)inportb(0x3DA);
        for (j = 0; j < 21; j++)
            outportb(0x3C0, j), outportb(0x3C0, ac[j]);
        outportb(0x3C0, 0x20);
        printf("G450OUT vga3 written (misc 67, seq, crtc with 17h=a3, gc, ac, crtcext 0)\n");
    }
    if (argc > 1 && !strcmp(argv[1], "bars")) {  /* (G450OUT bars: VBE mode 101h by its BIOS, then bars drawn into its LFB) */
        __dpmi_regs r;
        unsigned long tb = __tb, phys;
        unsigned pitch, x, y;
        __dpmi_meminfo fm;
        int fs;
        memset(&r, 0, sizeof r);
        r.x.ax = 0x4F01, r.x.cx = 0x101, r.x.es = tb >> 4, r.x.di = tb & 15;
        __dpmi_int(0x10, &r);
        phys = _farpeekl(_dos_ds, tb + 0x28), pitch = _farpeekw(_dos_ds, tb + 0x10);
        printf("G450OUT bars 4F01h ax=%04x lfb %08lx pitch %u\n", r.x.ax, phys, pitch);
        fflush(stdout);
        memset(&r, 0, sizeof r);
        r.x.ax = 0x4F02, r.x.bx = 0x4101;
        printf("G450OUT bars calling 4F02h bx=4101\n");
        fflush(stdout);
        __dpmi_int(0x10, &r);
        printf("G450OUT bars 4F02h returned ax=%04x\n", r.x.ax);
        fflush(stdout);
        if (r.x.ax == 0x004F && phys && pitch) {
            fm.address = phys, fm.size = pitch * 480UL;
            if (__dpmi_physical_address_mapping(&fm) == 0 && (fs = __dpmi_allocate_ldt_descriptors(1)) >= 0) {
                __dpmi_set_segment_base_address(fs, fm.address);
                __dpmi_set_segment_limit(fs, pitch * 480UL - 1);
                for (y = 0; y < 480; y++)
                    for (x = 0; x < 640; x++)
                        _farpokeb(fs, y * pitch + x, (unsigned char)(1 + x / 40 + (y / 240) * 16));
                printf("G450OUT bars drawn: 16 coloured columns, the lower half the next 16 colours\n");
                __dpmi_free_ldt_descriptor(fs);
                __dpmi_free_physical_address_mapping(&fm);
            }
        }
    }
    if (argc > 1 && !strcmp(argv[1], "dump")) {  /* (G450OUT dump: the card's BIOS's copy into C:\\OUT\\G450IMG.BIN) */
        unsigned seg = _farpeekw(_dos_ds, 0x42);
        unsigned len = _farpeekb(_dos_ds, (unsigned long)seg * 16 + 2) * 512u, j;
        FILE *fp = fopen("C:\\OUT\\G450IMG.BIN", "wb");
        for (j = 0; fp && j < len; j++)
            fputc(_farpeekb(_dos_ds, (unsigned long)seg * 16 + j), fp);
        if (fp)
            fclose(fp);
        printf("G450OUT dump %u bytes from %04x:0000 %s\n", len, seg, fp ? "to C:\\OUT\\G450IMG.BIN" : "FAILED");
    }
    printf("G450OUT vectors 10h %08lx 42h %08lx 6Dh %08lx 1Fh %08lx 43h %08lx; bda 49h mode %02x 63h crtc %04x 87h %02x 88h %02x 89h %02x 8Ah %02x; c000 %04x\n",
           _farpeekl(_dos_ds, 0x40), _farpeekl(_dos_ds, 0x42 * 4), _farpeekl(_dos_ds, 0x6D * 4), _farpeekl(_dos_ds, 0x1F * 4),
           _farpeekl(_dos_ds, 0x43 * 4), _farpeekb(_dos_ds, 0x449), _farpeekw(_dos_ds, 0x463), _farpeekb(_dos_ds, 0x487),
           _farpeekb(_dos_ds, 0x488), _farpeekb(_dos_ds, 0x489), _farpeekb(_dos_ds, 0x48A), _farpeekw(_dos_ds, 0xC0000));
    {                                           /* (does the card's BIOS keep the INT 10h it found? then it chains) */
        unsigned seg = _farpeekw(_dos_ds, 0x42), hits = 0;
        unsigned long base = (unsigned long)seg * 16, o;
        for (o = 0; o + 4 <= 0x10000UL; o++)
            if (_farpeekl(_dos_ds, base + o) == 0xC0001EF8UL) {
                if (hits++ < 4)
                    printf("G450OUT old INT 10h C000:1EF8 kept at %04x:%04lx\n", seg, o);
            }
        printf("G450OUT old INT 10h found %u times in the card's BIOS's 64 KB\n", hits);
    }
    {                                           /* (do VGA port writes reach the card? CRTC 0Ch by port, read by its mirror) */
        unsigned old_i, old_m, seen, seq_seen, old_s;
        old_i = inportb(0x3D4);
        w8(0x1FD4, 0x0C), old_m = m8(0x1FD5);
        outportb(0x3D4, 0x0C), outportb(0x3D5, 0x5A);
        w8(0x1FD4, 0x0C), seen = m8(0x1FD5);
        outportb(0x3D4, 0x0C), outportb(0x3D5, old_m);
        outportb(0x3D4, old_i);
        w8(0x1FC4, 2), old_s = m8(0x1FC5);
        outportb(0x3C4, 2), outportb(0x3C5, 0x0A);
        w8(0x1FC4, 2), seq_seen = m8(0x1FC5);
        outportb(0x3C4, 2), outportb(0x3C5, old_s);
        printf("G450OUT port-write crtc0c wrote 5a by 3D5h, the card has %02x (was %02x); seq2 wrote 0a by 3C5h, the card has %02x (was %02x)\n",
               seen, old_m, seq_seen, old_s);
    }
    last = inportb(0x3DA) & 8, slast = m32(0x1E14) & 8;
    for (i = 0; i < 2000000UL; i++) {
        now = inportb(0x3DA) & 8;
        if (now != last)
            edges++, last = now;
        if (!(i & 15)) {
            snow = m32(0x1E14) & 8;
            if (snow != slast)
                sedges++, slast = snow;
        }
    }
    printf("G450OUT retrace 3DAh edges %lu, STATUS vsyncsts edges %lu (over 2,000,000 reads)\n", edges, sedges);
    printf("G450OUT status %08lx misc-in %02x\n", m32(0x1E14), inportb(0x3CC));
    {
        unsigned si = inportb(0x3C4), ci = inportb(0x3D4), ei = m8(0x1FDE);
        outportb(0x3C4, 1);
        printf("G450OUT seq1 %02x (bit 5 screen off)", inportb(0x3C5));
        outportb(0x3D4, 0x17);
        printf(" crtc17 %02x (bit 7 syncs on)", inportb(0x3D5));
        for (k = 0; k < 6; k++) {
            w8(0x1FDE, k);
            printf(" crtcext%u %02x", k, m8(0x1FDF));
        }
        printf(" (ext1 bits 4-5: h/v sync off)\n");
        outportb(0x3C4, si), outportb(0x3D4, ci), w8(0x1FDE, ei);
    }
    {                                           /* (the VGA registers as the card itself has them: its range's mirror) */
        unsigned si = m8(0x1FC4), ci = m8(0x1FD4), v0 = m32(0x1E20) & 0xFFF, v1, n = 0;
        unsigned long j;
        for (j = 0; j < 200000UL; j++)
            if ((m32(0x1E20) & 0xFFF) != v0)
                n++;
        v1 = m32(0x1E20) & 0xFFF;
        printf("G450OUT vcount %03x then %03x (%u reads of 200,000 differed)\n", v0, v1, n);
        printf("G450OUT mmio misc %02x seq:", m8(0x1FCC));
        for (k = 0; k < 5; k++) {
            w8(0x1FC4, k);
            printf(" %02x", m8(0x1FC5));
        }
        printf(" crtc:");
        for (k = 0; k < 0x19; k++) {
            w8(0x1FD4, k);
            printf(" %02x", m8(0x1FD5));
        }
        printf(" isr1 %02x\n", m8(0x1FDA));
        w8(0x1FC4, si), w8(0x1FD4, ci);
    }
    {                                           /* (CRTC2, the second head: its control, its count moving?) */
        unsigned long c0 = m32(0x3C48), c1, j;
        unsigned n = 0;
        for (j = 0; j < 200000UL; j++)
            if (m32(0x3C48) != c0)
                n++;
        c1 = m32(0x3C48);
        printf("G450OUT crtc2 c2ctl %08lx hparam %08lx hsync %08lx vparam %08lx vsync %08lx vcount %08lx then %08lx (%u of 200,000 differed) datactl %08lx\n",
               m32(0x3C10), m32(0x3C14), m32(0x3C18), m32(0x3C1C), m32(0x3C20), c0, c1, n, m32(0x3C4C));
    }
    pal = m8(0x3C00);
    for (k = 0; k < sizeof regs / sizeof regs[0]; k++)
        printf("G450OUT dac %02x %-18s %02x\n", regs[k].idx, regs[k].name, dac(regs[k].idx));
    w8(0x3C00, pal);
    __dpmi_free_ldt_descriptor(sel);
    __dpmi_free_physical_address_mapping(&mi);
    printf("G450OUT done\n");
    return 0;
}
