/* Exclusive sessions (supervisor.md §11; M4e).
 *
 * A session is a top-level program: what GLOS.EXE's stub EXECs (an agent
 * job, the /RUN program), or, as the shell, what the console COMMAND.COM
 * EXECs. Its own EXECs belong to it. It begins at that INT 21h 4B00h (of a
 * file that is there) and ends when its parent asks for its exit code (INT
 * 21h 4Dh, which the stub and COMMAND.COM both call), or at the parent's
 * next EXEC or the stub's next NEXT when the EXEC failed.
 *
 * At the end GLOS puts back what a program leaves behind: the video mode it
 * began in, the virtual PIC's masks, PIT channel 0 (mode 2, FFFFh), the
 * virtual RTC's A and B, the 8042's command byte and the keyboard LEDs; it
 * stops Sound Blaster DMA; it puts back interrupt vectors the session
 * changed that now point into free memory, or still point where a program
 * left them in its own memory when it ended (a TSR's hooks stay, with its
 * memory); and it sets the BIOS clock from the RTC, which a program that
 * sped up the PIT has run fast.
 *
 * A session takes the profile (GLOS.CFG's [program NAME.EXT], §11.2) of the
 * program that begins it: its env lines go into that EXEC's environment (a
 * copy GLOS makes in DOS memory the parent owns, freed at the end), and its
 * memory caps the DPMI context the session makes; direct = 1 (or GLOS.EXE's
 * /DIRECT) runs the session in direct mode, at IOPL 3 (§9.7, v86.c). */
#include <string.h>

#include "glos/bootinfo.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "session.h"
#include "timer.h"
#include "vm.h"

static struct session {
    u8 active;
    u16 parent;                                 /* the PSP that EXECed it */
    char prog[13];                              /* its file name */
    u8 mode, leds, rtc_a, rtc_b, kbc_cmd;
    u8 imr[2];
    u32 ivt[256];
    u32 dangle[256];                            /* what the program left pointing into its own memory, or 0 */
    const struct bi_profile *prof;              /* its profile, or 0 */
    u8 direct;                                  /* glos run --direct */
    u16 env_seg, env_old;                       /* the environment GLOS made, and the EXEC block's own */
    u32 env_pb;                                 /* the EXEC block (linear) */
    u32 t0;
    u32 n;
} ses;

static int is_session_parent(u16 psp)
{
    if (!psp)
        return 0;
    if (vm.bi->flags & BI_F_SHELL) {            /* the console (or AUTOEXEC.BAT's) COMMAND.COM, which */
        u16 parent = vm_rd16(psp * 16u + 0x16); /* FreeCOM makes its own parent */
        return psp != vm.loader_psp && (parent == vm.loader_psp || parent == psp);
    }
    return psp == vm.loader_psp;                /* the stub: agent jobs, /RUN */
}

/* A real-mode INT on the stub's spare stack (the kill stub's, 256 bytes),
   from a trap in V86 mode: 0, or -1 if it couldn't be made. */
static int bios(struct rmregs *r, u8 vec)
{
    r->ss = (u16)vm.loader_cs;
    r->sp = (u16)vm.bi->kill_sp;
    r->flags = 2;
    return rm_call(0, r, RM_INT, vec, 0, 0);
}

static u32 bcd(u8 v) { return (v >> 4) * 10u + (v & 15); }

/* The owner of the DOS block a real-mode vector points into: its PSP, 0 for
   a free block, or FFFFh for none (below the arena or past its end: DOS,
   drivers, ROM). */
static u16 owner(u32 vec)
{
    u32 lin = ((vec >> 16) << 4) + (vec & 0xFFFF), mcb, n;
    if (!vm.bi->lol)
        return 0xFFFF;
    mcb = (u32)vm_rd16(vm.bi->lol - 2) << 4;
    if (lin < mcb)
        return 0xFFFF;
    for (n = 0; n < 4096; n++) {
        u8 type = vm_rd8(mcb);
        u16 own = vm_rd16(mcb + 1);
        u32 size = (u32)vm_rd16(mcb + 3) << 4;
        if (type != 'M' && type != 'Z')
            return 0xFFFF;                      /* (a broken chain: leave the vector be) */
        if (lin >= mcb + 16 && lin < mcb + 16 + size)
            return own;
        if (type == 'Z')
            return 0xFFFF;
        mcb += 16 + size;
    }
    return 0xFFFF;
}

