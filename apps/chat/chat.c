/* CHAT: Chat (GLOS v1, phase F; docs/v1/apps/chat.md): its first part. An IRC client: live chat with today's
 * networks, over TLS where the network has it.
 *
 *   the tabs      at the left: the server's own words, each channel, each private conversation, with the count
 *                 of what is unread (Alt+Down, Alt+Up: the next, the one before)
 *   a channel     its topic above, its lines (who said what and when; colours and bold as they were sent;
 *                 comings and goings in grey), its users at the right by rank
 *   the line      at the bottom: Enter says it; a slash begins a command (/join #channel, /msg nick text, /me,
 *                 /nick, /topic, /part, /quit and the rest: apps/shared/irc.h); Tab completes a nick, the latest
 *                 speaker first; Up and Down bring back what was typed before
 *   Chat          Connect, Disconnect, Settings... (the server, the nick, an account for SASL, channels to join)
 *
 * The settings are CHAT.CFG in GLOS's folder; the account's password is in GLOS's secrets. The connection is on
 * a thread of its own (apps/shared/ircnet.c); the protocol is apps/shared/irc.c.
 *
 * The second part (2026-11-23): a connection lost is made again by itself (5 seconds, then twice as long each
 * time, five minutes at most), the channels it was in joined again; a server silent for two minutes is pinged,
 * and one that does not answer within a minute is a connection lost; logs of what is said (CHAT\LOGS\NETWORK\
 * NAME.LOG in GLOS's folder, plain text); comings and goings hidden, colours left out, and a flash of the task
 * bar's button with a notification when the user is named or spoken to while Chat is not in front: settings.
 *
 * CHAT -demo talks to a server made up inside it (no network: the suite's, and a look at Chat without one).
 * In it "!drop" said in a channel ends the connection as a network would, and "!mute" makes the server silent;
 * -silence SECONDS (the suite's) pings after that long and gives up after as long again.
 *
 * "CHAT ..." lines on stdout (the suite's). */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/gui.h>
#include <glos/tls.h>

#include "../shared/irc.h"
#include "../shared/ircnet.h"

enum { C_CONNECT = 1, C_DISCONNECT, C_SETTINGS, C_EXIT, C_JOIN, C_CLOSE, C_NEXT, C_PREV };

