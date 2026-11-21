/* psp_test: kernel/ndos/psp.c, a PSP field by field as RBIL's table 01378 has it, and a child's environment.
 * "psp_test: 0 failures" when all held. */
#include <stdio.h>
#include <string.h>

#include "../../kernel/ndos/psp.h"

static int fails;

#define CHECK(cond, ...)                                                                                       \
    do {                                                                                                       \
        if (!(cond)) {                                                                                         \
            printf("FAIL %s:%d: ", __func__, __LINE__);                                                        \
            printf(__VA_ARGS__);                                                                               \
            printf("\n");                                                                                      \
            fails++;                                                                                           \
        }                                                                                                      \
    } while (0)

static unsigned w(const unsigned char *p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned long d(const unsigned char *p) { return w(p) | (unsigned long)w(p + 2) << 16; }

static void t_psp(void)
{
    unsigned char p[256], jft[20], f1[16];
    struct dos_psp_init in;
    char tail[200];

    memset(jft, 0xFF, 20);
    jft[0] = jft[1] = jft[2] = 1, jft[3] = 0, jft[4] = 2;
    memset(f1, 0, 16);
    f1[0] = 3;
    memcpy(f1 + 1, "README  TXT", 11);
    memset(&in, 0, sizeof in);
    in.seg = 0x1234;
    in.top = 0x9FFF;
    in.parent = 0x0F00;
    in.env = 0x1200;
    in.jft = jft;
    in.int22 = 0x0F00025AUL, in.int23 = 0x00700123UL, in.int24 = 0x00700456UL;
    in.version = 0x0A07;
    in.fcb1 = f1;
    in.tail = " README.TXT /X";
    dos_psp_make(p, &in);
    CHECK(p[0] == 0xCD && p[1] == 0x20, "INT 20h");
    CHECK(w(p + 2) == 0x9FFF, "memory top");
    CHECK(p[5] == 0x9A && w(p + 6) == 0xFEF0 && w(p + 8) == 0xF01D, "CALL 5");
    CHECK(d(p + 0x0A) == 0x0F00025AUL && d(p + 0x0E) == 0x00700123UL && d(p + 0x12) == 0x00700456UL, "vectors");
    CHECK(w(p + 0x16) == 0x0F00, "parent");
    CHECK(!memcmp(p + 0x18, jft, 20), "handles");
    CHECK(w(p + 0x2C) == 0x1200, "environment");
    CHECK(w(p + 0x32) == 20 && w(p + 0x34) == 0x18 && w(p + 0x36) == 0x1234, "handle table: %x:%x size %u",
          w(p + 0x36), w(p + 0x34), w(p + 0x32));
    CHECK(d(p + 0x38) == 0xFFFFFFFFUL, "previous PSP");
    CHECK(w(p + 0x40) == 0x0A07, "version");
    CHECK(p[0x50] == 0xCD && p[0x51] == 0x21 && p[0x52] == 0xCB, "INT 21h, RETF");
    CHECK(!memcmp(p + 0x5C, f1, 16), "first FCB");
    CHECK(p[0x6C] == 0 && !memcmp(p + 0x6D, "           ", 11), "second FCB blank");
    CHECK(p[0x80] == 14 && !memcmp(p + 0x81, " README.TXT /X", 14) && p[0x81 + 14] == 0x0D, "tail");

    memset(tail, 'x', sizeof tail);
    tail[199] = 0;
    in.tail = tail;
    in.jft = 0;
    in.fcb1 = 0;
    dos_psp_make(p, &in);
    CHECK(p[0x80] == 126 && p[0xFF] == 0x0D, "a tail too long: %u, %02x", p[0x80], p[0xFF]);
    CHECK(p[0x18] == 0xFF && p[0x18 + 19] == 0xFF, "no handles: all closed");
    CHECK(p[0x5C] == 0 && p[0x5D] == ' ', "first FCB blank");
}

static void t_env(void)
{
    static const unsigned char env[] = "PATH=C:\\DOS\0COMSPEC=C:\\COMMAND.COM\0\0";
    unsigned char out[200];
    unsigned long n;

    n = dos_env_make(out, sizeof out, env, sizeof env, "C:\\GAMES\\DOOM.EXE");
    CHECK(n == sizeof env - 1 + 2 + 18, "size %lu", n);
    CHECK(!memcmp(out, env, sizeof env - 1), "the strings");
    CHECK(out[sizeof env - 1] == 1 && out[sizeof env] == 0, "the count word");
    CHECK(!strcmp((char *)out + sizeof env + 1, "C:\\GAMES\\DOOM.EXE"), "the program's name");
    n = dos_env_make(out, sizeof out, 0, 0, "A:\\X.COM");
    CHECK(n == 2 + 2 + 9 && out[0] == 0 && out[1] == 0 && out[2] == 1 && !strcmp((char *)out + 4, "A:\\X.COM"),
          "no environment: %lu", n);
    CHECK(dos_env_make(out, 20, env, sizeof env, "C:\\GAMES\\DOOM.EXE") == 0, "no room");
    CHECK(dos_env_make(out, sizeof out, (const unsigned char *)"A=1B=2", 6, "X") == 0, "no end: refused");
}

int main(void)
{
    t_psp();
    t_env();
    printf("psp_test: %d failures\n", fails);
    return fails != 0;
}
