/* CONTROL's Devices page (phase F; the owner asked for it, 2026-11-21): every device GLOS finds, as a tree by
 * kind, read from SYS's STAT GLOS_STAT_DEVICES (API 2.12; kernel/drv/devlist.c says how its lines are written)
 * every two seconds. A PCI device's name is found in PCIIDS.DAT beside the program (the PCI ID Project's list:
 * tools/mkpciids.py, apps/shared/pciids.c); USB devices hang under their controller. Under the tree, the chosen
 * one: its name, where it is, what serves it in plain words, its resources, its numbers and what its driver
 * says of it.
 *
 * Turn off / Turn on: a device one of GLOS's drivers can have is named in the page's setting ([devices] off) or
 * taken out of it; the Control Panel's Apply writes it, and it takes effect when GLOS next starts (the drivers
 * look as GLOS starts). Turning off the network card GLOS uses, or a USB controller, is asked about first.
 *
 * "CONTROL usb devices=N" on stdout when the USB devices plugged in change (the suite's), as the USB page said
 * it before the tree moved here. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "control.h"
#include "../shared/pciids.h"

#define DEVS 64
#define DETAILS 4
#define INFO 10

struct dev {
    char id[16], kind[10], state[10], driver[16], why[32], name[48], parent[16], speed[6];
    unsigned vendor, device, cls, rev, subv, subd;
    int pci, usb, listed, can, irq;
    char dma[12], io[72], mem[96];
    char det[DETAILS][100];
    int ndet;
    char label[120];
    struct gui_node *node;
};

static struct dev dv[DEVS];
static int ndv;
static struct gui_window *d_win;
static struct gui_widget *d_tree, *d_info, *d_button, *d_field;
static char d_sig[2048], d_shown[INFO * 120];
static struct pci_name names[40];
static int nnames, names_read, d_usb = -1, d_btn = -1;      /* d_btn: the button as last set (-1: not yet) */

/* The button: Turn off or Turn on, usable or not (set only when that changes: no drawing for nothing). */
static void button(int listed, int can)
{
    int b = (listed ? 2 : 0) | (can ? 1 : 0);
    if (b == d_btn)
        return;
    d_btn = b;
    gui_set_text(d_button, listed ? GUI_T("Turn &on") : GUI_T("Turn &off"));
    gui_enable(d_button, can);
}

/* ---- the list's lines */

static void word(const char *line, const char *key, char *out, size_t room)
{
    size_t k = strlen(key), n = 0;
    const char *p = line;
    out[0] = 0;
    while ((p = strstr(p, key)) != NULL) {
        if ((p == line || p[-1] == ' ') && p[k] == '=') {
            for (p += k + 1; *p && *p != ' ' && n + 1 < room; p++)
                out[n++] = *p;
            out[n] = 0;
            return;
        }
        p += k;
    }
}

static unsigned hex(const char *line, const char *key)
{
    char v[12];
    word(line, key, v, sizeof v);
    return (unsigned)strtoul(v, NULL, 16);
}

static void parse(char *text)
{
    char *p = text, *e;
    struct dev *d = NULL;
    ndv = 0;
    for (; *p; p = e) {
        if ((e = strchr(p, '\n')) != NULL)
            *e++ = 0;
        else
            e = p + strlen(p);
        if (p[0] == ' ' && p[1] == ' ') {       /* a detail of the device before */
            if (d && d->ndet < DETAILS)
                snprintf(d->det[d->ndet++], sizeof d->det[0], "%.99s", p + 2);
            continue;
        }
        if (!p[0] || ndv == DEVS) {
            d = NULL;
            continue;
        }
        d = &dv[ndv++];
        memset(d, 0, sizeof *d);
        sscanf(p, "%15s", d->id);
        word(p, "kind", d->kind, sizeof d->kind);
        word(p, "state", d->state, sizeof d->state);
        word(p, "driver", d->driver, sizeof d->driver);
        word(p, "why", d->why, sizeof d->why);
        word(p, "name", d->name, sizeof d->name);
        word(p, "parent", d->parent, sizeof d->parent);
        word(p, "speed", d->speed, sizeof d->speed);
        word(p, "dma", d->dma, sizeof d->dma);
        word(p, "io", d->io, sizeof d->io);
        word(p, "mem", d->mem, sizeof d->mem);
        {
            char v[8];
            word(p, "listed", v, sizeof v);
            d->listed = v[0] == '1';
            word(p, "can", v, sizeof v);
            d->can = v[0] == '1';
            word(p, "irq", v, sizeof v);
            d->irq = v[0] ? atoi(v) : -1;
            word(p, "sub", v, sizeof v);
            if (v[0])
                sscanf(v, "%x:%x", &d->subv, &d->subd);
        }
        d->vendor = hex(p, "vendor"), d->device = hex(p, "device"), d->cls = hex(p, "class"), d->rev = hex(p, "rev");
        d->pci = isdigit((unsigned char)d->id[0]) && d->id[2] == ':';
        d->usb = !strcmp(d->kind, "usbdev");
        {
            char *c;
            for (c = d->name; *c; c++)          /* (the list's names have _ for spaces) */
                if (*c == '_')
                    *c = ' ';
        }
    }
}

