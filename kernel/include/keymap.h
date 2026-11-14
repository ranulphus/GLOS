/* The keyboard's layouts (PRD §6.11; docs/graphics.md §4.2; GLOS.CFG's
 * [region] layout): a set 1 key's character (Unicode) for what is held, as
 * the desktop gives it to apps (kernel/wm/deskin.c) and the host tests'
 * libglos makes it (tests/host/glosshim.c); and the other way, the key that
 * types a character, for text typed from afar (glos type, the remote
 * desktop). Plain C, for both. 0: no character, no key.
 *
 * "us": the 101-key US keyboard (and a 102-key one's extra key, 56h, as
 * Windows has it: \ and |). "uk": the 102-key British one: " on 2, the pound
 * on 3, @ beside ;, # and ~ beside Enter, \ and | on the extra key, the not
 * sign on the key under Esc; and with AltGr (the right Alt, which is no Alt
 * on this layout) the euro on 4, the broken bar, and the vowels with an
 * acute accent. */
#ifndef K_KEYMAP_H
#define K_KEYMAP_H

enum { KEYMAP_US, KEYMAP_UK, KEYMAP_N };

static inline const char *keymap_name(int layout)
{
    return layout == KEYMAP_UK ? "uk" : "us";
}

/* A layout by its name in GLOS.CFG: its number, or -1. */
static inline int keymap_id(const char *name)
{
    int i, k;
    for (i = 0; i < KEYMAP_N; i++) {
        const char *n = keymap_name(i);
        for (k = 0; n[k] && (name[k] | 32) == n[k]; k++)
            ;
        if (!n[k] && !name[k])
            return i;
    }
    return -1;
}

/* Whether the right Alt is AltGr on it: a key of its own for more characters, and not an Alt. */
static inline int keymap_altgr(int layout) { return layout == KEYMAP_UK; }

static inline unsigned keymap_char_in(int layout, unsigned key, int shift, int ctrl, int alt, int altgr, int caps, int num)
{
    static const char plain[0x3A] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 8, 9,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0, 'a', 's',
        'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ' };
    static const char shifted[0x3A] = {
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', 8, 9,
        'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', 13, 0, 'A', 'S',
        'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', 0, '|', 'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<', '>', '?', 0, '*', 0, ' ' };
    /* What the British keyboard has otherwise: the key; alone, with Shift; with AltGr, and Shift too. */
    static const struct { unsigned char key; unsigned short c[4]; } uk[] = {
        { 0x03, { '2', '"', 0, 0 } }, { 0x04, { '3', 0xA3, 0, 0 } }, { 0x05, { '4', '$', 0x20AC, 0 } },
        { 0x28, { '\'', '@', 0, 0 } }, { 0x29, { '`', 0xAC, 0xA6, 0 } }, { 0x2B, { '#', '~', 0, 0 } },
        { 0x12, { 'e', 'E', 0xE9, 0xC9 } }, { 0x16, { 'u', 'U', 0xFA, 0xDA } }, { 0x17, { 'i', 'I', 0xED, 0xCD } },
        { 0x18, { 'o', 'O', 0xF3, 0xD3 } }, { 0x1E, { 'a', 'A', 0xE1, 0xC1 } } };
    static const char pad[] = "789-456+1230.";
    unsigned c = 0, i;
    if (!keymap_altgr(layout)) {                /* (the right Alt is an Alt) */
        alt = alt || altgr;
        altgr = 0;
    }
    if (key < 0x3A) {
        c = (unsigned char)plain[key];
        if (c >= 'a' && c <= 'z')
            c -= (shift != (caps != 0)) ? 32u : 0u;
        else if (shift)
            c = (unsigned char)shifted[key];
        if (layout == KEYMAP_UK) {
            for (i = 0; i < sizeof uk / sizeof uk[0]; i++)
                if (uk[i].key == key) {
                    int letter = uk[i].c[0] >= 'a' && uk[i].c[0] <= 'z';
                    if (altgr)                  /* (Caps Lock is for letters, the accented ones too) */
                        c = uk[i].c[2 + (letter ? (shift != 0) != (caps != 0) : shift != 0)];
                    else if (!letter)
                        c = uk[i].c[shift != 0];
                    break;
                }
            if (altgr && i == sizeof uk / sizeof uk[0])
                c = 0;                          /* (AltGr with a key that has nothing more: nothing) */
        }
    } else if (key == 0x56) {                   /* the 102nd key, beside the left Shift */
        c = altgr ? 0u : shift ? '|' : '\\';
    } else if (altgr) {
        c = 0;
    } else if (key >= 0x47 && key <= 0x53) {    /* the number pad: digits with Num Lock, or Shift without */
        c = (unsigned char)pad[key - 0x47];
        if (c != '-' && c != '+' && shift == (num != 0))
            c = 0;
    } else if (key == 0x11C) {
        c = 13;
    } else if (key == 0x135) {
        c = '/';
    }
    if (alt)
        return 0;
    if (ctrl) {
        if (c >= 'a' && c <= 'z')
            return c - 0x60;
        if (c >= 0x40 && c < 0x60)
            return c - 0x40;
        return c == 13 ? 10 : c == 8 ? 127 : c == ' ' || c == 27 ? c : 0;
    }
    return c;
}

/* The key that types a character on a layout, and what is held with it (*held: bit 0 Shift, bit 1 AltGr): the
   key's code, or 0 if the layout has no key for it. A key alone before one with Shift, AltGr last; the number
   pad's keys never. */
static inline unsigned keymap_key(int layout, unsigned ch, int *held)
{
    unsigned key;
    int h;
    if (!ch)
        return 0;
    for (h = 0; h < (keymap_altgr(layout) ? 4 : 2); h++)
        for (key = 1; key <= 0x56; key++) {
            if (key == 0x37 || (key >= 0x3A && key < 0x56))
                continue;
            if (keymap_char_in(layout, key, h & 1, 0, 0, (h & 2) != 0, 0, 0) == ch) {
                *held = h;
                return key;
            }
        }
    return 0;
}

#endif
