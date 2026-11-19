/* The system VM's virtual 8237A channels (vdma.h; docs/v1/vsb.md). The two controllers' registers: 0 to 7 a
 * channel's address and count (two bytes each, by the controller's flip-flop), 8 status and command, 9 request,
 * 10 a channel's mask, 11 its mode, 12 the flip-flop cleared, 13 master clear, 14 every mask cleared, 15 the
 * four masks written. The first controller's are ports 00h to 0Fh (channels 0 to 3, counted in bytes), the
 * second's C0h to DEh, the even ones (channels 4 to 7, counted in words). */
#include "vdma.h"

static int page_chan(u16 port)
{
    switch (port) {
    case 0x87: return 0;
    case 0x83: return 1;
    case 0x81: return 2;
    case 0x82: return 3;
    case 0x8F: return 4;
    case 0x8B: return 5;
    case 0x89: return 6;
    case 0x8A: return 7;
    }
    return -1;
}

/* A controller's register for a port: 0 or 1 the controller, *reg its register; -1: none of theirs. */
static int decode(u16 port, int *reg)
{
    if (port <= 0x0F) {
        *reg = port;
        return 0;
    }
    if (port >= 0xC0 && port <= 0xDF && !(port & 1)) {
        *reg = (port - 0xC0) >> 1;
        return 1;
    }
    return -1;
}

static u16 port_of(int ctl, int reg) { return (u16)(ctl ? 0xC0 + reg * 2 : reg); }

int vdma_port(u16 port)
{
    int reg;
    return page_chan(port) >= 0 || decode(port, &reg) >= 0;
}

void vdma_reset(struct vdma *d, u8 virt, u8 real_masked)
{
    int i;
    for (i = 0; i < 8; i++) {
        struct vdma_chan *c = &d->ch[i];
        c->base_addr = c->base_count = c->cur_addr = c->cur_count = 0;
        c->page = c->mode = c->tc = 0;
        c->masked = 1;
    }
    d->ff[0] = d->ff[1] = 0;
    d->virt = virt;
    d->real_masked = (u8)(real_masked & virt);
}

static void pass(struct vdma_real *real, u16 port, u8 val)
{
    real->port[real->n] = port;
    real->val[real->n++] = val;
}

void vdma_out(struct vdma *d, u16 port, u8 val, struct vdma_real *real)
{
    int reg = 0, ch = page_chan(port), ctl, base, i;
    u8 virt, keep;
    real->n = 0;
    if (ch >= 0) {                              /* a page register */
        if (d->virt >> ch & 1)
            d->ch[ch].page = val;
        else
            pass(real, port, val);
        return;
    }
    if ((ctl = decode(port, &reg)) < 0) {
        pass(real, port, val);
        return;
    }
    base = ctl * 4;
    virt = (u8)((d->virt >> base) & 0x0F);      /* this controller's virtual channels, and of them the ones whose */
    keep = (u8)((d->real_masked >> base) & virt);       /* real channel stays masked */
    if (reg < 8) {                              /* a channel's address or count: a byte of it */
        struct vdma_chan *c = &d->ch[base + reg / 2];
        int high = d->ff[ctl];
        d->ff[ctl] ^= 1;                        /* (the program's one flip-flop, whoever's the register is) */
        if (!(virt >> (reg / 2) & 1)) {
            pass(real, port, val);
            return;
        }
        if (reg & 1) {
            c->base_count = (u16)(high ? (c->base_count & 0x00FF) | val << 8 : (c->base_count & 0xFF00) | val);
            c->cur_count = c->base_count;
        } else {
            c->base_addr = (u16)(high ? (c->base_addr & 0x00FF) | val << 8 : (c->base_addr & 0xFF00) | val);
            c->cur_addr = c->base_addr;
        }
        return;
    }
    switch (reg) {
    case 9:                                     /* a request by software: none is made for a virtual channel */
    case 10:                                    /* a channel's mask */
    case 11:                                    /* a channel's mode */
        if (!(virt >> (val & 3) & 1)) {
            pass(real, port, val);
            break;
        }
        if (reg == 10)
            d->ch[base + (val & 3)].masked = (val >> 2) & 1;
        else if (reg == 11)
            d->ch[base + (val & 3)].mode = val;
        break;
    case 12:                                    /* the flip-flop cleared: the real chip's too */
        d->ff[ctl] = 0;
        pass(real, port, val);
        break;
    case 13:                                    /* master clear: the flip-flop cleared, every channel masked */
        d->ff[ctl] = 0;
        for (i = 0; i < 4; i++)
            if (virt >> i & 1)
                d->ch[base + i].masked = 1, d->ch[base + i].tc = 0;
        if (!virt) {
            pass(real, port, val);
            break;
        }
        pass(real, port_of(ctl, 12), 0);        /* (the real chip: as much of it as is not GLOS's own channel's) */
        pass(real, port_of(ctl, 15), (u8)((0x0F & ~virt) | keep));
        break;
    case 14:                                    /* every mask cleared */
        for (i = 0; i < 4; i++)
            if (virt >> i & 1)
                d->ch[base + i].masked = 0;
        pass(real, port, val);
        for (i = 0; i < 4; i++)                 /* (a real channel that is nobody's stays masked) */
            if (keep >> i & 1)
                pass(real, port_of(ctl, 10), (u8)(4 | i));
        break;
    case 15:                                    /* the four masks */
        for (i = 0; i < 4; i++)
            if (virt >> i & 1)
                d->ch[base + i].masked = (val >> i) & 1;
        pass(real, port, (u8)((val & 0x0F & ~virt) | keep));
        break;
    default:                                    /* (8, the command register) */
        pass(real, port, val);
        break;
    }
}

