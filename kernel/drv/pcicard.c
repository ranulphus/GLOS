/* A display card the BIOS couldn't start (docs/v1/native-dos.md, N4; [display] card): the card [display] card names
 * by its PCI address is made GLOS's display. Two steps:
 *
 * pcicard_place (kmain, before display_init: before any driver of GLOS's looks at the cards): its memory BARs and
 * ROM that read 0 are given places (kernel/drv/pcialloc.c: aligned to their sizes, clear of every other function's
 * BARs, every other bridge's windows, its own bridge's other window, the RAM and the firmware's top), the bridge
 * above it has its window widened to hold them, and the card's memory decode and bus mastering are turned on.
 * Bench C's 486 (2026-11-26): its PCI G450 behind a HiNT bridge, beside the ViRGE the BIOS started, had only its
 * 16 KB of registers placed (or nothing at all). One bridge level for now (a deeper chain is refused, said why).
 *
 * pcicard_start (the system VM's thread, once it runs DOS: natively before EMS chooses its window, hosted when the
 * stub goes resident): the card's x86 ROM image read (its ROM BAR placed in the bridge's window for the reading
 * and given back), copied into frames of GLOS's and shown at C0000h by the VM's page tables (where a VGA BIOS
 * expects to be; the old card's ROM no longer seen there); the VGA ranges switched to the card (the old display's
 * I/O and memory decode off, the card's bridge's VGA Enable on, the card's own decode on); INT 10h pointed at the
 * system BIOS's own video handler (INT 42h's, which the old VGA BIOS moved there) so that the card's BIOS starts as
 * the PC's only one; its entry (C000:0003) called with AX its bus and device, on a stack of 4 KB of DOS's memory
 * (a BIOS's start goes deep). Then INT 10h's mode 3; where the card's BIOS leaves the CRTC's syncs off (bench C's
 * G450 run from DOS memory did), text mode 3 is set by its registers, said so. */
#include <string.h>

#include "glos/bootinfo.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "pci.h"
#include "pcialloc.h"
#include "pcicard.h"
#include "vm.h"
#include "dpmi.h"

#define MAXBUS 8

static int on;                                  /* placed: the card is to be started */
/* What pcicard_start changed that DOS must have back when a hosted GLOS leaves (pcicard_leave): the PC's INT 10h
   (the old display's BIOS) and INT 6Dh, the old display's command word and its place, the card's bridge's control
   word. started: there is something to give back. */
static struct { int started, ob, od, of; u32 v10, v6d, ocmd, bctl; } was = { 0, -1, 0, 0, 0, 0, 0, 0 };
static int cb, cd, cf, bd = -1, bf;             /* the card; its bridge on bus 0 (bd -1: none, the card on bus 0) */
static u32 rom_size;
static u32 card_np_lo, card_np_hi;              /* (the bridge's memory window, for the ROM's reading) */
static struct pa_range keep_np[6];
static int nkeep_np;
static struct pa_range busy_kept[48];           /* (what the ROM's place may not touch either: a card on bus 0's) */
static int nbusy_kept;

static u32 cfg(int b, int d, int f, int r) { return pci_read32((u32)b, (u32)d, (u32)f, (u32)r); }
static void wcfg(int b, int d, int f, int r, u32 v) { pci_write32((u32)b, (u32)d, (u32)f, (u32)r, v); }

/* A memory BAR's size, its function's decode off meanwhile (rom: the ROM BAR). 0: none. */
static u32 bar_size(int b, int d, int f, int r, int rom)
{
    u32 cmd = cfg(b, d, f, 4), old = cfg(b, d, f, r), got;
    wcfg(b, d, f, 4, cmd & ~3u);
    wcfg(b, d, f, r, rom ? 0xFFFFF800u : 0xFFFFFFFFu);
    got = cfg(b, d, f, r);
    wcfg(b, d, f, r, old);
    wcfg(b, d, f, 4, cmd);
    got &= rom ? 0xFFFFF800u : 0xFFFFFFF0u;
    return got ? ~got + 1 : 0;
}

