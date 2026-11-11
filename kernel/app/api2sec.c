/* API 2.0's SECRETS (include/glos/api2.h; docs/v1/phase-b.md step 13): PUT, GET and REMOVE over the store
 * (kernel/core/secrets.c).
 *
 * Whose a secret is: the calling app's program's file name ("TERM.EXE"), or, for a shared one, "*": for GLOS's
 * own programs, and for those the user allowed that name (a grant in the store: `glos secrets allow`). A store
 * that is locked (a master password not yet given since GLOS started) is opened by the user in the desktop's
 * dialog (kernel/wm/deskpass.c), which these calls wait for: they are made without the app's lock. */
#include <string.h>

#include "api2int.h"
#include "desk.h"
#include "dpmi.h"
#include "io.h"
#include "kprintf.h"
#include "secrets.h"

static u32 error(int r)
{
    return r == SEC_E_NONE ? GLOS_E_NOT_FOUND : r == SEC_E_ROOM ? GLOS_E_ROOM : r == SEC_E_LONG ? GLOS_E_ARGUMENT
         : GLOS_E_REFUSED;                      /* (locked, no store, a file that can't be read) */
}

static int call(struct app *a, struct trapframe *tf, u32 fn)
{
    struct glos_secret q;
    char name[64];                              /* (63 bytes and their end) */
    u8 value[1024];
    const char *owner = a->prog;
    u32 size, i, len = 0;
    int r;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || (q.flags & ~(u32)GLOS_SECRET_SHARED))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    for (i = 0; i < sizeof name; i++)
        if (user_rd((u16)tf->ds, q.name + i, &name[i], 1) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        else if (!name[i])
            break;
    if (i == sizeof name || !i || !owner[0])
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (secrets_state() == SEC_NONE)
        return a2_fail(tf, GLOS_E_REFUSED);
    if (secrets_state() == SEC_LOCKED && desk_password(a) != 0)        /* the user's to open: asked now, once at a time */
        return a2_fail(tf, GLOS_E_REFUSED);
    if (q.flags & GLOS_SECRET_SHARED) {
        if (!app_own(a) && !secrets_granted(a->prog, name))
            return a2_fail(tf, GLOS_E_REFUSED);
        owner = "*";
    }
    if (fn == GLOS_SECRETS_PUT) {
        if (q.count > sizeof value || (q.count && user_rd((u16)tf->ds, q.buffer, value, q.count) != 0))
            return a2_fail(tf, GLOS_E_ARGUMENT);
        r = secrets_put(owner, name, value, q.count);
        memset(value, 0, sizeof value);
        return r ? a2_fail(tf, error(r)) : a2_ok(tf, 0);
    }
    if (fn == GLOS_SECRETS_REMOVE)
        return (r = secrets_remove(owner, name)) != 0 ? a2_fail(tf, error(r)) : a2_ok(tf, 0);
    if ((r = secrets_get(owner, name, value, sizeof value, &len)) != 0)
        return a2_fail(tf, error(r));
    q.length = len;
    i = len < q.count ? len : q.count;
    r = (i && user_wr((u16)tf->ds, q.buffer, value, i) != 0) || a2_wr(tf, &q, sizeof q, size) != 0;
    memset(value, 0, sizeof value);
    if (r)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return len > q.count ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

int api2_secrets(struct app *a, struct trapframe *tf, u32 n)
{
    u32 fn = GLOS_FN(GLOS_AREA_SECRETS, n);
    if (fn == GLOS_SECRETS_PUT || fn == GLOS_SECRETS_GET || fn == GLOS_SECRETS_REMOVE)
        return call(a, tf, fn);
    return a2_fail(tf, GLOS_E_FUNCTION);
}