/* ---- names */

static const char *class_name(unsigned cls)
{
    static const struct { unsigned c; const char *n; } t[] = {
        { 0x0101, "IDE controller" }, { 0x0100, "SCSI controller" }, { 0x0102, "Floppy controller" }, { 0x0104, "RAID controller" },
        { 0x0106, "SATA controller" }, { 0x0180, "Storage controller" }, { 0x0200, "Ethernet controller" }, { 0x0280, "Network controller" },
        { 0x0300, "VGA graphics card" }, { 0x0302, "3D controller" }, { 0x0380, "Display controller" }, { 0x0400, "Video device" },
        { 0x0401, "Sound card" }, { 0x0403, "Audio device" }, { 0x0480, "Multimedia device" }, { 0x0600, "Host bridge" },
        { 0x0601, "ISA bridge" }, { 0x0604, "PCI bridge" }, { 0x0607, "CardBus bridge" }, { 0x0680, "Bridge" },
        { 0x0700, "Serial controller" }, { 0x0701, "Parallel port" }, { 0x0703, "Modem" }, { 0x0880, "System peripheral" },
        { 0x0900, "Keyboard controller" }, { 0x0c00, "FireWire controller" }, { 0x0c03, "USB controller" },
        { 0x0c05, "SMBus controller" }, { 0 , 0 } };
    int i;
    for (i = 0; t[i].n; i++)
        if (t[i].c == cls >> 8)
            return GUI_T(t[i].n);
    for (i = 0; t[i].n; i++)
        if ((t[i].c >> 8) == cls >> 16 && (t[i].c & 0xFF) == 0x80)
            return GUI_T(t[i].n);
    return GUI_T("Device");
}

/* A vendor's short name: what its list name has in brackets ("[AMD/ATI]"), else its first word ("Intel"). */
static void short_vendor(const char *v, char *out, size_t room)
{
    const char *b = strchr(v, '['), *e;
    size_t n;
    if (b && (e = strchr(b, ']')) != NULL && e > b + 1)
        n = (size_t)(e - b - 1), v = b + 1;
    else
        for (n = 0; v[n] && v[n] != ' ' && v[n] != ','; n++)
            ;
    if (n >= room)
        n = room - 1;
    memcpy(out, v, n);
    out[n] = 0;
}

static const struct pci_name *name_of(const struct dev *d)
{
    int i;
    for (i = 0; i < nnames; i++)
        if (names[i].vendor == d->vendor && names[i].device == d->device)
            return &names[i];
    return NULL;
}

/* The PCI devices' names, in one pass through the list, the first time and when a new device comes. */
static void find_names(void)
{
    int i, k, more = 0;
    for (i = 0; i < ndv; i++)
        if (dv[i].pci && !name_of(&dv[i]) && nnames < (int)(sizeof names / sizeof names[0])) {
            names[nnames].vendor = (unsigned short)dv[i].vendor, names[nnames].device = (unsigned short)dv[i].device;
            nnames++, more = 1;
        }
    if (!more && names_read)
        return;
    k = pciids_lookup_file(gui_open_beside("PCIIDS.DAT"), names, nnames);
    names_read = 1;
    printf("CONTROL devices names=%d of %d\n", k, nnames);
    fflush(stdout);
}

