/* NDOS, the shell's command for native machines (docs/v1/native-dos.md, native-DOS sessions, NS1): their volume
 * and their programs.
 *
 *   NDOS FORMAT n         GLOS's IDE disk n (glos ndos disks lists them) made a native volume: a partition table
 *                         with one partition of a hidden FAT type (DOS gives those no letter: the plan's rule), a
 *                         FAT volume in it. Only a disk that is blank, or a native volume already.
 *   NDOS DISKS            GLOS's IDE disks, and which is native
 *   NDOS DIR [N:\path]    a folder of the native volume
 *   NDOS COPY d:\path N:\path    a file from a DOS drive (the DOS server) onto the native volume: two volumes, one
 *                         writer each
 *   NDOS RUN N:\path [tail] | HELLO    a .COM on a native machine (kernel/vm/ndm.c): what it writes, its exit code
 *
 * The native volume's disk is read and written only by GLOS's own IDE driver (kernel/drv/ata.c) and only on the
 * VM thread, as work posted there (vm_post): at a point where the BIOS is not in the middle of a command (not
 * inside an INT 13h the disk cache makes, which every INT 13h is while the cache traps them), so one user of the
 * channel at a time. NDOS refuses while the cache is off. One NDOS command at a time, on a thread of its own. */
#include <string.h>

#include "cache.h"
#include "clock.h"
#include "dos.h"
#include "../drv/ide.h"
#include "io.h"
#include "kprintf.h"
#include "mm.h"
#include "ndcmd.h"
#include "ndm.h"
#include "sched.h"
#include "timer.h"
#include "vm.h"
#include "../disk/disk.h"
#include "../fs/fsv.h"
#include "../ndos/dtime.h"

#define OUT_RING 8192u

struct ndcmd {
    char args[256];
    char out[OUT_RING];
    u32 head, tail;
    volatile int done;
    int code, gone;
    void (*kick)(void);
};

static int busy;

static void say(struct ndcmd *c, const char *fmt, ...)
{
    char b[256];
    __builtin_va_list ap;
    u32 n, i, f;

    __builtin_va_start(ap, fmt);
    n = (u32)kvsnprintf(b, sizeof b, fmt, ap);
    __builtin_va_end(ap);
    f = irq_save();
    for (i = 0; i < n && i < sizeof b; i++)
        if (c->head - c->tail < OUT_RING)
            c->out[c->head++ % OUT_RING] = b[i];
    irq_restore(f);
    if (c->kick)
        c->kick();
}

/* ---- the disk, by work on the VM thread */

struct io {
    struct vm_work w;
    int op;                             /* 0 read, 1 write, 2 the probe */
    struct ata_dev *d;
    unsigned long lba;
    unsigned n;
    void *buf;
    int r;
};

static struct waitq io_q;

static int io_fn(struct vm_work *w, struct trapframe *tf)
{
    struct io *x = (struct io *)w;

    (void)tf;
    if (x->op == 2)
        x->r = ide_probe(0);
    else if (x->op == 1)
        x->r = ata_write(x->d, x->lba, 0, x->n, x->buf, 0);
    else
        x->r = ata_read(x->d, x->lba, 0, x->n, x->buf, 0);
    return 0;
}

static void io_done(void) { thread_wake(&io_q); }

static int on_vm(struct io *x)
{
    x->w.fn = io_fn;
    x->w.done = io_done;
    x->w.patience = 256;
    while (vm_post(&x->w) != 0)                 /* (another's work first) */
        thread_sleep_until(timer_ticks() + 4);
    while (x->w.state != VW_DONE)
        thread_wait(&io_q, timer_ticks() + 256);
    return x->r;
}

static int disk_io(struct ata_dev *d, int op, unsigned long lba, unsigned n, void *buf)
{
    struct io x;

    memset(&x, 0, sizeof x);
    x.op = op, x.d = d, x.lba = lba, x.n = n, x.buf = buf;
    return on_vm(&x);
}

