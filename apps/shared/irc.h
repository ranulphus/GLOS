/* IRC, for Chat (phase F; docs/v1/apps/chat.md): GLOS's own protocol code. A connection's whole state as a
   machine with no I/O of its own: the bytes the server sent are fed in, the bytes to send come out through a
   call, and what there is to show (channels and private conversations, their lines, their users) is kept here
   and told by events. One code for the app and for the host's tests, which feed it written-out sessions.

   From RFC 1459 and 2812 as servers speak them today, and the IRCv3 specifications for what is asked of the
   server where it has it: capability negotiation (CAP, version 302), SASL PLAIN, server-time, multi-prefix,
   away-notify. */
#ifndef IRC_H
#define IRC_H

/* ---- a line taken apart */
struct irc_msg {
    char *tags;                         /* its tags as sent ("time=...;account=x"), or NULL */
    char *prefix;                       /* who it is from ("nick!user@host", or a server's name), or NULL */
    char nick[40];                      /* ... the nick of that (a server's name whole) */
    char *command;                      /* "PRIVMSG", "001" */
    char *param[16];
    int nparams;
};
/* A line (no CR LF; changed where it lies: the message's strings point into it). 0, or -1: no command in it. */
int irc_parse(char *line, struct irc_msg *m);
/* A tag's value, its escapes undone: 1 and out; 0: no such tag. */
int irc_tag(const struct irc_msg *m, const char *key, char *out, int room);

/* ---- text with mIRC's codes (bold, colours, ...) */
#define IRC_BOLD 1
#define IRC_ITALIC 2
#define IRC_UNDERLINE 4
#define IRC_REVERSE 8
#define IRC_STRIKE 16
#define IRC_MONO 32
struct irc_span {
    unsigned start, len;                /* bytes of the plain text */
    unsigned char flags;
    signed char fg, bg;                 /* one of the 16 colours (0 white, 1 black, 2 blue, 3 green, 4 red, 5 brown,
                                           6 purple, 7 orange, 8 yellow, 9 light green, 10 teal, 11 cyan, 12 light
                                           blue, 13 pink, 14 grey, 15 light grey), or -1: the window's own */
};
/* text's letters without its codes into plain (room bytes), and the runs of one look into spans (max of them):
   how many runs. spans NULL: the codes are only taken out. */
int irc_format(const char *text, char *plain, int room, struct irc_span *spans, int max);

/* ---- a connection */
enum { IRC_L_MSG, IRC_L_ACTION, IRC_L_NOTICE, IRC_L_JOIN, IRC_L_PART, IRC_L_QUIT, IRC_L_NICK, IRC_L_MODE, IRC_L_TOPIC,
       IRC_L_INFO, IRC_L_ERROR };
struct irc_line {
    unsigned long when;                 /* seconds since 1970, UTC: the server's time for it where it says, else ours */
    unsigned char kind;                 /* IRC_L_ */
    unsigned char mine;                 /* said by this user */
    unsigned char highlight;            /* it names this user */
    char nick[40];                      /* who ("" for the server's own words) */
    char *text;                         /* as sent: mIRC's codes still in it (irc_format) */
};

struct irc_user {
    char nick[40];
    char rank[6];                       /* its prefixes in the channel, the highest first ("@", "@+", "") */
    unsigned char away;
    unsigned long spoke;                /* when it last spoke here (a count that grows): Tab offers the latest first */
};

enum { IRC_B_SERVER, IRC_B_CHANNEL, IRC_B_PRIVATE };
#define IRC_LINES 400
struct irc_buffer {                     /* what a tab shows: the server's own words, a channel, a private conversation */
    char name[64];
    int kind;
    int joined;                         /* a channel this user is in now */
    char topic[512];
    struct irc_user *users;             /* a channel's, in their order: by rank, then by name */
    unsigned nusers, room;
    struct irc_line line[IRC_LINES];    /* the last lines, a ring: irc_line_at */
    unsigned first, nlines;
    unsigned long total;                /* lines there have been (the UI knows by it how many are new) */
    unsigned unread, highlights;        /* since irc_read(): for the tree's badges */
    void *user;                         /* the app's */
};
const struct irc_line *irc_line_at(const struct irc_buffer *b, unsigned i);    /* i: 0 the oldest kept */

enum { IRC_E_LINE = 1,                  /* a line was added to the buffer */
       IRC_E_USERS,                     /* its users changed */
       IRC_E_TOPIC,                     /* its topic changed (the server's buffer: the network's name is known) */
       IRC_E_BUFFER,                    /* there is a new buffer (this one) */
       IRC_E_CLOSED,                    /* this buffer is about to go */
       IRC_E_STATE,                     /* the connection's state changed (irc_state) */
       IRC_E_NICK };                    /* this user's nick changed */
enum { IRC_OFF, IRC_REGISTERING, IRC_ON };

struct irc_opts {
    const char *nick, *alt;             /* the nick wanted, and another if it is taken ("": the nick with "_" after it) */
    const char *user, *real;            /* the user name and the "real name" */
    const char *account, *password;     /* SASL PLAIN's ("" or NULL: none); with no SASL at the server the password is
                                           given to NickServ once on */
    const char *server_password;        /* PASS, for servers that want one ("" or NULL: none) */
    void (*send)(void *ctx, const char *bytes, unsigned n);    /* bytes to the server */
    void (*event)(void *ctx, int what, struct irc_buffer *b);
    unsigned long (*now)(void *ctx);    /* seconds since 1970, UTC */
    void *ctx;
};

struct irc;
struct irc *irc_new(const struct irc_opts *o);  /* (its strings are copied) NULL: no memory */
void irc_free(struct irc *c);
void irc_start(struct irc *c);                  /* the connection is made: this user is announced */
void irc_feed(struct irc *c, const char *bytes, unsigned n);    /* what the server sent, in any pieces */
void irc_stopped(struct irc *c);                /* the connection ended (said in every buffer) */

int irc_state(const struct irc *c);
const char *irc_nick(const struct irc *c);      /* this user's, as the server has it now */
const char *irc_network(const struct irc *c);   /* the network's name as the server says it ("" until then) */
unsigned irc_buffers(const struct irc *c);
struct irc_buffer *irc_buffer(struct irc *c, unsigned i);       /* 0: the server's */
struct irc_buffer *irc_find(struct irc *c, const char *name);   /* by its name, as the server compares names */
void irc_read(struct irc_buffer *b);            /* its lines have been seen: the counts are 0 again */
void irc_close(struct irc *c, struct irc_buffer *b);    /* a channel left, a conversation put away (not the server's) */

/* What the user typed in a buffer: said there, or, begun with a slash, a command: /join #channel [key], /part
   [message], /msg nick text, /query nick, /me text, /nick new, /topic [text], /notice whom text, /away [text],
   /whois nick, /kick nick [why], /mode ..., /names, /quit [message], /raw line; any other is sent as it is.
   "//text" says "/text". 0; -1: there is nowhere to say it (the server's buffer, or not connected). */
int irc_input(struct irc *c, struct irc_buffer *b, const char *text);

/* A nick of the buffer's that begins as `begun` does, the nth such (from 0, round and round) in the order they
   last spoke (then by name): for Tab. NULL: none. */
const char *irc_complete(const struct irc_buffer *b, const char *begun, unsigned nth);

int irc_same(const struct irc *c, const char *a, const char *b);       /* two names the same, as the server compares */

#endif