static struct gui_window *win;
static struct gui_widget *tree, *view, *users, *input, *topic, *status;
static struct irc *irc;
static struct ircnet *net;
static int net_was;                     /* the connection's state when last looked at */
static int demo, demo_ended;
static struct irc_buffer *shown;
static unsigned long next_id = 1;       /* the tabs' own numbers (a buffer's `user`) */
static int tree_stale, users_stale, topic_stale, view_stale, status_stale;
static char cfg_path[300];
static struct {
    char server[80], nick[32], alt[32], real[64], account[40], join[160];
    unsigned port;
    int tls, at_start;
    int again, logs, quiet, plain, tell;        /* (the second part's: made again; logs kept; comings and goings
                                                   hidden; colours left out; told when named) */
} cfg = { "irc.libera.chat", "", "", "", "", "", 6697, 1, 0, 1, 0, 0, 0, 1 };
static int want_on;                     /* the user wants to be connected: a connection lost is made again */
static unsigned again_at, again_wait;   /* when to connect again (glos_ticks; 0: not waiting), and the wait (s) */
static char rejoin[400];                /* the channels this user was in when the connection was lost */
static unsigned heard_ms, pinged_ms;    /* when the server last said anything; when it was pinged (0: not) */
static unsigned silence_s = 120;        /* (-silence: the suite's) */
static int demo_dropped, demo_mute;
static char history[20][400];
static unsigned nhistory, at_history;
static char begun[40];                  /* Tab: what is being completed, and how many times Tab was pressed */
static unsigned tab_count, tab_from;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("CHAT ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- the clock: GLOS's; in the demo one of its own, the same every time */

static unsigned long demo_clock = 1795262400ul;        /* 2026-11-21 12:00:00 */

static unsigned long now(void *ctx)
{
    struct glos_time t = { sizeof t, 0, 0, 0 };
    (void)ctx;
    if (demo)
        return demo_clock;
    glos_time(&t);
    return t.utc;
}

static long zone(void)
{
    struct glos_time t = { sizeof t, 0, 0, 0 };
    if (demo || glos_time(&t) != 0)
        return 0;
    return t.offset;
}

/* ---- the server made up inside (CHAT -demo): it answers what Chat sends as a small network would */

static char demo_out[4096];             /* its lines, waiting to be read */
static unsigned demo_n;
static char demo_nick[40] = "*";
static int demo_user, demo_on;

static void demo_line(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void demo_line(const char *fmt, ...)
{
    char t[500];
    unsigned long s = demo_clock % 86400;
    va_list ap;
    int n;
    if (demo_mute)
        return;
    va_start(ap, fmt);
    vsnprintf(t, sizeof t, fmt, ap);
    va_end(ap);
    n = snprintf(demo_out + demo_n, sizeof demo_out - demo_n, "@time=2026-11-21T%02lu:%02lu:%02lu.000Z %s\r\n", s / 3600, s / 60 % 60,
                 s % 60, t);
    if (n > 0 && demo_n + (unsigned)n < sizeof demo_out)
        demo_n += (unsigned)n;
    demo_clock += 7;
}

static void demo_hears(char *line)
{
    char *rest = strchr(line, ' ');
    if (rest)
        *rest++ = 0;
    else
        rest = line + strlen(line);
    if (!strcmp(line, "CAP") && !strncmp(rest, "LS", 2)) {
        demo_line(":demo CAP * LS :server-time multi-prefix");
    } else if (!strcmp(line, "CAP") && !strncmp(rest, "REQ", 3)) {
        demo_line(":demo CAP * ACK :%s", strchr(rest, ':') ? strchr(rest, ':') + 1 : "");
    } else if (!strcmp(line, "CAP")) {                  /* END: the user is on */
        if (demo_user && !demo_on) {
            demo_on = 1;
            demo_line(":demo 001 %s :Welcome to DemoNet, %s", demo_nick, demo_nick);
            demo_line(":demo 005 %s PREFIX=(ov)@+ CHANTYPES=# NETWORK=DemoNet :are supported by this server", demo_nick);
            demo_line(":demo 372 %s :- This network is made up inside Chat: nothing leaves this PC.", demo_nick);
            demo_line(":demo 376 %s :End of the message of the day", demo_nick);
        }
    } else if (!strcmp(line, "NICK")) {
        if (demo_on)
            demo_line(":%s!glos@demo NICK %s", demo_nick, rest);
        snprintf(demo_nick, sizeof demo_nick, "%s", rest);
    } else if (!strcmp(line, "USER")) {
        demo_user = 1;
    } else if (!strcmp(line, "PING")) {
        demo_line("PONG %s", rest);
        if (!demo_mute)
            demo_clock -= 7;            /* (a PONG takes none of the made-up time: pings come by the wall clock) */
    } else if (!strcmp(line, "JOIN")) {
        char *end = strchr(rest, ' ');
        if (end)
            *end = 0;
        demo_line(":%s!glos@demo JOIN %s", demo_nick, rest);
        demo_line(":demo 332 %s %s :Welcome to %s: Chat's own \002made-up\002 channel", demo_nick, rest, rest);
        demo_line(":demo 353 %s = %s :%s @grace +ada linus", demo_nick, rest, demo_nick);
        demo_line(":demo 366 %s %s :End of names", demo_nick, rest);
        demo_line(":grace!g@demo PRIVMSG %s :Hello %s, welcome to %s", rest, demo_nick, rest);
    } else if (!strcmp(line, "PRIVMSG")) {
        char *text = strstr(rest, " :");
        if (!text)
            return;
        *text = 0, text += 2;
        if (text[0] == 1)                               /* (an action: seen, not answered) */
            return;
        if (!strcmp(text, "!drop")) {                   /* (the suite's: the connection lost, as a network loses it) */
            demo_dropped = 1;
            return;
        }
        if (!strcmp(text, "!mute")) {                   /* (... and a server that says nothing more) */
            demo_mute = 1;
            return;
        }
        if (rest[0] != '#')
            demo_line(":%s!a@demo PRIVMSG %s :Got it: %s", rest, demo_nick, text);
        else if (strchr(text, '?'))
            demo_line(":ada!a@demo PRIVMSG %s :%s: \00303a good question\003, see https://example.org/faq for \002more\002", rest, demo_nick);
        else
            demo_line(":ada!a@demo PRIVMSG %s :You said: %s", rest, text);
    } else if (!strcmp(line, "PART")) {
        char *end = strchr(rest, ' ');
        if (end)
            *end = 0;
        demo_line(":%s!glos@demo PART %s", demo_nick, rest);
    } else if (!strcmp(line, "TOPIC") && strstr(rest, " :")) {
        demo_line(":%s!glos@demo TOPIC %s", demo_nick, rest);
    } else if (!strcmp(line, "QUIT")) {
        demo_line("ERROR :Closing link");
        demo_ended = 1;
    } else if (strcmp(line, "AWAY") && strcmp(line, "NOTICE")) {
        demo_line(":demo 421 %s %s :Unknown command", demo_nick, line);
    }
}

/* ---- what goes to the server */

static void to_server(void *ctx, const char *bytes, unsigned n)
{
    (void)ctx;
    if (demo) {
        char line[520];
        unsigned i, k = 0;
        for (i = 0; i < n; i++) {
            if (bytes[i] == '\n') {
                line[k] = 0;
                demo_hears(line);
                k = 0;
            } else if (bytes[i] != '\r' && k < sizeof line - 1) {
                line[k++] = bytes[i];
            }
        }
    } else if (net) {
        ircnet_write(net, bytes, n);
    }
}

/* ---- the lines shown */

static const unsigned colours[16] = { 0x808080, 0x000000, 0x00007F, 0x009300, 0xD00000, 0x7F0000, 0x9C009C, 0xC06000,
                                      0x9A8A00, 0x30A030, 0x009393, 0x00A0A0, 0x0000D0, 0xD000D0, 0x606060, 0x909090 };
/* (no red among them: red is for a line that names the user) */
static const unsigned nick_colours[8] = { 0x1F5FBF, 0x8B5A2B, 0x2E8B57, 0x8B3A8B, 0xB8600B, 0x4B6CB7, 0x6B8E23, 0x008080 };

static unsigned nick_colour(const char *nick)
{
    unsigned h = 0;
    for (; *nick; nick++)
        h = h * 31 + (unsigned char)*nick;
    return nick_colours[h % 8];
}

/* Text as it was sent, into the document: its runs with their looks; what looks like a link is one. */
static void put_text(struct gui_rdoc *d, const char *text, unsigned base, unsigned rgb)
{
    struct irc_span span[24];
    char plain[600];
    int n = irc_format(text, plain, sizeof plain, span, 24), i;
    for (i = 0; i < n; i++) {
        unsigned style = base | (span[i].flags & IRC_BOLD ? GUI_RS_BOLD : 0) | (span[i].flags & IRC_ITALIC ? GUI_RS_ITALIC : 0)
                         | (span[i].flags & IRC_MONO ? GUI_RS_CODE : 0) | (span[i].flags & IRC_STRIKE ? GUI_RS_STRIKE : 0);
        const char *p = plain + span[i].start, *end = p + span[i].len;
        gui_rdoc_colour(d, span[i].fg >= 0 && !cfg.plain ? colours[span[i].fg & 15] : rgb);
        while (p < end) {
            const char *link = NULL, *q;
            for (q = p; q < end; q++)
                if ((end - q > 8 && (!strncmp(q, "https://", 8) || !strncmp(q, "http://", 7)))
                    && (q == plain || q[-1] == ' ' || q[-1] == '(' || q[-1] == '<')) {
                    link = q;
                    break;
                }
            if (!link) {
                gui_rdoc_text(d, p, (int)(end - p), style, NULL);
                break;
            }
            if (link > p)
                gui_rdoc_text(d, p, (int)(link - p), style, NULL);
            for (q = link; q < end && *q != ' ' && *q != '>' && *q != ')'; q++)
                ;
            {
                char url[300];
                snprintf(url, sizeof url, "%.*s", (int)(q - link), link);
                gui_rdoc_text(d, link, (int)(q - link), style, url);
            }
            p = q;
        }
    }
    gui_rdoc_colour(d, GFX_NONE);
}

static void put_line(struct gui_rdoc *d, const struct irc_buffer *b, const struct irc_line *l)
{
    unsigned long local = l->when + (unsigned long)zone(), s = local % 86400;
    const unsigned grey = 0x707070;
    char t[160];
    if (cfg.quiet && !l->mine && (l->kind == IRC_L_JOIN || l->kind == IRC_L_PART || l->kind == IRC_L_QUIT))
        return;                         /* (comings and goings hidden: a setting) */
    gui_rdoc_block(d, GUI_RB_PARA, 0);
    snprintf(t, sizeof t, "%02lu:%02lu ", s / 3600, s / 60 % 60);
    gui_rdoc_colour(d, grey);
    gui_rdoc_text(d, t, -1, 0, NULL);
    switch (l->kind) {
    case IRC_L_MSG:
    case IRC_L_NOTICE:
        snprintf(t, sizeof t, l->kind == IRC_L_NOTICE ? "-%s- " : "<%s> ", l->nick);
        /* (red: a line in a channel that names the user; in a private conversation every line is to the user) */
        gui_rdoc_colour(d, l->highlight && b->kind == IRC_B_CHANNEL ? 0xD00000 : l->mine ? 0x404040 : nick_colour(l->nick));
        gui_rdoc_text(d, t, -1, GUI_RS_BOLD, NULL);
        put_text(d, l->text, 0, GFX_NONE);
        break;
    case IRC_L_ACTION:
        snprintf(t, sizeof t, "* %s ", l->nick);
        gui_rdoc_colour(d, l->mine ? 0x404040 : nick_colour(l->nick));
        gui_rdoc_text(d, t, -1, GUI_RS_ITALIC, NULL);
        put_text(d, l->text, GUI_RS_ITALIC, GFX_NONE);
        break;
    case IRC_L_JOIN:
        if (l->mine)
            snprintf(t, sizeof t, GUI_T("--> You have joined %s"), b->name);
        else
            snprintf(t, sizeof t, GUI_T("--> %s has joined (%s)"), l->nick, l->text);
        gui_rdoc_text(d, t, -1, 0, NULL);
        break;
    case IRC_L_PART:
    case IRC_L_QUIT:
        snprintf(t, sizeof t, l->kind == IRC_L_QUIT ? GUI_T("<-- %s has quit") : l->mine ? GUI_T("<-- You have left") : GUI_T("<-- %s has left"),
                 l->nick);
        gui_rdoc_text(d, t, -1, 0, NULL);
        if (l->text[0]) {
            gui_rdoc_text(d, " (", -1, 0, NULL);
            put_text(d, l->text, 0, grey);
            gui_rdoc_colour(d, grey);
            gui_rdoc_text(d, ")", -1, 0, NULL);
        }
        break;
    case IRC_L_NICK:
        snprintf(t, sizeof t, GUI_T("%s is now %s"), l->nick, l->text);
        gui_rdoc_text(d, t, -1, 0, NULL);
        break;
    case IRC_L_MODE:
        snprintf(t, sizeof t, GUI_T("%s sets %s"), l->nick, l->text);
        gui_rdoc_text(d, t, -1, 0, NULL);
        break;
    case IRC_L_TOPIC:
        snprintf(t, sizeof t, GUI_T("%s changed the topic to: "), l->nick);
        gui_rdoc_text(d, t, -1, 0, NULL);
        put_text(d, l->text, 0, grey);
        break;
    case IRC_L_ERROR:
        gui_rdoc_colour(d, 0xD00000);
        gui_rdoc_text(d, l->text, -1, 0, NULL);
        break;
    default:
        put_text(d, l->text, 0, grey);
    }
    gui_rdoc_colour(d, GFX_NONE);
}

static void show(struct irc_buffer *b)  /* a tab's lines, users and topic in the window */
{
    struct gui_rdoc *d = gui_rdoc_new();
    unsigned i;
    shown = b;
    for (i = 0; d && i < b->nlines; i++)
        put_line(d, b, irc_line_at(b, i));
    gui_richview_set(view, d);
    gui_richview_follow(view, 1);
    irc_read(b);
    tree_stale = users_stale = topic_stale = 1;
    say("shown %s lines=%u", b->name, b->nlines);
}

/* ---- the tabs: a tree's nodes, each a buffer by its own number */

static struct irc_buffer *by_id(unsigned long id)
{
    unsigned i;
    for (i = 0; irc && i < irc_buffers(irc); i++)
        if ((unsigned long)irc_buffer(irc, i)->user == id)
            return irc_buffer(irc, i);
    return NULL;
}

static int t_children(void *ctx, unsigned long id, unsigned long *ids, int room, int from)
{
    unsigned n = irc && !id ? irc_buffers(irc) : 0, i;
    (void)ctx;
    for (i = (unsigned)from; i < n && (int)(i - (unsigned)from) < room; i++)
        ids[i - (unsigned)from] = (unsigned long)irc_buffer(irc, i)->user;
    return (int)n;
}

static void t_node(void *ctx, unsigned long id, struct gui_tnode *out)
{
    static char name[80], badge[16];
    struct irc_buffer *b = by_id(id);
    (void)ctx;
    memset(out, 0, sizeof *out);
    if (!b)
        return;
    snprintf(name, sizeof name, "%s", b->kind == IRC_B_SERVER && irc_network(irc)[0] ? irc_network(irc) : b->name);
    out->text = name;
    if (b->unread) {
        snprintf(badge, sizeof badge, b->highlights ? "%u!" : "%u", b->unread);
        out->badge = badge;
    }
    out->icon = gui_icon(b->kind == IRC_B_SERVER ? "network" : b->kind == IRC_B_CHANNEL ? "app-chat" : "user", 0);
}

static const struct gui_tmodel tabs = { t_children, t_node, NULL };

/* ---- a channel's users */

static int u_count(void *ctx) { (void)ctx; return shown && shown->kind == IRC_B_CHANNEL ? (int)shown->nusers : 0; }

static void u_row(void *ctx, int row, struct gui_vrow *out)
{
    static char name[48];
    (void)ctx;
    memset(out, 0, sizeof *out);
    if (!shown || row < 0 || (unsigned)row >= shown->nusers)
        return;
    snprintf(name, sizeof name, "%c%s", shown->users[row].rank[0] ? shown->users[row].rank[0] : ' ', shown->users[row].nick);
    out->cell[0] = name;
    out->id = (unsigned long)row + 1;
}

static const struct gui_model user_rows = { u_count, u_row, NULL, NULL, NULL };

/* ---- what IRC tells */

/* ---- the logs (a setting): CHAT\LOGS\NETWORK\NAME.LOG in GLOS's folder, a line each, as plain text */

static void log_part(char *out, size_t room, const char *name, const char *none)        /* a name as 8 letters */
{
    size_t n = 0;
    for (; *name && n + 1 < room && n < 8; name++)
        if ((*name >= 'A' && *name <= 'Z') || (*name >= '0' && *name <= '9') || *name == '-' || *name == '_')
            out[n++] = *name;
        else if (*name >= 'a' && *name <= 'z')
            out[n++] = (char)(*name - 'a' + 'A');
    out[n] = 0;
    if (!n)
        snprintf(out, room, "%s", none);
}

static void log_line(const struct irc_buffer *b, const struct irc_line *l)
{
    static char made[300];              /* (the network's folder last made) */
    char dir[300], path[320], net_name[12], name[12], plain[520], t[700];
    unsigned long local = l->when + (unsigned long)zone(), days = local / 86400, s = local % 86400;
    long y, m, d;
    FILE *f;
    if (!glos_home() || !glos_home()[0] || !irc_network(irc)[0])
        return;                         /* (the connection's first lines, before the network says its name: not kept,
                                           so that a network's logs are in one folder) */
    log_part(net_name, sizeof net_name, irc_network(irc), "NETWORK");
    log_part(name, sizeof name, b->kind == IRC_B_SERVER ? "SERVER" : b->name, "NONAME");
    gui_path_join(dir, sizeof dir, glos_home(), "CHAT");
    gui_path_join(path, sizeof path, dir, "LOGS");
    gui_path_join(dir, sizeof dir, path, net_name);
    if (strcmp(made, dir)) {
        char top[300];
        gui_path_join(top, sizeof top, glos_home(), "CHAT");
        gui_mkdir(top);
        gui_mkdir(path);
        gui_mkdir(dir);
        snprintf(made, sizeof made, "%s", dir);
    }
    snprintf(t, sizeof t, "%s.LOG", name);
    gui_path_join(path, sizeof path, dir, t);
    {                                   /* (the civil date from the days: Howard Hinnant's) */
        long z = (long)days + 719468, era = z / 146097, doe = z - era * 146097;
        long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
        d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9, y = yoe + era * 400 + (m <= 2);
    }
    irc_format(l->text, plain, sizeof plain, NULL, 0);
    switch (l->kind) {
    case IRC_L_MSG: snprintf(t, sizeof t, "<%s> %s", l->nick, plain); break;
    case IRC_L_NOTICE: snprintf(t, sizeof t, "-%s- %s", l->nick, plain); break;
    case IRC_L_ACTION: snprintf(t, sizeof t, "* %s %s", l->nick, plain); break;
    case IRC_L_JOIN: snprintf(t, sizeof t, "--> %s has joined", l->nick); break;
    case IRC_L_PART: snprintf(t, sizeof t, "<-- %s has left%s%s%s", l->nick, plain[0] ? " (" : "", plain, plain[0] ? ")" : ""); break;
    case IRC_L_QUIT: snprintf(t, sizeof t, "<-- %s has quit%s%s%s", l->nick, plain[0] ? " (" : "", plain, plain[0] ? ")" : ""); break;
    case IRC_L_NICK: snprintf(t, sizeof t, "%s is now %s", l->nick, plain); break;
    case IRC_L_MODE: snprintf(t, sizeof t, "%s sets %s", l->nick, plain); break;
    case IRC_L_TOPIC: snprintf(t, sizeof t, "%s changed the topic to: %s", l->nick, plain); break;
    default: snprintf(t, sizeof t, "-- %s", plain);
    }
    if ((f = gui_fopen(path, "ab")) != NULL) {          /* (DOS's text: CR LF, the same from the host's build) */
        fprintf(f, "%04ld-%02ld-%02ld %02lu:%02lu:%02lu %s\r\n", y, m, d, s / 3600, s / 60 % 60, s % 60, t);
        fclose(f);
    }
}

/* ---- told (a setting): named in a channel, or spoken to privately, while Chat is not in front */

static void tell_user(const struct irc_buffer *b, const struct irc_line *l)
{
    char title[80], plain[200];
    if (!cfg.tell || l->mine || win->active || (l->kind != IRC_L_MSG && l->kind != IRC_L_ACTION)
        || !(l->highlight || b->kind == IRC_B_PRIVATE))
        return;
    glos_set(win->id, GLOS_PROP_WIN_FLASH, 1);
    irc_format(l->text, plain, sizeof plain, NULL, 0);
    snprintf(title, sizeof title, b->kind == IRC_B_PRIVATE ? GUI_T("%s, privately") : GUI_T("%s in %s"), l->nick, b->name);
    glos_notify(title, plain, NULL, NULL, NULL, 0);
    say("told %s %s", b->name, l->nick);
}

static void events(void *ctx, int what, struct irc_buffer *b)
{
    (void)ctx;
    switch (what) {
    case IRC_E_BUFFER:
        b->user = (void *)++next_id;
        tree_stale = 1;
        if (b->kind == IRC_B_CHANNEL)                   /* (a channel joined is shown at once; a conversation begun by
                                                           another waits in its tab) */
            show(b);
        break;
    case IRC_E_CLOSED:
        if (b == shown)
            shown = NULL;                               /* (the server's, once it is gone: refresh) */
        tree_stale = 1;
        break;
    case IRC_E_LINE: {
        const struct irc_line *l = irc_line_at(b, b->nlines - 1);
        if (l && cfg.logs)
            log_line(b, l);
        if (l)
            tell_user(b, l);
        if (l && (l->kind == IRC_L_MSG || l->kind == IRC_L_ACTION || l->kind == IRC_L_NOTICE || l->kind == IRC_L_ERROR)) {
            char plain[520];                            /* (as text: its codes are for the window) */
            irc_format(l->text, plain, sizeof plain, NULL, 0);
            say("line %s %s%s%s %s", b->name, l->kind == IRC_L_ACTION ? "* " : "<", l->nick, l->kind == IRC_L_ACTION ? "" : ">", plain);
        }
        if (b == shown) {
            struct gui_rdoc *d = gui_richview_doc(view);
            if (d && l) {
                put_line(d, b, l);
                if (gui_rdoc_blocks(d) > IRC_LINES)
                    gui_rdoc_drop_first(d, gui_rdoc_blocks(d) - IRC_LINES);
                view_stale = 1;
            }
            irc_read(b);
        } else {
            tree_stale = 1;
        }
        break;
    }
    case IRC_E_USERS:
        users_stale |= b == shown;
        break;
    case IRC_E_TOPIC:
        topic_stale |= b == shown;
        tree_stale = 1;
        status_stale |= b->kind == IRC_B_SERVER;        /* (the network's name) */
        break;
    case IRC_E_STATE:
    case IRC_E_NICK:
        status_stale = 1;
        if (what == IRC_E_STATE && irc_state(irc) == IRC_ON) {         /* on: the channels to join */
            char *w, all[600], seen[16][64];
            int nseen = 0;
            say("on %s", irc_nick(irc));
            again_wait = 0;                     /* (on: the next connection lost waits the shortest again) */
            if (rejoin[0])
                say("rejoining %s", rejoin);
            snprintf(all, sizeof all, "%s,%s", cfg.join, rejoin);
            rejoin[0] = 0;
            for (w = strtok(all, ", "); w; w = strtok(NULL, ", ")) {
                char line[80];
                int k;
                for (k = 0; k < nseen && !irc_same(irc, seen[k], w); k++)
                    ;                           /* (a channel both in the settings and joined since: once) */
                if (k < nseen)
                    continue;
                if (nseen < 16)
                    snprintf(seen[nseen++], sizeof seen[0], "%s", w);
                snprintf(line, sizeof line, "/join %s", w);
                irc_input(irc, irc_buffer(irc, 0), line);
            }
        }
        break;
    }
}

static void refresh(void)               /* the window brought up to what has changed */
{
    char t[600];
    if (!irc)
        return;
    if (!shown)
        show(irc_buffer(irc, 0));
    if (tree_stale) {
        gui_vtree_changed(tree, 0);
        gui_vtree_set(tree, (unsigned long)shown->user);
        tree_stale = 0;
    }
    if (users_stale) {
        gui_vlist_changed(users);
        users_stale = 0;
    }
    if (topic_stale) {
        char plain[520];
        irc_format(shown->topic, plain, sizeof plain, NULL, 0);
        if (shown->kind == IRC_B_CHANNEL)
            snprintf(t, sizeof t, "%s: %s", shown->name, plain[0] ? plain : GUI_T("(no topic)"));
        else
            snprintf(t, sizeof t, "%s", shown->kind == IRC_B_SERVER ? GUI_T("The server's own words") : shown->name);
        gui_set_text(topic, t);
        topic_stale = 0;
    }
    if (view_stale) {
        gui_richview_changed(view);
        view_stale = 0;
    }
    if (status_stale) {
        int st = irc_state(irc);
        if (st == IRC_ON)
            snprintf(t, sizeof t, GUI_T("%s on %s"), irc_nick(irc), irc_network(irc)[0] ? irc_network(irc) : cfg.server);
        else
            snprintf(t, sizeof t, "%s", st == IRC_REGISTERING ? GUI_T("Connected: being announced...") : GUI_T("Not connected. Chat, Connect"));
        gui_set_text(status, t);
        status_stale = 0;
    }
}

/* ---- the connection */

static void disconnect(const char *why)
{
    if (net) {
        ircnet_close(net);
        net = NULL;
    }
    if (irc && irc_state(irc) != IRC_OFF) {
        irc_stopped(irc);
        say("ended %s", why);
    }
    status_stale = 1;
}

/* A connection ended that the user did not end: the channels it was in kept to join again, and, while the user
   wants to be connected (and the setting says so), a time to connect again: 5 seconds, then twice as long each
   time (five minutes at most) until it is on again. */
static void lost(const char *why)
{
    char t[200];
    unsigned i;
    if (irc && irc_state(irc) == IRC_ON) {
        rejoin[0] = 0;
        for (i = 1; i < irc_buffers(irc); i++) {
            const struct irc_buffer *b = irc_buffer(irc, i);
            if (b->kind == IRC_B_CHANNEL && b->joined && strlen(rejoin) + strlen(b->name) + 2 < sizeof rejoin)
                strcat(strcat(rejoin, rejoin[0] ? "," : ""), b->name);
        }
    }
    disconnect(why);
    if (!want_on || !cfg.again)
        return;
    again_wait = !again_wait ? 5 : again_wait >= 150 ? 300 : again_wait * 2;
    again_at = glos_ticks() + again_wait * 1000u;
    if (!again_at)
        again_at = 1;
    snprintf(t, sizeof t, GUI_T("The connection was lost (%s). Connecting again in %u seconds..."), why, again_wait);
    gui_set_text(status, t);
    status_stale = 0;
    say("again in %u", again_wait);
}

static void connect_now(void)
{
    char certs[300];
    if (!irc || irc_state(irc) != IRC_OFF || net)
        return;
    want_on = 1, again_at = 0;
    heard_ms = glos_ticks(), pinged_ms = 0;
    if (demo) {
        demo_n = 0, demo_user = demo_on = demo_ended = demo_dropped = demo_mute = 0;
        snprintf(demo_nick, sizeof demo_nick, "*");
        irc_start(irc);
        return;
    }
    gui_path_join(certs, sizeof certs, glos_home(), TLS_CERTS);
    if (!(net = ircnet_open(cfg.server, cfg.port, cfg.tls, certs, NULL))) {
        gui_message(win, GUI_T("Chat"), GUI_T("There is not enough memory to connect."), GUI_MB_OK);
        return;
    }
    net_was = IRCNET_CONNECTING;
    {
        char t[160];
        snprintf(t, sizeof t, GUI_T("Connecting to %s..."), cfg.server);
        gui_set_text(status, t);
    }
    say("connecting %s %u %s", cfg.server, cfg.port, cfg.tls ? "tls" : "plain");
}

static void tick(void *arg)             /* ten times a second: what came is read, the window brought up to it */
{
    char piece[1024], why[80];
    int r, state;
    (void)arg;
    if (!irc)
        return;
    if (again_at && (int)(glos_ticks() - again_at) >= 0) {       /* the time to connect again */
        again_at = 0;
        say("connecting again");
        connect_now();
    }
    if (irc_state(irc) == IRC_ON) {     /* a server silent too long: pinged; silent as long again: lost */
        unsigned ms = glos_ticks();
        if (!pinged_ms && ms - heard_ms >= silence_s * 1000u) {
            char line[40];
            snprintf(line, sizeof line, "/raw PING :glos%u", ms);
            irc_input(irc, irc_buffer(irc, 0), line);
            pinged_ms = ms ? ms : 1;
            say("pinged");
        } else if (pinged_ms && ms - pinged_ms >= (silence_s < 60 ? silence_s : 60) * 1000u) {
            pinged_ms = 0;
            if (net) {
                ircnet_close(net);
                net = NULL;
            }
            lost(GUI_T("no answer from the server"));
            refresh();
            return;
        }
    }
    if (demo) {
        while (demo_n) {                        /* (what it answers is answered in turn, until it has no more to say) */
            unsigned n = demo_n < sizeof piece ? demo_n : (unsigned)sizeof piece;
            memcpy(piece, demo_out, n);
            memmove(demo_out, demo_out + n, demo_n - n);
            demo_n -= n;
            heard_ms = glos_ticks(), pinged_ms = 0;
            irc_feed(irc, piece, n);
        }
        if (demo_ended && irc_state(irc) != IRC_OFF)
            lost("closed by the server");
        if (demo_dropped && irc_state(irc) != IRC_OFF) {
            demo_dropped = 0;
            lost(GUI_T("the network went away"));
        }
    } else if (net) {
        state = ircnet_state(net, why, sizeof why);
        if (state == IRCNET_ON && net_was == IRCNET_CONNECTING) {
            say("connected");
            irc_start(irc);
        }
        net_was = state;
        while ((r = ircnet_read(net, piece, sizeof piece)) > 0) {
            heard_ms = glos_ticks(), pinged_ms = 0;
            irc_feed(irc, piece, (unsigned)r);
        }
        if (state == IRCNET_ENDED) {
            char t[200];
            int was_on = irc_state(irc) != IRC_OFF;
            lost(why);
            if (!was_on) {                              /* (it never got there: said, not only in the tab) */
                say("failed %s", why);
                if (again_at)
                    snprintf(t, sizeof t, GUI_T("%s could not be reached: %s. Trying again in %u seconds..."), cfg.server, why,
                             again_wait);
                else
                    snprintf(t, sizeof t, GUI_T("%s could not be reached: %s."), cfg.server, why);
                gui_set_text(status, t);
                status_stale = 0;
            }
        }
    }
    refresh();
}

/* ---- the settings */

static void settings_read(void)
{
    char line[300], *v;
    FILE *f = cfg_path[0] ? gui_fopen(cfg_path, "r") : NULL;
    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!(v = strchr(line, ' ')))
            continue;
        *v++ = 0;
        if (!strcmp(line, "server"))
            snprintf(cfg.server, sizeof cfg.server, "%s", v);
        else if (!strcmp(line, "port"))
            cfg.port = (unsigned)atoi(v) > 0 && atoi(v) < 65536 ? (unsigned)atoi(v) : cfg.port;
        else if (!strcmp(line, "tls"))
            cfg.tls = atoi(v) != 0;
        else if (!strcmp(line, "nick"))
            snprintf(cfg.nick, sizeof cfg.nick, "%s", v);
        else if (!strcmp(line, "alt"))
            snprintf(cfg.alt, sizeof cfg.alt, "%s", v);
        else if (!strcmp(line, "real"))
            snprintf(cfg.real, sizeof cfg.real, "%s", v);
        else if (!strcmp(line, "account"))
            snprintf(cfg.account, sizeof cfg.account, "%s", v);
        else if (!strcmp(line, "join"))
            snprintf(cfg.join, sizeof cfg.join, "%s", v);
        else if (!strcmp(line, "start"))
            cfg.at_start = atoi(v) != 0;
        else if (!strcmp(line, "again"))
            cfg.again = atoi(v) != 0;
        else if (!strcmp(line, "logs"))
            cfg.logs = atoi(v) != 0;
        else if (!strcmp(line, "quiet"))
            cfg.quiet = atoi(v) != 0;
        else if (!strcmp(line, "plain"))
            cfg.plain = atoi(v) != 0;
        else if (!strcmp(line, "tell"))
            cfg.tell = atoi(v) != 0;
    }
    fclose(f);
}

