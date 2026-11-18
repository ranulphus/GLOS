/* CONTROL: how things are now, under a page's settings (phase D, docs/v1/phase-d.md D5).
 *
 * The Network page: each interface's card and hardware address; its address, mask and where it came from (DHCP,
 * with what is left of its lease); the gateway and the name servers; GLOS's name; the frames received and sent.
 * Read with NET's STATE once a second while the Control Panel is open.
 *
 * The Joystick page: the pads as they are (PADS, ten times a second): for each of the two joysticks a DOS program
 * sees, its stick's place in a square (and the right stick's, if the pad has one) and its first ten buttons,
 * lit while they are down. A pad's test before a game is started.
 *
 * The Input page: a field to try the keys' repeat in, and a place that counts the double clicks made on it (by
 * the double-click time this program started with: the setting is an app's at its start).
 *
 * The USB page: GLOS's USB controllers and what is plugged into them, as `glos usb` says it (SYS's STAT), read
 * every two seconds: a pad or a keyboard plugged in is there a moment later.
 *
 * The Sound page: Test sound plays two notes through the mixer at the volume that is set, and says so, or that
 * there is no card GLOS can play through. */
#include <stdio.h>
#include <string.h>

#include "control.h"

#define LINES 4

static struct gui_widget *net[LINES];
static char net_seen[LINES][96];

static const char *ip(unsigned a, char *buf)
{
    sprintf(buf, "%u.%u.%u.%u", a & 255, a >> 8 & 255, a >> 16 & 255, a >> 24);
    return buf;
}

static void put(int i, const char *text)
{
    if (strcmp(net_seen[i], text)) {            /* (set only when it is another: nothing drawn for the same) */
        snprintf(net_seen[i], sizeof net_seen[i], "%s", text);
        gui_set_text(net[i], text);
    }
}

/* ---- the pads */

#define PAD_W 220
#define PAD_H 44

static struct gui_widget *pads_view;
static struct glos_pads pads;

static void stick(struct gfx_surface *s, int x, int y, int ax, int ay)
{
    gfx_fill(s, x, y, 40, 40, GUI_ROLE(GLOS_ROLE_FIELD));
    gfx_frame(s, x, y, 40, 40, GUI_ROLE(GLOS_ROLE_LINE));
    gfx_fill(s, x + 18 + ax * 17 / 127, y + 18 + ay * 17 / 127, 4, 4, GUI_ROLE(GLOS_ROLE_MARK));
}

static void pads_draw(struct gui_widget *w, struct gfx_surface *s)
{
    int n, b;
    for (n = 0; n < GLOS_PADS; n++) {
        const struct glos_pad *p = &pads.pad[n];
        int x = w->r.x + n * (PAD_W + 10), y = w->r.y;
        gui_draw_text(s, x, y + 14, gui_font, n ? GUI_T("B:") : GUI_T("A:"), GUI_ROLE(GLOS_ROLE_FACE_TEXT));
        if (!p->present) {
            gui_draw_text(s, x + 20, y + 14, gui_font, GUI_T("no pad"), GUI_ROLE(GLOS_ROLE_DIM));
            continue;
        }
        stick(s, x + 20, y, p->lx, p->ly);
        if (p->flags & GLOS_PAD_RIGHT)
            stick(s, x + 66, y, p->rx, p->ry);
        for (b = 0; b < 10; b++) {              /* (two rows of five) */
            int bx = x + 114 + b % 5 * 16, by = y + 4 + b / 5 * 18;
            gfx_fill(s, bx, by, 12, 12, GUI_ROLE(p->buttons >> b & 1 ? GLOS_ROLE_MARK : GLOS_ROLE_FIELD));
            gfx_frame(s, bx, by, 12, 12, GUI_ROLE(GLOS_ROLE_LINE));
        }
    }
}

static void pads_poll(void)
{
    struct glos_pads now;
    if (!pads_view)
        return;
    memset(&now, 0, sizeof now);
    now.size = sizeof now;
    if (glos_pads(&now, 0) != 0)
        memset(&now, 0, sizeof now);
    now.size = pads.size, now.flags = pads.flags;       /* (what is compared: the pads alone) */
    if (memcmp(&now, &pads, sizeof now)) {
        pads = now;
        gui_damage(pads_view);
    }
}

