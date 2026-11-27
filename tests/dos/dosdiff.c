/* dosdiff: what DOS answers where the books leave it open (docs/v1/native-dos.md, NS2's "to check" list), one
 * line a probe, the same on any disk and in any memory: run under MS-DOS 7.1 (the reference: a private boot disk,
 * tools/ndos/msdos-image.sh) and in a native machine (loopa-ndos), and the lines compared. It works in a folder of
 * its own, C:\DDIFF, made and removed. */
#include <dos.h>
#include <i86.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static union REGS r;
static struct SREGS s;
static char buf[256];

/* INT 21h with r and s (DS and ES the program's own unless set); CF in r.x.cflag. */
static void dos(void)
{
    intdosx(&r, &r, &s);
}

static void ds_es(void)
{
    segread(&s);
}

/* (AX only when CF says it is an error code: what DOS leaves in it otherwise isn't the same twice) */
static void line(const char *name)
{
    if (r.x.cflag)
        printf("%-22s cf=1 ax=%04X\n", name, r.x.ax);
    else
        printf("%-22s cf=0\n", name);
}

static void call_dx(unsigned ax, const char *str, unsigned cx)
{
    ds_es();
    r.x.ax = ax;
    r.x.cx = cx;
    r.x.dx = (unsigned)str;
    dos();
}

static void err59(const char *name)
{
    union REGS e;
    struct SREGS es;

    segread(&es);
    e.x.ax = 0x5900;
    e.x.bx = 0;
    intdosx(&e, &e, &es);
    printf("%-22s ax=%04X class=%02X action=%02X locus=%02X\n", name, e.x.ax, e.h.bh, e.h.bl, e.h.ch);
}

static unsigned handle(const char *name, unsigned ax)
{
    call_dx(ax, name, 0);
    return r.x.cflag ? 0xFFFF : r.x.ax;
}

static void close_(unsigned h)
{
    r.x.ax = 0x3E00;
    r.x.bx = h;
    dos();
}

static void parse(const char *str, unsigned al)
{
    unsigned char fcb[37];
    unsigned i;

    ds_es();
    memset(fcb, 'z', sizeof fcb);
    r.x.ax = 0x2900 | al;
    r.x.si = (unsigned)str;
    r.x.di = (unsigned)fcb;
    dos();
    printf("29h %-14s al=%x al=%02X used=%u fcb=", str, al, r.h.al, r.x.si - (unsigned)str);
    for (i = 0; i < 12; i++)
        printf(fcb[i] >= 0x20 && fcb[i] < 0x7F ? "%c" : "<%02X>", fcb[i]);
    printf("\n");
}

static void truename(const char *p)
{
    ds_es();
    memset(buf, 0, sizeof buf);
    r.x.ax = 0x6000;
    r.x.si = (unsigned)p;
    r.x.di = (unsigned)buf;
    dos();
    if (r.x.cflag)
        printf("60h %-18s cf=1 ax=%04X\n", p, r.x.ax);
    else
        printf("60h %-18s cf=0 %s\n", p, buf);
}


/* ---- FCBs (0Fh-17h, 21h-24h, 27h, 28h): programs written for DOS 1. In \DDIFF of the drive given on the
   command line (C: if none), with files made by handle: MS-DOS 7.1 opens no file through an FCB on a FAT32
   volume, so its answers are a floppy's (tools/ndos/msdos-ref.sh), and no line says which drive it was. A
   critical error (no disk; an FCB DOS no longer knows, where SHARE is) fails its call. */
static unsigned char fcb[7 + 37], fdta[700];
static unsigned fdrive;                 /* the drive the FCB probes work on: 0 A: */

static int __far crit(unsigned deverr, unsigned errcode, unsigned __far *devhdr)
{
    (void)deverr, (void)errcode, (void)devhdr;
    return _HARDERR_FAIL;
}

/* An FCB for a name's 11 bytes in the current folder of the current drive, the rest zero; an extended one if
   attr isn't negative. The FCB proper. */
static unsigned char *fcb_make(const char *n11, int attr)
{
    memset(fcb, 0, sizeof fcb);
    if (attr < 0) {
        memcpy(fcb + 1, n11, 11);
        return fcb;
    }
    fcb[0] = 0xFF;
    fcb[6] = (unsigned char)attr;
    memcpy(fcb + 8, n11, 11);
    return fcb + 7;
}

/* A call with DS:DX the FCB as made (CX for the block calls): AL. */
static unsigned fcall(unsigned ah, unsigned cx)
{
    ds_es();
    r.x.ax = ah << 8;
    r.x.cx = cx;
    r.x.dx = (unsigned)fcb;
    dos();
    return r.h.al;
}

#define FW(f, o) (*(unsigned *)((f) + (o)))
#define FL(f, o) (*(unsigned long *)((f) + (o)))

/* A file made by handle with n bytes and attributes. */
static void make(const char *name, const char *data, unsigned n, unsigned attr)
{
    unsigned h = handle(name, 0x3C00);

    ds_es();
    r.x.ax = 0x4000, r.x.bx = h, r.x.cx = n, r.x.dx = (unsigned)data;
    dos();
    close_(h);
    if (attr)
        call_dx(0x4301, name, attr);
}

/* A file's size by a search (FFFFFFFFh: none). */
static unsigned long size_of(const char *name)
{
    call_dx(0x4E00, name, 0x07);
    return r.x.cflag ? 0xFFFFFFFFUL : *(unsigned long *)(fdta + 0x1A);
}

/* The entries 11h and 12h find for the FCB as made, their names in order, each with its attribute and size (the
   order in a folder isn't DOS's to promise); what the DTA began with at the first. */
static void fcb_list(const char *what, int ext)
{
    static char names[12][24];
    char t[24];
    unsigned n = 0, i, j, al, head = 0;
    unsigned char *e = fdta + (ext ? 7 : 0);

    for (al = fcall(0x11, 0); al == 0 && n < 12; al = fcall(0x12, 0)) {
        if (!n)
            head = ext ? fdta[0] << 8 | fdta[6] : e[0];
        if (e[0] != fdrive + 1)
            printf("%s: an entry's drive %u\n", what, e[0]);
        sprintf(names[n++], "%.11s:%02X:%lu", (char *)e + 1, e[12], *(unsigned long *)(e + 1 + 28));
    }
    for (i = 1; i < n; i++)
        for (j = i; j && strcmp(names[j - 1], names[j]) > 0; j--) {
            strcpy(t, names[j]), strcpy(names[j], names[j - 1]), strcpy(names[j - 1], t);
        }
    if (ext)
        printf("%s n=%u head=%04X last=%02X", what, n, head, al);
    else
        printf("%s n=%u head=%s last=%02X", what, n, head == fdrive + 1 ? "drive" : "?", al);
    for (i = 0; i < n; i++)
        printf(" %s", names[i]);
    printf("\n");
}

