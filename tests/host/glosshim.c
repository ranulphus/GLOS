/* glosshim.c - libglos on the host, for the toolkit's and the apps' tests
 * (docs/milestones-m9.md, "How the apps are tested").
 *
 * An app built for the host (its main() renamed app_main) runs here as on
 * a GLOS desktop: its windows are pixels in memory in the screen's format,
 * placed as the window manager places them, and its events come from a
 * script, the same script the Loop A suite drives the real app with over
 * ssh (tests/gui/NAME.txt). A shot writes a window's client area as a PNG,
 * the golden picture the real one must equal.
 *
 *   glosshim SCRIPT OUTDIR BPP        (GLOS_HOST_DRIVES: the drives' directories)
 *
 * The script, a command a line ("#" a comment; a title in quotes):
 *   run ARGS...                the app's arguments (the first line)
 *   wait "TITLE"               (Loop A: until the window is there; here it is)
 *   key SPEC...                keys: ctrl+s, alt+f, shift+tab, f10, down, enter, a ...
 *   type TEXT                  the rest of the line, as glos type has it (\n \t \e \b \\)
 *   click "TITLE" X Y [right] [double]
 *   drag "TITLE" X0 Y0 X1 Y1
 *   pause MS                   time passes (timers run)
 *   launch, syskey             the shell's events: the launcher (Ctrl+Esc), the task list (Ctrl+Alt+Del)
 *   clip TEXT                  the clipboard's text (glos clip)
 *   shot NAME "TITLE" [X Y W H]   OUTDIR/NAME-BPP.png: that window's client area (the rectangle, if given,
 *                              black: what differs from run to run, a date, a count)
 *   gone "TITLE"               (Loop A: until it is gone)
 *   expect TEXT                (Loop A: a line the app wrote, checked when it has ended)
 *   file PATH TEXT             the file's bytes are TEXT (C escapes: \r \n \t \\ \xNN)
 * When the script is done and the app waits for an event for ever, the run
 * ends (status 0). Its lines on stdout: "SHOT name WxH", "SCRIPT-ERROR ...". */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "glos/glos.h"
#include "glos/config.h"
#include "glos/gfx.h"
#include "keymap.h"
#include "png.h"

int app_main(int argc, char **argv);

#define MAXWIN 64

/* A window's flags here, and an event as the script makes it (glos_event() gives it as API 2.0 has it). */
#define HW_FIXED    1
#define HW_BARE     2
#define HW_ANYWHERE 4
#define HW_POPUP    8
#define HW_DESKTOP  16
enum { HEV_KEY = 1, HEV_MOUSE, HEV_FOCUS, HEV_RESIZE, HEV_CLOSE, HEV_LAUNCH, HEV_SYSKEY };
struct hev {
    unsigned type, window, time;
    int x, y;
    unsigned buttons, key, ch, mods, down;
};

struct hwin {
    unsigned id;
    int x, y, w, h;                     /* the client area on the screen */
    unsigned flags;
    char title[48];
    unsigned char *pix;
    unsigned pitch;
    int shown, used;
    unsigned z;                         /* raised order: the higher, the more on top */
};

static struct {
    struct hwin win[MAXWIN];
    unsigned next_id, ztop, focus;
    int sw, sh;
    struct gfx_fmt fmt;
    unsigned clock;
    struct hev q[256];
    unsigned qh, qt;
    FILE *script;
    const char *out;
    int bpp, line, done, settled;
    char *clip;
    unsigned clip_n;
    int px, py;                         /* the pointer */
    unsigned buttons;
} H;

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("SCRIPT-ERROR line %d: ", H.line);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    exit(3);
}

static struct hwin *find(unsigned id)
{
    int i;
    for (i = 0; i < MAXWIN; i++)
        if (H.win[i].used && H.win[i].id == id)
            return &H.win[i];
    return NULL;
}

static struct hwin *by_title(const char *t)
{
    struct hwin *best = NULL;
    int i;
    for (i = 0; i < MAXWIN; i++)
        if (H.win[i].used && H.win[i].shown && !strcmp(H.win[i].title, t) && (!best || H.win[i].z > best->z))
            best = &H.win[i];
    return best;
}

static void push(const struct hev *e)
{
    if (H.qt - H.qh >= 256)
        fail("event queue full");
    H.q[H.qt++ % 256] = *e;
}

static void ev_focus(unsigned id, int down)
{
    struct hev e;
    if (!id || !find(id))
        return;
    memset(&e, 0, sizeof e);
    e.type = HEV_FOCUS;
    e.window = id;
    e.time = H.clock;
    e.down = (unsigned)down;
    push(&e);
}

static void set_focus(unsigned id)
{
    struct hwin *w = find(id);
    if (getenv("GLOS_SHIM_DEBUG"))
        printf("DEBUG focus %u -> %u \"%s\"\n", H.focus, id, w ? w->title : "?");
    if (H.focus == id)
        return;
    if (H.focus)
        ev_focus(H.focus, 0);
    H.focus = id;
    if (w) {
        w->z = ++H.ztop;
        ev_focus(id, 1);
    }
}

/* The window to have the focus when the focus's goes: the topmost shown that isn't a pop-up. */
static void refocus(void)
{
    struct hwin *best = NULL;
    int i;
    for (i = 0; i < MAXWIN; i++)
        if (H.win[i].used && H.win[i].shown && !(H.win[i].flags & HW_POPUP) && (!best || H.win[i].z > best->z))
            best = &H.win[i];
    H.focus = 0;
    if (best)
        set_focus(best->id);
}

/* ---- libglos */

int glos_version(struct glos_version *v)
{
    unsigned size = v->size;
    memset(v, 0, sizeof *v);
    v->size = size;
    v->major = GLOS_MAJOR, v->minor = GLOS_MINOR;
    strcpy(v->build, "host");
    return 0;
}
int glos_running(void) { return 1; }
int glos_init(const char *name, const char *help, const char *category) { (void)name, (void)help, (void)category; return 0; }
int glos_error(void) { return GLOS_E_REFUSED; }         /* (what gives a handle and gave none: not on the host) */
unsigned glos_app(void) { return 1; }
int glos_yield(void) { return 0; }
int glos_sleep(unsigned ms) { H.clock += ms; return 0; }
unsigned glos_ticks(void) { return H.clock; }
void *glos_ptr(unsigned linear) { return (void *)(size_t)linear; }

