/* CENSUS - which DOS services each program asks for (docs/v1/native-dos.md, N0's census).
 *
 * A resident program that hooks INT 21h, 2Fh, 20h, 25h, 26h and 27h, and INT 33h and 67h when something is
 * there already, and counts every call by the program running (DOS's current PSP), the vector, the function
 * and where the call came from. A call seen for the first time is written to COM1 there and then (Loop A ends
 * the PC when a program says HX-DONE, before the program ends); when a program ends (INT 21h 00h, 31h or 4Ch;
 * INT 20h or 27h), its counts go to COM1 and are forgotten:
 *     GLOS-CENSUS new prog=NAME psp=SSSS int=21 ax=3D00 from=P
 *     GLOS-CENSUS prog=NAME psp=SSSS int=21 ax=3D00 from=P n=12
 *     GLOS-CENSUS end prog=NAME psp=SSSS entries=N calls=M dropped=K top21=SSSS:OOOO
 * - ax: the whole AX where AL picks a subfunction (INT 21h 0Ch, 33h, 43h, 44h, 4Bh, 57h, 58h, 5Dh to 5Fh, 63h,
 *   65h, 66h, 70h, 71h, 73h; every INT 2Fh, 33h and 67h call), else AH with AL 00h; 0000 for INT 20h, 25h,
 *   26h and 27h.
 * - from: P the program's own memory block, D the segment INT 21h pointed to when CENSUS was installed (DOS's
 *   kernel), O anywhere else (a DPMI host's real-mode code reflecting a protected-mode call, a TSR, the BIOS).
 * - top21: who INT 21h points to at the end. A program that hooks INT 21h after CENSUS (DOSLFN started later)
 *   sees calls first, and those it answers itself never reach CENSUS: load such programs before it.
 * - dropped: calls not counted because the table (768 entries) was full.
 *
 *     CENSUS      installs it (once; about 9 KB stays resident)
 *     CENSUS /D   writes every count still held (the shell's, TSRs') and forgets them
 *
 * The handlers run with interrupts off, as INT left them (a first call's line too), except while a program's
 * counts are written. Nothing is written to the screen once installed (Loop A's frames stay the same). They touch no
 * 32-bit register (8086 code: -0 in the Makefile's rule), so a caller that keeps something in a
 * register's upper half (a DPMI host's real-mode code) gets it back. Open Watcom, real mode. */
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

#pragma off (check_stack)

#define NENT 768

struct ent {
    unsigned psp, ax;
    unsigned char vec, from;
    unsigned long n;
};

typedef void (__interrupt __far *vec_t)(void);

static vec_t old21, old2f, old20, old25, old26, old27, old33, old67;
static struct ent tab[NENT];
static unsigned nent, last;
static unsigned long dropped;
static unsigned dos_seg;                /* INT 21h's segment at install: DOS's kernel */
static unsigned __far *sda_psp;         /* DOS's current PSP in its swappable data area; 0: ask DOS (62h) */
static unsigned last_psp;               /* the PSP the last INT 21h call found: for the other vectors without
                                           an SDA (DOS may be busy when they are called) */
static unsigned char busy;              /* a dump under way, with interrupts on: nothing is counted */
static char line[112];
static unsigned char sub21[256];        /* 1: AL picks INT 21h's subfunction */

static const unsigned char subs[] = {
    0x0C, 0x33, 0x43, 0x44, 0x4B, 0x57, 0x58, 0x5D, 0x5E, 0x5F, 0x63, 0x65, 0x66, 0x70, 0x71, 0x73
};

/* ---- the resident part (DS is ours, SS the caller's: no pointers to locals) */

static unsigned psp_by_dos(void)
{
    unsigned found;                     /* not "seg": the assembler's operator */
    _asm {
        mov ah, 62h
        pushf
        call dword ptr old21
        mov found, bx
    }
    return found;
}

static unsigned cur_psp(int may_ask_dos)
{
    if (sda_psp)
        return *sda_psp;
    if (may_ask_dos)
        last_psp = psp_by_dos();
    return last_psp;
}

static unsigned char from_of(unsigned psp, unsigned cs)
{
    unsigned size;

    if (cs == dos_seg)
        return 'D';
    if (psp && cs >= psp) {
        size = *(unsigned __far *)MK_FP(psp - 1, 3);
        if (cs - psp < size)
            return 'P';
    }
    return 'O';
}

static char *entry(char *p, struct ent *e, int count);
static void ser(const char *s);

static void note(unsigned psp, unsigned char vec, unsigned ax, unsigned char from)
{
    struct ent *e;
    unsigned i;

    e = &tab[last];
    if (last < nent && e->psp == psp && e->ax == ax && e->vec == vec && e->from == from) {
        e->n++;
        return;
    }
    for (i = 0, e = tab; i < nent; i++, e++)
        if (e->psp == psp && e->ax == ax && e->vec == vec && e->from == from) {
            e->n++;
            last = i;
            return;
        }
    if (nent == NENT) {
        dropped++;
        return;
    }
    e = &tab[nent];
    e->psp = psp;
    e->ax = ax;
    e->vec = vec;
    e->from = from;
    e->n = 1;
    last = nent++;
    entry(line, e, 0)[0] = 0;
    ser(line);
}

