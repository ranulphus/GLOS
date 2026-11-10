/* API 2.0's FS (include/glos/api2.h; docs/v1/phase-b.md step 7): a folder's entries in batches, several files'
 * details at once, the drives, watches of folders and files, a disk's sectors.
 *
 * Each call is one batch (app.c's app_batch): the VM thread makes all of the call's DOS and BIOS calls in one
 * turn, in the app's own DOS context (its PSP, its drive and directory), where the app making them itself would
 * cost a turn each. A batch borrows a block of DOS's memory for what DOS reads and writes (a path, the find
 * data, sectors), and points DOS's DTA at it for as long; both are as they were when it is done.
 *
 * Names: DOS has them in its code page, GLOS's are UTF-8; glos/cp.h's tables are between. The page is REGION's
 * ([region] codepage), or what DOS says (INT 21h 6601h, asked once).
 *
 * Long names: where INT 21h 71A0h answers for a drive (a driver such as DOSLFN is loaded), LIST and INFO use
 * the long-name find calls (714Eh, 714Fh, 71A1h) and give both names; otherwise the 8.3 ones (4Eh, 4Fh). GLOS
 * does not answer the long-name calls itself. */
#include <string.h>

#include <glos/cp.h>
#include "api2int.h"
#include "dpmi.h"
#include "fslist.h"
#include "glos/bootinfo.h"
#include "glos/region.h"
#include "io.h"
#include "kcfg.h"
#include "kprintf.h"
#include "mm.h"
#include "vm.h"
#include "watch.h"

#define OUT_MAX   16384u                /* a batch's entries at most (the kernel's copy of them) */
#define LIST_MAX  512u                  /* ... and how many */
#define INFO_MAX  64u
#define PATH_MAX_ 260u                  /* a path's bytes, with its NUL */
#define SECT_MAX  32u

/* The borrowed block: the find data (or a drive's information) at 0, a path at 180h. */
#define LOW_PARAS 0x40u
#define LOW_PATH  0x180u

static int dos_page;                    /* the code page DOS says it has (0: not asked yet) */

static int page(void)
{
    int p = kcfg_region()->codepage;
    return p ? p : dos_page ? dos_page : 437;
}

/* ---- in a batch (the VM thread) */

static int dos(struct rmregs *r)        /* an INT 21h: 0 done, 1 DOS refused (its error in AX), -1 the app is gone */
{
    if (app_batch_int(0x21, r) != 0)
        return -1;
    return r->flags & FL_CF ? 1 : 0;
}

static u32 low_get(u32 paras)           /* a block of DOS's memory: its segment, 0 if none */
{
    struct rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x4800;
    r.ebx = paras;
    return dos(&r) == 0 ? r.eax & 0xFFFF : 0;
}

static void low_put(u32 seg)
{
    struct rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x4900;
    r.es = (u16)seg;
    dos(&r);
}

static u32 dta_get(void)                /* DOS's DTA now, seg:off in one */
{
    struct rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x2F00;
    return dos(&r) == 0 ? (u32)r.es << 16 | (r.ebx & 0xFFFF) : 0;
}

static void dta_set(u32 far)
{
    struct rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x1A00;
    r.ds = (u16)(far >> 16);
    r.edx = far & 0xFFFF;
    dos(&r);
}

static void low_str(u32 seg, u32 off, const char *s)
{
    u32 lin = (seg << 4) + off;
    do
        vm_wr8(lin++, (u8)*s);
    while (*s++);
}

static void ask_page(void)              /* DOS's code page, the first time anything needs it */
{
    struct rmregs r;
    if (dos_page)
        return;
    memset(&r, 0, sizeof r);
    r.eax = 0x6601;
    dos_page = dos(&r) == 0 && cp_table((int)(r.ebx & 0xFFFF)) ? (int)(r.ebx & 0xFFFF) : 437;
}

/* Does DOS have long names for the drive of this path (its bytes at seg:LOW_PATH)? 71A0h on its root. */
static int long_names(u32 seg, const char *path)
{
    struct rmregs r;
    char root[4] = "C:\\";
    if (path[0] && path[1] == ':') {
        root[0] = path[0];
    } else {                            /* (no drive given: the current one) */
        memset(&r, 0, sizeof r);
        r.eax = 0x1900;
        if (dos(&r) != 0)
            return 0;
        root[0] = (char)('A' + (r.eax & 0xFF));
    }
    low_str(seg, 0x100, root);
    memset(&r, 0, sizeof r);
    r.eax = 0x71A0;
    r.ds = (u16)seg;
    r.edx = 0x100;
    r.es = (u16)seg;
    r.edi = 0x110;
    r.ecx = 32;
    r.flags = FL_CF;
    return dos(&r) == 0 && (r.eax & 0xFFFF) != 0x7100 && (r.ebx & 0x4000);
}

