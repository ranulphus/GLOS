/* CDTEST: MSCDEX's INT 2Fh 15xxh asked as programs ask it (GLOS's own natively: kernel/vm/mscdex.c, N4's C5), for
 * loopa's cdrom suite. Real mode, Open Watcom.
 *
 *   CDTEST             the first CD drive MSCDEX names
 *
 * Each check is a line "CDTEST name PASS|FAIL what"; then "CDTEST done failures=N", N the exit code. Asked: 1500h
 * (drives, the first letter), 150Bh (that drive is a CD's, C: isn't; BX ADADh), 150Ch (2.23), 150Dh (the letters),
 * 1505h (the primary descriptor: "CD001"), 1508h (sector 16 read, as 1505h's), 1510h's IOCTL input 6 (a disc in),
 * 7 (2048), 8 (the volume's size), 9 (media changed: an answer), READ LONG (sector 16 again), 1501h (the driver's
 * header: "GLOSCD01"), and the driver's own strategy and interrupt routines called with IOCTL input 8 (the same
 * size). Then README.TXT read through DOS from the drive: its first bytes "hello". */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

static int fails;

static void check(const char *name, int ok, const char *what)
{
    printf("CDTEST %s %s %s\n", name, ok ? "PASS" : "FAIL", what);
    fails += !ok;
}

static unsigned char sec[2048], sec2[2048];

/* A device request: 1510h (MSCDEX passes it on), or the driver's own routines (direct). Byte-packed: DOS's request
   header has its status word at offset 3. */
#pragma pack(push, 1)
struct req {
    unsigned char len, unit, cmd;
    unsigned short status;
    unsigned char resv[8];
    unsigned char media;
    void __far *addr;
    unsigned short count;
    unsigned long start;
    unsigned char mode, isize, iskip;
};
#pragma pack(pop)

static unsigned short ioctl_in(unsigned drive, unsigned char __far *cb, unsigned n)
{
    static struct req q;
    union REGS r;
    struct SREGS s;
    memset(&q, 0, sizeof q);
    q.len = 26, q.cmd = 3, q.addr = cb, q.count = (unsigned short)n;
    segread(&s);
    r.x.ax = 0x1510, r.x.cx = drive, r.x.bx = FP_OFF((void __far *)&q);
    s.es = FP_SEG((void __far *)&q);
    int86x(0x2F, &r, &r, &s);
    return q.status;
}

