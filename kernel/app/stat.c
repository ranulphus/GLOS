/* What the system is doing (GLOS_FN_STAT; docs/milestones-m9.md step C):
 * glos stat, glos ps, glos display and glos audio's figures, as structures
 * for the system monitor. */
#include <string.h>

#define GLOS_KERNEL
#include "glos/api.h"
#include "glos/bootinfo.h"
#include "agent.h"
#include "app.h"
#include "desk.h"
#include "display.h"
#include "kprintf.h"
#include "mixer.h"
#include "mm.h"
#include "nic.h"
#include "sched.h"
#include "session.h"
#include "stat.h"
#include "timer.h"
#include "vm.h"

u32 stat_apps(void *out, u32 room)
{
    u32 n = app_stat_list(out, room);
    n += agent_jobs_stat((u8 *)out + n, room - n);
    return n;
}
