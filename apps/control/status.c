/* CONTROL: how things are now, under a page's settings (phase D, docs/v1/phase-d.md D5).
 *
 * The Network page: each interface's card and hardware address; its address, mask and where it came from (DHCP,
 * with what is left of its lease); the gateway and the name servers; GLOS's name; the frames received and sent.
 * Read with NET's STATE once a second while the Control Panel is open. */
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

void status_poll(void)
{
    static struct glos_net_if nif[4];
    static unsigned last;
    char t[LINES][96], a[16], b[16], c[16];
    unsigned count = 0, now = glos_ticks(), i;
    const struct glos_net_if *n = &nif[0];
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
