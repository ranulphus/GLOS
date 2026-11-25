/* vkeyb.c - the keyboard's layout for DOS programs on a native boot (docs/v1/native-dos.md, N4: KEYB's work, with
 * no FreeDOS): [region] layout's characters in the BIOS's keyboard buffer, where INT 16h and DOS read them.
 *
 * The system VM's INT 09h is an ARPL of GLOS's (vdos.c), in front of the BIOS's handler, as KEYB hooks it. For a
 * key pressed whose character on the layout (keymap.h, with Shift, Caps Lock, Num Lock and AltGr as the BIOS's
 * flags have them) is not what a US keyboard's would be, and for the 102nd key (which some BIOSes give nothing
 * for), GLOS reads the byte from the virtual 8042 as the BIOS's handler would, puts the key and the character, in
 * [region] codepage, into the BIOS's buffer, and acknowledges the interrupt at the VM's PIC; every other byte
 * (a release, an E0 code, a shift key, a key the same on both, a key with Ctrl or an Alt that isn't AltGr) goes
 * on to the BIOS's handler as it came. AltGr with a key that types nothing on the layout types nothing (no Alt
 * combination either). A character the code page has not got is dropped. Programs that take INT 09h over and
 * read the 8042 themselves see the keys as they are, as under KEYB. Not in a direct-mode session (the 8042 and
 * the PIC are then the program's own): the BIOS's US keys there. */
#include "glos/cp.h"
#include "glos/region.h"
#include "arch.h"
#include "io.h"
#include "kcfg.h"
#include "keymap.h"
#include "kprintf.h"
#include "vm.h"

/* The key and character into the BIOS's buffer (0040:001Ah head, 001Ch tail, 0080h/0082h its bounds), unless it
   is full (the BIOS beeps; here the key is lost). */
static void put_key(u8 scan, u8 ch)
{
    u16 head = vm_rd16(0x41A), tail = vm_rd16(0x41C), start = vm_rd16(0x480), end = vm_rd16(0x482), next;

    if (start < 0x1E || end <= start || end > 0x100)
        start = 0x1E, end = 0x3E;               /* (bounds a program has spoilt: the BIOS's own) */
    next = (u16)(tail + 2);
    if (next >= end)
        next = start;
    if (next == head)
        return;
    vm_wr16(0x400u + tail, (u16)(scan << 8 | ch));
    vm_wr16(0x41C, next);
}

/* INT 09h's ARPL (vdos.c): 1 if GLOS answered the key (the byte read, the interrupt acknowledged), 0 if the
   BIOS's handler is to have it. */
int vkeyb_int09(void)
{
    static u8 said;
    int layout = kcfg_layout(), shift, caps, num, altgr, code;
    u8 b, f17, f18, f96;
    unsigned us, lay;

    if (layout <= KEYMAP_US || vm.direct || !vkbc_peek(&b) || b == 0xE0 || b == 0xE1 || (b & 0x80))
        return 0;
    f17 = vm_rd8(0x417), f18 = vm_rd8(0x418), f96 = vm_rd8(0x496);
    if ((f96 & 0x02) || (f17 & 0x04) || (f18 & 0x02))
        return 0;                               /* (an E0 key; Ctrl or the left Alt held: the BIOS's) */
    altgr = keymap_altgr(layout) && (f96 & 0x08);
    if ((f17 & 0x08) && !altgr)
        return 0;                               /* (an Alt: the BIOS's combinations) */
    if (b >= 0x3A && b != 0x56)
        return 0;                               /* (the function keys and the pad: the same everywhere) */
    shift = (f17 & 0x03) != 0, caps = (f17 & 0x40) != 0, num = (f17 & 0x20) != 0;
    us = keymap_char_in(KEYMAP_US, b, shift, 0, 0, 0, caps, num);
    lay = keymap_char_in(layout, b, shift, 0, 0, altgr, caps, num);
    if (!altgr && lay == us && b != 0x56)
        return 0;
    (void)vkbc_in(0x60);                        /* (the byte read, as the BIOS's IN reads it) */
    code = lay ? cp_byte(kcfg_region()->codepage ? kcfg_region()->codepage : 437, lay) : -1;
    if (code > 0)
        put_key(b, (u8)code);
    vpic_write(&vm.pic, 0x20, 0x20);            /* (the BIOS's EOI) */
    if (!said) {
        said = 1;
        kprintf("GLOS-KEYB layout=%s first key=%02x char=%04x code=%d\n", keymap_name(layout), b, lay, code);
    }
    return 1;
}
