/* Idle time, the screen saver and the screen's power (idle.h; API 2.0's IDLE, phase B step 5).
 *
 * The idle time runs from the last key, mouse movement, press or pad change the desktop was given (desk_in, and
 * the pads' news). When it reaches [saver] wait, and nothing keeps the screen on, the desktop's thread puts the
 * saver on the screen: v1's built-in one is a black screen, drawn in the desktop's own surface in place of the
 * windows (desk.c), so that no window, focus or app is touched by it. The first input ends it; the desktop's
 * input code uses that input up (deskin.c), so the key that wakes the screen types nothing. [saver] power
 * minutes later the screen's power goes off (VBE/PM, where the card has it), and comes on with the saver's end.
 *
 * What keeps the screen on: an AWAKE handle of any app's, a full-screen window with the focus, and no desktop on
 * the screen at all (a session has it). */
#include "idle.h"
#include "display.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "sched.h"
#include "timer.h"
#include "desk.h"

static u32 last_input;                  /* the tick of the last input */
static u32 pretend;                     /* ticks added to the idle time (tests), until the next input */
static u32 awake;                       /* AWAKE handles open */
static u32 wait_s, power_s;             /* [saver] wait and power, in seconds (0: never) */
static u8 on, off_screen;               /* the saver is on; the screen's power is off */
static u8 ask;                          /* 1 start, 2 end: asked of the desktop's thread */
static u32 since;                       /* the tick the saver came on ... */
static u32 idle_then;                   /* ... and the idle time then */
static void (*told)(int on);

void idle_touch(void)
{
    last_input = timer_ticks();
    pretend = 0;
    if (on)
        desk_kick();                    /* (the desktop's thread ends the saver) */
}

static u32 idle_ticks(void) { return timer_ticks() - last_input + pretend; }

u32 idle_ms(void) { return (u32)(((unsigned long long)idle_ticks() * 125) >> 7); }

static u32 minutes(const char *key)
{
    char v[16];
    u32 n = 0, i;
    kcfg_get("saver", key, v, sizeof v);
    for (i = 0; v[i] >= '0' && v[i] <= '9'; i++)
        n = n * 10 + (u32)(v[i] - '0');
    return v[i] ? 0 : n * 60;           /* ("off": never) */
}

void idle_config(void)
{
    wait_s = minutes("wait");
    power_s = minutes("power");
    desk_kick();
}

void idle_awake(int delta)
{
    u32 f = irq_save();
    awake += (u32)delta;
    irq_restore(f);
}

u32 idle_awake_count(void) { return awake; }
int idle_saver(void) { return on; }
u32 idle_saver_wait(void) { return wait_s; }
void idle_hook(void (*fn)(int on)) { told = fn; }

int idle_saver_ask(int start)
{
    if (!display_fb())
        return -1;
    ask = start ? 1 : 2;
    desk_kick();
    return 0;
}

void idle_pretend(u32 seconds)
{
    pretend += seconds * 1024u;
    desk_kick();
}

int idle_turn(int screen, int busy)
{
    u32 now = timer_ticks();
    int want = ask;
    ask = 0;
    if (on && (!screen || want == 2 || (s32)(last_input - since) > 0)) {
        on = 0;
        if (off_screen) {
            off_screen = 0;
            display_power_post(DISPLAY_POWER_ON);
        }
        kprintf("GLOS-IDLE saver off after=%u\n", (now - since) / 1024);
        if (told)
            told(0);
    } else if (!on && screen && want != 2
               && (want == 1 || (wait_s && !awake && !busy && idle_ticks() / 1024 >= wait_s))) {
        on = 1;
        since = now;
        idle_then = idle_ticks();
        kprintf("GLOS-IDLE saver on idle=%u\n", idle_ticks() / 1024);
        if (told)
            told(1);
    }
    if (on && power_s && !off_screen && !awake && (idle_ticks() - idle_then) / 1024 >= power_s) {
        off_screen = 1;
        display_power_post(DISPLAY_POWER_OFF);
    }
    return on;
}

u32 idle_stat(char *buf, u32 max)
{
    return (u32)ksnprintf(buf, max, "idle ms=%u saver=%s wait=%u power=%u awake=%u screen=%s\n", idle_ms(),
                          on ? "on" : "off", wait_s, power_s, awake, display_power_now() == DISPLAY_POWER_ON ? "on" : "off");
}