static void fcbs(unsigned cur)
{
    unsigned char *f;
    unsigned al, a2, a3;

    _harderr(crit);
    ds_es();
    r.x.ax = 0x1A00;
    r.x.dx = (unsigned)fdta;
    dos();
    if (fdrive != cur) {
        r.x.ax = 0x0E00, r.x.dx = fdrive;
        dos();
        call_dx(0x3900, "\\DDIFF", 0);
        call_dx(0x3B00, "\\DDIFF", 0);
    }
    make("FCBT1.DAT", "0123456789", 10, 0);
    make("FCBH.DAT", "hidden", 6, 0x02);
    make("FCBR.DAT", "read only", 9, 0x01);

    /* open, and what it fills */
    f = fcb_make("FCBT1   DAT", -1);
    f[0x20] = 0x77, f[0x21] = 0x66;
    al = fcall(0x0F, 0);
    printf("F0Fh open al=%02X drive=%s block=%u recsize=%u size=%lu rec=%02X rr=%02X\n", al,
           f[0] == fdrive + 1 ? "its number" : f[0] ? "?" : "0", FW(f, 0x0C), FW(f, 0x0E), FL(f, 0x10), f[0x20], f[0x21]);
    /* sequential reads */
    f[0x20] = 0;
    memset(fdta, 0xEE, 256);
    al = fcall(0x14, 0);
    printf("F14h short record al=%02X rec=%u data=%.10s after=%02X last=%02X beyond=%02X\n", al, f[0x20], (char *)fdta,
           fdta[10], fdta[127], fdta[128]);
    al = fcall(0x14, 0);
    printf("F14h at the end al=%02X rec=%u\n", al, f[0x20]);
    FW(f, 0x0E) = 4, f[0x20] = 0;
    al = fcall(0x14, 0), a2 = fcall(0x14, 0), a3 = fcall(0x14, 0);
    printf("F14h records of 4 al=%02X %02X %02X rec=%u data=%02X%02X%02X%02X\n", al, a2, a3, f[0x20], fdta[0], fdta[1],
           fdta[2], fdta[3]);
    al = fcall(0x14, 0);
    printf("F14h past them al=%02X rec=%u\n", al, f[0x20]);
    FW(f, 0x0E) = 1, FW(f, 0x0C) = 0, f[0x20] = 127;
    al = fcall(0x14, 0);
    printf("F14h record 127 al=%02X block=%u rec=%u\n", al, FW(f, 0x0C), f[0x20]);
    /* random reads */
    FW(f, 0x0E) = 2, FL(f, 0x21) = 3;
    al = fcall(0x21, 0);
    printf("F21h record 3 al=%02X data=%c%c block=%u rec=%u rr=%lu\n", al, fdta[0], fdta[1], FW(f, 0x0C), f[0x20],
           FL(f, 0x21) & 0xFFFFFFUL);
    FL(f, 0x21) = 9;
    al = fcall(0x21, 0);
    printf("F21h past the end al=%02X block=%u rec=%u\n", al, FW(f, 0x0C), f[0x20]);
    FW(f, 0x0C) = 2, f[0x20] = 5;
    memset(f + 0x21, 0xFF, 4);
    al = fcall(0x24, 0);
    printf("F24h rr=%02X%02X%02X%02X\n", f[0x24], f[0x23], f[0x22], f[0x21]);
    FW(f, 0x0E) = 100;
    memset(f + 0x21, 0xFF, 4);
    fcall(0x24, 0);
    printf("F24h long records rr=%02X%02X%02X%02X\n", f[0x24], f[0x23], f[0x22], f[0x21]);
    FW(f, 0x0E) = 3, FL(f, 0x21) = 1;
    memset(fdta, 0xEE, 64);
    al = fcall(0x27, 5);
    printf("F27h 5 from 1 al=%02X cx=%u rr=%lu block=%u rec=%u pad=%02X%02X next=%02X\n", al, r.x.cx,
           FL(f, 0x21) & 0xFFFFFFUL, FW(f, 0x0C), f[0x20], fdta[7], fdta[8], fdta[9]);
    FL(f, 0x21) = 0;
    al = fcall(0x27, 2);
    printf("F27h 2 from 0 al=%02X cx=%u rr=%lu\n", al, r.x.cx, FL(f, 0x21) & 0xFFFFFFUL);
    FL(f, 0x21) = 1;
    al = fcall(0x27, 0);
    printf("F27h none al=%02X cx=%u rr=%lu\n", al, r.x.cx, FL(f, 0x21) & 0xFFFFFFUL);
    FL(f, 0x21) = 4;
    al = fcall(0x27, 1);
    printf("F27h at the end al=%02X cx=%u rr=%lu\n", al, r.x.cx, FL(f, 0x21) & 0xFFFFFFUL);
    FW(f, 0x0E) = 0, FL(f, 0x21) = 0, FW(f, 0x0C) = 0, f[0x20] = 0;
    al = fcall(0x14, 0);
    printf("F14h no record size al=%02X recsize=%u rec=%u\n", al, FW(f, 0x0E), f[0x20]);
    al = fcall(0x10, 0);
    printf("F10h close al=%02X\n", al);
    al = fcall(0x10, 0);
    printf("F10h twice al=%02X\n", al);
    f = fcb_make("NOFILE  DAT", -1);
    al = fcall(0x10, 0);
    a2 = fcall(0x14, 0);
    printf("F10h, 14h of no file al=%02X %02X\n", al, a2);
    f = fcb_make("FCBT1   DAT", -1);                 /* (an FCB never opened: its name is all it has) */
    FW(f, 0x0E) = 4;
    fdta[0] = '?';
    al = fcall(0x14, 0);
    printf("F14h never opened al=%02X data=%c rec=%u\n", al, fdta[0], f[0x20]);
    fcall(0x10, 0);
    f = fcb_make("FCBT1   DAT", -1);
    fcall(0x0F, 0);
    fcall(0x10, 0);
    FW(f, 0x0E) = 4, FW(f, 0x0C) = 0, f[0x20] = 0;
    fdta[0] = '?';
    al = fcall(0x14, 0);
    printf("F14h closed al=%02X data=%c\n", al, fdta[0]);
    /* a file's size in records */
    f = fcb_make("FCBT1   DAT", -1);
    FW(f, 0x0E) = 3;
    al = fcall(0x23, 0);
    printf("F23h records of 3 al=%02X rr=%lu\n", al, FL(f, 0x21) & 0xFFFFFFUL);
    f = fcb_make("FCBT1   DAT", -1);
    memset(f + 0x21, 0xFF, 4);
    al = fcall(0x23, 0);
    printf("F23h no record size al=%02X rr=%02X%02X%02X%02X recsize=%u\n", al, f[0x24], f[0x23], f[0x22], f[0x21],
           FW(f, 0x0E));
    f = fcb_make("NOFILE  DAT", -1);
    printf("F23h no file al=%02X\n", fcall(0x23, 0));
    /* what an open refuses */
    printf("F0Fh no file al=%02X\n", fcall(0x0F, 0));
    err59("F59h after it");
    f = fcb_make("FCBT?   DAT", -1);
    al = fcall(0x0F, 0);
    printf("F0Fh pattern al=%02X name=%.11s\n", al, (char *)f + 1);
    if (!al)
        fcall(0x10, 0);
    f = fcb_make("FCBT1   DAT", -1);
    f[0] = 17;
    printf("F0Fh drive Q: al=%02X\n", fcall(0x0F, 0));
    f = fcb_make("FCBH    DAT", -1);
    al = fcall(0x0F, 0);
    printf("F0Fh hidden, plain al=%02X\n", al);
    if (!al)
        fcall(0x10, 0);
    f = fcb_make("FCBH    DAT", 0x02);
    al = fcall(0x0F, 0);
    printf("F0Fh hidden, extended al=%02X size=%lu\n", al, FL(f, 0x10));
    printf("F10h extended al=%02X\n", fcall(0x10, 0));
    /* create, writes, the size set */
    f = fcb_make("FCBN    DAT", -1);
    al = fcall(0x16, 0);
    printf("F16h create al=%02X drive=%s recsize=%u size=%lu\n", al, f[0] == fdrive + 1 ? "its number" : f[0] ? "?" : "0",
           FW(f, 0x0E), FL(f, 0x10));
    memset(fdta, 'A', 128);
    al = fcall(0x15, 0);
    printf("F15h write al=%02X size=%lu rec=%u\n", al, FL(f, 0x10), f[0x20]);
    memset(fdta, 'B', 128);
    FL(f, 0x21) = 2;
    al = fcall(0x22, 0);
    printf("F22h record 2 al=%02X size=%lu rec=%u\n", al, FL(f, 0x10), f[0x20]);
    memset(fdta, 'C', 256);
    FL(f, 0x21) = 3;
    al = fcall(0x28, 2);
    printf("F28h 2 from 3 al=%02X cx=%u size=%lu rr=%lu\n", al, r.x.cx, FL(f, 0x10), FL(f, 0x21) & 0xFFFFFFUL);
    FL(f, 0x21) = 4;
    al = fcall(0x21, 0);
    printf("F21h what was written al=%02X data=%c%c\n", al, fdta[0], fdta[127]);
    FL(f, 0x21) = 1;
    al = fcall(0x28, 0);
    printf("F28h none, at 1 al=%02X cx=%u size=%lu rr=%lu\n", al, r.x.cx, FL(f, 0x10), FL(f, 0x21) & 0xFFFFFFUL);
    FL(f, 0x21) = 4;
    al = fcall(0x28, 0);
    printf("F28h none, at 4 al=%02X size=%lu\n", al, FL(f, 0x10));
    printf("F10h written al=%02X\n", fcall(0x10, 0));
    printf("F its size on the disk %lu\n", size_of("FCBN.DAT"));
    f = fcb_make("FCBN    DAT", -1);
    al = fcall(0x16, 0);
    printf("F16h over it al=%02X size=%lu\n", al, FL(f, 0x10));
    fcall(0x10, 0);
    printf("F its size on the disk %lu\n", size_of("FCBN.DAT"));
    /* a read-only file */
    f = fcb_make("FCBR    DAT", -1);
    al = fcall(0x0F, 0);
    FW(f, 0x0E) = 9;
    a2 = fcall(0x14, 0);
    f[0x20] = 0;
    a3 = fcall(0x15, 0);
    printf("F0Fh read-only al=%02X read=%02X write=%02X\n", al, a2, a3);
    printf("F10h read-only al=%02X\n", fcall(0x10, 0));
    f = fcb_make("FCBR    DAT", -1);
    printf("F16h read-only al=%02X\n", fcall(0x16, 0));
    /* searches */
    f = fcb_make("????????DAT", -1);
    fcb_list("F11h *.DAT", 0);
    printf("F12h after the end al=%02X\n", fcall(0x12, 0));
    f = fcb_make("????????DAT", 0x02);
    fcb_list("F11h *.DAT hidden too", 1);
    f = fcb_make("???????????", 0x10);
    fcb_list("F11h all, folders too", 1);
    f = fcb_make("FCBT1   DAT", -1);
    printf("F12h with no 11h al=%02X\n", fcall(0x12, 0));
    /* (the label: found from a folder as from the root, if the volume has one) */
    call_dx(0x4E00, "\\*.*", 0x08);
    a2 = r.x.cflag ? 0xFF : 0;
    f = fcb_make("???????????", 0x08);
    al = fcall(0x11, 0);
    printf("F11h the label %s\n", al == a2 && (al || fdta[7 + 12] == 0x08) ? "as a search of the root finds it" : "not as the root's");
    /* rename and delete by pattern */
    f = fcb_make("FCBT1   DAT", -1);
    memcpy(f + 0x11, "FCBX?   BAK", 11);
    al = fcall(0x17, 0);
    printf("F17h FCBT1.DAT to FCBX?.BAK al=%02X new=%lu old=%lu\n", al, size_of("FCBX1.BAK"), size_of("FCBT1.DAT"));
    f = fcb_make("FCBX1   BAK", -1);
    memcpy(f + 0x11, "FCBR    DAT", 11);
    printf("F17h to a name there al=%02X\n", fcall(0x17, 0));
    f = fcb_make("NOFILE  DAT", -1);
    memcpy(f + 0x11, "OTHER   DAT", 11);
    printf("F17h no file al=%02X\n", fcall(0x17, 0));
    f = fcb_make("FCB?????DAT", -1);
    al = fcall(0x13, 0);
    printf("F13h FCB*.DAT al=%02X new=%lu read-only=%lu hidden=%lu\n", al, size_of("FCBN.DAT"), size_of("FCBR.DAT"),
           size_of("FCBH.DAT"));
    printf("F13h again al=%02X\n", fcall(0x13, 0));
    f = fcb_make("FCB?????DAT", 0x02);
    al = fcall(0x13, 0);
    printf("F13h hidden too al=%02X hidden=%lu\n", al, size_of("FCBH.DAT"));
    f = fcb_make("NOFILE  DAT", -1);
    printf("F13h no file al=%02X\n", fcall(0x13, 0));
    call_dx(0x4301, "FCBR.DAT", 0);
    call_dx(0x4100, "FCBR.DAT", 0);
    call_dx(0x4301, "FCBH.DAT", 0);
    call_dx(0x4100, "FCBH.DAT", 0);
    call_dx(0x4100, "FCBN.DAT", 0);
    call_dx(0x4100, "FCBX1.BAK", 0);
    call_dx(0x4100, "FCBT1.DAT", 0);
    if (fdrive != cur) {
        call_dx(0x3B00, "\\", 0);
        call_dx(0x3A00, "\\DDIFF", 0);
        r.x.ax = 0x0E00, r.x.dx = cur;
        dos();
    }
}

