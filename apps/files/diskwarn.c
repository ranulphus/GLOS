/* FILES: a disk nearly full is said (phase D, docs/v1/phase-d.md D2).
 *
 * The shell is there whenever the desktop is, so it is the one that looks: every half minute FS's DRIVES gives
 * each hard disk's size and what is free of it (no floppy or CD is made to spin for it), and a disk with under a
 * twentieth free is said with one of the shell's notifications: once, and again only after it has had twice that
 * free. GLOSDISKWARN in the environment is another percentage (0: never said): for a PC whose disk is always
 * nearly full, and for the tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"

static unsigned told;                   /* a bit a drive's letter: said, until it has room again */
static glos_handle note;                /* the last notification (let go at the next) */

static void look(void *arg)
{
    static struct glos_fs_drive d[26];
    const char *e = getenv("GLOSDISKWARN");
    int warn = e ? atoi(e) : 5;
    unsigned n = 0, i;
    (void)arg;
    if (warn <= 0 || warn > 100)
        return;
    memset(d, 0, sizeof d);
    if (glos_fs_drives(0, d, 26, &n) != 0 && glos_error() != GLOS_E_ROOM)
        return;
    for (i = 0; i < n && i < 26; i++) {
        unsigned long long f = d[i].free_kb, t = d[i].total_kb;
        unsigned bit;
        if (d[i].kind != GLOS_DRIVE_HARD || !(d[i].flags & GLOS_DRIVE_DETAILS) || !t || d[i].free_kb == 0xFFFFFFFFu
            || d[i].letter < 'A' || d[i].letter > 'Z')
            continue;
        bit = 1u << (d[i].letter - 'A');
        if (f * 100 < t * (unsigned)warn) {
            char text[96];
            if (told & bit)
                continue;
            told |= bit;
            snprintf(text, sizeof text, GUI_T("Drive %c: has %u MB free of %u MB."), (int)d[i].letter, d[i].free_kb / 1024,
                     d[i].total_kb / 1024);
            if (note)
                glos_close(note);
            note = glos_notify(GUI_T("Disk nearly full"), text, 0, 0, 0, 0);
            printf("FILES disk-low %c\n", (int)d[i].letter);
            fflush(stdout);
        } else if (f * 100 >= t * (unsigned)warn * 2 || warn >= 50) {
            told &= ~bit;                       /* (room again: a later shortage is news) */
        }
    }
}

/* The watch begun (the shell's start). */
void diskwarn_start(void)
{
    gui_idle(look, NULL, 30000);
}
