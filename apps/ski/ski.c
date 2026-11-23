/* SKI.EXE: the skiing game (GLOS v1, phase F; docs/v1/apps/skiing.md), its first part. One of the two games GLOS
 * comes with: GLOS's own, with its own skier, pictures, mountains and rules; nothing of any other game is in it.
 * Its name, Fall Line (skiing's word for the line straight down a slope), is the owner's choice (2026-11-21).
 *
 * A slope seen from above scrolls up under a skier. The mountain is made by rule from a number
 * (apps/shared/skimount.c); the skier moves in sixty steps a second from the keys held (apps/shared/skirun.c);
 * here are the window, the picture and the tables:
 *
 *   slalom           gates against the clock, five seconds for each one missed;
 *   free run         the whole mountain for points: the way made without a fall, time in the air, turns landed,
 *                    things passed close by;
 *   the day's        the mountain of the day: the same on every PC, with the day's own tables;
 *   four grades      green, blue, red, black: steeper, narrower, more in the way, ice;
 *   a number         a mountain's number can be typed in, to ski a friend's;
 *   the tables       the best ten of each grade and kind, and of the day, with names, in SKI.DAT;
 *   watching         the careful skier (skirun_auto) skis the mountain: the demonstration.
 *
 * Keys: Left and Right (or A and D) turn the skis, in the air the skier; Up (or W) tucks; Down (or S) ploughs.
 * Enter or Space starts; Esc or P pauses.
 *
 * The picture (apps/ski/art.c makes its pieces by rule): the snow in bands by its kind, the ice, the skier's
 * tracks, then everything in sight from the top of the window down, the skier among it; beside the field a
 * strip with the run's figures.
 *
 * SKI -demo N [-grade 0..3] [-free] [-until STEP,...] [-speed TIMES] [-table FILE] [-day YYYYMMDD] is the
 * suite's: the careful skier on mountain N, the picture held at given steps, the clock several times faster
 * (a run is its steps: it ends the same). tests/gui/ski.txt. Its lines: "SKI ..." on the output.
 *
 * Sound (apps/ski/sound.c), made by rule: the skis' hiss by the speed and the snow, a gate's bell, a miss, a
 * jump, a landing, a fall, the valley's chime.
 *
 * The ghost: the best slalom on a mountain is kept as its keys (SKIGHOST.DAT, the last mountain's) and skis
 * beside the player there, a grey shape on the snow.
 *
 * Not yet (the plan's rest): weather and night; other skiers, the groomer and the hare; the wind's sound; the
 * mouse and pads; the window doubled, and full screen. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glos/glos.h>
#include <glos/gui.h>

#include "../shared/skirun.h"
#include "../shared/skitable.h"
#include "art.h"
#include "sound.h"

#define GAME_NAME "Fall Line"           /* (the owner's choice, 2026-11-21) */

enum { C_AGAIN = 1, C_NEW, C_DAY, C_NUMBER, C_WATCH, C_TABLES, C_PAUSE, C_EXIT, C_SLALOM, C_FREE, C_GREEN, C_BLUE, C_RED, C_BLACK,
       C_SOUND, C_JACKET, C_FIGURE = C_JACKET + 8, C_WEATHER = C_FIGURE + 8, C_DOUBLE = C_WEATHER + 8, C_FULL };     /* (C_JACKET + n: the jackets;
                                                   C_FIGURE + n: the figures; C_WEATHER + n: the weathers) */
enum { W_CLEAR, W_SNOW, W_FOG, W_NIGHT, W_WIND, W_COUNT };
enum { P_READY, P_RUN, P_PAUSED, P_DONE };

#define FW 480                          /* the field */
#define FH 360
#define SW 140                          /* the strip beside it */
#define SKIER_ROW 120                   /* where in the field the skier is */
#define TRACKS 400

static const char *const grade_name[4] = { GUI_N("Green"), GUI_N("Blue"), GUI_N("Red"), GUI_N("Black") };
static const unsigned grade_rgb[4] = { 0x339933u, 0x3366CCu, 0xCC0000u, 0x333333u };
static const char *const figures[FIGURE_COUNT] = { GUI_N("&Skier"), GUI_N("R&acer"), GUI_N("Sn&owboarder"),
                                                   GUI_N("Si&t-skier") };   /* (the owner's: a few to choose) */
static const struct { const char *name; unsigned rgb; } jackets[5] = {
    { GUI_N("&Red"), 0xFF3333u }, { GUI_N("&Blue"), 0x3366FFu }, { GUI_N("&Green"), 0x33CC33u }, { GUI_N("&Yellow"), 0xFFCC00u },
    { GUI_N("&Purple"), 0x9933CCu } };

static struct gui_window *win;
static struct gui_widget *canvas;
static struct skirun run;
static int phase = P_READY, watching;   /* watching: the careful skier has the skis */
/* (phase F) The demonstration by itself: after attract_ms on the summit with nobody playing (no key, click or
   pointer moved, while the window has the keyboard), the careful skier skis; at the valley it goes on to the next
   mountain; any key or click gives the game back, on the mountain it is at. */
static unsigned attract_ms = 30000, idle_ms, stopped_ms;
static int attract, attract_given;
static unsigned seed = 1;
static int grade = SKI_BLUE, mode = SKI_SLALOM, of_day, jacket, figure, weather, arg_weather = -1;
static unsigned last_ms, acc;           /* the frame's clock: milliseconds times sixty not yet made steps of */
static long until = -1;                 /* the suite's: the picture held at this step ... */
static long untils[8];                  /* ... and at these after it, each when the one before is let go */
static int nuntils;
static int stopped;                     /* (past the finish: the glide has ended, and was said) */
static int sound_on = 1, sounding;      /* Sound is chosen; and there is a card it plays through */

/* The ghost: the best slalom skied on a mountain, as its keys (a run is its mountain and its keys), kept in a
   file beside the tables and skied again beside the player on that mountain. */
#define GHOST_STEPS (5 * 60 * SKI_STEPS)
static unsigned char rec[GHOST_STEPS];  /* the keys of the run under way */
static struct {
    unsigned seed, steps;
    int grade;
    unsigned long ms;
    unsigned char keys[GHOST_STEPS];
} ghost;
static struct skirun ghost_run;
static int ghost_on;                    /* it skis in this run */
static int speed = 1;                   /* the suite's: the clock so many times faster (a run is its steps: the same) */
static unsigned frames;                 /* (the strip of figures is drawn every third: ten times a second) */
static unsigned started_ms;             /* (the program's start: the frames a second it drew, for the suite) */
static int suite, held;                 /* the suite's run: every step that is due is made (its clock jumps); held:
                                           paused for a picture, with nothing written over the field */