int glos_screen(struct glos_screen *s)
{
    unsigned size = s->size;
    memset(s, 0, sizeof *s);
    s->size = size;
    s->w = H.sw, s->h = H.sh;
    s->format.bpp = H.fmt.bpp;
    s->format.rpos = H.fmt.rpos, s->format.rsize = H.fmt.rsize, s->format.gpos = H.fmt.gpos, s->format.gsize = H.fmt.gsize;
    s->format.bpos = H.fmt.bpos, s->format.bsize = H.fmt.bsize;
    s->work_w = H.sw, s->work_h = H.sh - 20;
    return 0;
}

static unsigned char *pixels(int w, int h, unsigned *pitch)
{
    *pitch = (unsigned)w * (H.fmt.bpp / 8u);
    return calloc(1, (size_t)*pitch * (size_t)h + 16);
}

glos_handle glos_window(const char *title, int x, int y, int cw, int ch, unsigned kind, unsigned flags, glos_handle owner)
{
    struct hwin *w = NULL;
    unsigned hf = (flags & GLOS_WF_FIXED ? HW_FIXED : 0u) | (flags & GLOS_WF_BARE ? HW_BARE : 0u)
                  | (kind == GLOS_WK_POPUP ? HW_POPUP : 0u) | (kind == GLOS_WK_DESKTOP ? HW_DESKTOP : 0u);
    int i, framed = !(hf & (HW_BARE | HW_POPUP));
    (void)owner;
    if (sizeof(void *) > 4)
        fail("the shim needs a 32-bit build");
    for (i = 0; i < MAXWIN && !w; i++)
        if (!H.win[i].used)
            w = &H.win[i];
    if (!w || cw < 1 || ch < 1)
        return 0;
    memset(w, 0, sizeof *w);
    w->used = 1;
    w->id = ++H.next_id + 1;                    /* (the task bar is the kernel's first) */
    w->flags = hf;
    if (hf & HW_DESKTOP) {                      /* (the window manager's: the screen less the task bar) */
        x = y = 0, cw = H.sw, ch = H.sh - 20;
        framed = 0;
    }
    if (flags & GLOS_WF_ANYWHERE) {             /* (as the window manager places them: on the screen) */
        int fw = cw + (framed ? 2 : 0), fh = ch + (framed ? 21 : 0);
        w->x = 24 + (int)(w->id % 8) * 24, w->y = 16 + (int)(w->id % 8) * 24;
        if (w->x + fw > H.sw)
            w->x = H.sw - fw > 0 ? H.sw - fw : 0;
        if (w->y + fh > H.sh - 20)
            w->y = H.sh - 20 - fh > 0 ? H.sh - 20 - fh : 0;
    } else
        w->x = x, w->y = y;
    if (framed)
        w->x += 1, w->y += 20;                  /* (the frame's corner given: the client area inside it) */
    w->w = cw, w->h = ch;
    snprintf(w->title, sizeof w->title, "%s", title ? title : "");
    w->pix = pixels(w->w, w->h, &w->pitch);
    w->z = ++H.ztop;
    return w->id;
}

/* What a handle is here: a window (its id), or one of the others (a watch: from 1000h). */
int glos_close(glos_handle id)
{
    struct hwin *w = find(id);
    if (id >= 0x1000)
        return 0;
    if (!w)
        return GLOS_E_HANDLE;
    free(w->pix);
    w->used = 0;
    if (H.focus == id) {
        H.focus = 0;
        refocus();
    }
    return 0;
}

int glos_show(glos_handle id, unsigned state)
{
    struct hwin *w = find(id);
    if (!w)
        return GLOS_E_HANDLE;
    w->shown = state != GLOS_WS_HIDDEN;
    if (w->shown)
        set_focus(id);
    else if (H.focus == id)
        refocus();
    return 0;
}

int glos_move(glos_handle id, int x, int y)
{
    struct hwin *w = find(id);
    int framed;
    if (!w)
        return GLOS_E_HANDLE;
    framed = !(w->flags & (HW_BARE | HW_POPUP));
    w->x = x + (framed ? 1 : 0), w->y = y + (framed ? 20 : 0);
    return 0;
}

int glos_resize(glos_handle id, int cw, int ch)
{
    struct hwin *w = find(id);
    unsigned pitch;
    unsigned char *p;
    int y, bytes;
    if (!w || cw < 1 || ch < 1)
        return GLOS_E_ARGUMENT;
    p = pixels(cw, ch, &pitch);
    bytes = (int)((cw < w->w ? cw : w->w) * (H.fmt.bpp / 8));
    for (y = 0; y < ch && y < w->h; y++)
        memcpy(p + (unsigned)y * pitch, w->pix + (unsigned)y * w->pitch, (size_t)bytes);
    free(w->pix);
    w->pix = p, w->pitch = pitch, w->w = cw, w->h = ch;
    return 0;
}

int glos_set_text(glos_handle id, unsigned prop, const char *text)
{
    struct hwin *w = find(id);
    if (!w || prop != GLOS_PROP_WIN_TITLE)
        return GLOS_E_HANDLE;
    snprintf(w->title, sizeof w->title, "%s", text);
    return 0;
}

int glos_get(glos_handle id, unsigned prop, int *value)
{
    struct hwin *w = find(id);
    if (!w)
        return GLOS_E_HANDLE;
    if (prop == GLOS_PROP_WIN_X)
        *value = w->x;
    else if (prop == GLOS_PROP_WIN_Y)
        *value = w->y;
    else if (prop == GLOS_PROP_WIN_W)
        *value = w->w;
    else if (prop == GLOS_PROP_WIN_H)
        *value = w->h;
    else
        return GLOS_E_PROPERTY;
    return 0;
}

