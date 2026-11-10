/* Kinds of file and what opens them (include/glos/assoc.h). No library but its own. */
#include <glos/assoc.h>

static int len(const char *s)
{
    int n = 0;
    while (s[n])
        n++;
    return n;
}

static char up(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

static int starts(const char *s, const char *with)     /* (letters in either case) */
{
    while (*with && up(*s) == up(*with))
        s++, with++;
    return !*with;
}

int assoc_link(const char *path) { return starts(path, "http://") || starts(path, "https://"); }

int assoc_kind(const char *path, char *out)
{
    int n = len(path), dot = -1, i;
    if (assoc_link(path)) {
        out[0] = '.', out[1] = 'U', out[2] = 'R', out[3] = 'L', out[4] = 0;
        return 0;
    }
    for (i = n - 1; i >= 0 && path[i] != '\\' && path[i] != '/' && path[i] != ':'; i--)
        if (path[i] == '.') {
            dot = i;
            break;
        }
    if (dot < 0) {                      /* (a bare extension without its dot: "txt") */
        if (n < 1 || n > 3)
            return -1;
        for (i = 0; i < n; i++)
            if (path[i] == '\\' || path[i] == '/' || path[i] == ':' || path[i] == ' ')
                return -1;
        dot = -1;
    }
    if (n - dot - 1 < 1 || n - dot - 1 > 3)
        return -1;
    out[0] = '.';
    for (i = dot + 1; i < n; i++)
        out[i - dot] = up(path[i]);
    out[n - dot] = 0;
    return 0;
}

int assoc_program(const char *k)
{
    return k[0] == '.' && ((k[1] == 'E' && k[2] == 'X' && k[3] == 'E') || (k[1] == 'C' && k[2] == 'O' && k[3] == 'M')
                           || (k[1] == 'B' && k[2] == 'A' && k[3] == 'T')) && !k[4];
}

/* The n-th verb's place: its label's start and length, its command's. 0 if there is none. */
static int verb_at(const char *v, int n, int *l0, int *ln, int *c0, int *cn)
{
    int at = 0, k = 0;
    while (v[at]) {
        int eq = at, end;
        while (v[eq] && v[eq] != '=' && v[eq] != ';')
            eq++;
        end = eq;
        while (v[end] && v[end] != ';')
            end++;
        if (v[eq] == '=' && eq > at) {                  /* (a name and "=": anything else is skipped) */
            if (k++ == n) {
                *l0 = at, *ln = eq - at, *c0 = eq + 1, *cn = end - eq - 1;
                return 1;
            }
        }
        at = v[end] ? end + 1 : end;
    }
    return 0;
}

int assoc_verb_count(const char *verbs)
{
    int n = 0, a, b, c, d;
    while (verb_at(verbs, n, &a, &b, &c, &d))
        n++;
    return n;
}

int assoc_verb_get(const char *verbs, int n, char *label, int label_room, char *command, int command_room)
{
    int l0, ln, c0, cn, i;
    if (n < 0 || !verb_at(verbs, n, &l0, &ln, &c0, &cn) || ln >= label_room || cn >= command_room)
        return -1;
    for (i = 0; i < ln; i++)
        label[i] = verbs[l0 + i];
    label[ln] = 0;
    for (i = 0; i < cn; i++)
        command[i] = verbs[c0 + i];
    command[cn] = 0;
    return 0;
}

static int same_n(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; i++)
        if (up(a[i]) != up(b[i]))
            return 0;
    return 1;
}

int assoc_verb_set(const char *verbs, const char *label, const char *command, char *out)
{
    int n = 0, used = 0, l0, ln, c0, cn, i, ll = len(label), found = 0;
    if (ll < 1 || ll >= ASSOC_LABEL)
        return -1;
    for (i = 0; i < ll; i++)
        if (label[i] == ';' || label[i] == '=' || (unsigned char)label[i] < ' ')
            return -1;
    if (command) {
        for (i = 0; command[i]; i++)
            if (command[i] == ';' || (unsigned char)command[i] < ' ')
                return -1;
    }
    while (verb_at(verbs, n++, &l0, &ln, &c0, &cn)) {
        int mine = ln == ll && same_n(verbs + l0, label, ll);
        const char *cmd = mine ? command : verbs + c0;
        int cl = mine ? (command ? len(command) : 0) : cn;
        found |= mine;
        if (mine && !command)
            continue;                   /* (taken out) */
        if (used + (used ? 1 : 0) + ln + 1 + cl >= ASSOC_VERBS)
            return -1;
        if (used)
            out[used++] = ';';
        for (i = 0; i < ln; i++)
            out[used++] = mine ? label[i] : verbs[l0 + i];
        out[used++] = '=';
        for (i = 0; i < cl; i++)
            out[used++] = cmd[i];
    }
    if (!found) {
        int cl;
        if (!command)
            return -1;
        cl = len(command);
        if (used + (used ? 1 : 0) + ll + 1 + cl >= ASSOC_VERBS)
            return -1;
        if (used)
            out[used++] = ';';
        for (i = 0; i < ll; i++)
            out[used++] = label[i];
        out[used++] = '=';
        for (i = 0; i < cl; i++)
            out[used++] = command[i];
    }
    out[used] = 0;
    return 0;
}

int assoc_verb_name(const char *kind, const char *label, char *out)
{
    int kl = len(kind), ll = len(label), i;
    if (kind[0] != '.' || kl < 2 || kl > 4 || ll < 1 || kl + 1 + ll >= ASSOC_LABEL)
        return -1;
    for (i = 0; i < ll; i++)
        if (label[i] == ':' || label[i] == ';' || label[i] == '=' || (unsigned char)label[i] < ' ')
            return -1;
    for (i = 0; i < kl; i++)
        out[i] = up(kind[i]);
    out[kl] = ':';
    for (i = 0; i <= ll; i++)
        out[kl + 1 + i] = label[i];
    return 0;
}

int assoc_command(const char *program, const char *path, const char *home, char *out, int room)
{
    int n = 0, i, bare = 1, pl = 0;
    while (program[pl] && program[pl] != ' ')           /* (the program's own name ends at its first space) */
        pl++;
    for (i = 0; i < pl; i++)
        if (program[i] == '\\' || program[i] == ':' || program[i] == '/')
            bare = 0;
    if (!pl)
        return -1;
    if (bare && home && home[0]) {
        int hl = len(home);
        if (n + hl + 1 >= room)
            return -1;
        for (i = 0; i < hl; i++)
            out[n++] = home[i];
        if (out[n - 1] != '\\')
            out[n++] = '\\';
    }
    if (n + len(program) + 1 + len(path) >= room)
        return -1;
    for (i = 0; program[i]; i++)
        out[n++] = program[i];
    if (path[0]) {
        out[n++] = ' ';
        for (i = 0; path[i]; i++)
            out[n++] = path[i];
    }
    out[n] = 0;
    return n;
}