/* A find: first (path at seg:LOW_PATH) or next; long (handle in *find) or 8.3 (the DTA at seg:0). 0 found (the
   data at seg:0), 1 no more or no such, -1 the app is gone. */
static u32 find_error;                  /* DOS's error from the last find that found nothing */

static int find(u32 seg, int lfn, int first, u32 *handle, u32 attrs)
{
    struct rmregs r;
    int e;
    memset(&r, 0, sizeof r);
    if (lfn) {
        r.eax = first ? 0x714E : 0x714F;
        r.ebx = first ? 0 : *handle;
        r.ecx = attrs;                  /* (CL: those allowed; CH: none required) */
        r.esi = 1;                      /* dates as DOS's */
        r.ds = (u16)seg;
        r.edx = LOW_PATH;
        r.es = (u16)seg;
        r.edi = 0;
        r.flags = FL_CF;
        if ((e = dos(&r)) == 0 && first)
            *handle = r.eax & 0xFFFF;
        if (e > 0)
            find_error = r.eax & 0xFFFF;
        return e;
    }
    r.eax = first ? 0x4E00 : 0x4F00;
    r.ecx = attrs;
    r.ds = (u16)seg;
    r.edx = LOW_PATH;
    if ((e = dos(&r)) > 0)
        find_error = r.eax & 0xFFFF;
    return e;
}

static void find_close(u32 handle)
{
    struct rmregs r;
    memset(&r, 0, sizeof r);
    r.eax = 0x71A1;
    r.ebx = handle;
    r.flags = FL_CF;
    dos(&r);
}

/* What was found. shortn: its 8.3 name is wanted (GLOS_FS_SHORT), where it has a long one too. */
static void found(u32 seg, int lfn, int shortn, struct fs_found *e)
{
    if (lfn)
        fs_from_lfn(vm_ptr(seg << 4), e);
    else
        fs_from_dta(vm_ptr(seg << 4), e);
    if (shortn && e->short_name[0]) {
        memcpy(e->name, e->short_name, sizeof e->short_name);
        e->short_name[0] = 0;
    }
}

/* ---- LIST */

struct listing {                        /* a folder being listed: a handle's object */
    u8 dta[FS_DTA];                     /* DOS's place in the folder (8.3 finds) */
    u32 find;                           /* ... or the long-name find's handle */
    u8 lfn, begun, held, done;          /* held: `hold` was found and not yet given (the batch had no room) */
    struct fs_found hold;
    char pattern[PATH_MAX_ + 8];        /* the folder and "\*.*", in DOS's code page */
};

struct list_job {
    struct listing *l;
    u8 *out;
    u32 room, used, count;
    int shortn, error;
};

static void list_batch(struct app *a, void *arg)
{
    struct list_job *j = arg;
    struct listing *l = j->l;
    u32 seg, old;
    int e = 0;
    (void)a;
    ask_page();
    if (!(seg = low_get(LOW_PARAS))) {
        j->error = GLOS_E_MEMORY;
        return;
    }
    old = dta_get();
    dta_set(seg << 16);
    if (!l->begun) {
        low_str(seg, LOW_PATH, l->pattern);
        l->lfn = long_names(seg, l->pattern);    /* (the folder's own name may be a long one, whatever is asked) */
        e = find(seg, l->lfn, 1, &l->find, 0x37);
        l->begun = 1;
        if (e == 0) {
            found(seg, l->lfn, j->shortn, &l->hold);
            l->held = 1;
        } else if (e > 0 && find_error == 3) {
            j->error = GLOS_E_ARGUMENT; /* (no such folder; an empty one is DOS's "no more files") */
        }
    } else if (!l->lfn) {
        memcpy(vm_ptr(seg << 4), l->dta, FS_DTA);       /* DOS's place, as the last batch left it */
    }
    while (e == 0 && j->count < LIST_MAX) {
        if (!l->held) {
            if ((e = find(seg, l->lfn, 0, &l->find, 0x37)) != 0)
                break;
            found(seg, l->lfn, j->shortn, &l->hold);
        }
        l->held = 0;
        if (fs_dots(&l->hold))
            continue;
        if (!fs_pack(j->out, j->room, &j->used, &l->hold, page())) {
            l->held = 1;                /* (the next batch's first) */
            break;
        }
        j->count++;
    }
    if (e > 0) {                        /* the folder's end (or no such folder: nothing in it) */
        l->done = 1;
        if (l->lfn && l->find)
            find_close(l->find);
    } else if (e == 0 && !l->lfn) {
        memcpy(l->dta, vm_ptr(seg << 4), FS_DTA);
    }
    if (e >= 0) {
        dta_set(old);
        low_put(seg);
    }
}

