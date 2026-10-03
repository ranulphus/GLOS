/* glos shot in text modes (PRD §7.2; M3 item 10): the visible page from
 * screen memory and the BIOS data area (mode, columns, rows, the page's
 * start, the character height), drawn with the video BIOS's own ROM font
 * (found by GLOS.EXE through INT 10h 1130h), so the picture has the glyphs
 * the card shows and GLOS ships no font. Cells are 8 pixels wide (the 9th
 * column VGA adds is left out), the colours are the default 16, and with
 * blinking on (the BIOS default) an attribute's bit 7 blinks instead of
 * brightening the background: the picture shows such text steadily. Read
 * without stopping the VM; a screen being rewritten may tear. */
#include "glos/bootinfo.h"
#include "mm.h"
#include "png.h"
#include "shot.h"
#include "vm.h"

static const u8 palette[16][3] = {
    { 0x00, 0x00, 0x00 }, { 0x00, 0x00, 0xAA }, { 0x00, 0xAA, 0x00 }, { 0x00, 0xAA, 0xAA },
    { 0xAA, 0x00, 0x00 }, { 0xAA, 0x00, 0xAA }, { 0xAA, 0x55, 0x00 }, { 0xAA, 0xAA, 0xAA },
    { 0x55, 0x55, 0x55 }, { 0x55, 0x55, 0xFF }, { 0x55, 0xFF, 0x55 }, { 0x55, 0xFF, 0xFF },
    { 0xFF, 0x55, 0x55 }, { 0xFF, 0x55, 0xFF }, { 0xFF, 0xFF, 0x55 }, { 0xFF, 0xFF, 0xFF },
};

u8 *shot_text(u32 *len, const char **why)
{
    const struct bootinfo *bi = vm.bi;
    u32 mode = vm_rd8(0x449) & 0x7F, cols = vm_rd16(0x44A), rows = vm_rd8(0x484) + 1u, ch = vm_rd16(0x485);
    u32 text = (mode == 7 ? 0xB0000u : 0xB8000u) + vm_rd16(0x44E), w, h, x, y, gy, gx;
    int blink = (vm_rd8(0x465) & 0x20) != 0;
    u8 *pix, *png;

    if (mode > 3 && mode != 7) {
        *why = "graphics modes come later";
        return 0;
    }
    if (rows < 12 || rows > 60)
        rows = 25;                              /* a BIOS without 0484h */
    if (cols < 40 || cols > 132)
        cols = 80;
    if (ch != 8 && ch != 14 && ch != 16)
        ch = 16;
    if (!(ch == 16 ? bi->font16 : ch == 14 ? bi->font14 : bi->font8)) {
        *why = "no ROM font";
        return 0;
    }
    w = cols * 8;
    h = rows * ch;
    if (!(pix = kmalloc(w * h))) {
        *why = "no memory";
        return 0;
    }
    for (y = 0; y < rows; y++)
        for (x = 0; x < cols; x++) {
            u32 cell = text + (y * cols + x) * 2, glyph;
            u8 c = vm_rd8(cell), a = vm_rd8(cell + 1), fg = a & 15, bg = (u8)(blink ? (a >> 4) & 7 : a >> 4);
            if (ch == 16)
                glyph = bi->font16 + c * 16u;
            else if (ch == 14)
                glyph = bi->font14 + c * 14u;
            else
                glyph = c < 128 ? bi->font8 + c * 8u : bi->font8hi + (c - 128u) * 8u;
            for (gy = 0; gy < ch; gy++) {
                u8 bits = vm_rd8(glyph + gy), *row = pix + (y * ch + gy) * w + x * 8;
                for (gx = 0; gx < 8; gx++)
                    row[gx] = (bits & (0x80 >> gx)) ? fg : bg;
            }
        }
    png = png_indexed4(w, h, palette, pix, len);
    kfree(pix);
    if (!png)
        *why = "no memory";
    return png;
}