static int asking;                      /* (a dialog is up: no steps meanwhile) */
static struct { short x, y; } track[TRACKS];    /* where the skier has been, every other step (y < 0: off the snow) */
static int ntrack, track_at;
static char banner[64];
static unsigned banner_until;
static struct ski_tables tables;
static char table_path[300];
static int today;                       /* yyyymmdd */

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("SKI ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- the tables' file */

static void tables_load(void)
{
    FILE *f = table_path[0] ? gui_fopen(table_path, "r") : NULL;
    skitables_init(&tables);
    tables.grade_last = SKI_BLUE;
    if (f) {
        skitables_read(&tables, f);
        fclose(f);
    }
    skitables_today(&tables, today);
}

static void tables_keep(void)
{
    FILE *f;
    tables.grade_last = grade, tables.mode_last = mode, tables.jacket = jacket, tables.figure = figure, tables.weather = weather;
    if (!table_path[0] || !(f = gui_fopen(table_path, "w")))
        return;
    skitables_write(&tables, f);
    fclose(f);
}

static struct ski_table *table_now(void) { return of_day ? &tables.day[mode] : &tables.grade[mode][grade]; }

/* ---- the ghost's file */

static void ghost_path(char *out, size_t room)
{
    char *cut;
    snprintf(out, room, "%s", table_path);
    cut = strrchr(out, '\\');
    snprintf(cut ? cut + 1 : out, room - (size_t)(cut ? cut + 1 - out : 0), "SKIGHOST.DAT");
}

static void ghost_load(void)
{
    char p[300];
    unsigned char h[20];
    FILE *f;
    ghost.steps = 0;
    if (!table_path[0])
        return;
    ghost_path(p, sizeof p);
    if (!(f = gui_fopen(p, "rb")))
        return;
    if (fread(h, 1, 20, f) == 20 && !memcmp(h, "SKIG", 4)) {
        unsigned n = h[12] | h[13] << 8 | (unsigned)h[14] << 16;
        ghost.seed = h[4] | h[5] << 8 | (unsigned)h[6] << 16 | (unsigned)h[7] << 24;
        ghost.grade = h[8] & 3;
        ghost.ms = h[16] | h[17] << 8 | (unsigned long)h[18] << 16 | (unsigned long)h[19] << 24;
        if (n && n <= GHOST_STEPS && fread(ghost.keys, 1, n, f) == n)
            ghost.steps = n;
    }
    fclose(f);
}

static void ghost_keep(void)            /* the run just ended is its mountain's best: it is the ghost now */
{
    char p[300];
    unsigned char h[20] = "SKIG";
    FILE *f;
    ghost.seed = seed, ghost.grade = grade, ghost.steps = run.finish_steps, ghost.ms = skirun_ms(&run);
    memcpy(ghost.keys, rec, ghost.steps);
    if (!table_path[0])
        return;
    ghost_path(p, sizeof p);
    if (!(f = gui_fopen(p, "wb")))
        return;
    h[4] = (unsigned char)seed, h[5] = (unsigned char)(seed >> 8), h[6] = (unsigned char)(seed >> 16), h[7] = (unsigned char)(seed >> 24);
    h[8] = (unsigned char)grade;
    h[12] = (unsigned char)ghost.steps, h[13] = (unsigned char)(ghost.steps >> 8), h[14] = (unsigned char)(ghost.steps >> 16);
    h[16] = (unsigned char)ghost.ms, h[17] = (unsigned char)(ghost.ms >> 8), h[18] = (unsigned char)(ghost.ms >> 16), h[19] = (unsigned char)(ghost.ms >> 24);
    fwrite(h, 1, 20, f);
    fwrite(ghost.keys, 1, ghost.steps, f);
    fclose(f);
    say("ghost kept steps=%u ms=%lu", ghost.steps, ghost.ms);
}

/* ---- a run begun, stepped, ended */

static void time_text(unsigned long ms, char *out, size_t room)
{
    snprintf(out, room, "%lu:%02lu.%02lu", ms / 60000, ms / 1000 % 60, ms / 10 % 100);
}

static void ready(void)                 /* at the summit of the mountain chosen */
{
    skirun_start(&run, seed, grade, mode);
    phase = P_READY;
    watching = 0;
    stopped = 0;
    ntrack = track_at = 0;
    banner[0] = 0;
    acc = 0;
    say("ready seed=%u grade=%d mode=%d day=%d", seed, grade, mode, of_day);
    ghost_on = mode == SKI_SLALOM && ghost.steps && ghost.seed == seed && ghost.grade == grade;
    if (ghost_on) {                     /* this mountain's best run, to ski beside */
        skirun_start(&ghost_run, seed, grade, mode);
        say("ghost steps=%u ms=%lu", ghost.steps, ghost.ms);
    }
    if (canvas)
        gui_damage(canvas);
}

static void start(int watch)
{
    if (phase != P_READY)
        ready();
    phase = P_RUN;
    watching = watch;
    last_ms = glos_ticks();
    say("started%s", watch ? " watching" : "");
}

static void someone(void)               /* somebody is here: the demonstration waits again */
{
    idle_ms = glos_ticks();
}

static int attract_end(void)            /* a key, click or command in the demonstration by itself: the game back */
{
    if (!attract)
        return 0;
    attract = 0;
    say("demonstration stopped");
    ready();
    return 1;
}

static unsigned next_mountain(unsigned n)       /* (the demonstration's next: another, and the same on every PC) */
{
    return (unsigned)(((unsigned long long)n * 7919u + 104729u) % 999999u) + 1;
}

static void pause_toggle(void)
{
    if (phase == P_RUN) {
        phase = P_PAUSED;
        say("paused");
    } else if (phase == P_PAUSED) {
        phase = P_RUN;
        held = 0;
        last_ms = glos_ticks();
        say("resumed");
    }
    gui_damage(canvas);
}

static void say_banner(const char *text)
{
    snprintf(banner, sizeof banner, "%s", text);
    banner_until = run.steps + 90;
}

static void finished(void)
{
    struct ski_table *t = table_now();
    unsigned long ms = skirun_ms(&run);
    int place = watching ? -1 : skitable_place(t, mode, ms, run.score);
    phase = P_DONE;
    say("finished steps=%u ms=%lu score=%ld passed=%d missed=%d falls=%d place=%d", run.finish_steps, ms, run.score, run.gates_passed,
        run.gates_missed, run.falls, place);
    if (!watching && mode == SKI_SLALOM && run.finish_steps <= GHOST_STEPS
        && (!ghost.steps || ghost.seed != seed || ghost.grade != grade || ms < ghost.ms))
        ghost_keep();
    if (place >= 0) {                   /* among the ten: a name for the table */
        struct ski_entry e;
        char name[20];
        memset(&e, 0, sizeof e);
        snprintf(name, sizeof name, "%s", tables.player);
        asking = 1;
        if (gui_ask(win, GUI_T("A Place in the Table"), GUI_T("Your &name:"), name, sizeof name) == 1 && name[0]) {
            snprintf(e.name, sizeof e.name, "%s", name);
            snprintf(tables.player, sizeof tables.player, "%s", name);
            e.ms = ms, e.score = run.score, e.seed = seed, e.day = today;
            place = skitable_add(t, mode, &e);
            tables_keep();
            say("table %d %s", place + 1, e.name);
        } else {
            say("table left");
        }
        asking = 0;
        last_ms = glos_ticks();
    }
}

/* The mouse over the slope (once it has moved there, until a key or the pad is used): the skier turns towards
   the pointer, the left button tucks, the right one ploughs. */
static int mouse_on, mouse_x, mouse_y;
static unsigned mouse_buttons;

static void game_corner(const struct gui_widget *w, int *ox, int *oy);
static int scale_now(void);

static int field_event(struct gui_widget *w, struct gui_ev *e)
{
    int ox, oy, x, y;
    game_corner(w, &ox, &oy);
    x = (e->x - ox) / scale_now(), y = (e->y - oy) / scale_now();     /* (the field's own pixels) */
    if (e->type != GUI_EV_MOVE && e->type != GUI_EV_PRESS && e->type != GUI_EV_RELEASE)
        return 0;
    if (e->type == GUI_EV_PRESS || (e->type == GUI_EV_MOVE && abs(x - mouse_x) + abs(y - mouse_y) >= 4)) {
        someone();                              /* (a click, or the pointer moved by a hand) */
        if (e->type == GUI_EV_PRESS && attract_end())
            return 1;
    }
    if (x < 0 || x >= FW || y < 0 || y >= FH) {         /* (off the slope: the tables' side is not steered from) */
        mouse_buttons = e->type == GUI_EV_RELEASE ? e->buttons : mouse_buttons;
        return 0;
    }
    if (!mouse_on && e->type == GUI_EV_MOVE && (abs(x - mouse_x) + abs(y - mouse_y) < 4))
        return 0;                               /* (a still pointer the slope scrolled under is no wish) */
    if (!mouse_on)
        say("mouse steers");
    mouse_on = 1;
    mouse_x = x, mouse_y = y;
    mouse_buttons = e->buttons;
    return 1;
}

/* The skier's heading towards the pointer: -8 (across, leftwards) to 8, from where the skier is drawn. */
static unsigned mouse_keys(void)
{
    static const int tan_x100[9] = { 0, 20, 41, 67, 100, 150, 241, 503, 100000 };     /* tan(h * 11.25 degrees) */
    int px = run.x >> 8, cam_x = px - FW / 2, sx, dx, dy, want = 8, k;
    unsigned keys = 0;
    if (cam_x < 0)
        cam_x = 0;
    if (cam_x > SKI_WORLD_W - FW)
        cam_x = SKI_WORLD_W - FW;
    sx = px - cam_x;
    dx = mouse_x - sx, dy = mouse_y - SKIER_ROW;
    if (dy > 4)                                 /* (below the skier: the way down towards it) */
        for (k = 0, want = 8; k < 8; k++)
            if (abs(dx) * 100 < dy * (tan_x100[k] + tan_x100[k + 1]) / 2) {
                want = k;
                break;
            }
    if (dx < 0)
        want = -want;
    if (abs(dx) < 6 && dy > 4)
        want = 0;
    if (want < run.heading)
        keys |= SKI_K_LEFT;
    else if (want > run.heading)
        keys |= SKI_K_RIGHT;
    if (mouse_buttons & 1)
        keys |= SKI_K_TUCK;
    if (mouse_buttons & 2)
        keys |= SKI_K_PLOUGH;
    return keys;
}

/* A pad (the first plugged in): the stick steers, forwards tucks and back ploughs; its first button tucks, its
   second ploughs. */
static unsigned pad_keys(void)
{
    struct glos_pads p;
    unsigned keys = 0;
    int i;
    memset(&p, 0, sizeof p);
    p.size = sizeof p;
    if (glos_pads(&p, 0) != 0)
        return 0;
    for (i = 0; i < GLOS_PADS && !p.pad[i].present; i++)
        ;
    if (i == GLOS_PADS)
        return 0;
    if (p.pad[i].lx < -40)
        keys |= SKI_K_LEFT;
    if (p.pad[i].lx > 40)
        keys |= SKI_K_RIGHT;
    if (p.pad[i].ly < -40 || (p.pad[i].buttons & 1))
        keys |= SKI_K_TUCK;
    if (p.pad[i].ly > 40 || (p.pad[i].buttons & 2))
        keys |= SKI_K_PLOUGH;
    return keys;
}

static unsigned keys_held(void)
{
    struct glos_keys k;
    unsigned keys = 0, pad;
    memset(&k, 0, sizeof k);
    k.size = sizeof k;
    if (glos_keys(&k) != 0)
        return 0;
    if (GLOS_KEY_DOWN(&k, GUI_K_LEFT) || GLOS_KEY_DOWN(&k, 0x1E))       /* (A) */
        keys |= SKI_K_LEFT;
    if (GLOS_KEY_DOWN(&k, GUI_K_RIGHT) || GLOS_KEY_DOWN(&k, 0x20))      /* (D) */
        keys |= SKI_K_RIGHT;
    if (GLOS_KEY_DOWN(&k, GUI_K_UP) || GLOS_KEY_DOWN(&k, 0x11))         /* (W) */
        keys |= SKI_K_TUCK;
    if (GLOS_KEY_DOWN(&k, GUI_K_DOWN) || GLOS_KEY_DOWN(&k, 0x1F))       /* (S) */
        keys |= SKI_K_PLOUGH;
    pad = pad_keys();
    if (keys || pad) {                          /* (the keys or the pad: the mouse no longer steers) */
        if (mouse_on)
            say("mouse steers no longer");
        mouse_on = 0;
        return keys | pad;
    }
    return mouse_on ? mouse_keys() : 0;
}

static unsigned keys_now;               /* (the keys of the step drawn: the skier's pose) */

static void step(void)
{
    char text[64];
    keys_now = (watching ? skirun_auto(&run) : keys_held()) | (weather == W_WIND ? SKI_K_WIND : 0);
                                                /* (the wind blows from the step it is chosen: kept with the keys) */
    if (run.state != SKI_S_DONE && run.steps < GHOST_STEPS)
        rec[run.steps] = (unsigned char)keys_now;
    skirun_step(&run, keys_now);
    if (ghost_on && !watching && ghost_run.state != SKI_S_DONE && ghost_run.steps < ghost.steps)
        skirun_step(&ghost_run, ghost.keys[ghost_run.steps]);
    if (sounding)
        sound_events(run.ev);
    if (!(run.steps & 1) && run.state != SKI_S_DONE) {  /* the track: a point every other step */
        track[track_at].x = (short)(run.x >> 8);
        track[track_at].y = run.state == SKI_S_SKIING ? (short)(run.y >> 8) : -1;
        track_at = (track_at + 1) % TRACKS;
        if (ntrack < TRACKS)
            ntrack++;
    }
    if (run.ev & SKI_EV_MISS)
        say_banner(GUI_T("Gate missed: 5 seconds"));
    if (run.ev & SKI_EV_FALL)
        say_banner(GUI_T("A fall!"));
    if (run.ev & SKI_EV_NEAR && mode == SKI_FREE)
        say_banner(GUI_T("Close! +25"));
    if (run.ev & SKI_EV_LAND && run.spin) {
        int halves = abs(run.spin + (run.spin >= 0 ? 4 : -4)) / 8;
        if (halves == 1)
            say_banner(GUI_T("A half turn!"));
        else if (halves == 2)
            say_banner(GUI_T("A whole turn!"));
        else if (halves > 2) {
            snprintf(text, sizeof text, GUI_T("%d half turns!"), halves);
            say_banner(text);
        }
    }
    if (run.ev & SKI_EV_FINISH)
        finished();
}

static void frame(void *arg)            /* the game's clock: the steps that are due, then the picture */
{
    unsigned now = glos_ticks(), due;
    int gliding = phase == P_DONE && skirun_speed(&run) > 4;
    (void)arg;
    if (asking)
        return;
    if (phase == P_RUN && !watching && !win->active && !suite)
        pause_toggle();                 /* (another window has the keyboard) */
    if (phase == P_DONE && !gliding && !stopped) {     /* at rest past the finish (the suite's pictures wait for it) */
        stopped = 1;
        stopped_ms = now;
        say("stopped x=%d y=%d", run.x >> 8, run.y >> 8);
    }
    if (attract && phase == P_DONE && stopped && now - stopped_ms >= 5000) {     /* on to the next mountain */
        seed = next_mountain(seed), of_day = 0;
        say("demonstration next seed=%u", seed);
        ready();
        start(1);
        return;
    }
    if (attract_ms && !attract && phase == P_READY && win->active && now - idle_ms >= attract_ms) {
        attract = 1;
        say("demonstration by itself");
        start(1);
        return;
    }
    if (phase != P_RUN && !gliding) {
        last_ms = now;
        return;
    }
    acc += (now - last_ms) * SKI_STEPS * (unsigned)speed;
    last_ms = now;
    due = acc / 1000;
    acc %= 1000;
    if (!suite && due > 10)             /* (a PC too slow for sixty a second: the run slows; it does not skip) */
        due = 10;
    while (due-- && (phase == P_RUN || phase == P_DONE)) {
        if (phase == P_DONE && skirun_speed(&run) <= 4)
            break;                      /* (at rest past the finish: the same step on every PC) */
        step();
        if (until >= 0 && (long)run.steps >= until && phase == P_RUN) {
            phase = P_PAUSED;           /* the suite's: held here for its picture */
            held = 1;
            until = nuntils ? untils[0] : -1;
            if (nuntils)
                memmove(untils, untils + 1, (size_t)--nuntils * sizeof *untils);
            say("at %u x=%d y=%d speed=%d score=%ld gates=%d/%d falls=%d", run.steps, run.x >> 8, run.y >> 8, skirun_speed(&run), run.score,
                run.gates_passed, run.gates_missed, run.falls);
            break;
        }
    }
    if (sounding)                       /* the skis' sound for this picture's time */
        sound_frame(skirun_speed(&run), run.state == SKI_S_SKIING ? skimount_snow(&run.m, run.x >> 8, run.y >> 8) : -1);
    if (++frames % 3 == 0 || phase != P_RUN) {  /* the figures ten times a second, and whenever the run stops */
        gui_damage(canvas);
    } else {                            /* the field alone */
        struct gfx_rect field = { canvas->r.x, canvas->r.y, FW * scale_now(), FH * scale_now() };
        game_corner(canvas, &field.x, &field.y);
        gui_damage_rect(win, &field);
    }
}

/* ---- the picture */

#define PISTE  0xFFFFFFu
#define POWDER 0xEEEEEEu
#define DEEP   0xDADADAu
#define ICE    0xCCFFFFu
#define TRACK  0xD0D0D0u

static int isqrt(int v)
{
    int r = 0;
    while ((r + 1) * (r + 1) <= v)
        r++;
    return r;
}

static void text_at(struct gfx_surface *s, int x, int y, const struct gfx_font *f, const char *text, unsigned rgb)
{
    gfx_text(s, x, y, f, text, (int)strlen(text), rgb);
}

static void box_text(struct gfx_surface *s, int ox, int oy, int y, const char *const *lines, int n)    /* lines in a box, mid field */
{
    int i, w = 0, h = n * 18 + 14, x;
    for (i = 0; i < n; i++) {
        int tw = gfx_text_width(i ? gui_font : gui_bold, lines[i], (int)strlen(lines[i]));
        if (tw > w)
            w = tw;
    }
    w += 28;
    x = ox + (FW - w) / 2;
    gfx_fill(s, x, oy + y, w, h, 0xFFFFFFu);
    gfx_frame(s, x, oy + y, w, h, 0x333333u);
    for (i = 0; i < n; i++) {
        int tw = gfx_text_width(i ? gui_font : gui_bold, lines[i], (int)strlen(lines[i]));
        text_at(s, ox + (FW - tw) / 2, oy + y + 8 + i * 18, i ? gui_font : gui_bold, lines[i], 0x000000u);
    }
}

/* ---- (phase F) the weather: pictures only, the run the same in any (the plan's wind, which pushes, is not yet).
   Snowfall: flakes drifting down, placed by rule from the run's own steps (a held picture is the same every
   time); fog: the slope ahead (below the skier) whitened more and more, by a dither of white dots; night: all
   dark by a dither but the skier's lamp, a pool of light before them. Fog's and night's dots are one-bit masks
   made once and laid over in one call: the same at 8, 16 and 32 bits, and cheap on a 486. */

static const unsigned char bayer[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };
static unsigned char *fog_bits, *night_bits;    /* FW x FH; 2FW x 2FH, the lamp at its middle */

static int dot(int x, int y, int level)        /* (level 0 to 16 of 16) */
{
    return bayer[y & 3][x & 3] < level;
}

static void masks_make(void)
{
    int x, y;
    if (!fog_bits && (fog_bits = calloc((size_t)(FW / 8) * FH, 1)) != NULL)
        for (y = SKIER_ROW + 24; y < FH; y++) {         /* (thicker with the distance ahead, to 14 of 16 at the foot) */
            int level = (y - SKIER_ROW - 24) * 15 / (FH - SKIER_ROW - 24);
            for (x = 0; x < FW; x++)
                if (dot(x, y, level))
                    fog_bits[y * (FW / 8) + x / 8] |= (unsigned char)(0x80 >> (x & 7));
        }
    if (!night_bits && (night_bits = calloc((size_t)(2 * FW / 8) * (2 * FH), 1)) != NULL)
        for (y = 0; y < 2 * FH; y++)
            for (x = 0; x < 2 * FW; x++) {
                long dx = x - FW, dy = y - FH - 48;     /* (the lamp's pool: ahead of the skier, longer down the slope) */
                long d = dx * dx * 100 / (60 * 60) + dy * dy * 100 / (95 * 95), level;
                level = d < 60 ? 0 : d < 160 ? (d - 60) * 13 / 100 : 13;
                if (dot(x, y, (int)level))
                    night_bits[y * (2 * FW / 8) + x / 8] |= (unsigned char)(0x80 >> (x & 7));
            }
}

static void draw_weather(struct gfx_surface *s, int ox, int oy, int sx, int sy)
{
    int g = skirun_gust(&run);
    if (g) {                                    /* a gust: streaks blown across, longer and faster the stronger */
        unsigned i, len = 6 + (unsigned)(g < 0 ? -g : g) / 6, drift = run.steps * (unsigned)(g < 0 ? -g : g) / 12;
        for (i = 0; i < 36; i++) {
            unsigned h = i * 2654435761u, fx = ((h >> 8) + (unsigned)(run.x >> 8)) % FW, fy = (h >> 16) % FH;
            fx = g > 0 ? (fx + drift) % FW : (fx + FW * 64 - drift % (FW * 64)) % FW;
            gfx_fill(s, ox + (int)fx - (int)len / 2, oy + (int)fy, (int)len, 1, 0x8899AAu);
        }
    }
    if (weather == W_SNOW) {
        unsigned i, t = run.steps;
        for (i = 0; i < 140; i++) {             /* (each flake's place by rule from its number and the steps) */
            unsigned h = i * 2654435761u, fx, fy;
            fx = (h >> 8) % FW + (t * (1 + (h >> 20) % 2) / 3) % FW + (unsigned)(run.x >> 8);
            fy = (h >> 16) % FH + t * (2 + (h >> 24) % 3) / 2 + (unsigned)(run.y >> 8) / 2;
            fx %= FW, fy %= FH;
            gfx_fill(s, ox + (int)fx + 1, oy + (int)fy + 1, 2, 2, 0x999999u);
            gfx_fill(s, ox + (int)fx, oy + (int)fy, 2, 2, 0xFFFFFFu);
        }
        return;
    }
    masks_make();
    if (weather == W_FOG && fog_bits)
        gfx_mono(s, ox, oy, fog_bits, FW, FH, FW / 8, 0xFFFFFFu, GFX_NONE);
    else if (weather == W_NIGHT && night_bits)
        gfx_mono(s, ox + sx - FW, oy + sy - FH, night_bits, 2 * FW, 2 * FH, 2 * FW / 8, 0x000033u, GFX_NONE);
}

/* ---- (phase F) the chair lift's cables and chairs, and the hares: pictures over the slope, in nobody's way.
   Both move by rule from the run's steps (a held picture is the same every time). */

#define LIFT_GAP (SKI_LIFT_PIECES * SKI_CHUNK)  /* from a pylon to the next */
#define CHAIR_GAP 64                            /* from a chair to the next along a cable */

static void draw_lift(struct gfx_surface *s, int ox, int oy, int cam_x, int cam_y)
{
    int k, side, y;
    for (k = (cam_y - 40) / LIFT_GAP; k <= (cam_y + FH + 40) / LIFT_GAP; k++) {
        int y0 = k * LIFT_GAP + SKI_LIFT_Y, y1 = y0 + LIFT_GAP, x0, x1;
        if (k < 1 || y1 >= run.m.length - 60)   /* (the spans between two pylons: from the first, to the last) */
            continue;
        x0 = skimount_lift(&run.m, y0), x1 = skimount_lift(&run.m, y1);
        for (side = -1; side <= 1; side += 2) { /* the cable going up (left) and the one coming down (right) */
            int cx0 = x0 + side * 9 - cam_x, cx1 = x1 + side * 9 - cam_x, top = 31;
            gfx_line(s, ox + cx0, oy + y0 - top - cam_y, ox + cx1, oy + y1 - top - cam_y, 0x30353Bu);
            /* the chairs: up the left cable, down the right, a pixel a step every other step */
            for (y = 0; y < LIFT_GAP; y += CHAIR_GAP) {
                int along = (y + (side < 0 ? LIFT_GAP * 8 - (int)(run.steps / 2) % LIFT_GAP : (int)(run.steps / 2))) % LIFT_GAP;
                int wy = y0 + along, wx = cx0 + (cx1 - cx0) * along / LIFT_GAP;
                if (wy - cam_y < -20 || wy - cam_y > FH + 40)
                    continue;
                art_draw(s, &art_thing[ART_CHAIR_SHADOW], ox + wx + 10, oy + wy - cam_y + 2);
                art_draw(s, &art_thing[ART_CHAIR], ox + wx, oy + wy - top - cam_y);
            }
        }
    }
}

static void draw_hares(struct gfx_surface *s, int ox, int oy, int cam_x, int cam_y)
{
    static const int hop[8] = { 0, 2, 4, 5, 5, 4, 2, 0 };
    int c;
    for (c = (cam_y < 0 ? 0 : cam_y) / SKI_CHUNK - 1; c <= (cam_y + FH + 16) / SKI_CHUNK; c++) {
        unsigned h = ((unsigned)c * 2654435761u) ^ (run.m.seed * 0x85EBCA6Bu);
        int y, half, left, width, t, x;
        h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12;
        if (c < 2 || h % 6 != 0 || (y = c * SKI_CHUNK + 40 + (int)(h >> 8) % 48) >= run.m.length - 80)
            continue;                   /* (a hare in one piece of six, now and then) */
        half = skimount_half(&run.m, y);
        left = skimount_centre(&run.m, y) - half - 70, width = 2 * half + 140;
        t = (int)(run.steps + (h >> 4)) % (2 * width);  /* across, and back: two pixels a step, hopping */
        x = left + (t < width ? t : 2 * width - t);
        art_draw(s, &art_thing[t < width ? ART_HARE_R : ART_HARE_L], ox + x - cam_x, oy + y - cam_y - hop[(run.steps / 3) % 8]);
    }
}

static void draw_field(struct gfx_surface *s, int ox, int oy)
{
    int px = run.x >> 8, py = run.y >> 8, cam_x = px - FW / 2, cam_y = py - SKIER_ROW, y, c, i, n, drawn = 0;
    int sx, sy, o, pose;
    if (cam_x < 0)
        cam_x = 0;
    if (cam_x > SKI_WORLD_W - FW)
        cam_x = SKI_WORLD_W - FW;
    /* the snow, in bands four pixels deep: the forest's, the powder, the piste */
    for (y = 0; y < FH; y += 4) {
        int wy = cam_y + y, mid = skimount_centre(&run.m, wy) - cam_x, half = skimount_half(&run.m, wy);
        int l = mid - half, r = mid + half;
        gfx_fill(s, ox, oy + y, FW, 4, DEEP);
        if (r + 36 > 0 && l - 36 < FW) {
            int a = l - 36 < 0 ? 0 : l - 36, b = r + 36 > FW ? FW : r + 36;
            gfx_fill(s, ox + a, oy + y, b - a, 4, POWDER);
            a = l < 0 ? 0 : l, b = r > FW ? FW : r;
            if (b > a)
                gfx_fill(s, ox + a, oy + y, b - a, 4, PISTE);
        }
    }
    /* the ice */
    for (c = (cam_y < 0 ? 0 : cam_y) / SKI_CHUNK - 1; c <= (cam_y + FH) / SKI_CHUNK + 1; c++) {
        int cx, cy, rx, ry, dy;
        if (!skimount_ice(&run.m, c, &cx, &cy, &rx, &ry))
            continue;
        for (dy = -ry; dy <= ry; dy += 2) {
            int half = rx * isqrt(ry * ry - dy * dy) / ry, yy = cy + dy - cam_y;
            if (yy >= 0 && yy < FH)
                gfx_fill(s, ox + cx - half - cam_x, oy + yy, half * 2, 2, ICE);
        }
    }
    /* the skier's tracks */
    for (i = 1; i < ntrack; i++) {
        int a = (track_at - ntrack + i - 1 + 2 * TRACKS) % TRACKS, b = (a + 1) % TRACKS;
        if (track[a].y < 0 || track[b].y < 0 || track[b].y - cam_y < -2 || track[a].y - cam_y > FH)
            continue;
        gfx_line(s, ox + track[a].x - 2 - cam_x, oy + track[a].y - cam_y, ox + track[b].x - 2 - cam_x, oy + track[b].y - cam_y, TRACK);
        gfx_line(s, ox + track[a].x + 2 - cam_x, oy + track[a].y - cam_y, ox + track[b].x + 2 - cam_x, oy + track[b].y - cam_y, TRACK);
    }
    /* the finish's banner, on the snow */
    y = run.m.length - cam_y;
    if (y > -8 && y < FH + 8) {
        int mid = skimount_centre(&run.m, run.m.length) - cam_x, half = skimount_half(&run.m, run.m.length), x;
        for (x = mid - half - 6; x < mid + half + 6; x += 8)
            gfx_fill(s, ox + x, oy + y - 3, 8, 6, ((x - mid + 4096) / 8) & 1 ? 0xCC0000u : 0xFFFFFFu);
    }
    /* the ghost: the best run's skier, a shape on the snow where that run was at this step */
    if (ghost_on && !watching && phase != P_READY && ghost_run.state != SKI_S_FALLEN) {
        int gx = (ghost_run.x >> 8) - cam_x, gy = (ghost_run.y >> 8) - cam_y;
        if (gx > -20 && gx < FW + 20 && gy > -20 && gy < FH + 20)
            art_draw_plain(s, &art_skier[POSE_SKI][(ghost_run.state == SKI_S_AIR ? ghost_run.heading + 2 * ghost_run.spin : ghost_run.heading) & 31],
                           ox + gx, oy + gy - skirun_height(&ghost_run), 0xB2B2B2u);
    }
    /* the skier, then everything in sight from the top down, the skier in their place among it */
    sx = px - cam_x, sy = py - cam_y;
    o = run.state == SKI_S_AIR ? run.heading + 2 * run.spin : run.heading;
    pose = run.state == SKI_S_AIR || !(keys_now & (SKI_K_TUCK | SKI_K_PLOUGH)) ? POSE_SKI : keys_now & SKI_K_PLOUGH ? POSE_PLOUGH : POSE_TUCK;
    for (c = (cam_y < 0 ? 0 : cam_y) / SKI_CHUNK - 1; c <= (cam_y + FH + 48) / SKI_CHUNK; c++) {
        const struct ski_thing *t;
        if (c < 0)
            continue;
        t = skirun_piece(&run, c, &n);
        for (i = 0; i <= n; i++) {
            int tx, ty, kind;
            if (!drawn && (i == n ? c == (cam_y + FH + 48) / SKI_CHUNK : t[i].y > py)) {        /* the skier's turn */
                drawn = 1;
                if (run.state == SKI_S_FALLEN) {
                    art_draw(s, &art_thing[ART_FALLEN], ox + sx, oy + sy);
                } else if (!run.grace || (run.steps & 4)) {     /* (just up again: flickering) */
                    art_draw(s, &art_thing[ART_SHADOW], ox + sx + 2, oy + sy + 3);
                    art_draw(s, &art_skier[pose][o & 31], ox + sx, oy + sy - skirun_height(&run));
                }
            }
            if (i == n)
                break;
            tx = t[i].x - cam_x, ty = t[i].y - cam_y, kind = t[i].kind;
            if (tx < -40 || tx > FW + 40 || ty < -8 || ty > FH + 48)
                continue;
            switch (kind) {
            case SKI_T_FIR: art_draw(s, &art_thing[ART_FIR], ox + tx, oy + ty); break;
            case SKI_T_PINE: art_draw(s, &art_thing[ART_PINE], ox + tx, oy + ty); break;
            case SKI_T_ROCK: art_draw(s, &art_thing[ART_ROCK], ox + tx, oy + ty); break;
            case SKI_T_FENCE:
                if (i + 1 < n && t[i + 1].kind == SKI_T_FENCE && t[i + 1].y == t[i].y && t[i + 1].x == t[i].x + 16)
                    art_draw(s, &art_thing[ART_NET], ox + tx, oy + ty);
                art_draw(s, &art_thing[ART_POST], ox + tx, oy + ty);
                break;
            case SKI_T_RAMP: art_draw(s, &art_thing[ART_RAMP], ox + tx, oy + ty); break;
            case SKI_T_MOGUL: art_draw(s, &art_thing[ART_MOGUL], ox + tx, oy + ty); break;
            case SKI_T_HUT: art_draw(s, &art_thing[ART_HUT], ox + tx, oy + ty); break;
            case SKI_T_GATE_L: art_draw(s, &art_thing[ART_GATE_L], ox + tx, oy + ty); break;
            case SKI_T_GATE_R: art_draw(s, &art_thing[ART_GATE_R], ox + tx, oy + ty); break;
            case SKI_T_FINISH_L: case SKI_T_FINISH_R: art_draw(s, &art_thing[ART_FINISH], ox + tx, oy + ty); break;
            case SKI_T_PYLON: art_draw(s, &art_thing[ART_PYLON], ox + tx, oy + ty); break;
            }
        }
    }
    {                                   /* the other skiers in sight (and a little beyond: their shadows) */
        int c, x, yy, moving, heading;
        for (c = (cam_y - 40) / SKI_CHUNK - 3; c <= (cam_y + FH + 40) / SKI_CHUNK; c++)
            if (c >= 0 && skirun_other(&run, c, &x, &yy, &moving, &heading) && yy - cam_y > -30 && yy - cam_y < FH + 30) {
                art_draw(s, &art_thing[ART_SHADOW], ox + x - cam_x + 2, oy + yy - cam_y + 3);
                art_draw(s, &art_other[c % OTHER_JACKETS][heading & 31], ox + x - cam_x, oy + yy - cam_y);
            }
    }
    {                                   /* the groomers: the snow groomed behind each, then itself */
        int c, x, gy, dir, moving, done, k;
        for (c = (cam_y - 40) / SKI_CHUNK - 1; c <= (cam_y + FH + 40) / SKI_CHUNK; c++)
            if (c >= 0 && skirun_groomer(&run, c, &x, &gy, &dir, &moving, &done) && gy - cam_y > -30 && gy - cam_y < FH + 30) {
                int x0 = x - dir * done;
                for (k = 0; k < 4 && done > SKI_GROOMER_W / 2; k++)      /* (its ridges, behind it) */
                    gfx_fill(s, ox + (dir > 0 ? x0 : x + SKI_GROOMER_W / 2) - cam_x, oy + gy - cam_y + 4 + k * 4 - 8,
                             done - SKI_GROOMER_W / 2, 1, 0xDDE6EE);
                art_draw(s, &art_thing[dir > 0 ? ART_GROOMER_R : ART_GROOMER_L], ox + x - cam_x, oy + gy - cam_y);
            }
    }
    draw_lift(s, ox, oy, cam_x, cam_y);
    draw_hares(s, ox, oy, cam_x, cam_y);
    draw_weather(s, ox, oy, px - cam_x, py - cam_y);
    /* what is said over it */
    if (phase == P_READY) {
        char l1[80], l2[80];
        const char *lines[4];
        snprintf(l1, sizeof l1, "%s, %s", mode == SKI_SLALOM ? GUI_T("Slalom") : GUI_T("Free run"), GUI_T(grade_name[grade]));
        if (of_day)
            snprintf(l2, sizeof l2, "%s", GUI_T("The mountain of the day"));
        else
            snprintf(l2, sizeof l2, GUI_T("Mountain %u"), seed);
        lines[0] = GUI_T(GAME_NAME), lines[1] = l1, lines[2] = l2, lines[3] = GUI_T("Enter starts. The arrows steer, tuck and brake.");
        box_text(s, ox, oy, 190, lines, 4);
    } else if (phase == P_PAUSED && !held) {
        const char *lines[2];
        lines[0] = GUI_T("Paused"), lines[1] = GUI_T("Esc goes on.");
        box_text(s, ox, oy, 200, lines, 2);
    } else if (phase == P_DONE) {
        char l1[80], t[24];
        const char *lines[3];
        time_text(skirun_ms(&run), t, sizeof t);
        if (mode == SKI_SLALOM && run.gates_missed)
            snprintf(l1, sizeof l1, GUI_T("%s, with %d gates missed"), t, run.gates_missed);
        else if (mode == SKI_SLALOM)
            snprintf(l1, sizeof l1, GUI_T("%s, every gate"), t);
        else
            snprintf(l1, sizeof l1, GUI_T("%ld points in %s"), run.score, t);
        lines[0] = watching ? GUI_T("The careful skier is at the valley station") : GUI_T("The valley station!"), lines[1] = l1;
        lines[2] = GUI_T("F2: again. F3: another mountain.");
        box_text(s, ox, oy, 200, lines, 3);
    } else if (banner[0] && run.steps < banner_until) {
        int tw = gfx_text_width(gui_bold, banner, (int)strlen(banner));
        gfx_fill(s, ox + (FW - tw) / 2 - 8, oy + 40, tw + 16, 20, 0xFFFFFFu);
        gfx_frame(s, ox + (FW - tw) / 2 - 8, oy + 40, tw + 16, 20, 0x333333u);
        text_at(s, ox + (FW - tw) / 2, oy + 44, gui_bold, banner, 0xCC0000u);
    }
}

static void strip_line(struct gfx_surface *s, int x, int *y, const char *label, const char *value)
{
    text_at(s, x, *y, gui_font, label, GUI_ROLE(GLOS_ROLE_DIM));
    text_at(s, x, *y + 14, gui_bold, value, GUI_TEXT);
    *y += 36;
}

static void draw_strip(struct gfx_surface *s, int x0, int oy)
{
    char v[48];
    int x = x0 + 10, y = oy + 10;
    gfx_fill(s, x0, oy, SW, FH, GUI_FACE);
    gfx_fill(s, x0, oy, 1, FH, GUI_LINE);
    gfx_fill(s, x, y, 12, 12, grade_rgb[grade]);
    text_at(s, x + 18, y, gui_bold, GUI_T(grade_name[grade]), GUI_TEXT);
    y += 18;
    text_at(s, x, y, gui_font, mode == SKI_SLALOM ? GUI_T("Slalom") : GUI_T("Free run"), GUI_TEXT);
    y += 26;
    time_text(skirun_ms(&run), v, sizeof v);
    strip_line(s, x, &y, GUI_T("Time"), v);
    snprintf(v, sizeof v, GUI_T("%d km/h"), skirun_speed(&run) / 4);
    strip_line(s, x, &y, GUI_T("Speed"), v);
    if (mode == SKI_SLALOM) {
        snprintf(v, sizeof v, GUI_T("%d of %d"), run.gates_passed, run.m.ngates);
        strip_line(s, x, &y, GUI_T("Gates"), v);
        snprintf(v, sizeof v, "%d", run.gates_missed);
        strip_line(s, x, &y, GUI_T("Missed"), v);
    } else {
        snprintf(v, sizeof v, "%ld", run.score);
        strip_line(s, x, &y, GUI_T("Points"), v);
        snprintf(v, sizeof v, "%d", run.turns);
        strip_line(s, x, &y, GUI_T("Half turns landed"), v);
    }
    snprintf(v, sizeof v, "%d", run.falls);
    strip_line(s, x, &y, GUI_T("Falls"), v);
    snprintf(v, sizeof v, GUI_T("%d m to go"), run.m.length > (run.y >> 8) ? (run.m.length - (run.y >> 8)) / 4 : 0);
    strip_line(s, x, &y, GUI_T("The valley"), v);
    if (of_day)
        snprintf(v, sizeof v, "%s", GUI_T("of the day"));
    else
        snprintf(v, sizeof v, "%u", seed);
    strip_line(s, x, &y, GUI_T("Mountain"), v);
    if (watching)
        text_at(s, x, oy + FH - 22, gui_font, GUI_T("The careful skier"), GUI_ROLE(GLOS_ROLE_DIM));
}

/* (2026-11-23) The game at double size (View, Double Size: a window twice as large, on a screen that has room) or
   full screen (View, Full Screen: the game in the middle, black round it, doubled where the screen has room for
   that). Doubled, the field and the strip are drawn at their size into memory, then each pixel made four. */
static int doubled, full;
static unsigned *one_px, *big_px;       /* (doubled) the picture at its size, and at twice it */

static int scale_now(void) { return doubled ? 2 : 1; }

static void game_corner(const struct gui_widget *w, int *ox, int *oy)  /* where the game's picture begins */
{
    int sc = scale_now();
    *ox = w->r.x + (w->r.w > (FW + SW) * sc ? (w->r.w - (FW + SW) * sc) / 2 : 0);
    *oy = w->r.y + (w->r.h > FH * sc ? (w->r.h - FH * sc) / 2 : 0);
}

static void draw(struct gui_widget *w, struct gfx_surface *s)
{
    struct gfx_rect was = s->clip, field;
    int ox, oy, sc = scale_now();
    game_corner(w, &ox, &oy);
    if (w->r.w > (FW + SW) * sc || w->r.h > FH * sc) { /* (full screen: black round the game) */
        gfx_fill(s, w->r.x, w->r.y, w->r.w, oy - w->r.y, 0);
        gfx_fill(s, w->r.x, oy + FH * sc, w->r.w, w->r.y + w->r.h - oy - FH * sc, 0);
        gfx_fill(s, w->r.x, oy, ox - w->r.x, FH * sc, 0);
        gfx_fill(s, ox + (FW + SW) * sc, oy, w->r.x + w->r.w - ox - (FW + SW) * sc, FH * sc, 0);
    }
    if (sc == 2) {                      /* (at its size into memory, then doubled) */
        struct gfx_surface one;
        int x, y, W = FW + SW;
        if (!one_px && !(one_px = malloc((size_t)W * FH * 4)))
            return;
        if (!big_px && !(big_px = malloc((size_t)W * 2 * FH * 2 * 4)))
            return;
        memset(&one, 0, sizeof one);
        one.pix = (unsigned char *)one_px, one.w = W, one.h = FH, one.pitch = (unsigned)W * 4, one.fmt = gfx_fmt888;
        one.clip.x = 0, one.clip.y = 0, one.clip.w = FW, one.clip.h = FH;
        draw_field(&one, 0, 0);
        one.clip.w = W;
        draw_strip(&one, FW, 0);
        for (y = 0; y < FH; y++) {
            const unsigned *a = one_px + (size_t)y * W;
            unsigned *b = big_px + (size_t)y * 2 * W * 2;
            for (x = 0; x < W; x++)
                b[2 * x] = b[2 * x + 1] = a[x] & 0xFFFFFF;
            memcpy(b + W * 2, b, (size_t)W * 2 * 4);
        }
        gfx_image(s, ox, oy, big_px, W * 2, FH * 2, (unsigned)W * 2);
        return;
    }
    field.x = ox, field.y = oy, field.w = FW, field.h = FH;
    if (gfx_rect_cut(&field, &was)) {
        gfx_clip(s, &field);
        draw_field(s, ox, oy);
        gfx_clip(s, &was);
    }
    {                                   /* the strip, when its pixels are asked for (frame: every third time) */
        struct gfx_rect strip = { ox + FW, oy, SW, FH };
        if (gfx_rect_cut(&strip, &was))
            draw_strip(s, ox + FW, oy);
    }
}

static void size_game(void)             /* the canvas as large as the window's way needs, the window round it */
{
    int sw = 640, sh = 480, sc = scale_now();
    gui_screen_size(&sw, &sh);
    if (full)
        gui_size(canvas, sw, sh - 24);  /* (the menu's bar above it) */
    else
        gui_size(canvas, (FW + SW) * sc, FH * sc);
    if (!full)
        gui_fit(win);
    gui_damage(canvas);
}

static void view_double(void)
{
    int sw = 640, sh = 480;
    gui_screen_size(&sw, &sh);
    if (!doubled && (sw < (FW + SW) * 2 + 20 || sh < FH * 2 + 80)) {
        say("double refused %dx%d", sw, sh);
        gui_message(win, GUI_T(GAME_NAME), GUI_T("The screen is too small for the game at double size: it wants 1280 by "
                                                  "800 at least."), GUI_MB_OK);
        return;
    }
    doubled = !doubled;
    size_game();
    say("double %s", doubled ? "on" : "off");
}

static void view_full(void)
{
    int sw = 640, sh = 480;
    full = !full;
    gui_screen_size(&sw, &sh);
    if (full)                           /* (doubled where the screen has room for it) */
        doubled = sw >= (FW + SW) * 2 && sh - 24 >= FH * 2;
    else
        doubled = 0;
    glos_show(win->id, full ? GLOS_WS_FULL : GLOS_WS_NORMAL);
    size_game();
    say("full %s %dx%d%s", full ? "on" : "off", sw, sh, doubled ? " doubled" : "");
}

/* ---- the tables' window */

static struct gui_widget *t_which, *t_list;

static void tables_fill(void)
{
    int which = gui_value(t_which), m = which < 8 ? which / 4 : which - 8, i;
    const struct ski_table *t = which < 8 ? &tables.grade[m][which % 4] : &tables.day[m];
    gui_list_clear(t_list);
    for (i = 0; i < t->n; i++) {
        char place[8], result[32], mountain[16], day[16];
        const char *cells[5];
        snprintf(place, sizeof place, "%d", i + 1);
        if (m == SKI_SLALOM)
            time_text(t->e[i].ms, result, sizeof result);
        else
            snprintf(result, sizeof result, "%ld", t->e[i].score);
        snprintf(mountain, sizeof mountain, "%u", t->e[i].seed);
        gui_date_text(t->e[i].day, day, sizeof day);
        cells[0] = place, cells[1] = t->e[i].name, cells[2] = result, cells[3] = which < 8 ? mountain : "", cells[4] = day;
        gui_list_add(t_list, cells, NULL, NULL);
    }
    say("tables %d rows=%d", which, t->n);
}

static void tables_on(struct gui_widget *w, int what)
{
    (void)w;
    if (what == GUI_ON_SELECT || what == GUI_ON_CHANGE)
        tables_fill();
}

static void tables_close(struct gui_widget *b, int what)
{
    (void)what;
    gui_end_modal(b->win, GUI_OK);
}

static void tables_show(void)
{
    static const char *const heads[] = { GUI_N("Place"), GUI_N("Name"), GUI_N("Result"), GUI_N("Mountain"), GUI_N("Day") };
    static const int widths[] = { 44, 130, 80, 80, 86 };
    struct gui_window *d = gui_window(GUI_T("Best Runs"), 460, 0, GUI_W_DIALOG);
    struct gui_widget *box = gui_vbox(d->root, 6, 8), *row;
    char text[64];
    int m, g;
    d->owner = win;
    t_which = gui_choice(gui_row(gui_form(box), GUI_T("&Table:"), NULL), 26);
    for (m = 0; m < 2; m++)
        for (g = 0; g < 4; g++) {
            snprintf(text, sizeof text, "%s, %s", m == SKI_SLALOM ? GUI_T("Slalom") : GUI_T("Free run"), GUI_T(grade_name[g]));
            gui_choice_add(t_which, text);
        }
    gui_choice_add(t_which, GUI_T("The mountain of the day, slalom"));
    gui_choice_add(t_which, GUI_T("The mountain of the day, free run"));
    gui_set_value(t_which, of_day ? 8 + mode : mode * 4 + grade);
    gui_on(t_which, tables_on, 0);
    t_list = gui_list(box, 5, heads, widths);
    gui_size(t_list, 440, 200);
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("Close"), tables_close, 1)->flags |= GUI_DEFAULT | GUI_CANCEL;
    tables_fill();
    asking = 1;
    gui_modal(d);
    asking = 0;
    last_ms = glos_ticks();
}