int vdma_in(struct vdma *d, u16 port, u8 *val, u8 *mask)
{
    int reg = 0, ch = page_chan(port), ctl, base, i;
    u8 virt;
    if (ch >= 0) {
        if (!(d->virt >> ch & 1))
            return 0;
        *val = d->ch[ch].page;
        return 1;
    }
    if ((ctl = decode(port, &reg)) < 0)
        return 0;
    base = ctl * 4;
    virt = (u8)((d->virt >> base) & 0x0F);
    if (reg < 8) {
        const struct vdma_chan *c = &d->ch[base + reg / 2];
        int high = d->ff[ctl];
        u16 v = reg & 1 ? c->cur_count : c->cur_addr;
        d->ff[ctl] ^= 1;
        if (!(virt >> (reg / 2) & 1))
            return 0;
        *val = (u8)(high ? v >> 8 : v);
        return 1;
    }
    if (reg == 8 && virt) {                     /* the status: who has reached the terminal count since it was read */
        *val = 0;
        for (i = 0; i < 4; i++)
            if ((virt >> i & 1) && d->ch[base + i].tc) {
                *val |= (u8)(1 << i);
                d->ch[base + i].tc = 0;
            }
        *mask = (u8)(virt | virt << 4);
        return 2;
    }
    return 0;
}

int vdma_running(const struct vdma *d, int ch) { return ch >= 0 && ch < 8 && (d->virt >> ch & 1) && !d->ch[ch].masked; }

u32 vdma_take(struct vdma *d, int ch, u32 bytes, u32 *phys)
{
    struct vdma_chan *c;
    u32 units, left, to_wrap;
    int wide = ch >= 4;
    if (!vdma_running(d, ch))
        return 0;
    c = &d->ch[ch];
    left = (u32)c->cur_count + 1;
    to_wrap = 0x10000u - c->cur_addr;           /* (the address is sixteen bits: it goes round within its page) */
    units = wide ? bytes / 2 : bytes;
    if (units > left)
        units = left;
    if (units > to_wrap)
        units = to_wrap;
    if (!units)
        return 0;
    *phys = wide ? ((u32)(c->page & 0xFE) << 16) | ((u32)c->cur_addr << 1) : ((u32)c->page << 16) | c->cur_addr;
    c->cur_addr = (u16)(c->cur_addr + units);
    if (units == left) {                        /* the terminal count */
        c->tc = 1;
        if (c->mode & 0x10) {                   /* auto-initialise: from the start again */
            c->cur_addr = c->base_addr;
            c->cur_count = c->base_count;
        } else {
            c->cur_count = 0xFFFF;
            c->masked = 1;
        }
    } else {
        c->cur_count = (u16)(c->cur_count - units);
    }
    return wide ? units * 2 : units;
}