/* The Joystick page's picture of the pads, under its settings. */
void status_add_pads(struct gui_widget *page)
{
    gui_label(page, GUI_T("The pads now (move a stick, press a button):"));
    pads_view = gui_canvas(page, 2 * PAD_W + 10, PAD_H, pads_draw, NULL);
}

/* ---- the keys and the mouse, tried */

static int doubles;

static void try_draw(struct gui_widget *w, struct gfx_surface *s)
{
    char t[40];
    if (doubles)
        snprintf(t, sizeof t, GUI_T("Double clicks: %d"), doubles);
    else
        snprintf(t, sizeof t, "%s", GUI_T("Double-click here"));
    gfx_fill(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_ROLE(GLOS_ROLE_FIELD));
    gfx_frame(s, w->r.x, w->r.y, w->r.w, w->r.h, GUI_ROLE(GLOS_ROLE_LINE));
    gui_draw_text(s, w->r.x + 8, w->r.y + (w->r.h - gui_font->height) / 2, gui_font, t, GUI_ROLE(GLOS_ROLE_FIELD_TEXT));
}

static int try_event(struct gui_widget *w, struct gui_ev *e)
{
    if (e->type != GUI_EV_PRESS)
        return 0;
    if (e->clicks == 2) {
        doubles++;
        gui_damage(w);
    }
    return 1;
}

/* The Input page's places to try the keys and the mouse, under its settings. */
void status_add_input(struct gui_widget *page)
{
    struct gui_widget *row;
    gui_label(page, GUI_T("Try the keys and the mouse as they are now:"));
    row = gui_hbox(page, 10, 0);
    gui_field(row, 22);
    gui_canvas(row, 170, gui_font->height + 10, try_draw, try_event);
}

/* ---- a sound, to hear that there is one */

static struct gui_widget *sound_says;
static glos_handle sound;
static unsigned sound_until;

/* A sine's value (-32767 to 32767) at phase p of 65536, by a parabola a side: near enough for two notes. */
static int wave(unsigned p)
{
    int x = (int)(p & 32767) - 16384;           /* -16384 to 16383 over half a turn */
    int y = 32767 - (int)((long)x * x / 8192);
    return p & 32768 ? -y : y;
}

static void sound_test(struct gui_widget *b, int what)
{
    static short note[2 * 5512];                /* half a second at 11025 Hz: two notes */
    struct glos_audio_ring *ring = NULL;
    unsigned i, phase = 0;
    (void)b, (void)what;
    if (sound)
        return;                                 /* (the last one is still playing) */
    if (!(sound = glos_audio_out(11025, 1, 32768, &ring, NULL)) || !ring) {
        gui_set_text(sound_says, glos_error() == GLOS_E_NO_AUDIO ? GUI_T("No sound card that GLOS can play through.")
                                                                 : GUI_T("GLOS couldn't play it."));
        if (sound)
            glos_close(sound);
        sound = 0;
        return;
    }
    for (i = 0; i < 2 * 5512; i++) {            /* 660 Hz, then 880 Hz, each fading out */
        unsigned k = i % 5512;
        phase += (i < 5512 ? 660u : 880u) * 65536u / 11025u;
        note[i] = (short)(wave(phase) / 4 * (int)(5512 - k) / 5512);
    }
    glos_audio_write(ring, note, sizeof note);
    sound_until = glos_ticks() + 1500;
    gui_set_text(sound_says, GUI_T("Two notes, at the volume that is set."));
}

static void sound_poll(void)
{
    if (sound && (int)(glos_ticks() - sound_until) >= 0) {      /* played out: its stream closed */
        glos_close(sound);
        sound = 0;
    }
}

/* The Sound page's test, under its settings. */
void status_add_sound(struct gui_widget *page)
{
    struct gui_widget *row = gui_hbox(page, 10, 0);
    gui_button(row, GUI_T("&Test sound"), sound_test, 0);
    sound_says = gui_label(row, "");
    gui_grow(sound_says, 1);
}

/* ---- USB's controllers and devices */

#define USB_LINES 8

static struct gui_widget *usb[USB_LINES];
static char usb_seen[USB_LINES][100];