/* ---- commands and keys */

static unsigned new_seed(void)
{
    static unsigned n;
    unsigned v = (glos_ticks() * 2654435761u) ^ ((unsigned)time(NULL) * 40503u) ^ (++n * 977u);
    return v % 999999u + 1;             /* (six figures: a number to say to a friend) */
}

static void command(struct gui_window *w, int id)
{
    (void)w;
    someone();
    if (attract && id != C_EXIT && id != C_SOUND && id != C_DOUBLE && id != C_FULL)  /* (the game back, then what was asked) */
        attract_end();
    if (id == C_DOUBLE) {
        if (!full)
            view_double();
        return;
    }
    if (id == C_FULL) {
        view_full();
        return;
    }
    if (id >= C_WEATHER && id < C_WEATHER + W_COUNT) {
        weather = id - C_WEATHER;
        tables_keep();
        gui_damage(canvas);
        say("weather %d", weather);
        return;
    }
    if (id >= C_FIGURE && id < C_FIGURE + FIGURE_COUNT) {
        figure = id - C_FIGURE;
        art_make(figure, jackets[jacket].rgb);
        tables_keep();
        gui_damage(canvas);
        say("figure %d", figure);
        return;
    }
    if (id >= C_JACKET && id < C_JACKET + 5) {
        jacket = id - C_JACKET;
        art_make(figure, jackets[jacket].rgb);
        tables_keep();
        gui_damage(canvas);
        say("jacket %d", jacket);
        return;
    }
    switch (id) {
    case C_AGAIN: ready(); break;
    case C_NEW:
        seed = new_seed();
        of_day = 0;
        ready();
        break;
    case C_DAY:
        seed = skimount_day(today / 10000, today / 100 % 100, today % 100);
        of_day = 1;
        ready();
        break;
    case C_NUMBER: {
        char text[16];
        unsigned long v;
        snprintf(text, sizeof text, "%u", of_day ? 0 : seed);
        asking = 1;
        if (gui_ask(win, GUI_T("A Mountain's Number"), GUI_T("&Number (1 to 999999):"), text, sizeof text) == 1
            && (v = strtoul(text, NULL, 10)) >= 1 && v <= 999999) {
            seed = (unsigned)v;
            of_day = 0;
            ready();
        } else {
            say("number left");
        }
        asking = 0;
        break;
    }
    case C_WATCH:
        ready();
        start(1);
        break;
    case C_TABLES: tables_show(); break;
    case C_PAUSE: pause_toggle(); break;
    case C_SOUND:
        sound_on = !sound_on;
        if (sound_on)
            sounding = sound_start();
        else
            sound_stop(), sounding = 0;
        say("sound %d playing=%d", sound_on, sounding);
        break;
    case C_SLALOM: case C_FREE:
        mode = id == C_SLALOM ? SKI_SLALOM : SKI_FREE;
        tables_keep();
        ready();
        break;
    case C_GREEN: case C_BLUE: case C_RED: case C_BLACK:
        grade = id - C_GREEN;
        tables_keep();
        ready();
        break;
    case C_EXIT: gui_close(win); break;
    }
}

