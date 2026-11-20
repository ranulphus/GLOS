/* The saved sessions' file (sessions.h): read, written, looked through. Plain C and stdio: the Terminal's,
   SSH.EXE's and Files', and the host's test's (tests/host/sessions_test.c). */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sessions.h"

const char *const session_colour_names[SESSION_COLOURS] = {
    "The theme's", "Black on white", "White on black", "Green screen", "Amber screen", "Blue",
};

static void *(*open_file)(const char *path, const char *mode);

void sessions_files(void *(*open)(const char *path, const char *mode)) { open_file = open; }

static FILE *opened(const char *path, const char *mode)
{
    return open_file ? (FILE *)open_file(path, mode) : fopen(path, mode);
}

void session_defaults(struct session *s)
{
    memset(s, 0, sizeof *s);
}

int session_name_ok(const char *name)
{
    size_t n = strlen(name), i;
    if (!n || n >= sizeof ((struct session *)0)->name || name[0] == '-')
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c <= ' ' || c == 127 || strchr("@:/\\\"'[]=;*?<>|", c))
            return 0;
    }
    return 1;
}

static int number(const char *p, int len, unsigned *out)      /* digits and nothing else, 1 to 65535: 0, or -1 */
{
    unsigned v = 0;
    int i;
    if (len < 1 || len > 5)
        return -1;
    for (i = 0; i < len; i++) {
        if (p[i] < '0' || p[i] > '9')
            return -1;
        v = v * 10 + (unsigned)(p[i] - '0');
    }
    if (!v || v > 65535)
        return -1;
    *out = v;
    return 0;
}

int session_tunnel(const char *spec, int len, int *open, unsigned *port, char *host, int room, unsigned *hport)
{
    const char *end, *c[4];
    int n = 0, o = -1, i;
    unsigned lp = 0, hp = 0;
    if (len < 0)
        len = (int)strlen(spec);
    end = spec + len;
    for (i = 0; i < len; i++)
        if (spec[i] == ':') {
            if (n == 3)
                return -1;
            c[n++] = spec + i;
        }
    if (n < 2)
        return -1;
    if (n == 3) {                               /* an address first: whose the port is */
        int k = (int)(c[0] - spec);
        if ((k == 1 && spec[0] == '*') || (k == 7 && !strncmp(spec, "0.0.0.0", 7)))
            o = 1;
        else if (!k || (k == 9 && (!strncmp(spec, "127.0.0.1", 9) || !strncmp(spec, "localhost", 9))))
            o = 0;
        else
            return -1;
        spec = c[0] + 1, c[0] = c[1], c[1] = c[2];
    }
    if (number(spec, (int)(c[0] - spec), &lp) != 0 || number(c[1] + 1, (int)(end - c[1] - 1), &hp) != 0)
        return -1;
    n = (int)(c[1] - c[0] - 1);                 /* the host's length */
    if (n < 1 || n > 79)
        return -1;
    for (i = 0; i < n; i++)
        if ((unsigned char)c[0][1 + i] <= ' ')
            return -1;
    if (host) {
        if (n >= room)
            return -1;
        memcpy(host, c[0] + 1, (size_t)n);
        host[n] = 0;
    }
    if (open)
        *open = o;
    if (port)
        *port = lp;
    if (hport)
        *hport = hp;
    return 0;
}

int session_tunnels_ok(const char *all)
{
    int n = 0;
    while (*all) {
        const char *e;
        while (*all == ' ')
            all++;
        if (!*all)
            break;
        for (e = all; *e && *e != ' '; e++)
            ;
        if (n == SESSION_TUNNELS || session_tunnel(all, (int)(e - all), 0, 0, 0, 0, 0) != 0)
            return -1;
        n++;
        all = e;
    }
    return n;
}

void sessions_path(char *out, int room, const char *ssh_dir)
{
    snprintf(out, (size_t)room, "%s%sSESSIONS.CFG", ssh_dir, ssh_dir[0] && !strchr("\\/", ssh_dir[strlen(ssh_dir) - 1]) ? "\\" : "");
}

static char *trimmed(char *p)                   /* without the blanks at its ends */
{
    char *e;
    while (*p == ' ' || *p == '\t')
        p++;
    for (e = p + strlen(p); e > p && (unsigned char)e[-1] <= ' '; e--)
        ;
    *e = 0;
    return p;
}

static int within(const char *v, int low, int high, int otherwise)
{
    char *end;
    long n = strtol(v, &end, 10);
    return *v && !*end && n >= low && n <= high ? (int)n : otherwise;
}

