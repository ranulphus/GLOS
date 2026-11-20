/* Telnet's own part of a connection (apps/shared/telnet.c): a server's opening answered, the terminal's type and
 * size told, text with Telnet's commands among it, whole and cut in two at every place, what is typed as Telnet
 * carries it, and rubbish, which must do no harm.
 *
 *   telnet_test      (prints "telnet_test: N failures (M checks)") */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../apps/shared/telnet.h"

static int fails, checks;

#define CHECK(c, ...) do { checks++; if (!(c)) { printf("telnet_test: " __VA_ARGS__); printf("\n"); fails++; } } while (0)

static unsigned char sent[4096], text[4096];
static int nsent, ntext;

static void on_send(void *ctx, const unsigned char *d, int n)
{
    (void)ctx;
    if (nsent + n <= (int)sizeof sent) {
        memcpy(sent + nsent, d, (size_t)n);
        nsent += n;
    }
}

static void on_data(void *ctx, const unsigned char *d, int n)
{
    (void)ctx;
    if (ntext + n <= (int)sizeof text) {
        memcpy(text + ntext, d, (size_t)n);
        ntext += n;
    }
}

static void fresh(struct telnet *t, const char *ttype)
{
    telnet_init(t, on_send, on_data, NULL, ttype, 80, 24);
    nsent = ntext = 0;
}

#define IS(buf, n, lit) ((n) == (int)sizeof(lit) - 1 && !memcmp(buf, lit, sizeof(lit) - 1))

