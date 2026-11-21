/* DOS's names in GLOS's own DOS (names.h). */
#include <string.h>

#include <glos/cp.h>
#include "../fs/fat.h"
#include "names.h"

/* A capital's letter without its accent (Unicode's canonical decomposition, made from it): Latin-1's C0h-DEh,
   Latin Extended-A's 100h-17Fh; '.' where there is none. */
static const char latin1_base[] = "AAAAAA.CEEEEIIII.NOOOOO..UUUUY.";
static const char exta_base[] = "AAAAAACCCCCCCCDD..EEEEEEEEEEGGGGGGGGHH..IIIIIIIII...JJKK.LLLLLL....NNNNNN...OOOOOO.."
                                "RRRRRRSSSSSSSSTTTT..UUUUUUUUUUUUWWYYYZZZZZZ.";

static int base_letter(unsigned u)
{
    char b = '.';

    if (u >= 0xC0 && u <= 0xDE)
        b = latin1_base[u - 0xC0];
    else if (u >= 0x100 && u <= 0x17F)
        b = exta_base[u - 0x100];
    return b == '.' ? -1 : b;
}

unsigned char dos_upper(int cp, unsigned char b)
{
    unsigned u, up;
    int r;

    if (b < 0x80)
        return (unsigned char)(b >= 'a' && b <= 'z' ? b - 32 : b);
    u = cp_char(cp, b);
    if (u >= 0x370 && u <= 0x3FF && cp != 737 && cp != 869)
        return b;                               /* (Greek in a Latin page, 437's maths letters: DOS leaves them) */
    up = fat_fold(u);
    if (up == u)
        return b;
    r = cp_byte(cp, up);
    if (r < 0 && (r = base_letter(up)) >= 0)    /* (the page hasn't the capital: DOS's tables give the letter) */
        r = cp_byte(cp, (unsigned)r);
    return r < 0 ? b : (unsigned char)r;
}

/* Not in a DOS name (a wildcard is a template's, not a name's). */
static int refused(unsigned char c)
{
    return c < 0x20 || c == ' ' || (c < 0x80 && strchr("\"/:<>\\|+,;=[]", c) != 0);
}

int dos_template(int cp, const char *spec, unsigned char t[11])
{
    const unsigned char *p = (const unsigned char *)spec;
    int part = 0, pos = 0, wild = 0, star = 0, had = 0;

    memset(t, ' ', 11);
    if (!strcmp(spec, ".") || !strcmp(spec, "..")) {
        memcpy(t, spec, strlen(spec));          /* (the folder's own entries) */
        return 0;
    }
    for (; *p; p++) {
        int max = part ? 3 : 8, off = part ? 8 : 0;

        if (*p == '.') {
            if (part)
                return -1;                      /* (a second dot: not a short name) */
            part = 1;
            pos = star = 0;
            continue;
        }
        if (star)
            continue;                           /* (after '*', the rest of the part is ignored) */
        if (*p == '*') {
            for (; pos < max; pos++)
                t[off + pos] = '?';
            star = wild = had = 1;
            continue;
        }
        if (*p == '?') {
            if (pos < max)
                t[off + pos++] = '?';
            wild = had = 1;
            continue;
        }
        if (refused(*p))
            return -1;
        if (pos < max)
            t[off + pos++] = dos_upper(cp, *p);    /* (too long: cut short) */
        if (!part)
            had = 1;
    }
    if (!had)
        return -1;                              /* (no name: ".TXT", "") */
    if (t[0] == 0xE5)
        t[0] = 0x05;                            /* (as the directory keeps it) */
    return wild;
}

int dos_match(const unsigned char tmpl[11], const unsigned char name[11])
{
    int i;

    for (i = 0; i < 11; i++)
        if (tmpl[i] != '?' && tmpl[i] != name[i])
            return 0;
    return 1;
}

int dos_attr_match(unsigned search, unsigned char attr)
{
    if ((search & 0xFF) == FAT_A_LABEL)
        return (attr & FAT_A_LABEL) != 0;
    if (attr & FAT_A_LABEL)
        return (search & FAT_A_LABEL) != 0;
    return (attr & (FAT_A_HIDDEN | FAT_A_SYSTEM | FAT_A_DIR) & ~search) == 0;
}
