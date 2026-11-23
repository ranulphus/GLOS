/* What GLOS.EXE's loader had of DOS, for its native build (-dNATIVE; docs/v1/native-dos.md, native boot, NB1):
 * files by kernel/fs/fat.c over the BIOS's INT 13h on the disk GLOS started from (its active FAT partition, or the
 * whole disk: a floppy, a disk with no table), as stdio's fopen, fgets, fread, fseek and fclose, which main.c
 * calls; paths "C:\GLOS\GLOS.CFG" or "\GLOS\..." (the drive ignored: there is only the one volume yet); memory
 * as _dos_allocmem and _dos_freemem give it, up from the end of the loader's own (no DOS frees it: the kernel
 * makes conventional memory its own DOS's, NB2); getenv, with no environment, none. */
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../kernel/fs/fat.h"

unsigned native_psp;                    /* (nstart.asm's; main.c's _psp, the C library's, is set from it) */
unsigned char native_disk;

/* ---- the disk */

static unsigned long vol_lba;           /* the volume's first sector on the disk */
static int ext;                         /* the BIOS's LBA calls (41h) */
static unsigned spt, heads;
static struct fat_vol vol;
static int mounted;

static int sectors_in(unsigned long lba, unsigned n, void *buf)
{
    union REGS r;
    struct SREGS s;
    int tries;

    for (tries = 0; tries < 3; tries++) {
        segread(&s);
        if (ext) {
            static struct { unsigned char size, zero; unsigned n, off, seg; unsigned long lo, hi; } dap;

            dap.size = 16, dap.zero = 0, dap.n = n, dap.off = (unsigned)buf, dap.seg = s.ds;
            dap.lo = lba, dap.hi = 0;
            r.h.ah = 0x42;
            r.h.dl = native_disk;
            r.x.si = (unsigned)&dap;
        } else {
            unsigned long c = lba / ((unsigned long)spt * heads), t = lba % ((unsigned long)spt * heads);

            r.h.ah = 0x02;
            r.h.al = (unsigned char)n;
            r.h.ch = (unsigned char)c;
            r.h.cl = (unsigned char)(((c >> 2) & 0xC0) | (t % spt + 1));
            r.h.dh = (unsigned char)(t / spt);
            r.h.dl = native_disk;
            s.es = s.ds;
            r.x.bx = (unsigned)buf;
        }
        int86x(0x13, &r, &r, &s);
        if (!r.x.cflag)
            return 0;
        r.h.ah = 0x00;                          /* (a reset, and again) */
        r.h.dl = native_disk;
        int86(0x13, &r, &r);
    }
    return -1;
}

/* fat.c's reads: count sectors of the volume, up to 32 in a call; a CHS call stays inside its track. Sector by
   sector until 2026-11-23 without the LBA calls: the owner's PCChips M925G, which starts a USB stick as a drive of
   the floppies' numbers, took 12 ms for each call and 55 seconds to load the kernel. A call of several sectors
   that fails is made again a sector at a time (a real floppy's DMA across a 64 KB boundary). */
static int vol_read(void *ctx, unsigned long lba, unsigned char *buf, unsigned count)
{
    (void)ctx;
    while (count) {
        unsigned n = count > 32 ? 32 : count;

        if (ext && n > 16)                      /* (the LBA calls: 16 at most, as before) */
            n = 16;
        if (!ext && n > spt - (unsigned)((vol_lba + lba) % spt))
            n = spt - (unsigned)((vol_lba + lba) % spt);
        if (sectors_in(vol_lba + lba, n, buf) != 0) {
            if (n == 1 || sectors_in(vol_lba + lba, 1, buf) != 0)
                return -1;
            n = 1;
        }
        lba += n, buf += n * 512u, count -= n;
    }
    return 0;
}

static int is_fat_type(unsigned t)
{
    return t == 0x01 || t == 0x04 || t == 0x06 || t == 0x0B || t == 0x0C || t == 0x0E;
}

