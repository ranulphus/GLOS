/* The toolkit's drawing (docs/gui.md): the Slate look's pieces, on the 2D
 * library. Lines a pixel wide in black, faces in the palette cube's greys;
 * nothing shaded or bevelled. */
#include "guiint.h"

void gui_fill(struct gfx_surface *s, const struct gfx_rect *r, unsigned rgb) { gfx_fill(s, r->x, r->y, r->w, r->h, rgb); }
void gui_frame(struct gfx_surface *s, const struct gfx_rect *r, unsigned rgb) { gfx_frame(s, r->x, r->y, r->w, r->h, rgb); }

/* ---- text */

/* One UTF-8 character's code point and length (a stray byte is itself). */
static unsigned utf8(const char *p, int *len)
{
    const unsigned char *u = (const unsigned char *)p;
    if (u[0] < 0x80 || !u[1]) {
        *len = 1;
        return u[0];
    }
    if ((u[0] & 0xE0) == 0xC0 && (u[1] & 0xC0) == 0x80) {
        *len = 2;
        return ((u[0] & 0x1Fu) << 6) | (u[1] & 0x3Fu);
    }
    if ((u[0] & 0xF0) == 0xE0 && (u[1] & 0xC0) == 0x80 && (u[2] & 0xC0) == 0x80) {
        *len = 3;
        return ((u[0] & 0x0Fu) << 12) | ((u[1] & 0x3Fu) << 6) | (u[2] & 0x3Fu);
    }
    *len = 1;
    return u[0];
}

int gui_char_w(const struct gfx_font *f, unsigned c)
{
    char b[4];
    int n;
    if (c < 0x80) {
        b[0] = (char)c;
        n = 1;
    } else {
        b[0] = (char)(0xC0 | (c >> 6));
        b[1] = (char)(0x80 | (c & 0x3F));
        n = 2;
    }
    return gfx_text_width(f, b, n);
}

void gui_strip_amp(char *out, const char *in, int room)
{
    int n = 0;
    for (; *in && n < room - 1; in++) {
        if (*in == '&') {
            if (in[1] == '&')
                in++;
            else
                continue;
        }
        out[n++] = *in;
    }
    out[n] = 0;
}

int gui_text_w(const struct gfx_font *f, const char *text)
{
    char buf[256];
    gui_strip_amp(buf, text ? text : "", sizeof buf);
    return gfx_text_width(f, buf, -1);
}

void gui_draw_text(struct gfx_surface *s, int x, int y, const struct gfx_font *f, const char *text, unsigned rgb)
{
    const char *p;
    int pen = x;
    if (!text)
        return;
    for (p = text; *p; ) {
        int len;
        unsigned c;
        if (*p == '&' && p[1] && p[1] != '&') {
            int at = pen;
            utf8(p + 1, &len);
            pen = gfx_text(s, pen, y, f, p + 1, len, rgb);
            gfx_fill(s, at, y + f->ascent + 1, pen - at - 1 > 0 ? pen - at - 1 : 1, 1, rgb);
            p += 1 + len;
            continue;
        }
        if (*p == '&')                          /* "&&" */
            p++;
        c = utf8(p, &len);
        (void)c;
        pen = gfx_text(s, pen, y, f, p, len, rgb);
        p += len;
    }
}

/* Bytes as Latin-1 (a file's text, M9: one byte, one character), in UTF-8 pieces. */
void gui_text_latin1(struct gfx_surface *s, int x, int y, const struct gfx_font *f, const char *bytes, int n,
                     unsigned rgb)
{
    char buf[256];
    int i = 0;
    while (i < n) {
        int o = 0;
        while (i < n && o < (int)sizeof buf - 2) {
            unsigned char c = (unsigned char)bytes[i++];
            if (c < 0x20)
                c = ' ';
            if (c < 0x80) {
                buf[o++] = (char)c;
            } else {
                buf[o++] = (char)(0xC0 | (c >> 6));
                buf[o++] = (char)(0x80 | (c & 0x3F));
            }
        }
        x = gfx_text(s, x, y, f, buf, o, rgb);
    }
}