/* ---- the native volume */

static struct fsv_vol vol;
static struct ata_dev *vdev;
static unsigned long vbase;
static int vmounted;

static int is_native(unsigned t) { return t == 0x11 || t == 0x14 || t == 0x16 || t == 0x1B || t == 0x1C || t == 0x1E; }

static int vrd(void *ctx, unsigned long lba, unsigned char *buf, unsigned count)
{
    (void)ctx;
    return disk_io(vdev, 0, vbase + lba, count, buf) != 0;
}

static int vwr(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count)
{
    (void)ctx;
    return disk_io(vdev, 1, vbase + lba, count, (void *)buf) != 0;
}

static unsigned long vnow(void *ctx)
{
    struct dos_date d;
    struct dos_time t;
    u32 local = clock_local();

    (void)ctx;
    if (local < 315532800u)
        return 0;
    dos_from_seconds(local - 315532800u, &d, &t);
    return (unsigned long)dos_pack_date(&d) << 16 | dos_pack_time(&t);
}

static int probed;

static int probe(void)
{
    struct io x;

    if (probed)
        return 0;
    memset(&x, 0, sizeof x);
    x.op = 2;
    on_vm(&x);
    probed = 1;
    return 0;
}

/* GLOS's IDE disk n (an ATA one), or 0. */
static struct ata_dev *disk(u32 n)
{
    struct ata_dev *a = ide_dev((int)n);

    return a && a->present && !a->atapi ? a : 0;
}

/* The native partition on disk a (its start and size), or -1; -2: the disk has other partitions. */
static int native_part(struct ata_dev *a, unsigned char *s, struct mbr_part *found)
{
    struct mbr_part p[4];
    int n, i, k = -1;

    if (disk_io(a, 0, 0, 1, s) != 0)
        return -3;
    n = mbr_parse(s, p);
    if (n <= 0)
        return -1;
    for (i = 0; i < 4; i++) {
        if (!p[i].type)
            continue;
        if (!is_native(p[i].type) || k >= 0)
            return -2;
        k = i;
    }
    if (k < 0)
        return -1;
    *found = p[k];
    return 0;
}

static int mount(struct ndcmd *c)
{
    static unsigned char s[512];
    struct mbr_part p;
    struct ata_dev *a;
    int i, r;

    if (vmounted)
        return 0;
    for (i = 0; (a = ide_dev(i)) != 0; i++) {
        if (!a->present || a->atapi || native_part(a, s, &p) != 0)
            continue;
        vdev = a;
        vbase = p.start;
        r = fsv_mount(&vol, vrd, vwr, vnow, 0, 437, 0);
        if (r) {
            say(c, "NDOS: disk %d's native volume can't be read (%d)\n", i, r);
            return -1;
        }
        vmounted = 1;
        return 0;
    }
    say(c, "NDOS: no native volume (NDOS FORMAT n makes one)\n");
    return -1;
}

/* ---- the commands */

static int is_blank(const unsigned char *s)
{
    u32 i;

    for (i = 0; i < 512; i++)
        if (s[i])
            return 0;
    return 1;
}

static int put_vol(void *ctx, unsigned long lba, const unsigned char *buf, unsigned count)
{
    struct mbr_part *p = ctx;

    return disk_io(vdev, 1, p->start + lba, count, (void *)buf) != 0;
}

