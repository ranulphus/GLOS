/* The PC's own screen and keyboard as a terminal of GLOS's shell (docs/v1/shell.md, step S2d): with no
 * desktop, a session at the text screen, as DOS has its prompt there.
 *
 * It is a session of its own, like each SSH login's and each Terminal window's (kernel/shell/shsess.c); this
 * file is its terminal. What the session writes goes through the xterm screen the Terminal app has
 * (apps/term/vt.c, built here for a text screen's sizes) into the VGA's text memory: a cell's character in
 * DOS's code page, its colours the nearest of the sixteen. What is typed comes from the keyboard's bytes
 * (kernel/vm/vkbc.c: the real keyboard's, and glos key's and glos type's), made characters by the keyboard's
 * layout (keymap.h) and bytes as a terminal sends keys.
 *
 * The screen and the keyboard are the terminal's only while no DOS program has them: one with no terminal (an
 * SSH command's), or a full-screen one, which every program started at this prompt is from its start, as under
 * DOS. While one has them nothing is drawn and the keys are its. When it has gone the screen is taken over as
 * it stands, the program's last output on it and the cursor where the BIOS has it, and the session goes on
 * below: its own prompt after its own program, its prompt again after another session's.
 *
 * Kernel messages (the owner, 2026-11-14): the start-up lines first (how the PC is reached, what was refused),
 * then the prompt; after that the log stays in LOG, and only what the person at the PC should know is shown, a
 * line above the prompt (lcon_note), never among a program's output.
 *
 * [shell] prompt = off keeps the screen as it was before S2d: a line that says GLOS runs (vm/vcon.c). */
#include <string.h>

#include "glos/api.h"
#include "glos/bootinfo.h"
#include "glos/cp.h"
#include "glos/region.h"
#include "agent.h"
#include "display.h"
#include "io.h"
#include "kcfg.h"
#include "keymap.h"
#include "kprintf.h"
#include "lcon.h"
#include "mm.h"
#include "nic.h"
#include "sched.h"
#include "session.h"
#include "shsess.h"
#include "timer.h"
#include "vm.h"
#include "../../apps/term/vt.h"

enum { M_LSHIFT = 1, M_RSHIFT = 2, M_LCTRL = 4, M_RCTRL = 8, M_LALT = 16, M_RALT = 32 };

static struct {
    struct waitq wq;
    struct shsess *s;                   /* the session at the screen now */
    struct vt *vt;
    volatile u8 ready;                  /* the VM has reached its idle screen once: DOS is there to ask */
    volatile u8 on;                     /* the thread has the screen's terminal going (keys may be its) */
    volatile u8 off;                    /* [shell] prompt = off, or GLOS is leaving */
    u8 had;                             /* the screen was ours at the last turn */
    u8 want_net;                        /* the start-up lines had no address to say: said when there is one */
    u32 seen;                           /* agent_vm_screen_had() then: programs that had it and ended */
    volatile u8 busy;                   /* what the session wrote is on its way to the screen */
    u8 kq[128];                         /* the keyboard's bytes, yet to be made keys */
    volatile u32 kh, kt;
    u8 mods, caps, num, e0;
    int page;                           /* DOS's code page: the text screen's characters */
    char pending[4][128];               /* notices from before the session was there */
    volatile u8 npending;
} l;

/* ---- the screen */

struct geom {
    u16 *scr;
    u32 cols, rows, page;
};

static int geom(struct geom *g)
{
    u8 mode = session_bios_mode();
    g->page = vm_rd8(0x462);
    g->cols = vm_rd8(0x44A) | (u32)vm_rd8(0x44B) << 8;
    g->rows = vm_rd8(0x484) + 1u;
    if ((mode != 2 && mode != 3 && mode != 7) || g->cols < 40 || g->cols > VT_COLS_MAX || g->rows < 25
        || g->rows > VT_ROWS_MAX || g->page > 7)
        return 0;
    g->scr = (u16 *)vm_ptr((mode == 7 ? 0xB0000u : 0xB8000u) + g->page * (vm_rd8(0x44C) | (u32)vm_rd8(0x44D) << 8));
    return 1;
}

