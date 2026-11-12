/* API 2.0's INPUT (include/glos/api.h; docs/v1/phase-b.md, step 3): the
 * keyboard's state, the mouse's capture, pads, the game port, and how an
 * app takes its keys. Key, text, mouse and wheel events are the desktop's,
 * given their 2.0 form in api2win.c.
 *
 * Pads: the game port's code (kernel/vm/vjoy.c) tells of every change of a
 * pad, on the thread that found it (USB's). Each app that asked for pad
 * events is told when one comes or goes; changes of a pad that is there go
 * to the app whose window has the focus. */
#include <string.h>

#include "api2int.h"
#include "desk.h"
#include "io.h"
#include "joy.h"

static void pad_fill(struct glos_pad *o, const struct joy_pad *p)
{
    memset(o, 0, sizeof *o);
    if (!p)
        return;
    o->present = 1;
    o->flags = p->right ? GLOS_PAD_RIGHT : 0;
    o->lx = p->lx ? (int)p->lx - 128 : -127;    /* (0-255 with 128 its rest: -127 to 127) */
    o->ly = p->ly ? (int)p->ly - 128 : -127;
    o->rx = p->rx ? (int)p->rx - 128 : -127;
    o->ry = p->ry ? (int)p->ry - 128 : -127;
    o->buttons = p->buttons;
}

struct pad_news {
    int n;
    const struct joy_pad *p;
};

static void pad_tell(struct app *a, void *arg)
{
    const struct pad_news *news = arg;
    struct api2 *s = a->v2;
    struct glos_ev_pad ev;
    struct glos_pad now;
    if (!s || !s->pads_events)
        return;
    pad_fill(&now, news->p);
    if (now.present == s->pad[news->n].present) {
        if (!now.present || a != deskin_focus_app() || !memcmp(&now, &s->pad[news->n], sizeof now))
            return;                             /* (changes: the focus's alone; the same again: nobody's) */
    }
    memset(&ev, 0, sizeof ev);
    ev.ev.size = sizeof ev;
    ev.ev.kind = GLOS_EV_PAD;
    ev.index = (unsigned)news->n;
    ev.pad = now;
    ev.changed = now.buttons ^ s->pad[news->n].buttons;
    if (a2_post(a, &ev.ev, 0) == 0)
        s->pad[news->n] = now;
}

static void pad_hook(int n, const struct joy_pad *p)
{
    struct pad_news news = { n, p };
    app_each(pad_tell, &news);
}

void api2_input_init(void) { joy_pad_hook(pad_hook); }

static int keys(struct trapframe *tf)
{
    struct glos_keys k;
    u32 size, mods, locks;
    if (a2_rd(tf, &k, sizeof k, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    desk_keys(k.down, &mods, &locks);
    k.mods = mods;
    k.locks = locks;
    return a2_wr(tf, &k, sizeof k, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int capture(struct app *a, struct trapframe *tf)
{
    struct glos_capture q;
    u32 size, id;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.on > 1)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(id = (u32)handle_obj(&a->v2->h, q.handle, GLOS_HK_WINDOW)))
        return a2_fail(tf, handle_kind(&a->v2->h, q.handle) ? GLOS_E_KIND : GLOS_E_HANDLE);
    return (r = desk_win_capture(a, id, (int)q.on)) != 0 ? a2_fail(tf, (u32)r) : a2_ok(tf, 0);
}

static int pads(struct app *a, struct trapframe *tf)
{
    struct glos_pads q;
    struct joy_pad jp;
    u32 size, f;
    int i;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || (q.flags & ~(u32)GLOS_PADS_EVENTS))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    f = irq_save();                             /* (as they are now, and events from here on, in one piece) */
    for (i = 0; i < GLOS_PADS; i++) {
        pad_fill(&q.pad[i], joy_pad_get(i, &jp) ? &jp : 0);
        a->v2->pad[i] = q.pad[i];
    }
    a->v2->pads_events = (q.flags & GLOS_PADS_EVENTS) != 0;
    irq_restore(f);
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int gameport(struct trapframe *tf)
{
    struct glos_gameport q;
    u32 size, range, p;
    int on, mode;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    joy_info(&on, &mode, &range, &p);
    q.virtual_on = (unsigned)on, q.mode = (unsigned)mode, q.range = range, q.pads = p;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

static int mode(struct app *a, struct trapframe *tf)
{
    struct glos_input_mode q;
    u32 size;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || (q.set && (q.flags & ~(u32)GLOS_INPUT_RAW)))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.set)
        a->v2->in_flags = q.flags;
    q.flags = a->v2->in_flags;
    return a2_wr(tf, &q, sizeof q, size) != 0 ? a2_fail(tf, GLOS_E_ARGUMENT) : a2_ok(tf, 0);
}

int api2_input(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_INPUT_KEYS & 0xFF: return keys(tf);
    case GLOS_INPUT_CAPTURE & 0xFF: return capture(a, tf);
    case GLOS_INPUT_PADS & 0xFF: return pads(a, tf);
    case GLOS_INPUT_GAMEPORT & 0xFF: return gameport(tf);
    case GLOS_INPUT_MODE & 0xFF: return mode(a, tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
