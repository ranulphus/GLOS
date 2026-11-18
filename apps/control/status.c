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
 * the double-click time this program started with: the setting is an app's at its start). */
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

/* ---- the network */

void status_poll(void)
{
    static struct glos_net_if nif[4];
    static unsigned last;
    char t[LINES][96], a[16], b[16], c[16];
    unsigned count = 0, now = glos_ticks(), i;
    const struct glos_net_if *n = &nif[0];
    pads_poll();
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
