/* FINDFIRST and FINDNEXT in GLOS's own DOS (find.h). */
#include <string.h>

#include "find.h"
#include "names.h"

static unsigned rd16(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }

static void wr16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

void dos_label_name(const unsigned char n11[11], char out[13])
{
    int n, k = 0, i;

    for (n = 8; n && n11[n - 1] == ' '; n--)
        ;
    for (i = 0; i < n; i++)
        out[k++] = (char)n11[i];
    for (n = 3; n && n11[8 + n - 1] == ' '; n--)
        ;
    if (n)
        out[k++] = '.';
    for (i = 0; i < n; i++)
        out[k++] = (char)n11[8 + i];
    out[k] = 0;
}

void dos_name11(const char *s, unsigned char out[11])
{
    int k = 0;

    memset(out, ' ', 11);
    if (!strcmp(s, ".") || !strcmp(s, "..")) {
        memcpy(out, s, strlen(s));
        return;
    }
    for (; *s && *s != '.' && k < 8; s++)
        out[k++] = (unsigned char)*s;
    if (*s == '.')
        for (s++, k = 8; *s && k < 11; s++)
            out[k++] = (unsigned char)*s;
    if (out[0] == 0xE5)
        out[0] = 0x05;
}

/* From the DTA's place on: the next entry matching it, filled in. 0, or 12h. */
static int next(struct fsv_vol *v, unsigned char *dta)
{
    struct fat_dir d;
    struct fat_ent e;
    int r;

    memset(&d, 0, sizeof d);
    d.v = &v->fat;
    d.first = rd16(dta + 0x0F) | (unsigned long)rd16(dta + 0x11) << 16;
    d.index = rd16(dta + 0x0D) | (unsigned long)(dta[0x13] << 16);
    while ((r = fsv_readdir(v, &d, &e)) > 0) {
        unsigned char n11[11];

        dos_name11(e.sname, n11);
        if (!dos_match(dta + 1, n11) || !dos_attr_match(dta[0x0C], e.attr))
            continue;
        wr16(dta + 0x0D, (unsigned)(d.index & 0xFFFF));
        dta[0x13] = (unsigned char)(d.index >> 16);
        dta[0x15] = e.attr;
        wr16(dta + 0x16, e.wtime);
        wr16(dta + 0x18, e.wdate);
        wr16(dta + 0x1A, (unsigned)(e.size & 0xFFFF));
        wr16(dta + 0x1C, (unsigned)(e.size >> 16));
        memset(dta + 0x1E, 0, 13);
        strncpy((char *)dta + 0x1E, e.sname, 12);
        return 0;
    }
    wr16(dta + 0x0D, (unsigned)(d.index & 0xFFFF));
    dta[0x13] = (unsigned char)(d.index >> 16);
    return 0x12;
}

int dos_findfirst(struct fsv_vol *v, unsigned drive, const char *dir, const char *spec, unsigned attr, int cp,
                  unsigned char *dta)
{
    struct fat_ent e;
    int r;

    memset(dta, 0, DOS_DTA_FIND);
    if (dos_template(cp, spec, dta + 1) < 0)
        return 0x02;
    if (fsv_lookup(v, dir, &e) != FAT_OK || !(e.attr & FAT_A_DIR))
        return 0x03;
    dta[0] = (unsigned char)(drive & 0x7F);
    dta[0x0C] = (unsigned char)attr;
    wr16(dta + 0x0F, (unsigned)(e.cluster & 0xFFFF));
    wr16(dta + 0x11, (unsigned)(e.cluster >> 16));
    /* The volume's label: 08h alone looks for it, in the root: the root folder's label entry, as MS-DOS 7.1 finds it
       (not the boot sector's copy: tests/dos/dosdiff.c's L lines), its name as a file's, a dot after the eighth
       character ("DDIFF LA.BEL"), its attribute (28h) and times the entry's. */
    if ((attr & 0xFF) == FAT_A_LABEL) {
        unsigned char n11[11], ent[32];

        if (e.cluster || fsv_label_get(v, n11, ent) != 1)
            return 0x12;
        dta[0x15] = ent[11];
        memcpy(dta + 0x16, ent + 22, 4);        /* (the time, the date) */
        memset(dta + 0x1A, 0, 4);
        memset(dta + 0x1E, 0, 13);
        dos_label_name(n11, (char *)dta + 0x1E);
        wr16(dta + 0x0D, 0xFFFF);               /* (the search is over) */
        return 0;
    }
    r = next(v, dta);
    return r;
}

