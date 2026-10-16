/* cfg_test.c - GLOS.CFG's schema and the kernel's keeper of the file on the
 * host (make host-test; M9 step B). The kernel's config.c is built here with
 * a DOS server that works on a host file at once (kstub/io.h for its locks).
 *
 *   - each kind of setting's checks: right values taken, wrong ones refused
 *     with a reason;
 *   - a file with comments, blank lines, unknown keys and sections, read,
 *     changed (a key in place, a new key in a section, a new section, an
 *     env key's lines, unsets) and written: every line not changed is as it
 *     was, in its place;
 *   - a program's profile read from its section, by NAME.EXT and by NAME;
 *   - what is applied at once, applied. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/core/config.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("cfg_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

/* ---- the kernel's, for config.c */
struct thread *current;
void thread_block(struct waitq *q) { (void)q; }
int thread_wake(struct waitq *q) { (void)q; return 0; }
int thread_wait(struct waitq *q, u32 tick) { (void)q, (void)tick; return 0; }
struct thread *thread_create(const char *n, int p, void (*fn)(void *), void *a) { (void)n, (void)p, (void)fn, (void)a; return 0; }
u32 timer_ticks(void) { return 0; }
void *kmalloc(size_t n) { return malloc(n); }
void kfree(void *p) { free(p); }
void kprintf(const char *fmt, ...) { (void)fmt; }
int ksnprintf(char *b, size_t n, const char *fmt, ...)
{
    va_list ap;
    int r;
    va_start(ap, fmt);
    r = vsnprintf(b, n, fmt, ap);
    va_end(ap);
    return r < (int)n ? r : (int)n - 1;
}
static char applied[256];
int display_ask(int what, u32 w, u32 h, u32 b) { snprintf(applied, sizeof applied, "display %d %ux%ux%u", what, w, h, b); return 0; }
void gpu_watchdog(u32 ms) { snprintf(applied, sizeof applied, "watchdog %u", ms); }
void mixer_enable(int on) { snprintf(applied, sizeof applied, "mixer %d", on); }
void mixer_set_rate(u32 r) { snprintf(applied, sizeof applied, "rate %u", r); }
void vkbc_typematic(u32 d, u32 r) { snprintf(applied, sizeof applied, "typematic %u %u", d, r); }
void deskin_pointer_speed(int s) { snprintf(applied, sizeof applied, "speed %d", s); }

/* The DOS server, at once, on host files: open, read, create, write, close (dos.c's calls as config.c makes them). */
static FILE *handle[8];
static u8 xfer[32768];
u32 dos_xfer_size(void) { return sizeof xfer; }
void dos_hold(void) {}
int dos_post(struct dos_req *q)
{
    u32 i;
    if (q->in && q->in_len)
        memcpy(xfer + q->in_at, q->in, q->in_len);
    for (i = 0; i < q->n; i++) {
        struct dos_regs *r = &q->r[i];
        int fail = 0;
        switch (r->ax >> 8) {
        case 0x3D:
        case 0x3C: {
            FILE *f = fopen((char *)xfer, (r->ax >> 8) == 0x3D ? "rb" : "wb");
            int h;
            for (h = 5; h < 8 && handle[h]; h++) ;
            if (!f || h == 8) {
                fail = 1;
                r->ax = 2;
                if (f)
                    fclose(f);
            } else {
                handle[h] = f;
                r->ax = (u16)h;
            }
            break;
        }
        case 0x3F: r->ax = (u16)fread(xfer, 1, r->cx, handle[r->bx]); break;
        case 0x40: r->ax = (u16)fwrite(xfer, 1, r->cx, handle[r->bx]); break;
        case 0x3E: fclose(handle[r->bx]); handle[r->bx] = 0; r->ax = 0; break;
        default: fail = 1, r->ax = 1;
        }
        q->ran = i + 1;
        if (fail) {
            q->error = r->ax;
            break;
        }
    }
    if (q->out && q->out_len)
        memcpy(q->out, xfer + q->out_at, q->out_len);
    q->state = DR_DONE;
    return 0;
}

/* ---- the tests */

static const struct { const char *sec, *key, *val; int ok; } values[] = {
    { "display", "mode", "800x600x16", 1 }, { "display", "mode", "none", 1 }, { "display", "mode", "800x600x24", 0 },
    { "display", "mode", "100x600x16", 0 }, { "display", "mode", "800x600", 0 }, { "display", "accel", "OFF", 1 },
    { "display", "accel", "maybe", 0 }, { "display", "gpu_watchdog", "500", 1 }, { "display", "gpu_watchdog", "off", 1 },
    { "display", "gpu_watchdog", "49", 0 }, { "display", "gpu_watchdog", "5x", 0 }, { "sound", "rate", "44100", 1 },
    { "sound", "rate", "48000", 0 }, { "sound", "blaster", "A220 I5 D1 H5 T6", 1 }, { "sound", "blaster", "I5 D1", 0 },
    { "sound", "blaster", "A220 I", 0 }, { "net", "hostname", "retro-486", 1 }, { "net", "hostname", "-x", 0 },
    { "net", "hostname", "a.b", 0 }, { "net", "hostname", "abcdefghijklmnop", 0 }, { "input", "repeat_rate", "30", 1 },
    { "input", "repeat_rate", "31", 0 }, { "input", "pointer_speed", "2", 1 }, { "shell", "comspec", "C:\\COMMAND.COM", 1 },
    { "shell", "comspec", "C:\\A|B", 0 }, { "program FWHEEL.EXE", "env", "DOOMWADDIR=D:\\DOOM", 1 },
    { "program FWHEEL.EXE", "env", "NOEQUALS", 0 }, { "program FWHEEL.EXE", "memory", "16384", 1 },
    { "program FWHEEL.EXE", "screen", "full", 1 }, { "open", ".TXT", "NOTEPAD.EXE", 1 },
};