int gui_latin1_w(const struct gfx_font *f, const char *bytes, int n)
{
    int w = 0, i;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)bytes[i];
        w += gui_char_w(f, c < 0x20 ? ' ' : c);
    }
    return w;
}

void gui_text_fit(struct gfx_surface *s, int x, int y, int w, const struct gfx_font *f, const char *text,
                  unsigned rgb)
{
    int n = (int)strlen(text), dots = gfx_text_width(f, "...", 3);
    if (gfx_text_width(f, text, n) <= w) {
        gfx_text(s, x, y, f, text, n, rgb);
        return;
    }
    while (n > 0) {
        n--;
        while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80)       /* (a whole character) */
            n--;
        if (gfx_text_width(f, text, n) + dots <= w)
            break;
    }
    x = gfx_text(s, x, y, f, text, n, rgb);
    gfx_text(s, x, y, f, "...", 3, rgb);
}

/* ---- pieces */

void gui_draw_focus(struct gfx_surface *s, const struct gfx_rect *r)
{
    int i;
    for (i = 0; i < r->w; i += 2) {
        gfx_fill(s, r->x + i, r->y, 1, 1, GUI_LINE);
        gfx_fill(s, r->x + i, r->y + r->h - 1, 1, 1, GUI_LINE);
    }
    for (i = 0; i < r->h; i += 2) {
        gfx_fill(s, r->x, r->y + i, 1, 1, GUI_LINE);
        gfx_fill(s, r->x + r->w - 1, r->y + i, 1, 1, GUI_LINE);
    }
}

void gui_draw_box(struct gfx_surface *s, const struct gfx_rect *r, unsigned face, int pressed)
{
    gfx_fill(s, r->x + 1, r->y + 1, r->w - 2, r->h - 2, pressed ? GUI_DIM : face);
    gui_frame(s, r, GUI_LINE);
    if (!pressed) {                             /* (a shadow a pixel deep along the bottom and right) */
        gfx_fill(s, r->x + 1, r->y + r->h - 2, r->w - 2, 1, GUI_DIM);
        gfx_fill(s, r->x + r->w - 2, r->y + 1, 1, r->h - 2, GUI_DIM);
    }
}

void gui_draw_field(struct gfx_surface *s, const struct gfx_rect *r, int enabled)
{
    gfx_fill(s, r->x + 1, r->y + 1, r->w - 2, r->h - 2, enabled ? GUI_FIELD : GUI_FACE);
    gui_frame(s, r, enabled ? GUI_LINE : GUI_DIM);
}

void gui_draw_icon(struct gfx_surface *s, int x, int y, const unsigned char *icon, int side)
{
    int i, j;
    if (!icon)
        return;
    for (j = 0; j < side; j++)
        for (i = 0; i < side; i++) {
            unsigned c = icon[j * side + i];
            if (c)
                gfx_fill(s, x + i, y + j, 1, 1,
                         (unsigned)gfx_palette[c][0] << 16 | (unsigned)gfx_palette[c][1] << 8 | gfx_palette[c][2]);
        }
}

/* A filled triangle 7 wide and 4 high (or 4 wide and 7 high) about cx, cy. */
void gui_draw_arrow(struct gfx_surface *s, int cx, int cy, int dir, unsigned rgb)
{
    int i;
    for (i = 0; i < 4; i++) {
        switch (dir) {
        case 0: gfx_fill(s, cx - i, cy - 2 + i, 2 * i + 1, 1, rgb); break;
        case 1: gfx_fill(s, cx - 3 + i, cy - 2 + i, 7 - 2 * i, 1, rgb); break;
        case 2: gfx_fill(s, cx - 2 + i, cy - i, 1, 2 * i + 1, rgb); break;
        default: gfx_fill(s, cx - 2 + i, cy - 3 + i, 1, 7 - 2 * i, rgb); break;
        }
    }
}