static int cmd_format(struct ndcmd *c, u32 n)
{
    static unsigned char s[512];
    struct mbr_part p;
    struct fat_geo g;
    struct fat_made made;
    struct ata_dev *a = disk(n);
    int r;

    if (!a) {
        say(c, "NDOS FORMAT: no IDE disk %u (NDOS DISKS lists them)\n", n);
        return 1;
    }
    r = native_part(a, s, &p);
    if (r == -3 || (r != 0 && !is_blank(s))) {
        say(c, "NDOS FORMAT: disk %u is in use (it has partitions, or data in its first sector): only a blank "
               "disk, or a native volume, is made one\n", n);
        return 1;
    }
    memset(s, 0, sizeof s);
    mbr_boot(s);
    if (mbr_one(s, a->sectors_lo, 16, 63, &p) != 0) {
        say(c, "NDOS FORMAT: disk %u is too small\n", n);
        return 1;
    }
    s[446] = 0;                                 /* (not the active partition: no PC starts from it) */
    s[446 + 4] = (unsigned char)(p.type | 0x10);   /* (its hidden kind: DOS gives it no letter) */
    vdev = a;
    vmounted = 0;
    if (disk_io(a, 1, 0, 1, s) != 0) {
        say(c, "NDOS FORMAT: disk %u's first sector can't be written\n", n);
        return 1;
    }
    memset(&g, 0, sizeof g);
    g.sectors = p.sectors, g.hidden = p.start, g.heads = 16, g.spt = 63, g.drive = 0x80;
    g.serial = clock_utc() ^ timer_ticks() << 12;
    g.label = "NDOS";
    if (fat_format(&g, put_vol, &p, &made) != 0) {
        say(c, "NDOS FORMAT: the volume can't be written\n");
        return 1;
    }
    say(c, "NDOS FORMAT: disk %u: a native volume, FAT%d, %u MB, at sector %u\n", n, made.fat, (u32)(p.sectors / 2048),
        (u32)p.start);
    return 0;
}

static int cmd_disks(struct ndcmd *c)
{
    static unsigned char s[512];
    struct mbr_part p;
    struct ata_dev *a;
    int i;

    for (i = 0; (a = ide_dev(i)) != 0; i++) {
        if (!a->present)
            continue;
        say(c, "%d %s %u MB \"%s\" %s\n", i, a->atapi ? "atapi" : "ata", (u32)(a->sectors_lo / 2048), a->model,
            a->atapi ? "" : native_part(a, s, &p) == 0 ? "native" : is_blank(s) ? "blank" : "in use");
    }
    return 0;
}

/* "N:\..." made the volume's own path ("\..."): 0 if it isn't one. */
static const char *native_path(const char *p)
{
    if ((p[0] == 'N' || p[0] == 'n') && p[1] == ':')
        return p[2] ? p + 2 : "\\";
    return 0;
}

static int cmd_dir(struct ndcmd *c, const char *arg)
{
    const char *path = *arg ? native_path(arg) : "\\";
    struct fat_dir d;
    struct fat_ent e;
    int r;

    if (!path) {
        say(c, "NDOS DIR: N:\\path (the native volume's)\n");
        return 1;
    }
    if (mount(c) != 0)
        return 1;
    if ((r = fsv_opendir(&vol, path, &d)) != 0) {
        say(c, "NDOS DIR: %s: not a folder (%d)\n", arg, r);
        return 1;
    }
    while ((r = fsv_readdir(&vol, &d, &e)) > 0) {
        char name[16];
        u32 k = 0;

        for (; e.sname[k] && k < 12; k++)       /* (kprintf has no '-' flag: padded here) */
            name[k] = e.sname[k];
        for (; k < 12; k++)
            name[k] = ' ';
        name[12] = 0;
        say(c, "%s %10u%s %s\n", name, (u32)e.size, (e.attr & FAT_A_DIR) ? " <DIR>" : "", e.has_long ? e.name : "");
    }
    return r < 0;
}

