/* GLOS.CFG's settings (PRD §10.7, D53; docs/milestones-m9.md step B): every
 * setting GLOS has, described once. The loader reads the file with it, the
 * kernel keeps and writes the file and applies what it can at once with it,
 * `glos config` and GLOS_FN_CONFIG answer from it, and the Control Panel is
 * drawn from it. Plain C (sdk/config/cfg.c), for Open Watcom, the kernel,
 * DJGPP and the host.
 *
 * The file: [section] lines, then KEY = VALUE lines; whole lines of
 * comment (";" or "#"); a value runs to its line's end (PATH has
 * semicolons). A [program NAME.EXT] section is a program's profile (PRD
 * D36); [open] has a key per kind of file (".TXT = NOTEPAD.EXE"). */
#ifndef GLOS_CONFIG_H
#define GLOS_CONFIG_H

/* A setting's kind of value. */
#define CFG_CHOICE  1                   /* one of `choices` ("on|off"; any case) */
#define CFG_NUMBER  2                   /* min..max, or one of `choices` as well ("off") */
#define CFG_TEXT    3                   /* up to max characters of the class `choices` names (CFG_CLASS_) */
#define CFG_MODE    4                   /* none, or WIDTHxHEIGHTxBITS (320-1600 x 200-1200 x 8, 16 or 32) */
#define CFG_PATH    5                   /* a DOS path, up to max characters */
#define CFG_BLASTER 6                   /* a BLASTER line: A220 I5 D1 H5 (A, I, D, H, others kept) */
#define CFG_ENV     7                   /* NAME=VALUE; the key may come more than once */

/* When a change takes effect. */
#define CFG_NOW      0                  /* at once */
#define CFG_RESTART  1                  /* at GLOS's next start */
#define CFG_PROGRAM  2                  /* when the program next starts */
#define CFG_DESKTOP  3                  /* when the desktop next starts */
#define CFG_APP      4                  /* when an app next starts */

struct cfg_key {
    const char *section;                /* "display"; "program" for every [program NAME.EXT] */
    const char *key;                    /* "mode"; "*" for any (an [open] section's extensions) */
    unsigned char type, when;
    long min, max;                      /* CFG_NUMBER's range; CFG_TEXT's and CFG_PATH's longest */
    const char *choices;                /* CFG_CHOICE's (and CFG_NUMBER's words); CFG_TEXT's class */
    const char *def;                    /* the default, as text ("" for none) */
    const char *label;                  /* the Control Panel's */
    const char *desc;                   /* a line: what it does */
};

extern const struct cfg_key cfg_schema[];
extern const int cfg_schema_n;

/* The sections a file may have, in the order the Control Panel shows them. */
extern const char *const cfg_sections[];

const struct cfg_key *cfg_find(const char *section, const char *key);  /* section: "program FWHEEL.EXE" too */

/* 0 if value is right for k; else -1 and why (a line) in why. */
int cfg_check(const struct cfg_key *k, const char *value, char *why, int room);

/* The section's kind: "program" for "program NAME.EXT", else the section itself (lower case) into out. */
void cfg_kind(const char *section, char *out, int room);

/* A section name as written: "display", "program FWHEEL.EXE" (the name upper case). */
int cfg_section_ok(const char *section);

#endif