/* A path of the app's (UTF-8) as DOS's bytes: its length, or -1. */
static int path_in(struct trapframe *tf, u32 off, char *out, u32 room)
{
    char utf[PATH_MAX_ * 3];
    u32 i;
    if (!off)
        return -1;
    for (i = 0; i < sizeof utf; i++) {
        if (user_rd((u16)tf->ds, off + i, &utf[i], 1) != 0)
            return -1;
        if (!utf[i])
            break;
    }
    if (i == sizeof utf || !i || cp_from_utf8(page(), utf, out, (int)room) < 0)
        return -1;
    for (i = 0; out[i]; i++)
        if (out[i] == '/')
            out[i] = '\\';
    return (int)i;
}

static int list(struct app *a, struct trapframe *tf)
{
    struct glos_fs_list q;
    struct list_job j;
    struct listing *l;
    u32 size;
    int n, made = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.room < 1024)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.handle) {
        if (!(l = handle_obj(&a->v2->h, q.handle, GLOS_HK_LIST)))
            return a2_fail(tf, handle_kind(&a->v2->h, q.handle) ? GLOS_E_KIND : GLOS_E_HANDLE);
    } else {
        if (!(l = kmalloc(sizeof *l)))
            return a2_fail(tf, GLOS_E_MEMORY);
        memset(l, 0, sizeof *l);
        if ((n = path_in(tf, q.path, l->pattern, PATH_MAX_)) < 0) {
            kfree(l);
            return a2_fail(tf, GLOS_E_ARGUMENT);
        }
        memcpy(l->pattern + n, l->pattern[n - 1] == '\\' ? "*.*" : "\\*.*", 5);
        made = 1;
    }
    memset(&j, 0, sizeof j);
    j.l = l;
    j.room = q.room < OUT_MAX ? q.room : OUT_MAX;
    j.shortn = (q.flags & GLOS_FS_SHORT) != 0;
    if (!(j.out = kmalloc(j.room)) || app_batch(a, list_batch, &j, 0) != 0 || j.error) {
        if (j.out)
            kfree(j.out);
        if (made)
            kfree(l);
        return a2_fail(tf, j.error ? (u32)j.error : GLOS_E_MEMORY);
    }
    q.count = j.count;
    q.length = j.used;
    q.long_names = l->lfn && !j.shortn;
    if (l->done) {                      /* the folder's last batch: nothing is left to close */
        if (q.handle)
            handle_close(&a->v2->h, q.handle);
        kfree(l);
        q.handle = 0;
    } else if (made && !(q.handle = handle_new(&a->v2->h, GLOS_HK_LIST, l))) {
        kfree(l);                       /* (a long-name find stays open in the driver: no handle to go on by) */
        kfree(j.out);
        return a2_fail(tf, GLOS_E_MEMORY);
    }
    n = (j.used && user_wr((u16)tf->ds, q.buffer, j.out, j.used) != 0) || a2_wr(tf, &q, sizeof q, size) != 0;
    kfree(j.out);
    if (n)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return !j.count && q.handle ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

/* A listing's handle closed before its folder's end: a long-name find is closed with it. */
struct close_job {
    u32 find;
};

static void close_batch(struct app *a, void *arg)
{
    (void)a;
    find_close(((struct close_job *)arg)->find);
}

/* ---- INFO */

struct info_job {
    const char *paths;                  /* each in DOS's code page, a NUL after it */
    u32 count;
    u8 *out;
    u32 room, used, given;
    int shortn, error;
};

static void info_batch(struct app *a, void *arg)
{
    struct info_job *j = arg;
    const char *p = j->paths;
    u32 seg, old, i, handle = 0;
    (void)a;
    ask_page();
    if (!(seg = low_get(LOW_PARAS))) {
        j->error = GLOS_E_MEMORY;
        return;
    }
    old = dta_get();
    dta_set(seg << 16);
    for (i = 0; i < j->count; i++, p += strlen(p) + 1) {
        struct fs_found f;
        u32 n = (u32)strlen(p);
        int lfn, e;
        memset(&f, 0, sizeof f);
        low_str(seg, LOW_PATH, p);
        lfn = long_names(seg, p);
        if (n <= 3 && n >= 2 && p[1] == ':') {          /* a drive's root is no entry of anything: a folder */
            struct rmregs r;
            memset(&r, 0, sizeof r);
            r.eax = 0x4409;
            r.ebx = (u32)((p[0] | 0x20) - 'a' + 1);
            e = dos(&r);
            f.attr = GLOS_FA_FOLDER;
            f.size_lo = r.eax & 0xFFFF;
            memcpy(f.name, p, n + 1);
        } else {
            if ((e = find(seg, lfn, 1, &handle, 0x37)) == 0) {
                found(seg, lfn, j->shortn, &f);
                if (lfn)
                    find_close(handle);
            } else {
                f.size_lo = find_error;                 /* (which error) */
            }
        }
        if (e < 0)
            return;                                     /* (the app is gone: nothing to put back) */
        if (e > 0) {
            f.attr = GLOS_FA_NONE;
            f.name[0] = 0;
        }
        if (!fs_pack(j->out, j->room, &j->used, &f, page()))
            break;
        j->given++;
    }
    dta_set(old);
    low_put(seg);
}