static void update(struct gui_window *w)
{
    int i;
    gui_command_check(w, C_SLALOM, mode == SKI_SLALOM), gui_command_check(w, C_FREE, mode == SKI_FREE);
    for (i = 0; i < 4; i++)
        gui_command_check(w, C_GREEN + i, grade == i);
    for (i = 0; i < 5; i++)
        gui_command_check(w, C_JACKET + i, jacket == i);
    for (i = 0; i < FIGURE_COUNT; i++)
        gui_command_check(w, C_FIGURE + i, figure == i);
    for (i = 0; i < W_COUNT; i++)
        gui_command_check(w, C_WEATHER + i, weather == i);
    gui_command_check(w, C_DAY, of_day);
    gui_command_check(w, C_SOUND, sound_on);
    gui_command_enable(w, C_PAUSE, phase == P_RUN || phase == P_PAUSED);
}

static int key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    if (e->type != GUI_EV_KEY)
        return 0;
    someone();
    if (e->mods)
        return 0;
    if (attract_end())                  /* (the key gives the game back, and does nothing more) */
        return 1;
    switch (e->key) {
    case GUI_K_ENTER: case GUI_K_SPACE:
        if (phase == P_READY)
            start(0);
        else if (phase == P_PAUSED)
            pause_toggle();
        else if (phase == P_DONE)
            ready();
        return 1;
    case GUI_K_ESC: case 0x19:          /* (P) */
        if (phase == P_RUN || phase == P_PAUSED)
            pause_toggle();
        return 1;
    case GUI_K_LEFT: case GUI_K_RIGHT: case GUI_K_UP: case GUI_K_DOWN:
    case 0x1E: case 0x20: case 0x11: case 0x1F:
        return 1;                       /* (the skier's: read as held, each step) */
    }
    return 0;
}