/* ---- the small calls: 37h, 1Bh/1Ch, 1Fh/32h, 5Ah, 67h, 5Ch, 5Eh, 5Fh, 65h */
static void small(void)
{
    unsigned h, n, seg0, fat32;
    unsigned __far *psp = MK_FP(_psp, 0);

    r.x.ax = 0x3700, r.x.dx = 0x1234;
    dos();
    printf("S3700h al=%02X dl=%02X\n", r.h.al, r.h.dl);
    r.x.ax = 0x3701, r.x.dx = '-';
    dos();
    n = r.h.al;
    r.x.ax = 0x3700, r.x.dx = 0;
    dos();
    printf("S3701h al=%02X then dl=%02X\n", n, r.h.dl);
    r.x.ax = 0x3702, r.x.dx = 0x1234;
    dos();
    printf("S3702h al=%02X dl=%02X\n", r.h.al, r.h.dl);
    r.x.ax = 0x3703, r.x.dx = 0;
    dos();
    printf("S3703h al=%02X\n", r.h.al);
    r.x.ax = 0x3704, r.x.dx = 0;
    dos();
    printf("S3704h al=%02X\n", r.h.al);
    ds_es();
    r.x.ax = 0x1C00, r.x.dx = 3;
    dos();
    printf("S1Ch C: bps=%u media=%02X\n", r.x.cx, r.h.al == 0xFF ? 0 : *(unsigned char __far *)MK_FP(s.ds, r.x.bx));
    ds_es();
    r.x.ax = 0x1C00, r.x.dx = 17;
    dos();
    printf("S1Ch Q: al=%02X\n", r.h.al);
    ds_es();
    r.x.ax = 0x1B00;
    dos();
    printf("S1Bh bps=%u media=%02X\n", r.x.cx, r.h.al == 0xFF ? 0 : *(unsigned char __far *)MK_FP(s.ds, r.x.bx));
    /* (MS-DOS 7.1 gives no parameter block of a FAT32 drive by 32h and 1Fh: 7302h's is that) */
    ds_es();
    memset(buf, 0, sizeof buf);
    r.x.ax = 0x7302, r.x.dx = 3, r.x.cx = 0x3F, r.x.di = (unsigned)buf;
    dos();
    fat32 = !r.x.cflag && *(unsigned *)(buf + 2 + 0x09) == 0;
    ds_es();
    r.x.ax = 0x3200, r.x.dx = 3;
    dos();
    if (r.h.al == 0) {
        unsigned char __far *d = MK_FP(s.ds, r.x.bx);

        printf("S32h C: %s\n", !fat32 && d[0] == 2 && *(unsigned __far *)(d + 2) == 512 && d[8] == 2 && d[0x17] == 0xF8
               ? "as its FAT asks" : "a block that is wrong");
    } else {
        printf("S32h C: %s\n", fat32 && r.h.al == 0xFF ? "as its FAT asks" : "refused");
    }
    ds_es();
    r.x.ax = 0x1F00;
    dos();
    printf("S1Fh %s\n", r.h.al == (fat32 ? 0xFF : 0) ? "as its FAT asks" : "not so");
    ds_es();
    r.x.ax = 0x3200, r.x.dx = 17;
    dos();
    printf("S32h Q: al=%02X\n", r.h.al);
    ds_es();
    r.x.ax = 0x3305;
    dos();
    printf("S3305h dl=%02X\n", r.h.dl);
    /* 5Ah */
    strcpy(buf, "C:\\DDIFF\\");
    call_dx(0x5A00, buf, 0);
    h = r.x.ax;
    printf("S5Ah cf=%u len=%u\n", r.x.cflag ? 1 : 0, (unsigned)strlen(buf));
    if (!r.x.cflag) {
        close_(h);
        call_dx(0x4100, buf, 0);
        line("S5Ah's file deleted");
    }
    strcpy(buf, "C:\\DDIFF");
    call_dx(0x5A00, buf, 0);
    h = r.x.ax;
    printf("S5Ah no backslash cf=%u len=%u at8=%c\n", r.x.cflag ? 1 : 0, (unsigned)strlen(buf), buf[8]);
    if (!r.x.cflag) {
        close_(h);
        call_dx(0x4100, buf, 0);
    }
    strcpy(buf, "C:\\NOSUCH\\");
    call_dx(0x5A00, buf, 0);
    line("S5Ah no folder");
    /* 67h */
    seg0 = psp[0x36 / 2];
    r.x.ax = 0x6700, r.x.bx = 10;
    dos();
    printf("S67h 10 cf=%u count=%u moved=%u\n", r.x.cflag ? 1 : 0, psp[0x32 / 2], psp[0x36 / 2] != seg0);
    r.x.ax = 0x6700, r.x.bx = 40;
    dos();
    printf("S67h 40 cf=%u count=%u moved=%u\n", r.x.cflag ? 1 : 0, psp[0x32 / 2], psp[0x36 / 2] != seg0);
    r.x.ax = 0x6700, r.x.bx = 20;
    dos();
    printf("S67h 20 cf=%u count=%u moved=%u off=%04X\n", r.x.cflag ? 1 : 0, psp[0x32 / 2], psp[0x36 / 2] != seg0,
           psp[0x34 / 2]);
    /* 55h and 26h: a PSP made by the program (55h makes it the current one: put back) */
    ds_es();
    r.x.ax = 0x4800, r.x.bx = 0x20;
    dos();
    if (!r.x.cflag) {
        unsigned seg = r.x.ax, cur;
        unsigned __far *np = MK_FP(seg, 0);
        unsigned char __far *nb = MK_FP(seg, 0), __far *pb = MK_FP(_psp, 0);

        r.x.ax = 0x5500, r.x.dx = seg, r.x.si = 0x9ABC;
        dos();
        r.x.ax = 0x5100;
        dos();
        cur = r.x.bx;
        r.x.ax = 0x5000, r.x.bx = _psp;
        dos();
        printf("S55h current=%s top=%04X parent=%s count=%u jft=%s:%04X h1=%s h19=%02X env=%s int20=%04X tail=%s\n",
               cur == seg ? "new" : cur == _psp ? "old" : "?", np[1], np[0x16 / 2] == _psp ? "old" : "?",
               np[0x32 / 2], np[0x36 / 2] == seg ? "new" : np[0x36 / 2] == _psp ? "old" : "?", np[0x34 / 2],
               nb[0x19] == pb[0x19] ? "same" : "other", nb[0x18 + 19], np[0x2C / 2] == psp[0x2C / 2] ? "same" : "other",
               np[0], nb[0x80] == pb[0x80] ? "same" : "other");
        _fmemset(nb, 0, 0x100);
        r.x.ax = 0x2600, r.x.dx = seg;
        dos();
        r.x.ax = 0x5100;
        dos();
        {
            /* (what it copies is its caller's code segment's first 256 bytes: a .COM's PSP, not this .EXE's) */
            unsigned __far *cw;
            unsigned char __far *cb;

            segread(&s);
            cw = MK_FP(s.cs, 0), cb = MK_FP(s.cs, 0);
            printf("S26h current=%s top=%s parent=%s count=%u jft=%s:%04X int20=%04X b40=%s b80=%s\n",
                   r.x.bx == _psp ? "old" : "?",
                   np[1] == psp[1] ? "the PSP's" : np[1] == cw[1] ? "the code's" : "another",
                   np[0x16 / 2] == _psp ? "the PSP" : np[0x16 / 2] == psp[0x16 / 2] ? "the PSP's" : np[0x16 / 2] == cw[0x16 / 2]
                   ? "the code's" : "another", np[0x32 / 2],
                   np[0x36 / 2] == seg ? "new" : np[0x36 / 2] == _psp ? "old" : "?", np[0x34 / 2], np[0],
                   nb[0x40] == cb[0x40] && nb[0x41] == cb[0x41] ? "the code's" : nb[0x40] == pb[0x40] ? "the PSP's" : "another",
                   nb[0x80] == cb[0x80] && nb[0x81] == cb[0x81] ? "the code's" : nb[0x80] == pb[0x80] ? "the PSP's" : "another");
        }
        r.x.ax = 0x4900;
        s.es = seg;
        intdosx(&r, &r, &s);
        ds_es();
    }
    /* 5Ch: SHARE's, which MS-DOS 7.1 hasn't got until SHARE is loaded */
    h = handle("LOCK.TMP", 0x3C00);
    r.x.ax = 0x5C00, r.x.bx = h, r.x.cx = 0, r.x.dx = 0, r.x.si = 0, r.x.di = 4;
    dos();
    line("S5C00h");
    r.x.ax = 0x5C01, r.x.bx = h, r.x.cx = 0, r.x.dx = 0, r.x.si = 0, r.x.di = 4;
    dos();
    line("S5C01h");
    close_(h);
    call_dx(0x4100, "LOCK.TMP", 0);
    /* the network's, the country's */
    ds_es();
    memset(buf, 0xEE, 32);
    r.x.ax = 0x5E00, r.x.cx = 0x1234, r.x.dx = (unsigned)buf;
    dos();
    printf("S5E00h cf=%u ch=%02X buf=%02X%02X%02X\n", r.x.cflag ? 1 : 0, r.h.ch, (unsigned char)buf[0],
           (unsigned char)buf[14], (unsigned char)buf[15]);
    ds_es();
    r.x.ax = 0x5F02, r.x.bx = 0, r.x.si = (unsigned)buf, r.x.di = (unsigned)buf + 64;
    dos();
    line("S5F02h");
    ds_es();
    memset(buf, 0, sizeof buf);
    r.x.ax = 0x6501, r.x.bx = 0xFFFF, r.x.dx = 0xFFFF, r.x.cx = 41, r.x.di = (unsigned)buf;
    dos();
    printf("S6501h cf=%u cx=%u id=%02X size=%u country=%u\n", r.x.cflag ? 1 : 0, r.x.cflag ? 0 : r.x.cx,
           (unsigned char)buf[0], *(unsigned *)(buf + 1), *(unsigned *)(buf + 3));
    ds_es();
    memset(buf, 0, sizeof buf);
    r.x.ax = 0x6502, r.x.bx = 0xFFFF, r.x.dx = 0xFFFF, r.x.cx = 5, r.x.di = (unsigned)buf;
    dos();
    printf("S6502h cf=%u cx=%u id=%02X\n", r.x.cflag ? 1 : 0, r.x.cflag ? 0 : r.x.cx, (unsigned char)buf[0]);
    if (!r.x.cflag) {
        unsigned char __far *t = *(unsigned char __far * __far *)(buf + 1);

        printf("S6502h's table size=%u e9=%02X e1=%02X\n", *(unsigned __far *)t, t[2 + 0x82 - 0x80], t[2 + 0xE1 - 0x80]);
    }
    ds_es();
    memset(buf, 0, sizeof buf);
    r.x.ax = 0x6504, r.x.bx = 0xFFFF, r.x.dx = 0xFFFF, r.x.cx = 5, r.x.di = (unsigned)buf;
    dos();
    printf("S6504h cf=%u cx=%u id=%02X\n", r.x.cflag ? 1 : 0, r.x.cflag ? 0 : r.x.cx, (unsigned char)buf[0]);
    if (!r.x.cflag) {
        unsigned char __far *t = *(unsigned char __far * __far *)(buf + 1);

        /* (e acute: E from COUNTRY.SYS's table, which GLOS has; E acute from the one MS-DOS has without it) */
        printf("S6504h's table size=%u e1=%02X\n", *(unsigned __far *)t, t[2 + 0xE1 - 0x80]);
    }
    ds_es();
    memset(buf, 0, sizeof buf);
    r.x.ax = 0x6505, r.x.bx = 0xFFFF, r.x.dx = 0xFFFF, r.x.cx = 5, r.x.di = (unsigned)buf;
    dos();
    printf("S6505h cf=%u id=%02X", r.x.cflag ? 1 : 0, (unsigned char)buf[0]);
    if (!r.x.cflag) {
        unsigned char __far *t = *(unsigned char __far * __far *)(buf + 1);

        for (n = 0; n < 2 + *(unsigned __far *)t && n < 40; n++)
            printf(" %02X", t[n]);
    }
    printf("\n");
    ds_es();
    memset(buf, 0, sizeof buf);
    r.x.ax = 0x6506, r.x.bx = 0xFFFF, r.x.dx = 0xFFFF, r.x.cx = 5, r.x.di = (unsigned)buf;
    dos();
    printf("S6506h cf=%u id=%02X\n", r.x.cflag ? 1 : 0, (unsigned char)buf[0]);
    ds_es();
    memset(buf, 0, sizeof buf);
    r.x.ax = 0x6507, r.x.bx = 0xFFFF, r.x.dx = 0xFFFF, r.x.cx = 5, r.x.di = (unsigned)buf;
    dos();
    printf("S6507h cf=%u id=%02X\n", r.x.cflag ? 1 : 0, (unsigned char)buf[0]);
    ds_es();
    r.x.ax = 0x6501, r.x.bx = 0xFFFF, r.x.dx = 0xFFFF, r.x.cx = 4, r.x.di = (unsigned)buf;
    dos();
    line("S6501h cx 4");
    ds_es();
    memset(buf, 0xEE, 16);
    r.x.ax = 0x6501, r.x.bx = 0xFFFF, r.x.dx = 0xFFFF, r.x.cx = 7, r.x.di = (unsigned)buf;
    dos();
    printf("S6501h cx 7 cf=%u cx=%u size=%u b7=%02X\n", r.x.cflag ? 1 : 0, r.x.cflag ? 0 : r.x.cx, *(unsigned *)(buf + 1),
           (unsigned char)buf[7]);
    ds_es();
    r.x.ax = 0x6501, r.x.bx = 999, r.x.dx = 0xFFFF, r.x.cx = 41, r.x.di = (unsigned)buf;
    dos();
    line("S6501h code page 999");
    ds_es();
    r.x.ax = 0x6501, r.x.bx = 0xFFFF, r.x.dx = 999, r.x.cx = 41, r.x.di = (unsigned)buf;
    dos();
    line("S6501h country 999");
    ds_es();
    r.x.ax = 0x6509, r.x.bx = 0xFFFF, r.x.dx = 0xFFFF, r.x.cx = 41, r.x.di = (unsigned)buf;
    dos();
    line("S6509h");
    r.x.ax = 0x6520, r.x.dx = 'a';
    dos();
    printf("S6520h cf=%u dl=%c\n", r.x.cflag ? 1 : 0, r.h.dl);
    ds_es();
    strcpy(buf, "abc\x82z");
    r.x.ax = 0x6521, r.x.cx = 4, r.x.dx = (unsigned)buf;
    dos();
    printf("S6521h cf=%u %c%c%c %02X %c\n", r.x.cflag ? 1 : 0, buf[0], buf[1], buf[2], (unsigned char)buf[3], buf[4]);
    ds_es();
    strcpy(buf, "abc\x82z");
    r.x.ax = 0x6522, r.x.dx = (unsigned)buf;
    dos();
    printf("S6522h cf=%u %c%c%c %02X %c\n", r.x.cflag ? 1 : 0, buf[0], buf[1], buf[2], (unsigned char)buf[3], buf[4]);
    r.x.ax = 0x6523, r.x.dx = 'y';
    dos();
    printf("S6523h y cf=%u ax=%04X\n", r.x.cflag ? 1 : 0, r.x.ax);
    r.x.ax = 0x6523, r.x.dx = 'N';
    dos();
    printf("S6523h N cf=%u ax=%04X\n", r.x.cflag ? 1 : 0, r.x.ax);
    r.x.ax = 0x6523, r.x.dx = 'x';
    dos();
    printf("S6523h x cf=%u ax=%04X\n", r.x.cflag ? 1 : 0, r.x.ax);
}

