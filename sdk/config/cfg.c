/* GLOS.CFG's schema and its checks (include/glos/config.h; PRD §10.7, D53).
 *
 * Shared by the loader (Open Watcom, 16-bit), the kernel, the Control Panel
 * (DJGPP) and the host tests, so it uses nothing of a C library: its own
 * small string helpers only. */
#include "glos/config.h"
#include "glos/tz.h"

/* ---- the schema */

const char *const cfg_sections[] = { "display", "input", "sound", "net", "time", "vnc", "desktop", "shell", "open",
                                     "program",
                                     0 };

const struct cfg_key cfg_schema[] = {
    /* [display] (M6, M7, M8) */
    { "display", "mode", CFG_MODE, CFG_NOW, 0, 0, 0, "none", "&Screen",
      "The desktop's screen: WIDTHxHEIGHTxBITS, or none for text mode (no desktop)" },
    { "display", "accel", CFG_CHOICE, CFG_DESKTOP, 0, 0, "on|off", "on", "&Drawing engine",
      "A Matrox card's engine draws the desktop (off: the processor does)" },
    { "display", "gpu_watchdog", CFG_NUMBER, CFG_NOW, 50, 60000, "off", "500", "Engine &watchdog (ms)",
      "How long the desktop waits for an app's turn with the engine before ending the app" },
    /* [input] (M9) */
    { "input", "repeat_delay", CFG_CHOICE, CFG_NOW, 0, 0, "250|500|750|1000", "500", "Repeat &delay (ms)",
      "How long a key is held before it repeats" },
    { "input", "repeat_rate", CFG_NUMBER, CFG_NOW, 2, 30, 0, "10", "Repeat &rate (a second)",
      "How many times a second a held key repeats" },
    { "input", "pointer_speed", CFG_CHOICE, CFG_NOW, 0, 0, "1|2|3", "1", "Pointer &speed",
      "How far the pointer moves for the mouse's movement: 1, 2 or 3 times" },
    { "input", "double_click", CFG_NUMBER, CFG_APP, 200, 1000, 0, "500", "Dou&ble click (ms)",
      "The longest time between the presses of a double click" },
    /* [sound] (M8) */
    { "sound", "mixer", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "on", "&Mixer",
      "GLOS plays the Sound Blaster 16 and mixes the apps' sound (off: programs have the card)" },
    { "sound", "rate", CFG_CHOICE, CFG_NOW, 0, 0, "11025|22050|44100", "22050", "Mixing &rate (Hz)",
      "Samples a second the mixer plays" },
    { "sound", "blaster", CFG_BLASTER, CFG_RESTART, 0, 40, 0, "", "&Card (BLASTER)",
      "Where the Sound Blaster is, as BLASTER says it (A220 I5 D1 H5); empty: BLASTER's" },
    /* [net] (M5) */
    { "net", "hostname", CFG_TEXT, CFG_RESTART, 1, 15, "host", "glos", "&Host name",
      "This machine's name on the network (NAME.local)" },
    /* [time] (M10): network time, the zone */
    { "time", "sntp", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "on", "&Network time",
      "The clock is set from a time server (SNTP) when the network is up, and kept so" },
    { "time", "server", CFG_TEXT, CFG_NOW, 1, 63, "hostport", "pool.ntp.org", "Time &server",
      "The SNTP server's name or address, and :port if it isn't 123" },
    { "time", "zone", CFG_TEXT, CFG_NOW, 1, 47, "tz", "UTC0", "Time &zone",
      "Where the PC is, as a POSIX TZ string: GMT0BST,M3.5.0/1,M10.5.0 is the United Kingdom's" },
    { "time", "interval", CFG_NUMBER, CFG_NOW, 1, 168, 0, "24", "&Ask again (hours)",
      "How often the time server is asked again" },
    /* [vnc] (M10 step F) */
    { "vnc", "enable", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "off", "&Viewers through SSH",
      "A VNC viewer may see and use the desktop, through an SSH tunnel to port 5900 (ssh -L): whoever may log in" },
    /* [desktop] (M9) */
    { "desktop", "start", CFG_TEXT, CFG_DESKTOP, 0, 120, "line", "", "&Start with the desktop",
      "Programs started when the desktop starts, beside GLOS.EXE (FILES.EXE draws the desktop's icons)" },
    /* [shell] (M3, M4e) */
    { "shell", "comspec", CFG_PATH, CFG_RESTART, 0, 79, 0, "", "&COMMAND.COM",
      "The command interpreter for batch files and the console (empty: COMSPEC, or found)" },
    { "shell", "autoexec", CFG_PATH, CFG_RESTART, 0, 79, 0, "", "&AUTOEXEC.BAT",
      "As the shell: the batch file run first (empty: the boot drive's \\AUTOEXEC.BAT)" },
    { "shell", "console", CFG_TEXT, CFG_RESTART, 0, 127, "line", "", "C&onsole",
      "As the shell: run (through COMSPEC /C) instead of the prompt" },
    { "shell", "envsize", CFG_NUMBER, CFG_RESTART, 256, 32768, 0, "1024", "&Environment (bytes)",
      "As the shell: the master environment's size" },
    { "shell", "options", CFG_TEXT, CFG_RESTART, 0, 79, "line", "", "&Flags",
      "GLOS.EXE's flags: /DPMITRACE, /NOVME, /GDB, /DIRECT" },
    /* [open] (M9): a kind of file and the program that opens it */
    { "open", "*", CFG_PATH, CFG_NOW, 0, 79, 0, "", "Opened by",
      "The program the file manager opens a kind of file with (.TXT = NOTEPAD.EXE)" },
    /* [program NAME.EXT] (M4e; M9's from title on) */
    { "program", "title", CFG_TEXT, CFG_NOW, 0, 40, "line", "", "&Title",
      "Its name in the launcher and on the desktop (empty: the file's)" },
    { "program", "args", CFG_TEXT, CFG_PROGRAM, 0, 120, "line", "", "&Arguments",
      "What it is started with" },
    { "program", "dir", CFG_PATH, CFG_PROGRAM, 0, 79, 0, "", "&Folder",
      "Where it starts (empty: its own folder)" },
    { "program", "icon", CFG_CHOICE, CFG_NOW, 0, 0, "program|game|tool|text|picture|sound|network", "program",
      "&Icon", "Its picture in the launcher and on the desktop" },
    { "program", "menu", CFG_CHOICE, CFG_NOW, 0, 0, "0|1", "0", "In the &launcher", "It is in the launcher's menu" },
    { "program", "desktop", CFG_CHOICE, CFG_NOW, 0, 0, "0|1", "0", "On the des&ktop", "It has an icon on the desktop" },
    { "program", "screen", CFG_CHOICE, CFG_PROGRAM, 0, 0, "window|full", "window", "&Screen",
      "From the terminal: in the window until it wants the screen, or always full-screen" },
    { "program", "direct", CFG_CHOICE, CFG_PROGRAM, 0, 0, "0|1", "0", "&Direct mode",
      "It runs at IOPL 3 with the PC's ports (PRD D20)" },
    { "program", "memory", CFG_NUMBER, CFG_PROGRAM, 0, 1048576, 0, "0", "&Memory (KB)",
      "The most DPMI memory it is given (0: no limit)" },
    { "program", "env", CFG_ENV, CFG_PROGRAM, 0, 190, 0, "", "&Environment",
      "NAME=VALUE lines added to its environment" },
};