/* The BIOS video mode back to mode (INT 10h, and so the palette) if the
   program left another: 1 if it did. */
static int remode_to(u8 mode)
{
    struct rmregs r;
    if ((vm_rd8(0x449) & 0x7F) == (mode & 0x7F))
        return 0;
    memset(&r, 0, sizeof r);
    r.eax = mode & 0x7F;
    bios(&r, 0x10);
    return 1;
}

/* Sound Blaster DMA stopped: channels 1 and 3, 5 to 7 masked, and the DSP
   reset at BLASTER's A. */
static void sb_stop(void)
{
    outb(0x0A, 0x05);
    outb(0x0A, 0x07);
    outb(0xD4, 0x05);
    outb(0xD4, 0x06);
    outb(0xD4, 0x07);
    if (vm.bi->sb_port) {
        u32 i;
        outb((u16)(vm.bi->sb_port + 6), 1);
        for (i = 0; i < 100; i++)
            inb(0x80);
        outb((u16)(vm.bi->sb_port + 6), 0);
    }
}

/* The BIOS clock from the RTC (INT 1Ah 02h, then 01h): the tick count set,
   or 0. A program that sped the PIT up has run the DOS clock fast. */
static u32 clock_from_rtc(void)
{
    struct rmregs r;
    u32 s, ticks;
    memset(&r, 0, sizeof r);
    r.eax = 0x0200;
    if (bios(&r, 0x1A) != 0 || (r.flags & FL_CF))
        return 0;
    s = bcd((u8)(r.ecx >> 8)) * 3600 + bcd((u8)r.ecx) * 60 + bcd((u8)(r.edx >> 8));
    ticks = (u32)(((unsigned long long)(s * 2 + 1) * 1193180ull) >> 17);   /* (the middle of that second) */
    memset(&r, 0, sizeof r);
    r.eax = 0x0100;
    r.ecx = ticks >> 16;
    r.edx = ticks & 0xFFFF;
    bios(&r, 0x1A);
    return ticks;
}

/* A kill (or an abort) has put back what the killed program found at its
   EXEC (v86.c snap_restore): also the video mode and lock bits it found, Sound
   Blaster DMA and the BIOS clock, as a session's end does (E5). */
void session_kill(u8 mode, u8 leds)
{
    int remode = remode_to(mode);
    vm_wr8(0x417, (u8)((vm_rd8(0x417) & ~0x70) | leds));
    sb_stop();
    kprintf("GLOS-SESSION kill-restore mode=%02x%s ticks=%u\n", mode, remode ? " remode=1" : "", clock_from_rtc());
}

static void end(const char *why)
{
    struct rmregs r;
    u32 v, fixed = 0, ticks;
    u8 mode = vm_rd8(0x449);
    int remode;

    ses.active = 0;
    vm_direct(0);                               /* (before the BIOS calls: they run at IOPL 0) */
    if (ses.env_seg) {                          /* the profile's environment: DOS has copied it */
        if (vm_rd16(ses.env_pb) == ses.env_seg) {
            vm_wr8(ses.env_pb, (u8)ses.env_old);
            vm_wr8(ses.env_pb + 1, (u8)(ses.env_old >> 8));
        }
        memset(&r, 0, sizeof r);
        r.eax = 0x4900;
        r.es = ses.env_seg;
        bios(&r, 0x21);
        ses.env_seg = 0;
    }
    remode = remode_to(ses.mode);               /* the mode it began in, and so the palette */
    outb(0x43, 0x34);                           /* PIT channel 0: mode 2, count FFFFh (§8.2) */
    outb(0x40, 0xFF);
    outb(0x40, 0xFF);
    vm.pic.p[0].imr = ses.imr[0];
    vm.pic.p[1].imr = ses.imr[1];
    vm_sync_mask();
    vm.rtc_a = ses.rtc_a;
    vm.rtc_b = ses.rtc_b;
    timer_write_b(vm.rtc_b);
    vkbc_restore(ses.kbc_cmd);
    vm_wr8(0x417, (u8)((vm_rd8(0x417) & ~0x70) | ses.leds));
    sb_stop();
    for (v = 0; v < 256; v++) {                 /* hooks left in memory DOS has taken back (which the */
        u32 cur = vm_rd32(v * 4u);              /* shell may have loaded something into since) */
        if (cur != ses.ivt[v] && (owner(cur) == 0 || cur == ses.dangle[v])) {
            vm_wr8(v * 4u, (u8)ses.ivt[v]);
            vm_wr8(v * 4u + 1, (u8)(ses.ivt[v] >> 8));
            vm_wr8(v * 4u + 2, (u8)(ses.ivt[v] >> 16));
            vm_wr8(v * 4u + 3, (u8)(ses.ivt[v] >> 24));
            fixed++;
        }
    }
    ticks = clock_from_rtc();
    kprintf("GLOS-SESSION end n=%u prog=%s why=%s mode=%02x%s vectors=%u ticks=%u t=%u\n", ses.n, ses.prog, why, mode,
            remode ? " remode=1" : "", fixed, ticks, timer_ticks() - ses.t0);
}

