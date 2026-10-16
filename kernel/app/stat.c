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

static u32 ms(u32 ticks) { return (u32)(((unsigned long long)ticks * 125) >> 7); }

u32 stat_system(void *out, u32 room)
{
    struct glos_stat_system s;
    u32 now = timer_ticks(), idle = sched_idle_ticks(), n;
    memset(&s, 0, sizeof s);
    s.size = sizeof s;
    s.version = GLOS_API_VERSION;
    s.uptime_ms = ms(now);
    s.idle_ms = ms(idle);
    s.busy_ms = s.uptime_ms > s.idle_ms ? s.uptime_ms - s.idle_ms : 0;
    s.cpu_family = vm.bi->cpu_family;
    s.mem_kb = pmm_total_frames() * 4;
    s.mem_free_kb = pmm_free_frames() * 4;
    s.heap_kb = kheap_bytes() / 1024;
    s.threads = sched_threads();
    s.apps = app_count();
    s.windows = desk_windows(0);
    net_counts(s.ip, &s.net_rx, &s.net_tx, s.nic, sizeof s.nic);
    display_counts(&s.screen_w, &s.screen_h, &s.screen_bpp, &s.accel, &s.vram_kb);
    mixer_counts(&s.mixer_rate, &s.streams, &s.underruns);
    {
        const char *h = vm.bi->hostname[0] ? vm.bi->hostname : "glos";
        for (n = 0; h[n] && n < sizeof s.hostname - 1; n++)
            s.hostname[n] = h[n];
    }
    if (s.accel)
        memcpy(s.card, "Matrox", 7);
    else if (s.screen_w)
        memcpy(s.card, "VESA", 5);
    if (session_active()) {
        const char *p = session_prog();
        for (n = 0; p[n] && n < sizeof s.session - 1; n++)
            s.session[n] = p[n];
    }
    n = room < sizeof s ? room : sizeof s;
    memcpy(out, &s, n);
    return n;
}

u32 stat_apps(void *out, u32 room)
{
    u32 n = app_stat_list(out, room);
    n += agent_jobs_stat((u8 *)out + n, room - n);
    return n;
}
