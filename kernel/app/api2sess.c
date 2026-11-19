/* NET for a program that is not an app (API 2.3; the owner, 2026-11-15: "sockets for sessions"): a full-screen
 * program, which is a plain DOS session to the kernel, may use the network calls. Asked for by a game's LAN play
 * at full-screen speed (Echoes of the Void); the DOS ports' net play is the same case.
 *
 * Such a program has no handles, events or thread of its own. Its first NET call makes it a context for those
 * calls alone: a table of handles and the sockets' owner (a struct app that is no app: never listed, never
 * run, `session` set), one for the DPMI program that asked, gone with it (api2_session_gone: its connections
 * and ports closed as an app's are at its end). A program that becomes an app (INIT) starts as an app with
 * none: what it had open before is closed then too.
 *
 * Since API 2.5 it may also open a stream out (AUDIO's OUT; the owner, 2026-11-19): GLOS's mixer plays it while
 * the program has it, full-screen too, where the card was the session's alone: an SDL game that draws by VESA
 * has sound on every card GLOS drives. kernel/drv/mixer.c keeps the card for it.
 *
 * What it has: NET's calls, AUDIO's OUT, and CLOSE, GET and SET for the handles those gave it. What it has not: events
 * (GLOS_NETF_EVENTS is taken and does nothing), and every other call, which answers GLOS_E_NOT_APP as before:
 * so does CLOSE of a handle it does not have, which is how a library tells an app from such a program
 * (libglos's glos_is_app).
 *
 * Its calls run on the system VM's thread, as all of the program does. A call that waits (kernel/net/sock.c)
 * waits there: the program's own interrupts wait with it, as an app's handlers do while its thread is in a
 * call, so a program with handlers to run uses short waits or none (a game polls). A kill ends the wait. */
#include <string.h>

#include "api2int.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "sock.h"
#include "vm.h"

static struct app *sess;                /* made at the first such call ever, and kept */
static struct dpmi_ctx *owner;          /* whose it is now; 0: nobody's */

static struct app *ctx_for(int make, const char *what)
{
    if (!dctx)
        return 0;                       /* (the API is a protected-mode program's) */
    if (owner == dctx)
        return sess;
    if (!make || owner)                 /* (another program's still: its end comes first) */
        return 0;
    if (!sess) {
        struct app *a = kmalloc(sizeof *a);
        struct api2 *v = kmalloc(sizeof *v);
        if (!a || !v) {
            kfree(a);
            kfree(v);
            return 0;
        }
        memset(a, 0, sizeof *a);
        memset(v, 0, sizeof *v);
        a->v2 = v;
        a->session = 1;
        strcpy(a->prog, "(session)");
        sess = a;
    }
    htab_init(&sess->v2->h);
    sess->kill = 0;
    sess->ctx = dctx;
    sess->psp = dctx->psp;
    owner = dctx;
    kprintf("GLOS-%s session psp=%04x\n", what, dctx->psp);
    return sess;
}

/* A call of a program that is not an app: 1 with *r its answer if it is one of these, 0 if not (the caller
   says GLOS_E_NOT_APP). */
int api2_session_call(struct trapframe *tf, u32 fn, int *r)
{
    struct app *a;
    u32 hdr[2];
    if (GLOS_FN_AREA(fn) == GLOS_AREA_NET || fn == GLOS_AUDIO_OUT) {    /* (2.5: a stream out, played by the mixer
                                                   while the program has it, full-screen or not) */
        int net = GLOS_FN_AREA(fn) == GLOS_AREA_NET;
        if (!(a = ctx_for(1, net ? "NET" : "AUDIO"))) {
            *r = a2_fail(tf, dctx && owner ? GLOS_E_REFUSED : GLOS_E_MEMORY);
            return 1;
        }
        *r = net ? api2_net(a, tf, fn & 0xFFu) : api2_audio(a, tf, fn & 0xFFu);
        return 1;
    }
    if (fn != GLOS_CORE_CLOSE && fn != GLOS_CORE_GET && fn != GLOS_CORE_SET)
        return 0;
    if (!(a = ctx_for(0, "")) || user_rd((u16)tf->ds, tf->ebx, hdr, sizeof hdr) != 0
        || (handle_kind(&a->v2->h, hdr[1]) != GLOS_HK_CONNECTION && handle_kind(&a->v2->h, hdr[1]) != GLOS_HK_STREAM))
        return 0;                       /* (a handle it does not have: it is not an app) */
    *r = api2_session_core(a, tf, fn);
    return 1;
}

/* The program has ended (its DPMI context goes): what it had open is closed. */
void api2_session_gone(struct dpmi_ctx *c)
{
    if (!owner || owner != c)
        return;
    sock_app_gone(sess);
    mixer_app_gone(sess);               /* (its streams: the card is the session's again, or the desktop's) */
    htab_init(&sess->v2->h);
    sess->ctx = 0;
    owner = 0;
}