int native_mount(void)
{
    static unsigned char s[512];
    union REGS r;
    struct SREGS sr;
    unsigned i;

    r.h.ah = 0x41, r.x.bx = 0x55AA, r.h.dl = native_disk;
    int86(0x13, &r, &r);
    ext = native_disk >= 0x80 && !r.x.cflag && r.x.bx == 0xAA55 && (r.x.cx & 1);
    segread(&sr);
    sr.es = 0;
    r.h.ah = 0x08, r.h.dl = native_disk, r.x.di = 0;
    int86x(0x13, &r, &r, &sr);
    spt = r.x.cflag ? 18 : (r.h.cl & 0x3F);
    heads = r.x.cflag ? 2 : r.h.dh + 1u;
    if (!spt)
        spt = 63;
    vol_lba = 0;
    if (native_disk >= 0x80 && sectors_in(0, 1, s) == 0 && s[510] == 0x55 && s[511] == 0xAA)
        for (i = 0; i < 4; i++) {
            unsigned char *e = s + 446 + i * 16;

            if ((e[0] & 0x80) && is_fat_type(e[4])) {
                vol_lba = *(unsigned long *)(e + 8);
                break;
            }
        }
    if (fat_mount(&vol, vol_read, 0, 437) != 0)
        return -1;
    mounted = 1;
    return 0;
}

/* ---- files */

struct nfile {
    struct fat_file f;
    int used, text;
};
static struct nfile files[4];

static const char *on_volume(const char *path)
{
    if (path[0] && path[1] == ':')
        path += 2;
    return path;
}

FILE *fopen(const char *path, const char *mode)
{
    unsigned i;

    if (!mounted || mode[0] != 'r')
        return NULL;
    for (i = 0; i < sizeof files / sizeof files[0] && files[i].used; i++)
        ;
    if (i == sizeof files / sizeof files[0] || fat_open(&vol, on_volume(path), &files[i].f) != 0)
        return NULL;
    files[i].used = 1;
    files[i].text = !strchr(mode, 'b');
    return (FILE *)&files[i];
}

int fclose(FILE *fp)
{
    ((struct nfile *)fp)->used = 0;
    return 0;
}

size_t fread(void *buf, size_t size, size_t n, FILE *fp)
{
    long got = fat_read(&((struct nfile *)fp)->f, buf, (unsigned long)size * n);

    return got <= 0 || !size ? 0 : (size_t)(got / size);
}

int fseek(FILE *fp, long off, int whence)
{
    struct fat_file *f = &((struct nfile *)fp)->f;
    long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)f->pos : (long)f->size;

    return base + off < 0 || fat_seek(f, (unsigned long)(base + off)) != 0 ? -1 : 0;
}

/* (text mode: CR LF read as LF, as DOS's C library gives a line) */
char *fgets(char *buf, int n, FILE *fp)
{
    struct fat_file *f = &((struct nfile *)fp)->f;
    int i = 0;
    char c;

    while (i < n - 1 && fat_read(f, &c, 1) == 1) {
        if (c == '\r' && ((struct nfile *)fp)->text)
            continue;
        buf[i++] = c;
        if (c == '\n')
            break;
    }
    if (!i)
        return NULL;
    buf[i] = 0;
    return buf;
}

int access(const char *path, int mode)
{
    struct fat_ent e;

    (void)mode;
    return mounted && fat_lookup(&vol, on_volume(path), &e) == 0 ? 0 : -1;
}

/* ---- memory: up from the end of the loader's own, as the PSP's word at 2 says */

static unsigned next_seg;

unsigned _dos_allocmem(unsigned paras, unsigned *seg)
{
    unsigned top = *(unsigned __far *)MK_FP(0x40, 0x13) * 64u;     /* (the BIOS's KB below 640: EBDA kept) */

    if (!next_seg)
        next_seg = *(unsigned __far *)MK_FP(native_psp, 2);
    if ((unsigned long)next_seg + paras > top) {
        *seg = top > next_seg ? top - next_seg : 0;
        return 8;
    }
    *seg = next_seg;
    next_seg += paras;
    return 0;
}

unsigned _dos_freemem(unsigned seg)
{
    (void)seg;                                  /* (the loader's memory is all the kernel's after it) */
    return 0;
}

char *getenv(const char *name)
{
    (void)name;
    return NULL;
}
