/* MSCDEX of GLOS's own (docs/v1/native-dos.md, N4's CD-ROM, step C5): INT 2Fh 15xxh for the system VM's programs on a
 * native boot, over kernel/drv/cdrom.c's drives, which GLOS's DOS letters after the disks (vdos.c).
 *
 * Answered: 1500h (the drives and the first letter), 1501h (the units and the driver's header), 1502h-1504h (the
 * disc's copyright, abstract and bibliography file names), 1505h (a volume descriptor), 1506h/1507h (debugging:
 * nothing), 1508h (sectors read), 1509h (refused: no writing), 150Bh (a drive is a CD's: BX ADADh), 150Ch (version
 * 2.23), 150Dh (the letters), 150Eh (the primary descriptor, always), 1510h (a device request: the CD driver's
 * own, as MSCDEX passes it on). A program that calls the driver itself (its header's strategy and interrupt
 * routines, from 1501h or IOCTL input 0) reaches the same requests: the two routines are ARPLs in the header's
 * block (vdos.c).
 *
 * Device requests: IOCTL input 0 (the header), 1 (the head's place), 4 (audio channels), 6 (the device's status),
 * 7 (sector size), 8 (volume size), 9 (media changed), 10 and 11 (the disc's and a track's audio layout, from its
 * TOC), 12 (the Q channel), 15 (audio status); IOCTL output 0 (eject), 1 (lock: nothing to lock), 2 (reset), 5
 * (close the tray); open and close; READ LONG (cooked sectors, by HSG or Red Book addresses) and its prefetch;
 * SEEK; PLAY AUDIO, STOP AUDIO (a second one forgets the play), RESUME. The audio is the drive's own, by its own
 * output (C6 adds the digital way). */
#include <string.h>

#include "cdrom.h"
#include "kprintf.h"
#include "vm.h"
#include "../ndos/dos21.h"

#define UNITS 4

static int units;
static signed char letter[UNITS];
static unsigned hdr_seg;
static struct {
    unsigned long last;                 /* the last sector read or sought (IOCTL input 1) */
    unsigned long play_from, play_to;   /* the last PLAY's, for RESUME and audio status */
    int paused, played;
} st[UNITS];
static u32 said;

static u32 lin(unsigned seg, unsigned off) { return (u32)seg * 16 + off; }
static u32 far(u32 at) { return (u32)vm_rd16(at + 2) * 16 + vm_rd16(at); }
static void wr32(u32 at, u32 v) { vm_wr16(at, (u16)v), vm_wr16(at + 2, (u16)(v >> 16)); }
static u32 rd32(u32 at) { return vm_rd16(at) | (u32)vm_rd16(at + 2) << 16; }
static int in_vm(u32 at, u32 n) { return at + n <= 0x110000u && at + n >= at; }

/* Red Book (frame, second, minute, 0 as a dword) from a sector, and back: the 150 frames of the lead-in. */
static u32 redbook(unsigned long lba)
{
    unsigned long f = lba + 150;
    return (u32)(f % 75) | (u32)(f / 75 % 60) << 8 | (u32)(f / 4500) << 16;
}

static unsigned long from_redbook(u32 rb)
{
    unsigned long f = (rb >> 16 & 0xFF) * 4500UL + (rb >> 8 & 0xFF) * 75UL + (rb & 0xFF);
    return f >= 150 ? f - 150 : 0;
}

static unsigned long addr(int mode, u32 v) { return mode ? from_redbook(v) : v; }

/* The units' letters now (-1: a unit not in use; images come and go: C7): the header's first letter and count. */
void mscdex_units(const signed char *letters)
{
    u32 h = lin(hdr_seg, 0);
    int k, first = -1, n = 0;

    for (k = 0; k < UNITS; k++) {
        letter[k] = letters[k];
        if (letters[k] >= 0) {
            n++;
            if (first < 0 || letters[k] < first)
                first = letters[k];
        }
    }
    units = n ? UNITS : 0;                      /* (the slots looked through; count() the ones in use) */
    if (hdr_seg) {
        vm_wr8(h + 20, (u8)(first >= 0 ? first + 1 : 0));
        vm_wr8(h + 21, (u8)n);
    }
}

