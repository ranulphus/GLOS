/* API 2.0's CAPTURE (include/glos/api.h; docs/v1/phase-b.md step 12): the windows of a program the caller
 * started, and the caller's own: what one shows, as RGBA or a PNG file, and keys and the mouse into it.
 *
 * Who may: the window's app is the caller, or began as a job that the caller started and still has the handle
 * of (kernel/dos/agent.c keeps a job's owner until then). The pixels are read through the desktop
 * (kernel/wm/deskapp.c) a few rows at a time, the manager's lock held for each lot only, and written to the
 * caller's memory from here. */
#include <string.h>

#include "api2int.h"
#include "agent.h"
#include "desk.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "png.h"

/* The window: 0 and its size if the caller may, else GLOS_E_. */
static int window(struct app *a, u32 id, s32 *w, s32 *h)
{
    u32 owner = 0, f, by;
    if (desk_win_who(id, &owner, w, h) != 0)
        return GLOS_E_NOT_FOUND;
    if (!owner)
        return GLOS_E_REFUSED;                  /* (GLOS's own) */
    if (owner == app_id(a))
        return 0;
    f = irq_save();
    by = agent_job_by(app_began_as(owner));
    irq_restore(f);
    return by && by == app_id(a) ? 0 : GLOS_E_REFUSED;
}

int api2_started_window(struct app *a, u32 id)
{
    s32 w, h;
    return window(a, id, &w, &h);
}

static int pixels(struct app *a, struct trapframe *tf)
{
    struct glos_capture_pixels q;
    struct png p;
    u8 *rgb, *lines, *out;
    u32 size, at = 0, len;
    s32 w = 0, h = 0, y, n, i, x;
    int r, k = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.format > GLOS_CAPTURE_PNG)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if ((r = window(a, q.window, &w, &h)) != 0)
        return a2_fail(tf, (u32)r);
    q.w = (u32)w, q.h = (u32)h;
    n = 60000 / (3 * w + 1);                    /* rows a lot: less than a PNG block's bytes */
    n = n < 1 ? 1 : n > 32 ? 32 : n;
    rgb = kmalloc((u32)n * 3u * (u32)w);                        /* the rows as the desktop gives them */
    lines = kmalloc((u32)n * (3u * (u32)w + 1));                /* ... as PNG scanlines (a filter byte each) */
    out = kmalloc(PNG_ROWS_MAX((u32)n * (4u * (u32)w + 1)) + PNG_HEAD_MAX);     /* ... and as they go to the app */
    if (!rgb || !lines || !out) {
        kfree(rgb);
        kfree(lines);
        kfree(out);
        return a2_fail(tf, GLOS_E_MEMORY);
    }
    r = 0;
    if (q.format == GLOS_CAPTURE_PNG) {         /* (the head; each lot of rows follows as a chunk) */
        at = png_begin(&p, (u32)w, (u32)h, 24, 0, 0, out);
        if (at <= q.room && user_wr((u16)tf->ds, q.buffer, out, at) != 0)
            r = GLOS_E_ARGUMENT;
    }
    for (y = 0; y < h && !r; y += k) {
        if ((k = desk_win_rows(q.window, y, h - y < n ? h - y : n, w, rgb)) <= 0) {
            r = GLOS_E_NOT_FOUND;               /* (gone, or another size now) */
            break;
        }
        if (q.format == GLOS_CAPTURE_RGBA) {
            for (i = 0; i < k * w; i++) {
                out[4 * i] = rgb[3 * i], out[4 * i + 1] = rgb[3 * i + 1], out[4 * i + 2] = rgb[3 * i + 2];
                out[4 * i + 3] = 0xFF;
            }
            len = (u32)(k * w) * 4u;
        } else {
            u8 *d = lines;
            for (i = 0; i < k; i++) {
                *d++ = 0;                       /* (no filter) */
                for (x = 0; x < 3 * w; x++)
                    *d++ = rgb[i * 3 * w + x];
            }
            len = png_rows(&p, lines, (u32)k, out);
        }
        if (at + len <= q.room && user_wr((u16)tf->ds, q.buffer + at, out, len) != 0)
            r = GLOS_E_ARGUMENT;
        at += len;                              /* (past the room: counted still, for the length) */
    }
    kfree(rgb);
    kfree(lines);
    kfree(out);
    q.length = at;
    if (r)
        return a2_fail(tf, (u32)r);
    if (a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return at > q.room ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

static int input(struct app *a, struct trapframe *tf, int mouse)
{
    struct glos_capture_input q;
    struct glos_capture_key key;
    struct glos_capture_mouse ms;
    u32 size, i;
    s32 w, h;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.count > 64)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if ((r = window(a, q.window, &w, &h)) != 0)
        return a2_fail(tf, (u32)r);
    for (i = 0; i < q.count; i++) {
        if (user_rd((u16)tf->ds, q.buffer + i * (mouse ? sizeof ms : sizeof key), mouse ? (void *)&ms : (void *)&key,
                    mouse ? sizeof ms : sizeof key) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        r = mouse ? desk_win_mouse(q.window, ms.x, ms.y, ms.buttons, ms.button, ms.down != 0, ms.clicks)
                  : desk_win_key(q.window, key.key, key.down != 0, key.mods);
        if (r)
            return a2_fail(tf, GLOS_E_NOT_FOUND);
    }
    return a2_ok(tf, 0);
}

int api2_capture(struct app *a, struct trapframe *tf, u32 n)
{
    switch (GLOS_FN(GLOS_AREA_CAPTURE, n)) {
    case GLOS_CAPTURE_PIXELS:
        return pixels(a, tf);
    case GLOS_CAPTURE_KEYS:
        return input(a, tf, 0);
    case GLOS_CAPTURE_MOUSE:
        return input(a, tf, 1);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