int sessions_load(const char *path, struct session *out, int room)
{
    FILE *f = opened(path, "r");
    char line[400];
    struct session *s = NULL;
    int n = 0, skip = 0;
    if (!f)
        return 0;
    while (fgets(line, sizeof line, f)) {
        char *p = trimmed(line), *eq, *v;
        if (!*p || *p == ';' || *p == '#')
            continue;
        if (*p == '[') {                        /* a session begins: one with a name that can't be, or had
                                                   already, or one too many, is passed over to the next */
            char *e = strchr(p, ']');
            s = NULL, skip = 1;
            if (!e)
                continue;
            *e = 0;
            p = trimmed(p + 1);
            if (n == room || !session_name_ok(p) || sessions_find(out, n, p) >= 0)
                continue;
            s = &out[n++], skip = 0;
            session_defaults(s);
            snprintf(s->name, sizeof s->name, "%s", p);
            continue;
        }
        if (!s || skip || !(eq = strchr(p, '=')))
            continue;
        *eq = 0;
        p = trimmed(p), v = trimmed(eq + 1);
        if (!strcmp(p, "host"))
            snprintf(s->host, sizeof s->host, "%s", v);
        else if (!strcmp(p, "user"))
            snprintf(s->user, sizeof s->user, "%s", v);
        else if (!strcmp(p, "port"))
            s->port = within(v, 1, 65535, 0);
        else if (!strcmp(p, "key"))
            snprintf(s->key, sizeof s->key, "%s", v);
        else if (!strcmp(p, "command"))
            snprintf(s->command, sizeof s->command, "%s", v);
        else if (!strcmp(p, "tunnels")) {
            if (session_tunnels_ok(v) >= 0)     /* (a line that is not tunnels is none) */
                snprintf(s->tunnels, sizeof s->tunnels, "%s", v);
        } else if (!strcmp(p, "keepalive"))
            s->keepalive = within(v, 0, 3600, 0);
        else if (!strcmp(p, "log"))
            snprintf(s->log, sizeof s->log, "%s", v);
        else if (!strcmp(p, "colours"))
            s->colours = within(v, 0, SESSION_COLOURS - 1, 0);
        else if (!strcmp(p, "font"))
            s->font = within(v, 0, 3, 0);
        else if (!strcmp(p, "lines"))
            s->lines = within(v, 100, 20000, 0);
    }
    fclose(f);
    return n;
}

int sessions_save(const char *path, const struct session *s, int n)
{
    FILE *f = opened(path, "w");
    int i, bad;
    if (!f)
        return -1;
    fprintf(f, "; GLOS's saved sessions: the Terminal's Sessions keeps them, and `ssh NAME` reads them.\n");
    for (i = 0; i < n; i++, s++) {
        fprintf(f, "\n[%s]\nhost = %s\n", s->name, s->host);
        if (s->port)
            fprintf(f, "port = %d\n", s->port);
        if (s->user[0])
            fprintf(f, "user = %s\n", s->user);
        if (s->key[0])
            fprintf(f, "key = %s\n", s->key);
        if (s->command[0])
            fprintf(f, "command = %s\n", s->command);
        if (s->tunnels[0])
            fprintf(f, "tunnels = %s\n", s->tunnels);
        if (s->keepalive)
            fprintf(f, "keepalive = %d\n", s->keepalive);
        if (s->log[0])
            fprintf(f, "log = %s\n", s->log);
        if (s->colours)
            fprintf(f, "colours = %d\n", s->colours);
        if (s->font)
            fprintf(f, "font = %d\n", s->font);
        if (s->lines)
            fprintf(f, "lines = %d\n", s->lines);
    }
    bad = ferror(f);
    return fclose(f) != 0 || bad ? -1 : 0;
}

int session_same(const struct session *a, const struct session *b)
{
    return !strcmp(a->name, b->name) && !strcmp(a->host, b->host) && !strcmp(a->user, b->user) && a->port == b->port
           && !strcmp(a->key, b->key) && !strcmp(a->command, b->command) && !strcmp(a->tunnels, b->tunnels)
           && a->keepalive == b->keepalive && !strcmp(a->log, b->log) && a->colours == b->colours && a->font == b->font && a->lines == b->lines;
}

int sessions_find(const struct session *s, int n, const char *name)
{
    int i;
    for (i = 0; i < n; i++) {
        const char *a = s[i].name, *b = name;
        while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b))
            a++, b++;
        if (!*a && !*b)
            return i;
    }
    return -1;
}