/* Handle h of the program's made name's (opened with ax), what it was kept in *keep (46h; N7's survey, 2026-11-27:
   GLOS's own DOS read the keyboard and wrote the screen whatever handles 0 and 1 were). */
static void redirect(unsigned h, const char *name, unsigned ax, unsigned *keep)
{
    unsigned f;

    fflush(stdout);
    r.x.ax = 0x4500, r.x.bx = h;
    dos();
    *keep = r.x.ax;
    f = handle(name, ax);
    r.x.ax = 0x4600, r.x.bx = f, r.x.cx = h;
    dos();
    close_(f);
}

static void unredirect(unsigned h, unsigned keep)
{
    r.x.ax = 0x4600, r.x.bx = keep, r.x.cx = h;
    dos();
    close_(keep);
}

static unsigned char ch(unsigned ax)
{
    r.x.ax = ax;
    dos();
    return r.h.al;
}

/* A file's first bytes as one line, CR, LF and BEL written \r, \n, \a (the echo of the reads below, kept in a file
   of its own: where a program's console output goes over a network shell isn't DOS's to say). */
static void show(const char *label, const char *name)
{
    unsigned h = handle(name, 0x3D00), n, i;

    ds_es();
    r.x.ax = 0x3F00, r.x.bx = h, r.x.cx = 48, r.x.dx = (unsigned)buf;
    dos();
    n = r.x.cflag ? 0 : r.x.ax;
    close_(h);
    printf("%s \"", label);
    for (i = 0; i < n; i++)
        printf(buf[i] == '\r' ? "\\r" : buf[i] == '\n' ? "\\n" : buf[i] == 7 ? "\\a" : "%c", buf[i]);
    printf("\"\n");
}

