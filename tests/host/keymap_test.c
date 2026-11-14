/* The keyboard's layouts (kernel/include/keymap.h): each layout's characters
 * for its keys, the keys the British keyboard has otherwise, AltGr, Caps
 * Lock and Ctrl; and the way back: every character a layout types has a key
 * that types it, held as keymap_key says. */
#include <stdio.h>

#include "keymap.h"

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

/* key with Shift (1), AltGr (2), Caps Lock (4), Ctrl (8), the left Alt (16) */
static unsigned ch(int layout, unsigned key, int with)
{
    return keymap_char_in(layout, key, with & 1, (with & 8) != 0, (with & 16) != 0, (with & 2) != 0, (with & 4) != 0, 0);
}

int main(void)
{
    int layout, held;
    unsigned c, n = 0;

    CHECK(keymap_id("us") == KEYMAP_US && keymap_id("uk") == KEYMAP_UK && keymap_id("UK") == KEYMAP_UK);
    CHECK(keymap_id("u") == -1 && keymap_id("ukx") == -1 && keymap_id("") == -1 && keymap_id("de") == -1);
    CHECK(!keymap_altgr(KEYMAP_US) && keymap_altgr(KEYMAP_UK));

    /* The US keyboard, as it was */
    CHECK(ch(KEYMAP_US, 0x03, 0) == '2' && ch(KEYMAP_US, 0x03, 1) == '@' && ch(KEYMAP_US, 0x04, 1) == '#');
    CHECK(ch(KEYMAP_US, 0x28, 0) == '\'' && ch(KEYMAP_US, 0x28, 1) == '"' && ch(KEYMAP_US, 0x29, 1) == '~');
    CHECK(ch(KEYMAP_US, 0x2B, 0) == '\\' && ch(KEYMAP_US, 0x2B, 1) == '|');
    CHECK(ch(KEYMAP_US, 0x56, 0) == '\\' && ch(KEYMAP_US, 0x56, 1) == '|');     /* a 102-key one's extra key */
    CHECK(ch(KEYMAP_US, 0x1E, 0) == 'a' && ch(KEYMAP_US, 0x1E, 1) == 'A' && ch(KEYMAP_US, 0x1E, 4) == 'A'
          && ch(KEYMAP_US, 0x1E, 5) == 'a');
    CHECK(ch(KEYMAP_US, 0x1E, 2) == 0 && ch(KEYMAP_US, 0x05, 2) == 0);         /* the right Alt is an Alt there */
    CHECK(ch(KEYMAP_US, 0x1E, 16) == 0 && ch(KEYMAP_US, 0x2E, 8) == 3 && ch(KEYMAP_US, 0x1C, 8) == 10);
    CHECK(keymap_char_in(KEYMAP_US, 0x47, 0, 0, 0, 0, 0, 1) == '7' && keymap_char_in(KEYMAP_US, 0x47, 0, 0, 0, 0, 0, 0) == 0);
    CHECK(keymap_char_in(KEYMAP_US, 0x11C, 0, 0, 0, 0, 0, 0) == 13 && keymap_char_in(KEYMAP_US, 0x135, 0, 0, 0, 0, 0, 0) == '/');

    /* The British one: what differs, and what doesn't */
    CHECK(ch(KEYMAP_UK, 0x03, 0) == '2' && ch(KEYMAP_UK, 0x03, 1) == '"');
    CHECK(ch(KEYMAP_UK, 0x04, 0) == '3' && ch(KEYMAP_UK, 0x04, 1) == 0xA3);     /* the pound sign */
    CHECK(ch(KEYMAP_UK, 0x05, 0) == '4' && ch(KEYMAP_UK, 0x05, 1) == '$' && ch(KEYMAP_UK, 0x05, 2) == 0x20AC);
    CHECK(ch(KEYMAP_UK, 0x28, 0) == '\'' && ch(KEYMAP_UK, 0x28, 1) == '@');
    CHECK(ch(KEYMAP_UK, 0x29, 0) == '`' && ch(KEYMAP_UK, 0x29, 1) == 0xAC && ch(KEYMAP_UK, 0x29, 2) == 0xA6);
    CHECK(ch(KEYMAP_UK, 0x2B, 0) == '#' && ch(KEYMAP_UK, 0x2B, 1) == '~');
    CHECK(ch(KEYMAP_UK, 0x56, 0) == '\\' && ch(KEYMAP_UK, 0x56, 1) == '|');
    CHECK(ch(KEYMAP_UK, 0x02, 1) == '!' && ch(KEYMAP_UK, 0x06, 1) == '%' && ch(KEYMAP_UK, 0x27, 1) == ':');
    /* ... Caps Lock is for letters alone: the pound sign needs Shift, the euro none */
    CHECK(ch(KEYMAP_UK, 0x04, 4) == '3' && ch(KEYMAP_UK, 0x05, 2 | 4) == 0x20AC && ch(KEYMAP_UK, 0x05, 2 | 1) == 0);
    /* ... AltGr: the vowels with an acute accent, capitals by Shift or Caps Lock; nothing on other keys; no Alt */
    CHECK(ch(KEYMAP_UK, 0x1E, 2) == 0xE1 && ch(KEYMAP_UK, 0x1E, 3) == 0xC1 && ch(KEYMAP_UK, 0x1E, 6) == 0xC1
          && ch(KEYMAP_UK, 0x1E, 7) == 0xE1);
    CHECK(ch(KEYMAP_UK, 0x12, 2) == 0xE9 && ch(KEYMAP_UK, 0x16, 2) == 0xFA && ch(KEYMAP_UK, 0x17, 2) == 0xED
          && ch(KEYMAP_UK, 0x18, 2) == 0xF3);
    CHECK(ch(KEYMAP_UK, 0x1E, 0) == 'a' && ch(KEYMAP_UK, 0x1E, 1) == 'A' && ch(KEYMAP_UK, 0x1E, 4) == 'A');
    CHECK(ch(KEYMAP_UK, 0x10, 2) == 0 && ch(KEYMAP_UK, 0x56, 2) == 0 && ch(KEYMAP_UK, 0x39, 2) == 0);
    CHECK(keymap_char_in(KEYMAP_UK, 0x47, 0, 0, 0, 1, 0, 1) == 0);
    CHECK(ch(KEYMAP_UK, 0x1E, 16) == 0 && ch(KEYMAP_UK, 0x56, 8) == 0x1C && ch(KEYMAP_UK, 0x2B, 8) == 0);

    /* The way back. Every character of ASCII has a key on both; each layout's own beyond it */
    for (layout = 0; layout < KEYMAP_N; layout++) {
        for (c = 0x20; c < 0x7F; c++) {
            unsigned key = keymap_key(layout, c, &held);
            if (!key || keymap_char_in(layout, key, held & 1, 0, 0, (held & 2) != 0, 0, 0) != c) {
                fails++;
                printf("FAIL layout=%s no key for %c\n", keymap_name(layout), c);
            }
            n++;
        }
        CHECK(keymap_key(layout, 13, &held) == 0x1C && held == 0 && keymap_key(layout, 27, &held) == 0x01);
        CHECK(keymap_key(layout, 8, &held) == 0x0E && keymap_key(layout, 9, &held) == 0x0F);
        CHECK(keymap_key(layout, '*', &held) == 0x09 && held == 1);             /* never the number pad's */
        CHECK(keymap_key(layout, 0, &held) == 0 && keymap_key(layout, 0x3B1, &held) == 0);
    }
    CHECK(keymap_key(KEYMAP_US, '"', &held) == 0x28 && held == 1 && keymap_key(KEYMAP_UK, '"', &held) == 0x03 && held == 1);
    CHECK(keymap_key(KEYMAP_US, '#', &held) == 0x04 && held == 1 && keymap_key(KEYMAP_UK, '#', &held) == 0x2B && held == 0);
    CHECK(keymap_key(KEYMAP_US, '\\', &held) == 0x2B && held == 0 && keymap_key(KEYMAP_UK, '\\', &held) == 0x56 && held == 0);
    CHECK(keymap_key(KEYMAP_UK, '|', &held) == 0x56 && held == 1 && keymap_key(KEYMAP_UK, '~', &held) == 0x2B && held == 1);
    CHECK(keymap_key(KEYMAP_UK, 0xA3, &held) == 0x04 && held == 1 && keymap_key(KEYMAP_US, 0xA3, &held) == 0);
    CHECK(keymap_key(KEYMAP_UK, 0x20AC, &held) == 0x05 && held == 2 && keymap_key(KEYMAP_UK, 0xC9, &held) == 0x12 && held == 3);
    CHECK(keymap_key(KEYMAP_US, 0xE9, &held) == 0);

    printf("keymap_test: %d failures (%u characters and back)\n", fails, n);
    return fails != 0;
}
