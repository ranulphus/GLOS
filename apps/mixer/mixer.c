/* MIXER: the volumes of everything that makes a sound (GLOS v1, phase F; docs/v1/apps/mixer.md), from AUDIO's
 * MIXER and LEVELS, again ten times a second:
 *
 *   Volume        GLOS's master: what the task bar's volume icon has; the output's level beside it
 *   Sound card    the chip's own channels where it has them (its master, wave, FM, CD, line, microphone), and the
 *                 input that is recorded
 *   Programs      every stream that plays, by its program's name: its volume, balance and mute, and its level
 *                 now; a strip comes when a program begins to play and goes when it stops
 *
 * What is moved here is set at once. What something else changes (the volume icon, a program's own volume)
 * shows here within the tenth of a second: a slider is moved from outside only when GLOS's number is another than
 * the one read last (the chip rounds what it is given: its numbers read back are not always those written).
 *
 * MIXER -demo: the numbers are the test's own, not GLOS's (a card that is always there, streams that come and go
 * at F9), so that the pictures of the GUI scripts are the same on every PC and on the host.
 *
 * "MIXER ..." lines on stdout (the suite's). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/gui.h>

#define MAXS 12                         /* streams shown, at most (and no more than the screen has room for) */
#define MAXC 8                          /* the chip's channels */
#define STRIP 50                        /* a strip's width: the master, six channels and four programs on 640 pixels */

enum { K_MASTER, K_CHANNEL, K_STREAM };

struct strip {
    int kind;
    unsigned id;                        /* the channel's, the stream's */
    struct gui_widget *box, *meter, *vol, *bal, *mute;
    int vol_seen, bal_seen, mute_seen;  /* GLOS's numbers as read last */
    char name[20];
};

static struct gui_window *win;
static struct gui_widget *card_box, *prog_box, *prog_none, *input, *status;
static struct strip master, chan[MAXC], strm[MAXS];
static int nchan, nstrm;
static int nplaying;                    /* the streams there are (those beyond the room: counted in the status bar) */
static char card_text[80];
static int input_seen = -1;
static int demo, setting, ticks, no_card;

/* ---- the test's own numbers (MIXER -demo) */
static struct {
    int volume, mute, input, step;
    struct glos_audio_channel ch[6];
    struct glos_audio_stream st[2];
    int on[2];
} D = {
    80, 0, GLOS_INPUT_MIC, 0,
    { { sizeof(struct glos_audio_channel), GLOS_CHANNEL_MASTER, 200, 200, 0, "Master" },
      { sizeof(struct glos_audio_channel), GLOS_CHANNEL_WAVE, 255, 255, 0, "Wave" },
      { sizeof(struct glos_audio_channel), GLOS_CHANNEL_FM, 180, 180, 0, "FM" },
      { sizeof(struct glos_audio_channel), GLOS_CHANNEL_CD, 128, 64, 0, "CD" },
      { sizeof(struct glos_audio_channel), GLOS_CHANNEL_LINE, 0, 0, 1, "Line" },
      { sizeof(struct glos_audio_channel), GLOS_CHANNEL_MIC, 100, 100, 0, "Mic" } },
    { { sizeof(struct glos_audio_stream), 1, 101, 44100, 2, 256, 0, 0, 0, 20000, 16000 },
      { sizeof(struct glos_audio_stream), 2, 102, 22050, 1, 128, -128, 0, 0, 9000, 3000 } },
    { 0, 0 }
};

