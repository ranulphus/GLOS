/* Watches of directories (GLOS_FN_WATCH; docs/milestones-m9.md step C): an
 * app is told when a file in a directory it watches is made, removed,
 * renamed or written, by anyone: a program in the system VM, an app, the
 * agent, SFTP.
 *
 * Every DOS call passes the V86 monitor or the DPMI host's reflection of it
 * (agent_vm_int21 sees both), so the calls that change a directory are
 * seen there before DOS makes them: 39h, 3Ah, 3Ch, 41h, 43h (setting), 56h,
 * 5Bh, 6Ch, and 3Eh after a write (40h with bytes). Their paths are as the
 * program gave them: one with a drive and a full path marks the watches of
 * its directory; any other (relative, or a handle's) marks every watch,
 * which costs the watcher a look at a directory that may not have changed.
 * A watch marked is told once, at most eight times a second. */
#include <string.h>

#define GLOS_KERNEL
#include "glos/api.h"
#include "app.h"
#include "io.h"
#include "kprintf.h"
#include "timer.h"
#include "vm.h"
#include "watch.h"

#define WATCHES 32

static struct {
    void *app;                                  /* 0: free */
    u32 id;
    char dir[68];                               /* upper case, "C:\GAMES" (a root: "C:\") */
    u8 due;
} w[WATCHES];
static u32 next_id = 1, last_told;
static u8 any_due, wrote;

static char up(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c == '/' ? '\\' : c; }

/* A path into dir form: upper case, backslashes, no trailing one but a root's. */
static void norm(const char *in, char *out, u32 room)
{
    u32 n = 0;
    while (*in && n < room - 1)
        out[n++] = up(*in++);
    while (n > 3 && out[n - 1] == '\\')
        n--;
    out[n] = 0;
}

int watch_add(void *app, const char *dir, u32 *id)
{
    u32 i, f;
    char d[68];
    norm(dir, d, sizeof d);
    if (d[0] < 'A' || d[0] > 'Z' || d[1] != ':' || d[2] != '\\')
        return GLOS_E_ARGUMENT;
    f = irq_save();
    for (i = 0; i < WATCHES; i++)
        if (!w[i].app) {
            w[i].app = app;
            w[i].id = next_id++;
            memcpy(w[i].dir, d, sizeof d);
            w[i].due = 0;
            *id = w[i].id;
            irq_restore(f);
            return 0;
        }
    irq_restore(f);
    return GLOS_E_MEMORY;
}

int watch_remove(void *app, u32 id)
{
    u32 i, f = irq_save();
    int r = GLOS_E_ARGUMENT;
    for (i = 0; i < WATCHES; i++)
        if (w[i].app == app && w[i].id == id) {
            w[i].app = 0;
            r = 0;
        }
    irq_restore(f);
    return r;
}

void watch_app_gone(void *app)
{
    u32 i, f = irq_save();
    for (i = 0; i < WATCHES; i++)
        if (w[i].app == app)
            w[i].app = 0;
    irq_restore(f);
}

/* A change at path (NULL: somewhere unknown). */
void watch_path(const char *path)
{
    char p[80], *slash;
    u32 i, f;
    int all = !path;
    if (path) {
        norm(path, p, sizeof p);
        slash = 0;
        for (i = 0; p[i]; i++)
            if (p[i] == '\\')
                slash = &p[i];
        if (p[0] < 'A' || p[0] > 'Z' || p[1] != ':' || p[2] != '\\' || !slash)
            all = 1;
        else if (slash == &p[2])
            p[3] = 0;                           /* (in the root) */
        else
            *slash = 0;
    }
    f = irq_save();
    for (i = 0; i < WATCHES; i++)
        if (w[i].app && (all || !strcmp(w[i].dir, p))) {
            w[i].due = 1;
            any_due = 1;
        }
    irq_restore(f);
}

/* A DOS call about to be made (the VM thread's, trap context): a path at DS:DX (or DS:SI for 6Ch). */
void watch_int21(u16 ax, u16 bx, u16 cx, u32 dx_lin, u32 si_lin)
{
    u32 ah = ax >> 8, i, lin = dx_lin;
    char p[80];
    (void)bx;
    switch (ah) {
    case 0x40:
        if (cx)
            wrote = 1;                          /* (a write: the close after it is a change) */
        return;
    case 0x3E:
        if (wrote) {
            wrote = 0;
            watch_path(NULL);
        }
        return;
    case 0x43:
        if ((ax & 0xFF) != 1)
            return;
        break;
    case 0x6C:
        lin = si_lin;
        break;
    case 0x56:                                  /* (a rename may move it to another directory: all of them) */
        watch_path(NULL);
        return;
    case 0x39: case 0x3A: case 0x3C: case 0x41: case 0x5B:
        break;
    default:
        return;
    }
    {
        u32 k;
        for (k = 0; k < WATCHES && !w[k].app; k++)
            ;
        if (k == WATCHES)
            return;                             /* (nobody watches: the path isn't even read) */
    }
    for (i = 0; i < sizeof p - 1; i++) {
        p[i] = (char)vm_rd8(lin + i);
        if (!p[i])
            break;
    }
    p[i] = 0;
    watch_path(p);
}

/* Every tick (interrupts off): watches marked told, eight times a second at most. */
void watch_tick(u32 now)
{
    u32 i;
    if (!any_due || now - last_told < 128)
        return;
    last_told = now;
    any_due = 0;
    for (i = 0; i < WATCHES; i++)
        if (w[i].app && w[i].due) {
            struct glos_event e;
            w[i].due = 0;
            memset(&e, 0, sizeof e);
            e.type = GLOS_EV_FILES;
            e.key = w[i].id;
            app_event(w[i].app, &e);
        }
}
