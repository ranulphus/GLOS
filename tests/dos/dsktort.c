/* dsktort.c - the disk cache's torture test (docs/disk-cache.md step C3; DJGPP).
 *
 *   DSKTORT DRIVE SEED OPS [PIOBASE]
 *
 * DRIVE (hex, e.g. 81) is a blank disk DOS doesn't use (run.py --scratch-hdd). Its first 8,192 sectors get
 * OPS random operations, deterministic from SEED, each read checked against a model of what every sector
 * must hold:
 *   - reads and writes by INT 13h 42h/43h (LBA) and 02h/03h (CHS, through AH=08h's geometry);
 *   - made from protected mode (0300h: the DPMI host's INT 13h) and by an INT 13h instruction in real-mode
 *     code (0301h into a two-byte thunk: the V86 trap);
 *   - small reads (1-7 sectors: the block fills), big ones (runs), any alignment;
 *   - reads that run past the disk's end (the BIOS's error, the same with the cache and without);
 *   - with PIOBASE (170 for IDE 1:0), single sectors written by the program itself through the IDE ports,
 *     behind the BIOS's back: the cache must drop what it held (its port watch), or later reads go wrong.
 * Every 64 operations, a file of C:\DSKTORT\ is written or read back through DOS and checked.
 * Then C:\DSKTORT.BIN, written by DOS (by CHS on FreeDOS), is read raw by LBA through its FAT16 clusters and
 * compared, before and after DOS rewrites it: the cache keys CHS and LBA alike.
 *
 * Output: "DSKTORT ops=N bad=B pio=P files=F cross=ok|skip|bad digest=XXXXXXXX ok=0|1". The digest is of every
 * call's AH, CF, AL (CHS) and the DAP's count: a run with the cache on must give the one without. */
#include <dpmi.h>
#include <fcntl.h>
#include <go32.h>
#include <io.h>
#include <pc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>
#include <sys/stat.h>
#include <unistd.h>

#define REGION   8192u                          /* sectors under test */
#define MAXN     64u                            /* sectors a call */
#define NFILES   4
#define FILEMAX  (256u * 1024u)

static unsigned ver[REGION];                    /* each sector's version: 0 zeros (blank), else pattern(lba, ver) */
static unsigned seed, digest = 2166136261u, bad, pio_writes, file_ops, verno = 1;
static int drive, dos_seg, dos_sel, thunk_seg, thunk_sel;
static unsigned cyls, heads, spt, dsectors;
static unsigned char sec[MAXN * 512];
static unsigned char *files[NFILES];
static unsigned flen[NFILES];
static int in_cross;                            /* drive 80h's reads: not in the digest (its clusters move) */
static FILE *trace;                             /* DSKTORT_LOG=FILE: every call's results, to compare two runs */
static unsigned calls;

static unsigned rnd(void) { seed = seed * 1103515245u + 12345u; return seed >> 8; }

static void mix(unsigned v)
{
    int i;
    for (i = 0; i < 4; i++, v >>= 8)
        digest = (digest ^ (v & 0xFF)) * 16777619u;
}

static void pattern(unsigned lba, unsigned v, unsigned char *out)
{
    unsigned i, *w = (unsigned *)out;
    for (i = 0; i < 128; i++)
        w[i] = v ? (lba * 2654435761u) ^ (v * 40503u) ^ (i * 69069u) ^ 0x5A5A5A5Au : 0;
}

/* One INT 13h: by 0300h, or (real) by the thunk's INT instruction. The DOS block: DAP at 0, data at 16. */
static void i13(__dpmi_regs *r, int real)
{
    if (real) {
        r->x.cs = (unsigned short)thunk_seg;
        r->x.ip = 0;
        r->x.ss = r->x.sp = 0;
        __dpmi_simulate_real_mode_procedure_retf(r);
    } else {
        r->x.ss = r->x.sp = 0;
        __dpmi_int(0x13, r);
    }
}