static void settings_keep(void)
{
    FILE *f = cfg_path[0] ? gui_fopen(cfg_path, "w") : NULL;
    if (!f)
        return;
    fprintf(f, "server %s\nport %u\ntls %d\nnick %s\nalt %s\nreal %s\naccount %s\njoin %s\nstart %d\n", cfg.server, cfg.port, cfg.tls, cfg.nick,
            cfg.alt, cfg.real, cfg.account, cfg.join, cfg.at_start);
    fprintf(f, "again %d\nlogs %d\nquiet %d\nplain %d\ntell %d\n", cfg.again, cfg.logs, cfg.quiet, cfg.plain, cfg.tell);
    fclose(f);
}

static void make_irc(void)              /* IRC's state made anew from the settings (not while connected) */
{
    struct irc_opts o;
    char password[128] = "";
    unsigned len = 0;
    if (irc && irc_state(irc) != IRC_OFF)
        return;
    if (!demo && cfg.account[0] && glos_secret_get("chat", password, sizeof password - 1, &len, 0) == 0)
        password[len < sizeof password ? len : sizeof password - 1] = 0;
    memset(&o, 0, sizeof o);
    o.nick = cfg.nick[0] ? cfg.nick : "glos", o.alt = cfg.alt, o.user = "glos", o.real = cfg.real[0] ? cfg.real : "A GLOS user";
    o.account = cfg.account, o.password = password;
    o.send = to_server, o.event = events, o.now = now;
    irc_free(irc);
    shown = NULL, next_id = 1;
    irc = irc_new(&o);
    memset(password, 0, sizeof password);
    if (irc)
        irc_buffer(irc, 0)->user = (void *)1ul;
    tree_stale = users_stale = topic_stale = status_stale = 1;
}