const int cfg_schema_n = (int)(sizeof cfg_schema / sizeof cfg_schema[0]);

/* ---- helpers (no C library) */

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

static int same(const char *a, const char *b)
{
    for (; *a && lower((unsigned char)*a) == lower((unsigned char)*b); a++, b++)
        ;
    return !*a && !*b;
}

static int same_n(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (lower((unsigned char)a[i]) != lower((unsigned char)b[i]))
            return 0;
    return 1;
}

static int len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

/* why = a, b, c (as much as fits) */
static int say(char *why, int room, const char *a, const char *b, const char *c)
{
    int n = 0;
    const char *parts[3];
    int i;
    parts[0] = a, parts[1] = b, parts[2] = c;
    if (!why || room <= 0)
        return -1;
    for (i = 0; i < 3; i++) {
        const char *p = parts[i];
        while (p && *p && n < room - 1)
            why[n++] = *p++;
    }
    why[n] = 0;
    return -1;
}

/* A whole number (digits only), into *v: 0, or -1. */
static int number(const char *s, long *v)
{
    long n = 0;
    if (!*s)
        return -1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9' || n > 100000000L)
            return -1;
        n = n * 10 + (*s - '0');
    }
    *v = n;
    return 0;
}

/* value is one of the choices "a|b|c" */
static int one_of(const char *choices, const char *value)
{
    const char *p = choices;
    int vn = len(value);
    while (p && *p) {
        const char *e = p;
        while (*e && *e != '|')
            e++;
        if (e - p == vn && same_n(p, value, vn))
            return 1;
        p = *e ? e + 1 : e;
    }
    return 0;
}