static void usb_poll(void)
{
    static char text[1600];
    static unsigned last;
    unsigned now = glos_ticks(), length = 0, count = 0;
    char *p = text, *e;
    int i;
    if (!usb[0] || (last && now - last < 2000))
        return;
    last = now ? now : 1;
    if (glos_sys_stat(GLOS_STAT_USB, text, sizeof text - 1, &length, &count) != 0 && glos_error() != GLOS_E_ROOM)
        length = 0;
    text[length < sizeof text - 1 ? length : sizeof text - 1] = 0;
    for (i = 0; i < USB_LINES; i++, p = e) {
        char line[100];
        if ((e = strchr(p, '\n')) != NULL)
            *e++ = 0;
        else
            e = p + strlen(p);
        snprintf(line, sizeof line, "%s", p);
        if (strlen(line) && line[strlen(line) - 1] == '\r')
            line[strlen(line) - 1] = 0;
        if (strcmp(usb_seen[i], line)) {
            strcpy(usb_seen[i], line);
            gui_set_text(usb[i], line);
        }
    }
}

/* The USB page's lines, under its settings. */
void status_add_usb(struct gui_widget *page)
{
    int i;
    gui_label(page, GUI_T("Controllers and devices now:"));
    for (i = 0; i < USB_LINES; i++)
        usb[i] = gui_label(page, "");
}

/* ---- the network */

void status_poll(void)
{
    static struct glos_net_if nif[4];
    static unsigned last;
    char t[LINES][96], a[16], b[16], c[16];
    unsigned count = 0, now = glos_ticks(), i;
    const struct glos_net_if *n = &nif[0];
    pads_poll();
    sound_poll();
    usb_poll();
    if (!net[0] || (last && now - last < 1000))
        return;
    last = now ? now : 1;
    memset(nif, 0, sizeof nif);
    for (i = 0; i < 4; i++)
        nif[i].size = sizeof nif[i];
    for (i = 0; i < LINES; i++)
        t[i][0] = 0;
    if (glos_net_state(nif, 4, &count) != 0 || !count) {
        snprintf(t[0], sizeof t[0], "%s", GUI_T("No network card or cable link that GLOS has a driver for."));
    } else {
        for (i = 0; i < count && i < 4; i++)    /* the one with an address, if any has */
            if (nif[i].flags & GLOS_NETIF_UP) {
                n = &nif[i];
                break;
            }
        snprintf(t[0], sizeof t[0], n->kind == GLOS_NETIF_ETHERNET ? GUI_T("Card: %s, %02X:%02X:%02X:%02X:%02X:%02X")
                 : GUI_T("Cable link: %s"), n->card, n->mac[0], n->mac[1], n->mac[2], n->mac[3], n->mac[4], n->mac[5]);
        if (!(n->flags & GLOS_NETIF_UP))
            snprintf(t[1], sizeof t[1], "%s", GUI_T("Address: none yet"));
        else if ((n->flags & GLOS_NETIF_DHCP) && n->lease)
            snprintf(t[1], sizeof t[1], GUI_T("Address: %s, mask %s (DHCP: %u h %02u min left)"), ip(n->address, a), ip(n->mask, b),
                     n->lease_left / 3600, n->lease_left / 60 % 60);
        else
            snprintf(t[1], sizeof t[1], GUI_T("Address: %s, mask %s%s"), ip(n->address, a), ip(n->mask, b),
                     n->flags & GLOS_NETIF_DHCP ? GUI_T(" (DHCP)") : "");
        if (n->dns[1])
            snprintf(t[2], sizeof t[2], GUI_T("Gateway: %s   Name servers: %s, %s"), ip(n->gateway, a), ip(n->dns[0], b), ip(n->dns[1], c));
        else
            snprintf(t[2], sizeof t[2], GUI_T("Gateway: %s   Name server: %s"), ip(n->gateway, a), n->dns[0] ? ip(n->dns[0], b) : GUI_T("none"));
        snprintf(t[3], sizeof t[3], GUI_T("Name: %s.local   Frames: %u in, %u out, %u bad"), n->host, n->rx_frames, n->tx_frames,
                 n->rx_errors + n->overruns);
    }
    for (i = 0; i < LINES; i++)
        put((int)i, t[i]);
}

/* The Network page's lines, under its settings. */
void status_add_net(struct gui_widget *page)
{
    int i;
    gui_label(page, GUI_T("Now:"));
    for (i = 0; i < LINES; i++)
        net[i] = gui_label(page, "");
}