int glos_pixels(glos_handle id, struct glos_pixels *g)
{
    struct hwin *w = find(id);
    unsigned size = g->size;
    if (!w)
        return GLOS_E_HANDLE;
    memset(g, 0, sizeof *g);
    g->size = size;
    g->handle = id;
    g->pixels = (unsigned)(size_t)w->pix;
    g->pitch = w->pitch;
    g->w = w->w, g->h = w->h;
    g->format.bpp = H.fmt.bpp;
    g->format.rpos = H.fmt.rpos, g->format.rsize = H.fmt.rsize, g->format.gpos = H.fmt.gpos, g->format.gsize = H.fmt.gsize;
    g->format.bpos = H.fmt.bpos, g->format.bsize = H.fmt.bsize;
    return 0;
}

int glos_present(glos_handle id, int x, int y, int cw, int ch)
{
    (void)x, (void)y, (void)cw, (void)ch;
    return find(id) ? 0 : GLOS_E_HANDLE;
}
int glos_focus(glos_handle id) { if (!find(id)) return GLOS_E_HANDLE; set_focus(id); return 0; }

static void clip_set(const char *text, unsigned n)
{
    char *c = malloc(n + 1);
    if (!c)
        return;
    memcpy(c, text, n);
    free(H.clip);
    H.clip = c;
    H.clip_n = n;
}

int glos_clip_put(unsigned format, const void *data, unsigned length)
{
    if (format != GLOS_CLIP_TEXT)
        return GLOS_E_ARGUMENT;
    clip_set(data, length);
    return 0;
}

int glos_clip_get(unsigned format, void *buf, unsigned room, unsigned *length)
{
    if (format != GLOS_CLIP_TEXT || !H.clip)
        return GLOS_E_ARGUMENT;
    if (length)
        *length = H.clip_n;
    if (buf)
        memcpy(buf, H.clip, room < H.clip_n ? room : H.clip_n);
    return H.clip_n > room ? GLOS_E_ROOM : 0;
}

/* The settings: GLOS.CFG's lines in memory, from $GLOS_HOST_DRIVES/C/GLOS/GLOS.CFG if there is one, checked by the
 * schema as the kernel checks them (kernel/core/config.c; the file isn't written back). */
static struct cfg_line { char sec[48], key[32], val[200]; } cfg[128];
static int ncfg, cfg_read;

static int cfg_same(const char *a, const char *b)
{
    for (; *a && *b && ((*a | 0x20) == (*b | 0x20) || *a == *b); a++, b++)
        ;
    return !*a && !*b;
}

static void cfg_trim(char *t)
{
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '\t' || t[n - 1] == '\r' || t[n - 1] == '\n'))
        t[--n] = 0;
}

static void cfg_load(void)
{
    const char *root = getenv("GLOS_HOST_DRIVES");
    char path[512], line[300], sec[48] = "";
    FILE *f;
    if (cfg_read)
        return;
    cfg_read = 1;
    snprintf(path, sizeof path, "%s/C/GLOS/GLOS.CFG", root ? root : ".");
    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f) && ncfg < 128) {
        char *p = line, *eq;
        cfg_trim(line);
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '[') {
            char *e = strchr(p, ']');
            if (e) {
                *e = 0;
                snprintf(sec, sizeof sec, "%s", p + 1);
            }
        } else if (*p && *p != ';' && *p != '#' && (eq = strchr(p, '='))) {
            char *v = eq + 1;
            *eq = 0;
            cfg_trim(p);
            while (*v == ' ' || *v == '\t')
                v++;
            snprintf(cfg[ncfg].sec, sizeof cfg[ncfg].sec, "%s", sec);
            snprintf(cfg[ncfg].key, sizeof cfg[ncfg].key, "%s", p);
            snprintf(cfg[ncfg].val, sizeof cfg[ncfg].val, "%s", v);
            ncfg++;
        }
    }
    fclose(f);
}

static int config_get(const char *section, const char *key, char *out, unsigned room)
{
    const struct cfg_key *k = cfg_find(section, key);
    int i, found = 0;
    size_t n = 0;
    cfg_load();
    if (!k || !room)
        return -1;
    out[0] = 0;
    for (i = 0; i < ncfg; i++)
        if (cfg_same(cfg[i].sec, section) && cfg_same(cfg[i].key, key)) {
            if (found && k->type == CFG_ENV && n + 1 < room)
                out[n++] = '\n';
            else if (!(found && k->type == CFG_ENV))
                n = 0;
            n += (size_t)snprintf(out + n, room - n, "%s", cfg[i].val);
            if (n >= room)
                n = room - 1;
            found = 1;
        }
    if (!found)
        snprintf(out, room, "%s", k->def);
    return found;
}

static int config_unset(const char *section, const char *key)
{
    int i, j;
    cfg_load();
    if (!cfg_find(section, key))
        return -1;
    for (i = j = 0; i < ncfg; i++)
        if (!(cfg_same(cfg[i].sec, section) && cfg_same(cfg[i].key, key)))
            cfg[j++] = cfg[i];
    ncfg = j;
    return 0;
}

static int config_set(const char *section, const char *key, const char *value, char *why, unsigned room)
{
    const struct cfg_key *k = cfg_find(section, key);
    char one[200];
    const char *p;
    int lines = strchr(value, '\n') != NULL;
    if (!k || !cfg_section_ok(section)) {
        snprintf(why, room, "no such setting: [%s] %s", section, key);
        return -1;
    }
    for (p = value;;) {                         /* (env: lines, or parts by ";") */
        size_t n = 0;
        while (*p && (k->type != CFG_ENV || (*p != '\n' && (lines || *p != ';'))) && n < sizeof one - 1)
            one[n++] = *p++;
        one[n] = 0;
        if (cfg_check(k, one, why, (int)room) != 0)
            return -1;
        if (!*p++ || k->type != CFG_ENV)
            break;
    }
    config_unset(section, key);
    for (p = value; ncfg < 128;) {
        size_t n = 0;
        while (*p && (k->type != CFG_ENV || (*p != '\n' && (lines || *p != ';'))) && n < sizeof one - 1)
            one[n++] = *p++;
        one[n] = 0;
        snprintf(cfg[ncfg].sec, sizeof cfg[ncfg].sec, "%s", section);
        snprintf(cfg[ncfg].key, sizeof cfg[ncfg].key, "%s", key);
        snprintf(cfg[ncfg].val, sizeof cfg[ncfg].val, "%s", one);
        ncfg++;
        if (!*p++ || k->type != CFG_ENV)
            break;
    }
    return k->when;
}