static int present(int b, int d, int f) { return (cfg(b, d, f, 0) & 0xFFFF) != 0xFFFF; }

void pcicard_place(const struct bootinfo *bi)
{
    struct pa_range busy[48], keep_pf[6];
    u32 bar[6], size[6], np, pf, top = 0, i;
    int nbusy = 0, npf = 0, b, d, f, k, unplaced = 0;
    char why[48];

    if (!(bi->display_card & BI_CARD_SET))
        return;
    cb = (int)(bi->display_card >> 8 & 0xFF), cd = (int)(bi->display_card >> 3 & 31), cf = (int)(bi->display_card & 7);
    if (!present(cb, cd, cf) || (cfg(cb, cd, cf, 8) >> 24) != 0x03) {
        kprintf("GLOS-CARD refused card=%02x:%02x.%u why=%s\n", cb, cd, cf, present(cb, cd, cf) ? "not-display" : "none");
        return;
    }
    if (cb) {                                   /* its bridge: on bus 0, its secondary bus the card's */
        for (d = 0; d < 32 && bd < 0; d++)
            for (f = 0; f < 8; f++) {
                if (!present(0, d, f)) {
                    if (!f)
                        break;
                    continue;
                }
                if ((cfg(0, d, f, 8) >> 16) == 0x0604 && (int)(cfg(0, d, f, 0x18) >> 8 & 0xFF) == cb) {
                    bd = d, bf = f;
                    break;
                }
            }
        if (bd < 0) {
            kprintf("GLOS-CARD refused card=%02x:%02x.%u why=bridge-not-on-bus-0\n", cb, cd, cf);
            return;
        }
    }
    for (k = 0; k < 6; k++) {
        bar[k] = cfg(cb, cd, cf, 0x10 + 4 * k);
        size[k] = (bar[k] & 1) ? 0 : bar_size(cb, cd, cf, 0x10 + 4 * k, 0);
        unplaced += size[k] && !(bar[k] & 0xFFFFFFF0u);
    }
    rom_size = bar_size(cb, cd, cf, 0x30, 1);
    /* what may not be touched */
    for (i = 0; i < bi->n_ranges && i < BI_MEM_RANGES; i++)
        if (bi->range[i].base + bi->range[i].length > top)
            top = bi->range[i].base + bi->range[i].length;
    busy[nbusy].base = 0, busy[nbusy++].last = (top > 0x01000000u ? top : 0x01000000u) - 1;
    busy[nbusy].base = 0xFEC00000u, busy[nbusy++].last = 0xFFFFFFFFu;
    for (b = 0; b < MAXBUS; b++)
        for (d = 0; d < 32; d++)
            for (f = 0; f < 8; f++) {
                u32 hdr;
                if (!present(b, d, f)) {
                    if (!f)
                        break;
                    continue;
                }
                hdr = cfg(b, d, f, 0x0C) >> 16 & 0x7F;
                if (b == cb && d == cd && f == cf)
                    continue;
                if (hdr == 1) {                 /* a bridge: its windows (the card's own bridge's are placed into) */
                    if (b == 0 && d == bd && f == bf)
                        continue;
                    np = cfg(b, d, f, 0x20), pf = cfg(b, d, f, 0x24);
                    if ((np & 0xFFF0) << 16 <= (np & 0xFFF00000u) && nbusy < 46)
                        busy[nbusy].base = (np & 0xFFF0) << 16, busy[nbusy++].last = (np & 0xFFF00000u) | 0xFFFFF;
                    if ((pf & 0xFFF0) << 16 <= (pf & 0xFFF00000u) && nbusy < 46)
                        busy[nbusy].base = (pf & 0xFFF0) << 16, busy[nbusy++].last = (pf & 0xFFF00000u) | 0xFFFFF;
                } else if (hdr == 0) {
                    for (k = 0; k < 6 && nbusy < 46; k++) {
                        u32 v = cfg(b, d, f, 0x10 + 4 * k), s;
                        if ((v & 1) || !(v & 0xFFFFFFF0u) || !(s = bar_size(b, d, f, 0x10 + 4 * k, 0)))
                            continue;
                        busy[nbusy].base = v & 0xFFFFFFF0u, busy[nbusy++].last = (v & 0xFFFFFFF0u) + s - 1;
                        if ((v & 6) == 4)
                            k++;                /* (a 64-bit BAR's upper half) */
                    }
                }
                if (!f && !(cfg(b, d, 0, 0x0C) & 0x00800000u))
                    break;                      /* (not a multi-function device) */
            }
    np = bd >= 0 ? cfg(0, bd, bf, 0x20) : 0x0000FFF0u, pf = bd >= 0 ? cfg(0, bd, bf, 0x24) : 0x0000FFF0u;
    {                                           /* (on bus 0, no bridge: empty windows, the place anywhere free) */
        struct pa_range win_np = { (np & 0xFFF0) << 16, (np & 0xFFF00000u) | 0xFFFFF };
        struct pa_range win_pf = { (pf & 0xFFF0) << 16, (pf & 0xFFF00000u) | 0xFFFFF };
        nkeep_np = 0;
        for (k = 0; k < 6; k++)
            if (size[k] && (bar[k] & 0xFFFFFFF0u)) {
                struct pa_range *kp = (bar[k] & 8) ? &keep_pf[npf++] : &keep_np[nkeep_np++];
                kp->base = bar[k] & 0xFFFFFFF0u, kp->last = kp->base + size[k] - 1;
            }
        memcpy(busy_kept, busy, sizeof busy[0] * (u32)nbusy);
        nbusy_kept = nbusy;
        card_np_lo = (u32)win_np.base, card_np_hi = (u32)win_np.last;
        if (!unplaced) {                        /* (all placed by the BIOS: only its BIOS to run) */
            kprintf("GLOS-CARD placed card=%02x:%02x.%u by=bios\n", cb, cd, cf);
            on = 1;
            return;
        }
        for (k = 0; k < 6; k++) {
            struct pa_range b2[49];
            unsigned long at;
            int pre = (bar[k] & 8) != 0;
            if (!size[k] || (bar[k] & 0xFFFFFFF0u))
                continue;
            memcpy(b2, busy, sizeof busy[0] * (u32)nbusy);
            b2[nbusy] = pre ? win_np : win_pf;
            if (pa_place(b2, nbusy + 1, pre ? keep_pf : keep_np, pre ? npf : nkeep_np, pre ? &win_pf : &win_np, size[k],
                         0x01000000u, 0xFEC00000u, &at) != 0) {
                ksnprintf(why, sizeof why, "no-room-bar%d-%uk", k, size[k] >> 10);
                kprintf("GLOS-CARD refused card=%02x:%02x.%u why=%s\n", cb, cd, cf, why);
                return;
            }
            bar[k] = (u32)at | (bar[k] & 0xF);
            if (pre)
                keep_pf[npf].base = at, keep_pf[npf++].last = at + size[k] - 1;
            else
                keep_np[nkeep_np].base = at, keep_np[nkeep_np++].last = at + size[k] - 1;
            kprintf("GLOS-PCI place %02x:%02x.%u bar%d=%08x size=%uk\n", cb, cd, cf, k, (u32)at, size[k] >> 10);
        }
        if (bd >= 0) {
            wcfg(0, bd, bf, 0x20, (u32)(win_np.last & 0xFFF00000u) | (u32)(win_np.base >> 16 & 0xFFF0));
            wcfg(0, bd, bf, 0x24, (u32)(win_pf.last & 0xFFF00000u) | (u32)(win_pf.base >> 16 & 0xFFF0) | (pf & 0xF));
            if ((pf & 0xF) == 1)
                wcfg(0, bd, bf, 0x28, 0), wcfg(0, bd, bf, 0x2C, 0);
        }
        for (k = 0; k < 6; k++)
            if (size[k])
                wcfg(cb, cd, cf, 0x10 + 4 * k, bar[k]);
        if (bd >= 0)
            wcfg(0, bd, bf, 4, cfg(0, bd, bf, 4) | 6);
        wcfg(cb, cd, cf, 4, (cfg(cb, cd, cf, 4) & ~0x400u) | 6);
        card_np_lo = (u32)win_np.base, card_np_hi = (u32)win_np.last;
        kprintf("GLOS-CARD placed card=%02x:%02x.%u by=glos bridge=%s mem=%08x-%08x prefetch=%08x-%08x\n", cb, cd, cf,
                bd >= 0 ? "yes" : "none", (u32)win_np.base, (u32)win_np.last, (u32)win_pf.base, (u32)win_pf.last);
    }
    on = 1;
}