static int demo_mixer(struct glos_audio_mixer *m)
{
    unsigned i, n = 0;
    switch (m->what) {
    case GLOS_MIXER_MASTER:
        m->volume = D.volume, m->mute = D.mute;
        return 0;
    case GLOS_MIXER_MASTER_SET:
        if (m->volume >= 0)
            D.volume = m->volume;
        if (m->mute >= 0)
            D.mute = m->mute;
        return 0;
    case GLOS_MIXER_STREAMS:
        for (i = 0; i < 2; i++)
            if (D.on[i] && (n + 1) * sizeof D.st[0] <= m->room)
                ((struct glos_audio_stream *)(unsigned long)m->buffer)[n++] = D.st[i];
        m->count = n, m->length = n * sizeof D.st[0];
        return 0;
    case GLOS_MIXER_STREAM_SET:
        for (i = 0; i < 2; i++)
            if (D.on[i] && D.st[i].id == m->id) {
                if (m->volume >= 0)
                    D.st[i].volume = (unsigned)m->volume;
                if (m->balance != -1000)
                    D.st[i].balance = m->balance;
                if (m->mute >= 0)
                    D.st[i].mute = (unsigned)m->mute;
                return 0;
            }
        return GLOS_E_NOT_FOUND;
    case GLOS_MIXER_CHANNELS:
        for (i = 0; i < 6 && (i + 1) * sizeof D.ch[0] <= m->room; i++)
            ((struct glos_audio_channel *)(unsigned long)m->buffer)[i] = D.ch[i];
        m->count = i, m->length = i * sizeof D.ch[0];
        return 0;
    case GLOS_MIXER_CHANNEL_SET:
        for (i = 0; i < 6; i++)
            if (D.ch[i].id == m->id) {
                if (m->volume >= 0)             /* (as a chip does: in steps of eight) */
                    D.ch[i].left = (unsigned)m->volume & ~7u, D.ch[i].right = (unsigned)m->balance & ~7u;
                if (m->mute >= 0)
                    D.ch[i].mute = (unsigned)m->mute;
                m->volume = (int)D.ch[i].left, m->balance = (int)D.ch[i].right;
                return 0;
            }
        return GLOS_E_ARGUMENT;
    case GLOS_MIXER_INPUT:
        m->id = (unsigned)D.input;
        return 0;
    case GLOS_MIXER_INPUT_SET:
        D.input = (int)m->id;
        return 0;
    }
    return GLOS_E_ARGUMENT;
}

static int mixer(struct glos_audio_mixer *m)
{
    return demo ? demo_mixer(m) : glos_mixer(m);
}

static void out_levels(unsigned *l, unsigned *r)
{
    *l = *r = 0;
    if (demo) {                                 /* what the streams have, through the master */
        int i;
        for (i = 0; i < 2; i++)
            if (D.on[i] && !D.st[i].mute) {
                *l += D.st[i].left * D.st[i].volume / 256;
                *r += D.st[i].right * D.st[i].volume / 256;
            }
        *l = D.mute ? 0 : *l * (unsigned)D.volume / 100;
        *r = D.mute ? 0 : *r * (unsigned)D.volume / 100;
    } else {
        glos_audio_levels(0, l, r);
    }
}

/* A stream's program by name ("PAINT"), or "Stream N". */
static void stream_name(const struct glos_audio_stream *s, char *out, size_t room)
{
    static struct glos_program progs[96];
    unsigned n = 0, i;
    char *dot;
    snprintf(out, room, GUI_T("Stream %u"), s->id);
    if (demo) {
        snprintf(out, room, "%s", s->app == 101 ? "PLAYER" : "SKIING");
        return;
    }
    if (glos_programs(progs, sizeof progs, &n) != 0)
        return;
    for (i = 0; i < n && i < 96; i++)
        if (progs[i].kind == GLOS_PROG_APP && progs[i].id == s->app && progs[i].name[0]) {
            snprintf(out, room, "%.15s", progs[i].name);
            if ((dot = strrchr(out, '.')) != NULL)
                *dot = 0;
            return;
        }
}

/* ---- a strip: a name, a meter and a slider side by side, a balance, a mute */

/* A channel's two levels as one volume and a balance (-255 left ... 255 right), and back. */
static void to_balance(unsigned l, unsigned r, int *vol, int *bal)
{
    *vol = (int)(l > r ? l : r);
    *bal = !*vol ? 0 : r >= l ? 255 - (int)(l * 255 / (unsigned)*vol) : -(255 - (int)(r * 255 / (unsigned)*vol));
}

static void from_balance(int vol, int bal, int *l, int *r)
{
    *l = bal > 0 ? vol * (255 - bal) / 255 : vol;
    *r = bal < 0 ? vol * (255 + bal) / 255 : vol;
}

