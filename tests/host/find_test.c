/* find_test: kernel/ndos/find.c, FINDFIRST and FINDNEXT over a volume in memory: attributes, wildcards, a
 * subfolder's "." and "..", long names found by their short names, two searches at once, a file deleted between
 * calls, the volume's label, the errors. "find_test: 0 failures" when all held. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../kernel/disk/disk.h"
#include "../../kernel/ndos/find.h"

static int fails;
static unsigned char *disk;
static struct fsv_vol vol;

#define SECTORS 65536UL

static int rd(void *c, unsigned long lba, unsigned char *b, unsigned n) { (void)c; memcpy(b, disk + lba * 512, n * 512UL); return 0; }
static int wr(void *c, unsigned long lba, const unsigned char *b, unsigned n) { (void)c; memcpy(disk + lba * 512, b, n * 512UL); return 0; }

static void file(const char *path, const char *data, unsigned char attr)
{
    struct fsv_file f;

    fsv_create(&vol, path, 0, 0, FSV_RW, FSV_DENY_NONE, &f);
    fsv_write(&f, data, strlen(data));
    fsv_close(&f);
    if (attr)
        fsv_setattr(&vol, path, attr);
}

/* Every name a search finds, space-separated, in the order found. */
static void list(const char *dir, const char *spec, unsigned attr, const char *want, int want_first)
{
    unsigned char dta[64];
    char got[512] = "";
    int r = dos_findfirst(&vol, 2, dir, spec, attr, 437, dta);

    if (r != want_first) {
        printf("FAIL %s %s %02x: first %d, want %d\n", dir, spec, attr, r, want_first);
        fails++;
        return;
    }
    if (r && r != 0x12)
        return;                                 /* (an error: nothing found, nothing more to look at) */
    while (!r) {
        strcat(got, (char *)dta + 0x1E);
        strcat(got, " ");
        r = dos_findnext(&vol, dta);
    }
    if (r != 0x12 || strcmp(got, want)) {
        printf("FAIL %s %s %02x: \"%s\" (end %02x), want \"%s\"\n", dir, spec, attr, got, r, want);
        fails++;
    }
}

int main(void)
{
    struct fat_geo g = { SECTORS, 0, 16, 63, 0x80, 1, "FINDLABEL", 16 };
    struct fat_made m;
    unsigned char a[64], b[64];
    int r;

    disk = calloc(SECTORS, 512);
    if (!disk)
        return 2;
    fat_format(&g, wr, 0, &m);
    fsv_mount(&vol, rd, wr, 0, 0, 437, 0);
    file("\\README.TXT", "readme", 0);
    file("\\AUTOEXEC.BAT", "@echo off", 0);
    file("\\HIDDEN.SYS", "x", FAT_A_HIDDEN);
    file("\\CONFIG.SYS", "files=40", FAT_A_RDONLY | FAT_A_ARCHIVE);
    fsv_mkdir(&vol, "\\SUBDIR");
    file("\\SUBDIR\\ONE.C", "1", 0);
    file("\\SUBDIR\\A Long Name.txt", "long", 0);

    list("\\", "*.*", 0x00, "README.TXT AUTOEXEC.BAT CONFIG.SYS ", 0);
    list("\\", "*.*", 0x16, "README.TXT AUTOEXEC.BAT HIDDEN.SYS CONFIG.SYS SUBDIR ", 0);
    list("\\", "*.BAT", 0x00, "AUTOEXEC.BAT ", 0);
    list("\\", "readme.txt", 0x00, "README.TXT ", 0);
    list("\\", "?????.*", 0x00, "", 0x12);
    list("\\", "??????.*", 0x00, "README.TXT CONFIG.SYS ", 0);
    list("\\", "*", 0x10, "SUBDIR ", 0);
    list("\\SUBDIR", "*.*", 0x10, ". .. ONE.C ALONGN~1.TXT ", 0);
    list("\\SUBDIR", "*.TXT", 0x00, "ALONGN~1.TXT ", 0);
    list("\\", "*.XYZ", 0x00, "", 0x12);
    list("\\NOPE", "*.*", 0x00, "", 0x03);
    list("\\", "a|b", 0x00, "", 0x02);

    /* Two searches at once, each in its own DTA. */
    r = dos_findfirst(&vol, 2, "\\", "*.*", 0, 437, a);
    r |= dos_findfirst(&vol, 2, "\\SUBDIR", "*.*", 0, 437, b);
    r |= dos_findnext(&vol, a);
    r |= dos_findnext(&vol, b);
    if (r || strcmp((char *)a + 0x1E, "AUTOEXEC.BAT") || strcmp((char *)b + 0x1E, "ALONGN~1.TXT")) {
        printf("FAIL two at once: %s %s\n", a + 0x1E, b + 0x1E);
        fails++;
    }
    if (a[0] != 2 || a[0x15] != 0x20 || (a[0x1A] | a[0x1B] << 8) != 9) {
        printf("FAIL the entry's fields: drive %u attr %02x size %u\n", a[0], a[0x15], a[0x1A] | a[0x1B] << 8);
        fails++;
    }

    /* A file deleted between calls: the search goes on from where it was. */
    r = dos_findfirst(&vol, 2, "\\", "*.*", 0, 437, a);
    fsv_unlink(&vol, "\\AUTOEXEC.BAT");
    r |= dos_findnext(&vol, a);
    if (r || strcmp((char *)a + 0x1E, "CONFIG.SYS")) {
        printf("FAIL after a deletion: %s\n", a + 0x1E);
        fails++;
    }

    /* The label: 08h alone, in the root. */
    r = dos_findfirst(&vol, 2, "\\", "*.*", 0x08, 437, a);
    if (r || a[0x15] != 0x08 || strcmp((char *)a + 0x1E, "FINDLABEL") || dos_findnext(&vol, a) != 0x12) {
        printf("FAIL the label: %d %02x \"%s\"\n", r, a[0x15], a + 0x1E);
        fails++;
    }
    printf("find_test: %d failures\n", fails);
    return fails != 0;
}
