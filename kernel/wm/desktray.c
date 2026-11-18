/* GLOS's own tray icons and notifications (phase D, docs/v1/phase-d.md D2): the volume, the network, the pads, a
 * remote session; and what GLOS itself has to say (a pad plugged in or out, the network up or down, an app that
 * isn't answering). Kernel-drawn, like the bar they are on, so that they answer whatever the apps are doing.
 *
 * Each turn of the desktop (four times a second at most) the icons are made to match what they stand for: there
 * or not, their picture, their tooltip. Their pictures are one-bit shapes in the bar's text colour, made again
 * when the theme changes. A press is theirs (deskshell.c):
 *   the volume    a press: its slider (deskpop.c) and mute; the wheel over it: up and down; the right button:
 *                 mute; twice: the Control Panel's Sound page. Kept in [sound] volume and mute once the slider
 *                 goes (after the desktop's lock is let go: desktray_keep).
 *   the network   a press: its address, gateway and counts, and Network settings; twice: the Network page.
 *   the pads      a press: which joystick each is, and Game controllers; twice: the Joystick page.
 *   a remote session   a press: who is looking (PRD D35's note, as an icon). */
#include <string.h>

#include "app.h"
#include "deskint.h"
#include "gfx.h"
#include "joy.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mixer.h"
#include "nic.h"
#include "popmenu.h"
#include "theme.h"
#include "timer.h"
#include "trayart.h"

static u32 id_vol, id_net, id_pad, id_rem;
static u32 last_check, ink;
static u8 started;                      /* the first look is over: what was there at the start is no news */
static u8 keep;                         /* the volume is to be kept in GLOS.CFG (desktray_keep) */
static u32 seen_v = ~0u, seen_pads;
static int seen_mute = -1, seen_net = -1;       /* seen_net: 0 no card, 1 a card with no address, 2 an address */
static char seen_ip[20], seen_note[16];
static u32 hung_told[8];

/* The icon `*id` there (with that picture and tooltip) or not. */
static void icon(struct wm *m, u32 *id, int there, const char *const *art, const char *tip)
{
    u32 px[256];
    if (!there) {
        if (*id)
            deskshell_own_remove(m, *id);
        *id = 0;
        return;
    }
    trayart_picture(px, art, ink);
    if (!*id)
        *id = deskshell_own_add(m, px, tip);
    else
        deskshell_own_set(m, *id, px, tip);
}

/* key=value in glos net's lines: the value. */
static int field(const char *text, const char *key, char *out, u32 room)
{
    u32 kn = (u32)strlen(key), n = 0;
    const char *p;
    for (p = text; (p = strstr(p, key)) != 0; p += kn)
        if ((p == text || p[-1] == ' ' || p[-1] == '\n') && p[kn] == '=') {
            for (p += kn + 1; *p && *p != ' ' && *p != '\n' && n < room - 1; p++)
                out[n++] = *p;
            out[n] = 0;
            return 1;
        }
    out[0] = 0;
    return 0;
}

static void vol_get(u32 *v, int *mute) { mixer_master_get(v, mute); }

static void vol_set(u32 v, int mute)
{
    mixer_master_set(v, mute);
    last_check = 0;                             /* (the icon follows at the next turn) */
    desk_kick();
}

static void vol_done(void) { keep = 1; }

void desktray_keep(void)
{
    char t[8], why[64];
    u32 v;
    int mute;
    if (!keep)
        return;
    keep = 0;
    mixer_master_get(&v, &mute);
    ksnprintf(t, sizeof t, "%u", (v * 100 + 128) / 256);
    kcfg_set("sound", "volume", t, why, sizeof why);
    kcfg_set("sound", "mute", mute ? "on" : "off", why, sizeof why);
}

static u32 pads_now(void)
{
    struct joy_pad p;
    u32 mask = 0;
    int n;
    for (n = 0; n < JOY_PADS; n++)
        if (joy_pad_get(n, &p))
            mask |= 1u << n;
    return mask;
}

