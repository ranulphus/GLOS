/* Every device GLOS finds, and what serves each one (devlist.h): the Control Panel's Devices page, the shell's
 * DEVICES and SYS's STAT GLOS_STAT_DEVICES (API 2.12; the owner asked for it, 2026-11-21).
 *
 * The list is text, a line a device, then its details, each line indented by two spaces:
 *
 *   ID kind=KIND state=STATE [driver=NAME] [why=WORD] [listed=1] [can=1] [name=WORDS_WITH_UNDERSCORES]
 *      [vendor=VVVV device=DDDD class=CCSSPP rev=RR sub=VVVV:DDDD] [parent=ID] [irq=N] [dma=N,N]
 *      [io=XXXX-XXXX,...] [mem=XXXXXXXX-XXXXXXXX,...]
 *
 * ID: a PCI function's place (00:03.0), a USB device's (usb0:1.2: its controller, then its place as glos usb
 * says it), an ISA Plug and Play logical device's (pnp1.0: its card's number, then the device's), or a name (com1, lpt1, kbd, ps2mouse, fdc, game, pic, pit, rtc, dma, sb16, crystal, ne2000, vsb).
 * KIND: display, net, sound, usb, usbdev, storage, port, input, system, bridge, virtual, other. STATE: on (one
 * of GLOS's drivers has it), off (its driver left it to DOS: [devices] off, or the setting why names), refused
 * (its driver could not have it: why), dos (GLOS has no driver for it: DOS's drivers and programs use it as the
 * BIOS set it up), quiet (nobody drives it: GLOS keeps its interrupts off, so that a device of GLOS's can share
 * its line), none (a USB device GLOS has no driver for). listed=1: [devices] off names it now (it is off from
 * GLOS's next start, if it isn't already). can=1: it is one of GLOS's drivers' devices, which [devices] off
 * can leave to DOS. */
#include <string.h>
#include "devlist.h"
#include "display.h"
#include "gpu.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mm.h"
#include "mgadisp.h"
#include "midi.h"
#include "mixer.h"
#include "nic.h"
#include "plip.h"
#include "pnpbus.h"
#include "timer.h"
#include "uart.h"
#include "usb.h"
#include "vm.h"
#include "glos/bootinfo.h"

#define NOTES 16

static struct note {
    char id[12], driver[12], why[24], res[64];
    u8 state;
} notes[NOTES];
static int nnotes;

static void copy(char *to, u32 room, const char *from)
{
    u32 i;
    for (i = 0; from && from[i] && i + 1 < room; i++)
        to[i] = from[i];
    to[i] = 0;
}

static int same(const char *a, const char *b)   /* (letters' case aside) */
{
    for (; *a && *b; a++, b++)
        if ((*a | 0x20) != (*b | 0x20) && *a != *b)
            return 0;
    return !*a && !*b;
}

static struct note *find(const char *id)
{
    int i;
    for (i = 0; i < nnotes; i++)
        if (same(notes[i].id, id))
            return &notes[i];
    return 0;
}

void dev_note(const char *id, const char *driver, int state, const char *why, const char *res)
{
    struct note *n = find(id);
    if (!n) {
        if (nnotes == NOTES)
            return;
        n = &notes[nnotes++];
    }
    copy(n->id, sizeof n->id, id);
    copy(n->driver, sizeof n->driver, driver);
    copy(n->why, sizeof n->why, why);
    copy(n->res, sizeof n->res, res);
    n->state = (u8)state;
}

/* 1 if the text has the word (its words: spaces or commas between them), letters' case aside. */
static int has(const char *v, const char *id)
{
    char w[16];
    const char *p = v;
    u32 k;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\t')
            p++;
        for (k = 0; *p && *p != ' ' && *p != ',' && *p != '\t'; p++)
            if (k + 1 < sizeof w)
                w[k++] = *p;
        w[k] = 0;
        if (k && same(w, id))
            return 1;
    }
    return 0;
}

/* [devices] off names it now: GLOS.CFG as the kernel has it (written by the Control Panel since GLOS started,
   perhaps: it is off from the next start). */
static int listed(const char *id)
{
    char v[200];
    v[0] = 0;
    if (!kcfg_loaded()) {                       /* (GLOS /RUN: the kernel keeps no GLOS.CFG) */
        if (!vm.bi)
            return 0;
        memcpy(v, vm.bi->dev_off, sizeof vm.bi->dev_off);
        v[sizeof vm.bi->dev_off] = 0;
        return has(v, id);
    }
    return kcfg_get("devices", "off", v, sizeof v) >= 0 && has(v, id);
}