static int config_sections(char *out, unsigned room)
{
    int i, j;
    size_t n = 0;
    cfg_load();
    for (i = 0; i < ncfg; i++) {
        for (j = 0; j < i && !cfg_same(cfg[j].sec, cfg[i].sec); j++)
            ;
        if (j == i && n < room)
            n += (size_t)snprintf(out + n, room - n, "%s\n", cfg[i].sec);
    }
    return (int)(n < room ? n : room);
}

static int config_keys(const char *section, char *out, unsigned room)
{
    int i;
    size_t n = 0;
    cfg_load();
    for (i = 0; i < ncfg && n < room; i++)
        if (cfg_same(cfg[i].sec, section))
            n += (size_t)snprintf(out + n, room - n, "%s = %s\n", cfg[i].key, cfg[i].val);
    return (int)(n < room ? n : room);
}

int glos_setting_get(const char *section, const char *key, char *value, unsigned room, int *set)
{
    int r = config_get(section, key, value, room);
    if (r < 0)
        return GLOS_E_SETTING;
    if (set)
        *set = r;
    return 0;
}

int glos_setting_set(const char *section, const char *key, const char *value, unsigned *when, char *why, unsigned room)
{
    char none[4];
    int r = value ? config_set(section, key, value, why ? why : none, why ? room : sizeof none) : config_unset(section, key);
    if (r < 0)
        return GLOS_E_SETTING;
    if (when)
        *when = (unsigned)r;
    return 0;
}

int glos_setting_sections(char *buf, unsigned room)
{
    int n = room ? config_sections(buf, room - 1) : 0;
    if (room)
        buf[n] = 0;
    return 0;
}

int glos_setting_keys(const char *section, char *buf, unsigned room)
{
    int n = room ? config_keys(section, buf, room - 1) : 0;
    if (room)
        buf[n] = 0;
    return 0;
}

/* Programs, jobs, watches, the shell (M9 step C): nothing runs on the host (the tests run built-in work only). */
glos_handle glos_job_run(const char *command, const char *dir, const char *env, unsigned how, unsigned flags,
                         const char *profile)
{
    (void)command, (void)dir, (void)env, (void)how, (void)flags, (void)profile;
    return 0;
}
__attribute__((weak)) int glos_job_read(glos_handle job, unsigned stream, void *buf, unsigned room) { (void)job, (void)stream, (void)buf, (void)room; return -1; }
int glos_job_keys(glos_handle job, const unsigned short *k, unsigned n) { (void)job, (void)k, (void)n; return -1; }
__attribute__((weak)) int glos_job_signal(glos_handle job, unsigned what) { (void)job, (void)what; return GLOS_E_HANDLE; }
__attribute__((weak)) int glos_job_status(glos_handle job, struct glos_job_status *out) { (void)job, (void)out; return GLOS_E_HANDLE; }
int glos_shell(int on) { (void)on; return 0; }

/* The machine's figures (SYS; MONITOR's tests): made up, from the script's clock, the same each run.
 * Two apps (the one under test and NOTEPAD) and a job; NOTEPAD can be ended. */
static int notepad_ended;
int glos_sys_stat(unsigned kind, void *out, unsigned room, unsigned *length, unsigned *count)
{
    unsigned s = H.clock / 1000;
    if (kind == GLOS_STAT_SYSTEM && room >= sizeof(struct glos_sys_system)) {
        struct glos_sys_system y;
        memset(&y, 0, sizeof y);
        y.size = sizeof y;
        y.uptime_ms = 3723000 + H.clock;
        {                                       /* 40% to 100% and back, a second at a time */
            static const unsigned pct[7] = { 40, 55, 70, 85, 100, 70, 55 };
            unsigned k;
            for (k = 0; k < s; k++)
                y.busy_ms += pct[k % 7] * 10;
            y.busy_ms += H.clock % 1000 * pct[s % 7] / 100;
        }
        y.idle_ms = H.clock - y.busy_ms;
        y.cpu_family = 6;
        y.threads = 9, y.apps = notepad_ended ? 1 : 2, y.windows = notepad_ended ? 1 : 2;
        y.net_rx = s * 12, y.net_tx = s * 9;
        y.ip[0] = 10, y.ip[1] = 0, y.ip[2] = 2, y.ip[3] = 15;
        y.screen_w = (unsigned)H.sw, y.screen_h = (unsigned)H.sh, y.screen_bpp = (unsigned)H.bpp;
        y.accel = 1;
        y.mixer_rate = 22050, y.streams = 1;
        y.agent = 1;
        strcpy(y.card, "Matrox G200"), strcpy(y.nic, "NE2000 PCI"), strcpy(y.hostname, "glos");
        memcpy(out, &y, sizeof y);
        if (length)
            *length = sizeof y;
        if (count)
            *count = 1;
        return 0;
    }
    if (kind == GLOS_STAT_MEMORY && room >= sizeof(struct glos_sys_memory)) {
        struct glos_sys_memory m;
        memset(&m, 0, sizeof m);
        m.size = sizeof m;
        m.total_kb = 65536, m.free_kb = 41216, m.heap_kb = 2048, m.vram_kb = 8192;
        memcpy(out, &m, sizeof m);
        if (length)
            *length = sizeof m;
        if (count)
            *count = 1;
        return 0;
    }
    return GLOS_E_ARGUMENT;
}

int glos_programs(struct glos_program *out, unsigned room, unsigned *count)
{
    static const struct glos_program all[3] = {
        { sizeof(struct glos_program), GLOS_PROG_APP, 1, 0, 0, 0, 1250, 1536, 1, "MONITOR.EXE" },
        { sizeof(struct glos_program), GLOS_PROG_APP, 2, 0, 0, 0, 31400, 2304, 1, "NOTEPAD.EXE" },
        { sizeof(struct glos_program), GLOS_PROG_JOB, 1, 1, 0, GLOS_JS_RUNNING, 0, 0, 0, "DIR /S" } };
    unsigned n = 0;
    int i;
    for (i = 0; i < 3; i++)
        if (!(i == 1 && notepad_ended) && (n + 1) * sizeof *all <= room)
            out[n++] = all[i];
    if (count)
        *count = n;
    return 0;
}