static void build(void)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    int i;
    win = gui_window(GUI_T(GAME_NAME), 0, 0, GUI_W_FIXED);
    win->on_command = command;
    win->on_update = update;
    win->on_key = key;
    gui_window_icon(win, "app-games");
    gui_command(win, C_AGAIN, GUI_T("Run &Again"), "F2", NULL, NULL);
    gui_command(win, C_NEW, GUI_T("&New Mountain"), "F3", NULL, NULL);
    gui_command(win, C_DAY, GUI_T("The Mountain of the &Day"), "F4", NULL, NULL);
    gui_command(win, C_NUMBER, GUI_T("A Mountain's N&umber..."), "Ctrl+G", NULL, NULL);
    gui_command(win, C_WATCH, GUI_T("&Watch the Careful Skier"), "F9", NULL, NULL);
    gui_command(win, C_TABLES, GUI_T("&Best Runs..."), "Ctrl+B", NULL, NULL);
    gui_command(win, C_PAUSE, GUI_T("&Pause"), NULL, NULL, NULL);
    gui_command(win, C_SOUND, GUI_T("&Sound"), NULL, NULL, NULL);
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    gui_command(win, C_SLALOM, GUI_T("&Slalom"), NULL, NULL, NULL);
    gui_command(win, C_FREE, GUI_T("&Free Run"), NULL, NULL, NULL);
    for (i = 0; i < 4; i++) {
        static const char *const names[4] = { GUI_N("&Green"), GUI_N("&Blue"), GUI_N("&Red"), GUI_N("Blac&k") };
        gui_command(win, C_GREEN + i, GUI_T(names[i]), NULL, NULL, NULL);
    }
    for (i = 0; i < 5; i++)
        gui_command(win, C_JACKET + i, GUI_T(jackets[i].name), NULL, NULL, NULL);
    for (i = 0; i < FIGURE_COUNT; i++)
        gui_command(win, C_FIGURE + i, GUI_T(figures[i]), NULL, NULL, NULL);
    for (i = 0; i < W_COUNT; i++) {
        static const char *const weathers[W_COUNT] = { GUI_N("&Clear"), GUI_N("&Snowfall"), GUI_N("&Fog"), GUI_N("&Night"),
                                                             GUI_N("&Wind") };
        gui_command(win, C_WEATHER + i, GUI_T(weathers[i]), NULL, NULL, NULL);
    }
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&Game"));
    gui_menu_command(m, C_AGAIN), gui_menu_command(m, C_PAUSE), gui_menu_command(m, C_WATCH);
    gui_menu_sep(m);
    gui_menu_command(m, C_TABLES), gui_menu_command(m, C_SOUND);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Mountain"));
    gui_menu_command(m, C_NEW), gui_menu_command(m, C_DAY), gui_menu_command(m, C_NUMBER);
    gui_menu_sep(m);
    gui_menu_command(m, C_SLALOM), gui_menu_command(m, C_FREE);
    gui_menu_sep(m);
    for (i = 0; i < 4; i++)
        gui_menu_command(m, C_GREEN + i);
    m = gui_menu(bar, GUI_T("&Skier"));         /* the figure, then its jacket */
    for (i = 0; i < FIGURE_COUNT; i++)
        gui_menu_command(m, C_FIGURE + i);
    gui_menu_sep(m);
    for (i = 0; i < 5; i++)
        gui_menu_command(m, C_JACKET + i);
    gui_command(win, C_DOUBLE, GUI_T("&Double Size"), "Ctrl+D", NULL, GUI_T("The game twice as large (on a screen with room)"));
    gui_command(win, C_FULL, GUI_T("&Full Screen"), "F11", NULL, GUI_T("The game alone on the screen (F11 again: back)"));
    m = gui_menu(bar, GUI_T("&View"));
    gui_menu_command(m, C_DOUBLE), gui_menu_command(m, C_FULL);
    m = gui_menu(bar, GUI_T("&Weather"));
    for (i = 0; i < W_COUNT; i++)
        gui_menu_command(m, C_WEATHER + i);
    canvas = gui_canvas(win->root, FW + SW, FH, draw, field_event);
}