/* ... and as GLOS started (the loader's copy: the network card's driver looks before the kernel reads
   GLOS.CFG), which is what the drivers obey. */
int dev_turned_off(const char *id)
{
    char v[sizeof vm.bi->dev_off + 1];
    if (!vm.bi)
        return 0;
    memcpy(v, vm.bi->dev_off, sizeof vm.bi->dev_off);
    v[sizeof v - 1] = 0;
    if (!has(v, id))
        return 0;
    kprintf("GLOS-DEV off id=%s\n", id);
    return 1;
}

int dev_pci_off(struct pci_dev *d, const char *driver)
{
    char w[8];
    if (!dev_turned_off(pci_where(d, w)))
        return 0;
    dev_note(w, driver, DEVS_OFF, "", 0);
    return 1;
}

/* ---- writing the list */

struct out {
    char *b;
    u32 n, room;
};

static void put(struct out *o, const char *fmt, ...)
{
    __builtin_va_list ap;
    if (o->n + 1 >= o->room)
        return;
    __builtin_va_start(ap, fmt);
    o->n += (u32)kvsnprintf(o->b + o->n, o->room - o->n, fmt, ap);
    __builtin_va_end(ap);
    if (o->n >= o->room)
        o->n = o->room - 1;
}

/* n bytes of s, as they are (the kernel's printf has no %.*s). */
static void put_n(struct out *o, const char *s, u32 n)
{
    while (n-- && *s && o->n + 1 < o->room)
        o->b[o->n++] = *s++;
    o->b[o->n] = 0;
}

/* Another part's text (glos net's, glos audio's ...) as details: each line indented; a line that starts with
   one of skip's words left out (several, | between them). */
static void details(struct out *o, const char *text, u32 len, const char *skip)
{
    u32 i = 0, j;
    while (i < len && text[i]) {
        for (j = i; j < len && text[j] && text[j] != '\n'; j++)
            ;
        if (j > i) {
            int out = 1;
            const char *s = skip;
            while (s && *s && out) {
                u32 k = 0;
                while (s[k] && s[k] != '|' && i + k < j && s[k] == text[i + k])
                    k++;
                if (!s[k] || s[k] == '|')
                    out = 0;
                while (*s && *s != '|')
                    s++;
                if (*s == '|')
                    s++;
            }
            if (out) {
                put(o, "  ");
                put_n(o, text + i, j - i);
                put(o, "\n");
            }
        }
        i = j + 1;
    }
}

static const char *pci_kind(const struct pci_dev *d)
{
    switch (d->class) {
    case 0x01: return "storage";
    case 0x02: return "net";
    case 0x03: return "display";
    case 0x04: return d->subclass == 0x00 ? "display" : d->subclass == 0x01 || d->subclass == 0x03 ? "sound" : "other";
                                                /* (04 00: video, a Voodoo Graphics; 01 and 03: audio) */
    case 0x06: return "bridge";
    case 0x07: return "port";
    case 0x08: return "system";
    case 0x09: return "input";
    case 0x0C: return d->subclass == 0x03 ? "usb" : "system";
    default: return "other";
    }
}

static void ranges(struct out *o, const struct pci_dev *d)
{
    int k, io = 0, mem = 0;
    for (k = 0; k < 6; k++)
        if (d->bar[k] && (d->bar[k] & 1) && d->bar_size[k]) {
            u32 b = d->bar[k] & ~3u;
            put(o, "%s%04x-%04x", io++ ? "," : " io=", b, b + d->bar_size[k] - 1);
        }
    for (k = 0; k < 6; k++)
        if (d->bar[k] && !(d->bar[k] & 1) && d->bar_size[k] && (d->bar[k] & ~15u)) {
            u32 b = d->bar[k] & ~15u;
            put(o, "%s%08x-%08x", mem++ ? "," : " mem=", b, b + d->bar_size[k] - 1);
        }
}