int pcicard_named(void) { return on; }

/* ---- the card's BIOS run (pcicard_start) */

static u32 vec(u32 n) { return (u32)vm_rd16(n * 4 + 2) << 16 | vm_rd16(n * 4); }

static void set_vec(u32 n, u32 v)
{
    vm_wr16(n * 4, v & 0xFFFF);
    vm_wr16(n * 4 + 2, v >> 16);
}

static int rm(struct rmregs *r, int kind, u8 v, u32 ss, u32 sp)
{
    r->ss = (u16)ss, r->sp = (u16)sp, r->flags = 0x202;
    return rm_call(0, r, kind, v, 0, 0);
}

/* Text mode 3 by its registers (a VGA's standard values; a Matrox card's CRTC extensions cleared at 3DEh/3DFh). */
static void mode3_by_registers(int matrox)
{
    static const u8 seq[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
    static const u8 crtc[25] = { 0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F, 0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00,
                                 0x00, 0x00, 0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3, 0xFF };
    static const u8 gc[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00, 0xFF };
    static const u8 ac[21] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D,
                               0x3E, 0x3F, 0x0C, 0x00, 0x0F, 0x08, 0x00 };
    u32 j;
    if (matrox)
        for (j = 0; j < 6; j++)
            outb(0x3DE, (u8)j), outb(0x3DF, 0);
    outb(0x3C2, 0x67);
    outb(0x3C4, 0), outb(0x3C5, 1);
    for (j = 1; j < 5; j++)
        outb(0x3C4, (u8)j), outb(0x3C5, seq[j]);
    outb(0x3C4, 0), outb(0x3C5, 3);
    outb(0x3D4, 0x11), outb(0x3D5, inb(0x3D5) & 0x7F);
    for (j = 0; j < 25; j++)
        outb(0x3D4, (u8)j), outb(0x3D5, crtc[j]);
    for (j = 0; j < 9; j++)
        outb(0x3CE, (u8)j), outb(0x3CF, gc[j]);
    (void)inb(0x3DA);
    for (j = 0; j < 21; j++)
        outb(0x3C0, (u8)j), outb(0x3C0, ac[j]);
    outb(0x3C0, 0x20);
}