void desktray_turn(struct wm *m, int screen, int again)
{
    struct theme_look c;
    struct wm_window *w;
    char buf[320], ip[20], tip[64];
    const char *note = desk_note_text();
    u32 now = timer_ticks(), v, pads, i, k;
    int mute, net;
    if (!screen)
        return;
    theme_look(desk_theme(), &c);
    if (again || ink != c.bar_text) {           /* another theme: every picture again */
        ink = c.bar_text;
        seen_v = ~0u, seen_mute = -1, seen_net = -1, seen_pads = ~0u, seen_note[0] = 1;
        last_check = 0;
    }
    if (last_check && now - last_check < 256)
        return;
    last_check = now ? now : 1;

    /* The volume: there while a card plays. */
    mixer_master_get(&v, &mute);
    if (!mixer_card()) {
        icon(m, &id_vol, 0, 0, 0);
    } else if (!id_vol || v != seen_v || mute != seen_mute) {
        ksnprintf(tip, sizeof tip, mute ? "Volume: %u%%, muted" : "Volume: %u%%", (v * 100 + 128) / 256);
        icon(m, &id_vol, 1, mute || !v ? art_mute : v < 96 ? art_low : art_vol, tip);
        seen_v = v, seen_mute = mute;
        deskvol_again(m);
    }

    /* The network: there with a card (or a cable link); crossed out without an address. */
    net_stat(buf, sizeof buf, 0);
    net = !field(buf, "ip", ip, sizeof ip) ? 0 : !strcmp(ip, "0.0.0.0") ? 1 : 2;
    if (net != seen_net || strcmp(ip, seen_ip)) {
        if (net == 2)
            ksnprintf(tip, sizeof tip, "Network: %s", ip);
        else
            ksnprintf(tip, sizeof tip, "Network: no address");
        icon(m, &id_net, net != 0, net == 2 ? art_net : art_nonet, tip);
        if (started && net == 2 && seen_net != 2) {
            ksnprintf(buf, sizeof buf, "Connected: %s", ip);
            deskshell_note("Network", buf);
        } else if (started && net != 2 && seen_net == 2) {
            deskshell_note("Network", "The network is down.");
        }
        seen_net = net;
        strcpy(seen_ip, ip);
    }

    /* The pads: there while one is plugged in. */
    pads = pads_now();
    if (pads != seen_pads) {
        ksnprintf(tip, sizeof tip, pads == 3 ? "Pads: joysticks A and B" : pads == 2 ? "Pad: joystick B" : "Pad: joystick A");
        icon(m, &id_pad, pads != 0, art_pad, tip);
        for (i = 0; started && seen_pads != ~0u && i < JOY_PADS; i++)
            if ((pads ^ seen_pads) & (1u << i)) {
                ksnprintf(buf, sizeof buf, pads & (1u << i) ? "A pad was plugged in: joystick %c." : "Joystick %c's pad was unplugged.",
                          'A' + (int)i);
                deskshell_note("Game controllers", buf);
            }
        seen_pads = pads;
    }

    /* A remote session: there while someone looks (PRD D35). */
    if (strcmp(note, seen_note)) {
        ksnprintf(tip, sizeof tip, "Remote session: %s", note);
        icon(m, &id_rem, note[0] != 0, art_remote, tip);
        strncpy(seen_note, note, sizeof seen_note - 1);
        seen_note[sizeof seen_note - 1] = 0;
    }

    /* An app that isn't answering: said once for each of its windows, until it answers again. */
    for (k = 0; k < ARRAY_SIZE(hung_told); k++)
        if (hung_told[k] && (!(w = wm_find(m, hung_told[k])) || !(w->flags & WF_HUNG)))
            hung_told[k] = 0;
    for (w = m->bottom; w; w = w->above) {
        if (!(w->flags & WF_HUNG) || !w->owner)
            continue;
        for (k = 0; k < ARRAY_SIZE(hung_told) && hung_told[k] != w->id; k++) ;
        if (k < ARRAY_SIZE(hung_told))
            continue;
        for (k = 0; k < ARRAY_SIZE(hung_told) && hung_told[k]; k++) ;
        if (k == ARRAY_SIZE(hung_told))
            break;
        hung_told[k] = w->id;
        ksnprintf(buf, sizeof buf, "%.60s isn't answering.", w->title[0] ? w->title : "A program");
        deskshell_note("Not answering", buf);
    }
    started = 1;
}