static int count(void)
{
    int k, n = 0;
    for (k = 0; k < units; k++)
        n += letter[k] >= 0;
    return n;
}

static int first_letter(void)
{
    int k, f = -1;
    for (k = 0; k < units; k++)
        if (letter[k] >= 0 && (f < 0 || letter[k] < f))
            f = letter[k];
    return f;
}

void mscdex_setup(unsigned seg, int n, const signed char *letters)
{
    u32 h = lin(seg, 0);

    (void)n;
    hdr_seg = seg;
    wr32(h, 0xFFFFFFFFu);                       /* (the last in no chain of DOS's) */
    vm_wr16(h + 4, 0xC800);                     /* character device, IOCTL, open and close */
    vm_wr16(h + 6, 0x16);                       /* strategy and interrupt: the ARPLs at +16h, +18h */
    vm_wr16(h + 8, 0x18);
    memcpy(vm_ptr(h + 10), "GLOSCD01", 8);
    vm_wr16(h + 18, 0);
    vm_wr8(h + 0x16, 0x63), vm_wr8(h + 0x17, 0xC0);
    vm_wr8(h + 0x18, 0x63), vm_wr8(h + 0x19, 0xC0);
    mscdex_units(letters);                      /* (+20: the first letter, 1 = A:; +21: the units) */
    kprintf("GLOS-MSCDEX units=%d first=%c header=%04x:0000\n", count(), count() ? 'A' + first_letter() : '-', seg);
}

static int unit_of(unsigned drive)
{
    int k;
    for (k = 0; k < units; k++)
        if (letter[k] >= 0 && (unsigned)letter[k] == drive)
            return k;
    return -1;
}

