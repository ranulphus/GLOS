/* DOS code pages to and from UTF-8 (include/glos/cp.h; M10 step G). */
#include "glos/cp.h"

unsigned cp_char(int page, unsigned char b)
{
    const unsigned short *t;
    if (b < 0x80)
        return b;
    t = cp_table(page);
    return (t ? t : cp_table(437))[b - 0x80];
}

int cp_byte(int page, unsigned u)
{
    const unsigned short *t = cp_table(page);
    int i;
    if (u < 0x80)
        return (int)u;
    if (!t)
        t = cp_table(437);
    for (i = 0; i < 128; i++)
        if (t[i] == u)
            return 0x80 + i;
    return -1;
}

static int put_utf8(unsigned u, char *out, int at, int room)
{
    if (u < 0x80) {
        if (at + 1 >= room)
            return -1;
        out[at++] = (char)u;
    } else if (u < 0x800) {
        if (at + 2 >= room)
            return -1;
        out[at++] = (char)(0xC0 | u >> 6);
        out[at++] = (char)(0x80 | (u & 0x3F));
    } else {
        if (at + 3 >= room)
            return -1;
        out[at++] = (char)(0xE0 | u >> 12);
        out[at++] = (char)(0x80 | ((u >> 6) & 0x3F));
        out[at++] = (char)(0x80 | (u & 0x3F));
    }
    return at;
}

int cp_to_utf8(int page, const char *in, char *out, int room)
{
    int n = 0, k;
    if (room < 1)
        return 0;
    for (; *in; in++) {
        if ((k = put_utf8(cp_char(page, (unsigned char)*in), out, n, room)) < 0)
            break;
        n = k;
    }
    out[n] = 0;
    return n;
}

unsigned cp_utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    unsigned c = s[0], v, need = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0, k;
    *p += 1;
    if (c < 0xC2 || c > 0xF4)
        return c;
    v = c & (0x3Fu >> need);
    for (k = 1; k <= need; k++) {
        if ((s[k] & 0xC0) != 0x80)
            return c;
        v = v << 6 | (s[k] & 0x3F);
    }
    *p += need;
    return v;
}

int cp_from_utf8(int page, const char *in, char *out, int room)
{
    int n = 0, lost = 0;
    while (*in) {
        unsigned u = cp_utf8_next(&in);
        int b = cp_byte(page, u);
        if (n + 1 >= room)
            return -1;
        if (b < 0) {
            b = '?';
            lost++;
        }
        out[n++] = (char)b;
    }
    if (room > 0)
        out[n] = 0;
    return lost;
}