static void strip_set(struct strip *s, int what)
{
    struct glos_audio_mixer m;
    int vol = gui_value(s->vol), bal = s->bal ? gui_value(s->bal) : 0, mute = gui_checked(s->mute), l, r;
    memset(&m, 0, sizeof m);
    if (s->kind == K_MASTER) {
        m.what = GLOS_MIXER_MASTER_SET, m.volume = vol, m.mute = mute;
        if (mixer(&m) == 0)
            printf("MIXER master %d mute %d\n", vol, mute);
        s->vol_seen = vol, s->mute_seen = mute;
    } else if (s->kind == K_STREAM) {
        m.what = GLOS_MIXER_STREAM_SET, m.id = s->id, m.volume = vol, m.balance = bal, m.mute = mute;
        if (mixer(&m) == 0)
            printf("MIXER stream %s volume %d balance %d mute %d\n", s->name, vol, bal, mute);
        s->vol_seen = vol, s->bal_seen = bal, s->mute_seen = mute;
    } else {
        from_balance(vol, bal, &l, &r);
        m.what = GLOS_MIXER_CHANNEL_SET, m.id = s->id, m.volume = l, m.balance = r, m.mute = mute;
        if (mixer(&m) == 0) {
            printf("MIXER channel %s %d,%d mute %d\n", s->name, m.volume, m.balance, mute);
            to_balance((unsigned)m.volume, (unsigned)m.balance, &s->vol_seen, &s->bal_seen);    /* (as the chip took it) */
            s->mute_seen = mute;
        }
    }
    fflush(stdout);
    (void)what;
}

static void strip_changed(struct gui_widget *w, int what)
{
    if (what == GUI_ON_CHANGE && !setting)
        strip_set(w->user, what);
}

/* GLOS's numbers shown, where they are others than those read last. */
static void strip_show(struct strip *s, int vol, int bal, int mute)
{
    setting = 1;
    if (vol != s->vol_seen)
        gui_set_value(s->vol, vol);
    if (s->bal && bal != s->bal_seen)
        gui_set_value(s->bal, bal);
    if (mute != s->mute_seen)
        gui_set_checked(s->mute, mute);
    setting = 0;
    s->vol_seen = vol, s->bal_seen = bal, s->mute_seen = mute;
}

static void strip_make(struct strip *s, struct gui_widget *parent, int kind, unsigned id, const char *name, int max,
                       int bal_max, int meter)
{
    struct gui_widget *row, *l;
    memset(s, 0, sizeof *s);
    s->kind = kind, s->id = id;
    snprintf(s->name, sizeof s->name, "%s", name);
    s->vol_seen = s->bal_seen = s->mute_seen = -100000;
    s->box = gui_vbox(parent, 4, 1);
    gui_size(s->box, STRIP, 0);
    l = gui_label(s->box, kind == K_CHANNEL ? GUI_T(name) : name);
    gui_label_style(l, gui_font_get(GUI_FONT_SANS, 0), GFX_NONE, 0);    /* (the small one: a program's eight letters fit) */
    row = gui_hbox(s->box, 4, 0);
    gui_space(row, 0, 0);
    if (meter) {
        s->meter = gui_meter(row, 2, 1);
        gui_size(s->meter, 10, 110);
    }
    s->vol = gui_slider(row, 0, max, 0);
    gui_slider_style(s->vol, GUI_SLIDER_VERTICAL, max / 4, NULL);
    gui_size(s->vol, 0, 110);
    s->vol->user = s;
    gui_on(s->vol, strip_changed, 0);
    gui_tip(s->vol, GUI_T("Volume"));
    gui_space(row, 0, 0);
    if (bal_max) {
        s->bal = gui_slider(s->box, -bal_max, bal_max, 0);
        gui_slider_style(s->bal, 0, bal_max, NULL);    /* (marks: left, the middle, right) */
        s->bal->user = s;
        gui_on(s->bal, strip_changed, 0);
        gui_tip(s->bal, GUI_T("Balance: left to right"));
    }
    s->mute = gui_check(s->box, GUI_T("Mute"), 0);
    s->mute->user = s;
    gui_on(s->mute, strip_changed, 0);
}