int main(void)
{
    struct telnet t;
    unsigned seed = 99;
    int cut, i, j;
    /* a server's opening: it will echo and suppress go-aheads; it asks for the type and the size, and for two
       things this side has not got; it will do one more that nobody wants */
    static const unsigned char hello[] = "\377\373\001\377\373\003\377\375\030\377\375\037\377\375\047\377\373\005"
                                         "login: ";
    static const unsigned char want[] = "\377\375\001\377\375\003\377\373\030\377\373\037\377\372\037\000\120\000\030\377\360"
                                        "\377\374\047\377\376\005";
    for (cut = 0; cut <= (int)sizeof hello - 1; cut++) {
        fresh(&t, "ANSI");
        telnet_input(&t, hello, cut);
        telnet_input(&t, hello + cut, (int)sizeof hello - 1 - cut);
        CHECK(nsent == (int)sizeof want - 1 && !memcmp(sent, want, sizeof want - 1), "the opening, cut at %d: %d bytes answered",
              cut, nsent);
        CHECK(IS(text, ntext, "login: ") && telnet_echoes(&t), "the opening, cut at %d: its text, the echo", cut);
    }
    /* asked again: nothing more is said; the type asked for: told; the echo taken back: agreed, once */
    nsent = 0;
    telnet_input(&t, (const unsigned char *)"\377\373\001\377\375\030\377\375\037", 9);
    CHECK(nsent == 0, "asked again: %d bytes", nsent);
    telnet_input(&t, (const unsigned char *)"\377\372\030\001\377\360", 6);
    CHECK(IS(sent, nsent, "\377\372\030\000ANSI\377\360"), "the type: %d bytes", nsent);
    nsent = 0;
    telnet_input(&t, (const unsigned char *)"\377\374\001\377\374\001", 6);
    CHECK(IS(sent, nsent, "\377\376\001") && !telnet_echoes(&t), "the echo taken back: %d bytes", nsent);
    /* the window's new size, and one with a 255 in it; the same size again: nothing */
    nsent = 0;
    telnet_size(&t, 132, 43);
    CHECK(IS(sent, nsent, "\377\372\037\000\204\000\053\377\360"), "a new size: %d bytes", nsent);
    nsent = 0;
    telnet_size(&t, 132, 43);
    telnet_size(&t, 255, 50);
    CHECK(IS(sent, nsent, "\377\372\037\000\377\377\000\062\377\360"), "a size of 255: %d bytes", nsent);
    /* the size is not told where nobody asked */
    fresh(&t, NULL);
    telnet_size(&t, 100, 30);
    CHECK(nsent == 0, "a size nobody asked for");
    telnet_input(&t, (const unsigned char *)"\377\372\030\001\377\360", 6);
    CHECK(nsent == 0, "a type nobody agreed to tell");

    /* text with commands among it, cut at every place: a 255 that is text, a carriage return's NUL, a line's
       end, a go-ahead, a subnegotiation nobody understands (with a 255 in it), a NOP */
    {
        static const unsigned char in[] = "one\377\377two\r\000three\r\nfour\377\371five\377\372\052\001\377\377\002\377\360six\377\361.";
        static const unsigned char out[] = "one\377two\rthree\r\nfourfivesix.";
        for (cut = 0; cut <= (int)sizeof in - 1; cut++) {
            fresh(&t, NULL);
            telnet_input(&t, in, cut);
            telnet_input(&t, in + cut, (int)sizeof in - 1 - cut);
            CHECK(ntext == (int)sizeof out - 1 && !memcmp(text, out, sizeof out - 1) && nsent == 0, "text cut at %d: %d bytes, %d sent",
                  cut, ntext, nsent);
        }
    }
    /* text and an option between: each in its place (the text before the option is given first) */
    fresh(&t, NULL);
    telnet_input(&t, (const unsigned char *)"before\377\373\001after", 14);
    CHECK(IS(text, ntext, "beforeafter") && IS(sent, nsent, "\377\375\001"), "an option among text");

    /* what is typed: a 255 doubled, a carriage return with its NUL, a line's end as it is; binary: as they are */
    fresh(&t, NULL);
    telnet_output(&t, (const unsigned char *)"a\377b\rc", 5);
    CHECK(IS(sent, nsent, "a\377\377b\r\000c"), "typed: %d bytes", nsent);
    nsent = 0;
    telnet_output(&t, (const unsigned char *)"a line\r\n", 8);
    CHECK(IS(sent, nsent, "a line\r\n"), "a line typed: %d bytes", nsent);
    telnet_input(&t, (const unsigned char *)"\377\375\000", 3);
    nsent = 0;
    telnet_output(&t, (const unsigned char *)"\r\377", 2);
    CHECK(IS(sent, nsent, "\r\377\377"), "typed, binary: %d bytes", nsent);
    {                                           /* a lot at once: all of it goes */
        static unsigned char big[3000];
        memset(big, 'x', sizeof big);
        big[100] = big[2999] = 255;
        nsent = 0;
        telnet_output(&t, big, sizeof big);
        CHECK(nsent == 3002 && sent[100] == 255 && sent[101] == 255 && sent[3001] == 255, "3000 bytes typed: %d sent", nsent);
    }

    /* what this side offers first, to a Telnet server: answered by the server, no word is said twice */
    fresh(&t, "XTERM");
    telnet_offer(&t);
    CHECK(IS(sent, nsent, "\377\373\030\377\373\037\377\375\003"), "the offer: %d bytes", nsent);
    nsent = 0;
    telnet_input(&t, (const unsigned char *)"\377\375\030\377\375\037\377\373\003", 9);
    CHECK(IS(sent, nsent, "\377\372\037\000\120\000\030\377\360"), "the offer taken: %d bytes", nsent);
    nsent = 0;
    telnet_input(&t, (const unsigned char *)"\377\376\030\377\374\003", 6);      /* ... and refused after all */
    CHECK(IS(sent, nsent, "\377\374\030\377\376\003"), "the offer refused: %d bytes", nsent);

    /* rubbish in pieces: no more text comes out than went in */
    for (j = 0; j < 3000; j++) {
        unsigned char in[200];
        int n, at = 0;
        seed = seed * 1103515245u + 12345u;
        n = (int)((seed >> 16) % sizeof in);
        for (i = 0; i < n; i++) {
            seed = seed * 1103515245u + 12345u;
            in[i] = (seed >> 24) % 3 ? (unsigned char)(seed >> 16) : (unsigned char)(240 + (seed >> 16) % 16);
        }
        fresh(&t, "ANSI");
        while (at < n) {
            int part;
            seed = seed * 1103515245u + 12345u;
            part = 1 + (int)((seed >> 16) % 30);
            if (part > n - at)
                part = n - at;
            telnet_input(&t, in + at, part);
            at += part;
        }
        CHECK(ntext <= n && t.nsub <= (int)sizeof t.sub && t.st >= 0 && t.st <= 4, "rubbish %d: %d of text from %d", j, ntext, n);
        telnet_size(&t, (unsigned)(seed >> 8) & 0xFFFF, (unsigned)(seed >> 4) & 0xFFFF);
        telnet_output(&t, in, n);
    }
    printf("telnet_test: %d failures (%d checks)\n", fails, checks);
    return fails != 0;
}