static int info(struct app *a, struct trapframe *tf)
{
    char *paths;                        /* (the app's own: another app may be in this call while the batch waits) */
    u32 paths_room = INFO_MAX * 80;
    struct glos_fs_info q;
    struct info_job j;
    u32 size, i, at = 0, off;
    int n, bad;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || !q.count || q.count > INFO_MAX || !q.paths)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (!(paths = kmalloc(paths_room)))
        return a2_fail(tf, GLOS_E_MEMORY);
    for (i = 0, off = q.paths; i < q.count; i++) {
        char one[PATH_MAX_];
        u32 len = 0;
        char c = 0;
        if ((n = path_in(tf, off, one, sizeof one)) < 0 || at + (u32)n + 1 > paths_room)
            break;
        memcpy(paths + at, one, (u32)n + 1);
        at += (u32)n + 1;
        do {                            /* (past it in the app's memory: its UTF-8 length may differ) */
            if (user_rd((u16)tf->ds, off + len, &c, 1) != 0)
                break;
            len++;
        } while (c);
        if (c)
            break;
        off += len;
    }
    if (i < q.count) {
        kfree(paths);
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    memset(&j, 0, sizeof j);
    j.paths = paths;
    j.count = q.count;
    j.room = q.room < OUT_MAX ? q.room : OUT_MAX;
    j.shortn = (q.flags & GLOS_FS_SHORT) != 0;
    if (!(j.out = kmalloc(j.room ? j.room : 1)) || app_batch(a, info_batch, &j, 0) != 0 || j.error) {
        if (j.out)
            kfree(j.out);
        kfree(paths);
        return a2_fail(tf, j.error ? (u32)j.error : GLOS_E_MEMORY);
    }
    kfree(paths);
    q.length = j.used;
    q.given = j.given;
    bad = (j.used && user_wr((u16)tf->ds, q.buffer, j.out, j.used) != 0) || a2_wr(tf, &q, sizeof q, size) != 0;
    kfree(j.out);
    if (bad)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return j.given < q.count ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

/* ---- for SHELL's OPEN and HELP (api2shell.c) */

int api2_fs_path(struct trapframe *tf, u32 off, char *out, u32 room) { return path_in(tf, off, out, room); }

struct exists_job {
    const char *path;                   /* in DOS's code page */
    int what;
};

static void exists_batch(struct app *a, void *arg)
{
    struct exists_job *j = arg;
    struct fs_found f;
    u32 seg, old, handle = 0, n = (u32)strlen(j->path);
    int lfn;
    (void)a;
    ask_page();
    if (n >= 2 && n <= 3 && j->path[1] == ':') {        /* (a drive's root: a folder) */
        j->what = 2;
        return;
    }
    if (!(seg = low_get(LOW_PARAS)))
        return;
    old = dta_get();
    dta_set(seg << 16);
    low_str(seg, LOW_PATH, j->path);
    lfn = long_names(seg, j->path);
    memset(&f, 0, sizeof f);
    if (find(seg, lfn, 1, &handle, 0x37) == 0) {
        found(seg, lfn, 0, &f);
        if (lfn)
            find_close(handle);
        j->what = f.attr & 0x10 ? 2 : 1;
    }
    dta_set(old);
    low_put(seg);
}

/* Is there such a file (1) or folder (2)? 0: no (or its name has wildcards); -1: the app is ending. */
int api2_fs_exists(struct app *a, const char *path)
{
    struct exists_job j;
    u32 i;
    for (i = 0; path[i]; i++)
        if (path[i] == '*' || path[i] == '?')
            return 0;
    if (!i)
        return 0;
    j.path = path;
    j.what = 0;
    return app_batch(a, exists_batch, &j, 0) != 0 ? -1 : j.what;
}

/* ---- DRIVES */

struct drives_job {
    u32 one;                            /* a letter, or 0: all of them */
    struct glos_fs_drive d[26];
    u32 n;
    int error;
};

static void details(u32 seg, struct glos_fs_drive *d)
{
    static const char *const ram[] = { "MS-RAMDRIVE", "RAMDISK", "RDV", "XMSDSK", "VDISK", "SRDISK", 0 };
    struct rmregs r;
    char root[4] = "A:\\";
    const u8 *p = vm_ptr(seg << 4);
    unsigned long long kb;
    u32 i;
    root[0] = (char)d->letter;
    memset(&r, 0, sizeof r);
    r.eax = 0x6900;                     /* the serial number, the label and the file system's name */
    r.ebx = d->letter - 'A' + 1;
    r.ds = (u16)seg;
    r.edx = 0;
    if (dos(&r) != 0)
        return;                         /* (no disk in it, or no such thing: no details) */
    d->serial = p[2] | (u32)p[3] << 8 | (u32)p[4] << 16 | (u32)p[5] << 24;
    for (i = 0; i < 11 && p[6 + i]; i++)
        d->label[i] = (char)(p[6 + i] < 0x80 ? p[6 + i] : '?');
    while (i && d->label[i - 1] == ' ')
        i--;
    d->label[i] = 0;
    if (!strcmp(d->label, "NO NAME"))
        d->label[0] = 0;
    for (i = 0; i < 8 && p[0x11 + i] && p[0x11 + i] != ' '; i++)
        d->system[i] = (char)p[0x11 + i];
    d->system[i] = 0;
    low_str(seg, LOW_PATH, root);
    memset(&r, 0, sizeof r);
    r.eax = 0x7303;                     /* sizes past 2 GB (FAT32's call), where DOS has it */
    r.ds = (u16)seg;
    r.edx = LOW_PATH;
    r.es = (u16)seg;
    r.edi = 0x40;
    r.ecx = 0x30;
    r.flags = FL_CF;
    memset(vm_ptr((seg << 4) + 0x40), 0, 0x30);
    if (dos(&r) == 0 && (r.eax & 0xFFFF) != 0x7300 && vm_rd32((seg << 4) + 0x44)) {
        u32 spc = vm_rd32((seg << 4) + 0x44), bps = vm_rd32((seg << 4) + 0x48);
        kb = (unsigned long long)vm_rd32((seg << 4) + 0x50) * spc * bps >> 10;
        d->total_kb = kb > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)kb;
        kb = (unsigned long long)vm_rd32((seg << 4) + 0x4C) * spc * bps >> 10;
        d->free_kb = kb > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32)kb;
    } else {
        memset(&r, 0, sizeof r);
        r.eax = 0x3600;
        r.edx = d->letter - 'A' + 1;
        if (dos(&r) != 0 || (r.eax & 0xFFFF) == 0xFFFF)
            return;
        kb = (unsigned long long)(r.edx & 0xFFFF) * (r.eax & 0xFFFF) * (r.ecx & 0xFFFF) >> 10;
        d->total_kb = (u32)kb;
        kb = (unsigned long long)(r.ebx & 0xFFFF) * (r.eax & 0xFFFF) * (r.ecx & 0xFFFF) >> 10;
        d->free_kb = (u32)kb;
    }
    d->flags |= GLOS_DRIVE_DETAILS;
    if (long_names(seg, root))
        d->flags |= GLOS_DRIVE_LONG;
    for (i = 0; ram[i] && d->kind == GLOS_DRIVE_HARD; i++)
        if (!strncmp(d->label, ram[i], strlen(ram[i])))
            d->kind = GLOS_DRIVE_RAM;
}