int main(void)
{
    union REGS r;
    struct SREGS s;
    unsigned drive, k, ok;
    unsigned char letters[26], cb[16];
    unsigned long size8 = 0;
    char what[96];

    r.x.ax = 0x1500, r.x.bx = 0;
    int86(0x2F, &r, &r);
    drive = r.x.cx;
    sprintf(what, "%u drive(s), the first %c:", r.x.bx, 'A' + drive);
    check("1500h", r.x.bx >= 1, what);
    if (r.x.bx < 1) {
        printf("CDTEST done failures=%d\n", fails);
        return fails;
    }
    r.x.ax = 0x150B, r.x.cx = drive;
    int86(0x2F, &r, &r);
    k = r.x.ax;
    ok = k != 0 && r.x.bx == 0xADAD;
    r.x.ax = 0x150B, r.x.cx = 2;
    int86(0x2F, &r, &r);
    check("150Bh", ok && r.x.ax == 0, "the drive a CD's, C: not");
    r.x.ax = 0x150C, r.x.bx = 0;
    int86(0x2F, &r, &r);
    sprintf(what, "version %u.%02u", r.h.bh, r.h.bl);
    check("150Ch", r.x.bx == 0x0217, what);
    segread(&s);
    memset(letters, 0xEE, sizeof letters);
    r.x.ax = 0x150D, r.x.bx = FP_OFF((void __far *)letters);
    s.es = FP_SEG((void __far *)letters);
    int86x(0x2F, &r, &r, &s);
    check("150Dh", letters[0] == drive, "the letters");
    segread(&s);
    r.x.ax = 0x1505, r.x.cx = drive, r.x.dx = 0, r.x.bx = FP_OFF((void __far *)sec);
    s.es = FP_SEG((void __far *)sec);
    int86x(0x2F, &r, &r, &s);
    check("1505h", !r.x.cflag && r.x.ax == 1 && !memcmp(sec + 1, "CD001", 5), "the primary volume descriptor");
    segread(&s);
    r.x.ax = 0x1508, r.x.cx = drive, r.x.dx = 1, r.x.si = 0, r.x.di = 16, r.x.bx = FP_OFF((void __far *)sec2);
    s.es = FP_SEG((void __far *)sec2);
    int86x(0x2F, &r, &r, &s);
    check("1508h", !r.x.cflag && !memcmp(sec, sec2, 2048), "sector 16, as 1505h gave it");
    memset(cb, 0, sizeof cb), cb[0] = 6;
    k = ioctl_in(drive, cb, 5);
    check("ioctl-6", k == 0x0100 && !(cb[2] & 0x08), "done, a disc in");
    memset(cb, 0, sizeof cb), cb[0] = 7;
    k = ioctl_in(drive, cb, 4);
    check("ioctl-7", k == 0x0100 && (cb[2] | cb[3] << 8) == 2048, "a sector's 2048 bytes");
    memset(cb, 0, sizeof cb), cb[0] = 8;
    k = ioctl_in(drive, cb, 5);
    size8 = cb[1] | (unsigned long)cb[2] << 8 | (unsigned long)cb[3] << 16 | (unsigned long)cb[4] << 24;
    sprintf(what, "the volume's %lu sectors", size8);
    check("ioctl-8", k == 0x0100 && size8 > 16, what);
    memset(cb, 0, sizeof cb), cb[0] = 9;
    k = ioctl_in(drive, cb, 2);
    check("ioctl-9", k == 0x0100 && (cb[1] == 1 || cb[1] == 0xFF || cb[1] == 0), "media changed: answered");
    {
        static struct req q;
        memset(&q, 0, sizeof q);
        memset(sec2, 0, sizeof sec2);
        q.len = 27, q.cmd = 128, q.mode = 0, q.addr = (void __far *)sec2, q.count = 1, q.start = 16;
        segread(&s);
        r.x.ax = 0x1510, r.x.cx = drive, r.x.bx = FP_OFF((void __far *)&q);
        s.es = FP_SEG((void __far *)&q);
        int86x(0x2F, &r, &r, &s);
        check("read-long", q.status == 0x0100 && !memcmp(sec, sec2, 2048), "sector 16 by READ LONG");
    }
    {
        static unsigned char list[5 * 8];
        unsigned char __far *hdr;
        segread(&s);
        r.x.ax = 0x1501, r.x.bx = FP_OFF((void __far *)list);
        s.es = FP_SEG((void __far *)list);
        int86x(0x2F, &r, &r, &s);
        hdr = *(unsigned char __far * __far *)(list + 1);
        check("1501h", hdr && !_fmemcmp(hdr + 10, "GLOSCD01", 8), "the driver's header");
        if (hdr) {
            static struct req q;
            void (__far *strat)(void) = (void (__far *)(void))MK_FP(FP_SEG(hdr), *(unsigned __far *)(hdr + 6));
            void (__far *intr)(void) = (void (__far *)(void))MK_FP(FP_SEG(hdr), *(unsigned __far *)(hdr + 8));
            void __far *qp = (void __far *)&q;
            unsigned qs = FP_SEG(qp), qo = FP_OFF(qp);
            unsigned long size = 0;
            memset(&q, 0, sizeof q);
            memset(cb, 0, sizeof cb), cb[0] = 8;
            q.len = 26, q.unit = list[0], q.cmd = 3, q.addr = (void __far *)cb, q.count = 5;
            _asm {                              /* (ES:BX the request: set and both calls made in one go) */
                push es
                push bx
                mov es, qs
                mov bx, qo
                call dword ptr strat
                call dword ptr intr
                pop bx
                pop es
            }
            size = cb[1] | (unsigned long)cb[2] << 8 | (unsigned long)cb[3] << 16 | (unsigned long)cb[4] << 24;
            sprintf(what, "IOCTL input 8 through the driver itself: %lu", size);
            check("direct", q.status == 0x0100 && size == size8, what);
        }
    }
    {
        char path[16];
        int h, n;
        sprintf(path, "%c:\\README.TXT", 'A' + drive);
        memset(sec2, 0, 16);
        if (_dos_open(path, 0, &h) == 0) {
            n = 0;
            _dos_read(h, sec2, 16, (unsigned *)&n);
            _dos_close(h);
        }
        check("dos-read", !memcmp(sec2, "hello", 5), "README.TXT through DOS");
    }
    printf("CDTEST done failures=%d\n", fails);
    return fails;
}