/* ---- the three parts */

static void master_read(void)
{
    struct glos_audio_mixer m;
    unsigned l, r;
    memset(&m, 0, sizeof m);
    m.what = GLOS_MIXER_MASTER;
    if (mixer(&m) == 0)
        strip_show(&master, m.volume, 0, m.mute);
    out_levels(&l, &r);
    gui_meter_set(master.meter, (int)(l * 1000 / 32767), (int)(r * 1000 / 32767));
}

static const char *const inputs[] = { GUI_N("Microphone"), GUI_N("Line in"), GUI_N("CD"), GUI_N("All of them") };

static void input_chosen(struct gui_widget *w, int what)
{
    struct glos_audio_mixer m;
    int v = gui_value(w);
    if (what != GUI_ON_SELECT || setting || v < 0 || v > 3)
        return;
    memset(&m, 0, sizeof m);
    m.what = GLOS_MIXER_INPUT_SET, m.id = (unsigned)v;
    if (mixer(&m) == 0) {
        printf("MIXER input %s\n", inputs[v]);
        fflush(stdout);
        input_seen = v;
    }
}

static void card_read(int first)
{
    static struct glos_audio_channel ch[MAXC];
    struct glos_audio_mixer m;
    unsigned i;
    int k, vol, bal;
    memset(&m, 0, sizeof m);
    m.what = GLOS_MIXER_CHANNELS, m.buffer = (unsigned)(unsigned long)ch, m.room = sizeof ch;
    if (mixer(&m) != 0)
        m.count = 0;
    if (first) {
        struct gui_widget *row = gui_hbox(card_box, 2, 0), *line;
        for (i = 0; i < m.count && i < MAXC; i++)
            strip_make(&chan[nchan++], row, K_CHANNEL, ch[i].id, ch[i].name, 255, 255, 0);
        if (!nchan) {
            gui_label(row, GUI_T("This card has no levels of its own."));
        } else {
            line = gui_hbox(card_box, 6, 2);
            gui_label(line, GUI_T("Record from:"));
            input = gui_choice(line, 14);
            for (k = 0; k < 4; k++)
                gui_choice_add(input, GUI_T(inputs[k]));
            gui_on(input, input_chosen, 0);
        }
    }
    for (i = 0; i < m.count; i++)
        for (k = 0; k < nchan; k++)
            if (chan[k].id == ch[i].id) {
                to_balance(ch[i].left, ch[i].right, &vol, &bal);
                strip_show(&chan[k], vol, bal, (int)ch[i].mute);
            }
    if (input) {
        memset(&m, 0, sizeof m);
        m.what = GLOS_MIXER_INPUT;
        if (mixer(&m) == 0 && (int)m.id != input_seen && m.id <= 3) {
            setting = 1;
            gui_set_value(input, (int)m.id);
            setting = 0;
            input_seen = (int)m.id;
        }
    }
}

static void streams_read(void)
{
    static struct glos_audio_stream st[MAXS];
    struct glos_audio_mixer m;
    unsigned i;
    int k, same, shown;
    memset(&m, 0, sizeof m);
    m.what = GLOS_MIXER_STREAMS, m.buffer = (unsigned)(unsigned long)st, m.room = sizeof st;
    if (mixer(&m) != 0)
        m.count = 0;
    {                                           /* as many strips as the screen has room for beside the rest */
        int sw = 640, sh = 480, room;
        gui_screen_size(&sw, &sh);
        room = (sw - 24 - (STRIP + 30) - (nchan ? nchan * (STRIP + 2) + 30 : 170) - 30) / (STRIP + 2);
        room = room < 1 ? 1 : room > MAXS ? MAXS : room;
        shown = (int)m.count < room ? (int)m.count : room;
    }
    same = shown == nstrm && (int)m.count == nplaying;
    for (i = 0; same && (int)i < shown; i++)
        same = strm[i].id == st[i].id;
    if (!same) {                                /* other streams than those shown: the strips made again */
        char names[160] = "", text[120];
        gui_clear(prog_box);
        nstrm = 0;
        prog_none = NULL;
        nplaying = (int)m.count;
        for (i = 0; (int)i < shown; i++) {
            char name[20];
            stream_name(&st[i], name, sizeof name);
            strip_make(&strm[nstrm++], prog_box, K_STREAM, st[i].id, name, 256, 256, 1);
            snprintf(names + strlen(names), sizeof names - strlen(names), " %s", name);
        }
        if (!nstrm) {
            prog_none = gui_label(prog_box, GUI_T("Nothing is playing."));
            gui_size(prog_none, 130, 0);
        }
        if (nplaying > nstrm)                   /* (more programs play than there is room to show) */
            snprintf(text, sizeof text, GUI_T("%s   and %d more programs playing"), card_text, nplaying - nstrm);
        else
            snprintf(text, sizeof text, "%s", card_text);
        gui_set_text(status, text);
        gui_fit(win);
        printf("MIXER streams %d%s\n", nplaying, names);
        fflush(stdout);
    }
    for (k = 0; k < nstrm; k++) {
        strip_show(&strm[k], (int)st[k].volume, st[k].balance, (int)st[k].mute);
        gui_meter_set(strm[k].meter, (int)(st[k].left * 1000 / 32767), (int)(st[k].right * 1000 / 32767));
    }
}