/* The screen and the keyboard are the terminal's now (any context). */
static int ours(void)
{
    u8 mode;
    if (l.off || display_desktop_bpp() || display_fb() || agent_vm_screen())
        return 0;
    mode = session_bios_mode();
    return mode == 2 || mode == 3 || mode == 7;
}

/* A colour of the sixteen as xterm numbers it and as the VGA does: red and blue change places. */
static unsigned rb(unsigned c) { return (c & 0xA) | (c & 1) << 2 | (c & 4) >> 2; }

static u8 vga16(unsigned c, int fg)
{
    unsigned rgb, r, g, b;
    if (c == VT_DEFAULT)
        return fg ? 7 : 0;
    if (c < 16)
        return (u8)rb(c);
    rgb = vt_rgb((unsigned short)c, fg);
    r = rgb >> 16 & 255, g = rgb >> 8 & 255, b = rgb & 255;
    return (u8)((r > 96 ? 4 : 0) | (g > 96 ? 2 : 0) | (b > 96 ? 1 : 0) | (r > 200 || g > 200 || b > 200 ? 8 : 0));
}

/* The cursor where the terminal has it: the BIOS's note of it and the card's (off the screen's end when hidden). */
static void cursor(const struct geom *g)
{
    const struct vt *t = l.vt;
    u32 x = (u32)t->x, y = (u32)t->y, at;
    u16 crtc = (u16)(vm_rd8(0x463) | (u32)vm_rd8(0x464) << 8);
    u32 f = irq_save();
    vm_wr8(0x450 + g->page * 2u, (u8)x);
    vm_wr8(0x451 + g->page * 2u, (u8)y);
    at = (vm_rd8(0x44E) | (u32)vm_rd8(0x44F) << 8) / 2 + (t->cursor_on ? y * g->cols + x : g->rows * g->cols);
    if (crtc == 0x3D4 || crtc == 0x3B4) {
        outb(crtc, 0x0E);
        outb((u16)(crtc + 1), (u8)(at >> 8));
        outb(crtc, 0x0F);
        outb((u16)(crtc + 1), (u8)at);
    }
    irq_restore(f);
}

/* The rows that changed, into the text memory. */
static void render(void)
{
    struct vt *t = l.vt;
    struct geom g;
    int x, y;
    if (!geom(&g) || (int)g.cols != t->cols || (int)g.rows != t->rows)
        return;
    for (y = 0; y < t->rows; y++) {
        if (!t->dirty[y])
            continue;
        t->dirty[y] = 0;
        for (x = 0; x < t->cols; x++) {
            const struct vt_cell *c = &t->cell[y * VT_COLS_MAX + x];
            int b = c->ch < 0x80 ? (int)c->ch : cp_byte(l.page, c->ch);
            u8 fg = vga16(c->fg, 1), bg = vga16(c->bg, 0) & 7, k;
            if (c->attr & VT_BOLD)
                fg |= 8;
            if (c->attr & VT_REVERSE)
                k = fg & 7, fg = (u8)(bg | (fg & 8)), bg = k;
            if (c->attr & VT_HIDDEN)
                fg = bg;
            g.scr[(u32)y * g.cols + (u32)x] = (u16)((u8)(b < 0 ? '?' : b ? b : ' ') | (u32)(bg << 4 | fg) << 8);
        }
    }
    cursor(&g);
}

/* The screen taken over as it stands: its cells and the BIOS's cursor are the terminal's from here. 0 if it is
   no text screen the terminal can have. */