/* A DOS drive's file, whole (DOS_MAX of it at most), through the DOS server: its bytes, or -1. */
static long hosted_read(const char *path, u8 *buf, u32 max)
{
    struct dos_req q;
    u32 room = dos_xfer_size(), got = 0, plen = (u32)strlen(path) + 1;
    u16 h;

    memset(&q, 0, sizeof q);
    q.n = 1;
    q.r[0].ax = 0x3D00;
    q.r[0].ds = DOS_XFER;
    q.in = (const u8 *)path, q.in_len = plen, q.in_at = 0;
    if (dos_call(&q) != 0 || q.error)
        return -1;
    h = q.r[0].ax;
    for (;;) {
        u32 want = max - got < room ? max - got : room;

        memset(&q, 0, sizeof q);
        q.n = 1;
        q.r[0].ax = 0x3F00, q.r[0].bx = h, q.r[0].cx = (u16)want, q.r[0].ds = DOS_XFER;
        q.out = buf + got, q.out_len = want, q.out_at = 0;
        if (!want || dos_call(&q) != 0 || q.error)
            break;
        got += q.r[0].ax;
        if (q.r[0].ax < want)
            break;
    }
    memset(&q, 0, sizeof q);
    q.n = 1;
    q.r[0].ax = 0x3E00, q.r[0].bx = h;
    dos_call(&q);
    return (long)got;
}

static int cmd_copy(struct ndcmd *c, const char *arg)
{
    char from[128], *to;
    const char *dst;
    struct fsv_file f;
    u8 *buf;
    long n;
    int r;

    strncpy(from, arg, sizeof from - 1);
    from[sizeof from - 1] = 0;
    to = strchr(from, ' ');
    if (!to || !(dst = native_path(to + 1))) {
        say(c, "NDOS COPY: d:\\path N:\\path (a DOS drive's file onto the native volume)\n");
        return 1;
    }
    *to = 0;
    if (mount(c) != 0)
        return 1;
    if (!(buf = kmalloc(256 * 1024))) {
        say(c, "NDOS COPY: no memory\n");
        return 1;
    }
    n = hosted_read(from, buf, 256 * 1024);
    if (n < 0) {
        kfree(buf);
        say(c, "NDOS COPY: %s can't be read\n", from);
        return 1;
    }
    r = fsv_create(&vol, dst, 0, 1, FSV_RW, FSV_DENY_NONE, &f);
    if (!r && fsv_write(&f, buf, (unsigned long)n) != n)
        r = -1;
    if (!r)
        fsv_close(&f);
    fsv_flush(&vol);
    kfree(buf);
    if (r) {
        say(c, "NDOS COPY: %s can't be written (%d)\n", dst, r);
        return 1;
    }
    say(c, "NDOS COPY: %u bytes\n", (u32)n);
    return 0;
}

static struct ndcmd *running_cmd;

static void relay(void)
{
    struct ndcmd *c = running_cmd;

    if (c && c->kick)
        c->kick();
}

static int cmd_run(struct ndcmd *c, const char *arg)
{
    char name[128], dospath[132], *tail;
    const char *path;
    struct ndm *m;
    int code = 255, e;

    strncpy(name, arg, sizeof name - 1);
    name[sizeof name - 1] = 0;
    tail = strchr(name, ' ');
    if (tail) {
        static char t[130];                     /* (DOS's tail keeps the space before it: " abc") */

        *tail++ = 0;
        t[0] = ' ';
        strncpy(t + 1, tail, sizeof t - 2);
        t[sizeof t - 1] = 0;
        tail = t;
    }
    running_cmd = c;
    if (!strcmp(name, "HELLO") || !strcmp(name, "hello")) {
        m = ndm_start(vmounted ? &vol : 0, 0, ndm_hello, ndm_hello_len, "HELLO", tail ? tail : "", relay, &e);
    } else {
        const char *b;

        if (!(path = native_path(name))) {
            say(c, "NDOS RUN: N:\\path [tail], or HELLO\n");
            return 1;
        }
        if (mount(c) != 0)
            return 1;
        dospath[0] = 'C', dospath[1] = ':';     /* (the native volume is the machine's C:) */
        strncpy(dospath + 2, path, sizeof dospath - 3);
        dospath[sizeof dospath - 1] = 0;
        b = strrchr(path, '\\');
        m = ndm_start(&vol, dospath, 0, 0, b ? b + 1 : path, tail ? tail : "", relay, &e);
    }
    if (!m) {
        say(c, e == 0x02 || e == 0x03 ? "NDOS RUN: %s: no such program\n"
               : e == 0x0B ? "NDOS RUN: %s: not a program\n" : "NDOS RUN: %s: DOS error %02x\n", name, e);
        return 1;
    }
    for (;;) {                                  /* what it writes, as it writes it; then its end */
        char b[256];
        int done = ndm_done(m, &code);
        u32 k, i, fl;

        while ((k = ndm_read(m, b, sizeof b)) != 0) {
            fl = irq_save();
            for (i = 0; i < k; i++)
                if (c->head - c->tail < OUT_RING)
                    c->out[c->head++ % OUT_RING] = b[i];
            irq_restore(fl);
            if (c->kick)
                c->kick();
        }
        if (done)
            break;
        thread_sleep_until(timer_ticks() + 8);
    }
    ndm_free(m);
    running_cmd = 0;
    return code;
}