static void drives_batch(struct app *a, void *arg)
{
    struct drives_job *j = arg;
    u32 seg, equip = vm_rd16(0x410), floppies = equip & 1 ? ((equip >> 6) & 3) + 1 : 0, l;
    (void)a;
    if (!(seg = low_get(LOW_PARAS))) {
        j->error = GLOS_E_MEMORY;
        return;
    }
    for (l = 0; l < 26; l++) {
        struct glos_fs_drive *d = &j->d[j->n];
        struct rmregs r;
        if (j->one && j->one != 'A' + l)
            continue;
        memset(d, 0, sizeof *d);
        d->letter = 'A' + l;
        if (l < 2) {
            if (l >= floppies)
                continue;
            d->kind = GLOS_DRIVE_FLOPPY;
            d->flags = GLOS_DRIVE_REMOVABLE;
        } else {
            memset(&r, 0, sizeof r);
            r.eax = 0x4409;             /* is it there, and is it remote? */
            r.ebx = l + 1;
            if (dos(&r) != 0)
                continue;
            if (r.edx & 0x1000) {
                struct rmregs m;
                memset(&m, 0, sizeof m);
                m.eax = 0x150B;         /* MSCDEX: is it one of its drives? */
                m.ecx = l;
                d->kind = app_batch_int(0x2F, &m) == 0 && (m.ebx & 0xFFFF) == 0xADAD && (m.eax & 0xFFFF)
                          ? GLOS_DRIVE_CD : GLOS_DRIVE_NET;
                if (d->kind == GLOS_DRIVE_CD)
                    d->flags = GLOS_DRIVE_REMOVABLE;
            } else {
                d->kind = GLOS_DRIVE_HARD;
                memset(&r, 0, sizeof r);
                r.eax = 0x4408;         /* can its disk be taken out? */
                r.ebx = l + 1;
                if (dos(&r) == 0 && (r.eax & 0xFFFF) == 0)
                    d->flags = GLOS_DRIVE_REMOVABLE;
            }
        }
        if (j->one || (d->kind == GLOS_DRIVE_HARD && !(d->flags & GLOS_DRIVE_REMOVABLE)))
            details(seg, d);
        j->n++;
    }
    low_put(seg);
}