static int adopt(void)
{
    struct vt *t = l.vt;
    struct geom g;
    u32 x, y, cx, cy;
    if (!geom(&g))
        return 0;
    if ((int)g.cols != t->cols || (int)g.rows != t->rows) {
        vt_resize(t, (int)g.cols, (int)g.rows);
        if (l.s)
            shsess_resize(l.s, g.cols, g.rows);
    }
    for (y = 0; y < g.rows; y++)
        for (x = 0; x < g.cols; x++) {
            u16 v = g.scr[y * g.cols + x];
            unsigned fg = v >> 8 & 15, bg = v >> 12 & 7;
            vt_set(t, (int)x, (int)y, (v & 0xFF) ? cp_char(l.page, (u8)v) : ' ', fg == 7 ? VT_DEFAULT : rb(fg),
                   bg ? rb(bg) : VT_DEFAULT);
        }
    memset(t->dirty, 0, sizeof t->dirty);
    cx = vm_rd8(0x450 + g.page * 2u), cy = vm_rd8(0x451 + g.page * 2u);
    vt_goto(t, cx < g.cols ? (int)cx : 0, cy < g.rows ? (int)cy : (int)g.rows - 1);
    if (t->x)                                   /* (a program's last line had no end: ours start on a fresh row) */
        vt_feed(t, (const unsigned char *)"\r\n", 2);
    return 1;
}

static void say(const char *text)
{
    vt_feed(l.vt, (const unsigned char *)text, (int)strlen(text));
}

/* ---- the keyboard */

int lcon_key(u8 b)
{
    if (!l.on || !ours() || agent_vm_app_keys(l.s))     /* (a GLOS app someone else started, with no terminal:
                                                   it reads the keyboard through DOS, as before the prompt) */
        return 0;
    if (l.kt - l.kh < sizeof l.kq)
        l.kq[l.kt++ % sizeof l.kq] = b;
    thread_wake(&l.wq);
    return 1;
}

/* Caps Lock and Num Lock as the BIOS has them, there for the next DOS program as they are here. */
static void locks_out(void)
{
    u32 f = irq_save();
    vm_wr8(0x417, (u8)((vm_rd8(0x417) & ~0x60) | (l.caps ? 0x40 : 0) | (l.num ? 0x20 : 0)));
    irq_restore(f);
}

static void key_byte(u8 b)
{
    u32 code;
    int down, layout, n;
    unsigned ch, mods;
    char out[16];
    u8 mod = 0;
    if (b == 0xE0) {
        l.e0 = 1;
        return;
    }
    code = (b & 0x7Fu) | (l.e0 ? 0x100u : 0);
    down = !(b & 0x80);
    l.e0 = 0;
    switch (code) {
    case 0x2A: mod = M_LSHIFT; break;
    case 0x36: mod = M_RSHIFT; break;
    case 0x1D: mod = M_LCTRL; break;
    case 0x11D: mod = M_RCTRL; break;
    case 0x38: mod = M_LALT; break;
    case 0x138: mod = M_RALT; break;
    case 0x12A: case 0x136: return;             /* (the shifts a keyboard makes up round the grey keys) */
    case 0x3A: if (down) l.caps ^= 1, locks_out(); return;
    case 0x45: if (down) l.num ^= 1, locks_out(); return;
    }
    if (mod) {
        l.mods = (u8)(down ? l.mods | mod : l.mods & ~mod);
        return;
    }
    if (!down || !l.s)
        return;
    layout = kcfg_layout();
    ch = keymap_char_in(layout, code, (l.mods & (M_LSHIFT | M_RSHIFT)) != 0, (l.mods & (M_LCTRL | M_RCTRL)) != 0,
                        (l.mods & M_LALT) != 0, (l.mods & M_RALT) != 0, l.caps, l.num);
    mods = (l.mods & (M_LSHIFT | M_RSHIFT) ? GLOS_MOD_SHIFT : 0u) | (l.mods & (M_LCTRL | M_RCTRL) ? GLOS_MOD_CTRL : 0u)
        | (l.mods & (keymap_altgr(layout) ? M_LALT : M_LALT | M_RALT) ? GLOS_MOD_ALT : 0u);
    n = vt_key(l.vt, code, ch, mods, out, sizeof out);
    if (n > 0)
        shsess_write(l.s, (const u8 *)out, (u32)n);
}

