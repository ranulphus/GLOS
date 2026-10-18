/* The GLOS API's entry (PRD §5.6, §10; include/glos/api.h): the far
 * procedure DPMI 0A00h "GLOS" gives a program, a HLT in the host's
 * trampoline page. The function is in EAX; its results go into the frame,
 * and the host's far return follows (kernel/dpmi/host.c). 1: the program
 * becomes an app. 2: the call is to be made again from the same HLT, when a
 * handler of the app's has run (a sleep an interrupt of its own cut short). */
#include "appint.h"
#include "agent.h"
#include "clip.h"
#include "clock.h"
#include "stat.h"
#include "watch.h"
#include "kcfg.h"
#include "desk.h"
#include "display.h"
#include "dpmi.h"
#include "gpu.h"
#include "io.h"
#include "sched.h"
#include "kprintf.h"
#include "mgadisp.h"
#include "mixer.h"
#include "nic.h"
#include "randombytes.h"
#include "sock.h"
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

/* What the machine can do (GLOS_FN_VERSION's EBX): the desktop's, even while a session has the screen (a program
   asking is often that session, and has the desktop again once it is an app). */
static u32 caps(void)
{
    u32 bpp = (u32)display_desktop_bpp();
    u32 c = (mixer_on() || mixer_found() ? GLOS_CAP_AUDIO : 0u) | (net_up() ? GLOS_CAP_NET : 0u);
    if (!bpp || !mga_desktop_card())
        return c;
    return c | GLOS_CAP_GPU | (bpp == 16 ? GLOS_CAP_GLWIN : 0u);
}

static int app_fn(struct app *a, struct trapframe *tf, u32 fn);

/* A packed request's NUL-ended strings, up to n of them (GLOS_FN_CONFIG): their count. */
static int strings(struct trapframe *tf, u32 at, char *buf, u32 room, const char **s, int n)
{
    u32 i = 0;
    int k = 0;
    while (k < n && i < room) {
        s[k] = buf + i;
        for (;;) {
            if (i >= room || user_rd((u16)tf->ds, at + i, &buf[i], 1) != 0)
                return -1;
            if (!buf[i++])
                break;
        }
        k++;
    }
    return k;
}

/* GLOS_FN_JOB: a job's output read (waiting in pieces, so that a kill is seen), keys typed, ended, let go. */
static int job_fn(struct app *a, struct trapframe *tf)
{
    static u8 buf[4096];
    static u16 keys[64];
    u32 st, code, waited = 0, want, i;
    int n;
    switch (tf->ebx) {
    case 0:
        want = tf->edi == 0xFFFFFFFFu ? 0xFFFFFFFFu : ticks_of(tf->edi);
        for (;;) {                              /* (one app at a time in buf: an app's thread is the only reader */
            u32 step = want - waited < 64 ? want - waited : 64;        /* of its own jobs, and these are copied out
                                                   before it waits again) */
            n = agent_app_read(a, tf->ecx, buf, tf->esi < sizeof buf ? tf->esi : sizeof buf, step, &st, &code);
            if (n < 0)
                return fail(tf, GLOS_E_ARGUMENT);
            if (n > 0 && user_wr((u16)tf->ds, tf->edx, buf, (u32)n) != 0)
                return fail(tf, GLOS_E_ARGUMENT);
            if (n > 0 || st == GLOS_JOB_DONE || a->kill || waited >= want || app_event_waiting(a))
                break;
            waited += step;
        }
        tf->eax = (u32)n;
        tf->ebx = st;
        tf->ecx = code;
        return 0;
    case 1:
        for (i = 0; i < tf->esi && i < 64; i++) {
            if (tf->edi == 2) {
                if (user_rd((u16)tf->ds, tf->edx + i * 2, &keys[i], 2) != 0)
                    return fail(tf, GLOS_E_ARGUMENT);
            } else {
                u8 c;
                if (user_rd((u16)tf->ds, tf->edx + i, &c, 1) != 0)
                    return fail(tf, GLOS_E_ARGUMENT);
                keys[i] = c;
            }
        }
        if ((n = agent_app_type(a, tf->ecx, keys, i)) < 0)
            return fail(tf, GLOS_E_ARGUMENT);
        tf->eax = (u32)n;
        return 0;
    case 2:
        return done(tf, agent_app_kill(a, tf->ecx) == 0 ? 0 : GLOS_E_ARGUMENT);
    case 3:
        return done(tf, agent_app_forget(a, tf->ecx) == 0 ? 0 : GLOS_E_ARGUMENT);
    case 4:
        if ((n = agent_app_keys(a, tf->ecx, keys, tf->esi < 64 ? tf->esi : 64)) < 0
            || (n && user_wr((u16)tf->ds, tf->edx, keys, (u32)n * 2) != 0))
            return fail(tf, GLOS_E_ARGUMENT);
        tf->eax = (u32)n;
        return 0;
    }
    return fail(tf, GLOS_E_ARGUMENT);
}