static struct gui_widget *s_server, *s_port, *s_tls, *s_nick, *s_alt, *s_real, *s_account, *s_password, *s_join, *s_start;
static struct gui_widget *s_again, *s_logs, *s_quiet, *s_plain, *s_tell;
static char s_pw[128];
static int s_ok;

static void settings_done(struct gui_widget *b, int what)
{
    (void)what;                         /* (the button's own number is b->id) */
    if (b->id == 1) {                              /* (read while the dialog is there: gui_modal closes it) */
        snprintf(cfg.server, sizeof cfg.server, "%s", gui_get_text(s_server));
        cfg.port = (unsigned)gui_value(s_port);
        cfg.tls = gui_checked(s_tls);
        snprintf(cfg.nick, sizeof cfg.nick, "%s", gui_get_text(s_nick));
        snprintf(cfg.alt, sizeof cfg.alt, "%s", gui_get_text(s_alt));
        snprintf(cfg.real, sizeof cfg.real, "%s", gui_get_text(s_real));
        snprintf(cfg.account, sizeof cfg.account, "%s", gui_get_text(s_account));
        snprintf(s_pw, sizeof s_pw, "%s", gui_get_text(s_password));
        snprintf(cfg.join, sizeof cfg.join, "%s", gui_get_text(s_join));
        cfg.at_start = gui_checked(s_start);
        cfg.again = gui_checked(s_again), cfg.logs = gui_checked(s_logs), cfg.quiet = gui_checked(s_quiet);
        cfg.plain = gui_checked(s_plain), cfg.tell = gui_checked(s_tell);
        s_ok = 1;
    }
    gui_end_modal(b->win, b->id == 1 ? GUI_OK : GUI_CANCEL_R);
}

