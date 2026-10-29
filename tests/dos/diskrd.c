/* diskrd.c - how fast a DJGPP program reads the disk, under plain DOS or GLOS
 * (read-only; docs/disk-cache.md: the bench's measurements, and the cache
 * suite's reads). DISKRD FILE [LBA]: one line per test,
 *   DISKRD test=NAME bytes=B calls=N secs=S mbs=M us_call=U
 * seq32 (32 KB read()s, the first 16 MB of FILE), seq32b (the same again: a
 * cache shows), rnd4 (1000 4 KB reads at random offsets in its first 64 MB),
 * seq512 (512-byte read()s, 2 MB: the cost of a call), int13 (16 MB of raw
 * sectors by INT 13h AH=42h, 64 a call, from the drive's sector LBA (2,000,000):
 * the BIOS's own speed). The 32 KB of a call go to a DOS block of their own:
 * the stub's transfer buffer is only 16 KB, and overrunning it hangs CWSDPMI. */
#include <dpmi.h>
#include <fcntl.h>
#include <go32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/movedata.h>
#include <time.h>
#include <unistd.h>

static char buf[32768];

static void report(const char *name, double bytes, long calls, uclock_t t)
{
    double s = (double)t / UCLOCKS_PER_SEC;
    printf("DISKRD test=%s bytes=%.0f calls=%ld secs=%.3f mbs=%.3f us_call=%.1f\n", name, bytes, calls, s,
           s > 0 ? bytes / s / 1048576.0 : 0, calls ? s * 1e6 / calls : 0);
    fflush(stdout);
}

static void seq(const char *path, const char *name, int chunk, long total)
{
    int fd = open(path, O_RDONLY | O_BINARY);
    long done = 0, calls = 0;
    uclock_t t0;
    int n;
    if (fd < 0) {
        printf("DISKRD test=%s error=open\n", name);
        return;
    }
    t0 = uclock();
    while (done < total && (n = read(fd, buf, chunk)) > 0) {
        done += n;
        calls++;
    }
    report(name, done, calls, uclock() - t0);
    close(fd);
}

static void rnd(const char *path)
{
    int fd = open(path, O_RDONLY | O_BINARY), i;
    long size;
    uclock_t t0;
    unsigned seed = 12345;
    if (fd < 0)
        return;
    size = lseek(fd, 0, SEEK_END);
    if (size > 64L << 20)
        size = 64L << 20;
    t0 = uclock();
    for (i = 0; i < 1000; i++) {
        seed = seed * 1103515245u + 12345u;
        lseek(fd, (long)((seed >> 8) % (unsigned long)(size - 4096)) & ~511L, SEEK_SET);
        read(fd, buf, 4096);
    }
    report("rnd4", 4096.0 * 1000, 1000, uclock() - t0);
    close(fd);
}

/* INT 13h extended read: the disk address packet in the transfer buffer, the
 * sectors in 32 KB of a 96 KB DOS block that cross no 64 KB boundary. */
static void int13(unsigned long lba)
{
    struct { unsigned char size, zero; unsigned short count, off, seg; unsigned long lba_lo, lba_hi; } __attribute__((packed)) dap;
    __dpmi_regs r;
    long calls = 0, bad = 0;
    unsigned long at;
    int sel, seg = __dpmi_allocate_dos_memory(6144, &sel);
    uclock_t t0;
    if (seg == -1) {
        printf("DISKRD test=int13 error=dosmem\n");
        return;
    }
    at = (unsigned long)seg << 4;
    if ((at & 0xFFFF) + 32768 > 0x10000)
        at = (at + 0xFFFF) & ~0xFFFFUL;
    t0 = uclock();
    for (; calls < 512; calls++, lba += 64) {
        dap.size = 16; dap.zero = 0; dap.count = 64;
        dap.off = at & 15; dap.seg = at >> 4;
        dap.lba_lo = lba; dap.lba_hi = 0;
        dosmemput(&dap, sizeof dap, __tb);
        memset(&r, 0, sizeof r);
        r.h.ah = 0x42; r.h.dl = 0x80;
        r.x.ds = __tb >> 4; r.x.si = __tb & 15;
        __dpmi_int(0x13, &r);
        if (r.x.flags & 1)
            bad++;
    }
    report(bad ? "int13-errors" : "int13", 512.0 * 64 * calls, calls, uclock() - t0);
    __dpmi_free_dos_memory(sel);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: DISKRD FILE [LBA]\n");
        return 2;
    }
    seq(argv[1], "seq32", 32768, 16L << 20);
    seq(argv[1], "seq32b", 32768, 16L << 20);
    rnd(argv[1]);
    seq(argv[1], "seq512", 512, 2L << 20);
    int13(argc > 2 ? strtoul(argv[2], 0, 0) : 2000000);
    return 0;
}