int glos_sys_log(unsigned pos, char *buf, unsigned room, unsigned *next, int follow)
{
    static const char log[] = "GLOS-DISPLAY on mode=640x480 bpp=16\nGLOS-APP start id=1 prog=MONITOR.EXE\n"
                              "GLOS-APP start id=2 prog=NOTEPAD.EXE\nGLOS-AGENT job id=1 cmd=\"DIR /S\"\n";
    unsigned n = pos < sizeof log - 1 ? sizeof log - 1 - pos : 0;
    (void)follow;
    if (n > room)
        n = room;
    memcpy(buf, log + pos, n);
    if (next)
        *next = pos + n;
    return (int)n;
}

int glos_sys_kill(unsigned what, unsigned id)
{
    if (what == GLOS_KILL_APP && id == 2 && !notepad_ended) {
        notepad_ended = 1;
        return 0;
    }
    return GLOS_E_ARGUMENT;
}
static unsigned watches;
glos_handle glos_fs_watch(const char *path) { (void)path; return 0x1000 + ++watches; }

/* The network: none on the host. */
int glos_net_resolve(const char *n, unsigned w, unsigned *a) { (void)n, (void)w, (void)a; return GLOS_E_NET_DOWN; }
glos_handle glos_net_connect(unsigned a, unsigned p, unsigned w, unsigned f) { (void)a, (void)p, (void)w, (void)f; return 0; }
int glos_net_send(glos_handle h, const void *b, unsigned n, unsigned w) { (void)h, (void)b, (void)n, (void)w; return -GLOS_E_HANDLE; }
int glos_net_receive(glos_handle h, void *b, unsigned n, unsigned w, unsigned *st) { (void)h, (void)b, (void)n, (void)w, (void)st; return -GLOS_E_HANDLE; }
int glos_net_close(glos_handle h, int reset) { (void)h, (void)reset; return GLOS_E_HANDLE; }
int glos_random(void *buf, unsigned n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    int ok = f && fread(buf, 1, n, f) == n;
    if (f)
        fclose(f);
    return ok ? 0 : -1;
}
int cp_active(void)
{
    const char *e = getenv("GLOSCP");
    return e && atoi(e) > 0 ? atoi(e) : 437;
}

int glos_time(struct glos_time *t)
{
    t->utc = 0, t->how = GLOS_TIME_RTC, t->offset = 0;
    return 0;
}

/* ---- the script */

static char *word(char **p, char *buf, size_t room)
{
    char *s = *p;
    size_t n = 0;
    while (*s == ' ' || *s == '\t')
        s++;
    if (!*s || *s == '\n')
        return NULL;
    if (*s == '"') {
        s++;
        while (*s && *s != '"' && n < room - 1)
            buf[n++] = *s++;
        if (*s == '"')
            s++;
    } else {
        while (*s && *s != ' ' && *s != '\t' && *s != '\n' && n < room - 1)
            buf[n++] = *s++;
    }
    buf[n] = 0;
    *p = s;
    return buf;
}

static void key_event(unsigned key, int down, unsigned mods)
{
    struct hev e;
    if (getenv("GLOS_SHIM_DEBUG"))
        printf("DEBUG key %x down=%d mods=%x to=%u \"%s\"\n", key, down, mods, H.focus,
               find(H.focus) ? find(H.focus)->title : "?");
    if (!H.focus)
        return;
    memset(&e, 0, sizeof e);
    e.type = HEV_KEY;
    e.window = H.focus;
    e.time = H.clock;
    e.key = key;
    e.mods = mods;
    e.down = (unsigned)down;
    e.ch = down ? keymap_char(key, (mods & GLOS_MOD_SHIFT) != 0, (mods & GLOS_MOD_CTRL) != 0,
                              (mods & GLOS_MOD_ALT) != 0, 0, 0) : 0;
    push(&e);
    H.clock += 20;
}

static const struct { const char *name; unsigned key; } names[] = {
    { "esc", 0x01 }, { "backspace", 0x0E }, { "tab", 0x0F }, { "enter", 0x1C }, { "space", 0x39 },
    { "f1", 0x3B }, { "f2", 0x3C }, { "f3", 0x3D }, { "f4", 0x3E }, { "f5", 0x3F }, { "f6", 0x40 }, { "f7", 0x41 },
    { "f8", 0x42 }, { "f9", 0x43 }, { "f10", 0x44 }, { "f11", 0x57 }, { "f12", 0x58 }, { "home", 0x147 },
    { "up", 0x148 }, { "pgup", 0x149 }, { "left", 0x14B }, { "right", 0x14D }, { "end", 0x14F }, { "down", 0x150 },
    { "pgdn", 0x151 }, { "ins", 0x152 }, { "del", 0x153 }, { "win", 0x15B },
};