static const char *const before[] = {
    "; GLOS.CFG for the test", "", "[display]", "mode = 640x480x8", "  ; the engine", "accel = on", "",
    "[unknown]", "colour = blue", "", "[program FWHEEL.EXE]", "env = A=1", "direct = 0", "env = B=2",
    "[sound]", "mixer = on", "something = else", "",
};

static char *file_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *t = calloc(1, 65536);
    if (f) {
        size_t n = fread(t, 1, 65535, f);
        t[n] = 0;
        fclose(f);
    }
    return t;
}

int main(void)
{
    const char *path = "build/host/cfg_test.cfg";
    char why[160], v[256];
    struct bi_profile p;
    unsigned i;
    int r;
    FILE *f;

    for (i = 0; i < sizeof values / sizeof *values; i++) {
        const struct cfg_key *k = cfg_find(values[i].sec, values[i].key);
        why[0] = 0;
        r = k ? cfg_check(k, values[i].val, why, sizeof why) : -9;
        CHECK((r == 0) == values[i].ok && (r == 0 || why[0]), "[%s] %s = %s: %d (%s)", values[i].sec, values[i].key,
              values[i].val, r, why);
    }
    CHECK(!cfg_find("display", "colour") && !cfg_find("program X.EXE", "mode") && cfg_find("program X.EXE", "title"),
          "cfg_find's sections");
    CHECK(cfg_section_ok("program FWHEEL.EXE") && !cfg_section_ok("program A B") && !cfg_section_ok("nonsense"),
          "cfg_section_ok");

    f = fopen(path, "wb");
    for (i = 0; i < sizeof before / sizeof *before; i++)
        fprintf(f, "%s\r\n", before[i]);
    fclose(f);
    strcpy(c.path, path);
    load();
    CHECK(c.loaded && c.n == sizeof before / sizeof *before, "loaded %u lines", c.n);
    CHECK(kcfg_get("display", "mode", v, sizeof v) == 1 && !strcmp(v, "640x480x8"), "get mode: %s", v);
    CHECK(kcfg_get("sound", "rate", v, sizeof v) == 0 && !strcmp(v, "22050"), "get rate (the default): %s", v);
    CHECK(kcfg_get("program FWHEEL.EXE", "env", v, sizeof v) == 1 && !strcmp(v, "A=1\nB=2"), "get env: %s", v);

    r = kcfg_set("display", "mode", "800x600x16", why, sizeof why);
    CHECK(r == CFG_NOW && !strcmp(applied, "display 1 800x600x16"), "set mode: %d %s", r, applied);
    r = kcfg_set("display", "gpu_watchdog", "off", why, sizeof why);
    CHECK(r == CFG_NOW && !strcmp(applied, "watchdog 0"), "set watchdog: %d %s", r, applied);
    CHECK(kcfg_set("display", "mode", "800x600x24", why, sizeof why) < 0 && why[0], "a wrong mode refused");
    CHECK(kcfg_set("display", "colour", "red", why, sizeof why) < 0, "an unknown key refused");
    r = kcfg_set("input", "repeat_rate", "20", why, sizeof why);
    CHECK(r == CFG_NOW && !strcmp(applied, "typematic 500 20"), "set repeat rate: %d %s", r, applied);
    CHECK(kcfg_set("program FWHEEL.EXE", "env", "C=3;D=4", why, sizeof why) == CFG_PROGRAM, "set env");
    CHECK(kcfg_set("program DOOM.EXE", "memory", "8192", why, sizeof why) == CFG_PROGRAM, "a new program's section");
    CHECK(kcfg_set("sound", "mixer", 0, why, sizeof why) == CFG_NOW && !strcmp(applied, "mixer 1"), "unset mixer");
    CHECK(save() == 0, "written");
    {
        static const char *const after[] = {
            "; GLOS.CFG for the test", "", "[display]", "mode = 800x600x16", "  ; the engine", "accel = on",
            "gpu_watchdog = off", "", "[unknown]", "colour = blue", "", "[program FWHEEL.EXE]", "direct = 0",
            "env = C=3", "env = D=4", "[sound]", "something = else", "", "[input]", "repeat_rate = 20", "",
            "[program DOOM.EXE]", "memory = 8192",
        };
        char *t = file_text(path), want[4096];
        size_t n = 0;
        for (i = 0; i < sizeof after / sizeof *after; i++)
            n += (size_t)sprintf(want + n, "%s\r\n", after[i]);
        CHECK(!strcmp(t, want), "the file written:\n%s\nwanted:\n%s", t, want);
        free(t);
    }
    CHECK(kcfg_profile("FWHEEL.EXE", &p) == 0 && !strcmp(p.name, "FWHEEL.EXE") && !strcmp(p.env, "C=3")
          && !strcmp(p.env + 4, "D=4") && !(p.flags & BI_PROF_DIRECT), "FWHEEL.EXE's profile");
    CHECK(kcfg_profile("DOOM", &p) == 0 && p.memory_kb == 8192, "DOOM's profile by its name alone");
    CHECK(kcfg_profile("OTHER.EXE", &p) == -1, "no profile");
    printf("cfg_test: %d failures\n", fails);
    return fails != 0;
}
