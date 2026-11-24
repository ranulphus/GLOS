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
    /* The volume's label: 08h alone looks for it, in the root. */
    if ((attr & 0xFF) == FAT_A_LABEL) {
        if (e.cluster || !v->fat.label[0])
            return 0x12;
        dta[0x15] = FAT_A_LABEL;
        memset(dta + 0x1E, 0, 13);
        strncpy((char *)dta + 0x1E, v->fat.label, 12);
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