/* A key's code by name ("a", "5", "f10", "del", "minus"...), or 0. */
unsigned shim_key_code(const char *n)
{
    static const char row[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0\\" "zxcvbnm,./";
    unsigned i;
    for (i = 0; i < sizeof names / sizeof *names; i++)
        if (!strcmp(n, names[i].name))
            return names[i].key;
    if (n[0] && !n[1])
        for (i = 0; i < sizeof row - 1; i++)
            if (row[i] && row[i] == n[0])
                return i;
    return 0;
}

static void key_spec(const char *spec)
{
    char part[32];
    unsigned mods = 0, key;
    const char *p = spec;
    for (;;) {
        const char *plus = strchr(p, '+');
        size_t n = plus ? (size_t)(plus - p) : strlen(p);
        if (n >= sizeof part)
            fail("key %s", spec);
        memcpy(part, p, n);
        part[n] = 0;
        if (!plus || !plus[1])
            break;
        if (!strcmp(part, "ctrl"))
            mods |= GLOS_MOD_CTRL;
        else if (!strcmp(part, "alt"))
            mods |= GLOS_MOD_ALT;
        else if (!strcmp(part, "shift"))
            mods |= GLOS_MOD_SHIFT;
        else
            fail("key %s: %s", spec, part);
        p = plus + 1;
    }
    if (!(key = shim_key_code(part)))
        fail("key %s: no key %s", spec, part);
    if (mods == GLOS_MOD_ALT && key == 0x3E) {  /* Alt+F4 is the window manager's: the window asked to close */
        struct hev e;
        if (!H.focus)
            return;
        memset(&e, 0, sizeof e);
        e.type = HEV_CLOSE;
        e.window = H.focus;
        e.time = H.clock;
        push(&e);
        H.clock += 50;
        return;
    }
    if (mods == GLOS_MOD_ALT && key == 0x0F) {  /* Alt+Tab too: the focus to the next window by its id (wm_next) */
        unsigned next = 0, first = 0;
        int i;
        for (i = 0; i < MAXWIN; i++) {
            const struct hwin *v = &H.win[i];
            if (!v->used || !v->shown || (v->flags & (HW_POPUP | HW_DESKTOP)))
                continue;
            if (!first || v->id < first)
                first = v->id;
            if (v->id > H.focus && (!next || v->id < next))
                next = v->id;
        }
        if (next || first)
            set_focus(next ? next : first);
        H.clock += 50;
        return;
    }
    if (mods & GLOS_MOD_CTRL)
        key_event(0x1D, 1, mods & GLOS_MOD_CTRL);
    if (mods & GLOS_MOD_ALT)
        key_event(0x38, 1, mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT));
    if (mods & GLOS_MOD_SHIFT)
        key_event(0x2A, 1, mods);
    key_event(key, 1, mods);
    key_event(key, 0, mods);
    if (mods & GLOS_MOD_SHIFT)
        key_event(0x2A, 0, mods & ~GLOS_MOD_SHIFT);
    if (mods & GLOS_MOD_ALT)
        key_event(0x38, 0, mods & GLOS_MOD_CTRL);
    if (mods & GLOS_MOD_CTRL)
        key_event(0x1D, 0, 0);
}

/* As the agent's glos type: the key for a character, 200h added with Shift. */
static int us_key(char c)
{
    static const char plain[] = "\0331234567890-=\b\tqwertyuiop[]\n\0asdfghjkl;'`\0\\zxcvbnm,./\0*\0 ";
    static const char shift[] = "\0!@#$%^&*()_+\0\0QWERTYUIOP{}\0\0ASDFGHJKL:\"~\0|ZXCVBNM<>?";
    unsigned i;
    if (!c)
        return 0;
    for (i = 0; i < sizeof plain - 1; i++)
        if (plain[i] == c && i != 0x36 && i != 0x37)
            return (int)i + 1;
    for (i = 0; i < sizeof shift - 1; i++)
        if (shift[i] == c)
            return ((int)i + 1) | 0x200;
    return 0;
}

static void type_text(const char *p)
{
    for (; *p && *p != '\n'; p++) {
        char c = *p;
        int k;
        if (c == '\\' && p[1]) {
            p++;
            c = *p == 'n' ? '\n' : *p == 't' ? '\t' : *p == 'e' ? 27 : *p == 'b' ? '\b' : *p;
        }
        if (!(k = us_key(c)))
            fail("type: no key for %02x", (unsigned char)c);
        if (k & 0x200)
            key_event(0x2A, 1, GLOS_MOD_SHIFT);
        key_event((unsigned)k & 0xFF, 1, k & 0x200 ? GLOS_MOD_SHIFT : 0);
        key_event((unsigned)k & 0xFF, 0, k & 0x200 ? GLOS_MOD_SHIFT : 0);
        if (k & 0x200)
            key_event(0x2A, 0, 0);
    }
}

/* The window at a screen point, topmost first. */
static struct hwin *at_point(int x, int y)
{
    struct hwin *best = NULL;
    int i;
    for (i = 0; i < MAXWIN; i++) {
        struct hwin *w = &H.win[i];
        int top = w->flags & HW_POPUP ? 1 << 20 : 0;
        if (!w->used || !w->shown || x < w->x || y < w->y || x >= w->x + w->w || y >= w->y + w->h)
            continue;
        if (!best || w->z + (unsigned)top > best->z + (unsigned)(best->flags & HW_POPUP ? 1 << 20 : 0))
            best = w;
    }
    return best;
}

static void mouse_event(struct hwin *w, unsigned key, int down)
{
    struct hev e;
    memset(&e, 0, sizeof e);
    e.type = HEV_MOUSE;
    e.window = w->id;
    e.time = H.clock;
    e.x = H.px - w->x, e.y = H.py - w->y;
    e.buttons = H.buttons;
    e.key = key;
    e.down = (unsigned)down;
    push(&e);
    H.clock += 30;
}

static void point(int x, int y)
{
    struct hwin *w;
    H.px = x, H.py = y;
    w = at_point(x, y);
    if (w && w->id == H.focus)
        mouse_event(w, 0, 0);
}

static void press(unsigned button)
{
    struct hwin *w = at_point(H.px, H.py);
    H.buttons |= button;
    if (w && w->id != H.focus)
        set_focus(w->id);
    else if (!w && H.focus && find(H.focus) && find(H.focus)->flags & HW_POPUP)
        ev_focus(H.focus, 0), H.focus = 0;
    if (w)
        mouse_event(w, button, 1);
}

static void release(unsigned button)
{
    struct hwin *w = find(H.focus);
    H.buttons &= ~button;
    if (w)
        mouse_event(w, button, 0);
}

static int masks[8][4], nmask;               /* a shot's areas that differ in Loop A (x, y, w, h): black */

static int masked(int x, int y)
{
    int i;
    for (i = 0; i < nmask; i++)
        if (x >= masks[i][0] && y >= masks[i][1] && x < masks[i][0] + masks[i][2] && y < masks[i][1] + masks[i][3])
            return 1;
    return 0;
}