/* A device request at linear address r for unit u (MSCDEX's 1510h, or the driver called itself). */
void mscdex_request(u32 r, int u)
{
    unsigned cmd = vm_rd8(r + 2), status = 0x0100, err = 0;
    unsigned char b[16];
    unsigned long n;

    if (u < 0 || u >= units) {
        vm_wr16(r + 3, 0x8100 | 0x01);          /* (unknown unit) */
        return;
    }
    switch (cmd) {
    case 3: {                                   /* IOCTL input */
        u32 t = far(r + 14), code = vm_rd8(t);
        if (!in_vm(t, 16)) {
            err = 0x0C;
            break;
        }
        switch (code) {
        case 0:
            wr32(t + 1, (u32)hdr_seg << 16);
            break;
        case 1:
            wr32(t + 2, vm_rd8(t + 1) ? redbook(st[u].last) : (u32)st[u].last);
            break;
        case 4:
            vm_wr8(t + 1, 0), vm_wr8(t + 2, 0xFF), vm_wr8(t + 3, 1), vm_wr8(t + 4, 0xFF);
            vm_wr8(t + 5, 2), vm_wr8(t + 6, 0), vm_wr8(t + 7, 3), vm_wr8(t + 8, 0);
            break;
        case 6: {
            unsigned e2;
            wr32(t + 1, 0x00000212u | (cdrom_media(u, &e2) ? 0 : 0x800u));     /* (unlocked, data and audio, HSG
                                                                                    and Red Book; no disc: bit 11) */
            break;
        }
        case 7:
            vm_wr16(t + 2, vm_rd8(t + 1) ? 2352 : 2048);
            break;
        case 8:
            if (cdrom_capacity(u, &n) != 0)
                err = 0x02;
            else
                wr32(t + 1, (u32)n);
            break;
        case 9: {
            unsigned e2;
            (void)cdrom_media(u, &e2);
            vm_wr8(t + 1, cdrom_changed(u) ? 0xFF : 1);
            break;
        }
        case 10:
        case 11: {
            static unsigned char toc[804];
            int len = cdrom_toc(u, toc, sizeof toc, 1), k, nt;
            if (len < 12) {
                err = 0x02;
                break;
            }
            nt = (len - 4) / 8;
            if (code == 10) {                   /* lowest and highest tracks, the lead-out's start */
                vm_wr8(t + 1, toc[2]), vm_wr8(t + 2, toc[3]);
                for (k = 0; k < nt; k++)
                    if (toc[4 + k * 8 + 2] == 0xAA)
                        wr32(t + 3, (u32)toc[4 + k * 8 + 7] | (u32)toc[4 + k * 8 + 6] << 8 | (u32)toc[4 + k * 8 + 5] << 16);
                break;
            }
            for (k = 0; k < nt && toc[4 + k * 8 + 2] != vm_rd8(t + 1); k++)
                ;
            if (k == nt) {
                err = 0x0C;
                break;
            }
            wr32(t + 2, (u32)toc[4 + k * 8 + 7] | (u32)toc[4 + k * 8 + 6] << 8 | (u32)toc[4 + k * 8 + 5] << 16);
            vm_wr8(t + 6, (u8)((toc[4 + k * 8 + 1] & 0x0F) << 4 | (toc[4 + k * 8 + 1] >> 4)));   /* (control, ADR) */
            break;
        }
        case 12:
            if (cdrom_subq(u, b) != 0) {
                memset(b, 0, sizeof b);
            }
            vm_wr8(t + 1, (u8)((b[5] & 0x0F) << 4 | b[5] >> 4));
            vm_wr8(t + 2, b[6]), vm_wr8(t + 3, b[7]);
            vm_wr8(t + 4, b[13]), vm_wr8(t + 5, b[14]), vm_wr8(t + 6, b[15]), vm_wr8(t + 7, 0);  /* (in the track) */
            vm_wr8(t + 8, b[9]), vm_wr8(t + 9, b[10]), vm_wr8(t + 10, b[11]);                    /* (on the disc) */
            break;
        case 15:
            vm_wr16(t + 1, st[u].paused ? 1 : 0);
            wr32(t + 3, redbook(st[u].play_from));
            wr32(t + 7, redbook(st[u].play_to));
            break;
        default:
            err = 0x03;
        }
        break;
    }
    case 12: {                                  /* IOCTL output */
        u32 t = far(r + 14), code = vm_rd8(t);
        if (code == 0 || code == 5)
            cdrom_tray(u, code == 0);
        else if (code != 1 && code != 2 && code != 3)
            err = 0x03;
        break;
    }
    case 13:                                    /* open, close */
    case 14:
        break;
    case 128:                                   /* READ LONG */
    case 130: {                                 /* ... its prefetch */
        unsigned long at = addr(vm_rd8(r + 13), rd32(r + 20));
        unsigned cnt = vm_rd16(r + 18);
        u32 dst = far(r + 14);
        if (vm_rd8(r + 24) != 0) {
            err = 0x03;                         /* (raw: not yet) */
            break;
        }
        st[u].last = at;
        if (cmd == 130 || !cnt)
            break;
        if (!in_vm(dst, (u32)cnt * 2048) || cdrom_read(u, at, cnt, vm_ptr(dst)) != 0)
            err = 0x0B;                         /* (read fault) */
        break;
    }
    case 131:                                   /* SEEK */
        st[u].last = addr(vm_rd8(r + 13), rd32(r + 20));
        break;
    case 132: {                                 /* PLAY AUDIO */
        unsigned long from = addr(vm_rd8(r + 13), rd32(r + 14)), cnt = rd32(r + 18);
        int k = cdrom_play(u, from, cnt);
        if (k != 0)
            err = k == 2 ? 0x02 : 0x0C;
        else
            st[u].play_from = from, st[u].play_to = from + cnt, st[u].paused = 0, st[u].played = 1;
        break;
    }
    case 133:                                   /* STOP AUDIO: a pause; a second, the play forgotten */
        if (st[u].paused)
            cdrom_stop(u), st[u].played = 0, st[u].paused = 0;
        else if (st[u].played && cdrom_pause(u, 0) == 0)
            st[u].paused = 1;
        break;
    case 136:                                   /* RESUME AUDIO */
        if (!st[u].paused || cdrom_pause(u, 1) != 0)
            err = 0x0C;
        else
            st[u].paused = 0;
        break;
    default:
        err = 0x03;
    }
    if (cmd == 3 || cmd == 132 || cmd == 136) { /* busy while the drive plays */
        if (cdrom_subq(u, b) == 0 && b[1] == 0x11)
            status |= 0x0200;
    }
    vm_wr16(r + 3, err ? 0x8100 | err : status);
    if (!(said & (1u << (cmd & 31))) && cmd != 3) {
        said |= 1u << (cmd & 31);
        kprintf("GLOS-MSCDEX request cmd=%u unit=%d status=%04x\n", cmd, u, vm_rd16(r + 3));
    }
}