static const char *usb_kind(const char *is)
{
    return !strcmp(is, "hub") ? GUI_T("USB hub") : !strcmp(is, "hid-keyboard") || !strcmp(is, "keyboard") ? GUI_T("USB keyboard")
         : !strcmp(is, "hid-mouse") || !strcmp(is, "mouse") ? GUI_T("USB mouse")
         : !strcmp(is, "xbox360") || !strcmp(is, "xbox-gip") || !strcmp(is, "hid") ? GUI_T("USB gamepad")
         : GUI_T("USB device");
}

static void label_of(struct dev *d)
{
    char sv[40];
    const struct pci_name *n;
    if (d->pci) {
        n = name_of(d);
        if (n && n->device_name[0]) {
            short_vendor(n->vendor_name, sv, sizeof sv);
            if (!strncmp(n->device_name, sv, strlen(sv)))
                snprintf(d->label, sizeof d->label, "%s", n->device_name);
            else
                snprintf(d->label, sizeof d->label, "%s %s", sv, n->device_name);
        } else if (n && n->vendor_name[0]) {
            short_vendor(n->vendor_name, sv, sizeof sv);
            snprintf(d->label, sizeof d->label, "%s %s %04x", sv, class_name(d->cls), d->device);
        } else {
            snprintf(d->label, sizeof d->label, "%s %04x:%04x", class_name(d->cls), d->vendor, d->device);
        }
    } else if (d->usb) {
        snprintf(d->label, sizeof d->label, "%s %04x:%04x", usb_kind(d->driver), d->vendor, d->device);
    } else if (!strncmp(d->id, "com", 3)) {
        snprintf(d->label, sizeof d->label, GUI_T("Serial port (%s)"), d->name);
    } else if (!strncmp(d->id, "lpt", 3)) {
        snprintf(d->label, sizeof d->label, GUI_T("Printer port (%s)"), d->name);
    } else if (!strcmp(d->id, "sb16")) {
        snprintf(d->label, sizeof d->label, "%s", GUI_T("Sound Blaster 16"));
    } else if (!strcmp(d->id, "crystal")) {
        snprintf(d->label, sizeof d->label, "%s", GUI_T("Crystal CS423x sound chip"));
    } else if (!strcmp(d->id, "ne2000")) {
        snprintf(d->label, sizeof d->label, "%s", GUI_T("NE2000 network card (ISA)"));
    } else {
        snprintf(d->label, sizeof d->label, "%s", d->name[0] ? GUI_T(d->name) : d->id);
    }
}

/* ---- what is said of the chosen one */

static const char *driver_name(const char *drv)
{
    static const struct { const char *d, *n; } t[] = {
        { "rtl8139", "Realtek RTL8139" }, { "rtl8029", "RTL8029 (NE2000)" }, { "ne2000", "NE2000" }, { "i8255x", "Intel 8255x" },
        { "rhine", "VIA Rhine" }, { "uhci", "USB (UHCI)" }, { "sb16", "Sound Blaster 16" }, { "crystal", "Crystal" },
        { "mga", "Matrox engine" }, { "vbe", "VESA BIOS" }, { "8042", "keyboard and mouse" }, { "uart", "serial line" },
        { "plip", "parallel cable" }, { "glos", "own" }, { "hid-keyboard", "USB keyboard" }, { "hid-mouse", "USB mouse" },
        { "hid", "USB gamepad" }, { "xbox360", "Xbox 360 pad" }, { "xbox-gip", "Xbox One pad" }, { "hub", "USB hub" },
        { 0, 0 } };
    int i;
    for (i = 0; t[i].d; i++)
        if (!strcmp(t[i].d, drv))
            return GUI_T(t[i].n);
    return drv;
}

