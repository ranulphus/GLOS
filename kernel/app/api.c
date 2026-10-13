/* The GLOS API's entry (PRD §5.6, §10; include/glos/api.h): the far
 * procedure DPMI 0A00h "GLOS" gives a program, a HLT in the host's
 * trampoline page. The function is in EAX; its results go into the frame,
 * and the host's far return follows (kernel/dpmi/host.c). 1: the program
 * becomes an app. 2: the call is to be made again from the same HLT, when a
 * handler of the app's has run (a sleep an interrupt of its own cut short). */
#include "appint.h"
#include "desk.h"
#include "dpmi.h"
#include "kprintf.h"
#include "timer.h"
#include "vm.h"

static int fail(struct trapframe *tf, u32 e)
{
    tf->eax = e;
    tf->eflags |= FL_CF;
    return 0;
}

/* Milliseconds as ticks, 1024 to the second, and back. */
static u32 ticks_of(u32 ms)
{
    if (ms > 0x03FFFFFFu)                       /* (18 hours: the sum below in 32 bits) */
        ms = 0x03FFFFFFu;
    return ms + (ms * 24 + 999) / 1000;
}

static u32 ms_of(u32 ticks) { return (u32)(((unsigned long long)ticks * 125 + 127) >> 7); }

/* A structure of the app's at DS:off that starts with its own size: read
   (as much as the kernel knows of), written back. */
static int get(struct trapframe *tf, u32 off, void *s, u32 n)
{
    return user_rd((u16)tf->ds, off, s, n) == 0 && *(u32 *)s >= n ? 0 : -1;
}

static int put(struct trapframe *tf, u32 off, const void *s, u32 n) { return user_wr((u16)tf->ds, off, s, n); }

/* The window functions: the frame's answer for desk_win_*()'s. */
static int done(struct trapframe *tf, int e)
{
    if (e)
        return fail(tf, (u32)e);
    tf->eax = 0;
    return 0;
}

int glos_api(struct trapframe *tf)
{
    static const char *const why[] = { "?", "already", "nested", "16-bit", "parent", "exclusive", "memory" };
    struct app *a = app_cur();
    struct glos_window gw;
    struct glos_event ev;
    struct glos_rect gr;
    char title[sizeof gw.title];
    u32 fn = tf->eax, id, left, i;
    int r;

    tf->eflags &= ~FL_CF;
    if (fn == GLOS_FN_VERSION) {
        tf->eax = GLOS_API_VERSION;
        tf->ebx = 0;
        tf->ecx = GLOS_FN_COUNT;
        return 0;
    }
    if (fn == GLOS_FN_INIT) {
        if ((tf->ebx >> 16) != GLOS_API_MAJOR)
            return fail(tf, GLOS_E_VERSION);
        if ((r = app_init(tf, &id)) != 0) {
            kprintf("GLOS-APP refuse psp=%04x why=%s\n", dctx->psp, why[r]);
            tf->ebx = (u32)r;
            return fail(tf, GLOS_E_INIT);
        }
        tf->eax = 0;
        tf->ebx = id;
        return 1;
    }
    if (fn >= GLOS_FN_COUNT)
        return fail(tf, GLOS_E_FUNCTION);
    if (!a)
        return fail(tf, GLOS_E_NOT_APP);
    switch (fn) {
    case GLOS_FN_YIELD:
        thread_yield();
        tf->eax = 0;
        break;
    case GLOS_FN_SLEEP:
        left = app_sleep(a, ticks_of(tf->ebx));
        if (left) {                             /* an interrupt of its own is due: its handler runs, and the call */
            tf->ebx = ms_of(left);              /* is made again, for the rest */
            return 2;
        }
        tf->eax = 0;
        break;
    case GLOS_FN_TICKS:
        tf->eax = (u32)(((unsigned long long)timer_ticks() * 125) >> 7);
        break;
    case GLOS_FN_EVENT:
        if (get(tf, tf->ebx, &ev, sizeof ev) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        while (!app_event_get(a, &ev)) {
            if (!tf->ecx) {
                tf->eax = 0;
                return 0;
            }
            left = app_sleep(a, tf->ecx == 0xFFFFFFFFu ? 0x7FFFFFFFu : ticks_of(tf->ecx));
            if (app_event_waiting(a))
                continue;
            if (!left)
                tf->ecx = 0;                    /* (it has waited that long) */
            else if (tf->ecx != 0xFFFFFFFFu)    /* an interrupt of its own: again after its handler, for the rest */
                tf->ecx = ms_of(left);
            if (left)
                return 2;
        }
        if (put(tf, tf->ebx, &ev, sizeof ev) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        tf->eax = 1;
        break;
    case GLOS_FN_WINDOW:
        if (get(tf, tf->ebx, &gw, sizeof gw) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        if ((r = desk_win_create(a, &gw)) == 0 && put(tf, tf->ebx, &gw, sizeof gw) != 0) {
            desk_win_close(a, gw.id);
            r = GLOS_E_ARGUMENT;
        }
        if (!r)
            a->gui = 1;                         /* (its keyboard and its mouse are the desktop's from now on) */
        return done(tf, r);
    case GLOS_FN_CLOSE:
        return done(tf, desk_win_close(a, tf->ebx));
    case GLOS_FN_SHOW:
        return done(tf, desk_win_show(a, tf->ebx, tf->ecx != 0));
    case GLOS_FN_MOVE:
        return done(tf, desk_win_move(a, tf->ebx, (s32)tf->ecx, (s32)tf->edx));
    case GLOS_FN_RESIZE:
        return done(tf, desk_win_resize(a, tf->ebx, (s32)tf->ecx, (s32)tf->edx));
    case GLOS_FN_TITLE:
        for (i = 0; i < sizeof title - 1; i++)
            if (user_rd((u16)tf->ds, tf->ecx + i, &title[i], 1) != 0)
                return fail(tf, GLOS_E_ARGUMENT);
            else if (!title[i])
                break;
        title[i] = 0;
        return done(tf, desk_win_title(a, tf->ebx, title));
    case GLOS_FN_PIXELS:
        if (get(tf, tf->ecx, &gw, sizeof gw) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        if ((r = desk_win_pixels(a, tf->ebx, &gw)) == 0 && put(tf, tf->ecx, &gw, sizeof gw) != 0)
            r = GLOS_E_ARGUMENT;
        return done(tf, r);
    case GLOS_FN_PRESENT:
        if (tf->ecx && user_rd((u16)tf->ds, tf->ecx, &gr, sizeof gr) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        return done(tf, desk_win_present(a, tf->ebx, tf->ecx ? &gr : 0));
    case GLOS_FN_FOCUS:
        return done(tf, desk_win_focus(a, tf->ebx));
    case GLOS_FN_SCREEN:
        if (get(tf, tf->ebx, &gw, sizeof gw) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        desk_screen(&gw);
        return done(tf, put(tf, tf->ebx, &gw, sizeof gw) != 0 ? GLOS_E_ARGUMENT : 0);
    }
    return 0;
}