static int config_fn(struct trapframe *tf)
{
    static char req[600], out[1024];          /* (one at a time: they are used under no lock of their own) */
    static struct waitq q;
    static struct thread *owner;
    const char *s[3];
    int r, n;
    u32 f = irq_save(), len;
    while (owner && owner != current)
        thread_block(&q);
    owner = current;
    irq_restore(f);
    switch (tf->ebx) {
    case 0:
        if ((n = strings(tf, tf->ecx, req, sizeof req, s, 2)) != 2) {
            r = -1;
            break;
        }
        r = kcfg_get(s[0], s[1], out, sizeof out);
        len = strlen(out);
        if (r >= 0 && tf->esi && user_wr((u16)tf->ds, tf->edx, out, len + 1 <= tf->esi ? len + 1 : tf->esi) != 0)
            r = -1;
        if (r >= 0) {
            tf->eax = len;
            tf->ebx = (u32)r;
        }
        break;
    case 1:
    case 2:
        if ((n = strings(tf, tf->ecx, req, sizeof req, s, tf->ebx == 1 ? 3 : 2)) != (tf->ebx == 1 ? 3 : 2)) {
            r = -1;
            break;
        }
        out[0] = 0;
        r = kcfg_set(s[0], s[1], tf->ebx == 1 ? s[2] : 0, out, sizeof out);
        if (r >= 0)
            tf->eax = (u32)r;
        else if (tf->esi)
            user_wr((u16)tf->ds, tf->edx, out, strlen(out) + 1 <= tf->esi ? strlen(out) + 1 : tf->esi);
        break;
    case 3:
    case 4:
        if (tf->ebx == 4 && strings(tf, tf->ecx, req, sizeof req, s, 1) != 1) {
            r = -1;
            break;
        }
        len = tf->ebx == 3 ? kcfg_sections(out, sizeof out) : kcfg_keys(s[0], out, sizeof out);
        r = tf->esi && user_wr((u16)tf->ds, tf->edx, out, len <= tf->esi ? len : tf->esi) != 0 ? -1 : 0;
        tf->eax = len;
        break;
    default:
        r = -1;
    }
    f = irq_save();
    owner = 0;
    thread_wake(&q);
    irq_restore(f);
    return r < 0 ? fail(tf, GLOS_E_CONFIG) : 0;
}