/* glos run's options for the session the stub's next EXEC begins (E4). */
static struct { u8 direct; char profile[16]; } job;

void session_job(int direct, const char *profile)
{
    u32 i;
    job.direct = (u8)(direct != 0);
    for (i = 0; profile[i] && i < sizeof job.profile - 1; i++)
        job.profile[i] = profile[i] >= 'a' && profile[i] <= 'z' ? (char)(profile[i] - 32) : profile[i];
    job.profile[i] = 0;
}

/* The profile for program file name prog (upper case): the same NAME.EXT,
   or the same NAME when either has no extension. */
static const struct bi_profile *profile_for(const char *prog)
{
    const struct bi_profile *p;
    u32 n, m;
    for (n = 0; prog[n] && prog[n] != '.'; n++) ;
    for (p = vm.bi->profiles; p < vm.bi->profiles + BI_PROFILES; p++) {
        for (m = 0; p->name[m] && p->name[m] != '.'; m++) ;
        if (p->name[0] && (!strcmp(p->name, prog)
                           || (m == n && !strncmp(p->name, prog, n) && (!p->name[m] || !prog[n]))))
            return p;
    }
    return 0;
}

/* Two environment strings set the same variable (case aside). */
static int same_var(const char *a, const char *b)
{
    for (; *a && *a != '=' && *b && *b != '='; a++, b++)
        if (*a != *b && !((*a | 0x20) == (*b | 0x20) && (*a | 0x20) >= 'a' && (*a | 0x20) <= 'z'))
            return 0;
    return *a == '=' && *b == '=';
}

/* The profile's env lines into the environment the EXEC at tf gives the
   program: a copy of the one it names (or the parent's), without the
   variables the profile sets, then the profile's lines. */
static void profile_env(const struct trapframe *tf, u16 psp)
{
    static char buf[8192];
    struct rmregs r;
    u32 pb = vm_lin(tf->v86_es, tf->ebx), src, n = 0, i, len;
    const char *e;
    u16 seg = vm_rd16(pb);

    src = (u32)(seg ? seg : vm_rd16(psp * 16u + 0x2C)) << 4;
    for (i = 0; src && i < 32768 && vm_rd8(src + i);) {        /* its strings, but those the profile sets */
        const char *s = (const char *)vm_ptr(src + i);
        int keep = 1;
        len = (u32)strlen(s) + 1;
        for (e = ses.prof->env; *e && keep; e += strlen(e) + 1)
            keep = !same_var(s, e);
        if (keep) {
            if (n + len > sizeof buf - BI_PROF_ENV - 2) {
                kprintf("GLOS-WARN session-env prog=%s why=too-big\n", ses.prog);
                return;
            }
            memcpy(buf + n, s, len);
            n += len;
        }
        i += len;
    }
    for (e = ses.prof->env; *e; e += len) {     /* then the profile's */
        len = (u32)strlen(e) + 1;
        memcpy(buf + n, e, len);
        n += len;
    }
    buf[n++] = 0;
    memset(&r, 0, sizeof r);
    r.eax = 0x4800;
    r.ebx = (n + 15) >> 4;
    if (bios(&r, 0x21) != 0 || (r.flags & FL_CF)) {
        kprintf("GLOS-WARN session-env prog=%s why=no-memory\n", ses.prog);
        return;
    }
    ses.env_seg = (u16)r.eax;
    ses.env_pb = pb;
    ses.env_old = seg;
    memcpy(vm_ptr((u32)ses.env_seg << 4), buf, n);
    vm_wr8(pb, (u8)ses.env_seg);
    vm_wr8(pb + 1, (u8)(ses.env_seg >> 8));
}