static void status_lines(const struct dev *d, char *a, char *b, size_t room)
{
    b[0] = 0;
    if (!strcmp(d->state, "on")) {
        if (!strcmp(d->driver, "vbe"))
            snprintf(a, room, "%s", GUI_T("In use: GLOS draws on it through its VESA BIOS."));
        else
            snprintf(a, room, GUI_T("Working: GLOS's %s driver has it."), driver_name(d->driver));
    } else if (!strcmp(d->state, "off")) {
        if (!strcmp(d->why, "net-setting"))
            snprintf(a, room, "%s", GUI_T("Off: the Network page's card setting is off."));
        else if (!strcmp(d->why, "usb-setting"))
            snprintf(a, room, "%s", GUI_T("Off: the USB page's setting is off."));
        else if (!strcmp(d->why, "packet-driver"))
            snprintf(a, room, "%s", GUI_T("Left to a DOS packet driver, which was there first."));
        else
            snprintf(a, room, "%s", GUI_T("Turned off: GLOS leaves it to DOS."));
    } else if (!strcmp(d->state, "refused")) {
        snprintf(a, room, GUI_T("Not working: GLOS's %s driver can't use it:"), driver_name(d->driver));
        if (!strcmp(d->why, "no-irq"))
            snprintf(b, room, "%s", GUI_T("it has no interrupt line."));
        else if (!strncmp(d->why, "shared-irq", 10))
            snprintf(b, room, GUI_T("its interrupt line is shared with %s."), d->why[10] ? d->why + 11 : "another device");
        else if (!strcmp(d->why, "not-sb16"))
            snprintf(b, room, "%s", GUI_T("it is an older Sound Blaster."));
        else if (!strcmp(d->why, "no-chip"))
            snprintf(b, room, "%s", GUI_T("nothing answered where the setting says it is."));
        else if (!strcmp(d->why, "conflict"))
            snprintf(b, room, "%s", GUI_T("another device has its ports or line."));
        else
            snprintf(b, room, "%s.", d->why);
    } else if (!strcmp(d->state, "quiet")) {
        snprintf(a, room, "%s", GUI_T("Kept quiet: no driver; GLOS keeps its interrupts off."));
    } else if (!strcmp(d->state, "none")) {
        snprintf(a, room, "%s", GUI_T("No driver: GLOS has none for this kind yet."));
    } else {
        snprintf(a, room, "%s", GUI_T("DOS's: GLOS has no driver of its own for it."));
    }
    if (d->listed && strcmp(d->state, "off"))
        snprintf(b, room, "%s", GUI_T("It is turned off when GLOS starts again."));
    else if (d->can && !d->listed && !strcmp(d->state, "off") && !d->why[0])
        snprintf(b, room, "%s", GUI_T("It is turned on when GLOS starts again."));
}

/* The chosen one's lines, in the list under the tree (made again only when they change). */
static void show(char t[][120], int n)
{
    char all[sizeof d_shown];
    size_t m = 0;
    int i;
    for (i = 0; i < n && t[i][0]; i++)
        m += (size_t)snprintf(all + m, sizeof all - m, "%s\n", t[i]);
    all[m < sizeof all ? m : sizeof all - 1] = 0;
    if (!strcmp(all, d_shown))
        return;
    strcpy(d_shown, all);
    gui_list_clear(d_info);
    for (i = 0; i < n && t[i][0]; i++) {
        const char *cells[1] = { t[i] };
        gui_list_add(d_info, cells, NULL, NULL);
    }
}

/* Is it named in the field now (not yet applied, perhaps)? */
static int in_field(const char *id)
{
    const char *p = gui_get_text(d_field);
    size_t n = strlen(id);
    while (*p) {
        while (*p == ' ' || *p == ',')
            p++;
        if (!strncasecmp(p, id, n) && (!p[n] || p[n] == ' ' || p[n] == ','))
            return 1;
        while (*p && *p != ' ' && *p != ',')
            p++;
    }
    return 0;
}

static struct dev *chosen(void)
{
    struct gui_node *n = d_tree ? gui_tree_get(d_tree) : NULL;
    long k = n ? (long)gui_node_user(n) : 0;
    return k > 0 && k <= ndv ? &dv[k - 1] : NULL;
}