static int word(const char **p, const char *w)
{
    const char *s = *p;

    while (*s == ' ')
        s++;
    for (; *w; s++, w++)
        if ((*s >= 'a' && *s <= 'z' ? *s - 32 : *s) != *w)
            return 0;
    if (*s && *s != ' ')
        return 0;
    while (*s == ' ')
        s++;
    *p = s;
    return 1;
}

static int run(struct ndcmd *c)
{
    const char *p = c->args;

    if (!cache_trapping()) {
        say(c, "NDOS: the disk cache is off ([cache] enabled): GLOS's disk work can't be kept from the BIOS's\n");
        return 1;
    }
    probe();
    if (word(&p, "FORMAT"))
        return cmd_format(c, (u32)(*p >= '0' && *p <= '9' ? *p - '0' : 99));
    if (word(&p, "DISKS"))
        return cmd_disks(c);
    if (word(&p, "DIR"))
        return cmd_dir(c, p);
    if (word(&p, "COPY"))
        return cmd_copy(c, p);
    if (word(&p, "RUN"))
        return cmd_run(c, p);
    say(c, "NDOS DISKS | FORMAT n | DIR [N:\\path] | COPY d:\\path N:\\path | RUN N:\\path [tail] | RUN HELLO\n");
    return 2;
}

static void worker(void *arg)
{
    struct ndcmd *c = arg;
    void (*kick)(void);
    u32 f;
    int gone;

    c->code = run(c);
    f = irq_save();
    busy = 0;
    gone = c->gone, kick = c->kick;
    c->done = 1;
    irq_restore(f);
    if (gone)
        kfree(c);
    else if (kick)
        kick();
    thread_exit();
}

struct ndcmd *ndcmd_start(const char *args, void (*kick)(void))
{
    struct ndcmd *c;
    u32 f = irq_save();

    if (busy || strlen(args) >= sizeof c->args) {
        irq_restore(f);
        return 0;
    }
    busy = 1;
    irq_restore(f);
    if (!(c = kmalloc(sizeof *c))) {
        busy = 0;
        return 0;
    }
    memset(c, 0, sizeof *c);
    strcpy(c->args, args);
    c->kick = kick;
    if (!thread_create("ndos-cmd", PRIO_NORMAL, worker, c)) {
        kfree(c);
        busy = 0;
        return 0;
    }
    return c;
}

u32 ndcmd_read(struct ndcmd *c, char *buf, u32 room)
{
    u32 n = 0, f = irq_save();

    while (n < room && c->tail != c->head)
        buf[n++] = c->out[c->tail++ % OUT_RING];
    irq_restore(f);
    return n;
}

int ndcmd_done(const struct ndcmd *c, int *code)
{
    if (!c->done)
        return 0;
    *code = c->code;
    return 1;
}

void ndcmd_free(struct ndcmd *c)
{
    u32 f = irq_save();

    if (c->done) {
        irq_restore(f);
        kfree(c);
        return;
    }
    c->gone = 1;
    c->kick = 0;
    irq_restore(f);
}