/* ---- notices */

void lcon_note(const char *text)
{
    u32 f = irq_save();
    if (l.s && l.on) {
        shsess_note(l.s, text);
    } else if (!l.off && l.npending < 4) {      /* (before the prompt is there: among the start-up lines) */
        u32 n = strlen(text);
        if (n >= sizeof l.pending[0])
            n = sizeof l.pending[0] - 1;
        memcpy(l.pending[l.npending], text, n);
        l.pending[l.npending++][n] = 0;
    }
    irq_restore(f);
}

void lcon_network(const char *ip, const char *name)
{
    char line[128];
    if (!l.on)                                  /* (before the prompt: the start-up lines say it, or the first
                                                   turns after them) */
        return;
    l.want_net = 0;
    ksnprintf(line, sizeof line, ip ? "The network is up: this PC is %s.local, %s." : "The network has gone: no address.",
              name, ip);
    lcon_note(line);
}

/* A DOS program is about to have the screen (the VM thread, interrupts off; kernel/dos/agent.c): nothing more is
   drawn from here, and if the cursor stands in a row with something on it (the prompt, a line half typed) the
   program begins on the next, as if Enter had been pressed. */
void lcon_program(void)
{
    struct geom g;
    u32 col, row, i;
    if (!l.on || !l.had || !geom(&g))
        return;
    l.had = 0;
    col = vm_rd8(0x450 + g.page * 2u), row = vm_rd8(0x451 + g.page * 2u);
    if (!col || col >= g.cols || row >= g.rows)
        return;
    if (++row == g.rows) {                      /* (up a line, as the BIOS's teletype scrolls) */
        for (i = 0; i < (g.rows - 1) * g.cols; i++)
            g.scr[i] = g.scr[i + g.cols];
        for (i = 0; i < g.cols; i++)
            g.scr[(g.rows - 1) * g.cols + i] = 0x0720;
        row = g.rows - 1;
    }
    vm_wr8(0x450 + g.page * 2u, 0);
    vm_wr8(0x451 + g.page * 2u, (u8)row);
}

/* The start-up lines: how the PC is reached; what GLOS refused as it started (the log's refuse lines, as they
   are); what was noted before the prompt was there. */
static void greet(void)
{
    static char log[8192];
    char ip[16], line[200];
    const char *name, *p, *e;
    u32 n, k;
    l.want_net = !net_address(ip, &name);
    if (!l.want_net)
        ksnprintf(line, sizeof line, "GLOS is running on this PC: %s.local, %s.\r\n", name, ip);
    else
        ksnprintf(line, sizeof line, "GLOS is running on this PC (%s; no network address yet).\r\n", name);
    say(line);
    n = klog_read(log, sizeof log - 1);
    log[n] = 0;
    for (p = log; (e = strchr(p, '\n')) != 0; p = e + 1) {
        const char *r = 0;
        u32 m = 0;
        for (k = 0; p + k + 8 < e && !r; k++)
            if (!memcmp(p + k, " refuse ", 8))
                r = p + k + 8;
        if (!r || memcmp(p, "GLOS-", 5))
            continue;
        for (; r < e && m < sizeof line - 1; r++)       /* (GLOS-NET refuse card=... reason=...: what follows) */
            line[m++] = *r;
        line[m] = 0;
        say("Not used: ");
        say(line);
        say(" (LOG has more).\r\n");
    }
    for (k = 0; k < l.npending; k++) {
        say(l.pending[k]);
        say("\r\n");
    }
    l.npending = 0;
}

/* ---- the thread */

static void kick(void *arg);
static void nap(u32 ticks);