int main(int argc, char **argv)
{
    const char *home;
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    int i, demo = 0, free_run = 0, demo_grade = -1;
    if (gui_init(argc, argv) != 0) {
        printf("SKI no GLOS\n");
        return 1;
    }
    today = tm ? (tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday : 20260101;
    home = glos_home();
    if (home && home[0])
        gui_path_join(table_path, sizeof table_path, home, "SKI.DAT");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-demo") && i + 1 < argc)
            demo = suite = 1, seed = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-grade") && i + 1 < argc)
            demo_grade = atoi(argv[++i]) & 3;
        else if (!strcmp(argv[i], "-free"))
            free_run = 1;
        else if (!strcmp(argv[i], "-until") && i + 1 < argc) {       /* -until 300,900,1500 */
            const char *p = argv[++i];
            until = atol(p);
            while ((p = strchr(p, ',')) != NULL && nuntils < 8)
                untils[nuntils++] = atol(++p);
            suite = 1;                  /* (a run of the player's too: every step that is due made, held at those) */
        }
        else if (!strcmp(argv[i], "-table") && i + 1 < argc)
            snprintf(table_path, sizeof table_path, "%s", argv[++i]);
        else if (!strcmp(argv[i], "-speed") && i + 1 < argc)
            speed = atoi(argv[++i]) >= 1 && atoi(argv[i]) <= 16 ? atoi(argv[i]) : 1;
        else if (!strcmp(argv[i], "-day") && i + 1 < argc)
            today = atoi(argv[++i]);    /* (the suite's: a day that does not change) */
        else if (!strcmp(argv[i], "-attract") && i + 1 < argc)
            attract_ms = (unsigned)atol(argv[++i]), attract_given = 1;  /* (milliseconds; 0 never) */
        else if (!strcmp(argv[i], "-weather") && i + 1 < argc)
            arg_weather = atoi(argv[++i]);      /* (the suite's: a weather from the first step) */
    }
    if (suite && !attract_given)        /* (the suite's runs: none by itself unless asked for) */
        attract_ms = 0;
    tables_load();
    ghost_load();
    grade = tables.grade_last, mode = tables.mode_last, jacket = tables.jacket < 5 ? tables.jacket : 0;
    figure = tables.figure < FIGURE_COUNT ? tables.figure : 0;
    weather = tables.weather < W_COUNT ? tables.weather : 0;
    if (arg_weather >= 0 && arg_weather < W_COUNT)
        weather = arg_weather;
    if (!demo)
        seed = new_seed();
    if (demo_grade >= 0)
        grade = demo_grade;
    if (demo)
        mode = free_run ? SKI_FREE : SKI_SLALOM;
    art_make(figure, jackets[jacket].rgb);
    build();
    ready();
    gui_show(win, 1);
    if (!suite)                         /* (the suite's runs are silent: its clock is not the sound's) */
        sounding = sound_start();
    gui_idle(frame, NULL, 33);          /* thirty pictures a second (the motion is sixty steps, whatever the pictures) */
    if (demo && until >= 0)
        start(1);
    started_ms = idle_ms = glos_ticks();
    gui_run();
    sound_stop();
    if (suite) {                                /* (the suite's, and the benches': the frames a second drawn) */
        unsigned ms = glos_ticks() - started_ms;
        printf("SKI frames=%u ms=%u fps=%u.%u\n", frames, ms, ms ? frames * 1000 / ms : 0, ms ? frames * 10000 / ms % 10 : 0);
    }
    printf("SKI end\n");
    return 0;
}
