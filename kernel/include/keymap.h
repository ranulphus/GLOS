/* The US layout (PRD §6.11; docs/graphics.md §4.2): a set 1 key's character
 * for what is held, as the desktop gives it to apps (kernel/wm/deskin.c)
 * and the host tests' libglos makes it (tests/host/glosshim.c). Plain C,
 * for both. 0: no character. */
#ifndef K_KEYMAP_H
#define K_KEYMAP_H

static inline unsigned keymap_char(unsigned key, int shift, int ctrl, int alt, int caps, int num)
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
    static const char pad[] = "789-456+1230.";
    unsigned c = 0;
    if (key < 0x3A) {
        c = (unsigned char)plain[key];
        if (c >= 'a' && c <= 'z')
            c -= (shift != (caps != 0)) ? 32u : 0u;
        else if (shift)
            c = (unsigned char)shifted[key];
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

#endif