/* ---- their presses */

enum { GO_SOUND = 1, GO_NETWORK, GO_PADS };

static void go(struct wm *m, u32 data)
{
    (void)m;
    api2_shell_start("CONTROL.EXE", data == GO_SOUND ? "sound" : data == GO_NETWORK ? "network" : "joystick");
}

static void line(struct popmenu *pm, const char *fmt, const char *a, const char *b)
{
    char t[PM_TEXT];
    ksnprintf(t, sizeof t, fmt, a, b);
    pm_add(pm, t, PMI_TEXT, 0);
}

void desktray_press(struct wm *m, u32 id, int what, const struct gfx_rect *r)
{
    struct popmenu *pm;
    char buf[320], a[24], b[24];
    int below, n;
    s32 edge = wm_bar_edge(m, &below);
    if (id == id_vol) {
        u32 v;
        int mute;
        if (what == 2) {
            deskvol_close(m);
            go(m, GO_SOUND);
        } else if (what == 3) {
            mixer_master_get(&v, &mute);
            vol_set(v, !mute);
            keep = 1;
        } else if (deskvol_up()) {
            deskvol_close(m);
        } else {
            deskvol_open(m, r, edge, below, vol_get, vol_set, vol_done);
        }
    } else if (id == id_net) {
        if (what == 2) {
            go(m, GO_NETWORK);
            return;
        }
        net_stat(buf, sizeof buf, 0);
        pm = deskpop_begin(m);
        if (field(buf, "card", a, sizeof a))
            line(pm, "Card: %s", a, "");
        if (field(buf, "ip", a, sizeof a))
            line(pm, "Address: %s", a, "");
        if (field(buf, "gw", a, sizeof a))
            line(pm, "Gateway: %s", a, "");
        if (field(buf, "dns", a, sizeof a))
            line(pm, "Name server: %s", a, "");
        if (field(buf, "rx", a, sizeof a) && field(buf, "tx", b, sizeof b))
            line(pm, "Frames in: %s, out: %s", a, b);
        pm_add(pm, "", PMI_SEP, 0);
        pm_add(pm, "&Network settings...", 0, GO_NETWORK);
        deskpop_show(m, r->x, edge, !below, go);
    } else if (id == id_pad) {
        struct joy_pad p;
        if (what == 2) {
            go(m, GO_PADS);
            return;
        }
        pm = deskpop_begin(m);
        for (n = 0; n < JOY_PADS; n++)
            if (joy_pad_get(n, &p)) {
                a[0] = (char)('A' + n), a[1] = 0;
                line(pm, p.right ? "Joystick %s: a pad with two sticks" : "Joystick %s: a pad", a, "");
            }
        pm_add(pm, "", PMI_SEP, 0);
        pm_add(pm, "&Game controllers...", 0, GO_PADS);
        deskpop_show(m, r->x, edge, !below, go);
    } else if (id == id_rem) {
        pm = deskpop_begin(m);
        line(pm, "Someone is looking at this screen (%s).", desk_note_text(), "");
        deskpop_show(m, r->x, edge, !below, go);
    }
}

void desktray_wheel(struct wm *m, u32 id, int notches)
{
    u32 v;
    int mute;
    s32 n;
    (void)m;
    if (id != id_vol)
        return;
    mixer_master_get(&v, &mute);
    n = (s32)v + notches * 8;
    vol_set((u32)(n < 0 ? 0 : n > 256 ? 256 : n), mute);
    keep = 1;
}