static const unsigned char tick[9] = { 0x01, 0x03, 0x07, 0x8E, 0xDC, 0xF8, 0x70, 0x20, 0x00 };

void gui_draw_check(struct gfx_surface *s, int x, int y, int on, int enabled)
{
    gfx_fill(s, x + 1, y + 1, 11, 11, enabled ? GUI_FIELD : GUI_FACE);
    gfx_frame(s, x, y, 13, 13, enabled ? GUI_LINE : GUI_DIM);
    if (on)
        gfx_mono(s, x + 2, y + 2, tick, 8, 9, 1, enabled ? GUI_LINE : GUI_DIM, GFX_NONE);
}

static const unsigned char ring[13][2] = {
    { 0x0F, 0x80 }, { 0x30, 0x60 }, { 0x40, 0x10 }, { 0x40, 0x10 }, { 0x80, 0x08 }, { 0x80, 0x08 }, { 0x80, 0x08 },
    { 0x80, 0x08 }, { 0x80, 0x08 }, { 0x40, 0x10 }, { 0x40, 0x10 }, { 0x30, 0x60 }, { 0x0F, 0x80 } };
static const unsigned char disc[13][2] = {
    { 0x0F, 0x80 }, { 0x3F, 0xE0 }, { 0x7F, 0xF0 }, { 0x7F, 0xF0 }, { 0xFF, 0xF8 }, { 0xFF, 0xF8 }, { 0xFF, 0xF8 },
    { 0xFF, 0xF8 }, { 0xFF, 0xF8 }, { 0x7F, 0xF0 }, { 0x7F, 0xF0 }, { 0x3F, 0xE0 }, { 0x0F, 0x80 } };
static const unsigned char dot[5] = { 0x70, 0xF8, 0xF8, 0xF8, 0x70 };

void gui_draw_radio(struct gfx_surface *s, int x, int y, int on, int enabled)
{
    gfx_mono(s, x, y, &disc[0][0], 13, 13, 2, enabled ? GUI_FIELD : GUI_FACE, GFX_NONE);
    gfx_mono(s, x, y, &ring[0][0], 13, 13, 2, enabled ? GUI_LINE : GUI_DIM, GFX_NONE);
    if (on)
        gfx_mono(s, x + 4, y + 4, dot, 5, 5, 1, enabled ? GUI_LINE : GUI_DIM, GFX_NONE);
}

/* ---- scroll bars: arrows at the ends, a trough, a thumb its size by what shows */

static void sb_parts(const struct gui_sb *b, int *thumb_at, int *thumb_len)
{
    int vertical = b->r.h > b->r.w, len = (vertical ? b->r.h : b->r.w) - 2 * GUI_SB, span;
    int t = b->total > 0 ? (int)((long long)len * b->page / b->total) : len;
    if (t < 10)
        t = 10;
    if (t > len)
        t = len;
    span = b->total - b->page;
    *thumb_len = t;
    *thumb_at = GUI_SB + (span > 0 ? (int)((long long)(len - t) * b->pos / span) : 0);
}

void gui_sb_clamp(struct gui_sb *b)
{
    if (b->pos > b->total - b->page)
        b->pos = b->total - b->page;
    if (b->pos < 0)
        b->pos = 0;
}