/* To COM1, polled. It returns only when the transmitter is empty (LSR bit 6, not just the holding register's
 * bit 5): a program that sets the UART up next (SERSAY) resets its FIFO, and the line's CR LF went with it, the
 * program's own line then glued to the end of this one (Loop A missed an HX-DONE that way). */
static void ser(const char *s)
{
    unsigned n;

    for (; *s; s++) {
        n = 0;
        while (!(inp(0x3FD) & 0x20) && ++n < 60000u)
            ;
        outp(0x3F8, *s);
    }
    n = 0;
    while (!(inp(0x3FD) & 0x40) && ++n < 60000u)
        ;
}

static char *str(char *p, const char *s)
{
    while (*s)
        *p++ = *s++;
    return p;
}

static char *hex(char *p, unsigned v, int digits)
{
    while (digits--)
        *p++ = "0123456789ABCDEF"[(v >> (digits * 4)) & 15];
    return p;
}

static char *dec(char *p, unsigned long v)
{
    char t[11];
    int i = 0;

    do
        t[i++] = (char)('0' + v % 10);
    while (v /= 10);
    while (i)
        *p++ = t[--i];
    return p;
}

static char *name(char *p, unsigned psp)
{
    unsigned char __far *m = (unsigned char __far *)MK_FP(psp - 1, 0);
    int i;

    if (!psp)
        return str(p, "all");
    if (m[0] != 'M' && m[0] != 'Z')
        return str(p, "?");
    for (i = 0; i < 8 && m[8 + i] > ' ' && m[8 + i] < 127; i++)
        *p++ = m[8 + i];
    if (!i)
        *p++ = '?';
    return p;
}

/* "GLOS-CENSUS new prog=NAME psp=SSSS int=VV ax=AAAA from=F\r\n" at p, or with count "GLOS-CENSUS prog=...
 * from=F n=N\r\n"; returns its end. */
static char *entry(char *p, struct ent *e, int count)
{
    p = str(p, count ? "GLOS-CENSUS prog=" : "GLOS-CENSUS new prog=");
    p = name(p, e->psp);
    p = str(p, " psp=");
    p = hex(p, e->psp, 4);
    p = str(p, " int=");
    p = hex(p, e->vec, 2);
    p = str(p, " ax=");
    p = hex(p, e->ax, 4);
    p = str(p, " from=");
    *p++ = e->from;
    if (count) {
        p = str(p, " n=");
        p = dec(p, e->n);
    }
    return str(p, "\r\n");
}

/* psp's counts (all of them with psp 0) to COM1, and forgets them. */
static void dump(unsigned psp)
{
    unsigned i, k = 0;
    unsigned long calls = 0;
    unsigned __far *ivt = (unsigned __far *)MK_FP(0, 0x21 * 4);
    struct ent *e, *z;
    char *p;

    busy = 1;
    _enable();
    for (i = nent; i--; ) {
        e = &tab[i];
        if (psp && e->psp != psp)
            continue;
        entry(line, e, 1)[0] = 0;
        ser(line);
        k++;
        calls += e->n;
        z = &tab[--nent];               /* the last entry into the hole: it was looked at already */
        e->psp = z->psp;
        e->ax = z->ax;
        e->vec = z->vec;
        e->from = z->from;
        e->n = z->n;
    }
    last = 0;
    p = str(line, "GLOS-CENSUS end prog=");
    p = name(p, psp);
    p = str(p, " psp=");
    p = hex(p, psp, 4);
    p = str(p, " entries=");
    p = dec(p, k);
    p = str(p, " calls=");
    p = dec(p, calls);
    p = str(p, " dropped=");
    p = dec(p, dropped);
    p = str(p, " top21=");
    p = hex(p, ivt[1], 4);
    *p++ = ':';
    p = hex(p, ivt[0], 4);
    p = str(p, "\r\n");
    *p = 0;
    ser(line);
    _disable();
    busy = 0;
}

static void __interrupt __far new21(union INTPACK r)
{
    unsigned char ah = r.h.ah;
    unsigned psp;

    if (!busy) {
        psp = cur_psp(1);
        note(psp, 0x21, sub21[ah] ? r.w.ax : (unsigned)ah << 8, from_of(psp, r.w.cs));
        if (ah == 0x4C || ah == 0x00 || ah == 0x31)
            dump(psp);
    }
    _chain_intr(old21);
}

