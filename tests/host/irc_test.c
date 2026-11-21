/* Host test for Chat's IRC (apps/shared/irc.c): lines taken apart (tags and their escapes, prefixes, the last
 * parameter); mIRC's codes made runs of one look; and whole sessions written out here, fed as a server would
 * send them: the user announced with and without SASL, a nick that is taken, channels joined (names with their
 * ranks, topics), people coming, going, renamed, ranked and put out, messages (to the channel, to the user,
 * actions, notices, questions answered), the server's time on a line, highlights and the counts for the tabs,
 * what the user types (said, and every command), long lines sent in pieces, Tab's nicks, the names compared as
 * the server compares them, a buffer closed, the connection ended; and whatever bytes may come: no harm. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/irc.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); \
                                                printf("\n"); fails++; } } while (0)

static char sent[64][520];              /* the lines sent, as they come */
static unsigned nsent, taken;
static unsigned events[8];
static struct irc_buffer *last_new;
static unsigned long clock_now = 1000;

static void a_send(void *ctx, const char *bytes, unsigned n)
{
    (void)ctx;
    if (nsent < 64 && n >= 2 && n < 520) {
        memcpy(sent[nsent], bytes, n - 2);
        sent[nsent][n - 2] = 0;
        if (bytes[n - 2] != '\r' || bytes[n - 1] != '\n')
            strcpy(sent[nsent], "(no line's end)");
        nsent++;
    }
}

static void an_event(void *ctx, int what, struct irc_buffer *b)
{
    (void)ctx;
    if (what >= 0 && what < 8)
        events[what]++;
    if (what == IRC_E_BUFFER)
        last_new = b;
}

static unsigned long a_clock(void *ctx) { (void)ctx; return clock_now; }

static unsigned long seed = 20261121ul;
static unsigned rnd(void)
{
    seed = seed * 1103515245ul + 12345ul;
    return (unsigned)((seed >> 8) & 0xFFFFFFul);
}

static int was_sent(const char *line)   /* the next line sent is that */
{
    int ok = taken < nsent && !strcmp(sent[taken], line);
    if (!ok)
        printf("  sent \"%s\", not \"%s\"\n", taken < nsent ? sent[taken] : "(nothing)", line);
    taken++;
    return ok;
}

static int no_more(void)                /* ... and nothing after it; the list begun anew */
{
    int ok = taken >= nsent;
    if (!ok)
        printf("  also sent \"%s\"\n", sent[taken]);
    nsent = taken = 0;
    return ok;
}

static void feed(struct irc *c, const char *lines) { irc_feed(c, lines, (unsigned)strlen(lines)); }

static const struct irc_line *newest(const struct irc_buffer *b) { return b && b->nlines ? irc_line_at(b, b->nlines - 1) : NULL; }

static struct irc *session(const char *account, const char *password)
{
    struct irc_opts o;
    memset(&o, 0, sizeof o);
    o.nick = "glos", o.alt = "glos2", o.user = "glosuser", o.real = "A GLOS user", o.account = account, o.password = password;
    o.send = a_send, o.event = an_event, o.now = a_clock;
    nsent = taken = 0;
    memset(events, 0, sizeof events);
    return irc_new(&o);
}

static struct irc *on(void)             /* a session with the user on, in #glos with four others */
{
    struct irc *c = session("", "");
    irc_start(c);
    feed(c, ":srv CAP * LS :multi-prefix server-time\r\n:srv CAP glos ACK :multi-prefix server-time\r\n"
            ":srv 001 glos :Welcome to TestNet, glos\r\n"
            ":srv 005 glos PREFIX=(qaohv)~&@%+ CHANTYPES=# CHANMODES=beI,k,l,imnpst NETWORK=TestNet :are supported\r\n"
            ":glos!glosuser@host JOIN #glos\r\n:srv 332 glos #glos :Graphical DOS, and \002why\002\r\n"
            ":srv 353 glos = #glos :plain +voiced glos @op\r\n:srv 353 glos = #glos :~@owner\r\n:srv 366 glos #glos :End of names\r\n");
    nsent = taken = 0;
    memset(events, 0, sizeof events);
    return c;
}