void gui_sb_draw(struct gfx_surface *s, const struct gui_sb *b, int enabled)
{
    int vertical = b->r.h > b->r.w, at, len;
    struct gfx_rect r = b->r, a, z, t;
    int can = enabled && b->total > b->page;
    if (r.w <= 0 || r.h <= 0)
        return;
    gfx_fill(s, r.x, r.y, r.w, r.h, GUI_FACE);
    gui_frame(s, &r, GUI_LINE);
    if (vertical) {
        a.x = r.x, a.y = r.y, a.w = r.w, a.h = GUI_SB;
        z.x = r.x, z.y = r.y + r.h - GUI_SB, z.w = r.w, z.h = GUI_SB;
    } else {
        a.x = r.x, a.y = r.y, a.w = GUI_SB, a.h = r.h;
        z.x = r.x + r.w - GUI_SB, z.y = r.y, z.w = GUI_SB, z.h = r.h;
    }
    gui_draw_box(s, &a, GUI_FACE, b->held == 1);
    gui_draw_box(s, &z, GUI_FACE, b->held == 2);
    gui_draw_arrow(s, a.x + a.w / 2, a.y + a.h / 2, vertical ? 0 : 2, can ? GUI_LINE : GUI_DIM);
    gui_draw_arrow(s, z.x + z.w / 2, z.y + z.h / 2, vertical ? 1 : 3, can ? GUI_LINE : GUI_DIM);
    if (!can)
        return;
    sb_parts(b, &at, &len);
    if (vertical) {
        gfx_fill(s, r.x + 1, r.y + GUI_SB, r.w - 2, r.h - 2 * GUI_SB, GUI_SHADE);
        t.x = r.x, t.y = r.y + at, t.w = r.w, t.h = len;
    } else {
        gfx_fill(s, r.x + GUI_SB, r.y + 1, r.w - 2 * GUI_SB, r.h - 2, GUI_SHADE);
        t.x = r.x + at, t.y = r.y, t.w = len, t.h = r.h;
    }
    gui_draw_box(s, &t, GUI_FACE, 0);
}

/* The mouse on a scroll bar: 1 if it was the bar's. `line` units an arrow moves. */
int gui_sb_event(struct gui_widget *w, struct gui_sb *b, struct gui_ev *e, int line)
{
    int vertical = b->r.h > b->r.w, at, len, p, old = b->pos;
    int in = e->x >= b->r.x && e->y >= b->r.y && e->x < b->r.x + b->r.w && e->y < b->r.y + b->r.h;
    if (b->r.w <= 0 || b->r.h <= 0)
        return 0;
    p = vertical ? e->y - b->r.y : e->x - b->r.x;
    switch (e->type) {
    case GUI_EV_PRESS:
        if (!in || e->button != 1)
            return 0;
        sb_parts(b, &at, &len);
        if (p < GUI_SB) {
            b->held = 1;
            b->pos -= line;
        } else if (p >= (vertical ? b->r.h : b->r.w) - GUI_SB) {
            b->held = 2;
            b->pos += line;
        } else if (p < at) {
            b->held = 3;
            b->pos -= b->page;
        } else if (p >= at + len) {
            b->held = 4;
            b->pos += b->page;
        } else {
            b->drag = 1;
            b->drag_off = p - at;
        }
        if (b->held)
            gui_timer(w, 300);
        break;
    case GUI_EV_MOVE:
        if (!b->drag)
            return b->held != 0;
        sb_parts(b, &at, &len);
        {
            int room = (vertical ? b->r.h : b->r.w) - 2 * GUI_SB - len;
            int span = b->total - b->page;
            if (room > 0 && span > 0)
                b->pos = (int)((long long)(p - b->drag_off - GUI_SB) * span / room);
        }
        break;
    case GUI_EV_RELEASE:
        if (!b->drag && !b->held)
            return 0;
        b->drag = 0;
        b->held = 0;
        gui_timer(w, 0);
        gui_damage(w);
        return 1;
    case GUI_EV_TIMER:
        if (!b->held)
            return 0;
        gui_timer(w, 60);
        if (b->held == 1)
            b->pos -= line;
        else if (b->held == 2)
            b->pos += line;
        else if (b->held == 3)
            b->pos -= b->page;
        else
            b->pos += b->page;
        break;
    default:
        return 0;
    }
    gui_sb_clamp(b);
    if (b->pos != old || e->type == GUI_EV_PRESS)
        gui_damage(w);
    return 1;
}