/* n sectors at lba by LBA (42h/43h) or CHS (02h/03h); data in sec[]. Returns AH (0 ok), CF in *cf. */
static unsigned xfer(int write, int chs, int real, unsigned lba, unsigned n, int *cf)
{
    __dpmi_regs r;
    unsigned char dap[16];
    unsigned base = (unsigned)dos_seg << 4, cnt = 0;
    memset(&r, 0, sizeof r);
    if (write)
        dosmemput(sec, n * 512, base + 16);
    if (chs) {
        unsigned c = lba / (heads * spt), h = (lba / spt) % heads, s = lba % spt + 1;
        r.h.ah = write ? 0x03 : 0x02;
        r.h.al = (unsigned char)n;
        r.x.cx = (unsigned short)(((c & 0xFF) << 8) | ((c >> 2) & 0xC0) | s);
        r.h.dh = (unsigned char)h;
        r.x.es = (unsigned short)dos_seg;
        r.x.bx = 16;
    } else {
        memset(dap, 0, 16);
        dap[0] = 16;
        *(unsigned short *)(dap + 2) = (unsigned short)n;
        *(unsigned short *)(dap + 4) = 16;
        *(unsigned short *)(dap + 6) = (unsigned short)dos_seg;
        *(unsigned *)(dap + 8) = lba;
        dosmemput(dap, 16, base);
        r.h.ah = write ? 0x43 : 0x42;
        r.x.ds = (unsigned short)dos_seg;
        r.x.si = 0;
    }
    r.h.dl = (unsigned char)drive;
    i13(&r, real);
    *cf = r.x.flags & 1;
    if (!chs)
        dosmemget(base + 2, 2, &cnt);
    if (!in_cross) {
        mix(r.h.ah | (*cf << 8) | (chs ? (unsigned)r.h.al << 16 : 0) | (write << 24));
        if (!chs)
            mix(cnt & 0xFFFF);
    }
    if (trace)
        fprintf(trace, "%u %s%s lba=%u n=%u real=%d ah=%02x cf=%d al=%02x cnt=%u\n", calls, write ? "w" : "r",
                chs ? "chs" : "lba", lba, n, real, r.h.ah, *cf, r.h.al, chs ? 0 : cnt & 0xFFFF);
    calls++;
    if (!write && !*cf)
        dosmemget(base + 16, n * 512, sec);
    return r.h.ah;
}

static int pio_wait(unsigned base, unsigned want, int err)  /* status: BSY clear, want's bits set; err: ERR fails */
{                                               /* (ERR stays from the last command until the next) */
    long i;
    for (i = 0; i < 2000000L; i++) {
        unsigned s = inportb(base + 7);
        if (!(s & 0x80) && (s & want) == want)
            return err && (s & 1) ? -1 : 0;
    }
    return -2;
}

/* One sector by the IDE ports (master, LBA 28), interrupts off at the device (nIEN). */
static int pio_write(unsigned base, unsigned lba, const unsigned char *data)
{
    unsigned i;
    int r;
    outportb(base + 0x206, 0x02);
    if (pio_wait(base, 0, 0) != 0)
        return -1;
    outportb(base + 6, 0xE0 | ((lba >> 24) & 0x0F));
    if (pio_wait(base, 0x40, 0) != 0)           /* DRDY after the device is selected */
        return -3;
    outportb(base + 2, 1);
    outportb(base + 3, lba & 0xFF);
    outportb(base + 4, (lba >> 8) & 0xFF);
    outportb(base + 5, (lba >> 16) & 0xFF);
    outportb(base + 7, 0x30);                   /* WRITE SECTORS: the command the cache's watch must see */
    if (pio_wait(base, 0x08, 1) != 0)
        return -2;
    for (i = 0; i < 256; i++)
        outportw(base, ((const unsigned short *)data)[i]);
    r = pio_wait(base, 0, 1);
    inportb(base + 7);
    outportb(base + 0x206, 0x00);
    return r;
}

static void check(unsigned lba, unsigned n, const char *how)
{
    unsigned i;
    static unsigned char want[512];
    for (i = 0; i < n; i++) {
        pattern(lba + i, ver[lba + i], want);
        if (memcmp(want, sec + i * 512, 512)) {
            if (bad++ < 20)
                printf("DSKTORT bad lba=%u ver=%u how=%s\n", lba + i, ver[lba + i], how);
        }
    }
}

static void file_op(void)
{
    unsigned k = rnd() % NFILES, off, len, i;
    char name[32];
    int fd;
    sprintf(name, "C:\\DSKTORT\\F%u.BIN", k);
    if (rnd() % 3 || !flen[k]) {                /* write a piece: within the file or just after it (DOS leaves
                                                   a gap past the end holding whatever the disk had) */
        len = 1 + rnd() % 16384;
        off = rnd() % (flen[k] + 1);
        if (off > FILEMAX - len)
            off = FILEMAX - len;
        for (i = 0; i < len; i++)
            files[k][off + i] = (unsigned char)(rnd() >> 4);
        if ((fd = open(name, O_WRONLY | O_CREAT | O_BINARY, S_IRUSR | S_IWUSR)) < 0
            || lseek(fd, off, SEEK_SET) != (off_t)off || write(fd, files[k] + off, len) != (int)len) {
            if (bad++ < 20)
                printf("DSKTORT bad file=%u write\n", k);
        }
        if (fd >= 0)
            close(fd);
        if (off + len > flen[k])
            flen[k] = off + len;
    } else {                                    /* read it all back */
        static unsigned char got[FILEMAX];
        if ((fd = open(name, O_RDONLY | O_BINARY)) < 0 || read(fd, got, flen[k]) != (int)flen[k]
            || memcmp(got, files[k], flen[k])) {
            if (bad++ < 20)
                printf("DSKTORT bad file=%u read\n", k);
        }
        if (fd >= 0)
            close(fd);
    }
    file_ops++;
}