static int syncs_on(void)
{
    outb(0x3D4, 0x17);
    return (inb(0x3D5) & 0x80) != 0;
}

int pcicard_start(u32 stack_seg, u32 stack_bytes)
{
    struct pa_range none[1] = { { 0, 0 } }, win = { card_np_lo, card_np_hi };
    unsigned long at;
    u8 *rom;
    u32 off = 0, len, pages, p, frames[16], v10, i, sum;
    int b, d, f, ob = -1, od = -1, of = -1, ok;
    struct rmregs r;

    if (!on)
        return -1;
    /* the card the BIOS started already (its I/O decode on, the image at C0000h its own by its PCI header): left
       as it is, its BIOS not run twice */
    if ((cfg(cb, cd, cf, 4) & 1) && vm_rd16(0xC0000) == 0xAA55) {
        u32 pcir = 0xC0000u + vm_rd16(0xC0018);
        if (vm_rd16(pcir) == 0x4350 && vm_rd16(pcir + 2) == 0x5249
            && ((u32)vm_rd16(pcir + 6) << 16 | vm_rd16(pcir + 4)) == cfg(cb, cd, cf, 0)) {
            kprintf("GLOS-CARD start-skipped card=%02x:%02x.%u why=the-bios-display\n", cb, cd, cf);
            return 0;
        }
    }
    if (!rom_size) {
        kprintf("GLOS-CARD start-refused why=no-rom\n");
        return -1;
    }
    /* its ROM, placed in the bridge's memory window for the reading (on bus 0: anywhere free) */
    if (pa_place(bd >= 0 ? none : busy_kept, bd >= 0 ? 0 : nbusy_kept, keep_np, nkeep_np, &win, rom_size, 0x01000000u,
                 0xFEC00000u, &at) != 0
        || (bd >= 0 && ((u32)at < card_np_lo || (u32)at + rom_size - 1 > card_np_hi))
        || !(rom = mm_dev_map((u32)at, rom_size))) {
        kprintf("GLOS-CARD start-refused why=rom-no-place\n");
        return -1;
    }
    wcfg(cb, cd, cf, 0x30, (u32)at | 1);
    for (;;) {                                  /* (the x86 image: code type 0, else the next while not the last) */
        u32 pcir = rom[off + 0x18] | (u32)rom[off + 0x19] << 8;
        if (rom[off] != 0x55 || rom[off + 1] != 0xAA || off + pcir + 0x18 > rom_size || memcmp(rom + off + pcir, "PCIR", 4))
            break;
        if (rom[off + pcir + 0x14] == 0 || (rom[off + pcir + 0x15] & 0x80))
            break;
        off += (rom[off + pcir + 0x10] | (u32)rom[off + pcir + 0x11] << 8) * 512;
        if (off >= rom_size)
            break;
    }
    len = off < rom_size ? (u32)rom[off + 2] * 512 : 0;
    pages = (len + 4095) / 4096;
    if (rom[off] != 0x55 || rom[off + 1] != 0xAA || !len || off + len > rom_size || pages > 16) {
        wcfg(cb, cd, cf, 0x30, 0);
        kprintf("GLOS-CARD start-refused why=no-x86-image len=%u\n", len);
        return -1;
    }
    for (sum = 0, i = 0; i < len; i++)
        sum += rom[off + i];
    for (p = 0; p < pages; p++) {
        u32 n = len - p * 4096 < 4096 ? len - p * 4096 : 4096;
        u8 *to;
        if (!(frames[p] = pmm_alloc())) {
            while (p--)
                pmm_free(frames[p]);
            wcfg(cb, cd, cf, 0x30, 0);
            kprintf("GLOS-CARD start-refused why=memory\n");
            return -1;
        }
        to = kmap(frames[p]);
        memset(to, 0, 4096);
        memcpy(to, rom + off + p * 4096, n);
    }
    wcfg(cb, cd, cf, 0x30, 0);                  /* (the ROM's range given up) */
    kprintf("GLOS-CARD rom card=%02x:%02x.%u from=%08x image=%u bytes=%u checksum=%02x\n", cb, cd, cf, (u32)at, off, len,
            sum & 0xFF);

    /* the VGA ranges: the old display's decode off, the card's bridge's VGA Enable on */
    for (b = 0; b < MAXBUS && ob < 0; b++)
        for (d = 0; d < 32 && ob < 0; d++)
            for (f = 0; f < 8; f++) {
                if (!present(b, d, f)) {
                    if (!f)
                        break;
                    continue;
                }
                if ((b != cb || d != cd || f != cf) && (cfg(b, d, f, 8) >> 24) == 0x03 && (cfg(b, d, f, 4) & 1)) {
                    ob = b, od = d, of = f;
                    break;
                }
            }
    was.started = 1, was.ob = ob, was.od = od, was.of = of;
    was.v10 = vec(0x10), was.v6d = vec(0x6D);
    if (ob >= 0) {
        was.ocmd = cfg(ob, od, of, 4) & 0xFFFF;
        wcfg(ob, od, of, 4, cfg(ob, od, of, 4) & ~3u);
    }
    if (bd >= 0) {
        was.bctl = cfg(0, bd, bf, 0x3C) >> 16 & 0xFFFF;
        wcfg(0, bd, bf, 0x3C, cfg(0, bd, bf, 0x3C) | (8u << 16));
        wcfg(0, bd, bf, 4, cfg(0, bd, bf, 4) | 7);
    }
    wcfg(cb, cd, cf, 4, (cfg(cb, cd, cf, 4) & ~0x400u) | 7);

    /* its image at C0000h; INT 10h the system BIOS's own, as at the PC's start */
    for (p = 0; p < pages; p++)
        mm_low_page(0xC0000u + p * 4096, frames[p]);
    v10 = vec(0x10);
    if ((vec(0x42) >> 16) == 0xF000)
        set_vec(0x10, vec(0x42));
    memset(&r, 0, sizeof r);
    r.eax = (u32)(cb << 8 | cd << 3 | cf);
    r.cs = 0xC000, r.ip = 3;
    kprintf("GLOS-CARD post card=%02x:%02x.%u old=%02x:%02x.%u int10=%04x:%04x pages=%u\n", cb, cd, cf, ob < 0 ? 0 : ob,
            od < 0 ? 0 : od, of < 0 ? 0 : of, v10 >> 16, v10 & 0xFFFF, pages);
    if (rm(&r, RM_FAR, 0, stack_seg, stack_bytes - 2) != 0) {
        kprintf("GLOS-CARD post-failed why=rm-call\n");
        return -1;
    }
    v10 = vec(0x10);
    ok = (v10 >> 16) == 0xC000;
    kprintf("GLOS-CARD post-done int10=%04x:%04x %s\n", v10 >> 16, v10 & 0xFFFF, ok ? "ok" : "not-hooked");
    if (!ok)
        return -1;
    memset(&r, 0, sizeof r);
    r.eax = 0x0003;
    rm(&r, RM_INT, 0x10, stack_seg, stack_bytes - 2);
    if (!syncs_on()) {                          /* (bench C's G450, its BIOS run from DOS memory: syncs left off) */
        mode3_by_registers((cfg(cb, cd, cf, 0) & 0xFFFF) == 0x102B);
        memset(&r, 0, sizeof r);
        r.eax = 0x1104;                         /* (its BIOS's 8 x 16 font into plane 2) */
        rm(&r, RM_INT, 0x10, stack_seg, stack_bytes - 2);
        kprintf("GLOS-CARD mode3 by=registers syncs=%s\n", syncs_on() ? "on" : "off");
    } else {
        kprintf("GLOS-CARD mode3 by=bios\n");
    }
    return 0;
}

