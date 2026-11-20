/* The saved sessions' file (apps/shared/sessions.c): names, a tunnel's words, what is written read back, a
 * file written by hand with its faults, and files of rubbish, which must do no harm.
 *
 *   sessions_test WORKDIR      (prints "sessions_test: N failures (M checks)") */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/sessions.h"

static int fails, checks;

#define CHECK(c, ...) do { checks++; if (!(c)) { printf("sessions_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static void put(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    fwrite(text, 1, strlen(text), f);
    fclose(f);
}

int main(int argc, char **argv)
{
    static struct session s[SESSIONS_MAX + 2], back[SESSIONS_MAX + 2];
    char path[400], host[80];
    unsigned port, hport, seed = 12345;
    int open, n, i, j;
    if (argc < 2)
        return 2;

    /* names */
    CHECK(session_name_ok("office") && session_name_ok("a") && session_name_ok("web-1.example"), "good names");
    CHECK(!session_name_ok("") && !session_name_ok("two words") && !session_name_ok("me@there") && !session_name_ok("a:b")
          && !session_name_ok("C:\\X") && !session_name_ok("-N") && !session_name_ok("[x]") && !session_name_ok("a=b")
          && !session_name_ok("abcdefghijklmnopqrstuvwx"), "bad names");
    CHECK(session_name_ok("abcdefghijklmnopqrstuvw"), "23 characters");

    /* a tunnel's words */
    open = 7, port = hport = 0;
    CHECK(session_tunnel("8080:intranet:80", -1, &open, &port, host, sizeof host, &hport) == 0 && open == -1 && port == 8080
          && hport == 80 && !strcmp(host, "intranet"), "a tunnel: %d %u %s %u", open, port, host, hport);
    CHECK(session_tunnel("*:2222:10.0.0.9:22", -1, &open, &port, host, sizeof host, &hport) == 0 && open == 1 && port == 2222
          && hport == 22 && !strcmp(host, "10.0.0.9"), "an open one: %d %u %s %u", open, port, host, hport);
    CHECK(session_tunnel("0.0.0.0:1:h:65535", -1, &open, &port, 0, 0, &hport) == 0 && open == 1 && port == 1 && hport == 65535,
          "0.0.0.0");
    CHECK(session_tunnel("127.0.0.1:5:h:6", -1, &open, 0, 0, 0, 0) == 0 && open == 0, "127.0.0.1");
    CHECK(session_tunnel("localhost:5:h:6", -1, &open, 0, 0, 0, 0) == 0 && open == 0, "localhost");
    CHECK(session_tunnel(":5:h:6", -1, &open, 0, 0, 0, 0) == 0 && open == 0, "an empty address");
    CHECK(session_tunnel("8080:intranet:80 and more", 16, 0, &port, 0, 0, 0) == 0 && port == 8080, "by its length");
    {
        static const char *const bad[] = { "", "80", "80:h", "80:h:", ":h:80", "0:h:80", "65536:h:80", "80:h:0", "80:h:65536",
            "80::80", "x:h:80", "80:h:x", "8 0:h:80", "1.2.3.4:80:h:80", "*:*:80:h:80", "80:h h:80", "80:h:80:", "123456:h:80",
            "80:h:8 0" };
        for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); i++)
            CHECK(session_tunnel(bad[i], -1, 0, 0, 0, 0, 0) == -1, "a bad tunnel taken: %s", bad[i]);
    }
    CHECK(session_tunnel("80:a-host-with-a-name-as-long-as-it-may-be:80", -1, 0, 0, host, 8, 0) == -1, "no room for its host");
    CHECK(session_tunnels_ok("") == 0 && session_tunnels_ok("   ") == 0 && session_tunnels_ok("1:a:2") == 1
          && session_tunnels_ok(" 1:a:2  *:3:b:4 ") == 2 && session_tunnels_ok("1:a:2 2:a:2 3:a:2 4:a:2") == 4, "lines of tunnels");
    CHECK(session_tunnels_ok("1:a:2 nonsense") == -1 && session_tunnels_ok("1:a:2 2:a:2 3:a:2 4:a:2 5:a:2") == -1,
          "bad lines of tunnels");

    /* the file's name */
    sessions_path(path, sizeof path, "C:\\GLOS\\SSH");
    CHECK(!strcmp(path, "C:\\GLOS\\SSH\\SESSIONS.CFG"), "path: %s", path);
    sessions_path(path, sizeof path, "C:\\GLOS\\SSH\\");
    CHECK(!strcmp(path, "C:\\GLOS\\SSH\\SESSIONS.CFG"), "path with its backslash: %s", path);

    /* written, and read back the same */
    snprintf(path, sizeof path, "%s/SESSIONS.CFG", argv[1]);
    remove(path);
    CHECK(sessions_load(path, back, SESSIONS_MAX) == 0, "no file: none");
    for (i = 0; i < SESSIONS_MAX; i++) {
        session_defaults(&s[i]);
        snprintf(s[i].name, sizeof s[i].name, "session-%d", i);
        snprintf(s[i].host, sizeof s[i].host, "host%d.example.org", i);
        if (i & 1)
            snprintf(s[i].user, sizeof s[i].user, "user%d", i);
        if (i & 2)
            s[i].port = 2000 + i;
        if (i & 4)
            snprintf(s[i].key, sizeof s[i].key, "C:\\KEYS\\KEY%d", i);
        if (i & 8)
            snprintf(s[i].command, sizeof s[i].command, "echo a = b; top -d %d", i);
        if (i & 16)
            snprintf(s[i].tunnels, sizeof s[i].tunnels, "%d:web:80 *:%d:10.0.0.%d:22", 8000 + i, 2200 + i, i);
        s[i].keepalive = i % 3 ? 30 * i : 0;
        s[i].kind = i % 5 == 1 ? SESSION_TELNET : i % 5 == 2 ? SESSION_RAW : SESSION_SSH;
        s[i].charset = i % 4 == 1 ? 437 : i % 4 == 2 ? 850 : 0;
        if (i % 7 == 3)
            snprintf(s[i].log, sizeof s[i].log, "C:\\LOGS\\S%d.LOG", i);
        s[i].colours = i % SESSION_COLOURS, s[i].font = i % 4, s[i].lines = i % 5 ? 100 * (i + 1) : 0;
    }
    CHECK(sessions_save(path, s, SESSIONS_MAX) == 0, "saved");
    n = sessions_load(path, back, SESSIONS_MAX + 2);
    CHECK(n == SESSIONS_MAX && !memcmp(s, back, sizeof s[0] * SESSIONS_MAX), "read back: %d of them", n);
    CHECK(sessions_load(path, back, 5) == 5, "no more than there is room for");
    CHECK(sessions_find(s, SESSIONS_MAX, "session-7") == 7 && sessions_find(s, SESSIONS_MAX, "SESSION-31") == 31
          && sessions_find(s, SESSIONS_MAX, "session-") == -1 && sessions_find(s, SESSIONS_MAX, "session-77") == -1
          && sessions_find(s, 0, "session-7") == -1, "found by name");
    snprintf(path, sizeof path, "%s/no/such/folder/SESSIONS.CFG", argv[1]);
    CHECK(sessions_save(path, s, 1) == -1, "a file that can't be written");

    /* one written by hand: CR LF, comments, blanks, things unknown, numbers out of range, a name twice, a bad
       name, lines before any session, tunnels that are none */
    snprintf(path, sizeof path, "%s/HAND.CFG", argv[1]);
    put(path, "host = nobody's\r\n; a comment\r\n\r\n  [ Office ]  \r\n  host=10.0.0.5\r\nport = 2222 \r\nuser =  me \r\n"
              "colour = 3\r\ncolours = 3\r\nfont = 9\r\nlines = 50\r\nkeepalive = 60\r\nwhat is this\r\n"
              "tunnels = 8080:intranet:80\r\ncommand = echo x = y\r\nlog = C:\\LOGS\\OFFICE.LOG\r\n"
              "[office]\r\nhost = second\r\n[two words]\r\nhost = bad\r\n[home]\r\nkind = telnet\r\ncharset = 437\r\n"
              "host = home.example\r\nport = 0\r\n"
              "tunnels = nonsense\r\nkey = C:\\KEYS\\HOME\r\n[broken\r\nhost = never\r\n");
    n = sessions_load(path, back, SESSIONS_MAX);
    CHECK(n == 2, "by hand: %d sessions", n);
    CHECK(!strcmp(back[0].name, "Office") && !strcmp(back[0].host, "10.0.0.5") && back[0].port == 2222
          && !strcmp(back[0].user, "me") && back[0].colours == 3 && back[0].font == 0 && back[0].lines == 0
          && back[0].keepalive == 60 && !strcmp(back[0].tunnels, "8080:intranet:80") && !strcmp(back[0].command, "echo x = y")
          && !strcmp(back[0].log, "C:\\LOGS\\OFFICE.LOG"),
          "by hand, the first: %s %s %d %s %d %d %d %d %s %s", back[0].name, back[0].host, back[0].port, back[0].user,
          back[0].colours, back[0].font, back[0].lines, back[0].keepalive, back[0].tunnels, back[0].command);
    CHECK(back[0].kind == SESSION_SSH && back[0].charset == 0 && back[1].kind == SESSION_TELNET && back[1].charset == 437,
          "by hand, their kinds: %d %d, %d %d", back[0].kind, back[0].charset, back[1].kind, back[1].charset);
    CHECK(!strcmp(back[1].name, "home") && !strcmp(back[1].host, "home.example") && back[1].port == 0 && !back[1].tunnels[0]
          && !strcmp(back[1].key, "C:\\KEYS\\HOME"), "by hand, the second: %s %s %d %s %s", back[1].name, back[1].host,
          back[1].port, back[1].tunnels, back[1].key);

    /* rubbish: nothing breaks, and what comes of it is sessions as they may be */
    snprintf(path, sizeof path, "%s/RUBBISH.CFG", argv[1]);
    for (j = 0; j < 300; j++) {
        static char junk[6000];
        static const char *const bits[] = { "[", "]", "=", "\n", "\r\n", "host", "port", "tunnels", " ", "*:1:a:2", "65536", ";",
                                            "lines", "colours", "-1", "99999999999999999999", "command", "key" };
        int len = 0;
        FILE *f;
        while (len < (int)sizeof junk - 40) {
            seed = seed * 1103515245u + 12345u;
            if ((seed >> 16) % 3)
                len += sprintf(junk + len, "%s", bits[(seed >> 8) % (sizeof bits / sizeof bits[0])]);
            else
                junk[len++] = (char)((seed >> 20) % 255 + 1);
            if (!((seed >> 12) % 400))
                break;
        }
        f = fopen(path, "wb");
        fwrite(junk, 1, (size_t)len, f);
        fclose(f);
        n = sessions_load(path, back, SESSIONS_MAX);
        CHECK(n >= 0 && n <= SESSIONS_MAX, "rubbish %d: %d sessions", j, n);
        for (i = 0; i < n; i++)
            CHECK(session_name_ok(back[i].name) && sessions_find(back, n, back[i].name) == i && back[i].port >= 0
                  && back[i].port <= 65535 && session_tunnels_ok(back[i].tunnels) >= 0 && back[i].colours >= 0
                  && back[i].colours < SESSION_COLOURS && back[i].font >= 0 && back[i].font <= 3
                  && (back[i].lines == 0 || (back[i].lines >= 100 && back[i].lines <= 20000)) && back[i].keepalive >= 0
                  && back[i].keepalive <= 3600 && back[i].kind >= SESSION_SSH && back[i].kind <= SESSION_RAW
                  && back[i].charset >= 0 && back[i].charset <= 65535, "rubbish %d: session %d is not one", j, i);
    }
    printf("sessions_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