int glos_api(struct trapframe *tf)
{
    static const char *const why[] = { "?", "already", "nested", "16-bit", "parent", "exclusive", "memory" };
    struct app *a = app_cur();
    u32 fn = tf->eax, id;
    int r;

    tf->eflags &= ~FL_CF;
    if (fn == GLOS_FN_VERSION) {
        tf->eax = GLOS_API_VERSION;
        tf->ebx = caps();
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
    if (fn == GLOS_FN_SCREEN && !a) {           /* (1.1: for any program, as GLOS_FN_VERSION: the desktop's size
                                                   before it asks to be an app, for DOS-GL's windowed sizes) */
        struct glos_window gw;
        if (get(tf, tf->ebx, &gw, sizeof gw) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        desk_screen(&gw);
        return done(tf, put(tf, tf->ebx, &gw, sizeof gw) != 0 ? GLOS_E_ARGUMENT : 0);
    }
    if (fn == GLOS_FN_TIME) {                   /* (1.3: for any program) */
        tf->eax = clock_utc();
        tf->ebx = (u32)clock_how();
        tf->ecx = (u32)clock_offset();
        return 0;
    }
    if (fn == GLOS_FN_RANDOM) {                 /* (1.3: for any program) */
        u8 buf[256];
        if (tf->ecx > sizeof buf)
            return fail(tf, GLOS_E_ARGUMENT);
        randombytes(buf, tf->ecx);
        r = user_wr((u16)tf->ds, tf->ebx, buf, tf->ecx);
        memset(buf, 0, sizeof buf);
        return done(tf, r != 0 ? GLOS_E_ARGUMENT : 0);
    }
    if (!a)
        return fail(tf, GLOS_E_NOT_APP);
    if (fn == GLOS_FN_GPU_LOCK)                 /* (not a safe point: the lock is being taken, not left) */
        return gpu_app_lock(a) == 0 ? done(tf, 0) : fail(tf, GLOS_E_NO_GPU);
    gpu_safe_begin(a);                          /* the app's turn with the engine may end here */
    r = app_fn(a, tf, fn);
    gpu_safe_end(a);
    return r;
}

static int app_fn(struct app *a, struct trapframe *tf, u32 fn)
{
    struct glos_window gw;
    struct glos_event ev;
    struct glos_rect gr;
    struct glos_gpu gg;
    char title[sizeof gw.title];
    u32 left, i, off, bytes;
    int r;

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
    case GLOS_FN_GPU:
        if (get(tf, tf->ebx, &gg, sizeof gg) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        if ((r = gpu_app_open(a, &gg)) == 0 && put(tf, tf->ebx, &gg, sizeof gg) != 0)
            r = GLOS_E_ARGUMENT;
        return done(tf, r);
    case GLOS_FN_GPU_UNLOCK:
        gpu_app_unlock(a);
        return done(tf, 0);
    case GLOS_FN_VRAM:
        if (tf->ebx == 1) {
            if (mga_app_block(a->id, tf->ecx, 1, &off, &bytes) && off == tf->ecx)
                desk_app_vram_gone(a, off, bytes);      /* (no window may show it after) */
            return done(tf, gpu_vram_free(a, tf->ecx));
        }
        if (tf->ebx != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        if ((r = gpu_vram_alloc(a, tf->ecx, tf->edx, tf->esi, &off, &bytes)) != 0)
            return fail(tf, (u32)r);
        tf->eax = off;
        tf->ebx = bytes;
        return 0;
    case GLOS_FN_WINVRAM:
        return done(tf, desk_win_vram(a, tf->ebx, tf->ecx, tf->edx));
    case GLOS_FN_AUDIO:
        if (tf->ebx == 1)
            return done(tf, mixer_close(a, tf->ecx));
        if (tf->ebx == 2)
            return done(tf, mixer_volume(a, tf->ecx, tf->edx));
        if (tf->ebx != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        if ((r = mixer_open(a, tf->ecx, tf->edx, tf->esi, &off, &bytes)) != 0)
            return fail(tf, (u32)r);
        tf->eax = off;                          /* (its id) */
        tf->ebx = bytes;                        /* (its header's address in the app) */
        return 0;
    case GLOS_FN_CONFIG:
        return config_fn(tf);
    case GLOS_FN_RUN: {
        struct glos_run run;
        u32 seq;
        if (get(tf, tf->ebx, &run, sizeof run) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        run.command[sizeof run.command - 1] = 0;
        run.dir[sizeof run.dir - 1] = 0;
        if ((r = agent_post_app(a, &run, &seq)) != 0)
            return fail(tf, (u32)r);
        tf->eax = seq;
        return 0;
    }
    case GLOS_FN_JOB:
        return job_fn(a, tf);
    case GLOS_FN_NET:
        return sock_api(a, tf);
    case GLOS_FN_SHELL:
        desk_shell(a, tf->ebx != 0);
        return done(tf, 0);
    case GLOS_FN_STAT: {
        static u8 buf[2048];                    /* (one app's call at a time: the lock below) */
        static struct waitq q;
        static struct thread *owner;
        u32 n = 0, f = irq_save();
        while (owner && owner != current)
            thread_block(&q);
        owner = current;
        irq_restore(f);
        if (tf->ebx == GLOS_STAT_SYSTEM)
            n = stat_system(buf, sizeof buf);
        else if (tf->ebx == GLOS_STAT_APPS)
            n = stat_apps(buf, sizeof buf);
        else if (tf->ebx == GLOS_STAT_LOG)
            n = klog_read((char *)buf, sizeof buf);
        if (n > tf->edx)
            n = tf->edx;
        r = n && user_wr((u16)tf->ds, tf->ecx, buf, n) != 0;
        f = irq_save();
        owner = 0;
        thread_wake(&q);
        irq_restore(f);
        if (r || tf->ebx > GLOS_STAT_LOG)
            return fail(tf, GLOS_E_ARGUMENT);
        tf->eax = n;
        return 0;
    }
    case GLOS_FN_KILL:
        if (tf->ebx == 0)
            app_kill_id(tf->ecx, "monitor");
        else if (tf->ebx == 1)
            return done(tf, agent_kill_seq(tf->ecx) == 0 ? 0 : GLOS_E_ARGUMENT);
        else if (tf->ebx == 2)
            return done(tf, agent_kill() == 0 ? 0 : GLOS_E_ARGUMENT);
        else
            return fail(tf, GLOS_E_ARGUMENT);
        return done(tf, 0);
    case GLOS_FN_WATCH: {
        char dir[80];
        u32 i, id;
        if (tf->ebx == 1)
            return done(tf, watch_remove(a, tf->ecx));
        for (i = 0; i < sizeof dir - 1; i++)
            if (user_rd((u16)tf->ds, tf->ecx + i, &dir[i], 1) != 0)
                return fail(tf, GLOS_E_ARGUMENT);
            else if (!dir[i])
                break;
        dir[i] = 0;
        if ((r = watch_add(a, dir, &id)) != 0)
            return fail(tf, (u32)r);
        tf->eax = id;
        return 0;
    }
    case GLOS_FN_CLIP:
        if (tf->ebx == 0)
            return done(tf, clip_set_user((u16)tf->ds, tf->ecx, tf->edx) == 0 ? 0 : GLOS_E_ARGUMENT);
        if (tf->ebx != 1)
            return fail(tf, GLOS_E_ARGUMENT);
        if (clip_get_user((u16)tf->ds, tf->ecx, tf->edx, &bytes) != 0)
            return fail(tf, GLOS_E_ARGUMENT);
        tf->eax = bytes;
        return 0;
    }
    return 0;
}