/* AUTOEXEC.BAT, when GLOS is DOS's shell with the shell its own (docs/v1/shell.md, S4d): the line that runs it. */
static const char *boot_line(void)
{
    static char line[96];
    ksnprintf(line, sizeof line, "CALL %s", vm.bi->autoexec);
    return line;
}

/* ... run with no terminal, where the screen is not a text screen of ours to show it on ([shell] prompt = off,
   the desktop): what it writes is dropped, its resident programs load and its variables are the system's. */
static void boot_headless(void)
{
    struct shsess *s;
    u8 drop[256];
    int code;
    vm.autoexec = 2;
    kprintf("GLOS-SHELL autoexec=%s terminal=none\n", vm.bi->autoexec);
    if (!(s = shsess_boot(0, 0, boot_line(), kick, 0))) {
        vm.autoexec = 0;
        return;
    }
    while (!shsess_ended(s, &code)) {
        while (shsess_read(s, 0, drop, sizeof drop) || shsess_read(s, 1, drop, sizeof drop))
            ;
        nap(64);
    }
    shsess_free(s);
    vm.autoexec = 0;
}

static void kick(void *arg)                     /* (the session's thread: it has written, or ended) */
{
    u32 f = irq_save();
    (void)arg;
    thread_wake(&l.wq);
    irq_restore(f);
}

static void nap(u32 ticks)
{
    u32 f = irq_save();
    thread_wait(&l.wq, timer_ticks() + ticks);
    irq_restore(f);
}

/* One turn at the terminal: the keys typed, what the session wrote, the screen. */
static void turn(void)
{
    u8 buf[512];
    u32 n, f, had;
    void *whose;
    if (!ours()) {
        l.had = 0;
        return;
    }
    if (l.want_net && l.s) {                    /* (an address since the start-up lines) */
        char ip[16];
        const char *name;
        if (net_address(ip, &name))
            lcon_network(ip, name);
    }
    had = agent_vm_screen_had(&whose);          /* (a program that came and went between two turns too) */
    if (had != l.seen)
        l.had = 0;
    if (!l.had) {
        if (!adopt())
            return;
        l.had = 1;
        l.seen = had;
        f = irq_save();
        l.kh = l.kt;                            /* (keys from before are not ours) */
        l.mods = (u8)((vm_rd8(0x417) & 2 ? M_LSHIFT : 0) | (vm_rd8(0x417) & 1 ? M_RSHIFT : 0));
        l.caps = (vm_rd8(0x417) & 0x40) != 0, l.num = (vm_rd8(0x417) & 0x20) != 0;
        l.e0 = 0;
        irq_restore(f);
        if (l.s && whose != l.s)
            shsess_redraw(l.s);                 /* (another session's program wrote over the prompt: the prompt
                                                   again, below. After its own program the session writes its
                                                   prompt itself) */
    }
    for (;;) {
        u8 b;
        f = irq_save();
        if (l.kh == l.kt) {
            irq_restore(f);
            break;
        }
        b = l.kq[l.kh++ % sizeof l.kq];
        irq_restore(f);
        key_byte(b);
    }
    l.busy = 1;
    while (l.s && (n = shsess_read(l.s, 0, buf, sizeof buf)) != 0)
        vt_feed(l.vt, buf, (int)n);
    if (l.vt->reply_n && l.s) {                 /* (the terminal's answers: where the cursor is, what it is) */
        shsess_write(l.s, (const u8 *)l.vt->reply, (u32)l.vt->reply_n);
        l.vt->reply_n = 0;
    }
    l.vt->bell = l.vt->title_set = 0;
    render();
    l.busy = 0;
}

/* The session's thread, about to start a program that will have the screen: what the session wrote before it
   (the typed line's end) is on the screen first, so that the program's first line begins below it. */