int dos_findnext(struct fsv_vol *v, unsigned char *dta)
{
    if (rd16(dta + 0x0D) == 0xFFFF && dta[0x13] == 0)
        return 0x12;
    return next(v, dta);
}

/* ---- a CD's drive */

static unsigned long rd32(const unsigned char *p) { return rd16(p) | (unsigned long)rd16(p + 2) << 16; }

static int next_cd(struct iso_vol *cv, unsigned char *dta)
{
    struct iso_entry d, e;
    unsigned long pos = rd16(dta + 0x0D) | (unsigned long)dta[0x13] << 16;
    int r;

    if (iso_dir_at(cv, rd32(dta + 0x0F), &d) != ISO_OK)
        return 0x12;
    while ((r = iso_next(cv, &d, &pos, &e)) == ISO_OK) {
        unsigned char n11[11];

        dos_name11(e.name, n11);
        if (!dos_match(dta + 1, n11) || !dos_attr_match(dta[0x0C], e.attr))
            continue;
        wr16(dta + 0x0D, (unsigned)(pos & 0xFFFF));
        dta[0x13] = (unsigned char)(pos >> 16);
        dta[0x15] = e.attr;
        wr16(dta + 0x16, e.time);
        wr16(dta + 0x18, e.date);
        wr16(dta + 0x1A, (unsigned)(e.size & 0xFFFF));
        wr16(dta + 0x1C, (unsigned)(e.size >> 16));
        memset(dta + 0x1E, 0, 13);
        strncpy((char *)dta + 0x1E, e.name, 12);
        return 0;
    }
    wr16(dta + 0x0D, 0xFFFF);                   /* (the search is over) */
    dta[0x13] = 0xFF;
    return 0x12;
}

int dos_findfirst_cd(struct iso_vol *cv, unsigned drive, const char *dir, const char *spec, unsigned attr, int cp,
                     unsigned char *dta)
{
    struct iso_entry d;
    int r;

    memset(dta, 0, DOS_DTA_FIND);
    if (dos_template(cp, spec, dta + 1) < 0)
        return 0x02;
    r = iso_lookup(cv, dir, &d);
    if (r == ISO_EIO)
        return 0x1F;
    if (r != ISO_OK || !d.dir)
        return 0x03;
    dta[0] = (unsigned char)((drive & 0x7F) | 0x80);
    dta[0x0C] = (unsigned char)attr;
    wr16(dta + 0x0F, (unsigned)(d.lba & 0xFFFF));
    wr16(dta + 0x11, (unsigned)(d.lba >> 16));
    if ((attr & 0xFF) == 0x08) {                /* (the volume's label: in the root alone) */
        if (d.lba != cv->root.lba || !cv->label[0])
            return 0x12;
        dta[0x15] = 0x08;
        memset(dta + 0x1E, 0, 13);
        strncpy((char *)dta + 0x1E, cv->label, 11);
        wr16(dta + 0x0D, 0xFFFF);
        dta[0x13] = 0xFF;
        return 0;
    }
    return next_cd(cv, dta);
}

int dos_findnext_cd(struct iso_vol *cv, unsigned char *dta)
{
    if (rd16(dta + 0x0D) == 0xFFFF && dta[0x13] == 0xFF)
        return 0x12;
    return next_cd(cv, dta);
}