static void pci_lines(struct out *o, char *tmp, u32 tmp_room)
{
    static const char *const keeper[] = { "dos", "on", "quiet" };
    const struct pci_dev *d, *lfb_card = 0;
    const char *refusal = nic_refusal();
    u32 lfb_base = 0, x;
    char w[8];
    int i;
    pci_lfb_range(&lfb_base, &lfb_card);
    for (i = 0; (d = pci_at(i)) != 0; i++) {
        const struct note *n = find(pci_where(d, w));
        const char *kind = pci_kind(d), *state = keeper[d->keeper < 3 ? d->keeper : 0], *driver = 0, *why = 0;
        if (n) {
            state = n->state == DEVS_ON ? "on" : n->state == DEVS_OFF ? "off" : "refused";
            driver = n->driver, why = n->why;
        } else if (d->class == 0x02 && refusal) {      /* (no card was looked for) */
            state = "off", why = refusal;
        } else if (d->class == 0x0C && d->subclass == 0x03 && d->progif == 0x00 && usb_off()) {
            state = "off", why = "usb-setting";
        } else if (d->class == 0x0C && d->subclass == 0x03 && usb_left()) {
            state = "off", why = "bios-disk";
        } else if (d->class == 0x03 && mga_is(d->bus, d->dev, d->fn)) {
            state = "on", driver = "mga";       /* (GLOS's engine: the desktop's drawing, and its apps' turns) */
        } else if (d->class == 0x03 && d == lfb_card) {
            state = "on", driver = "vbe";       /* (GLOS draws the desktop through its VESA BIOS) */
        }
        put(o, "%s kind=%s state=%s", w, kind, state);
        if (driver && *driver)
            put(o, " driver=%s", driver);
        if (why && *why)
            put(o, " why=%s", why);
        if (listed(w))
            put(o, " listed=1");
        if (n)
            put(o, " can=1");
        put(o, " vendor=%04x device=%04x class=%02x%02x%02x rev=%02x", d->vendor, d->device, d->class, d->subclass,
            d->progif, d->rev);
        if (d->sub_vendor && d->sub_vendor != 0xFFFF)
            put(o, " sub=%04x:%04x", d->sub_vendor, d->sub_device);
        if (d->pin && d->irq && d->irq < 16)
            put(o, " irq=%u", d->irq);
        ranges(o, d);
        put(o, "\n");
        if (n && n->state == DEVS_ON && d->class == 0x02) {
            x = net_stat(tmp, tmp_room, 0);
            details(o, tmp, x, "sockets");
        } else if (n && n->state == DEVS_ON && d->class == 0x0C) {
            x = usb_hc_detail(d, tmp, tmp_room);
            details(o, tmp, x, 0);
        } else if (d->class == 0x03 && (d == lfb_card || mga_is(d->bus, d->dev, d->fn))) {
            x = display_stat(tmp, tmp_room);
            x += gpu_stat(tmp + x, tmp_room - x);
            details(o, tmp, x, 0);
        }
    }
}

/* A note's device that is not a PCI function: an ISA card (the Sound Blaster 16, a Crystal chip, an NE2000). */
static void isa_lines(struct out *o, char *tmp, u32 tmp_room)
{
    int i;
    u32 x;
    for (i = 0; i < nnotes; i++) {
        const struct note *n = &notes[i];
        const char *kind = same(n->id, "ne2000") ? "net" : "sound";
        if (n->id[2] == ':')                    /* (a PCI function's: written above) */
            continue;
        put(o, "%s kind=%s state=%s driver=%s", n->id, kind,
            n->state == DEVS_ON ? "on" : n->state == DEVS_OFF ? "off" : "refused", n->driver);
        if (n->why[0])
            put(o, " why=%s", n->why);
        if (listed(n->id))
            put(o, " listed=1");
        put(o, " can=1%s%s\n", n->res[0] ? " " : "", n->res);
        if (n->state == DEVS_ON && !same(kind, "net")) {
            x = mixer_stat(tmp, tmp_room);
            details(o, tmp, x, "blaster virtual");
        } else if (n->state == DEVS_ON) {
            x = net_stat(tmp, tmp_room, 0);
            details(o, tmp, x, "sockets");
        }
    }
}

/* ISA Plug and Play's logical devices, as the bus's scan found them (pnpbus.c; phase H, tier 2). The bus is never
   scanned for the list: it is listed when it has been (a PC with no BLASTER, whose sound is looked for there, or
   after glos pnp). A device is on if its ports are among those of a device one of GLOS's drivers has (the Crystal
   driver sets its card up with its own key, not through the bus), dos if it is set up and GLOS doesn't drive it,
   none if nothing has set it up. Its kind by its IDs: a Sound Blaster's or an MPU's (PNPB0xx, PNPB006) or a known
   maker's audio device is sound, a game port (PNPB02F, CSC0001, CTL7001) input, PNP0600 storage, PNP050x and
   PNP040x port. */