int mscdex_int2f(struct dos_regs86 *r)
{
    unsigned al = r->ax & 0xFF;
    int u, k;
    static unsigned char sec[2048];

    if (!count() || (r->ax >> 8) != 0x15)
        return 0;
    if (!(said & 0x80000000u)) {
        said |= 0x80000000u;
        kprintf("GLOS-MSCDEX first ax=%04x\n", r->ax);
    }
    switch (al) {
    case 0x00:
        r->bx = (unsigned)count(), r->cx = (unsigned)first_letter();
        return 1;
    case 0x01: {
        int i = 0;
        for (k = 0; k < units; k++)
            if (letter[k] >= 0) {
                u32 at = lin(r->es, r->bx) + 5u * i++;
                vm_wr8(at, (u8)k);
                wr32(at + 1, (u32)hdr_seg << 16);
            }
        return 1;
    }
    case 0x02:
    case 0x03:
    case 0x04:
    case 0x05:
    case 0x08:
        if ((u = unit_of(r->cx)) < 0) {
            r->ax = 0x0F, r->flags |= 1;
            return 1;
        }
        if (al == 0x08) {
            unsigned long at = (unsigned long)r->si << 16 | r->di;
            u32 dst = lin(r->es, r->bx);
            if (!in_vm(dst, (u32)r->dx * 2048) || cdrom_read(u, at, r->dx, vm_ptr(dst)) != 0)
                r->ax = 0x15, r->flags |= 1;
            else
                r->flags &= ~1u;
            return 1;
        }
        if (cdrom_read(u, 16 + (al == 0x05 ? r->dx : 0), 1, sec) != 0) {
            r->ax = 0x15, r->flags |= 1;
            return 1;
        }
        r->flags &= ~1u;
        if (al == 0x05) {                       /* a descriptor: AX its type (1 primary, FFh the set's end) */
            memcpy(vm_ptr(lin(r->es, r->bx)), sec, 2048);
            r->ax = sec[0] == 1 ? 1 : sec[0] == 0xFF ? 0xFF : 0;
        } else {                                /* the copyright, abstract or bibliography file's name */
            unsigned at = al == 0x02 ? 702 : al == 0x03 ? 739 : 776;
            memcpy(vm_ptr(lin(r->es, r->bx)), sec + at, 37);
            vm_wr8(lin(r->es, r->bx) + 37, 0);
        }
        return 1;
    case 0x06:
    case 0x07:
        return 1;
    case 0x09:
        r->ax = 0x05, r->flags |= 1;            /* (no writing to a CD) */
        return 1;
    case 0x0B:
        r->ax = unit_of(r->cx) >= 0 ? 0x5AD8 : 0, r->bx = 0xADAD;
        return 1;
    case 0x0C:
        r->bx = 0x0217;
        return 1;
    case 0x0D: {
        int i = 0, l;
        for (l = 0; l < 26; l++)                /* (in the letters' order) */
            for (k = 0; k < units; k++)
                if (letter[k] == l)
                    vm_wr8(lin(r->es, r->bx) + i++, (u8)l);
        return 1;
    }
    case 0x0E:
        if (r->bx == 0)
            r->dx = 0x0100;                     /* (the primary descriptor) */
        r->flags &= ~1u;
        return 1;
    case 0x10:
        if ((u = unit_of(r->cx)) < 0) {
            r->ax = 0x0F, r->flags |= 1;
            return 1;
        }
        mscdex_request(lin(r->es, r->bx), u);
        r->flags &= ~1u;
        return 1;
    }
    r->ax = 0x01, r->flags |= 1;                /* (a function MSCDEX 2.23 hasn't got) */
    return 1;
}

/* The driver called itself: its strategy (ES:BX the request, kept) and interrupt routine (vdos.c's ARPLs). */
static u32 kept;

void mscdex_strategy(unsigned es, unsigned bx) { kept = lin(es, bx); }

void mscdex_interrupt(void)
{
    if (kept)
        mscdex_request(kept, vm_rd8(kept + 1));
}