static void settings(void)
{
    struct gui_window *d = gui_window(GUI_T("Chat Settings"), 400, 0, GUI_W_DIALOG);
    struct gui_widget *box, *form, *row;
    d->owner = win;
    box = gui_vbox(d->root, 6, 8);
    form = gui_form(box);
    s_server = gui_field(gui_row(form, GUI_T("&Server:"), NULL), 30);
    gui_set_text(s_server, cfg.server);
    row = gui_row(form, GUI_T("&Port:"), NULL);
    s_port = gui_spin(row, 1, 65535, (int)cfg.port);
    s_tls = gui_check(row, GUI_T("&TLS (the connection is locked)"), cfg.tls);
    s_nick = gui_field(gui_row(form, GUI_T("&Nick:"), NULL), 20);
    gui_set_text(s_nick, cfg.nick);
    s_alt = gui_field(gui_row(form, GUI_T("&Other nick:"), NULL), 20);
    gui_set_text(s_alt, cfg.alt);
    s_real = gui_field(gui_row(form, GUI_T("&Real name:"), NULL), 30);
    gui_set_text(s_real, cfg.real);
    s_account = gui_field(gui_row(form, GUI_T("&Account:"), NULL), 20);
    gui_set_text(s_account, cfg.account);
    s_password = gui_field(gui_row(form, GUI_T("Pass&word:"), NULL), 20);
    gui_field_mask(s_password, 1);
    s_join = gui_field(gui_row(form, GUI_T("&Channels:"), NULL), 30);
    gui_set_text(s_join, cfg.join);
    {                                   /* (the checks in two columns: the dialog fits a 640 x 480 screen) */
        struct gui_widget *cols = gui_hbox(box, 12, 0), *a = gui_vbox(cols, 4, 0), *b = gui_vbox(cols, 4, 0);
        s_start = gui_check(a, GUI_T("Connect as Chat &starts"), cfg.at_start);
        s_again = gui_check(b, GUI_T("Connect a&gain by itself"), cfg.again);
        s_quiet = gui_check(a, GUI_T("&Hide comings and goings"), cfg.quiet);
        s_plain = gui_check(b, GUI_T("Leave co&lours out"), cfg.plain);
        s_logs = gui_check(a, GUI_T("&Keep logs"), cfg.logs);
        s_tell = gui_check(b, GUI_T("Tell &me when I am named"), cfg.tell);
    }
    gui_label(box, GUI_T("A password typed here is kept in GLOS's secrets; left empty, the one kept stays."));
    row = gui_hbox(box, 6, 0);
    gui_space(row, 0, 0);
    gui_button(row, GUI_T("OK"), settings_done, 1)->flags |= GUI_DEFAULT;
    gui_button(row, GUI_T("Cancel"), settings_done, 2)->flags |= GUI_CANCEL;
    gui_focus(s_server);
    s_ok = 0, s_pw[0] = 0;
    if (gui_modal(d) != GUI_OK || !s_ok) {
        say("settings left");
        return;
    }
    if (s_pw[0] && !demo)
        glos_secret_put("chat", s_pw, (unsigned)strlen(s_pw), 0);
    memset(s_pw, 0, sizeof s_pw);
    settings_keep();
    say("settings %s %u %s nick=%s join=%s again=%d logs=%d quiet=%d plain=%d tell=%d", cfg.server, cfg.port,
        cfg.tls ? "tls" : "plain", cfg.nick, cfg.join, cfg.again, cfg.logs, cfg.quiet, cfg.plain, cfg.tell);
    make_irc();
    if (shown)                          /* (hidden lines and colours: the tab drawn again) */
        show(shown);
}