static void tick(void *arg)
{
    (void)arg;
    master_read();
    if (!no_card) {
        streams_read();
        if (ticks++ % 5 == 0)
            card_read(0);
    }
}

static int key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    if (demo && e->key == 0x43) {                    /* F9: the test's streams: one, two, the second alone */
        D.step = (D.step + 1) % 4;
        D.on[0] = D.step == 1 || D.step == 2;
        D.on[1] = D.step == 2 || D.step == 3;
        tick(NULL);
        return 1;
    }
    if (e->key == GUI_K_ESC) {
        gui_close_later(win);
        return 1;
    }
    return 0;
}

static void build(void)
{
    struct gui_widget *row, *g;
    struct glos_audio_device dev[8];
    unsigned ndev = 0;
    char text[80];
    win = gui_window(GUI_T("Mixer"), 300, 220, GUI_W_FIXED);
    gui_window_icon(win, "app-audio");
    win->on_key = key;
    row = gui_hbox(win->root, 6, 6);
    g = gui_group(row, GUI_T("Volume"));
    strip_make(&master, g, K_MASTER, 0, "GLOS", 100, 0, 1);
    card_box = gui_group(row, GUI_T("Sound card"));
    g = gui_group(row, GUI_T("Programs"));
    prog_box = gui_hbox(g, 2, 0);
    status = gui_status(win);
    if (demo) {
        snprintf(text, sizeof text, GUI_T("%s, %u Hz"), "test", 44100u);
    } else if (glos_audio_devices(dev, sizeof dev, &ndev) != 0 || !ndev || dev[0].kind != GLOS_AUDIO_DEV_OUT) {
        no_card = 1;
        snprintf(text, sizeof text, "%s", GUI_T("There is no sound card."));
    } else {
        snprintf(text, sizeof text, GUI_T("%s, %u Hz"), dev[0].name, dev[0].rate);
    }
    snprintf(card_text, sizeof card_text, "%s", text);
    gui_set_text(status, text);
    if (no_card) {
        gui_label(card_box, GUI_T("There is no sound card."));
        prog_none = gui_label(prog_box, GUI_T("Nothing is playing."));
        gui_enable(master.vol, 0);
        gui_enable(master.mute, 0);
    } else {
        card_read(1);
        nstrm = -1;                             /* (the first look makes the strips, or says there are none) */
    }
    printf("MIXER card %s channels %d\n", no_card ? "none" : "yes", nchan);
    fflush(stdout);
}

int main(int argc, char **argv)
{
    if (gui_init(argc, argv) != 0) {
        printf("MIXER no GLOS\n");
        return 1;
    }
    demo = argc > 1 && !strcmp(argv[1], "-demo");
    build();
    tick(NULL);
    gui_fit(win);
    gui_show(win, 1);
    gui_focus(master.vol);
    gui_idle(tick, NULL, 100);
    gui_run();
    printf("MIXER end\n");
    return 0;
}