/* C:\DSKTORT.BIN through FAT16 by LBA (drive 80h): 1 same, 0 skipped (not FAT16), -1 different. */
static int cross1(const unsigned char *want, unsigned len)
{
    unsigned char b[512];
    unsigned part, spc, rsv, nfat, rootents, fatsz, root, data, i, j, clus, n, got = 0;
    int cf, saved = drive;
    drive = 0x80;
    in_cross = 1;
#define RD(lba) (xfer(0, 0, 0, (lba), 1, &cf), memcpy(b, sec, 512), cf)
    if (RD(0))
        return drive = saved, 0;
    part = *(unsigned *)(b + 0x1C6);
    if (RD(part) || *(unsigned short *)(b + 11) != 512 || !*(unsigned short *)(b + 22))
        return drive = saved, 0;                /* (not FAT12/16: FAT32 keeps its FAT size elsewhere) */
    spc = b[13], rsv = *(unsigned short *)(b + 14), nfat = b[16], rootents = *(unsigned short *)(b + 17);
    fatsz = *(unsigned short *)(b + 22);
    root = part + rsv + nfat * fatsz;
    data = root + (rootents * 32 + 511) / 512;
    for (i = 0, clus = 0; i < (rootents * 32 + 511) / 512 && !clus; i++) {
        if (RD(root + i))
            return drive = saved, -1;
        for (j = 0; j < 512; j += 32)
            if (!memcmp(b + j, "DSKTORT BIN", 11)) {
                clus = *(unsigned short *)(b + j + 26);
                break;
            }
    }
    if (!clus)
        return drive = saved, -1;
    while (clus >= 2 && clus < 0xFFF8 && got < len) {
        for (i = 0; i < spc && got < len; i++, got += 512) {
            if (RD(data + (clus - 2) * spc + i) || memcmp(b, want + got, len - got < 512 ? len - got : 512))
                return drive = saved, -1;
        }
        n = clus * 2;                           /* the next cluster from the FAT */
        if (RD(part + rsv + n / 512))
            return drive = saved, -1;
        clus = *(unsigned short *)(b + n % 512);
    }
#undef RD
    drive = saved;
    return got >= len ? 1 : -1;
}

static int cross(const unsigned char *want, unsigned len)
{
    int r = cross1(want, len);
    in_cross = 0;
    return r;
}

static int dos_file(const char *name, const unsigned char *data, unsigned len)
{
    int fd = open(name, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, S_IRUSR | S_IWUSR);
    __dpmi_regs r;
    if (fd < 0 || write(fd, data, len) != (int)len)
        return -1;
    close(fd);
    memset(&r, 0, sizeof r);
    r.h.ah = 0x0D;                              /* DOS's buffers to the disk */
    __dpmi_int(0x21, &r);
    return 0;
}