/* ---- the line typed */

static void typed(struct gui_widget *w, int what)
{
    char line[400];
    if (what != GUI_ON_ACTIVATE || !irc)
        return;
    snprintf(line, sizeof line, "%s", gui_get_text(w));
    if (!line[0])
        return;
    if (nhistory < 20)
        nhistory++;
    memmove(history[1], history[0], sizeof history[0] * 19);
    snprintf(history[0], sizeof history[0], "%s", line);
    at_history = 0, tab_count = 0;
    gui_set_text(w, "");
    if ((!strncmp(line, "/quit", 5) || !strncmp(line, "/QUIT", 5)) && (!line[5] || line[5] == ' '))
        want_on = 0, again_at = 0;                      /* (left on purpose: not made again) */
    if (!strncmp(line, "/close", 6) && (!line[6] || line[6] == ' ')) {
        if (shown && shown != irc_buffer(irc, 0)) {
            say("closed %s", shown->name);
            irc_close(irc, shown);
        }
    } else if (irc_input(irc, shown, line) != 0) {
        gui_set_text(status, irc_state(irc) == IRC_OFF ? GUI_T("Not connected. Chat, Connect")
                     : GUI_T("There is nobody to say that to here: choose a channel, or begin a command with a slash."));
        say("unsaid");
    }
    refresh();
}

