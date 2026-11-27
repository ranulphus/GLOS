/* GLOS.CFG's schema and its checks (include/glos/config.h; PRD §10.7, D53).
 *
 * Shared by the loader (Open Watcom, 16-bit), the kernel, the Control Panel
 * (DJGPP) and the host tests, so it uses nothing of a C library: its own
 * small string helpers only. */
#include "glos/config.h"
#include "glos/tz.h"

/* ---- the schema */

const char *const cfg_sections[] = { "display", "input", "joystick", "usb", "sound", "net", "devices", "time", "region", "cache",
                                     "memory", "vnc", "desktop", "saver", "services", "shell", "open", "program",
                                     "boot", 0 };

const struct cfg_key cfg_schema[] = {
    /* [display] (M6, M7, M8) */
    { "display", "mode", CFG_MODE, CFG_NOW, 0, 0, 0, "none", "&Screen",
      "The desktop's screen: WIDTHxHEIGHTxBITS, or none for text mode (no desktop)" },
    { "display", "accel", CFG_CHOICE, CFG_DESKTOP, 0, 0, "on|off", "on", "&Drawing engine",
      "A Matrox card's engine draws the desktop (off: the processor does)" },
    { "display", "clocks", CFG_CHOICE, CFG_RESTART, 0, 0, "auto|bios", "auto", "Card's cloc&ks",
      "A Matrox G400 runs on the clocks Matrox's own driver uses, where its BIOS has them (auto: half as fast "
      "again), or on those the BIOS leaves it on (bios)" },
    { "display", "write_combining", CFG_CHOICE, CFG_RESTART, 0, 0, "auto|on|off", "auto", "Fast &framebuffer writes",
      "Write-combining for the video card's linear framebuffer, which software renderers write several times "
      "faster with: auto for the cards it has been checked on, on for any card, off" },
    { "display", "card", CFG_TEXT, CFG_RESTART, 4, 7, "pciaddr", "auto", "Display &card",
      "Which display card GLOS starts and shows its screen on: auto, the one the PC's BIOS started, or another "
      "card's PCI address (as glos pci lists it, 01:00.0): GLOS then places its ranges and runs its BIOS" },
    { "display", "refresh", CFG_CHOICE, CFG_NOW, 0, 0, "auto|60|70|75|85", "auto", "&Refresh rate (Hz)",
      "The screen's refresh rate, where the card's BIOS can set one (VBE 3.0) and the monitor says it takes it "
      "(its EDID): auto keeps the BIOS's own" },
    { "display", "gpu_watchdog", CFG_NUMBER, CFG_NOW, 50, 60000, "off", "500", "Engine &watchdog (ms)",
      "How long the desktop waits for an app's turn with the engine before ending the app" },
    { "display", "font_smoothing", CFG_CHOICE, CFG_PROGRAM, 0, 0, "on|off", "off", "Smooth &text",
      "Programs draw their text smoothed at 16 and 32 bits (the desktop's own stays crisp)" },
    /* [input] (M9) */
    { "input", "repeat_delay", CFG_CHOICE, CFG_NOW, 0, 0, "250|500|750|1000", "500", "Repeat &delay (ms)",
      "How long a key is held before it repeats" },
    { "input", "repeat_rate", CFG_NUMBER, CFG_NOW, 2, 30, 0, "10", "Repeat &rate (a second)",
      "How many times a second a held key repeats" },
    { "input", "pointer_speed", CFG_CHOICE, CFG_NOW, 0, 0, "1|2|3", "1", "Pointer &speed",
      "How far the pointer moves for the mouse's movement: 1, 2 or 3 times" },
    { "input", "double_click", CFG_NUMBER, CFG_APP, 200, 1000, 0, "500", "Dou&ble click (ms)",
      "The longest time between the presses of a double click" },
    /* [joystick]: the virtual game port (docs/gamepad.md) */
    { "joystick", "port", CFG_CHOICE, CFG_NOW, 0, 0, "auto|virtual|off", "auto", "&Game port",
      "auto: GLOS answers port 201h while a joystick source (a USB pad, glos joy) is on, and the PC's own game port "
      "is used otherwise; virtual: always GLOS's; off: always the PC's" },
    { "joystick", "range", CFG_NUMBER, CFG_NOW, 100, 10000, 0, "1124", "Axis &range (microseconds)",
      "How long an axis's one-shot runs at its far end (24 at the near end): 1124 as a 100 kΩ stick" },
    { "joystick", "map", CFG_TEXT, CFG_NOW, 0, 47, "line", "lx ly rx ry 1 2 3 4", "&Mapping",
      "What of a pad drives the port: for A's X and Y and B's X and Y one of lx ly rx ry (-ly: the other way; none), "
      "then for the buttons A1 A2 B1 B2 the pad's button's number (1 to 16; none)" },
    /* [usb]: GLOS's own USB stack (docs/gamepad.md) */
    { "usb", "enabled", CFG_CHOICE, CFG_RESTART, 0, 0, "on|off", "on", "&USB",
      "GLOS drives the PC's USB controllers itself (UHCI and OHCI) and finds what is plugged in; off: they are left to "
      "the BIOS and DOS" },
    { "usb", "ehci", CFG_CHOICE, CFG_RESTART, 0, 0, "off|on", "off", "USB &2.0 speed for disks",
      "A USB 2.0 disk at high speed (EHCI); everything else stays at USB 1.1's speed. Off: every device at USB 1.1's "
      "speed (not yet tried on a real PC)" },
    /* [sound] (M8) */
    { "sound", "mixer", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "on", "&Mixer",
      "GLOS plays the Sound Blaster 16 and mixes the apps' sound (off: programs have the card)" },
    { "sound", "volume", CFG_NUMBER, CFG_NOW, 0, 100, 0, "100", "&Volume",
      "The master volume of what GLOS's mixer plays, in percent" },
    { "sound", "mute", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "off", "M&ute",
      "GLOS's mixer plays nothing" },
    { "sound", "rate", CFG_CHOICE, CFG_NOW, 0, 0, "11025|22050|44100", "22050", "Mixing &rate (Hz)",
      "Samples a second the mixer plays" },
    { "sound", "midi", CFG_CHOICE, CFG_NOW, 0, 0, "fm|port", "fm", "MI&DI out",
      "Where the Player and MIDI Studio play MIDI: the card's FM synthesiser, or its MIDI port (an MPU-401: a "
      "synthesiser plugged into it); the other where that one is not there" },
    { "sound", "blaster", CFG_BLASTER, CFG_RESTART, 0, 40, 0, "", "&Card (BLASTER)",
      "Where the Sound Blaster is, as BLASTER says it (A220 I5 D1 H5); empty: BLASTER's" },
    { "sound", "virtual", CFG_CHOICE, CFG_RESTART, 0, 0, "on|off", "on", "Virtual &Sound Blaster",
      "DOS programs find a Sound Blaster 16 that plays through GLOS's mixer, on any card it drives (off: they have "
      "the PC's own card)" },
    { "sound", "crystal", CFG_TEXT, CFG_RESTART, 1, 95, "line", "auto", "Cr&ystal chip",
      "A Crystal CS423x chip GLOS sets up when there is no SB16 (docs/crystal-audio.md): auto, off, or its resources "
      "(wss=534 sb=220 fm=388 irq=5 dma=1 cap=0 game=200 ctrl=120)" },
    { "sound", "cdaudio", CFG_CHOICE, CFG_NOW, 0, 0, "auto|analog", "auto", "CD &audio",
      "A music CD's sound: auto reads it through the mixer where the drive can (its volume the mixer's, played "
      "with the rest), analog leaves it to the drive's own output to the card (its cable). Images: the mixer's" },
    /* [net] (M5) */
    { "net", "hostname", CFG_TEXT, CFG_RESTART, 1, 15, "host", "glos", "&Host name",
      "This machine's name on the network (NAME.local)" },
    { "net", "card", CFG_CHOICE, CFG_RESTART, 0, 0, "auto|off", "auto", "Network c&ard",
      "off: GLOS looks for no network card, and the PC is on the network by its cable link alone" },
    { "net", "cable", CFG_TEXT, CFG_RESTART, 1, 31, "line", "off", "&Cable link",
      "The network over a cable to another computer (docs/cable.md): off; server COM1 or client COM1 (a serial "
      "cable, COM1 to COM4, with a speed after it if not 115200); server LPT1 or client LPT1 (a parallel LapLink "
      "cable, LPT1 to LPT3)" },
    { "net", "cable_share", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "on", "&Share the network over the cable",
      "A PC with a network card and a cable link as its server lets the PC at the cable's far end onto its network, "
      "with an address of its own there" },
    { "net", "cable_join", CFG_CHOICE, CFG_RESTART, 0, 0, "on|off", "on", "&Join the network at the cable's far end",
      "A PC with no network card asks over its cable link for an address of the far PC's network" },
    /* [time] (M10): network time, the zone */
    { "time", "sntp", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "on", "&Network time",
      "The clock is set from a time server (SNTP) when the network is up, and kept so" },
    { "time", "server", CFG_TEXT, CFG_NOW, 1, 63, "hostport", "pool.ntp.org", "Time &server",
      "The SNTP server's name or address, and :port if it isn't 123" },
    { "time", "zone", CFG_TEXT, CFG_NOW, 1, 47, "tz", "UTC0", "Time &zone",
      "Where the PC is, as a POSIX TZ string: GMT0BST,M3.5.0/1,M10.5.0 is the United Kingdom's" },
    { "time", "interval", CFG_NUMBER, CFG_NOW, 1, 168, 0, "24", "&Ask again (hours)",
      "How often the time server is asked again" },
    /* [region]: how dates, times and numbers are written (API 2.0 step 5; include/glos/region.h) */
    { "region", "date", CFG_CHOICE, CFG_NOW, 0, 0, "yyyy-mm-dd|dd/mm/yyyy|dd.mm.yyyy|dd-mm-yyyy|mm/dd/yyyy", "yyyy-mm-dd",
      "&Date", "How a date is written: the order of the year, the month and the day, and what is between them" },
    { "region", "clock", CFG_CHOICE, CFG_NOW, 0, 0, "24|12", "24", "&Clock (hours)",
      "The time on a clock of 24 hours, or of 12 with AM and PM" },
    { "region", "decimal", CFG_CHOICE, CFG_NOW, 0, 0, "point|comma", "point", "Decimal &mark",
      "What is written between a number's whole part and the rest" },
    { "region", "thousands", CFG_CHOICE, CFG_NOW, 0, 0, "comma|point|space|none", "comma", "&Thousands mark",
      "What is written between a number's groups of three digits" },
    { "region", "first_day", CFG_CHOICE, CFG_NOW, 0, 0, "monday|sunday", "monday", "&Week starts on",
      "The first day of the week in a calendar" },
    { "region", "codepage", CFG_CHOICE, CFG_PROGRAM, 0, 0, "dos|437|850|852|866|737", "dos", "Code pa&ge",
      "The characters of file names and of DOS programs' text: what DOS says its code page is, or one named here" },
    { "region", "layout", CFG_CHOICE, CFG_NOW, 0, 0, "us|uk", "us", "&Keyboard layout",
      "Which character each key types: a US keyboard's, or a British one's (DOS programs go by DOS's own KEYB)" },
    { "region", "language", CFG_CHOICE, CFG_APP, 0, 0, "en", "en", "&Language",
      "The language of GLOS's own words" },
    /* [cache]: the disk cache (docs/disk-cache.md) */
    { "cache", "enabled", CFG_CHOICE, CFG_NOW, 0, 0, "on|off|verify|count", "on", "&Disk cache",
      "GLOS keeps what DOS reads from the hard disks in spare memory; verify also reads the disk and compares, count "
      "keeps nothing and counts. Its 4 KB block buffer comes with GLOS's next start" },
    { "cache", "size", CFG_NUMBER, CFG_NOW, 1, 2048, "auto", "auto", "Cache &size (MB)",
      "At most this much memory; auto: all that is spare, given back the moment anything needs it" },
    { "cache", "driver", CFG_CHOICE, CFG_NOW, 0, 0, "bios|glos|glos-read", "bios", "Disk dri&ver",
      "How the cache reads and writes IDE hard disks: through the BIOS, or by GLOS's own driver (a trial: "
      "each disk it can match to the BIOS's); glos-read: GLOS's driver reads, the BIOS writes" },
    /* [memory]: upper memory (docs/upper-memory.md) */
    { "memory", "stub_page", CFG_TEXT, CFG_RESTART, 3, 4, "page", "off", "&Upper memory page",
      "GLOS's resident part (2.6 KB) in a free 4 KB page above 640 KB: auto finds one from C800, or name it (D800). "
      "A page a card or ROM uses is refused" },
    { "memory", "ems", CFG_TEXT, CFG_RESTART, 2, 5, "ems", "auto", "&Expanded memory (EMS)",
      "DOS programs' expanded memory, with GLOS started by itself: auto (up to 8 MB), off, or the KB (64 to 32768). "
      "Its 64 KB window is the first free one above 640 KB" },
    /* [vnc] (M10 step F) */
    { "vnc", "enable", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "off", "&Viewers through SSH",
      "A VNC viewer may see and use the desktop, through an SSH tunnel to port 5900 (ssh -L): whoever may log in" },
    /* [desktop] (M9) */
    { "desktop", "start", CFG_TEXT, CFG_DESKTOP, 0, 120, "line", "", "&Start with the desktop",
      "Programs started when the desktop starts, beside GLOS.EXE (FILES.EXE draws the desktop's icons)" },
    { "desktop", "tour", CFG_CHOICE, CFG_DESKTOP, 0, 0, "off|on", "off", "Show the t&our again",
      "Help's tour of the desktop when the desktop next comes on, once (GLOS's installer sets it for the first start)" },
    { "desktop", "tips", CFG_CHOICE, CFG_DESKTOP, 0, 0, "off|on", "off", "T&ip of the day",
      "A tip from the user guide each time the desktop comes on" },
    { "desktop", "bar", CFG_CHOICE, CFG_NOW, 0, 0, "bottom|top", "bottom", "Task &bar at the screen's",
      "Where the task bar is: along the screen's bottom or its top" },
    { "desktop", "bar_hide", CFG_CHOICE, CFG_NOW, 0, 0, "off|on", "off", "&Hide the task bar",
      "The task bar out of the way until the pointer reaches the screen's edge it is at" },
    { "desktop", "group", CFG_CHOICE, CFG_NOW, 0, 0, "auto|always|never", "auto", "&Group the task bar's buttons",
      "A program's windows share one button on the task bar (auto: when their buttons would be too narrow)" },
    { "desktop", "quiet", CFG_CHOICE, CFG_NOW, 0, 0, "off|on", "off", "&Quiet: hold notifications back",
      "Notifications wait, and the task bar says how many: a press on that number shows them" },
    { "desktop", "recycle", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "on", "&Recycle folder",
      "What is deleted in Files goes to its drive's Recycle folder and can be had back (off: it is deleted at once)" },
    { "desktop", "snap", CFG_CHOICE, CFG_NOW, 0, 0, "on|off", "on", "S&nap to the screen's edges",
      "A window dragged to the screen's top fills it; dragged to a side, it fills that half" },
    { "desktop", "text", CFG_CHOICE, CFG_NOW, 0, 0, "small|normal|large", "normal", "Te&xt size",
      "The size of the text in windows, menus and dialogs: every program lays its windows out again" },
    { "desktop", "theme", CFG_CHOICE, CFG_NOW, 0, 0, "slate|dark|contrast", "slate", "&Theme",
      "The desktop's colours: Slate, a dark one, or one of the strongest contrast" },
    { "desktop", "wallpaper", CFG_PATH, CFG_NOW, 0, 119, 0, "", "&Wallpaper",
      "A picture for the desktop (PNG, JPEG, BMP, GIF or TGA); empty: the theme's colour" },
    { "desktop", "wallpaper_style", CFG_CHOICE, CFG_NOW, 0, 0, "fill|stretch|centre|tile", "fill", "Wallpaper st&yle",
      "How the picture is put on the desktop: made to cover it, made its size, in its middle, or side by side" },
    { "desktop", "colour", CFG_TEXT, CFG_NOW, 5, 6, "colour", "theme", "Desktop co&lour",
      "The desktop's colour where no picture covers it: theme (the theme's), or red, green and blue as six hexadecimal digits (204060)" },
    /* [devices]: the devices GLOS's drivers leave to DOS (phase F, the Devices page; kernel/drv/devlist.c) */
    { "devices", "off", CFG_TEXT, CFG_RESTART, 0, 190, "line", "", "&Left to DOS",
      "Devices GLOS's drivers leave to DOS: a PCI device by its place (00:03.0), sb16, crystal or ne2000, spaces "
      "between them" },
    /* [services]: the background apps (phase D; kernel/core/services.c) */
    { "services", "run", CFG_TEXT, CFG_NOW, 0, 190, "line", "", "Pr&ograms",
      "Programs GLOS starts with itself and starts again if they end: their command lines, ; between them" },
    /* [saver]: the screen saver and the screen's power (API 2.0 step 5; kernel/wm/idle.c) */
    { "saver", "wait", CFG_NUMBER, CFG_NOW, 1, 240, "off", "off", "&Wait (minutes)",
      "The saver comes on when nobody has touched the keyboard or the mouse for so long (off: never)" },
    { "saver", "saver", CFG_CHOICE, CFG_NOW, 0, 0, "blank|stars|pipes|text|objects|maze", "blank", "&Saver",
      "What the saver shows: a black screen, stars, pipes, a word that turns, tumbling blocks, or a walk in a maze" },
    { "saver", "text", CFG_TEXT, CFG_NOW, 0, 12, "line", "GLOS", "&Text saver's word",
      "The word the text saver shows: capitals and digits, twelve at most (as the saver next starts)" },
    { "saver", "speed", CFG_CHOICE, CFG_NOW, 0, 0, "slow|normal|fast", "normal", "Sp&eed",
      "How fast the savers move (as the saver next starts)" },
    { "saver", "detail", CFG_CHOICE, CFG_NOW, 0, 0, "less|normal|more", "normal", "&Detail",
      "How much the savers draw: the stars, the pipes at once, the blocks, the maze's size (as the saver next starts)" },
    { "saver", "power", CFG_NUMBER, CFG_NOW, 1, 240, "off", "off", "Screen &off after (minutes more)",
      "The screen's power is turned off so long after the saver came on, where the card can (off: never)" },
    /* [shell] (M3, M4e) */
    { "shell", "comspec", CFG_PATH, CFG_RESTART, 0, 79, 0, "", "&COMMAND.COM",
      "The command interpreter for batch files and the console (empty: COMSPEC, or found)" },
    { "shell", "command", CFG_CHOICE, CFG_RESTART, 0, 0, "glos|dos", "glos", "COMMAND.COM for progra&ms",
      "What a DOS program that shells out is given: GLOS's own COMMAND.COM, which is GLOS's shell, or the DOS's "
      "(for a program that needs the real one)" },
    { "shell", "autoexec", CFG_PATH, CFG_RESTART, 0, 79, 0, "", "&AUTOEXEC.BAT",
      "As the shell: the batch file run first (empty: the boot drive's \\AUTOEXEC.BAT)" },
    { "shell", "console", CFG_TEXT, CFG_RESTART, 0, 127, "line", "", "C&onsole",
      "As the shell: run (through COMSPEC /C) instead of the prompt" },
    { "shell", "prompt", CFG_CHOICE, CFG_RESTART, 0, 0, "on|off", "on", "&Prompt on the PC's screen",
      "With no desktop: GLOS's shell at the PC's own screen and keyboard (off: a line that says GLOS runs)" },
    { "shell", "envsize", CFG_NUMBER, CFG_RESTART, 256, 32768, 0, "1024", "&Environment (bytes)",
      "As the shell: the master environment's size" },
    { "shell", "options", CFG_TEXT, CFG_RESTART, 0, 79, "line", "", "&Flags",
      "GLOS.EXE's flags: /DPMITRACE, /NOVME, /GDB, /DIRECT" },
    /* [boot] (native GLOS's start-up menu: the loader reads them, GLOSLDR.SYS; 2026-11-25) */
    { "boot", "default", CFG_TEXT, CFG_RESTART, 1, 12, "boot", "glos", "&Start",
      "What the start-up menu starts when nobody chooses: glos, dos (the DOS on GLOS's volume), or partition N" },
    { "boot", "timeout", CFG_NUMBER, CFG_RESTART, 0, 60, "off", "5", "&Wait (seconds)",
      "How long the start-up menu waits before starting it (0: at once; off: until a key is pressed)" },
    { "boot", "glos_name", CFG_TEXT, CFG_RESTART, 0, 31, "line", "", "&GLOS's name",
      "GLOS's line in the menu (empty: GLOS)" },
    { "boot", "dos_name", CFG_TEXT, CFG_RESTART, 0, 31, "line", "", "&DOS's name",
      "The other system's line in the menu (empty: as the loader finds it, MS-DOS or Windows)" },
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
    { "program", "group", CFG_TEXT, CFG_NOW, 0, 31, "line", "", "Gro&up",
      "Its group in the Start menu" },
    { "program", "path", CFG_PATH, CFG_NOW, 0, 79, 0, "", "Pat&h",
      "The folder its file is in (empty: GLOS's own, or DOS's PATH)" },
    { "program", "tasks", CFG_TEXT, CFG_NOW, 0, 191, "line", "", "&Jump list",
      "Its tasks in the Start menu: Label=ARGUMENTS;..." },
    { "program", "verbs", CFG_TEXT, CFG_NOW, 0, 191, "line", "", "&Verbs",
      "What it offers for kinds of file beside opening them: .KIND:Label=ARGUMENTS;..." },
    { "program", "env", CFG_ENV, CFG_PROGRAM, 0, 190, 0, "", "&Environment",
      "NAME=VALUE lines added to its environment" },
    { "program", "joymap", CFG_TEXT, CFG_PROGRAM, 0, 47, "line", "", "&Controller mapping",
      "Its own pad mapping ([joystick] map) while it runs: the same eight words (empty: [joystick] map's)" },
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
static int hexval(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : -1; }
static int hexpair(const char *s) { return hexval(s[0]) >= 0 && hexval(s[1]) >= 0; }

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

long cfg_page(const char *v)
{
    long seg = 0;
    int i;
    if (same(v, "off"))
        return 0;
    if (same(v, "auto"))
        return 1;
    for (i = 0; i < 4; i++) {
        int c = lower((unsigned char)v[i]);
        if (c >= '0' && c <= '9')
            seg = seg * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f')
            seg = seg * 16 + (c - 'a' + 10);
        else
            return -1;
    }
    return !v[4] && !(seg & 0xFF) && seg >= 0xC800 && seg <= 0xEF00 ? seg : -1;
}

/* (cfg_text_set's) out[at..] = s, n bytes, or room exceeded */
static int put_n(char *out, int at, int room, const char *s, int n)
{
    int i;
    if (at < 0 || at + n > room)
        return -1;
    for (i = 0; i < n; i++)
        out[at + i] = s[i];
    return at + n;
}

/* A line's text between its spaces: *b, *e */
static void trim(const char *l, int n, int *b, int *e)
{
    *b = 0, *e = n;
    while (*b < *e && (l[*b] == ' ' || l[*b] == '\t'))
        (*b)++;
    while (*e > *b && (l[*e - 1] == ' ' || l[*e - 1] == '\t'))
        (*e)--;
}

int cfg_text_set(const char *in, int n, const char *section, const char *key, const char *value, char *out, int room)
{
    const char *eol = "\r\n";
    int i, o = 0, in_it = 0, done = 0, found = 0, mark = -1, el, sl = len(section), kl = len(key);

    for (i = 0; i < n && in[i] != '\n'; i++)
        ;
    if (i < n && (i == 0 || in[i - 1] != '\r'))
        eol = "\n";
    el = len(eol);
    for (i = 0; i < n;) {
        int s = i, e, b, t, set_here = 0, drop = 0;
        while (i < n && in[i] != '\n')
            i++;
        e = i > s && in[i - 1] == '\r' ? i - 1 : i;
        if (i < n)
            i++;
        trim(in + s, e - s, &b, &t);
        if (t > b && in[s + b] == '[') {        /* a section begins: the one wanted left without the key, it goes in */
            int nb = b + 1, ne;
            for (ne = nb; ne < t && in[s + ne] != ']'; ne++)
                ;
            if (in_it && !done)
                set_here = 1;
            while (nb < ne && (in[s + nb] == ' ' || in[s + nb] == '\t'))
                nb++;
            while (ne > nb && (in[s + ne - 1] == ' ' || in[s + ne - 1] == '\t'))
                ne--;
            in_it = ne - nb == sl && same_n(in + s + nb, section, sl);
            found |= in_it;
        } else if (in_it && t > b && in[s + b] != ';' && in[s + b] != '#') {
            int k = b, ke;
            while (k < t && in[s + k] != '=')
                k++;
            for (ke = k; ke > b && (in[s + ke - 1] == ' ' || in[s + ke - 1] == '\t'); ke--)
                ;
            if (k < t && ke - b == kl && same_n(in + s + b, key, kl)) {
                if (done)
                    drop = 1;           /* (a second line of the key's) */
                else {
                    o = put_n(out, o, room, key, kl);
                    o = put_n(out, o, room, " = ", 3);
                    o = put_n(out, o, room, value, len(value));
                    o = put_n(out, o, room, eol, el);
                    done = 1, mark = o;
                    continue;
                }
            }
        }
        if (set_here) {                         /* after the section's last line that isn't blank */
            int at = mark, tail = o - mark, j;
            char line[200];
            int ln = 0;
            ln = put_n(line, ln, (int)sizeof line, key, kl);
            ln = put_n(line, ln, (int)sizeof line, " = ", 3);
            ln = put_n(line, ln, (int)sizeof line, value, len(value));
            ln = put_n(line, ln, (int)sizeof line, eol, el);
            if (ln < 0 || o < 0 || o + ln > room)
                return -1;
            for (j = tail - 1; j >= 0; j--)
                out[at + ln + j] = out[at + j];
            for (j = 0; j < ln; j++)
                out[at + j] = line[j];
            o += ln;
            done = 1;
        }
        if (drop)
            continue;
        o = put_n(out, o, room, in + s, e - s);
        o = put_n(out, o, room, eol, el);
        if (in_it && t > b)
            mark = o;                           /* (the section's last line with something on it, so far) */
        if (o < 0)
            return -1;
    }
    if (in_it && !done) {
        int at = mark, tail = o - mark, j, ln = kl + 3 + len(value) + el;
        if (o + ln > room)
            return -1;
        for (j = tail - 1; j >= 0; j--)
            out[at + ln + j] = out[at + j];
        o = at;
        o = put_n(out, o, room, key, kl);
        o = put_n(out, o, room, " = ", 3);
        o = put_n(out, o, room, value, len(value));
        o = put_n(out, o, room, eol, el);
        o += tail;
        done = 1;
    }
    if (!found) {                               /* a new section at the end, after a blank line */
        if (o > 0 && !(o >= 2 * el && out[o - el - 1] == '\n') && o >= el)
            o = put_n(out, o, room, eol, el);
        o = put_n(out, o, room, "[", 1);
        o = put_n(out, o, room, section, sl);
        o = put_n(out, o, room, "]", 1);
        o = put_n(out, o, room, eol, el);
        o = put_n(out, o, room, key, kl);
        o = put_n(out, o, room, " = ", 3);
        o = put_n(out, o, room, value, len(value));
        o = put_n(out, o, room, eol, el);
    }
    return o;
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

int cfg_colour(const char *value, unsigned *rgb)
{
    unsigned c = 0;
    int i;
    if (same(value, "theme"))
        return 1;
    for (i = 0; i < 6; i++) {
        char d = value[i];
        c <<= 4;
        if (d >= '0' && d <= '9')
            c |= (unsigned)(d - '0');
        else if ((d | 0x20) >= 'a' && (d | 0x20) <= 'f')
            c |= (unsigned)((d | 0x20) - 'a' + 10);
        else
            return -1;
    }
    if (value[6])
        return -1;
    *rgb = c;
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
        if (same(k->choices ? k->choices : "", "page")) {
            if (cfg_page(v) < 0)
                return say(why, room, "off, auto, or a page's segment from C800 to EF00 (D800)", "", "");
            return 0;
        }
        if (same(k->choices ? k->choices : "", "pciaddr")) {  /* auto, or BB:DD.F in hex (glos pci's form) */
            if (same(v, "auto"))
                return 0;
            if (vl != 7 || v[2] != ':' || v[5] != '.' || !hexpair(v) || !hexpair(v + 3) || hexval(v[3]) > 1
                || v[6] < '0' || v[6] > '7')
                return say(why, room, "auto, or a PCI address as glos pci lists it (01:00.0)", "", "");
            return 0;
        }
        if (same(k->choices ? k->choices : "", "ems")) {      /* auto, off, or KB: 64 to 32768, 16 KB pages */
            long kb;
            if (same(v, "auto") || same(v, "off"))
                return 0;
            if (number(v, &kb) != 0 || kb < 64 || kb > 32768 || kb % 16)
                return say(why, room, "auto, off, or the KB from 64 to 32768, a multiple of 16", "", "");
            return 0;
        }
        if (same(k->choices ? k->choices : "", "boot")) {
            if (same(v, "glos") || same(v, "dos"))
                return 0;
            if (len(v) == 11 && v[0] == 'p' && v[9] == ' ' && v[10] >= '1' && v[10] <= '8') {
                char head[10];
                int i;
                for (i = 0; i < 9; i++)
                    head[i] = v[i];
                head[9] = 0;
                if (same(head, "partition"))
                    return 0;
            }
            return say(why, room, "glos, dos, or partition 1 to 8", "", "");
        }
        if (same(k->choices ? k->choices : "", "colour")) {
            unsigned rgb;
            if (cfg_colour(v, &rgb) < 0)
                return say(why, room, "theme, or six hexadecimal digits: red, green, blue (as 204060)", "", "");
            return 0;
        }
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