/* A hosted GLOS leaves (v86.c's vm_leave, after display_leave): DOS gets the PC back as its BIOS had it. The card's
   BIOS was in GLOS's own pages at C0000h, which go with GLOS: left as it was, INT 10h would point into the middle
   of the old display's BIOS, which is what C0000h holds without GLOS (bench C, 2026-11-27: the next GLOS.EXE hung
   at its first line on the screen). So INT 10h and INT 6Dh as they were before the card's start, the card's
   bridge's VGA Enable off again, and the old display's decode on: it shows DOS's screen as before. */
void pcicard_leave(void)
{
    if (!was.started)
        return;
    set_vec(0x10, was.v10);
    set_vec(0x6D, was.v6d);
    if (bd >= 0)
        wcfg(0, bd, bf, 0x3C, (cfg(0, bd, bf, 0x3C) & 0xFFFFu) | was.bctl << 16);
    if (was.ob >= 0)
        wcfg(was.ob, was.od, was.of, 4, (cfg(was.ob, was.od, was.of, 4) & ~0xFFFFu) | was.ocmd);
    kprintf("GLOS-CARD left card=%02x:%02x.%u int10=%04x:%04x old=%02x:%02x.%u\n", cb, cd, cf, was.v10 >> 16,
            was.v10 & 0xFFFF, was.ob < 0 ? 0 : was.ob, was.od, was.of);
    was.started = 0;
}
