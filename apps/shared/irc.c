/* IRC (irc.h): a connection's state, fed the server's bytes.
 *
 * A line is "[@tags ][:prefix ]COMMAND[ param ...][ :the last parameter, which may have spaces]", ended CR LF, 512
 * bytes at most without its tags (which may be 8191 more). The user is announced by CAP LS, NICK and USER; the
 * server's capabilities come as CAP LS lines, the wanted ones are asked for (CAP REQ), SASL PLAIN is done where
 * the user has an account and the server has SASL, CAP END ends it, and 001 says the user is on. From then the
 * server tells what happens: who joins, leaves and speaks, the channels' names lists and topics, and numbers
 * for everything else.
 *
 * Whatever the server sends: every index is checked, every string cut to its room, every list has a limit. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irc.h"

#define MAX_BUFFERS 64
#define MAX_USERS 8000

struct irc {
    struct irc_opts o;
    char *want, *alt, *user, *real, *account, *password, *server_password;
    char nick[40], network[40];
    int state, tries, sasl;             /* sasl: 0 not begun, 1 under way, 2 over (or CAP's end said without it) */
    int sasl_tried;                     /* the account was given to the server by SASL (it took it or not) */
    char in[4608];                      /* the line being received */
    unsigned nin;
    int skipping;                       /* ... was too long: passed over to its end */
    struct irc_buffer *buf[MAX_BUFFERS];
    unsigned nbuf;
    char modes[10], marks[10];          /* PREFIX=(ov)@+: the modes that give a rank, and their marks */
    char chantypes[10];
    char always[24], when_set[24];      /* CHANMODES: modes that have a parameter always, and only when set */
    int ascii;                          /* CASEMAPPING=ascii (else rfc1459's: {}|^ are []\~ in small) */
    unsigned long spoke;
    char caps[200];                     /* the server's capabilities that are wanted, as they come */
};

static struct irc *sorting;

/* ---- strings */

static void put(char *to, unsigned room, const char *from)
{
    unsigned n = from ? (unsigned)strlen(from) : 0;
    n = n < room - 1 ? n : room - 1;
    if (n)
        memcpy(to, from, n);
    to[n] = 0;
}

static char *copy_of(const char *s)
{
    size_t n = strlen(s ? s : "") + 1;
    char *c = malloc(n);
    if (c)
        memcpy(c, s ? s : "", n);
    return c;
}

static int fold(const struct irc *c, int ch)
{
    ch &= 255;
    if (ch >= 'A' && ch <= 'Z')
        return ch + 32;
    if (c && !c->ascii && (ch == '[' || ch == ']' || ch == '\\' || ch == '~'))
        return ch == '~' ? '^' : ch + 32;
    return ch;
}

int irc_same(const struct irc *c, const char *a, const char *b)
{
    for (; *a && fold(c, *a) == fold(c, *b); a++, b++)
        ;
    return !*a && !*b;
}