void lcon_settle(struct shsess *s)
{
    u32 n;
    for (n = 0; n < 200 && l.on && l.s == s && (shsess_waiting(s) || l.busy); n++) {
        u32 f = irq_save();
        thread_wake(&l.wq);
        irq_restore(f);
        thread_sleep_until(timer_ticks() + 2);
    }
}

static void lcon_thread(void *arg)
{
    char v[16];
    u32 t0;
    (void)arg;
    while (!l.ready || !kcfg_settled())         /* DOS at its idle screen, GLOS.CFG read */
        nap(64);
    kcfg_get("shell", "prompt", v, sizeof v);
    if (!strcmp(v, "off") || !(l.vt = kmalloc(sizeof *l.vt))) {
        l.off = 1;
        if (vm.autoexec == 1)
            boot_headless();
        thread_exit();
    }
    l.page = kcfg_region()->codepage ? kcfg_region()->codepage : 437;
    for (t0 = timer_ticks(); nic0 && (s32)(timer_ticks() - t0) < 4 * 1024;) {   /* (a card's address, if DHCP is quick) */
        char ip[16];
        const char *name;
        if (net_address(ip, &name))
            break;
        nap(64);
    }
    while (!l.off) {
        struct geom g;
        int code;
        u32 waited = 0;
        while (!ours() || !geom(&g)) {          /* (a program has the screen already: when it has gone) */
            if (l.off)
                thread_exit();
            if (vm.autoexec == 1 && (display_desktop_bpp() || display_fb() || ++waited > 80))
                boot_headless();                /* (the desktop's screen, or one that is no text screen of ours
                                                   after ten seconds: AUTOEXEC.BAT without a terminal) */
            nap(128);
        }
        vt_init(l.vt, (int)g.cols, (int)g.rows);
        if (!adopt()) {
            nap(128);
            continue;
        }
        greet();
        l.had = 1;
        {
            void *whose;
            l.seen = agent_vm_screen_had(&whose);
        }
        l.kh = l.kt;
        if (vm.autoexec == 1) {                 /* DOS's shell: AUTOEXEC.BAT first, here, then the prompt */
            vm.autoexec = 2;
            kprintf("GLOS-SHELL autoexec=%s\n", vm.bi->autoexec);
            if (!(l.s = shsess_boot(g.cols, g.rows, boot_line(), kick, 0)))
                vm.autoexec = 0;
        } else {
            l.s = shsess_local(g.cols, g.rows, kick, 0);
        }
        if (!l.s) {
            say("GLOS's shell could not start here (no memory, or DOS has none to spare).\r\n");
            render();
            l.off = 1;
            break;
        }
        l.on = 1;
        while (!l.off && !(shsess_ended(l.s, &code) && !shsess_waiting(l.s))) {
            turn();
            nap(256);
        }
        turn();
        l.on = 0;
        {                                       /* EXIT at the PC's own screen: there is nowhere to go, so a new
                                                   session (SHUTDOWN leaves GLOS, REBOOT restarts the PC) */
            u32 f = irq_save();
            struct shsess *s = l.s;
            l.s = 0;
            irq_restore(f);
            shsess_free(s);
        }
    }
    thread_exit();
}

void lcon_poke(void)                            /* (any context) */
{
    u32 f = irq_save();
    thread_wake(&l.wq);
    irq_restore(f);
}

void lcon_ready(void)
{
    if (!l.ready) {
        u32 f = irq_save();
        l.ready = 1;
        thread_wake(&l.wq);
        irq_restore(f);
    }
}

int lcon_on(void) { return !l.off; }

void lcon_leave(void)
{
    u32 f = irq_save();
    l.off = 1;
    l.on = 0;
    thread_wake(&l.wq);
    irq_restore(f);
}

void lcon_init(void)
{
    if (vm_own_shell() && vm.bi->autoexec[0])
        vm.autoexec = 1;
    if (!(vm.bi->flags & BI_F_AGENT) || !thread_create("console", PRIO_NORMAL, lcon_thread, 0))
        l.off = 1;
}
