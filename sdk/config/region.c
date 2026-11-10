/* The region's formats (include/glos/region.h). No library but its own. */
#include <glos/region.h>

static int same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}

static void copy(char *dst, const char *src, int room)
{
    int i;
    for (i = 0; i < room - 1 && src[i]; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

void region_default(struct region *r)
{
    r->date = REGION_YMD;
    r->date_sep = '-';
    r->clock12 = 0;
    r->decimal = '.';
    r->thousands = ',';
    r->first_day = 1;
    r->codepage = 0;
    copy(r->layout, "us", sizeof r->layout);
    copy(r->language, "en", sizeof r->language);
}

int region_set(struct region *r, const char *key, const char *v)
{
    if (same(key, "date")) {                    /* the pattern: its first letter the order, its fifth or third */
        if (same(v, "yyyy-mm-dd"))              /* character the separator */
            r->date = REGION_YMD, r->date_sep = '-';
        else if (same(v, "dd/mm/yyyy"))
            r->date = REGION_DMY, r->date_sep = '/';
        else if (same(v, "dd.mm.yyyy"))
            r->date = REGION_DMY, r->date_sep = '.';
        else if (same(v, "dd-mm-yyyy"))
            r->date = REGION_DMY, r->date_sep = '-';
        else if (same(v, "mm/dd/yyyy"))
            r->date = REGION_MDY, r->date_sep = '/';
        else
            return -1;
    } else if (same(key, "clock")) {
        if (!same(v, "24") && !same(v, "12"))
            return -1;
        r->clock12 = same(v, "12");
    } else if (same(key, "decimal")) {
        if (!same(v, "point") && !same(v, "comma"))
            return -1;
        r->decimal = same(v, "comma") ? ',' : '.';
    } else if (same(key, "thousands")) {
        if (same(v, "comma"))
            r->thousands = ',';
        else if (same(v, "point"))
            r->thousands = '.';
        else if (same(v, "space"))
            r->thousands = ' ';
        else if (same(v, "none"))
            r->thousands = 0;
        else
            return -1;
    } else if (same(key, "first_day")) {
        if (!same(v, "monday") && !same(v, "sunday"))
            return -1;
        r->first_day = same(v, "monday");
    } else if (same(key, "codepage")) {
        unsigned n = 0;
        const char *p;
        if (same(v, "dos")) {                   /* (what DOS says its code page is) */
            r->codepage = 0;
            return 0;
        }
        for (p = v; *p >= '0' && *p <= '9' && n < 10000; p++)
            n = n * 10 + (unsigned)(*p - '0');
        if (*p || (n != 437 && n != 850 && n != 852 && n != 866 && n != 737))
            return -1;
        r->codepage = (unsigned short)n;
    } else if (same(key, "layout")) {
        if (!same(v, "us"))
            return -1;
        copy(r->layout, v, sizeof r->layout);
    } else if (same(key, "language")) {
        if (!same(v, "en"))
            return -1;
        copy(r->language, v, sizeof r->language);
    } else {
        return -1;
    }
    return 0;
}

/* ---- writing */

struct out {
    char *p;
    int room, n;
};

static void put(struct out *o, char c)
{
    if (o->n < o->room - 1)
        o->p[o->n] = c;
    o->n++;
}

static void digits(struct out *o, unsigned v, int width)       /* at least `width` digits */
{
    char t[12];
    int n = 0;
    do
        t[n++] = (char)('0' + v % 10);
    while ((v /= 10) != 0);
    while (n < width)
        t[n++] = '0';
    while (n)
        put(o, t[--n]);
}

static int end(struct out *o)
{
    if (o->room > 0)
        o->p[o->n < o->room ? o->n : o->room - 1] = 0;
    return o->n;
}

int region_date(const struct region *r, char *out, int room, int year, int month, int day)
{
    struct out o = { out, room, 0 };
    unsigned y = (unsigned)year, m = (unsigned)month, d = (unsigned)day;
    if (r->date == REGION_YMD) {
        digits(&o, y, 4), put(&o, r->date_sep), digits(&o, m, 2), put(&o, r->date_sep), digits(&o, d, 2);
    } else {
        digits(&o, r->date == REGION_DMY ? d : m, 2), put(&o, r->date_sep);
        digits(&o, r->date == REGION_DMY ? m : d, 2), put(&o, r->date_sep), digits(&o, y, 4);
    }
    return end(&o);
}

int region_time(const struct region *r, char *out, int room, int hour, int minute, int second)
{
    struct out o = { out, room, 0 };
    unsigned h = (unsigned)hour % 24;
    if (r->clock12)
        digits(&o, h % 12 ? h % 12 : 12, 1);    /* (12:05 AM, 1:05 PM: no leading zero) */
    else
        digits(&o, h, 2);
    put(&o, ':');
    digits(&o, (unsigned)minute, 2);
    if (second >= 0) {
        put(&o, ':');
        digits(&o, (unsigned)second, 2);
    }
    if (r->clock12) {
        put(&o, ' ');
        put(&o, h < 12 ? 'A' : 'P');
        put(&o, 'M');
    }
    return end(&o);
}

int region_number(const struct region *r, char *out, int room, long value, int decimals)
{
    struct out o = { out, room, 0 };
    unsigned long v = value < 0 ? 0ul - (unsigned long)value : (unsigned long)value, whole, frac, scale = 1;
    char t[16];
    int n = 0, i;
    if (decimals < 0)
        decimals = 0;
    if (decimals > 9)
        decimals = 9;
    for (i = 0; i < decimals; i++)
        scale *= 10;
    whole = v / scale;
    frac = v % scale;
    if (value < 0)
        put(&o, '-');
    do
        t[n++] = (char)('0' + whole % 10);
    while ((whole /= 10) != 0);
    while (n) {
        put(&o, t[--n]);
        if (n && n % 3 == 0 && r->thousands)
            put(&o, r->thousands);
    }
    if (decimals) {
        put(&o, r->decimal);
        digits(&o, (unsigned)frac, decimals);
    }
    return end(&o);
}