/* The console functions through a redirected standard input and output (DOS 2+: RBIL's 01h-0Ch "may be
   redirected"): handle 0 a file's, read by 0Bh, 08h, 01h, 07h, 06h and 0Ah; handle 1 a file's, written by 02h,
   09h and 06h and read back. What 01h and 0Ah echo goes to standard output, the probe's own lines. */
static void redirection(void)
{
    static const char in[] = "ab\r\nxyz\r\n";
    static const char lines[] = "ab\r\ncd\r\nlonger\r\n";
    unsigned char lb[16];
    unsigned char got[3][6];
    unsigned keep, keep1, h, n, s0, s1, s2, c1, c2, c3, c4, c5, a2, a9;

    make("RIN.TXT", in, sizeof in - 1, 0);
    make("RECHO.TXT", "", 0, 0);
    redirect(1, "RECHO.TXT", 0x3D02, &keep1);
    redirect(0, "RIN.TXT", 0x3D00, &keep);
    s0 = ch(0x0B00);
    c1 = ch(0x0800);
    c2 = ch(0x0100);
    c3 = ch(0x0700);
    r.x.ax = 0x0600, r.h.dl = 0xFF;
    dos();
    c4 = r.h.al;                                /* (its ZF: intdosx gives CF alone) */
    memset(lb, 0, sizeof lb);
    lb[0] = 10;
    ds_es();
    r.x.ax = 0x0A00, r.x.dx = (unsigned)lb;
    dos();
    s1 = ch(0x0B00);
    c5 = ch(0x0800);
    s2 = ch(0x0B00);
    unredirect(0, keep);
    unredirect(1, keep1);
    show("redir in echo", "RECHO.TXT");
    printf("redir in 0Bh=%02X 08h=%02X 01h=%02X 07h=%02X 06h=%02X\n", s0, c1, c2, c3, c4);
    printf("redir in 0Ah n=%u \"%.*s\" then=%02X 0Bh=%02X 08h=%02X 0Bh=%02X\n", lb[1], lb[1] < 10 ? lb[1] : 10,
           lb + 2, lb[2 + (lb[1] < 10 ? lb[1] : 10)], s1, c5, s2);
    /* 0Ah line after line (the LF the last left first in the next) and past its buffer's room */
    make("RIN.TXT", lines, sizeof lines - 1, 0);
    make("RECHO.TXT", "", 0, 0);
    redirect(1, "RECHO.TXT", 0x3D02, &keep1);
    redirect(0, "RIN.TXT", 0x3D00, &keep);
    for (h = 0; h < 3; h++) {
        memset(lb, 0, sizeof lb);
        lb[0] = h == 2 ? 3 : 10;
        ds_es();
        r.x.ax = 0x0A00, r.x.dx = (unsigned)lb;
        dos();
        got[h][0] = lb[1], memcpy(got[h] + 1, lb + 2, 5);
    }
    s1 = ch(0x0B00);
    c5 = s1 ? ch(0x0800) : 0;
    unredirect(0, keep);
    unredirect(1, keep1);
    show("redir 0Ah echo", "RECHO.TXT");
    for (h = 0; h < 3; h++)
        printf("redir 0Ah%u max=%u n=%u %02X %02X %02X %02X %02X\n", h, h == 2 ? 3 : 10, got[h][0], got[h][1],
               got[h][2], got[h][3], got[h][4], got[h][5]);
    printf("redir 0Ah after 0Bh=%02X 08h=%02X\n", s1, c5);
    make("ROUT.TXT", "", 0, 0);
    redirect(1, "ROUT.TXT", 0x3D02, &keep);
    r.x.ax = 0x0200, r.h.dl = 'Q';
    dos();
    a2 = r.h.al;
    ds_es();
    r.x.ax = 0x0900, r.x.dx = (unsigned)"R$";
    dos();
    a9 = r.h.al;
    r.x.ax = 0x0600, r.h.dl = 'S';
    dos();
    unredirect(1, keep);
    h = handle("ROUT.TXT", 0x3D00);
    ds_es();
    r.x.ax = 0x3F00, r.x.bx = h, r.x.cx = 16, r.x.dx = (unsigned)buf;
    dos();
    n = r.x.cflag ? 0 : r.x.ax;
    close_(h);
    printf("redir out 02h al=%02X 09h al=%02X file=%u \"%.*s\"\n", a2, a9, n, n < 16 ? n : 16, buf);
    call_dx(0x4100, "RIN.TXT", 0);
    call_dx(0x4100, "ROUT.TXT", 0);
    call_dx(0x4100, "RECHO.TXT", 0);
}