int main(int argc, char **argv)
{
    static unsigned char xbin[65536];
    unsigned ops, op, i, lba, n, pio = 0, what, k;
    int cf, cr1 = 0, cr2 = 0, real, chs;
    __dpmi_regs r;
    unsigned char code[3] = { 0xCD, 0x13, 0xCB };       /* INT 13h; RETF */

    if (argc < 4) {
        printf("usage: DSKTORT DRIVE SEED OPS [PIOBASE]\n");
        return 2;
    }
    drive = (int)strtol(argv[1], 0, 16);
    seed = (unsigned)strtoul(argv[2], 0, 0);
    ops = (unsigned)strtoul(argv[3], 0, 0);
    if (argc > 4)
        pio = (unsigned)strtoul(argv[4], 0, 16);
    if ((dos_seg = __dpmi_allocate_dos_memory((16 + MAXN * 512 + 15) / 16, &dos_sel)) == -1
        || (thunk_seg = __dpmi_allocate_dos_memory(1, &thunk_sel)) == -1) {
        printf("DSKTORT error=dosmem ok=0\n");
        return 1;
    }
    dosmemput(code, 3, (unsigned)thunk_seg << 4);
    memset(&r, 0, sizeof r);
    r.h.ah = 0x08;
    r.h.dl = (unsigned char)drive;
    i13(&r, 0);
    if (r.x.flags & 1) {
        printf("DSKTORT error=geometry ok=0\n");
        return 1;
    }
    cyls = (r.h.ch | ((r.h.cl & 0xC0) << 2)) + 1, heads = r.h.dh + 1, spt = r.h.cl & 63;
    dsectors = cyls * heads * spt;
    if (dsectors < REGION + 64) {
        printf("DSKTORT error=small sectors=%u ok=0\n", dsectors);
        return 1;
    }
    mkdir("C:\\DSKTORT", 0);
    for (k = 0; k < NFILES; k++) {              /* (a run before left its files) */
        char name[32];
        sprintf(name, "C:\\DSKTORT\\F%u.BIN", k);
        unlink(name);
        files[k] = calloc(FILEMAX, 1);
    }
    if (getenv("DSKTORT_LOG"))
        trace = fopen(getenv("DSKTORT_LOG"), "w");

    memset(sec, 0, sizeof sec);                 /* the region blank, whatever a run before left */
    for (lba = 0; lba < REGION; lba += MAXN)
        if (xfer(1, 0, 0, lba, MAXN, &cf) || cf) {
            printf("DSKTORT error=zero lba=%u ok=0\n", lba);
            return 1;
        }
    for (i = 0; i < sizeof xbin; i++)           /* DOS's file, then read raw through its clusters */
        xbin[i] = (unsigned char)(i * 7 + seed);
    if (dos_file("C:\\DSKTORT.BIN", xbin, sizeof xbin) == 0)
        cr1 = cross(xbin, sizeof xbin);

    for (op = 0; op < ops; op++) {
        what = rnd() % 100;
        real = rnd() & 1;
        n = what >= 84 ? 1 + rnd() % 7 : 1 + rnd() % MAXN;
        lba = rnd() % (REGION - n);
        if (what < 84 && (rnd() & 3) == 0)      /* a quarter of them on 4 KB boundaries */
            lba &= ~7u;
        chs = (what >= 50 && what < 60) || (what >= 75 && what < 80) || (what >= 84 && (rnd() & 1));
        if (what < 60 || what >= 84) {          /* a read */
            if (xfer(0, chs, real, lba, n, &cf) || cf) {
                if (bad++ < 20)
                    printf("DSKTORT bad read lba=%u n=%u chs=%d real=%d\n", lba, n, chs, real);
            } else {
                check(lba, n, chs ? "chs" : "lba");
            }
        } else if (what < 80) {                 /* a write: new versions */
            for (i = 0; i < n; i++)
                pattern(lba + i, verno + i, sec + i * 512);
            if (xfer(1, chs, real, lba, n, &cf) || cf) {
                if (bad++ < 20)
                    printf("DSKTORT bad write lba=%u n=%u\n", lba, n);
            } else {
                for (i = 0; i < n; i++)
                    ver[lba + i] = verno + i;
            }
            verno += n;
        } else if (what < 82) {                 /* behind the BIOS's back */
            if (pio) {
                pattern(lba, verno, sec);
                int e = pio_write(pio, lba, sec);
                if (e == 0) {
                    ver[lba] = verno;
                    pio_writes++;
                } else if (bad++ < 20) {
                    printf("DSKTORT bad pio lba=%u e=%d\n", lba, e);
                }
                verno++;
            }
        } else {                                /* past the end: the BIOS's error */
            xfer(0, rnd() & 1, real, dsectors - 1, 2, &cf);
        }
        if ((op & 63) == 63)
            file_op();
    }
    for (k = 0; k < NFILES; k++)                /* every file read back once more */
        if (flen[k]) {
            static unsigned char got[FILEMAX];
            char name[32];
            int fd;
            sprintf(name, "C:\\DSKTORT\\F%u.BIN", k);
            if ((fd = open(name, O_RDONLY | O_BINARY)) < 0 || read(fd, got, flen[k]) != (int)flen[k]
                || memcmp(got, files[k], flen[k])) {
                if (bad++ < 20)
                    printf("DSKTORT bad file=%u end\n", k);
            }
            if (fd >= 0)
                close(fd);
        }
    for (i = 0; i < sizeof xbin; i++)           /* DOS rewrites it: the raw reads must see the new one */
        xbin[i] = (unsigned char)(i * 13 + seed + 1);
    if (dos_file("C:\\DSKTORT.BIN", xbin, sizeof xbin) == 0)
        cr2 = cross(xbin, sizeof xbin);
    printf("DSKTORT ops=%u bad=%u pio=%u files=%u cross=%s digest=%08x ok=%d\n", ops, bad, pio_writes, file_ops,
           cr1 == 1 && cr2 == 1 ? "ok" : cr1 == 0 || cr2 == 0 ? "skip" : "bad", digest,
           !bad && cr1 >= 0 && cr2 >= 0);
    return bad != 0;
}
