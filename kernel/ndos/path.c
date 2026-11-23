/* Paths in GLOS's own DOS (path.h). */
#include <string.h>

#include "names.h"
#include "path.h"

#define DOS_PATH_LONGEST 127            /* a whole path, drive and all, as 60h's 128-byte buffer holds it (to check) */

static int sep(char c)
{
    return c == '\\' || c == '/';
}

/* One part ("name.ext", n bytes) appended to out at *len as upper-case 8.3 (longer parts cut: to check): 0, 02h
   (not a name), 03h (too long a path). */
static int add_part(int cp, const char *p, unsigned n, char *out, unsigned room, unsigned *len)
{
    char base[8], ext[3];
    unsigned i, nb = 0, ne = 0, k;
    int in_ext = 0;

    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];

        if (c == '.') {
            if (in_ext)
                return 0x02;                    /* (a second dot) */
            in_ext = 1;
            continue;
        }
        if (c < 0x20 || (c < 0x80 && strchr("\"*:<>?|+,;=[] ", c)))
            return 0x02;
        if (in_ext) {
            if (ne < 3)
                ext[ne++] = (char)dos_upper(cp, c);
        } else if (nb < 8) {
            base[nb++] = (char)dos_upper(cp, c);
        }
    }
    if (!nb)
        return 0x02;
    k = nb + (ne ? 1 + ne : 0);
    if (*len + 1 + k + 1 > room || 2 + *len + 1 + k > DOS_PATH_LONGEST)
        return 0x03;
    out[(*len)++] = '\\';
    memcpy(out + *len, base, nb);
    *len += nb;
    if (ne) {
        out[(*len)++] = '.';
        memcpy(out + *len, ext, ne);
        *len += ne;
    }
    out[*len] = 0;
    return 0;
}

int dos_resolve(const struct dos_cds *c, int cp, const char *path, int *drive, char *out, unsigned room)
{
    const char *p = path;
    unsigned len = 0;
    int d = c->current, r;

    if (room < 2)
        return 0x03;
    if (p[0] && p[1] == ':') {
        unsigned char l = (unsigned char)p[0];

        d = (l | 0x20) - 'a';
        if (d < 0 || d >= DOS_DRIVES)
            return 0x0F;
        p += 2;
    }
    if (d < 0 || d >= DOS_DRIVES || !(c->valid >> d & 1))
        return 0x0F;
    *drive = d;
    out[0] = 0;
    if (!sep(*p)) {                             /* relative: from the drive's current folder */
        const char *q = c->cwd[d];

        while (*q) {
            const char *s;

            while (sep(*q))
                q++;
            for (s = q; *q && !sep(*q); q++)
                ;
            if (q > s && (r = add_part(cp, s, (unsigned)(q - s), out, room, &len)) != 0)
                return r;
        }
    }
    while (*p) {
        const char *s;
        unsigned n;

        while (sep(*p))
            p++;
        for (s = p; *p && !sep(*p); p++)
            ;
        n = (unsigned)(p - s);
        if (!n)
            break;
        if (n == 1 && s[0] == '.')
            continue;
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            if (!len)
                return 0x03;                    /* (above the root: no such path, as MS-DOS 7.1 says) */
            while (len && out[len - 1] != '\\')     /* (the last part off) */
                len--;
            if (len)
                len--;
            out[len] = 0;
            continue;
        }
        r = add_part(cp, s, n, out, room, &len);
        if (r)
            return r;
    }
    if (!len) {
        out[0] = '\\';
        out[1] = 0;
    }
    return 0;
}

/* One part of a long path appended as it is. */
static int add_long(const char *p, unsigned n, char *out, unsigned room, unsigned *len)
{
    unsigned i;

    while (n && (p[n - 1] == ' ' || p[n - 1] == '.'))
        n--;
    if (!n)
        return 0x02;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];

        if (c < 0x20 || c == '"' || c == ':' || c == '<' || c == '>' || c == '|')
            return 0x02;
    }
    if (*len + 1 + n + 1 > room || 2 + *len + 1 + n >= DOS_LONG_PATH)
        return 0x03;
    out[(*len)++] = '\\';
    memcpy(out + *len, p, n);
    *len += n;
    out[*len] = 0;
    return 0;
}

int dos_resolve_long(const struct dos_cds *c, const char *path, int *drive, char *out, unsigned room)
{
    const char *p = path;
    unsigned len = 0;
    int d = c->current, r;

    if (room < 2)
        return 0x03;
    if (p[0] && p[1] == ':') {
        unsigned char l = (unsigned char)p[0];

        d = (l | 0x20) - 'a';
        if (d < 0 || d >= DOS_DRIVES)
            return 0x0F;
        p += 2;
    }
    if (d < 0 || d >= DOS_DRIVES || !(c->valid >> d & 1))
        return 0x0F;
    *drive = d;
    out[0] = 0;
    if (!sep(*p)) {                             /* relative: from the drive's current folder */
        const char *q = c->cwd[d];

        while (*q) {
            const char *s;

            while (sep(*q))
                q++;
            for (s = q; *q && !sep(*q); q++)
                ;
            if (q > s && (r = add_long(s, (unsigned)(q - s), out, room, &len)) != 0)
                return r;
        }
    }
    while (*p) {
        const char *s;
        unsigned n;

        while (sep(*p))
            p++;
        for (s = p; *p && !sep(*p); p++)
            ;
        n = (unsigned)(p - s);
        if (!n)
            break;
        if (n == 1 && s[0] == '.')
            continue;
        if (n == 2 && s[0] == '.' && s[1] == '.') {
            if (!len)
                return 0x03;
            while (len && out[len - 1] != '\\')
                len--;
            if (len)
                len--;
            out[len] = 0;
            continue;
        }
        r = add_long(s, n, out, room, &len);
        if (r)
            return r;
    }
    if (!len) {
        out[0] = '\\';
        out[1] = 0;
    }
    return 0;
}

int dos_truename(const struct dos_cds *c, int cp, const char *path, char *out, unsigned room)
{
    int d, r;

    if (room < 4)
        return 0x03;
    r = dos_resolve(c, cp, path, &d, out + 2, room - 2);
    if (r)
        return r;
    out[0] = (char)('A' + d);
    out[1] = ':';
    return 0;
}

int dos_getcwd(const struct dos_cds *c, int drive, char *out, unsigned room)
{
    int d = drive ? drive - 1 : c->current;
    const char *s;

    if (d < 0 || d >= DOS_DRIVES || !(c->valid >> d & 1))
        return 0x0F;
    s = c->cwd[d];
    while (sep(*s))
        s++;
    if (strlen(s) + 1 > room)
        return 0x03;
    strcpy(out, s);
    return 0;
}