void cfg_kind(const char *section, char *out, int room)
{
    int i = 0;
    if (same_n(section, "program ", 8) || same(section, "program"))
        section = "program";
    while (section[i] && i < room - 1) {
        out[i] = (char)lower((unsigned char)section[i]);
        i++;
    }
    out[i] = 0;
}

int cfg_section_ok(const char *section)
{
    char kind[16];
    int i;
    if (same_n(section, "program ", 8)) {       /* a program's: NAME.EXT */
        const char *n = section + 8;
        int dot = 0, k = 0;
        for (; *n; n++, k++) {
            if (*n == '.')
                dot++;
            else if (!((*n >= 'A' && *n <= 'Z') || (*n >= 'a' && *n <= 'z') || (*n >= '0' && *n <= '9') || *n == '_'
                       || *n == '-' || *n == '~' || *n == '!' || *n == '$'))
                return 0;
        }
        return k > 0 && k <= 12 && dot <= 1;
    }
    cfg_kind(section, kind, sizeof kind);
    for (i = 0; cfg_sections[i]; i++)
        if (same(kind, cfg_sections[i]) && !same(kind, "program"))
            return 1;
    return 0;
}

const struct cfg_key *cfg_find(const char *section, const char *key)
{
    char kind[16];
    int i;
    cfg_kind(section, kind, sizeof kind);
    for (i = 0; i < cfg_schema_n; i++)
        if (same(kind, cfg_schema[i].section)) {
            if (cfg_schema[i].key[0] == '*' && !cfg_schema[i].key[1]) {
                if (key[0] == '.' && len(key) >= 2 && len(key) <= 4)    /* an [open] key: .EXT */
                    return &cfg_schema[i];
            } else if (same(cfg_schema[i].key, key)) {
                return &cfg_schema[i];
            }
        }
    return 0;
}

static int text_class(const char *cls, const char *v)
{
    for (; *v; v++) {
        unsigned char c = (unsigned char)*v;
        if (same(cls, "host") || same(cls, "hostport")) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-'
                  || (same(cls, "hostport") && (c == '.' || c == ':'))))
                return 0;
        } else if (c < 0x20 || c == 0x7F) {     /* "line": anything printable */
            return 0;
        }
    }
    return 1;
}