/* INT 21h 4B00h from V86 code (DS:DX the program). */
void session_exec(const struct trapframe *tf)
{
    struct rmregs r;
    u16 psp = vm_current_psp();
    u32 path = vm_lin(tf->v86_ds, tf->edx), i, b = 0;
    if (ses.active && psp == ses.parent)
        end("next-exec");                       /* the last one's EXEC failed */
    if (ses.active || !is_session_parent(psp) || (tf->eax & 0xFF) != 0)
        return;
    memset(&r, 0, sizeof r);                    /* no file, no session: the agent looks for a program by */
    r.eax = 0x4300;                             /* trying each place (DOS isn't entered yet: the INT */
    r.ds = (u16)tf->v86_ds;                     /* 21h trapped) */
    r.edx = tf->edx & 0xFFFF;
    if (bios(&r, 0x21) != 0 || (r.flags & FL_CF))
        return;
    for (i = 0; i < 128 && vm_rd8(path + i); i++)
        if (vm_rd8(path + i) == '\\' || vm_rd8(path + i) == ':')
            b = i + 1;
    for (i = 0; i < 12 && vm_rd8(path + b + i); i++) {
        char c = (char)vm_rd8(path + b + i);
        ses.prog[i] = c >= 'a' && c <= 'z' ? (char)(c - 32) : c;
    }
    ses.prog[i] = 0;
    ses.active = 1;
    ses.parent = psp;
    memset(ses.dangle, 0, sizeof ses.dangle);
    ses.mode = vm_rd8(0x449);
    ses.leds = vm_rd8(0x417) & 0x70;
    ses.imr[0] = vm.pic.p[0].imr;
    ses.imr[1] = vm.pic.p[1].imr;
    ses.rtc_a = vm.rtc_a;
    ses.rtc_b = vm.rtc_b;
    ses.kbc_cmd = vkbc_cmd();
    memcpy(ses.ivt, vm_ptr(0), sizeof ses.ivt);
    ses.t0 = timer_ticks();
    ses.n++;
    ses.prof = profile_for(psp == vm.loader_psp && job.profile[0] ? job.profile : ses.prog);
    ses.direct = psp == vm.loader_psp && job.direct;
    ses.env_seg = 0;
    if (ses.prof && ses.prof->env[0])
        profile_env(tf, psp);
    kprintf("GLOS-SESSION begin n=%u prog=%s parent=%04x mode=%02x%s%s%s\n", ses.n, ses.prog, psp, ses.mode,
            ses.prof ? " profile=" : "", ses.prof ? ses.prof->name : "", session_direct() ? " direct=1" : "");
    if (session_direct())
        vm_direct(1);                           /* from the return into DOS's EXEC on */
}

/* INT 20h, or INT 21h 4Ch or 00h, from V86 code: a program ending (the
   session's, or one it started). The vectors it left pointing into its own
   memory are noted: DOS is about to free that memory. */
void session_terminate(void)
{
    u16 psp = vm_current_psp();
    u32 v;
    if (!ses.active || !psp || psp == ses.parent)
        return;
    for (v = 0; v < 256; v++) {
        u32 cur = vm_rd32(v * 4u);
        if (cur != ses.ivt[v] && owner(cur) == psp)
            ses.dangle[v] = cur;
    }
}

/* INT 21h 4Dh from V86 code: the parent of a session asking how it ended. */
void session_exit_code(void)
{
    if (ses.active && vm_current_psp() == ses.parent)
        end("exit");
}

/* GLOS.EXE's stub asking for the next thing to run: anything it EXECed
   has ended (or never started). */
void session_stub_next(void)
{
    if (ses.active && ses.parent == vm.loader_psp)
        end("stub");
}

int session_active(void) { return ses.active; }

u32 session_memory_kb(void) { return ses.active && ses.prof ? ses.prof->memory_kb : 0; }

int session_direct(void)
{
    return ses.active && ((vm.bi->flags & BI_F_DIRECT) || ses.direct
                          || (ses.prof && (ses.prof->flags & BI_PROF_DIRECT)));
}

int session_profile_known(const char *name)
{
    char up[16];
    u32 i;
    for (i = 0; name[i] && i < sizeof up - 1; i++)
        up[i] = name[i] >= 'a' && name[i] <= 'z' ? (char)(name[i] - 32) : name[i];
    up[i] = 0;
    return profile_for(up) != 0;
}