static void info(void)
{
    struct dev *d = chosen();
    char t[INFO][120], a[120], b[120];
    const struct pci_name *n;
    int i, k = 0;
    for (i = 0; i < INFO; i++)
        t[i][0] = 0;
    if (d) {
        struct dev shown = *d;
        shown.listed = in_field(d->id);         /* (as the field says now: Turn off pressed, not yet applied) */
        n = d->pci ? name_of(d) : NULL;
        if (n && n->vendor_name[0])
            snprintf(t[k++], sizeof t[0], "%.40s, %.70s", n->vendor_name, n->device_name[0] ? n->device_name : class_name(d->cls));
        else
            snprintf(t[k++], sizeof t[0], "%s", d->label);
        if (d->pci)
            snprintf(t[k++], sizeof t[0], GUI_T("PCI %s, %s"), d->id, class_name(d->cls));
        else if (d->usb)
            snprintf(t[k++], sizeof t[0], GUI_T("USB port %s, %s speed, under the controller %s"), strchr(d->id, ':') + 1,
                     !strcmp(d->speed, "low") ? GUI_T("low") : GUI_T("full"), d->parent);
        else
            snprintf(t[k++], sizeof t[0], GUI_T("Built into the PC, or an ISA card (%s)"), d->id);
        status_lines(&shown, a, b, sizeof a);
        snprintf(t[k++], sizeof t[0], "%s", a);
        if (b[0])
            snprintf(t[k++], sizeof t[0], "%s", b);
        {
            size_t m = 0;
            char *r = t[k];
            if (d->irq >= 0)
                m += (size_t)snprintf(r + m, sizeof t[0] - m, GUI_T("IRQ %d"), d->irq);
            if (d->dma[0] && m < sizeof t[0])
                m += (size_t)snprintf(r + m, sizeof t[0] - m, GUI_T("%sDMA %s"), m ? ", " : "", d->dma);
            if (d->io[0] && m < sizeof t[0])
                m += (size_t)snprintf(r + m, sizeof t[0] - m, GUI_T("%sI/O %s"), m ? ", " : "", d->io);
            if (d->mem[0] && m < sizeof t[0])
                m += (size_t)snprintf(r + m, sizeof t[0] - m, GUI_T("%smemory %s"), m ? ", " : "", d->mem);
            if (m)
                k++;
        }
        if (d->pci || d->usb)
            snprintf(t[k++], sizeof t[0], GUI_T("ID %04x:%04x, class %06x, revision %02x%s"), d->vendor, d->device, d->cls,
                     d->rev, d->subv ? GUI_T(", board's ID given") : "");
        for (i = 0; i < d->ndet && k < INFO; i++)
            snprintf(t[k++], sizeof t[0], "%s", d->det[i]);
        button(shown.listed, d->can);
    } else {
        snprintf(t[0], sizeof t[0], "%s", GUI_T("Choose a device to see what serves it."));
        button(0, 0);
    }
    show(t, INFO);
}

/* ---- the tree */

static const struct { const char *kind, *title, *icon; } groups[] = {
    { "display", "Display", "display" }, { "net", "Network", "network" }, { "sound", "Sound", "sound" },
    { "usb", "USB", "computer" }, { "storage", "Storage", "drive" }, { "port", "Ports", "printer" },
    { "input", "Keyboard and mouse", "keyboard" }, { "virtual", "GLOS's own, for DOS programs", "computer" },
    { "bridge", "Bridges", "settings" }, { "system", "System", "settings" }, { "other", "Other", "question" }, { 0, 0, 0 } };

static void build_tree(void)
{
    struct gui_node *g;
    char keep[16];
    struct dev *was = chosen();
    int i, k, j;
    snprintf(keep, sizeof keep, "%s", was ? was->id : "");
    gui_tree_clear(d_tree, NULL);
    for (i = 0; i < ndv; i++)
        dv[i].node = NULL;
    for (k = 0; groups[k].kind; k++) {
        int any = 0;
        for (i = 0; i < ndv; i++)
            any |= !strcmp(dv[i].kind, groups[k].kind);
        if (!any)
            continue;
        g = gui_tree_add(d_tree, NULL, GUI_T(groups[k].title), gui_icon(groups[k].icon, 0), NULL, 1);
        for (i = 0; i < ndv; i++) {
            if (strcmp(dv[i].kind, groups[k].kind))
                continue;
            dv[i].node = gui_tree_add(d_tree, g, dv[i].label, NULL, (void *)(long)(i + 1), 0);
            if (!strcmp(dv[i].kind, "usb"))     /* (a controller's USB devices hang under it) */
                for (j = 0; j < ndv; j++)
                    if (dv[j].usb && !strcmp(dv[j].parent, dv[i].id))
                        dv[j].node = gui_tree_add(d_tree, dv[i].node, dv[j].label, NULL, (void *)(long)(j + 1), 0);
        }
        gui_tree_expand(d_tree, g, 1);
    }
    for (i = 0; i < ndv; i++) {
        if (!strcmp(dv[i].kind, "usb") && dv[i].node)
            gui_tree_expand(d_tree, dv[i].node, 1);
        if (keep[0] && !strcmp(dv[i].id, keep) && dv[i].node)
            gui_tree_set(d_tree, dv[i].node);
    }
}