static int input_keys(struct gui_widget *w, struct gui_ev *e)  /* the line's own keys: Tab, and the arrows */
{
    char line[400];
    if (e->type != GUI_EV_KEY || e->mods & (GLOS_MOD_CTRL | GLOS_MOD_ALT))
        return 0;
    if (e->key == GUI_K_TAB && !(e->mods & GLOS_MOD_SHIFT)) {  /* a nick completed: the word at the line's end */
        const char *nick, *text = gui_get_text(w);
        if (!shown)
            return 1;
        if (!tab_count) {                               /* (the first Tab: what is begun is kept for the next ones) */
            size_t n = strlen(text), at = n;
            while (at && text[at - 1] != ' ')
                at--;
            snprintf(begun, sizeof begun, "%s", text + at);
            tab_from = (unsigned)at;
        }
        if (!begun[0] || !(nick = irc_complete(shown, begun, tab_count)))
            return 1;
        snprintf(line, sizeof line, "%.*s%s%s", (int)tab_from, text, nick, tab_from ? " " : ": ");
        tab_count++;
        gui_set_text(w, line);
        return 1;
    }
    if (e->key == GUI_K_UP || e->key == GUI_K_DOWN) {
        if (e->key == GUI_K_UP && at_history < nhistory)
            at_history++;
        else if (e->key == GUI_K_DOWN && at_history)
            at_history--;
        gui_set_text(w, at_history ? history[at_history - 1] : "");
        tab_count = 0;
        return 1;
    }
    tab_count = 0;
    return 0;
}