int main(int argc, char **argv)
{
    unsigned h, h2, seg, cur;
    unsigned char dta[64];

    printf("DOSDIFF 4\n");
    /* the version */
    r.x.ax = 0x3000;
    r.x.bx = 0x1234, r.x.cx = 0x5678;
    dos();
    printf("30h00 ax=%04X bh=%02X\n", r.x.ax, r.h.bh);
    r.x.ax = 0x3001;
    dos();
    printf("30h01 ax=%04X bh=%02X\n", r.x.ax, r.h.bh);
    r.x.ax = 0x3306;
    dos();
    printf("3306h bx=%04X dx=%04X\n", r.x.bx, r.x.dx);
    r.x.ax = 0x0E00;
    r.h.dl = 2;
    dos();
    printf("0Eh al=%02X\n", r.h.al);
    /* a folder of its own */
    call_dx(0x3900, "C:\\DDIFF", 0);
    call_dx(0x3B00, "C:\\DDIFF", 0);
    line("3Bh DDIFF");
    ds_es();
    r.x.ax = 0x4700;
    r.h.dl = 0;
    r.x.si = (unsigned)buf;
    dos();
    printf("47h cf=%u ax=%04X \"%s\"\n", r.x.cflag ? 1 : 0, r.x.ax, buf);
    /* files that aren't there */
    call_dx(0x3D00, "NOFILE.TXT", 0);
    line("3Dh no file");
    err59("59h after it");
    call_dx(0x3D00, "\\NODIR\\X.TXT", 0);
    line("3Dh no folder");
    err59("59h after it");
    call_dx(0x3D03, "NOFILE.TXT", 0);
    line("3Dh mode 3");
    err59("59h after it");
    call_dx(0x4100, "NOFILE.TXT", 0);
    line("41h no file");
    call_dx(0x4300, "\\NODIR\\X.TXT", 0);
    line("43h no folder");
    /* searches */
    ds_es();
    r.x.ax = 0x1A00;
    r.x.dx = (unsigned)dta;
    dos();
    call_dx(0x4E00, "*.ZZZ", 0);
    line("4Eh none match");
    err59("59h after it");
    call_dx(0x4E00, "\\NODIR\\*.*", 0);
    line("4Eh no folder");
    call_dx(0x4E00, "*.*", 0x10);
    printf("4Eh *.* dirs cf=%u name=%s attr=%02X\n", r.x.cflag ? 1 : 0, dta + 0x1E, dta[0x15]);
    r.x.ax = 0x4F00;
    dos();
    printf("4Fh cf=%u name=%s\n", r.x.cflag ? 1 : 0, r.x.cflag ? "" : (char *)dta + 0x1E);
    r.x.ax = 0x4F00;
    dos();
    line("4Fh at the end");
    /* files */
    h = handle("A.TXT", 0x3C00);
    printf("3Ch handle=%u\n", h);
    ds_es();
    r.x.ax = 0x4000, r.x.bx = h, r.x.cx = 5, r.x.dx = (unsigned)"hello";
    dos();
    r.x.ax = 0x4201, r.x.bx = h, r.x.cx = 0xFFFF, r.x.dx = 0xFFF0;
    dos();
    printf("42h before 0 cf=%u ax=%04X dx=%04X\n", r.x.cflag ? 1 : 0, r.x.ax, r.x.dx);
    r.x.ax = 0x4400, r.x.bx = h;
    dos();
    printf("44h00 file dx=%04X\n", r.x.dx & 0xFFBF);
    h2 = handle("CON", 0x3D02);
    r.x.ax = 0x4400, r.x.bx = h2;
    dos();
    printf("44h00 CON dx=%04X\n", r.x.dx);
    close_(h2);
    h2 = handle("NUL", 0x3D02);
    r.x.ax = 0x4400, r.x.bx = h2;
    dos();
    printf("44h00 NUL dx=%04X\n", r.x.dx);
    close_(h2);
    r.x.ax = 0x4406, r.x.bx = h;
    dos();
    printf("44h06 file at end al=%02X\n", r.h.al);
    r.x.ax = 0x4408, r.x.bx = 3;
    dos();
    line("4408h C:");
    r.x.ax = 0x4409, r.x.bx = 3;
    dos();
    printf("4409h C: cf=%u dx=%04X\n", r.x.cflag ? 1 : 0, r.x.dx & 0x9200);
    ds_es();
    r.x.ax = 0x5600, r.x.dx = (unsigned)"A.TXT", r.x.di = (unsigned)"B.TXT";
    dos();
    line("56h of an open file");
    close_(h);
    close_(h);
    line("3Eh twice");
    err59("59h after it");
    call_dx(0x5B00, "A.TXT", 0);
    line("5Bh there");
    if (!r.x.cflag)
        close_(r.x.ax);
    call_dx(0x5B00, "B.TXT", 0);
    if (!r.x.cflag)
        close_(r.x.ax);
    call_dx(0x4301, "A.TXT", 0x01);
    call_dx(0x4100, "A.TXT", 0);
    line("41h read-only");
    err59("59h after it");
    call_dx(0x3C00, "A.TXT", 0);
    line("3Ch read-only");
    call_dx(0x3D01, "A.TXT", 0);
    line("3Dh write read-only");
    call_dx(0x4301, "A.TXT", 0x10);
    line("4301h dir bit");
    call_dx(0x4301, "A.TXT", 0x00);
    call_dx(0x4100, "A.TXT", 0);
    call_dx(0x4100, "B.TXT", 0);
    /* 6Ch */
    ds_es();
    r.x.ax = 0x6C00, r.x.bx = 0x0002, r.x.cx = 0, r.x.dx = 0x0010, r.x.si = (unsigned)"E.TXT";
    dos();
    printf("6Ch make cf=%u cx=%04X\n", r.x.cflag ? 1 : 0, r.x.cx);
    if (!r.x.cflag)
        close_(r.x.ax);
    r.x.ax = 0x6C00, r.x.bx = 0x0002, r.x.cx = 0, r.x.dx = 0x0010, r.x.si = (unsigned)"E.TXT";
    dos();
    line("6Ch there, fail");
    err59("59h after it");
    r.x.ax = 0x6C00, r.x.bx = 0x0002, r.x.cx = 0, r.x.dx = 0x0012, r.x.si = (unsigned)"E.TXT";
    dos();
    printf("6Ch replace cf=%u cx=%04X\n", r.x.cflag ? 1 : 0, r.x.cx);
    if (!r.x.cflag)
        close_(r.x.ax);
    r.x.ax = 0x6C00, r.x.bx = 0x0002, r.x.cx = 0, r.x.dx = 0x0001, r.x.si = (unsigned)"NONE.TXT";
    dos();
    line("6Ch none, fail");
    r.x.ax = 0x6C00, r.x.bx = 0x0002, r.x.cx = 0, r.x.dx = 0x0033, r.x.si = (unsigned)"E.TXT";
    dos();
    line("6Ch bad action");
    err59("59h after it");
    /* an open file: deleted? written before its start? */
    h = handle("E.TXT", 0x3D02);
    r.x.ax = 0x4200, r.x.bx = h, r.x.cx = 0xFFFF, r.x.dx = 0xFFFE;
    dos();
    printf("42h to -2 cf=%u ax=%04X dx=%04X\n", r.x.cflag ? 1 : 0, r.x.ax, r.x.dx);
    ds_es();
    r.x.ax = 0x4000, r.x.bx = h, r.x.cx = 1, r.x.dx = (unsigned)"x";
    dos();
    printf("40h at -2 cf=%u ax=%04X\n", r.x.cflag ? 1 : 0, r.x.ax);
    r.x.ax = 0x3F00, r.x.bx = h, r.x.cx = 1, r.x.dx = (unsigned)buf;
    dos();
    printf("3Fh at -2 cf=%u ax=%04X\n", r.x.cflag ? 1 : 0, r.x.ax);
    call_dx(0x4100, "E.TXT", 0);
    line("41h of an open file");
    close_(h);
    call_dx(0x4100, "E.TXT", 0);
    /* folders */
    call_dx(0x3900, "SUB", 0);
    call_dx(0x3900, "SUB", 0);
    line("39h there");
    h = handle("SUB\\F.TXT", 0x3C00);
    close_(h);
    call_dx(0x3A00, "SUB", 0);
    line("3Ah not empty");
    call_dx(0x3A00, "\\DDIFF", 0);
    line("3Ah current");
    err59("59h after it");
    call_dx(0x3A00, "\\", 0);
    line("3Ah root");
    call_dx(0x3B00, "SUB\\F.TXT", 0);
    line("3Bh to a file");
    call_dx(0x3B00, "NODIR", 0);
    line("3Bh no folder");
    call_dx(0x4100, "SUB\\F.TXT", 0);
    call_dx(0x3A00, "SUB", 0);
    line("3Ah");
    /* names */
    truename("c:sub\\..\\x.txt");
    truename("x.txt");
    truename("NUL");
    truename("C:\\DDIFF\\CON");
    truename("lower.txt");
    truename("A.B.C");
    truename("LONGNAMEXYZ.TEXT");
    truename("..\\..\\..\\X");
    parse("  c:foo.bar rest", 0);
    parse("  c:foo.bar rest", 1);
    parse(",*.c", 1);
    parse(";;x", 1);
    parse("file name", 0);
    parse("q:x", 0);
    parse("longername.text", 0);
    parse("a?b.*", 0);
    parse("[bad", 0);
    parse("x/y", 0);
    parse("", 0);
    /* memory */
    r.x.ax = 0x4800, r.x.bx = 0x10;
    dos();
    seg = r.x.ax;
    r.x.ax = 0x4900;
    s.es = seg;
    intdosx(&r, &r, &s);
    line("49h");
    r.x.ax = 0x4900;
    s.es = seg;
    intdosx(&r, &r, &s);
    line("49h twice");
    r.x.ax = 0x4900;
    s.es = 0x0040;
    intdosx(&r, &r, &s);
    line("49h not a block");
    err59("59h after it");
    r.x.ax = 0x5800;
    dos();
    line("5800h");
    r.x.ax = 0x5802;
    dos();
    printf("5802h cf=%u al=%02X\n", r.x.cflag ? 1 : 0, r.h.al);
    /* the rest */
    r.x.ax = 0x2B00, r.x.cx = 2026, r.x.dx = 0x021E;
    dos();
    printf("2Bh 30 Feb al=%02X\n", r.h.al);
    r.x.ax = 0x2D00, r.x.cx = 0x1900, r.x.dx = 0;
    dos();
    printf("2Dh 25:00 al=%02X\n", r.h.al);
    r.x.ax = 0x5D06;
    dos();
    printf("5D06h cf=%u cx=%04X dx=%04X\n", r.x.cflag ? 1 : 0, r.x.cx, r.x.dx);
    r.x.ax = 0x6300;
    dos();
    printf("6300h cf=%u al=%02X\n", r.x.cflag ? 1 : 0, r.h.al);
    ds_es();
    r.x.ax = 0x71A0, r.x.dx = (unsigned)"C:\\", r.x.di = (unsigned)buf, r.x.cx = 32;
    r.x.cflag = 0;
    dos();
    line("71A0h");
    r.x.ax = 0x4411, r.x.bx = 1;
    dos();
    line("4411h");
    ds_es();
    r.x.ax = 0x6900, r.x.bx = 3, r.x.dx = (unsigned)buf;
    dos();
    line("6900h C:");
    ds_es();                                    /* (what doesn't hang on the disk's size or its kind of FAT) */
    memset(buf, 0, sizeof buf);
    r.x.ax = 0x7302, r.x.dx = 3, r.x.cx = 0x3F, r.x.di = (unsigned)buf;
    dos();
    printf("7302h C: cf=%u len=%04X bps=%u fats=%u media=%02X\n", r.x.cflag ? 1 : 0, *(unsigned *)buf,
           *(unsigned *)(buf + 4), (unsigned char)buf[10], (unsigned char)buf[0x19]);
    r.x.ax = 0x7302, r.x.dx = 3, r.x.cx = 0x20, r.x.di = (unsigned)buf;
    dos();
    line("7302h small buffer");
    r.x.ax = 0x1900;
    dos();
    cur = r.h.al;
    fdrive = argc > 1 && argv[1][0] >= 'A' && argv[1][0] <= 'Z' ? (unsigned)(argv[1][0] - 'A') : cur;
    fcbs(cur);
    small();
    ds_es();
    r.x.ax = 0x4D00;
    dos();
    printf("4Dh at the start ax=%04X\n", r.x.ax);
    h2 = spawnl(P_WAIT, "C:\\NONE.EXE", "C:\\NONE.EXE", NULL);
    printf("spawn none=%d\n", (int)h2);
    redirection();
    call_dx(0x3B00, "\\", 0);
    call_dx(0x3A00, "C:\\DDIFF", 0);
    line("3Ah DDIFF");
    printf("DOSDIFF end\n");
    return 0;
}