static void out(struct irc *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void out(struct irc *c, const char *fmt, ...)
{
    char line[514];
    va_list ap;
    int n;
    unsigned i;
    va_start(ap, fmt);
    n = vsnprintf(line, 511, fmt, ap);
    va_end(ap);
    n = n < 0 ? 0 : n > 510 ? 510 : n;
    for (i = 0; i < (unsigned)n; i++)           /* (a line is one line, whatever was typed into it) */
        if (line[i] == '\r' || line[i] == '\n' || !line[i])
            line[i] = ' ';
    line[n] = '\r', line[n + 1] = '\n';
    if (c->o.send)
        c->o.send(c->o.ctx, line, (unsigned)n + 2);
}

/* ---- a line taken apart */

int irc_parse(char *line, struct irc_msg *m)
{
    char *p = line, *bang;
    memset(m, 0, sizeof *m);
    if (*p == '@') {
        m->tags = p + 1;
        if (!(p = strchr(p, ' ')))
            return -1;
        *p++ = 0;
        while (*p == ' ')
            p++;
    }
    if (*p == ':') {
        m->prefix = p + 1;
        if (!(p = strchr(p, ' ')))
            return -1;
        *p++ = 0;
        while (*p == ' ')
            p++;
        put(m->nick, sizeof m->nick, m->prefix);
        if ((bang = strpbrk(m->nick, "!@")) != NULL)
            *bang = 0;
    }
    if (!*p)
        return -1;
    m->command = p;
    while (*p && *p != ' ')
        p++;
    while (*p) {
        *p++ = 0;
        while (*p == ' ')
            p++;
        if (!*p)
            break;
        if (*p == ':' || m->nparams == 15) {    /* the last: all that is left */
            m->param[m->nparams++] = *p == ':' ? p + 1 : p;
            break;
        }
        m->param[m->nparams++] = p;
        while (*p && *p != ' ')
            p++;
    }
    return 0;
}

int irc_tag(const struct irc_msg *m, const char *key, char *outp, int room)
{
    const char *p = m->tags;
    size_t kl = strlen(key);
    int n = 0;
    while (p && *p) {
        const char *end = p + strcspn(p, ";");
        if (!strncmp(p, key, kl) && (p[kl] == '=' || p + kl == end)) {
            for (p += kl + (p[kl] == '='); p < end && n < room - 1; p++) {
                if (*p == '\\' && p + 1 < end) {        /* \: is ;  \s a space  \\ itself  \r \n */
                    p++;
                    outp[n++] = *p == ':' ? ';' : *p == 's' ? ' ' : *p == 'r' ? '\r' : *p == 'n' ? '\n' : *p;
                } else if (*p != '\\') {
                    outp[n++] = *p;
                }
            }
            outp[n] = 0;
            return 1;
        }
        p = *end ? end + 1 : end;
    }
    return 0;
}

/* "2026-11-21T01:23:45.678Z" as seconds since 1970; 0: not such a time. */
static unsigned long stamp(const char *t)
{
    unsigned y, mo, d, h, mi, s;
    long era, yoe, doy, doe;
    if (sscanf(t, "%4u-%2u-%2uT%2u:%2u:%2u", &y, &mo, &d, &h, &mi, &s) != 6 || y < 1970 || y > 2105 || mo < 1 || mo > 12 || d < 1 || d > 31
        || h > 23 || mi > 59 || s > 60)
        return 0;
    y -= mo <= 2;                               /* (days from the calendar, by the 400-year cycle) */
    era = (long)y / 400, yoe = (long)y - era * 400;
    doy = (153 * ((long)mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (long)d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (unsigned long)(era * 146097 + doe - 719468) * 86400ul + h * 3600ul + mi * 60ul + s;
}

/* ---- text with mIRC's codes */

int irc_format(const char *text, char *plain, int room, struct irc_span *spans, int max)
{
    const unsigned char *p = (const unsigned char *)text;
    unsigned flags = 0, n = 0, start = 0;
    int fg = -1, bg = -1, count = 0;
    struct irc_span look = { 0, 0, 0, -1, -1 };
    for (;; p++) {
        unsigned f = flags;
        int nf = fg, nb = bg, code = 1, digits, v;
        switch (*p) {
        case 0x02: f ^= IRC_BOLD; break;
        case 0x1D: f ^= IRC_ITALIC; break;
        case 0x1F: f ^= IRC_UNDERLINE; break;
        case 0x16: f ^= IRC_REVERSE; break;
        case 0x1E: f ^= IRC_STRIKE; break;
        case 0x11: f ^= IRC_MONO; break;
        case 0x0F: f = 0, nf = nb = -1; break;
        case 0x03:                              /* a colour: one or two digits, and after a comma the ground's */
            nf = nb = -1;
            for (digits = 0, v = 0; digits < 2 && p[1] >= '0' && p[1] <= '9'; digits++, p++)
                v = v * 10 + (p[1] - '0');
            if (digits) {
                nf = v < 16 ? v : -1, nb = bg;
                if (p[1] == ',' && p[2] >= '0' && p[2] <= '9') {
                    for (p++, digits = 0, v = 0; digits < 2 && p[1] >= '0' && p[1] <= '9'; digits++, p++)
                        v = v * 10 + (p[1] - '0');
                    nb = v < 16 ? v : -1;
                }
            }
            break;
        case 0x04:                              /* a colour by its red, green and blue: passed over */
            for (digits = 0; digits < 6 && ((p[1] >= '0' && p[1] <= '9') || ((p[1] | 32) >= 'a' && (p[1] | 32) <= 'f')); digits++, p++)
                ;
            if (digits == 6 && p[1] == ',')
                for (p++, digits = 0; digits < 6 && ((p[1] >= '0' && p[1] <= '9') || ((p[1] | 32) >= 'a' && (p[1] | 32) <= 'f'));
                     digits++, p++)
                    ;
            break;
        default:
            code = 0;
        }
        if (code || !*p) {                      /* a look ends here: its run, if it has letters */
            if (n > start && spans) {
                if (count < max) {
                    look.start = start, look.len = n - start;
                    spans[count++] = look;
                } else if (count) {
                    spans[count - 1].len = n - spans[count - 1].start;
                }
            }
            start = n;
            flags = f, fg = nf, bg = nb;
            look.flags = (unsigned char)flags, look.fg = (signed char)fg, look.bg = (signed char)bg;
            if (!*p)
                break;
            continue;
        }
        if ((int)n < room - 1 && *p >= 0x20)
            plain[n++] = (char)*p;
    }
    if (room > 0)
        plain[n] = 0;
    return count;
}

/* ---- buffers, lines, users */

const struct irc_line *irc_line_at(const struct irc_buffer *b, unsigned i)
{
    return i < b->nlines ? &b->line[(b->first + i) % IRC_LINES] : NULL;
}

static void tell(struct irc *c, int what, struct irc_buffer *b)
{
    if (c->o.event)
        c->o.event(c->o.ctx, what, b);
}

static struct irc_buffer *buffer_of(struct irc *c, const char *name, int kind, int make)
{
    struct irc_buffer *b;
    unsigned i;
    for (i = 1; i < c->nbuf; i++)
        if (irc_same(c, c->buf[i]->name, name))
            return c->buf[i];
    if (!make || c->nbuf == MAX_BUFFERS || !name[0] || !(b = calloc(1, sizeof *b)))
        return NULL;
    put(b->name, sizeof b->name, name);
    b->kind = kind;
    c->buf[c->nbuf++] = b;
    tell(c, IRC_E_BUFFER, b);
    return b;
}

static int names_me(const struct irc *c, const char *text)     /* this user's nick is in it, as a word */
{
    size_t n = strlen(c->nick), i, k;
    for (i = 0; n && text[i]; i++) {
        int before = i ? (unsigned char)text[i - 1] : ' ';
        if ((before >= '0' && before <= '9') || ((before | 32) >= 'a' && (before | 32) <= 'z') || before >= 128)
            continue;
        for (k = 0; k < n && text[i + k] && fold(c, text[i + k]) == fold(c, c->nick[k]); k++)
            ;
        if (k == n) {
            int after = (unsigned char)text[i + n];
            if (!((after >= '0' && after <= '9') || ((after | 32) >= 'a' && (after | 32) <= 'z') || after >= 128))
                return 1;
        }
    }
    return 0;
}

static void say(struct irc *c, struct irc_buffer *b, int kind, const char *nick, const char *text, unsigned long when, int mine)
{
    struct irc_line *l;
    if (!b)
        return;
    if (b->nlines == IRC_LINES) {               /* the oldest gives way */
        free(b->line[b->first].text);
        b->first = (b->first + 1) % IRC_LINES, b->nlines--;
    }
    l = &b->line[(b->first + b->nlines) % IRC_LINES];
    memset(l, 0, sizeof *l);
    if (!(l->text = copy_of(text)))
        return;
    l->when = when ? when : c->o.now ? c->o.now(c->o.ctx) : 0;
    l->kind = (unsigned char)kind, l->mine = (unsigned char)(mine != 0);
    put(l->nick, sizeof l->nick, nick);
    if (!mine && (kind == IRC_L_MSG || kind == IRC_L_ACTION || kind == IRC_L_NOTICE)) {
        l->highlight = (unsigned char)((b->kind == IRC_B_PRIVATE && kind != IRC_L_NOTICE) || names_me(c, text));
        b->unread++;
        b->highlights += l->highlight;
    }
    b->nlines++, b->total++;
    tell(c, IRC_E_LINE, b);
}

static void sayf(struct irc *c, struct irc_buffer *b, int kind, const char *nick, const char *fmt, ...)
    __attribute__((format(printf, 5, 6)));
static void sayf(struct irc *c, struct irc_buffer *b, int kind, const char *nick, const char *fmt, ...)
{
    char t[600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    say(c, b, kind, nick, t, 0, 0);
}

static struct irc_user *user_of(struct irc *c, struct irc_buffer *b, const char *nick)
{
    unsigned i;
    for (i = 0; i < b->nusers; i++)
        if (irc_same(c, b->users[i].nick, nick))
            return &b->users[i];
    return NULL;
}

static int rank_of(const struct irc *c, const struct irc_user *u)       /* the lower, the higher its rank */
{
    const char *at = u->rank[0] ? strchr(c->marks, u->rank[0]) : NULL;
    return at ? (int)(at - c->marks) : 99;
}

static int by_rank(const void *a, const void *b)
{
    const struct irc_user *x = a, *y = b;
    int rx = rank_of(sorting, x), ry = rank_of(sorting, y), i;
    if (rx != ry)
        return rx - ry;
    for (i = 0; x->nick[i] && fold(sorting, x->nick[i]) == fold(sorting, y->nick[i]); i++)
        ;
    return fold(sorting, x->nick[i]) - fold(sorting, y->nick[i]);
}

static void sort_users(struct irc *c, struct irc_buffer *b)
{
    sorting = c;
    if (b->nusers > 1)
        qsort(b->users, b->nusers, sizeof b->users[0], by_rank);
}

static struct irc_user *user_add(struct irc *c, struct irc_buffer *b, const char *nick, const char *rank)
{
    struct irc_user *u = user_of(c, b, nick);
    if (!u) {
        if (b->nusers == b->room) {
            unsigned room = b->room ? b->room * 2 : 32;
            if (room > MAX_USERS || !(u = realloc(b->users, room * sizeof *u)))
                return NULL;
            b->users = u, b->room = room;
        }
        u = &b->users[b->nusers++];
        memset(u, 0, sizeof *u);
        put(u->nick, sizeof u->nick, nick);
    }
    put(u->rank, sizeof u->rank, rank);
    return u;
}

static int user_drop(struct irc *c, struct irc_buffer *b, const char *nick)
{
    struct irc_user *u = user_of(c, b, nick);
    if (!u)
        return 0;
    memmove(u, u + 1, (size_t)(b->users + b->nusers - (u + 1)) * sizeof *u);
    b->nusers--;
    return 1;
}

static int is_channel(const struct irc *c, const char *name) { return name[0] && strchr(c->chantypes, name[0]) != NULL; }

/* ---- the connection */

struct irc *irc_new(const struct irc_opts *o)
{
    struct irc *c = calloc(1, sizeof *c);
    if (!c)
        return NULL;
    c->o = *o;
    c->want = copy_of(o->nick), c->alt = copy_of(o->alt), c->user = copy_of(o->user), c->real = copy_of(o->real);
    c->account = copy_of(o->account), c->password = copy_of(o->password), c->server_password = copy_of(o->server_password);
    c->buf[0] = calloc(1, sizeof *c->buf[0]);
    if (!c->want || !c->alt || !c->user || !c->real || !c->account || !c->password || !c->server_password || !c->buf[0]) {
        irc_free(c);
        return NULL;
    }
    c->nbuf = 1;
    put(c->buf[0]->name, sizeof c->buf[0]->name, "Server");
    put(c->modes, sizeof c->modes, "ov"), put(c->marks, sizeof c->marks, "@+");
    put(c->chantypes, sizeof c->chantypes, "#&");
    put(c->always, sizeof c->always, "bk"), put(c->when_set, sizeof c->when_set, "l");
    put(c->nick, sizeof c->nick, c->want);
    return c;
}

static void buffer_free(struct irc_buffer *b)
{
    unsigned i;
    if (!b)
        return;
    for (i = 0; i < IRC_LINES; i++)
        free(b->line[i].text);
    free(b->users);
    free(b);
}

void irc_free(struct irc *c)
{
    unsigned i;
    if (!c)
        return;
    for (i = 0; i < MAX_BUFFERS; i++)
        buffer_free(c->buf[i]);
    free(c->want), free(c->alt), free(c->user), free(c->real), free(c->account), free(c->password), free(c->server_password);
    free(c);
}

int irc_state(const struct irc *c) { return c->state; }
const char *irc_nick(const struct irc *c) { return c->nick; }
const char *irc_network(const struct irc *c) { return c->network; }
unsigned irc_buffers(const struct irc *c) { return c->nbuf; }
struct irc_buffer *irc_buffer(struct irc *c, unsigned i) { return i < c->nbuf ? c->buf[i] : NULL; }
struct irc_buffer *irc_find(struct irc *c, const char *name) { return buffer_of(c, name, 0, 0); }
void irc_read(struct irc_buffer *b) { b->unread = b->highlights = 0; }

void irc_start(struct irc *c)
{
    c->state = IRC_REGISTERING, c->tries = 0, c->sasl = 0, c->sasl_tried = 0, c->nin = 0, c->skipping = 0, c->caps[0] = 0;
    put(c->nick, sizeof c->nick, c->want);
    tell(c, IRC_E_STATE, c->buf[0]);
    out(c, "CAP LS 302");
    if (c->server_password[0])
        out(c, "PASS %s", c->server_password);
    out(c, "NICK %s", c->nick);
    out(c, "USER %s 0 * :%s", c->user[0] ? c->user : c->nick, c->real[0] ? c->real : c->nick);
}

void irc_stopped(struct irc *c)
{
    unsigned i;
    if (c->state == IRC_OFF)
        return;
    c->state = IRC_OFF;
    for (i = 0; i < c->nbuf; i++) {
        if (c->buf[i]->kind == IRC_B_CHANNEL && c->buf[i]->joined) {
            c->buf[i]->joined = 0, c->buf[i]->nusers = 0;
            tell(c, IRC_E_USERS, c->buf[i]);
        }
        say(c, c->buf[i], IRC_L_ERROR, "", "The connection has ended.", 0, 0);
    }
    tell(c, IRC_E_STATE, c->buf[0]);
}

void irc_close(struct irc *c, struct irc_buffer *b)
{
    unsigned i;
    for (i = 1; i < c->nbuf; i++)
        if (c->buf[i] == b) {
            if (b->kind == IRC_B_CHANNEL && b->joined && c->state == IRC_ON)
                out(c, "PART %s", b->name);
            tell(c, IRC_E_CLOSED, b);
            memmove(&c->buf[i], &c->buf[i + 1], (c->nbuf - i - 1) * sizeof c->buf[0]);
            c->buf[--c->nbuf] = NULL;
            buffer_free(b);
            return;
        }
}

/* ---- what the server says */

static void base64(const unsigned char *in, unsigned n, char *outp)
{
    static const char abc[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned i, o = 0;
    for (i = 0; i < n; i += 3) {
        unsigned long v = (unsigned long)in[i] << 16 | (i + 1 < n ? (unsigned long)in[i + 1] << 8 : 0) | (i + 2 < n ? in[i + 2] : 0);
        outp[o++] = abc[v >> 18 & 63], outp[o++] = abc[v >> 12 & 63];
        outp[o++] = i + 1 < n ? abc[v >> 6 & 63] : '=', outp[o++] = i + 2 < n ? abc[v & 63] : '=';
    }
    outp[o] = 0;
}

static void joined_params(const struct irc_msg *m, int from, char *outp, unsigned room)        /* the parameters from one on, as a line */
{
    unsigned n = 0;
    int i;
    outp[0] = 0;
    for (i = from; i < m->nparams; i++)
        n += (unsigned)snprintf(outp + n, n < room ? room - n : 0, "%s%s", i > from ? " " : "", m->param[i]), n = n < room ? n : room - 1;
}

static void support(struct irc *c, const char *token)  /* a word of 005's: NAME=value */
{
    const char *v = strchr(token, '=');
    if (!v)
        return;
    v++;
    if (!strncmp(token, "PREFIX=", 7) && v[0] == '(') {         /* (modes)marks, of the same number */
        const char *close = strchr(v, ')');
        unsigned n = close ? (unsigned)(close - v - 1) : 0;
        if (n && n < sizeof c->modes && strlen(close + 1) == n) {
            memcpy(c->modes, v + 1, n), c->modes[n] = 0;
            memcpy(c->marks, close + 1, n), c->marks[n] = 0;
        }
    } else if (!strncmp(token, "CHANTYPES=", 10) && v[0]) {
        put(c->chantypes, sizeof c->chantypes, v);
    } else if (!strncmp(token, "CASEMAPPING=", 12)) {
        c->ascii = !strcmp(v, "ascii");
    } else if (!strncmp(token, "NETWORK=", 8)) {
        put(c->network, sizeof c->network, v);
        tell(c, IRC_E_TOPIC, c->buf[0]);                /* (the server's buffer has its name now) */
    } else if (!strncmp(token, "CHANMODES=", 10)) {             /* A,B,C,D: lists, always a parameter, when set, never */
        const char *b = strchr(v, ','), *cc = b ? strchr(b + 1, ',') : NULL, *d = cc ? strchr(cc + 1, ',') : NULL;
        if (b && cc && d) {
            char t[24];
            snprintf(t, sizeof t, "%.*s%.*s", (int)(b - v > 11 ? 11 : b - v), v, (int)(cc - b - 1 > 11 ? 11 : cc - b - 1), b + 1);
            put(c->always, sizeof c->always, t);
            snprintf(t, sizeof t, "%.*s", (int)(d - cc - 1 > 22 ? 22 : d - cc - 1), cc + 1);
            put(c->when_set, sizeof c->when_set, t);
        }
    }
}

static void rank_change(struct irc *c, struct irc_user *u, char mark, int on)
{
    char had[6], now[6];
    unsigned i, n = 0;
    put(had, sizeof had, u->rank);
    for (i = 0; c->marks[i] && n < 5; i++)      /* (kept in the server's order of ranks) */
        if (c->marks[i] == mark ? on : strchr(had, c->marks[i]) != NULL)
            now[n++] = c->marks[i];
    now[n] = 0;
    put(u->rank, sizeof u->rank, now);
}

static void channel_modes(struct irc *c, struct irc_buffer *b, const struct irc_msg *m)
{
    const char *p;
    int on = 1, arg = 2, changed = 0;
    for (p = m->param[1]; *p; p++) {
        const char *rank = strchr(c->modes, *p);
        if (*p == '+' || *p == '-') {
            on = *p == '+';
        } else if (rank) {
            struct irc_user *u = arg < m->nparams ? user_of(c, b, m->param[arg]) : NULL;
            if (u)
                rank_change(c, u, c->marks[rank - c->modes], on), changed = 1;
            arg++;
        } else if (strchr(c->always, *p) || (on && strchr(c->when_set, *p))) {
            arg++;
        }
    }
    if (changed) {
        sort_users(c, b);
        tell(c, IRC_E_USERS, b);
    }
}

static void caps_done(struct irc *c)
{
    if (c->sasl != 2) {
        c->sasl = 2;
        out(c, "CAP END");
    }
}

static void handle(struct irc *c, struct irc_msg *m)
{
    struct irc_buffer *b, *server = c->buf[0];
    char t[600], tag[64];
    unsigned long when = irc_tag(m, "time", tag, sizeof tag) ? stamp(tag) : 0;
    const char *cmd = m->command, *p0 = m->nparams > 0 ? m->param[0] : "", *p1 = m->nparams > 1 ? m->param[1] : "";
    const char *last = m->nparams ? m->param[m->nparams - 1] : "";
    int me = m->nick[0] && irc_same(c, m->nick, c->nick), number = 0;
    unsigned i;
    if (cmd[0] >= '0' && cmd[0] <= '9' && cmd[1] >= '0' && cmd[1] <= '9' && cmd[2] >= '0' && cmd[2] <= '9' && !cmd[3])
        number = atoi(cmd);

    if (!strcmp(cmd, "PING")) {
        out(c, "PONG :%s", last);
    } else if (!strcmp(cmd, "CAP") && m->nparams >= 3) {
        const char *sub = m->param[1];
        if (!strcmp(sub, "LS")) {                       /* its capabilities (more lines to come if a * is before them) */
            static const char *const wanted[] = { "server-time", "multi-prefix", "away-notify", "sasl" };
            char list[400], *w;
            put(list, sizeof list, last);
            for (w = strtok(list, " "); w; w = strtok(NULL, " ")) {
                char *eq = strchr(w, '=');
                if (eq)
                    *eq = 0;
                for (i = 0; i < 4; i++)
                    if (!strcmp(w, wanted[i]) && (i < 3 || (c->account[0] && c->password[0])) && strlen(c->caps) + strlen(w) + 2 < sizeof c->caps)
                        strcat(c->caps, c->caps[0] ? " " : ""), strcat(c->caps, w);
            }
            if (!(m->nparams >= 4 && !strcmp(m->param[2], "*"))) {
                if (c->caps[0])
                    out(c, "CAP REQ :%s", c->caps);
                else
                    caps_done(c);
            }
        } else if (!strcmp(sub, "ACK")) {
            put(t, sizeof t, last);
            if (c->state == IRC_REGISTERING && c->sasl == 0 && strstr(t, "sasl") && c->account[0] && c->password[0]) {
                c->sasl = 1, c->sasl_tried = 1;
                out(c, "AUTHENTICATE PLAIN");
            } else if (c->state == IRC_REGISTERING) {
                caps_done(c);
            }
        } else if (!strcmp(sub, "NAK") && c->state == IRC_REGISTERING) {
            caps_done(c);
        }
    } else if (!strcmp(cmd, "AUTHENTICATE") && c->sasl == 1) {         /* its "+": the name, the name again, the password */
        unsigned char plain[300];
        char coded[412];
        unsigned al = (unsigned)strlen(c->account), pl = (unsigned)strlen(c->password);
        if (2 * al + pl + 2 <= sizeof plain) {
            memcpy(plain, c->account, al + 1), memcpy(plain + al + 1, c->account, al + 1), memcpy(plain + 2 * al + 2, c->password, pl);
            base64(plain, 2 * al + 2 + pl, coded);
            out(c, "AUTHENTICATE %s", coded);
            if (strlen(coded) == 400)
                out(c, "AUTHENTICATE +");
        } else {
            out(c, "AUTHENTICATE *");
        }
    } else if (number == 903 || number == 904 || number == 905 || number == 906 || number == 908) {
        if (number != 903 && number != 908)
            sayf(c, server, IRC_L_ERROR, "", "The account's name or password was refused: %s", last);
        if (number != 908)
            caps_done(c);
    } else if (number == 1) {                           /* on */
        put(c->nick, sizeof c->nick, p0);
        c->state = IRC_ON;
        say(c, server, IRC_L_INFO, "", last, when, 0);
        tell(c, IRC_E_NICK, server);
        tell(c, IRC_E_STATE, server);
        if (c->password[0] && !c->sasl_tried)          /* (the server has no SASL: the password to the nick's keeper) */
            out(c, "PRIVMSG NickServ :IDENTIFY %s%s%s", c->account, c->account[0] ? " " : "", c->password);
    } else if (number == 5) {
        for (i = 1; (int)i + 1 < m->nparams; i++)
            support(c, m->param[i]);
    } else if ((number == 433 || number == 432 || number == 436 || number == 437) && c->state == IRC_REGISTERING) {
        char next[40];                                  /* the nick is taken: the other, then the nick with _ after it */
        c->tries++;
        if (c->tries == 1 && c->alt[0] && !irc_same(c, c->alt, c->nick))
            put(next, sizeof next, c->alt);
        else
            snprintf(next, sizeof next, "%.30s%.*s", c->want, c->tries > 6 ? 6 : c->tries, "______");
        if (c->tries > 8) {
            say(c, server, IRC_L_ERROR, "", "No nick could be had: the connection is given up.", 0, 0);
            out(c, "QUIT");
        } else {
            put(c->nick, sizeof c->nick, next);
            out(c, "NICK %s", next);
        }
    } else if (number == 332 && m->nparams >= 3) {      /* a channel's topic, as it is joined */
        if ((b = buffer_of(c, p1, IRC_B_CHANNEL, 0)) != NULL) {
            put(b->topic, sizeof b->topic, last);
            tell(c, IRC_E_TOPIC, b);
        }
    } else if (number == 331 || number == 333 || number == 366 || number == 329 || number == 324) {
        if (number == 366 && (b = buffer_of(c, p1, IRC_B_CHANNEL, 0)) != NULL) {        /* its names are all here */
            sort_users(c, b);
            tell(c, IRC_E_USERS, b);
        }
    } else if (number == 353 && m->nparams >= 4) {      /* names: each with its ranks' marks before it */
        if ((b = buffer_of(c, m->param[2], IRC_B_CHANNEL, 0)) != NULL) {
            char list[512], *w;
            put(list, sizeof list, last);
            for (w = strtok(list, " "); w; w = strtok(NULL, " ")) {
                char rank[6], *bang;
                unsigned n = 0;
                while (*w && strchr(c->marks, *w) && n < 5)
                    rank[n++] = *w++;
                rank[n] = 0;
                if ((bang = strchr(w, '!')) != NULL)
                    *bang = 0;
                if (*w)
                    user_add(c, b, w, rank);
            }
        }
    } else if (number == 301 && m->nparams >= 3) {
        b = buffer_of(c, p1, IRC_B_PRIVATE, 0);
        sayf(c, b ? b : server, IRC_L_INFO, "", "%s is away: %s", p1, last);
    } else if (number) {                                /* every other number: its words; an error's where it belongs */
        joined_params(m, 1, t, sizeof t);
        b = number >= 400 && number < 600 && m->nparams >= 3 ? buffer_of(c, p1, 0, 0) : NULL;
        say(c, b ? b : server, number >= 400 && number < 600 ? IRC_L_ERROR : IRC_L_INFO, "", t, when, 0);
    } else if (!strcmp(cmd, "JOIN") && m->nparams >= 1) {
        if (me) {
            if ((b = buffer_of(c, p0, IRC_B_CHANNEL, 1)) != NULL) {
                b->kind = IRC_B_CHANNEL, b->joined = 1, b->nusers = 0, b->topic[0] = 0;
                say(c, b, IRC_L_JOIN, c->nick, "", when, 1);
                tell(c, IRC_E_USERS, b);
            }
        } else if ((b = buffer_of(c, p0, IRC_B_CHANNEL, 0)) != NULL) {
            user_add(c, b, m->nick, "");
            sort_users(c, b);
            say(c, b, IRC_L_JOIN, m->nick, strchr(m->prefix, '!') ? strchr(m->prefix, '!') + 1 : "", when, 0);
            tell(c, IRC_E_USERS, b);
        }
    } else if (!strcmp(cmd, "PART") && m->nparams >= 1) {
        if ((b = buffer_of(c, p0, IRC_B_CHANNEL, 0)) != NULL) {
            if (me)
                b->joined = 0, b->nusers = 0;
            else
                user_drop(c, b, m->nick);
            say(c, b, IRC_L_PART, m->nick, m->nparams >= 2 ? last : "", when, me);
            tell(c, IRC_E_USERS, b);
        }
    } else if (!strcmp(cmd, "KICK") && m->nparams >= 2) {
        if ((b = buffer_of(c, p0, IRC_B_CHANNEL, 0)) != NULL) {
            if (irc_same(c, p1, c->nick)) {
                b->joined = 0, b->nusers = 0;
                sayf(c, b, IRC_L_ERROR, "", "%s put you out of %s%s%s", m->nick, b->name, m->nparams >= 3 ? ": " : "", m->nparams >= 3 ? last : "");
            } else {
                user_drop(c, b, p1);
                sayf(c, b, IRC_L_PART, p1, "put out by %s%s%s", m->nick, m->nparams >= 3 ? ": " : "", m->nparams >= 3 ? last : "");
            }
            tell(c, IRC_E_USERS, b);
        }
    } else if (!strcmp(cmd, "QUIT")) {
        for (i = 1; i < c->nbuf; i++) {
            b = c->buf[i];
            if (b->kind == IRC_B_CHANNEL ? user_drop(c, b, m->nick) : irc_same(c, b->name, m->nick)) {
                say(c, b, IRC_L_QUIT, m->nick, last, when, 0);
                if (b->kind == IRC_B_CHANNEL)
                    tell(c, IRC_E_USERS, b);
            }
        }
    } else if (!strcmp(cmd, "NICK") && m->nparams >= 1) {
        for (i = 1; i < c->nbuf; i++) {
            struct irc_user *u;
            b = c->buf[i];
            if (b->kind == IRC_B_CHANNEL && (u = user_of(c, b, m->nick)) != NULL) {
                put(u->nick, sizeof u->nick, p0);
                sort_users(c, b);
                say(c, b, IRC_L_NICK, m->nick, p0, when, me);
                tell(c, IRC_E_USERS, b);
            } else if (b->kind == IRC_B_PRIVATE && irc_same(c, b->name, m->nick) && !buffer_of(c, p0, 0, 0)) {
                put(b->name, sizeof b->name, p0);
                say(c, b, IRC_L_NICK, m->nick, p0, when, 0);
                tell(c, IRC_E_TOPIC, b);
            }
        }
        if (me) {
            put(c->nick, sizeof c->nick, p0);
            sayf(c, server, IRC_L_INFO, "", "You are now %s.", c->nick);
            tell(c, IRC_E_NICK, server);
        }
    } else if (!strcmp(cmd, "MODE") && m->nparams >= 2) {
        joined_params(m, 1, t, sizeof t);
        if (is_channel(c, p0) && (b = buffer_of(c, p0, IRC_B_CHANNEL, 0)) != NULL) {
            channel_modes(c, b, m);
            say(c, b, IRC_L_MODE, m->nick, t, when, me);
        } else if (!is_channel(c, p0)) {
            sayf(c, server, IRC_L_INFO, "", "Your modes: %s", t);
        }
    } else if (!strcmp(cmd, "TOPIC") && m->nparams >= 2) {
        if ((b = buffer_of(c, p0, IRC_B_CHANNEL, 0)) != NULL) {
            put(b->topic, sizeof b->topic, last);
            say(c, b, IRC_L_TOPIC, m->nick, last, when, me);
            tell(c, IRC_E_TOPIC, b);
        }
    } else if ((!strcmp(cmd, "PRIVMSG") || !strcmp(cmd, "NOTICE")) && m->nparams >= 2) {
        int notice = cmd[0] == 'N', kind = notice ? IRC_L_NOTICE : IRC_L_MSG;
        const char *to = p0, *text = last;
        struct irc_user *u;
        while (*to && strchr(c->marks, *to) && to[1] && is_channel(c, to + 1))  /* ("@#channel": to its operators) */
            to++;
        if (text[0] == 1) {                             /* CTCP: an action is shown; a few questions are answered */
            size_t n = strlen(text);
            put(t, sizeof t, text + 1);
            if (n >= 2 && t[n - 2] == 1)
                t[n - 2] = 0;
            if (!strncmp(t, "ACTION ", 7)) {
                kind = IRC_L_ACTION, text = t + 7;
            } else {
                if (!notice && !strcmp(t, "VERSION"))
                    out(c, "NOTICE %s :\001VERSION GLOS Chat\001", m->nick);
                else if (!notice && !strncmp(t, "PING", 4))
                    out(c, "NOTICE %s :\001%s\001", m->nick, t);
                return;
            }
        }
        if (is_channel(c, to)) {
            b = buffer_of(c, to, IRC_B_CHANNEL, 0);
            if (b && (u = user_of(c, b, m->nick)) != NULL)
                u->spoke = ++c->spoke;
        } else if (notice || !strchr(m->prefix ? m->prefix : "", '!')) {
            b = buffer_of(c, m->nick, IRC_B_PRIVATE, 0);        /* (the server's and the services' words: no tab for each) */
        } else {
            b = buffer_of(c, m->nick, IRC_B_PRIVATE, 1);
        }
        say(c, b ? b : server, kind, m->nick, text, when, 0);
    } else if (!strcmp(cmd, "AWAY")) {
        for (i = 1; i < c->nbuf; i++) {
            struct irc_user *u = c->buf[i]->kind == IRC_B_CHANNEL ? user_of(c, c->buf[i], m->nick) : NULL;
            if (u && u->away != (m->nparams > 0)) {
                u->away = m->nparams > 0;
                tell(c, IRC_E_USERS, c->buf[i]);
            }
        }
    } else if (!strcmp(cmd, "ERROR")) {
        say(c, server, IRC_L_ERROR, "", last, when, 0);
    }
}

void irc_feed(struct irc *c, const char *bytes, unsigned n)
{
    unsigned i;
    for (i = 0; i < n; i++) {
        char ch = bytes[i];
        if (ch == '\n') {
            if (!c->skipping) {
                struct irc_msg m;
                while (c->nin && (c->in[c->nin - 1] == '\r' || c->in[c->nin - 1] == ' '))
                    c->nin--;
                c->in[c->nin] = 0;
                if (c->nin && irc_parse(c->in, &m) == 0)
                    handle(c, &m);
            }
            c->nin = 0, c->skipping = 0;
        } else if (c->nin + 1 < sizeof c->in) {
            c->in[c->nin++] = ch ? ch : ' ';
        } else {
            c->skipping = 1;
        }
    }
}

/* ---- what the user types */

static const char *word(const char *s, char *outp, unsigned room)      /* its first word; what is after it */
{
    unsigned n = 0;
    while (*s == ' ')
        s++;
    for (; *s && *s != ' '; s++)
        if (n < room - 1)
            outp[n++] = *s;
    outp[n] = 0;
    while (*s == ' ')
        s++;
    return s;
}

static void speak(struct irc *c, struct irc_buffer *b, const char *target, const char *text, int kind)
{
    /* in pieces a line can carry (the server puts this user's own prefix before each): cut at a space where
       there is one, never within a character */
    size_t left = strlen(text);
    while (left) {
        size_t n = left > 380 ? 380 : left, k;
        char piece[400];
        if (n < left) {
            for (k = n; k > 300 && text[k] != ' '; k--)
                ;
            if (k > 300)
                n = k;
            else
                while (n > 1 && ((unsigned char)text[n] & 0xC0) == 0x80)
                    n--;
        }
        memcpy(piece, text, n), piece[n] = 0;
        if (kind == IRC_L_ACTION)
            out(c, "PRIVMSG %s :\001ACTION %s\001", target, piece);
        else
            out(c, "%s %s :%s", kind == IRC_L_NOTICE ? "NOTICE" : "PRIVMSG", target, piece);
        say(c, b, kind, c->nick, piece, 0, 1);
        text += n, left -= n;
        while (*text == ' ' && left)
            text++, left--;
    }
}

int irc_input(struct irc *c, struct irc_buffer *b, const char *text)
{
    char cmd[32], a[128];
    const char *rest;
    unsigned i;
    int here = b && b != c->buf[0];
    while (*text == ' ')
        text++;
    if (!*text)
        return 0;
    if (c->state == IRC_OFF)
        return -1;
    if (text[0] != '/' || text[1] == '/') {             /* said */
        if (!here || c->state != IRC_ON)
            return -1;
        speak(c, b, b->name, text[0] == '/' ? text + 1 : text, IRC_L_MSG);
        return 0;
    }
    rest = word(text + 1, cmd, sizeof cmd);
    for (i = 0; cmd[i]; i++)
        cmd[i] = (char)(cmd[i] >= 'a' && cmd[i] <= 'z' ? cmd[i] - 32 : cmd[i]);
    if (!strcmp(cmd, "ME")) {
        if (!here || !*rest)
            return -1;
        speak(c, b, b->name, rest, IRC_L_ACTION);
    } else if (!strcmp(cmd, "MSG") || !strcmp(cmd, "NOTICE") || !strcmp(cmd, "QUERY")) {
        struct irc_buffer *to;
        rest = word(rest, a, sizeof a);
        if (!a[0] || (cmd[0] != 'Q' && !*rest))
            return -1;
        to = buffer_of(c, a, IRC_B_PRIVATE, !is_channel(c, a) && cmd[0] != 'N');
        if (*rest)
            speak(c, to ? to : c->buf[0], a, rest, cmd[0] == 'N' ? IRC_L_NOTICE : IRC_L_MSG);
    } else if (!strcmp(cmd, "JOIN")) {
        if (!*rest)
            return -1;
        out(c, "JOIN %s", rest);
    } else if (!strcmp(cmd, "PART") || !strcmp(cmd, "LEAVE")) {
        const char *after = word(rest, a, sizeof a);
        if (is_channel(c, a))
            out(c, "PART %s%s%s", a, *after ? " :" : "", after);
        else if (here && b->kind == IRC_B_CHANNEL)
            out(c, "PART %s%s%s", b->name, *rest ? " :" : "", rest);
        else
            return -1;
    } else if (!strcmp(cmd, "TOPIC")) {
        if (!here || b->kind != IRC_B_CHANNEL)
            return -1;
        out(c, "TOPIC %s%s%s", b->name, *rest ? " :" : "", rest);
    } else if (!strcmp(cmd, "NICK")) {
        if (!*rest)
            return -1;
        out(c, "NICK %s", rest);
    } else if (!strcmp(cmd, "AWAY")) {
        out(c, "AWAY%s%s", *rest ? " :" : "", rest);
    } else if (!strcmp(cmd, "QUIT")) {
        out(c, "QUIT :%s", *rest ? rest : "GLOS Chat");
    } else if (!strcmp(cmd, "KICK")) {
        rest = word(rest, a, sizeof a);
        if (!here || b->kind != IRC_B_CHANNEL || !a[0])
            return -1;
        out(c, "KICK %s %s%s%s", b->name, a, *rest ? " :" : "", rest);
    } else if (!strcmp(cmd, "MODE")) {
        if (here && b->kind == IRC_B_CHANNEL && (*rest == '+' || *rest == '-' || !*rest))
            out(c, "MODE %s%s%s", b->name, *rest ? " " : "", rest);
        else
            out(c, "MODE %s", rest);
    } else if (!strcmp(cmd, "NAMES")) {
        out(c, "NAMES %s", *rest ? rest : here ? b->name : "");
    } else if (!strcmp(cmd, "RAW") || !strcmp(cmd, "QUOTE")) {
        if (!*rest)
            return -1;
        out(c, "%s", rest);
    } else {                                            /* any other: as it is, for the server to know */
        out(c, "%s%s%s", cmd, *rest ? " " : "", rest);
    }
    return 0;
}

const char *irc_complete(const struct irc_buffer *b, const char *begun, unsigned nth)
{
    const struct irc_user *best;
    size_t n = strlen(begun);
    unsigned count = 0, i, k, taken;
    unsigned long under = ~0ul;
    const char *after = NULL;
    for (i = 0; i < b->nusers; i++) {
        for (k = 0; k < n && fold(NULL, b->users[i].nick[k]) == fold(NULL, begun[k]) && b->users[i].nick[k]; k++)
            ;
        count += k == n;
    }
    if (!count)
        return NULL;
    /* the nth in the order: those who spoke latest first, then the rest by name: found by going down the order nth
       times (no list is made: this is for a key pressed) */
    for (taken = 0, best = NULL; taken <= nth % count; taken++) {
        const struct irc_user *pick = NULL;
        for (i = 0; i < b->nusers; i++) {
            const struct irc_user *u = &b->users[i];
            int earlier;
            for (k = 0; k < n && fold(NULL, u->nick[k]) == fold(NULL, begun[k]) && u->nick[k]; k++)
                ;
            if (k != n)
                continue;
            /* after the last one taken in the order (spoke down, then name up)? */
            if (best && !(u->spoke < under || (u->spoke == under && strcmp(u->nick, after) > 0)))
                continue;
            earlier = !pick || u->spoke > pick->spoke || (u->spoke == pick->spoke && strcmp(u->nick, pick->nick) < 0);
            if (earlier)
                pick = u;
        }
        if (!pick)
            break;
        best = pick, under = pick->spoke, after = pick->nick;
    }
    return best ? best->nick : NULL;
}