static void png_out(struct hwin *w, const char *name)
{
    char path[512];
    unsigned row = 1 + (unsigned)w->w * 3, n;
    unsigned char *rgb = malloc((size_t)row * (size_t)w->h), *out = malloc(PNG_ROWS_MAX(PNG_BLOCK) + PNG_HEAD_MAX);
    struct gfx_surface s;
    struct png png;
    int x, y, at = 0;
    FILE *fp;
    gfx_init(&s, w->pix, w->w, w->h, w->pitch, &H.fmt);
    for (y = 0; y < w->h; y++) {
        rgb[(unsigned)y * row] = 0;
        for (x = 0; x < w->w; x++) {
            unsigned c = masked(x, y) ? 0 : gfx_rgb(&H.fmt, gfx_get(&s, x, y));
            unsigned char *p = rgb + (unsigned)y * row + 1 + (unsigned)x * 3;
            p[0] = (unsigned char)(c >> 16), p[1] = (unsigned char)(c >> 8), p[2] = (unsigned char)c;
        }
    }
    snprintf(path, sizeof path, "%s/%s-%d.png", H.out, name, H.bpp);
    if (!(fp = fopen(path, "wb")))
        fail("can't write %s", path);
    n = png_begin(&png, (unsigned)w->w, (unsigned)w->h, 24, 0, 0, out);
    fwrite(out, 1, n, fp);
    while (at < w->h) {
        int rows = (int)(PNG_BLOCK / row) < w->h - at ? (int)(PNG_BLOCK / row) : w->h - at;
        n = png_rows(&png, rgb + (unsigned)at * row, (unsigned)rows, out);
        fwrite(out, 1, n, fp);
        at += rows;
    }
    fclose(fp);
    printf("SHOT %s %dx%d\n", name, w->w, w->h);
    free(rgb);
    free(out);
}

/* The script's next commands, up to one that makes events or lets time pass: 1, or 0 at its end. */
static int step(void)
{
    char line[1024], cmd[32], a[256], b[64];
    for (;;) {
        long at = ftell(H.script);
        char *p = line;
        if (!fgets(line, sizeof line, H.script))
            break;
        H.line++;
        if (!strncmp(line, "shot", 4) && !H.settled) {     /* half a second first, as Loop A's shot has: the */
            H.settled = 1;                      /* app's timers run and it draws what they changed */
            fseek(H.script, at, SEEK_SET);
            H.line--;
            H.clock += 500;
            return 1;
        }
        if (!word(&p, cmd, sizeof cmd) || cmd[0] == '#')
            continue;
        if (!strcmp(cmd, "run") || !strcmp(cmd, "wait") || !strcmp(cmd, "gone") || !strcmp(cmd, "expect")) {
            continue;
        } else if (!strcmp(cmd, "key") || !strcmp(cmd, "quick")) {
            while (word(&p, a, sizeof a))
                key_spec(a);
            return 1;
        } else if (!strcmp(cmd, "type")) {
            while (*p == ' ')                   /* (as Loop A's driver and glos type take it: no leading spaces) */
                p++;
            type_text(p);
            return 1;
        } else if (!strcmp(cmd, "click") || !strcmp(cmd, "drag")) {
            struct hwin *w;
            int x, y, x1 = 0, y1 = 0, dbl = 0;
            unsigned button = 1;
            if (!word(&p, a, sizeof a) || !(w = by_title(a)))
                fail("%s: no window \"%s\"", cmd, a);
            x = atoi(word(&p, b, sizeof b) ? b : "0");
            y = atoi(word(&p, b, sizeof b) ? b : "0");
            if (!strcmp(cmd, "drag")) {
                x1 = atoi(word(&p, b, sizeof b) ? b : "0");
                y1 = atoi(word(&p, b, sizeof b) ? b : "0");
            }
            while (word(&p, b, sizeof b))
                if (!strcmp(b, "right"))
                    button = 2;
                else if (!strcmp(b, "double"))
                    dbl = 1;
            point(w->x + x, w->y + y);
            press(button);
            if (!strcmp(cmd, "drag"))
                point(w->x + x1, w->y + y1);
            release(button);
            if (dbl) {
                press(button);
                release(button);
            }
            return 1;
        } else if (!strcmp(cmd, "launch") || !strcmp(cmd, "syskey")) {
            struct hev e;
            memset(&e, 0, sizeof e);
            e.type = cmd[0] == 'l' ? HEV_LAUNCH : HEV_SYSKEY;
            e.window = 0;
            e.x = 0, e.y = H.sh - 20;
            e.time = H.clock;
            push(&e);
            H.clock += 50;
            return 1;
        } else if (!strcmp(cmd, "pause")) {
            H.clock += (unsigned)atoi(word(&p, b, sizeof b) ? b : "0");
            return 1;
        } else if (!strcmp(cmd, "clip")) {
            char *e;
            if (*p == ' ')
                p++;
            if ((e = strchr(p, '\n')) != NULL)
                *e = 0;
            clip_set(p, (unsigned)strlen(p));
        } else if (!strcmp(cmd, "file")) {
            char want[2048], got[2048], *q;
            const char *root = getenv("GLOS_HOST_DRIVES");
            int n = 0, k;
            FILE *f;
            if (!word(&p, a, sizeof a) || !root)
                fail("file: no path");
            while (*p == ' ')
                p++;
            for (q = p; *q && *q != '\n' && n < (int)sizeof want; q++) {
                char c = *q;
                if (c == '\\' && q[1]) {
                    q++;
                    c = *q == 'r' ? '\r' : *q == 'n' ? '\n' : *q == 't' ? '\t' : *q;
                    if (*q == 'x' && q[1] && q[2]) {
                        char hx[3] = { q[1], q[2], 0 };
                        c = (char)strtol(hx, NULL, 16);
                        q += 2;
                    }
                }
                want[n++] = c;
            }
            snprintf(got, sizeof got, "%s/%c%s", root, a[0], a + 2);
            for (q = got + strlen(root); *q; q++)
                if (*q == '\\')
                    *q = '/';
            if (!(f = fopen(got, "rb")))
                fail("file %s: not there", a);
            k = (int)fread(got, 1, sizeof got, f);
            fclose(f);
            if (k != n || memcmp(got, want, (size_t)n))
                fail("file %s: %d bytes, not the %d wanted", a, k, n);
            printf("FILE %s ok\n", a);
        } else if (!strcmp(cmd, "shot")) {
            struct hwin *w;
            char title[64];
            if (!word(&p, a, sizeof a) || !word(&p, title, sizeof title) || !(w = by_title(title)))
                fail("shot: no window \"%s\"", title);
            for (nmask = 0; nmask < 8 && word(&p, b, sizeof b); nmask++) {
                masks[nmask][0] = atoi(b);
                masks[nmask][1] = atoi(word(&p, b, sizeof b) ? b : "0");
                masks[nmask][2] = atoi(word(&p, b, sizeof b) ? b : "0");
                masks[nmask][3] = atoi(word(&p, b, sizeof b) ? b : "0");
            }
            png_out(w, a);
            H.settled = 0;
        } else {
            fail("no command %s", cmd);
        }
    }
    return 0;
}