static int io_overlaps(const char *res, u16 lo, u16 hi)
{
    const char *p = res;
    while (p && *p && !(p[0] == 'i' && p[1] == 'o' && p[2] == '='))
        p++;
    if (!p || !*p)
        return 0;
    p += 3;
    for (;;) {
        u32 a = 0, b = 0, k;
        for (k = 0; k < 4 && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')); k++, p++)
            a = a << 4 | (u32)(*p <= '9' ? *p - '0' : *p - 'a' + 10);
        if (*p != '-')
            return 0;
        p++;
        for (k = 0; k < 4 && ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f')); k++, p++)
            b = b << 4 | (u32)(*p <= '9' ? *p - '0' : *p - 'a' + 10);
        if (a <= hi && b >= lo)
            return 1;
        if (*p != ',')
            return 0;
        p++;
    }
}

static const char *pnp_kind(const char *id, const char *compat)
{
    static const char *const inputs[] = { "PNPB02F", "CSC0001", "CTL7001", "ESS0001", 0 };
    static const char *const sounds[] = { "PNPB0", "CSC0000", "CSC0003", "CSC0010", "CTL00", "ESS1", "ALS00",
                                          "OPT9", 0 };
    u32 k;
    for (k = 0; inputs[k]; k++)
        if (same(id, inputs[k]) || same(compat, inputs[k]))
            return "input";
    if (!strncmp(id, "PNP0600", 7) || !strncmp(compat, "PNP0600", 7))
        return "storage";
    if (!strncmp(id, "PNP050", 6) || !strncmp(id, "PNP040", 6) || !strncmp(compat, "PNP050", 6)
        || !strncmp(compat, "PNP040", 6))
        return "port";
    for (k = 0; sounds[k]; k++)
        if (!strncmp(id, sounds[k], strlen(sounds[k])) || !strncmp(compat, sounds[k], strlen(sounds[k])))
            return "sound";
    return "other";
}

static void pnp_lines(struct out *o)
{
    const struct pnp *b = pnpbus_scanned();
    u32 i, k;
    for (i = 0; b && i < b->ndevs; i++) {
        const struct pnp_dev *d = &b->dev[i];
        const struct note *by = 0;
        char id[8], compat[8], card[8];
        int n, mpu;
        pnp_id_text(d->id, id);
        pnp_id_text(d->card, card);
        compat[0] = 0;
        if (d->compat)
            pnp_id_text(d->compat, compat);
        for (n = 0; d->active && !by && n < nnotes; n++)
            for (k = 0; k < 4 && !by; k++)
                if (notes[n].state == DEVS_ON && d->io[k] && io_overlaps(notes[n].res, d->io[k], d->io[k] + 7))
                    by = &notes[n];
        mpu = !by && d->active && midi_mpu_port() && d->io[0] == midi_mpu_port();      /* (GLOS's MIDI's MPU-401) */
        put(o, "pnp%u.%u kind=%s state=%s", d->csn, d->ldn, pnp_kind(id, compat),
            by || mpu ? "on" : d->active ? "dos" : "none");
        if (by || mpu)
            put(o, " driver=%s", by ? by->driver : "midi");
        put(o, " name=%s", id);
        if (d->active && d->irq[0])
            put(o, " irq=%u", d->irq[0]);
        if (d->active && d->dma[0] < 4)
            put(o, d->dma[1] < 8 ? " dma=%u,%u" : " dma=%u", d->dma[0], d->dma[1]);
        for (k = 0; d->active && k < 4 && d->io[k]; k++)
            put(o, "%s%04x-%04x", k ? "," : " io=", d->io[k],
                d->io[k] + (k < d->n_io && d->io_opt[k].len ? d->io_opt[k].len : 8) - 1);
        put(o, "\n  card %s, logical device %u%s%s\n", card, d->ldn, compat[0] ? ", compatible " : "", compat);
    }
}

/* The PC's own: its ports, as the BIOS counted them; the keyboard's controller; the floppy drives; the clocks.
   Then GLOS's own devices for DOS's programs. */
static void legacy_lines(struct out *o, char *tmp, u32 tmp_room)
{
    static const char *const fd[] = { "none", "360 KB", "1.2 MB", "720 KB", "1.44 MB", "2.88 MB", "2.88 MB" };
    u16 equip = vm_rd16(0x410), port;
    u32 lpt = 0, irq = 0, k, x;
    u16 lbase = 0;
    u8 types;
    char *v;
    for (k = 0; k < 4; k++)
        if ((port = vm_rd16(0x400 + 2 * k)) != 0) {
            int ours = uart_is_open(port);
            put(o, "com%u kind=port state=%s%s name=COM%u io=%04x-%04x irq=%u\n", k + 1, ours ? "on" : "dos",
                ours ? " driver=uart" : "", k + 1, port, port + 7, (k & 1) ? 3 : 4);
            if (ours)
                put(o, "  GLOS's: the cable link, or a serial session\n");
        }
    plip_info(&lpt, &lbase, &irq, 0);
    for (k = 0; k < 3; k++)
        if ((port = vm_rd16(0x408 + 2 * k)) != 0) {
            int ours = lpt == k + 1;
            put(o, "lpt%u kind=port state=%s%s name=LPT%u io=%04x-%04x", k + 1, ours ? "on" : "dos",
                ours ? " driver=plip" : "", k + 1, port, port + 2);
            if (ours && irq)
                put(o, " irq=%u", irq);
            put(o, "\n");
            if (ours)
                put(o, "  GLOS's: the cable link over a parallel cable\n");
        }
    put(o, "kbd kind=input state=on driver=8042 name=Keyboard_controller io=0060,0064 irq=1\n"
           "  GLOS reads the keyboard; DOS's programs are given what it types\n");
    if (equip & 4)
        put(o, "ps2mouse kind=input state=on driver=8042 name=PS/2_mouse irq=12\n");
    if (equip & 1) {
        types = rtc_read(0x10);
        put(o, "fdc kind=storage state=dos name=Floppy_controller io=03f0-03f7 irq=6 dma=2\n");
        put(o, "  A: %s\n", fd[(types >> 4) < 7 ? types >> 4 : 0]);
        if (((equip >> 6) & 3) >= 1 && (types & 15))
            put(o, "  B: %s\n", fd[(types & 15) < 7 ? types & 15 : 0]);
    }
    if (vm_rd8(0x475))
        put(o, "disks kind=storage state=dos name=Hard_disks\n  %u, as the BIOS counted them\n", vm_rd8(0x475));
    if (equip & 0x1000)
        put(o, "game kind=port state=dos name=Game_port io=0201\n");
    put(o, "pic kind=system state=on driver=glos name=Interrupt_controllers io=0020-0021,00a0-00a1\n"
           "pit kind=system state=dos name=System_timer io=0040-0043 irq=0\n"
           "  DOS's clock: GLOS keeps it as DOS set it\n"
           "rtc kind=system state=on driver=glos name=Real-time_clock io=0070-0071 irq=8\n"
           "  GLOS's clock\n"
           "dma kind=system state=dos name=DMA_controllers io=0000-000f,0080-008f,00c0-00df\n");
    x = mixer_stat(tmp, tmp_room);              /* GLOS's own Sound Blaster for DOS's programs (vsbdev.c) */
    for (v = tmp; v + 16 <= tmp + x; v++)
        if (!memcmp(v, "blaster virtual ", 16) && (v == tmp || v[-1] == '\n')) {
            char *e = v + 16;
            while (e < tmp + x && *e != '\n')
                e++;
            put(o, "vsb kind=virtual state=on driver=glos name=Sound_Blaster_(GLOS's)\n  BLASTER=");
            put_n(o, v + 16, (u32)(e - v - 16));
            put(o, "\n");
            break;
        }
}

u32 dev_list(char *b, u32 room)
{
    struct out o = { b, 0, room };
    u32 tmp_room = 1536;
    char *tmp = kmalloc(tmp_room);
    if (!room)
        return 0;
    b[0] = 0;
    if (!tmp)
        return 0;
    pci_lines(&o, tmp, tmp_room);
    isa_lines(&o, tmp, tmp_room);
    pnp_lines(&o);
    o.n += usb_dev_lines(b + o.n, room - o.n);
    legacy_lines(&o, tmp, tmp_room);
    kfree(tmp);
    return o.n;
}