int main(void)
{
    struct irc *c;
    struct irc_buffer *b, *pm;
    struct irc_msg m;
    const struct irc_line *l;
    char line[600], out[80];
    unsigned i;

    /* ---- lines taken apart */
    strcpy(line, "@time=2026-11-21T01:02:03.000Z;msgid=a\\:b\\sc\\\\d;flag :nick!user@host PRIVMSG #chan :hello  world: all");
    CHECK(irc_parse(line, &m) == 0 && !strcmp(m.prefix, "nick!user@host") && !strcmp(m.nick, "nick") && !strcmp(m.command, "PRIVMSG")
          && m.nparams == 2 && !strcmp(m.param[0], "#chan") && !strcmp(m.param[1], "hello  world: all"), "a line with everything");
    CHECK(irc_tag(&m, "msgid", out, sizeof out) && !strcmp(out, "a;b c\\d") && irc_tag(&m, "time", out, sizeof out)
          && !strcmp(out, "2026-11-21T01:02:03.000Z") && irc_tag(&m, "flag", out, sizeof out) && !out[0] && !irc_tag(&m, "msg", out, sizeof out)
          && !irc_tag(&m, "none", out, sizeof out), "its tags, their escapes undone");
    strcpy(line, ":irc.example.org 001 me :Welcome");
    CHECK(irc_parse(line, &m) == 0 && !strcmp(m.nick, "irc.example.org") && m.nparams == 2 && !m.tags, "a server's line");
    strcpy(line, "PING :abc");
    CHECK(irc_parse(line, &m) == 0 && !m.prefix && !m.nick[0] && m.nparams == 1 && !strcmp(m.param[0], "abc"), "a line with no prefix");
    strcpy(line, "X 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17");
    CHECK(irc_parse(line, &m) == 0 && m.nparams == 16 && !strcmp(m.param[14], "15") && !strcmp(m.param[15], "16 17"), "more parameters than fit");
    strcpy(line, ":only");
    CHECK(irc_parse(line, &m) == -1, "no command");
    strcpy(line, "MODE #c  +o   nick ");
    CHECK(irc_parse(line, &m) == 0 && m.nparams == 3 && !strcmp(m.param[2], "nick"), "spaces in plenty");

    /* ---- mIRC's codes */
    {
        struct irc_span s[8];
        char plain[80];
        int n = irc_format("\002bold\002 plain \0034,1red on black\003 \035it\017 end", plain, sizeof plain, s, 8);
        CHECK(n == 6 && !strcmp(plain, "bold plain red on black it end"), "the codes taken out: %d runs, \"%s\"", n, plain);
        CHECK(s[0].start == 0 && s[0].len == 4 && s[0].flags == IRC_BOLD && s[0].fg == -1 && s[1].flags == 0 && s[1].len == 7
              && s[2].fg == 4 && s[2].bg == 1 && s[2].len == 12 && s[3].fg == -1 && s[3].bg == -1 && s[3].len == 1
              && s[4].flags == IRC_ITALIC && s[4].len == 2 && s[5].flags == 0 && s[5].start == 26 && s[5].len == 4, "each run's look");
        n = irc_format("\00312blue\0035\003,7,x \00399none \0033,\037u\026r\036s\021m", plain, sizeof plain, s, 8);
        CHECK(!strcmp(plain, "blue,7,x none ,ursm") && s[0].fg == 12 && s[0].len == 4 && s[1].fg == -1 && s[1].len == 5,
              "colours of two digits, a colour and no letters, a comma that is no ground's: \"%s\" %d", plain, n);
        n = irc_format("\004FF0000red\004 \00400ff00,0000FFboth", plain, sizeof plain, s, 8);
        CHECK(!strcmp(plain, "red both"), "colours by red, green and blue passed over: \"%s\"", plain);
        n = irc_format("a\002b\002c\002d\002e\002f", plain, sizeof plain, s, 3);
        CHECK(n == 3 && !strcmp(plain, "abcdef") && s[2].start == 2 && s[2].len == 4, "more runs than there is room for: the last has the rest");
        n = irc_format("\002x\tcut", plain, 4, NULL, 0);
        CHECK(n == 0 && !strcmp(plain, "xcu"), "only taken out, and cut to the room: \"%s\"", plain);
    }

    /* ---- the user announced: no account */
    c = session("", "");
    irc_start(c);
    CHECK(was_sent("CAP LS 302") && was_sent("NICK glos") && was_sent("USER glosuser 0 * :A GLOS user") && no_more()
          && irc_state(c) == IRC_REGISTERING && events[IRC_E_STATE] == 1, "the user announced");
    feed(c, ":srv CAP * LS :multi-prefix sasl=PLAIN,EXTERNAL server-time extended-join\r\n");
    CHECK(was_sent("CAP REQ :multi-prefix server-time") && no_more(), "the capabilities wanted, of those the server has");
    feed(c, ":srv CAP glos ACK :multi-prefix server-time\r\n");
    CHECK(was_sent("CAP END") && no_more(), "... granted");
    feed(c, "PING :token 1\r\n");
    CHECK(was_sent("PONG :token 1") && no_more(), "a ping answered");
    feed(c, ":srv 001 glos :Welcome\r\n:srv 005 glos PREFIX=(qaohv)~&@%+ CHANTYPES=# NETWORK=TestNet :are supported by this server\r\n"
            ":srv 372 glos :- the day's message\r\n");
    b = irc_buffer(c, 0);
    CHECK(irc_state(c) == IRC_ON && !strcmp(irc_nick(c), "glos") && !strcmp(irc_network(c), "TestNet") && no_more() && b->nlines == 2
          && !strcmp(newest(b)->text, "- the day's message") && newest(b)->kind == IRC_L_INFO && newest(b)->when == 1000, "on: %u lines", b->nlines);
    irc_free(c);

    /* ---- with an account: SASL, the capabilities in two lines */
    c = session("acct", "pw");
    irc_start(c);
    nsent = taken = 0;
    feed(c, ":srv CAP * LS * :server-time\r\n");
    CHECK(no_more(), "more capabilities to come");
    feed(c, ":srv CAP * LS :away-notify sasl\r\n");
    CHECK(was_sent("CAP REQ :server-time away-notify sasl") && no_more(), "... all of them");
    feed(c, ":srv CAP * ACK :server-time away-notify sasl\r\n");
    CHECK(was_sent("AUTHENTICATE PLAIN") && no_more(), "SASL begun");
    feed(c, "AUTHENTICATE +\r\n");
    CHECK(was_sent("AUTHENTICATE YWNjdABhY2N0AHB3") && no_more(), "the account and its password");
    feed(c, ":srv 900 glos glos!u@h acct :You are now logged in as acct\r\n:srv 903 glos :SASL authentication successful\r\n");
    CHECK(was_sent("CAP END") && no_more(), "... taken");
    feed(c, ":srv 001 glos :Welcome\r\n");
    CHECK(no_more() && irc_state(c) == IRC_ON, "on, and no password to the nick's keeper after it");
    irc_free(c);
    c = session("acct", "bad");
    irc_start(c);
    feed(c, ":srv CAP * LS :sasl\r\n:srv CAP * ACK :sasl\r\nAUTHENTICATE +\r\n");
    nsent = taken = 0;
    feed(c, ":srv 904 glos :SASL authentication failed\r\n");
    CHECK(was_sent("CAP END") && no_more() && newest(irc_buffer(c, 0))->kind == IRC_L_ERROR, "the account refused: said, and on without it");
    irc_free(c);
    c = session("acct", "pw");                  /* a server with no SASL: the password to NickServ once on */
    irc_start(c);
    feed(c, ":srv CAP * LS :multi-prefix\r\n:srv CAP * ACK :multi-prefix\r\n");
    nsent = taken = 0;
    feed(c, ":srv 001 glos :Welcome\r\n");
    CHECK(was_sent("PRIVMSG NickServ :IDENTIFY acct pw") && no_more(), "no SASL at the server");
    irc_free(c);
    c = session("", "");                        /* a server that knows no CAP at all, and the nick taken twice */
    irc_start(c);
    nsent = taken = 0;
    feed(c, ":srv 421 * CAP :Unknown command\r\n:srv 433 * glos :Nickname is already in use\r\n");
    CHECK(was_sent("NICK glos2") && no_more(), "the nick is taken: the other");
    feed(c, ":srv 433 * glos2 :Nickname is already in use\r\n");
    CHECK(was_sent("NICK glos__") && no_more(), "... taken too");
    feed(c, ":srv 001 glos__ :Welcome\r\n");
    CHECK(irc_state(c) == IRC_ON && !strcmp(irc_nick(c), "glos__"), "on under the nick the server gave");
    irc_free(c);

    /* ---- a channel */
    c = on();
    b = irc_find(c, "#GLOS");
    CHECK(b && irc_buffers(c) == 2 && b->kind == IRC_B_CHANNEL && b->joined && !strcmp(b->topic, "Graphical DOS, and \002why\002")
          && b->nusers == 5, "the channel joined: its topic, %u users", b ? b->nusers : 0);
    CHECK(!strcmp(b->users[0].nick, "owner") && !strcmp(b->users[0].rank, "~@") && !strcmp(b->users[1].nick, "op")
          && !strcmp(b->users[2].nick, "voiced") && !strcmp(b->users[2].rank, "+") && !strcmp(b->users[3].nick, "glos")
          && !strcmp(b->users[4].nick, "plain"), "its users by rank, then by name");
    feed(c, ":newbie!n@example.org JOIN #glos\r\n");
    l = newest(b);
    CHECK(b->nusers == 6 && l->kind == IRC_L_JOIN && !strcmp(l->nick, "newbie") && !strcmp(l->text, "n@example.org")
          && events[IRC_E_USERS] == 1 && b->unread == 0, "someone joins (no unread for that)");
    feed(c, ":op!o@h MODE #glos +lo-v 10 plain voiced\r\n");
    CHECK(!strcmp(b->users[2].nick, "plain") && !strcmp(b->users[2].rank, "@") && !b->users[5].rank[0] && !strcmp(b->users[5].nick, "voiced")
          && newest(b)->kind == IRC_L_MODE && !strcmp(newest(b)->text, "+lo-v 10 plain voiced"), "ranks given and taken, a mode with a number between");
    feed(c, ":owner!o@h MODE #glos -q+v owner owner\r\n");
    CHECK(!strcmp(b->users[0].nick, "op") && !strcmp(b->users[1].nick, "owner") && !strcmp(b->users[1].rank, "@+")
          && !strcmp(b->users[2].nick, "plain"), "a rank lost, another had: among its new equals by name");
    feed(c, ":newbie!n@example.org NICK Regular\r\n");
    CHECK(newest(b)->kind == IRC_L_NICK && !strcmp(newest(b)->nick, "newbie") && !strcmp(newest(b)->text, "Regular"), "renamed");
    feed(c, ":plain!p@h PART #glos :bye\r\n:voiced!v@h QUIT :Ping timeout\r\n:op!o@h KICK #glos Regular :enough\r\n");
    CHECK(b->nusers == 3 && newest(b)->kind == IRC_L_PART && !strcmp(newest(b)->nick, "Regular") && strstr(newest(b)->text, "put out by op: enough")
          && irc_line_at(b, b->nlines - 2)->kind == IRC_L_QUIT && !strcmp(irc_line_at(b, b->nlines - 3)->text, "bye"), "leaving, each way");
    feed(c, ":op!o@h TOPIC #glos :A new topic\r\n");
    CHECK(!strcmp(b->topic, "A new topic") && newest(b)->kind == IRC_L_TOPIC && events[IRC_E_TOPIC] == 1, "the topic changed");
    feed(c, ":op!o@h AWAY :lunch\r\n");
    CHECK(b->users[0].away == 1, "away");
    feed(c, ":op!o@h AWAY\r\n");
    CHECK(b->users[0].away == 0, "... and back");

    /* ---- messages */
    memset(events, 0, sizeof events);
    feed(c, ":op!o@h PRIVMSG #glos :hello all\r\n");
    l = newest(b);
    CHECK(l->kind == IRC_L_MSG && !strcmp(l->nick, "op") && !strcmp(l->text, "hello all") && !l->highlight && !l->mine && b->unread == 1
          && b->highlights == 0 && events[IRC_E_LINE] == 1 && l->when == 1000, "said in the channel");
    feed(c, ":op!o@h PRIVMSG #glos :GLOS: are you there? glossary and xglos are no names\r\n:owner!o@h PRIVMSG #glos :the glossary\r\n");
    CHECK(irc_line_at(b, b->nlines - 2)->highlight && !newest(b)->highlight && b->unread == 3 && b->highlights == 1, "this user named, as a word");
    feed(c, "@time=2026-11-21T01:02:03.000Z :op!o@h PRIVMSG #glos :\001ACTION waves\001\r\n");
    l = newest(b);
    CHECK(l->kind == IRC_L_ACTION && !strcmp(l->text, "waves") && l->when == 1795222923ul, "an action, at the server's time: %lu", l->when);
    irc_read(b);
    CHECK(!b->unread && !b->highlights, "read");
    feed(c, ":srv NOTICE glos :*** Looking up your hostname\r\n:NickServ!s@services NOTICE glos :This nick is registered\r\n");
    CHECK(irc_buffers(c) == 2 && newest(irc_buffer(c, 0))->kind == IRC_L_NOTICE && !strcmp(newest(irc_buffer(c, 0))->nick, "NickServ"),
          "notices: the server's buffer, no tab each");
    feed(c, ":friend!f@h PRIVMSG glos :psst\r\n");
    pm = irc_find(c, "friend");
    CHECK(pm && pm == last_new && irc_buffers(c) == 3 && pm->kind == IRC_B_PRIVATE && newest(pm)->highlight && pm->highlights == 1
          && events[IRC_E_BUFFER] == 1, "a private message: a conversation of its own");
    nsent = taken = 0;
    feed(c, ":friend!f@h PRIVMSG glos :\001VERSION\001\r\n:friend!f@h PRIVMSG glos :\001PING 12345\001\r\n:friend!f@h PRIVMSG glos :\001DCC SEND x 1 2 3\001\r\n");
    CHECK(was_sent("NOTICE friend :\001VERSION GLOS Chat\001") && was_sent("NOTICE friend :\001PING 12345\001") && no_more() && pm->nlines == 1,
          "questions answered; a file offered is not taken");
    feed(c, ":friend!f@h NICK buddy\r\n:buddy!f@h PRIVMSG glos :still me\r\n");
    CHECK(irc_find(c, "buddy") == pm && !irc_find(c, "friend") && pm->nlines == 3, "the conversation follows a new nick");
    feed(c, ":srv 401 glos nobody :No such nick/channel\r\n:srv 404 glos #glos :Cannot send to channel\r\n");
    CHECK(newest(irc_buffer(c, 0))->kind == IRC_L_ERROR && !strcmp(newest(irc_buffer(c, 0))->text, "nobody No such nick/channel")
          && newest(b)->kind == IRC_L_ERROR && !strcmp(newest(b)->text, "#glos Cannot send to channel"), "errors, each where it belongs");

    /* ---- what the user types */
    nsent = taken = 0;
    CHECK(irc_input(c, b, "hello there") == 0 && was_sent("PRIVMSG #glos :hello there") && no_more() && newest(b)->mine
          && !strcmp(newest(b)->nick, "glos") && !strcmp(newest(b)->text, "hello there"), "said");
    CHECK(irc_input(c, b, "/me waves back") == 0 && was_sent("PRIVMSG #glos :\001ACTION waves back\001") && no_more()
          && newest(b)->kind == IRC_L_ACTION, "/me");
    CHECK(irc_input(c, b, "//slash") == 0 && was_sent("PRIVMSG #glos :/slash") && no_more(), "a line that begins with a slash");
    CHECK(irc_input(c, b, "/msg someone hi you") == 0 && was_sent("PRIVMSG someone :hi you") && no_more() && irc_find(c, "someone")
          && newest(irc_find(c, "someone"))->mine, "/msg: a conversation begun");
    CHECK(irc_input(c, b, "/query other") == 0 && no_more() && irc_find(c, "other") && !irc_find(c, "other")->nlines, "/query");
    CHECK(irc_input(c, b, "/notice op psst") == 0 && was_sent("NOTICE op :psst") && no_more() && !irc_find(c, "op"), "/notice");
    CHECK(irc_input(c, b, "/join #other key") == 0 && was_sent("JOIN #other key") && irc_input(c, b, "/part bye for now") == 0
          && was_sent("PART #glos :bye for now") && irc_input(c, b, "/part #other") == 0 && was_sent("PART #other") && no_more(), "/join, /part");
    CHECK(irc_input(c, b, "/topic A topic: this") == 0 && was_sent("TOPIC #glos :A topic: this") && irc_input(c, b, "/topic") == 0
          && was_sent("TOPIC #glos") && irc_input(c, b, "/NICK newer") == 0 && was_sent("NICK newer") && irc_input(c, b, "/away gone fishing") == 0
          && was_sent("AWAY :gone fishing") && irc_input(c, b, "/away") == 0 && was_sent("AWAY") && no_more(), "/topic, /nick, /away");
    CHECK(irc_input(c, b, "/kick op too loud") == 0 && was_sent("KICK #glos op :too loud") && irc_input(c, b, "/mode +m") == 0
          && was_sent("MODE #glos +m") && irc_input(c, b, "/mode glos +i") == 0 && was_sent("MODE glos +i") && irc_input(c, b, "/names") == 0
          && was_sent("NAMES #glos") && irc_input(c, b, "/whois op") == 0 && was_sent("WHOIS op") && irc_input(c, b, "/raw PING :x") == 0
          && was_sent("PING :x") && no_more(), "/kick, /mode, /names, any other, /raw");
    CHECK(irc_input(c, irc_buffer(c, 0), "nowhere to say this") == -1 && irc_input(c, b, "/me") == -1 && irc_input(c, b, "/msg") == -1
          && irc_input(c, b, "/join") == -1 && irc_input(c, irc_buffer(c, 0), "/topic x") == -1 && irc_input(c, b, "   ") == 0 && no_more(),
          "what can't be said or done");
    {                                           /* a long line: in pieces, cut at spaces, never within a character */
        char big[1200];
        unsigned n = 0, total = 0;
        while (n < 1100)
            n += (unsigned)sprintf(big + n, "w\xC3\xB6rter und W\xC3\xB6rter ");
        big[--n] = 0;
        nsent = taken = 0;
        irc_input(c, b, big);
        for (i = 0; i < nsent; i++) {
            const char *text = strstr(sent[i], " :");
            size_t len = text ? strlen(text + 2) : 0;
            total += (unsigned)len + (i + 1 < nsent);
            CHECK(text && len <= 380 && len > 250 - 250 * (i + 1 == nsent) && ((unsigned char)text[2] & 0xC0) != 0x80 && text[2] != ' '
                  && text[len + 1] != ' ', "piece %u of a long line: %u bytes", i, (unsigned)len);
        }
        CHECK(nsent == 3 && total == n, "a long line in %u pieces, %u bytes of %u", nsent, total, n);
        nsent = taken = 0;
    }
    CHECK(irc_input(c, b, "a line\r\nQUIT :smuggled") == 0 && was_sent("PRIVMSG #glos :a line  QUIT :smuggled") && no_more(),
          "a line's end typed is no line's end sent");

    /* ---- Tab's nicks: those who spoke latest first, then by name; round and round */
    feed(c, ":pat!p@h JOIN #glos\r\n:paula!p@h JOIN #glos\r\n:Pim!p@h JOIN #glos\r\n:paula!p@h PRIVMSG #glos :one\r\n:pat!p@h PRIVMSG #glos :two\r\n");
    CHECK(!strcmp(irc_complete(b, "p", 0), "pat") && !strcmp(irc_complete(b, "P", 1), "paula") && !strcmp(irc_complete(b, "p", 2), "Pim")
          && !strcmp(irc_complete(b, "p", 3), "pat") && !strcmp(irc_complete(b, "pau", 5), "paula") && !irc_complete(b, "zz", 0)
          && !strcmp(irc_complete(b, "", 0), "pat"), "Tab's nicks");

    /* ---- names compared as the server compares them */
    CHECK(irc_same(c, "[Nick]\\~", "{nICK}|^") && !irc_same(c, "nick", "nick2") && !irc_same(c, "nick2", "nick"), "names, by RFC 1459's rule");
    feed(c, ":srv 005 glos CASEMAPPING=ascii :are supported\r\n");
    CHECK(!irc_same(c, "[Nick]", "{nick}") && irc_same(c, "NICK", "nick"), "... and by ASCII's, where the server says so");

    /* ---- more lines than are kept; a line too long; a buffer closed; the connection ended */
    for (i = 0; i < IRC_LINES + 50; i++) {
        sprintf(line, ":pat!p@h PRIVMSG #glos :line %u\r\n", i);
        feed(c, line);
    }
    CHECK(b->nlines == IRC_LINES && !strcmp(newest(b)->text, "line 449") && !strcmp(irc_line_at(b, 0)->text, "line 50") && !irc_line_at(b, IRC_LINES),
          "the last %d lines kept", IRC_LINES);
    {
        static char huge[6000];
        unsigned long before = b->total;
        memset(huge, 'x', sizeof huge - 1);
        memcpy(huge, ":pat!p@h PRIVMSG #glos :", 24);
        irc_feed(c, huge, sizeof huge - 1);
        feed(c, "\r\n:pat!p@h PRIVMSG #glos :after it\r\n");
        CHECK(b->total == before + 1 && !strcmp(newest(b)->text, "after it"), "a line too long is passed over, the next is read");
    }
    feed(c, ":pat!p@h PRIVMSG #glos :in two ");
    feed(c, "pieces\r");
    feed(c, "\n");
    CHECK(!strcmp(newest(b)->text, "in two pieces"), "a line that comes in pieces");
    nsent = taken = 0, memset(events, 0, sizeof events);
    irc_close(c, pm);
    CHECK(no_more() && events[IRC_E_CLOSED] == 1 && !irc_find(c, "buddy"), "a conversation put away");
    irc_close(c, b);
    CHECK(was_sent("PART #glos") && no_more() && !irc_find(c, "#glos"), "a channel left by closing it");
    irc_close(c, irc_buffer(c, 0));
    CHECK(irc_buffer(c, 0) && irc_buffers(c) == 3, "the server's buffer stays");
    feed(c, ":glos!g@h JOIN #again\r\n:srv 353 glos = #again :glos @op\r\n:srv 366 glos #again :End\r\n");
    b = irc_find(c, "#again");
    irc_stopped(c);
    CHECK(irc_state(c) == IRC_OFF && b && !b->joined && !b->nusers && newest(b)->kind == IRC_L_ERROR && irc_input(c, b, "hello") == -1,
          "the connection ended");
    irc_start(c);
    CHECK(irc_state(c) == IRC_REGISTERING, "... and begun again");
    irc_free(c);

    /* ---- whatever bytes come: a session's lines with bytes changed, and bytes of no sense at all */
    {
        static const char *const lines[] = {
            ":srv CAP * LS * :multi-prefix sasl=PLAIN server-time away-notify\r\n", ":srv CAP * ACK :multi-prefix sasl\r\n", "AUTHENTICATE +\r\n",
            ":srv 903 glos :ok\r\n", ":srv 001 glos :Welcome\r\n", ":srv 005 glos PREFIX=(qaohv)~&@%+ CHANTYPES=#& CHANMODES=beI,k,l,imnpst CASEMAPPING=rfc1459 NETWORK=N :ok\r\n",
            ":glos!g@h JOIN #a\r\n", ":srv 332 glos #a :topic\r\n", ":srv 353 glos = #a :@a +b ~@c d e f glos\r\n", ":srv 366 glos #a :end\r\n",
            ":a!a@h MODE #a +ov-o+k b c a key\r\n", ":b!b@h PRIVMSG #a :\00304glos\003: hi \001\r\n", ":b!b@h NICK bb\r\n", ":c!c@h KICK #a d :out\r\n",
            ":e!e@h QUIT :gone\r\n", ":f!f@h PART #a\r\n", "@time=2026-11-21T01:02:03.000Z;x=\\s\\: :bb!b@h PRIVMSG glos :\001ACTION x\001\r\n",
            ":srv 433 * glos :taken\r\n", ":a!a@h TOPIC #a :new\r\n", ":a!a@h AWAY :x\r\n", "PING :p\r\n", "ERROR :Closing link\r\n",
            ":glos!g@h NICK other\r\n", ":a!a@h KICK #a glos\r\n", ":srv 301 glos bb :away\r\n" };
        static const char *const typed[] = { "hi", "/me x", "/msg a b", "/query q", "/part", "/topic t", "/kick a", "/mode +o a", "/names", "/x y z",
                                             "/join #b", "//", "/", "/msg", "/raw" };
        unsigned long rounds = 0;
        unsigned k, r;
        for (r = 0; r < 3000; r++) {
            c = session(r & 1 ? "acct" : "", r & 2 ? "pw" : "");
            if (r % 7)
                irc_start(c);
            for (k = 0; k < 40; k++) {
                char copy[400];
                unsigned n, changes, which = rnd() % 25;
                strcpy(copy, lines[which]);
                n = (unsigned)strlen(copy);
                for (changes = rnd() % 4; changes; changes--) {
                    unsigned at = rnd() % n;
                    copy[at] = rnd() % 3 ? (char)rnd() : " :!@#+-,=\001\003\r\n"[rnd() % 13];
                }
                irc_feed(c, copy, rnd() % 9 ? n : rnd() % (n + 1));
                if (rnd() % 5 == 0)
                {
                    struct irc_buffer *where = irc_buffer(c, rnd() % 4);
                    irc_input(c, where, typed[rnd() % 15]);
                }
                if (rnd() % 40 == 0 && irc_buffers(c) > 1)
                    irc_close(c, irc_buffer(c, 1 + rnd() % (irc_buffers(c) - 1)));
                if (rnd() % 60 == 0)
                    irc_stopped(c);
                for (i = 0; i < irc_buffers(c); i++) {
                    struct irc_buffer *x = irc_buffer(c, i);
                    char plain[64];
                    struct irc_span s[4];
                    if (x->nlines)
                        irc_format(newest(x)->text, plain, sizeof plain, s, 4);
                    irc_complete(x, "a", rnd() % 5);
                }
            }
            irc_free(c);
            rounds++;
            nsent = taken = 0;
        }
        for (r = 0; r < 200; r++) {             /* ... and no sense at all */
            char junk[700];
            c = session("", "");
            irc_start(c);
            for (k = 0; k < sizeof junk; k++)
                junk[k] = rnd() % 5 ? (char)rnd() : "\r\n :"[rnd() % 4];
            irc_feed(c, junk, sizeof junk);
            irc_free(c);
            nsent = taken = 0;
        }
        CHECK(rounds == 3000, "sessions with bytes changed");
        printf("irc_test: %lu changed sessions\n", rounds);
    }
    printf("irc_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