/* ---- commands */

static void tab_step(int by)
{
    unsigned n = irc ? irc_buffers(irc) : 0, i;
    for (i = 0; i < n; i++)
        if (irc_buffer(irc, i) == shown) {
            show(irc_buffer(irc, (i + n + (unsigned)by) % n));
            refresh();
            return;
        }
}

static void command(struct gui_window *w, int id)
{
    char t[80] = "#";
    (void)w;
    switch (id) {
    case C_CONNECT:
        make_irc();
        connect_now();
        break;
    case C_DISCONNECT:
        want_on = 0, again_at = 0;
        if (irc && irc_state(irc) == IRC_ON)
            irc_input(irc, irc_buffer(irc, 0), "/quit");
        else
            disconnect("closed");
        break;
    case C_SETTINGS:
        settings();
        break;
    case C_EXIT:
        gui_quit(0);
        break;
    case C_JOIN:
        if (irc && irc_state(irc) == IRC_ON && gui_ask(win, GUI_T("Join a Channel"), GUI_T("The channel's name:"), t, sizeof t) && t[0]) {
            char line[100];
            snprintf(line, sizeof line, "/join %s%s", t[0] == '#' || t[0] == '&' ? "" : "#", t);
            irc_input(irc, irc_buffer(irc, 0), line);
        }
        break;
    case C_CLOSE:
        if (irc && shown && shown != irc_buffer(irc, 0)) {
            say("closed %s", shown->name);
            irc_close(irc, shown);
            refresh();
        }
        break;
    case C_NEXT:
    case C_PREV:
        tab_step(id == C_NEXT ? 1 : -1);
        break;
    }
}

static void update(struct gui_window *w)
{
    int st = irc ? irc_state(irc) : IRC_OFF;
    gui_command_enable(w, C_CONNECT, st == IRC_OFF && !net);
    gui_command_enable(w, C_DISCONNECT, st != IRC_OFF || net != NULL);
    gui_command_enable(w, C_SETTINGS, st == IRC_OFF && !net);
    gui_command_enable(w, C_JOIN, st == IRC_ON);
    gui_command_enable(w, C_CLOSE, irc && shown && shown != irc_buffer(irc, 0));
}

static void tree_on(struct gui_widget *w, int what)
{
    struct irc_buffer *b = by_id(gui_vtree_get(w));
    if (what == GUI_ON_SELECT && b && b != shown) {
        show(b);
        refresh();
    }
}

static void view_on(struct gui_widget *w, int what)    /* a link pressed: copied, until there is a Web Reader to open it */
{
    const char *link = what == GUI_ON_ACTIVATE ? gui_richview_link(w) : NULL;
    if (link && link[0]) {
        char t[360];
        gui_clip_set(link, strlen(link));
        snprintf(t, sizeof t, GUI_T("The link was copied: %s"), link);
        gui_set_text(status, t);
        say("link %s", link);
    }
}

static void build(void)
{
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *side, *right;
    win = gui_window(GUI_T("Chat"), 620, 440, 0);
    win->on_command = command;
    win->on_update = update;
    gui_command(win, C_CONNECT, GUI_T("&Connect"), "Ctrl+K", NULL, NULL);
    gui_command(win, C_DISCONNECT, GUI_T("&Disconnect"), NULL, NULL, NULL);
    gui_command(win, C_SETTINGS, GUI_T("&Settings..."), NULL, NULL, NULL);
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    gui_command(win, C_JOIN, GUI_T("&Join..."), "Ctrl+J", NULL, NULL);
    gui_command(win, C_CLOSE, GUI_T("&Leave and Close"), "Ctrl+W", NULL, NULL);
    gui_command(win, C_NEXT, GUI_T("&Next Tab"), "Alt+Down", NULL, NULL);
    gui_command(win, C_PREV, GUI_T("&Previous Tab"), "Alt+Up", NULL, NULL);
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&Chat"));
    gui_menu_command(m, C_CONNECT), gui_menu_command(m, C_DISCONNECT);
    gui_menu_sep(m);
    gui_menu_command(m, C_SETTINGS);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("C&hannel"));
    gui_menu_command(m, C_JOIN), gui_menu_command(m, C_CLOSE);
    gui_menu_sep(m);
    gui_menu_command(m, C_NEXT), gui_menu_command(m, C_PREV);
    topic = gui_label(win->root, "");
    side = gui_split(win->root, 0, 120);                /* the tabs | the rest */
    gui_grow(side, 1);
    tree = gui_vtree(side, &tabs);
    gui_on(tree, tree_on, 0);
    right = gui_split(side, 0, -110);                   /* the lines | the users, who keep their width */
    view = gui_richview(right);
    gui_richview_tight(view, 1);
    gui_on(view, view_on, 0);
    users = gui_vlist(right, 1, NULL, NULL, &user_rows);
    input = gui_field(win->root, 60);
    gui_on(input, typed, 0);
    gui_keys(input, input_keys);
    status = gui_status(win);
}

int main(int argc, char **argv)
{
    const char *home;
    int i;
    if (gui_init(argc, argv) != 0) {
        printf("CHAT no GLOS\n");
        return 1;
    }
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-demo"))
            demo = 1;
        else if (!strcmp(argv[i], "-silence") && i + 1 < argc)
            silence_s = atoi(argv[i + 1]) > 0 ? (unsigned)atoi(argv[i + 1]) : 120, i++;
    home = glos_home();
    if (home && home[0] && !demo)
        gui_path_join(cfg_path, sizeof cfg_path, home, "CHAT.CFG");
    settings_read();
    if (demo) {                                 /* (its own settings, the same every time) */
        snprintf(cfg.server, sizeof cfg.server, "demo");
        snprintf(cfg.nick, sizeof cfg.nick, "glos");
        snprintf(cfg.join, sizeof cfg.join, "#glos");
        cfg.account[0] = 0;
    }
    build();
    make_irc();
    gui_show(win, 1);
    gui_focus(input);
    refresh();
    gui_idle(tick, NULL, 100);
    if (demo || cfg.at_start)
        connect_now();
    gui_run();
    gui_idle(tick, NULL, 0);
    if (net)
        ircnet_close(net);
    irc_free(irc);
    printf("CHAT end\n");
    return 0;
}