int cfg_check(const struct cfg_key *k, const char *v, char *why, int room)
{
    long n;
    int vl = len(v);
    switch (k->type) {
    case CFG_CHOICE:
        if (!one_of(k->choices, v))
            return say(why, room, "not one of ", k->choices, "");
        return 0;
    case CFG_NUMBER:
        if (k->choices && one_of(k->choices, v))
            return 0;
        if (number(v, &n) != 0 || n < k->min || n > k->max) {
            char range[48];
            long a = k->min, b = k->max;
            int i = 0, j;
            char t[16];
            for (j = 0; a || !j; a /= 10)       /* (the range written out by hand: no printf here) */
                t[j++] = (char)('0' + a % 10);
            while (j)
                range[i++] = t[--j];
            range[i++] = '-';
            for (j = 0; b || !j; b /= 10)
                t[j++] = (char)('0' + b % 10);
            while (j)
                range[i++] = t[--j];
            range[i] = 0;
            return say(why, room, "a number from ", range, k->choices ? " (or off)" : "");
        }
        return 0;
    case CFG_TEXT:
        if (vl > k->max)
            return say(why, room, "too long", "", "");
        if (vl < k->min)
            return say(why, room, "empty", "", "");
        if (same(k->choices ? k->choices : "", "tz")) {
            struct tz z;
            if (tz_parse(v, &z) != 0)
                return say(why, room, "not a POSIX time zone (as GMT0BST,M3.5.0/1,M10.5.0 or UTC0)", "", "");
            return 0;
        }
        if (!text_class(k->choices ? k->choices : "line", v))
            return say(why, room, same(k->choices ? k->choices : "", "host") ? "letters, digits and hyphens only"
                              : same(k->choices ? k->choices : "", "hostport") ? "a name or an address, and :port"
                              : "a character that isn't printable", "", "");
        if ((same(k->choices ? k->choices : "", "host") || same(k->choices ? k->choices : "", "hostport")) && v[0] == '-')
            return say(why, room, "can't begin with a hyphen", "", "");
        if (same(k->choices ? k->choices : "", "hostport")) {
            const char *c = v;
            long port = 0;
            while (*c && *c != ':')
                c++;
            if (*c == ':') {                    /* (a port: 1-65535, nothing after) */
                for (c++; *c >= '0' && *c <= '9' && port <= 65535; c++)
                    port = port * 10 + (*c - '0');
                if (*c || port < 1 || port > 65535)
                    return say(why, room, "a port is 1-65535, after the one colon", "", "");
            }
        }
        return 0;
    case CFG_MODE: {
        long w = 0, h = 0, b = 0;
        const char *p = v;
        if (same(v, "none"))
            return 0;
        while (*p >= '0' && *p <= '9')
            w = w * 10 + (*p++ - '0');
        if (*p == 'x' || *p == 'X') {
            p++;
            while (*p >= '0' && *p <= '9')
                h = h * 10 + (*p++ - '0');
            if (*p == 'x' || *p == 'X') {
                p++;
                while (*p >= '0' && *p <= '9')
                    b = b * 10 + (*p++ - '0');
            }
        }
        if (*p || w < 320 || w > 1600 || h < 200 || h > 1200 || (b != 8 && b != 16 && b != 32))
            return say(why, room, "none, or WIDTHxHEIGHTxBITS (320-1600, 200-1200, 8, 16 or 32)", "", "");
        return 0;
    }
    case CFG_PATH: {
        const char *p;
        if (vl > k->max)
            return say(why, room, "too long", "", "");
        for (p = v; *p; p++)
            if ((unsigned char)*p < 0x20 || *p == '"' || *p == '<' || *p == '>' || *p == '|' || *p == '*' || *p == '?')
                return say(why, room, "not a DOS path", "", "");
        return 0;
    }
    case CFG_BLASTER: {
        const char *p = v;
        int seen = 0;
        if (vl > k->max)
            return say(why, room, "too long", "", "");
        while (*p) {
            char c = (char)lower((unsigned char)*p);
            if (*p == ' ' || *p == '\t') {
                p++;
                continue;
            }
            if (c < 'a' || c > 'z' || !p[1] || p[1] == ' ')
                return say(why, room, "A220 I5 D1 H5: letters each with their value", "", "");
            p++;
            while (*p && *p != ' ' && *p != '\t') {
                if (!((*p >= '0' && *p <= '9') || (lower((unsigned char)*p) >= 'a' && lower((unsigned char)*p) <= 'f')))
                    return say(why, room, "A220 I5 D1 H5: letters each with their value", "", "");
                p++;
            }
            if (c == 'a')
                seen |= 1;
        }
        if (vl && !(seen & 1))
            return say(why, room, "no port (A220)", "", "");
        return 0;
    }
    case CFG_ENV: {
        const char *p;
        if (vl > k->max)
            return say(why, room, "too long", "", "");
        for (p = v; *p && *p != '='; p++)
            if (*p == ' ' || (unsigned char)*p < 0x20)
                return say(why, room, "NAME=VALUE", "", "");
        if (!*p || p == v)
            return say(why, room, "NAME=VALUE", "", "");
        return 0;
    }
    }
    return say(why, room, "unknown kind of setting", "", "");
}
