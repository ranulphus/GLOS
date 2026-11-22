/* dosdiff: what DOS answers where the books leave it open (docs/v1/native-dos.md, NS2's "to check" list), one
 * line a probe, the same on any disk and in any memory: run under MS-DOS 7.1 (the reference: a private boot disk,
 * tools/ndos/msdos-image.sh) and in a native machine (loopa-ndos), and the lines compared. It works in a folder of
 * its own, C:\DDIFF, made and removed. */
#include <dos.h>
#include <i86.h>
#include <process.h>
#include <stdio.h>
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

int main(void)
{
    unsigned h, h2, seg;
    unsigned char dta[64];

    printf("DOSDIFF 2\n");
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
    r.x.ax = 0x4D00;
    dos();
    printf("4Dh at the start ax=%04X\n", r.x.ax);
    h2 = spawnl(P_WAIT, "C:\\NONE.EXE", "C:\\NONE.EXE", NULL);
    printf("spawn none=%d\n", (int)h2);
    call_dx(0x3B00, "\\", 0);
    call_dx(0x3A00, "C:\\DDIFF", 0);
    line("3Ah DDIFF");
    printf("DOSDIFF end\n");
    return 0;
}