void devices_poll(void)
{
    static char text[12288];
    static unsigned last;
    char sig[sizeof d_sig];
    unsigned now = glos_ticks(), length = 0, count = 0;
    size_t n = 0;
    int i, usb = 0;
    if (!d_tree)
        return;
    if (!last || now - last >= 2000) {
        last = now ? now : 1;
        if (glos_sys_stat(GLOS_STAT_DEVICES, text, sizeof text - 1, &length, &count) != 0 && glos_error() != GLOS_E_ROOM)
            length = 0;
        text[length < sizeof text - 1 ? length : sizeof text - 1] = 0;
        parse(text);
        find_names();
        sig[0] = 0;
        for (i = 0; i < ndv && n + 48 < sizeof sig; i++) {
            label_of(&dv[i]);
            n += (size_t)snprintf(sig + n, sizeof sig - n, "%s:%s:%s:%d;", dv[i].id, dv[i].kind, dv[i].label, dv[i].pci);
            usb += dv[i].usb;
        }
        if (strcmp(sig, d_sig)) {               /* what is there changed: the tree again */
            strcpy(d_sig, sig);
            build_tree();
            printf("CONTROL devices=%d\n", ndv);
            fflush(stdout);
        } else {                                /* (the same devices: each node's place in dv stays) */
            for (i = 0; i < ndv; i++)
                label_of(&dv[i]);
        }
        if (usb != d_usb) {
            d_usb = usb;
            printf("CONTROL usb devices=%d\n", usb);
            fflush(stdout);
        }
    }
    info();
}

/* ---- Turn off, Turn on */

static void toggle(struct gui_widget *b, int what)
{
    struct dev *d = chosen();
    char line[256], one[32];
    const char *p;
    size_t m = 0;
    (void)b, (void)what;
    if (!d || !d->can)
        return;
    if (!in_field(d->id)) {
        const char *ask = !strcmp(d->kind, "net") && !strcmp(d->state, "on")
            ? GUI_T("GLOS will have no network through this card after it next starts: no remote session (SSH, "
                    "Remote Desktop) will reach this PC until it is turned on again here. Turn it off?")
            : !strcmp(d->kind, "usb") && !strcmp(d->state, "on")
            ? GUI_T("USB keyboards, mice and pads plugged into it won't work under GLOS after it next starts. Turn it off?")
            : NULL;
        if (ask && gui_message(d_win, GUI_T("Turn Off a Device"), ask, GUI_MB_YESNO) != GUI_YES) {
            printf("CONTROL devices kept %s\n", d->id);
            fflush(stdout);
            return;
        }
        snprintf(line, sizeof line, "%s%s%s", gui_get_text(d_field), gui_get_text(d_field)[0] ? " " : "", d->id);
    } else {                                    /* (every word but its own, in their order) */
        line[0] = 0;
        for (p = gui_get_text(d_field); *p; ) {
            size_t k = 0;
            while (*p == ' ' || *p == ',')
                p++;
            while (*p && *p != ' ' && *p != ',' && k + 1 < sizeof one)
                one[k++] = *p++;
            while (*p && *p != ' ' && *p != ',')
                p++;
            one[k] = 0;
            if (k && strcasecmp(one, d->id) && m + k + 2 < sizeof line)
                m += (size_t)snprintf(line + m, sizeof line - m, "%s%s", m ? " " : "", one);
        }
    }
    gui_set_text(d_field, line);
    printf("CONTROL devices off=%s\n", line);
    fflush(stdout);
    info();
}

/* The Devices page: its setting's field (made by control.c), then the tree, the chosen one, and the button. */
void devices_add(struct gui_window *win, struct gui_widget *page, struct gui_widget *field, const char *argv0)
{
    struct gui_widget *row;
    (void)argv0;                                /* (PCIIDS.DAT is found beside the program: gui_open_beside) */
    d_win = win;
    d_field = field;
    d_tree = gui_tree(page);
    gui_grow(d_tree, 1);
    d_info = gui_list(page, 1, NULL, NULL);     /* (its lines: read and chosen, never typed into; six in sight) */
    gui_size(d_info, 0, 6 * 16 + 4);
    row = gui_hbox(page, 6, 0);
    gui_space(row, 0, 0);
    d_button = gui_button(row, GUI_T("Turn &off"), toggle, 1);
    gui_enable(d_button, 0);
}