/* An event of the script's, as API 2.0 gives it: its length. */
static int give(const struct hev *e, void *buf, unsigned room)
{
    union {
        struct glos_ev ev;
        struct glos_ev_key key;
        struct glos_ev_mouse mouse;
        struct glos_ev_focus focus;
        struct glos_ev_size size;
        struct glos_ev_shell shell;
    } u;
    unsigned n = sizeof u.ev;
    memset(&u, 0, sizeof u);
    u.ev.handle = e->window;
    u.ev.time = e->time;
    switch (e->type) {
    case HEV_KEY:
        u.ev.kind = GLOS_EV_KEY, n = sizeof u.key;
        u.key.key = e->key, u.key.ch = e->ch, u.key.mods = e->mods, u.key.down = e->down;
        break;
    case HEV_MOUSE:
        u.ev.kind = GLOS_EV_MOUSE, n = sizeof u.mouse;
        u.mouse.x = e->x, u.mouse.y = e->y, u.mouse.buttons = e->buttons, u.mouse.button = e->key;
        u.mouse.down = e->down, u.mouse.mods = e->mods, u.mouse.clicks = e->key && e->down;
        break;
    case HEV_FOCUS:
        u.ev.kind = GLOS_EV_FOCUS, n = sizeof u.focus;
        u.focus.got = e->down;
        break;
    case HEV_RESIZE:
        u.ev.kind = GLOS_EV_SIZE, n = sizeof u.size;
        break;
    case HEV_CLOSE:
        u.ev.kind = GLOS_EV_CLOSE;
        break;
    case HEV_LAUNCH:
    case HEV_SYSKEY:
        u.ev.kind = GLOS_EV_SHELL, n = sizeof u.shell;
        u.shell.what = e->type == HEV_LAUNCH ? GLOS_SHELL_LAUNCH : GLOS_SHELL_TASKS;
        u.shell.x = e->x, u.shell.y = e->y;
        break;
    }
    u.ev.size = n;
    if (n > room)
        fail("an event of %u bytes, room for %u", n, room);
    memcpy(buf, &u, n);
    return (int)n;
}

int glos_event(void *buf, unsigned room, unsigned wait_ms, unsigned areas)
{
    if (H.qh == H.qt && !H.done) {
        if (!step())
            H.done = 1;
    }
    if (H.qh != H.qt) {
        struct hev e = H.q[H.qh++ % 256];
        if (e.type == HEV_KEY) {                /* (a key goes to the window with the focus when it arrives, as the
                                                   window manager sends it: a menu opened by the key before) */
            if (!H.focus)
                return glos_event(buf, room, wait_ms, areas);
            e.window = H.focus;
        }
        if (e.time > H.clock)
            H.clock = e.time;
        return give(&e, buf, room);
    }
    if (H.done) {                               /* (the script done, nothing left: the run is over) */
        fflush(stdout);
        exit(0);
    }
    if (wait_ms != 0xFFFFFFFFu)
        H.clock += wait_ms;
    return 0;
}

#ifndef GLOSSHIM_NO_MAIN
int main(int argc, char **argv)
{
    char line[1024], *args[32], *p, buf[256];
    int n = 0;
    if (argc != 4) {
        fprintf(stderr, "usage: %s SCRIPT OUTDIR BPP\n", argv[0]);
        return 2;
    }
    if (!(H.script = fopen(argv[1], "r"))) {
        fprintf(stderr, "%s: can't read %s\n", argv[0], argv[1]);
        return 2;
    }
    H.out = argv[2];
    H.bpp = atoi(argv[3]);
    H.fmt = H.bpp == 8 ? gfx_fmt8 : H.bpp == 16 ? gfx_fmt565 : gfx_fmt888;
    H.sw = 640, H.sh = 480;
    H.next_id = 0;
    H.clock = 1000;
    H.px = 320, H.py = 240;
    while (fgets(line, sizeof line, H.script)) {    /* the first command: run ARGS (config lines before it) */
        H.line++;
        p = line;
        if (!word(&p, buf, sizeof buf) || buf[0] == '#')
            continue;
        if (!strcmp(buf, "config")) {           /* config SECTION KEY VALUE: a setting for this script (M10) */
            char sec[48], key[48], why[160];
            cfg_load();
            if (!word(&p, sec, sizeof sec) || !word(&p, key, sizeof key))
                fail("config SECTION KEY VALUE");
            if (!strncmp(sec, "program:", 8))   /* (glos config's spelling of [program NAME.EXT]) */
                sec[7] = ' ';
            while (*p == ' ')
                p++;
            p[strcspn(p, "\r\n")] = 0;
            if (config_set(sec, key, p, why, sizeof why) < 0)
                fail("config: %s", why);
            continue;
        }
        if (strcmp(buf, "run"))
            fail("the script begins with run");
        while (n < 31 && word(&p, buf, sizeof buf))
            args[n++] = strdup(buf);
        break;
    }
    args[n] = NULL;
    if (!n)
        fail("no program");
    {                                           /* (its profile's env, as GLOS gives it at the EXEC: M10's GLOSCP) */
        char sec[64], env[600], *e, *nl;
        snprintf(sec, sizeof sec, "program %s", args[0]);
        if (config_get(sec, "env", env, sizeof env) > 0)
            for (e = env; *e; e = nl ? nl + 1 : e + strlen(e)) {
                if ((nl = strchr(e, '\n')) != NULL)
                    *nl = 0;
                if (strchr(e, '='))
                    putenv(strdup(e));
                if (!nl)
                    break;
            }
    }
    return app_main(n, args);
}
#endif
