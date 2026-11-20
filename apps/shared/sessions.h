/* The saved sessions (phase F; the owner's choice of 2026-11-20: saved connections and their settings, as PuTTY
   has them; docs/v1/apps/term.md): a name for a computer to log in to and all that goes with it. One file,
   SESSIONS.CFG beside SSH's key and known hosts, read by the Terminal (which edits it), by SSH.EXE (`ssh NAME`)
   and by Files (its places on other computers).

   The file is text, a section a session:

       [office]
       kind = telnet                    (or raw, or serial; left out: ssh)
       charset = 437                    (the far side's DOS code page; left out: its bytes as they are)
       host = 10.0.0.5
       port = 2222
       user = me
       key = C:\KEYS\WORK
       command = top
       tunnels = 8080:intranet:80 *:2222:10.0.0.9:22
       keepalive = 60
       speed = 9600                     (a serial session's, whose host is its port: COM1 to COM4)
       frame = 8N1                      (its bits, parity N, E or O, and stop bits)
       flow = rts                       (or xon; left out: none)
       log = C:\LOGS\OFFICE.LOG
       colours = 2
       font = 0
       lines = 1000

   A line that is not understood is left out when the file is written again. Nothing secret is kept: a key is
   its file's name, and there are no passwords. */
#ifndef SESSIONS_H
#define SESSIONS_H

#define SESSIONS_MAX 32
#define SESSION_TUNNELS 4               /* -L's a session (SSH.EXE's own limit) */

/* A session's kind: SSH.EXE's; TELNET.EXE's (raw: -r); SERIAL.EXE's, a COM port in a computer's place. */
enum { SESSION_SSH, SESSION_TELNET, SESSION_RAW, SESSION_SERIAL };

struct session {
    char name[24];                      /* a word: no spaces, and nothing an address has (@ : / \) */
    int kind;                           /* SESSION_ */
    int charset;                        /* Telnet and raw: the far side's code page (437, ...); 0: as they are */
    char host[64], user[24];
    int port;                           /* 0: SSH's own (22) */
    char key[100];                      /* a private key's file; "": SSH's own (ID_ED25 beside the file) */
    char command[128];                  /* run there in place of a shell; "": a shell */
    char tunnels[200];                  /* -L's as ssh takes them, a space between */
    int keepalive;                      /* seconds of silence before a packet that keeps the connection up; 0: none */
    char log[100];                      /* a file that what the session writes is added to (sesslog.h); "": none */
    /* A serial session's (its host is the port's name, COM1 to COM4): */
    int speed;                          /* bits a second (50 to 115200; 0: 115200) */
    char frame[4];                      /* "8N1": the bits (5 to 8), the parity (N, E, O), the stop bits (1, 2); "": 8N1 */
    int flow;                           /* 0 none, 1 the RTS and CTS wires, 2 XON and XOFF */
    /* The Terminal's, for the session's tab: */
    int colours;                        /* a colour scheme (SESSION_COLOURS of them; 0: the theme's) */
    int font;                           /* its text: 0 the theme's size, 1 small, 2 normal, 3 large */
    int lines;                          /* kept above the screen (100 to 20000; 0: the Terminal's own, 1000) */
};

#define SESSION_COLOURS 6
extern const char *const session_colour_names[SESSION_COLOURS];

void session_defaults(struct session *s);
int session_name_ok(const char *name);          /* 1: a session can be called that */
int session_same(const struct session *a, const struct session *b);     /* 1: alike in everything */
/* One tunnel's words, [ADDRESS:]PORT:HOST:HOSTPORT (ADDRESS: * or 0.0.0.0, every computer's; 127.0.0.1,
   localhost or nothing, this PC's): 0 and its parts (open: 1, 0, or -1 for none said; any of them NULL), or -1. */
int session_tunnel(const char *spec, int len, int *open, unsigned *port, char *host, int room, unsigned *hport);
int session_tunnels_ok(const char *all);        /* the tunnels' line: how many (0 to SESSION_TUNNELS), or -1 */
int session_com(const char *host);              /* a serial session's port: 1 to 4 for COM1 to COM4 (any case), or 0 */
/* A frame's words ("8N1", "7e2"; "": 8N1): 0 and its parts (parity 0 none, 1 odd, 2 even), or -1. */
int session_frame(const char *frame, unsigned *bits, unsigned *parity, unsigned *stop);

void sessions_path(char *out, int room, const char *ssh_dir);   /* SESSIONS.CFG there */
void sessions_files(void *(*open)(const char *path, const char *mode));        /* fopen's stand-in (the toolkit's
                                                                           gui_fopen, for an app of its) */
int sessions_load(const char *path, struct session *out, int room);    /* how many (no file: 0) */
int sessions_save(const char *path, const struct session *s, int n);   /* 0, or -1 (it can't be written) */
int sessions_find(const struct session *s, int n, const char *name);   /* which, the case aside; -1: none */

#endif