static void __interrupt __far new2f(union INTPACK r)
{
    unsigned psp;

    if (r.w.ax == 0xC7F1) {                     /* is CENSUS there */
        r.w.ax = 0x55AA;
        return;
    }
    if (r.w.ax == 0xC7F0) {                     /* CENSUS /D */
        if (!busy)
            dump(0);
        r.w.ax = 0x55AA;
        return;
    }
    if (!busy) {
        psp = cur_psp(0);
        note(psp, 0x2F, r.w.ax, from_of(psp, r.w.cs));
    }
    _chain_intr(old2f);
}

static void __interrupt __far new20(union INTPACK r)
{
    unsigned psp;

    if (!busy) {
        psp = cur_psp(0);
        note(psp, 0x20, 0, from_of(psp, r.w.cs));
        dump(psp);
    }
    _chain_intr(old20);
}

static void __interrupt __far new27(union INTPACK r)
{
    unsigned psp;

    if (!busy) {
        psp = cur_psp(0);
        note(psp, 0x27, 0, from_of(psp, r.w.cs));
        dump(psp);
    }
    _chain_intr(old27);
}

static void __interrupt __far new25(union INTPACK r)
{
    unsigned psp;

    if (!busy) {
        psp = cur_psp(0);
        note(psp, 0x25, 0, from_of(psp, r.w.cs));
    }
    _chain_intr(old25);
}

static void __interrupt __far new26(union INTPACK r)
{
    unsigned psp;

    if (!busy) {
        psp = cur_psp(0);
        note(psp, 0x26, 0, from_of(psp, r.w.cs));
    }
    _chain_intr(old26);
}

static void __interrupt __far new33(union INTPACK r)
{
    unsigned psp;

    if (!busy) {
        psp = cur_psp(0);
        note(psp, 0x33, r.w.ax, from_of(psp, r.w.cs));
    }
    _chain_intr(old33);
}

static void __interrupt __far new67(union INTPACK r)
{
    unsigned psp;

    if (!busy) {
        psp = cur_psp(0);
        note(psp, 0x67, r.w.ax, from_of(psp, r.w.cs));
    }
    _chain_intr(old67);
}

/* ---- the transient part */

static int null_vec(vec_t v)
{
    return !FP_SEG(v) && !FP_OFF(v);
}

int main(int argc, char **argv)
{
    union REGS r;
    struct SREGS s;
    unsigned psp, i;
    int sda = 0, h33 = 0, h67 = 0;

    r.w.ax = 0xC7F1;
    int86(0x2F, &r, &r);
    if (argc > 1 && !stricmp(argv[1], "/D")) {
        if (r.w.ax != 0x55AA) {
            puts("CENSUS: not resident");
            return 1;
        }
        r.w.ax = 0xC7F0;
        int86(0x2F, &r, &r);
        return 0;
    }
    if (argc > 1) {
        puts("CENSUS [/D]: counts the DOS services each program asks for, on COM1");
        return 2;
    }
    if (r.w.ax == 0x55AA) {
        puts("CENSUS: already resident");
        return 0;
    }
    for (i = 0; i < sizeof subs; i++)
        sub21[subs[i]] = 1;
    r.h.ah = 0x62;
    intdos(&r, &r);
    psp = r.w.bx;
    last_psp = psp;
    old21 = _dos_getvect(0x21);
    dos_seg = FP_SEG(old21);
    segread(&s);
    r.w.ax = 0x5D06;                            /* the swappable data area: the current PSP at 10h */
    intdosx(&r, &r, &s);
    if (!r.w.cflag) {
        sda_psp = (unsigned __far *)MK_FP(s.ds, r.w.si + 0x10);
        if (*sda_psp == psp)
            sda = 1;
        else
            sda_psp = 0;
    }
    old2f = _dos_getvect(0x2F);
    old20 = _dos_getvect(0x20);
    old25 = _dos_getvect(0x25);
    old26 = _dos_getvect(0x26);
    old27 = _dos_getvect(0x27);
    old33 = _dos_getvect(0x33);
    old67 = _dos_getvect(0x67);
    _disable();
    _dos_setvect(0x21, new21);
    _dos_setvect(0x2F, new2f);
    _dos_setvect(0x20, new20);
    _dos_setvect(0x25, new25);
    _dos_setvect(0x26, new26);
    _dos_setvect(0x27, new27);
    if (!null_vec(old33)) {
        _dos_setvect(0x33, new33);
        h33 = 1;
    }
    if (!null_vec(old67)) {
        _dos_setvect(0x67, new67);
        h67 = 1;
    }
    _enable();
    sprintf(line, "GLOS-CENSUS install psp=%04X dos=%04X sda=%d hooks=21,2F,20,25,26,27%s%s\r\n", psp, dos_seg,
            sda, h33 ? ",33" : "", h67 ? ",67" : "");
    ser(line);
    _dos_keep(0, *(unsigned __far *)MK_FP(psp - 1, 3));        /* the block as DOS gave it: code, data, stack */
    return 0;
}