static int drives(struct app *a, struct trapframe *tf)
{
    static struct drives_job j;         /* (under the app's lock) */
    struct glos_fs_drives q;
    u32 size, i, give;
    if (a2_rd(tf, &q, sizeof q, &size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.drive >= 'a' && q.drive <= 'z')
        q.drive -= 32;
    if (q.drive && (q.drive < 'A' || q.drive > 'Z'))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    memset(&j, 0, sizeof j);
    j.one = q.drive;
    if (app_batch(a, drives_batch, &j, 0) != 0 || j.error)
        return a2_fail(tf, j.error ? (u32)j.error : GLOS_E_MEMORY);
    q.count = j.n;
    give = q.room < j.n ? q.room : j.n;
    for (i = 0; i < give; i++)
        if (user_wr((u16)tf->ds, q.buffer + i * sizeof j.d[0], &j.d[i], sizeof j.d[0]) != 0)
            return a2_fail(tf, GLOS_E_ARGUMENT);
    if (a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return q.room < j.n ? a2_fail(tf, GLOS_E_ROOM) : a2_ok(tf, 0);
}

/* ---- WATCH */

struct watch_job {
    char path[PATH_MAX_];               /* as the app gave it, in DOS's code page; -> by its 8.3 names */
    int folder, error;
};

/* Is it a folder, or a file (there, or yet to be made)? And its path by 8.3 names: the watches see DOS's calls
   by the paths programs give them, which are 8.3 ones unless made through the long-name calls (those mark
   every watch on the drive: kernel/dos/watch.c). */
static void watch_batch(struct app *a, void *arg)
{
    struct watch_job *j = arg;
    struct fs_found f;
    struct rmregs r;
    u32 seg, old, handle = 0, n = (u32)strlen(j->path);
    int lfn, e;
    (void)a;
    if (!(seg = low_get(LOW_PARAS))) {
        j->error = GLOS_E_MEMORY;
        return;
    }
    old = dta_get();
    dta_set(seg << 16);
    low_str(seg, LOW_PATH, j->path);
    lfn = long_names(seg, j->path);
    if (n == 3) {                       /* (a root) */
        j->folder = 1;
        e = 0;
    } else if ((e = find(seg, lfn, 1, &handle, 0x37)) == 0) {
        found(seg, lfn, 0, &f);
        j->folder = (f.attr & GLOS_FA_FOLDER) != 0;
        if (lfn)
            find_close(handle);
    }
    if (e >= 0 && lfn) {
        memset(&r, 0, sizeof r);
        r.eax = 0x7160;                 /* the path by its short names (CL 1), where all of it is there */
        r.ecx = 0x8001;
        r.ds = (u16)seg;
        r.esi = LOW_PATH;
        r.es = (u16)seg;
        r.edi = 0;
        r.flags = FL_CF;
        if ((e = dos(&r)) == 0) {
            u32 i;
            for (i = 0; i < PATH_MAX_ - 1 && vm_rd8((seg << 4) + i); i++)
                j->path[i] = (char)vm_rd8((seg << 4) + i);
            j->path[i] = 0;
        } else if (e > 0) {
            e = 0;                      /* (a file yet to be made: as it was given) */
        }
    }
    if (e >= 0) {
        dta_set(old);
        low_put(seg);
    }
}

static int watch(struct app *a, struct trapframe *tf)
{
    static struct watch_job j;          /* (under the app's lock) */
    struct glos_fs_watch q;
    char *name = 0;
    u32 size, id = 0, i;
    int n, r;
    memset(&j, 0, sizeof j);
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || (n = path_in(tf, q.path, j.path, sizeof j.path)) < 3 || j.path[1] != ':'
        || j.path[2] != '\\')
        return a2_fail(tf, GLOS_E_ARGUMENT);
    while (n > 3 && j.path[n - 1] == '\\')
        j.path[--n] = 0;
    if (app_batch(a, watch_batch, &j, 0) != 0 || j.error)
        return a2_fail(tf, j.error ? (u32)j.error : GLOS_E_MEMORY);
    if (j.folder) {
        r = strlen(j.path) > 67 ? GLOS_E_ARGUMENT : watch_add(a, j.path, &id);
    } else {
        for (i = 0; j.path[i]; i++)     /* a file: its folder is watched, for it alone */
            if (j.path[i] == '\\')
                name = &j.path[i];
        if (!name || !name[1] || strlen(name + 1) > 12 || name - j.path > 67)
            return a2_fail(tf, GLOS_E_ARGUMENT);
        if (name == &j.path[2]) {       /* (in the root: "C:\" stays) */
            char root[4] = "C:\\";
            root[0] = j.path[0];
            r = watch_add_file(a, root, name + 1, &id);
        } else {
            *name = 0;
            r = watch_add_file(a, j.path, name + 1, &id);
        }
    }
    if (r)
        return a2_fail(tf, (u32)r);
    if (!(q.handle = handle_new(&a->v2->h, GLOS_HK_WATCH, (void *)id)) || a2_wr(tf, &q, sizeof q, size) != 0) {
        int full = !q.handle;
        if (q.handle)
            handle_close(&a->v2->h, q.handle);
        watch_remove(a, id);
        return a2_fail(tf, full ? GLOS_E_MEMORY : GLOS_E_ARGUMENT);
    }
    return a2_ok(tf, 0);
}

/* ---- SECTORS */

struct sect_job {
    u32 disk, write, count;
    u32 lba_lo, lba_hi;
    u8 *data;
    u32 bytes, total_lo, total_hi;
    u32 bios;                           /* the BIOS's error (AH), 0 none */
    int error;
};

static int bios13(struct rmregs *r)     /* 0 done, 1 the BIOS refused (AH its error), -1 the app is gone */
{
    if (app_batch_int(0x13, r) != 0)
        return -1;
    return r->flags & FL_CF ? 1 : 0;
}

static void sect_batch(struct app *a, void *arg)
{
    struct sect_job *j = arg;
    struct rmregs r;
    u32 seg, lin, i, cyls = 0, heads = 0, spt = 0, tries;
    int ext = 0, e = 0;
    (void)a;
    if (!(seg = low_get(LOW_PARAS))) {  /* (the BIOS's packet at 0, a sector at 100h) */
        j->error = GLOS_E_MEMORY;
        return;
    }
    lin = seg << 4;
    j->bytes = 512;
    if (j->disk >= 0x80) {              /* a hard disk: the BIOS's extensions, where it has them */
        memset(&r, 0, sizeof r);
        r.eax = 0x4100;
        r.ebx = 0x55AA;
        r.edx = j->disk;
        ext = bios13(&r) == 0 && (r.ebx & 0xFFFF) == 0xAA55 && (r.ecx & 1);
    }
    if (ext) {
        memset(vm_ptr(lin), 0, 0x20);
        vm_wr8(lin, 0x1A);
        memset(&r, 0, sizeof r);
        r.eax = 0x4800;
        r.edx = j->disk;
        r.ds = (u16)seg;
        r.esi = 0;
        if ((e = bios13(&r)) == 0) {
            j->total_lo = vm_rd32(lin + 0x10);
            j->total_hi = vm_rd32(lin + 0x14);
            if (vm_rd16(lin + 0x18))
                j->bytes = vm_rd16(lin + 0x18);
        }
    }
    if (!ext || e > 0) {                /* its size by cylinders, heads and sectors */
        memset(&r, 0, sizeof r);
        r.eax = 0x0800;
        r.edx = j->disk;
        if ((e = bios13(&r)) == 0) {
            cyls = (((r.ecx & 0xC0) << 2) | ((r.ecx >> 8) & 0xFF)) + 1;
            heads = ((r.edx >> 8) & 0xFF) + 1;
            spt = r.ecx & 0x3F;
            if (!ext) {
                j->total_lo = cyls * heads * spt;
                j->total_hi = 0;
            }
        }
    }
    if (e > 0)
        j->bios = (r.eax >> 8) & 0xFF ? (r.eax >> 8) & 0xFF : 1;
    if (e == 0 && j->bytes != 512 && j->count)
        j->bios = 1;                    /* (sectors of another size: not read or written here) */
    for (i = 0; e == 0 && !j->bios && i < j->count; i++) {
        u32 at = lin + 0x100, lba = j->lba_lo + i;
        if (j->write)
            memcpy(vm_ptr(at), j->data + i * 512, 512);
        for (tries = 0; tries < 3; tries++) {
            memset(&r, 0, sizeof r);
            r.edx = j->disk;
            if (ext) {
                memset(vm_ptr(lin), 0, 0x10);
                vm_wr8(lin, 0x10);
                vm_wr8(lin + 2, 1);
                vm_wr8(lin + 4, 0x00), vm_wr8(lin + 5, 0x01);    /* (the sector's bytes at seg:0100h) */
                vm_wr8(lin + 6, (u8)seg), vm_wr8(lin + 7, (u8)(seg >> 8));
                memcpy(vm_ptr(lin + 8), &lba, 4);
                memcpy(vm_ptr(lin + 12), &j->lba_hi, 4);
                r.eax = j->write ? 0x4300 : 0x4200;
                r.ds = (u16)seg;
                r.esi = 0;
            } else {
                u32 c, h, s;
                if (!spt || !heads || j->lba_hi || lba >= cyls * heads * spt) {
                    j->bios = 4;        /* (no such sector) */
                    break;
                }
                c = lba / (heads * spt), h = lba / spt % heads, s = lba % spt + 1;
                r.eax = (j->write ? 0x0300u : 0x0200u) | 1;
                r.ecx = ((c & 0xFF) << 8) | ((c >> 2) & 0xC0) | s;
                r.edx |= h << 8;
                r.es = (u16)seg;
                r.ebx = 0x100;
            }
            if ((e = bios13(&r)) <= 0)
                break;
            j->bios = (r.eax >> 8) & 0xFF ? (r.eax >> 8) & 0xFF : 1;
            memset(&r, 0, sizeof r);    /* (a floppy's motor may not have been up: reset, and again) */
            r.edx = j->disk;
            if (bios13(&r) < 0) {
                e = -1;
                break;
            }
        }
        if (e == 0 && !j->bios && !j->write)
            memcpy(j->data + i * 512, vm_ptr(at), 512);
        if (e == 0 && tries < 3)
            j->bios = j->bios == 4 ? 4 : 0;     /* (it went through, perhaps at a second try) */
        if (j->bios)
            break;
    }
    if (e >= 0)
        low_put(seg);
}

/* The disk GLOS itself runs from is never written (docs/v1-route.md): the first hard disk, and the floppy GLOS
   was started from if it was. */
static int own_disk(u32 disk)
{
    char c = vm.bi->cfg_path[0] & (char)~0x20;
    return disk == 0x80 || (disk < 2 && (c == 'A' || c == 'B') && disk == (u32)(c - 'A'));
}

static int sectors(struct app *a, struct trapframe *tf)
{
    struct glos_fs_sectors q;
    struct sect_job j;
    u32 size;
    int bad = 0;
    if (a2_rd(tf, &q, sizeof q, &size) != 0 || q.count > SECT_MAX || q.write > 1 || q.disk > 0xFF
        || (q.disk >= 4 && q.disk < 0x80))
        return a2_fail(tf, GLOS_E_ARGUMENT);
    if (q.write && q.count && own_disk(q.disk))
        return a2_fail(tf, GLOS_E_REFUSED);
    memset(&j, 0, sizeof j);
    j.disk = q.disk;
    j.write = q.write;
    j.count = q.count;
    j.lba_lo = q.lba_lo;
    j.lba_hi = q.lba_hi;
    if (!(j.data = kmalloc(q.count ? q.count * 512 : 1)))
        return a2_fail(tf, GLOS_E_MEMORY);
    if (q.write && q.count && user_rd((u16)tf->ds, q.buffer, j.data, q.count * 512) != 0) {
        kfree(j.data);
        return a2_fail(tf, GLOS_E_ARGUMENT);
    }
    if (app_batch(a, sect_batch, &j, 1) != 0 || j.error) {
        kfree(j.data);
        return a2_fail(tf, j.error ? (u32)j.error : GLOS_E_MEMORY);
    }
    q.bytes = j.bytes;
    q.total_lo = j.total_lo;
    q.total_hi = j.total_hi;
    q.error = j.bios;
    if (!j.bios && !q.write && q.count)
        bad = user_wr((u16)tf->ds, q.buffer, j.data, q.count * 512) != 0;
    kfree(j.data);
    if (bad || a2_wr(tf, &q, sizeof q, size) != 0)
        return a2_fail(tf, GLOS_E_ARGUMENT);
    return j.bios ? a2_fail(tf, GLOS_E_REFUSED) : a2_ok(tf, 0);
}

/* ---- the handles' ends, the calls */

void api2_fs_close(struct app *a, int kind, void *obj, int ending)
{
    if (kind == GLOS_HK_WATCH) {
        watch_remove(a, (u32)obj);
    } else {
        struct listing *l = obj;
        if (l->lfn && l->begun && !l->done && !ending) {
            struct close_job j = { l->find };
            app_batch(a, close_batch, &j, 0);
        }
        kfree(l);
    }
}

int api2_fs(struct app *a, struct trapframe *tf, u32 n)
{
    switch (n) {
    case GLOS_FS_LIST & 0xFF: return list(a, tf);
    case GLOS_FS_INFO & 0xFF: return info(a, tf);
    case GLOS_FS_DRIVES & 0xFF: return drives(a, tf);
    case GLOS_FS_WATCH & 0xFF: return watch(a, tf);
    case GLOS_FS_SECTORS & 0xFF: return sectors(a, tf);
    }
    return a2_fail(tf, GLOS_E_FUNCTION);
}
