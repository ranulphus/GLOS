/* The GLOS API's entry (PRD §5.6, §10; include/glos/api.h): the far
 * procedure DPMI 0A00h "GLOS" gives a program, a HLT in the host's
 * trampoline page. The function is in EAX; its results go into the frame,
 * and the host's far return follows (kernel/dpmi/host.c). 1: the program
 * becomes an app. 2: the call is to be made again from the same HLT, when a
 * handler of the app's has run (a sleep an interrupt of its own cut short). */
#define GLOS_KERNEL
#include "glos/api.h"
#include "app.h"
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

int glos_api(struct trapframe *tf)
{
    static const char *const why[] = { "?", "already", "nested", "16-bit", "parent", "exclusive", "memory" };
    struct app *a = app_cur();
    u32 fn = tf->eax, id, ms, left;
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
        ms = tf->ebx > 0x03FFFFFFu ? 0x03FFFFFFu : tf->ebx;     /* (18 hours: the sum below in 32 bits) */
        left = app_sleep(a, ms + (ms * 24 + 999) / 1000);       /* 1024 ticks to the second */
        if (left) {                             /* an interrupt of its own is due: its handler runs, and the call */
            tf->ebx = (left * 125 + 127) >> 7;  /* is made again, for the rest */
            return 2;
        }
        tf->eax = 0;
        break;
    case GLOS_FN_TICKS:
        tf->eax = (u32)(((unsigned long long)timer_ticks() * 125) >> 7);
        break;
    }
    return 0;
}
