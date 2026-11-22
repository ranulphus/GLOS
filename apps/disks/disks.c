/* DISKS.EXE: Disk Tools (GLOS v1, phase F; docs/v1/apps/disk-tools.md), the first part.
 *
 * The PC's disks as the BIOS has them (GLOS_FS_SECTORS: the floppy drives, the hard disks), and the jobs retro
 * PCs still need done to them:
 *   - a disk's image to a file (sectors that can't be read tried again, counted, and zeros in the image);
 *   - an image written to a disk, and read back to see that it is there (a hard disk: asked twice; the disk
 *     GLOS runs from: never, by the kernel's own rule);
 *   - a disk checked: every sector read;
 *   - a floppy formatted (its file system made new: the disk as its drive formats it);
 *   - a hard disk's partitions listed;
 *   - an image (or a floppy) looked inside: its folders and files, which can be copied out;
 *   - a new image made of a folder's files;
 *   - a folder backed up to another computer over SFTP (everything, or what changed since the last), and a
 *     backup restored (apps/shared/backup.c's sets; bknet.c: a saved session's or an address's computer). The
 *     same from the command line, for the schedule: DISKS -backup FOLDER COMPUTER WHERE [-changes] and
 *     DISKS -restore COMPUTER WHERE SET|newest FOLDER (no window; the lines below, and the exit code).
 * The long ones run as the toolkit's tasks, with their progress shown and a Cancel that stops them cleanly.
 *
 * The volume code is apps/shared/fatimg.c (FAT12 and FAT16), the sector jobs apps/shared/diskjob.c, new volumes
 * the kernel's own kernel/disk/fatfmt.c: all three are tested on the host against mtools and fsck.fat.
 *
 * Not yet (docs/v1/phase-f.md): a full format and floppies of another size than their drive's (both need the
 * BIOS's track format, which GLOS_FS_SECTORS has not got), images in parts for disks over 2 GB, backups as
 * ZIPs. (Images are opened as folders in Files too: apps/shared/arc.c's IMG kind.)
 *
 * The suite's lines (tests/gui/disks.txt, disks-floppy.txt): "DISKS ..." on the output. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glos/glos.h>
#include <glos/gui.h>

#ifdef __DJGPP__
#include <dpmi.h>
#endif

#include "../../kernel/disk/disk.h"
#include "../shared/backup.h"
#include "../shared/diskjob.h"
#include "../shared/fatimg.h"
#include "bknet.h"

enum { C_TOIMAGE = 1, C_FROMIMAGE, C_CHECK, C_FORMAT, C_INSIDE, C_REFRESH, C_OPEN, C_NEW, C_EXIT, C_BACKUP, C_RESTORE,
       C_SCHEDULED };
enum { J_READ = 1, J_WRITE, J_CHECK, J_FORMAT, J_NEW, J_EXTRACT, J_BACKUP, J_SETS, J_RESTORE };
enum { L_NOT = 0, L_VOLUME, L_NONE, L_OTHER, L_UNREAD = -1 };  /* what a look at a disk found */

#define E_OWN 0x100                     /* dev_io: GLOS refused (the disk it runs from) */
#define FLOPPY_MOST 5760ul

struct disk {
    unsigned bios;                      /* the BIOS's number: 00h and 01h the floppy drives, 80h up the hard disks */
    unsigned long total;                /* its sectors (a floppy drive: of the largest disk it takes) */
    int looked;                         /* L_ */
    unsigned long sectors;              /* a floppy's disk, as its first sector says it */
    int fat;
    unsigned long free_kb;
    char label[12];
    struct mbr_part part[4];
    int parts;                          /* a hard disk's partitions; -1: no table */
};

static struct gui_window *win;
static struct gui_widget *dlist, *plist, *about, *status;
static struct disk disks[8];
static int ndisks;

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
    va_list ap;
    printf("DISKS ");
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    fflush(stdout);
}

/* ---- sectors: a disk's by GLOS_FS_SECTORS, an image file's by the C library */

static int dev_io(void *ctx, unsigned long lba, unsigned char *data, unsigned count, int write)
{
    unsigned done = 0;
    while (done < count) {              /* (a call takes 32 at most; the volume code may ask for more) */
        struct glos_fs_sectors q;
        unsigned n = count - done < 32 ? count - done : 32;
        memset(&q, 0, sizeof q);
        q.disk = *(unsigned *)ctx;
        q.write = (unsigned)(write != 0);
        q.lba_lo = (unsigned)(lba + done);
        q.count = n;
        q.buffer = (unsigned)(size_t)(data + done * 512);
        if (glos_fs_sectors(&q) != 0)
            return q.error ? (int)q.error : E_OWN;
        done += n;
    }
    return 0;
}

static int file_io(void *ctx, unsigned long lba, unsigned char *data, unsigned count, int write)
{
    FILE *f = ctx;
    if (lba > 0x3FFFFFul || fseek(f, (long)(lba * 512), SEEK_SET) != 0)
        return 1;
    return (write ? fwrite(data, 512, count, f) : fread(data, 512, count, f)) != count;
}

static int dev_format(void *ctx, unsigned long lba, const unsigned char *data, unsigned count)
{
    return dev_io(ctx, lba, (unsigned char *)(size_t)data, count, 1);
}

static int file_format(void *ctx, unsigned long lba, const unsigned char *data, unsigned count)
{
    return file_io(ctx, lba, (unsigned char *)(size_t)data, count, 1);
}

/* A disk was written under DOS: DOS is told to forget what it holds of its disks (the sectors in its buffers;
   a floppy is looked at anew when it is next used). INT 21h's disk reset, as the tools that write disks do. */
static void dos_forget(void)
{
#ifdef __DJGPP__
    __dpmi_regs r;
    memset(&r, 0, sizeof r);
    r.h.ah = 0x0D;
    __dpmi_int(0x21, &r);
#endif
}

/* ---- what things are called */

static int is_floppy(const struct disk *d) { return d->bios < 0x80; }

static void disk_name(const struct disk *d, char *out, size_t room)
{
    if (is_floppy(d))
        snprintf(out, room, GUI_T("Floppy %c:"), 'A' + (int)d->bios);
    else
        snprintf(out, room, GUI_T("Hard disk %u"), d->bios - 0x7F);
}

static void size_text(unsigned long sectors, char *out, size_t room)
{
    unsigned long kb = sectors / 2;
    if (sectors == 2400)
        snprintf(out, room, "1.2 MB");
    else if (sectors == 2880)
        snprintf(out, room, "1.44 MB");
    else if (sectors == 5760)
        snprintf(out, room, "2.88 MB");
    else if (kb >= 1024ul * 1024)
        snprintf(out, room, "%lu.%lu GB", kb >> 20, ((kb >> 10) & 1023) * 10 / 1024);
    else if (kb >= 10240)
        snprintf(out, room, "%lu MB", kb >> 10);
    else if (kb >= 1024)
        snprintf(out, room, "%lu.%lu MB", kb >> 10, (kb & 1023) * 10 / 1024);
    else
        snprintf(out, room, "%lu KB", kb);
}

static void bytes_text(unsigned long n, char *out, size_t room)
{
    if (n < 1024)
        snprintf(out, room, GUI_T("%lu bytes"), n);
    else
        size_text((n + 511) / 512, out, room);
}

static const char *part_kind(unsigned type)
{
    static char other[24];
    switch (type) {
    case 0x01: return "FAT12";
    case 0x04: case 0x06: return "FAT16";
    case 0x0E: return "FAT16 (LBA)";
    case 0x0B: return "FAT32";
    case 0x0C: return "FAT32 (LBA)";
    case 0x05: case 0x0F: return GUI_T("Extended");
    case 0x07: return "NTFS / HPFS";
    case 0x82: return GUI_T("Linux swap");
    case 0x83: return "Linux";
    case 0xEE: return "GPT";
    }
    snprintf(other, sizeof other, GUI_T("Type %02Xh"), type);
    return other;
}

/* A floppy's size by its first sector, where that has DOS's parameters: 0 if it has not. */
static unsigned long bpb_sectors(const unsigned char *s)
{
    unsigned long total = (unsigned long)s[19] | (unsigned long)s[20] << 8;
    if ((s[11] | s[12] << 8) != 512 || !s[13] || (s[13] & (s[13] - 1)) || !s[16] || !(s[14] | s[15] << 8) || !(s[17] | s[18] << 8))
        return 0;
    if (!total)
        total = (unsigned long)s[32] | (unsigned long)s[33] << 8 | (unsigned long)s[34] << 16 | (unsigned long)s[35] << 24;
    return total >= 320 && total <= FLOPPY_MOST ? total : 0;
}

/* ---- the disks found, and looked at */

/* A disk's first sector read: a floppy's volume (which spins it: only when asked, or for a job), a hard disk's
   partitions. */
static void look(struct disk *d)
{
    unsigned char sec[512];
    d->looked = L_UNREAD;
    d->sectors = d->total;
    d->label[0] = 0;
    d->fat = 0;
    d->parts = -1;
    if (dev_io(&d->bios, 0, sec, 1, 0) != 0)
        return;
    if (!is_floppy(d)) {
        d->looked = L_VOLUME;
        d->parts = mbr_parse(sec, d->part);
        return;
    }
    d->looked = L_NONE;
    if (bpb_sectors(sec)) {
        struct fatimg *v;
        char why[96];
        d->sectors = bpb_sectors(sec);
        if (d->sectors != d->total) {   /* (its tracks are not laid out as the kernel's call counts them) */
            d->looked = L_OTHER;
            return;
        }
        if ((v = fatimg_open(dev_io, &d->bios, why, sizeof why)) != NULL) {
            struct fatimg_info in;
            fatimg_info(v, &in);
            d->looked = L_VOLUME;
            d->fat = in.fat;
            d->free_kb = (unsigned long)((unsigned long long)in.free_clusters * in.cluster_bytes / 1024);
            snprintf(d->label, sizeof d->label, "%s", in.label);
            fatimg_close(v);
        }
    }
}

static void find_disks(void)
{
    static const unsigned char numbers[] = { 0, 1, 0x80, 0x81, 0x82, 0x83, 0x84, 0x85 };
    unsigned i;
    ndisks = 0;
    for (i = 0; i < sizeof numbers; i++) {
        struct glos_fs_sectors q;
        struct disk *d = &disks[ndisks];
        memset(&q, 0, sizeof q);
        q.disk = numbers[i];
        if (glos_fs_sectors(&q) != 0 || (!q.total_lo && !q.total_hi))
            continue;
        memset(d, 0, sizeof *d);
        d->bios = numbers[i];
        d->total = q.total_hi ? 0xFFFFFFFFul : q.total_lo;
        d->sectors = d->total;
        if (is_floppy(d) && d->total > FLOPPY_MOST)
            continue;
        if (!is_floppy(d))
            look(d);
        ndisks++;
    }
}

static struct disk *current(void)
{
    int row = gui_list_get(dlist);
    return row >= 0 && row < ndisks ? &disks[row] : NULL;
}

static void disk_about(const struct disk *d, char *out, size_t room)
{
    char size[24];
    if (is_floppy(d)) {
        switch (d->looked) {
        case L_NOT: snprintf(out, room, "%s", GUI_T("Not looked at yet (F5 reads the disk in the drive)")); break;
        case L_UNREAD: snprintf(out, room, "%s", GUI_T("No disk in the drive, or it can't be read")); break;
        case L_NONE: snprintf(out, room, "%s", GUI_T("Not a DOS disk")); break;
        case L_OTHER:
            size_text(d->sectors, size, sizeof size);
            snprintf(out, room, GUI_T("A %s disk: not of this drive's own size"), size);
            break;
        default:
            size_text(d->free_kb * 2, size, sizeof size);
            snprintf(out, room, GUI_T("%s%sFAT%d, %s free"), d->label, d->label[0] ? ", " : "", d->fat, size);
        }
    } else if (d->looked == L_UNREAD) {
        snprintf(out, room, "%s", GUI_T("It can't be read"));
    } else if (d->parts < 0) {
        snprintf(out, room, "%s", GUI_T("No partition table"));
    } else {
        snprintf(out, room, d->parts == 1 ? GUI_T("%d partition%s") : GUI_T("%d partitions%s"), d->parts,
                 d->bios == 0x80 ? GUI_T("; GLOS runs from it: never written") : "");
    }
}

static void show_disk(void)             /* the chosen disk's line, and its partitions */
{
    struct disk *d = current();
    char name[40], text[160], line[220];
    int i;
    gui_list_clear(plist);
    if (!d) {
        gui_set_text(about, "");
        return;
    }
    disk_name(d, name, sizeof name);
    disk_about(d, text, sizeof text);
    snprintf(line, sizeof line, "%s%s %s", name, name[0] && name[strlen(name) - 1] == ':' ? "" : ":", text);
    gui_set_text(about, line);
    for (i = 0; !is_floppy(d) && i < 4; i++) {
        char no[8], start[16], size[24];
        const char *cells[5];
        if (!d->part[i].type || d->parts < 0)
            continue;
        snprintf(no, sizeof no, "%d", i + 1);
        snprintf(start, sizeof start, "%lu", d->part[i].start);
        size_text(d->part[i].sectors, size, sizeof size);
        cells[0] = no, cells[1] = part_kind(d->part[i].type), cells[2] = start, cells[3] = size;
        cells[4] = d->part[i].active ? GUI_T("Yes") : "";
        gui_list_add(plist, cells, gui_icon("drive", 0), NULL);
    }
}

static void fill(int keep)              /* the disks' list, anew; row `keep` chosen */
{
    int i;
    gui_list_clear(dlist);
    for (i = 0; i < ndisks; i++) {
        char name[40], size[24], text[160];
        const char *cells[3];
        disk_name(&disks[i], name, sizeof name);
        size_text(disks[i].total, size, sizeof size);
        disk_about(&disks[i], text, sizeof text);
        cells[0] = name, cells[1] = size, cells[2] = text;
        gui_list_add(dlist, cells, gui_icon(is_floppy(&disks[i]) ? "drive-floppy" : "drive", 0), NULL);
    }
    if (ndisks)
        gui_list_set(dlist, keep >= 0 && keep < ndisks ? keep : 0);
    show_disk();
}

static void looked_say(const struct disk *d)
{
    say("disk %02X sectors=%lu looked=%d fat=%d label=%s parts=%d", d->bios, d->sectors, d->looked, d->fat,
        d->label[0] ? d->label : "-", d->parts);
}

/* ---- a volume looked inside (an image file's, a floppy's): its folders and files, copied out */

static struct {
    struct gui_window *w;
    struct gui_widget *list, *where, *status;
    struct fatimg *v;
    FILE *f;                            /* an image's file; NULL: a disk */
    unsigned bios;
    char name[64];
    char path[260];                     /* the folder shown ("" the root) */
    char dest[300];                     /* where files are copied out to: at first the image's own folder */
    struct fatimg_entry *e;             /* its entries, folders first */
    int n;
} im;

struct job {
    int kind;                           /* J_ */
    struct disk *d;                     /* the disk it is done to, or NULL */
    unsigned bios;
    unsigned long sectors;
    unsigned run;                       /* sectors a call: a floppy's track (0: the most) */
    char path[300];                     /* the image's file; or the folder files go to */
    char from[300];                     /* a new image: the folder its files come from */
    char label[12];
    FILE *f;
    struct gui_task *t;
    struct diskjob_report r;
    int rc;
    int fat;
    char why[160];
    unsigned long files, bytes, left;   /* files copied, their bytes; files left out */
    char base[260];                     /* files out of a volume: the folder they are in, and their entries */
    struct fatimg_entry *items;
    int nitems;
    /* A backup's (J_BACKUP: from the folder `from`; J_SETS: the sets listed; J_RESTORE: a set into `path`): */
    char computer[100], where[200], set[BK_SET];
    int changes, nsets, err;
    char (*sets)[BK_SET];
    struct bk_job bk;
};
static struct job *running;             /* one job at a time */

static int job_stopped(struct job *j) { return j->t && gui_task_cancelled(j->t); }

static void im_close(void)
{
    if (!im.w)
        return;
    gui_close(im.w);
    if (im.v)
        fatimg_close(im.v);
    if (im.f)
        fclose(im.f);
    free(im.e);
    memset(&im, 0, sizeof im);
}

static void im_on_close(struct gui_window *w)
{
    (void)w;
    if (!running)
        im_close();
}

static int by_kind_and_name(const void *a, const void *b)
{
    const struct fatimg_entry *x = a, *y = b;
    return x->dir != y->dir ? y->dir - x->dir : strcmp(x->dos, y->dos);
}

static void date_text(unsigned date, char *out, size_t room)
{
    out[0] = 0;
    if (date)
        gui_date_text((int)(((date >> 9) + 1980) * 10000 + ((date >> 5) & 15) * 100 + (date & 31)), out, (int)room);
}

static void im_show(void)               /* the folder at im.path, listed */
{
    struct fatimg_info in;
    char line[300], size[24], free_text[24];
    int i;
    free(im.e);
    im.e = NULL;
    if ((im.n = fatimg_list(im.v, im.path, &im.e)) < 0) {
        im.n = 0;
        gui_message(im.w, im.name, GUI_T("The folder is damaged: it can't be read."), GUI_MB_OK);
    }
    qsort(im.e, (size_t)im.n, sizeof *im.e, by_kind_and_name);
    gui_list_clear(im.list);
    if (im.path[0]) {
        const char *cells[3] = { "..", "", "" };
        gui_list_add(im.list, cells, gui_icon("folder-open", 0), NULL);
    }
    for (i = 0; i < im.n; i++) {
        char date[24];
        const char *cells[3];
        size[0] = 0;
        if (!im.e[i].dir)
            bytes_text(im.e[i].size, size, sizeof size);
        date_text(im.e[i].date, date, sizeof date);
        cells[0] = im.e[i].name, cells[1] = size, cells[2] = date;
        gui_list_add(im.list, cells, gui_icon(im.e[i].dir ? "folder" : "file", 0), &im.e[i]);
    }
    if (gui_list_count(im.list))
        gui_list_set(im.list, 0);
    snprintf(line, sizeof line, "\\%s", im.path);
    gui_set_text(im.where, line);
    fatimg_info(im.v, &in);
    size_text((unsigned long)((unsigned long long)in.free_clusters * in.cluster_bytes / 512), free_text, sizeof free_text);
    size_text(in.sectors, size, sizeof size);
    snprintf(line, sizeof line, GUI_T("%s%s%s, FAT%d, %s free"), in.label, in.label[0] ? ", " : "", size, in.fat, free_text);
    gui_set_text(im.status, line);
    say("folder \\%s entries=%d", im.path, im.n);
}

static void im_up(void)
{
    char *cut = strrchr(im.path, '\\');
    if (!im.path[0] || running)
        return;
    if (cut)
        *cut = 0;
    else
        im.path[0] = 0;
    im_show();
}

static void im_list_on(struct gui_widget *w, int what)
{
    struct fatimg_entry *e;
    int row = gui_list_get(w);
    if (what != GUI_ON_ACTIVATE || row < 0 || running)
        return;
    if (!(e = gui_list_user(w, row))) {
        im_up();
    } else if (e->dir && strlen(im.path) + strlen(e->dos) + 2 < sizeof im.path) {
        if (im.path[0])
            strcat(im.path, "\\");
        strcat(im.path, e->dos);
        im_show();
    }
}

static void job_start(struct job *j, const char *title);

static void im_extract(int all)         /* what is chosen (or everything) copied out to a folder */
{
    char dest[300];
    struct job *j;
    int i, rows = gui_list_count(im.list);
    if (running || !im.v)
        return;
    snprintf(dest, sizeof dest, "%s", im.dest);
    if (!(j = calloc(1, sizeof *j)) || !(j->items = calloc((size_t)im.n + 1, sizeof *j->items))) {
        free(j);
        return;
    }
    if (all) {
        struct fatimg_entry *root = NULL;
        int n = fatimg_list(im.v, "", &root);
        free(j->items);
        j->items = root;
        j->nitems = n < 0 ? 0 : n;
    } else {
        snprintf(j->base, sizeof j->base, "%s", im.path);
        for (i = 0; i < rows; i++) {
            struct fatimg_entry *e = gui_list_user(im.list, i);
            if (e && gui_list_chosen(im.list, i))
                j->items[j->nitems++] = *e;
        }
    }
    if (!j->nitems || !gui_folder_dialog(im.w, GUI_T("Copy the Files to"), dest, sizeof dest)) {
        free(j->items);
        free(j);
        return;
    }
    j->kind = J_EXTRACT;
    snprintf(im.dest, sizeof im.dest, "%s", dest);
    snprintf(j->path, sizeof j->path, "%s", dest);
    job_start(j, GUI_T("Copying Files Out"));
}

static void im_button(struct gui_widget *b, int what)
{
    (void)what;                         /* (the button's own number is b->id) */
    if (b->id == 1)
        im_up();
    else
        im_extract(b->id == 3);
}

static int im_key(struct gui_window *w, struct gui_ev *e)
{
    (void)w;
    if (e->type == GUI_EV_KEY && e->key == GUI_K_BACK) {     /* Backspace: up */
        im_up();
        return 1;
    }
    return 0;
}

/* The window for a volume that has been opened (v; f its file, or NULL and bios its disk). */
static void im_open(struct fatimg *v, FILE *f, unsigned bios, const char *name, const char *dest)
{
    static const char *const heads[] = { GUI_N("Name"), GUI_N("Size"), GUI_N("Changed") };
    static const int widths[] = { 230, 80, 90 };
    struct gui_widget *row;
    struct fatimg_info in;
    im_close();
    im.v = v, im.f = f, im.bios = bios;
    snprintf(im.name, sizeof im.name, "%s", name);
    snprintf(im.dest, sizeof im.dest, "%s", dest);
    im.w = gui_window(im.name, 460, 340, 0);
    im.w->owner = win;
    im.w->on_close = im_on_close;
    im.w->on_key = im_key;
    row = gui_hbox(im.w->root, 6, 4);
    gui_button(row, GUI_T("&Up"), im_button, 1);
    gui_button(row, GUI_T("&Copy Out..."), im_button, 2);
    gui_button(row, GUI_T("Copy &Everything Out..."), im_button, 3);
    im.where = gui_label(row, "\\");
    im.list = gui_list(im.w->root, 3, heads, widths);
    gui_list_multi(im.list, 1);
    gui_grow(im.list, 1);
    gui_on(im.list, im_list_on, 0);
    im.status = gui_status(im.w);
    gui_show(im.w, 1);
    gui_focus(im.list);
    fatimg_info(v, &in);
    say("opened %s fat=%d label=%s sectors=%lu", im.name, in.fat, in.label[0] ? in.label : "-", in.sectors);
    im_show();
}

/* ---- the jobs: their work (a worker's thread), and what is said when they end (the window's) */

static int tick(void *arg, unsigned long done, unsigned long total)
{
    struct job *j = arg;
    char text[96];
    if (!j->t)
        return 0;
    if (j->r.bad)
        snprintf(text, sizeof text, GUI_T("Sector %lu of %lu; %lu bad"), done, total, j->r.bad);
    else
        snprintf(text, sizeof text, GUI_T("Sector %lu of %lu"), done, total);
    gui_task_progress(j->t, (int)done, (int)total, text);
    return gui_task_cancelled(j->t);
}

static unsigned spt_of(unsigned long sectors)   /* a floppy's sectors a track, by its size; 0: not a floppy's size */
{
    return sectors == 720 || sectors == 1440 ? 9 : sectors == 2400 ? 15 : sectors == 2880 ? 18 : sectors == 5760 ? 36 : 0;
}

static int make_volume(struct job *j, fat_write write, void *ctx)
{
    struct fat_geo g;
    struct fat_made made;
    int rc;
    memset(&g, 0, sizeof g);
    g.sectors = j->sectors;
    g.heads = 2;
    g.spt = spt_of(j->sectors);
    g.serial = (unsigned long)time(NULL) ^ ((unsigned long)glos_ticks() << 16);
    g.label = j->label[0] ? j->label : 0;
    if ((rc = fat_format(&g, write, ctx, &made)) != 0) {
        snprintf(j->why, sizeof j->why, "%s", rc == -1 ? GUI_T("No file system fits a disk of that size.")
                                                       : GUI_T("The disk could not be written."));
        return -1;
    }
    j->fat = made.fat;
    return 0;
}

/* A folder's files and folders into a new volume's folder (vpath; "" its root). */
static int add_tree(struct job *j, struct fatimg *v, const char *dir, const char *vpath, int depth)
{
    struct gui_dirent *e = NULL;
    int n = gui_dir_read(dir, &e), i, rc = 0;
    if (n < 0) {
        snprintf(j->why, sizeof j->why, GUI_T("The folder %s can't be read."), dir);
        return -1;
    }
    for (i = 0; i < n && rc == 0; i++) {
        char p[300], vp[300];
        if (job_stopped(j)) {
            j->r.stopped = 1;
            rc = -1;
            break;
        }
        gui_path_join(p, sizeof p, dir, e[i].name);
        snprintf(vp, sizeof vp, "%s%s%s", vpath, vpath[0] ? "\\" : "", e[i].name);
        if (e[i].dir) {
            if (depth >= 16 || fatimg_mkdir(v, vp, e[i].date, e[i].time) != 0)
                j->left++;              /* (a name that is not DOS's, or no room for the folder) */
            else
                rc = add_tree(j, v, p, vp, depth + 1);
        } else {
            unsigned char *data = malloc(e[i].size ? e[i].size : 1);
            FILE *f = data ? gui_fopen(p, "rb") : NULL;
            unsigned long got = f ? (unsigned long)fread(data, 1, e[i].size, f) : 0;
            int r;
            if (f)
                fclose(f);
            if (!f || got != e[i].size) {
                snprintf(j->why, sizeof j->why, GUI_T("%s can't be read."), p);
                rc = -1;
            } else if ((r = fatimg_add(v, vp, data, got, e[i].date, e[i].time)) == -2) {
                snprintf(j->why, sizeof j->why, GUI_T("%s does not fit: the image is full."), e[i].text);
                rc = -1;
            } else if (r != 0) {
                j->left++;
            } else {
                j->files++;
                j->bytes += got;
                if (j->t)
                    gui_task_progress(j->t, (int)(j->bytes / 512), (int)j->sectors, e[i].text);
            }
            free(data);
        }
    }
    free(e);
    return rc;
}

static int new_image(struct job *j)
{
    static unsigned char zeros[16 * 512];
    struct fatimg *v;
    unsigned long at;
    int rc;
    for (at = 0; at < j->sectors; at += 16)             /* the whole of it, empty: then its file system */
        if (fwrite(zeros, 512, 16, j->f) != 16) {
            snprintf(j->why, sizeof j->why, "%s", GUI_T("The image could not be written (is its disk full?)"));
            return -1;
        }
    if (fflush(j->f) != 0 || make_volume(j, file_format, j->f) != 0) {
        if (!j->why[0])
            snprintf(j->why, sizeof j->why, "%s", GUI_T("The image could not be written (is its disk full?)"));
        return -1;
    }
    if (!(v = fatimg_open(file_io, j->f, j->why, sizeof j->why)))
        return -1;
    rc = add_tree(j, v, j->from, "", 0);
    if (fatimg_close(v) != 0 && rc == 0) {
        snprintf(j->why, sizeof j->why, "%s", GUI_T("The image could not be written (is its disk full?)"));
        rc = -1;
    }
    return rc;
}

/* A file or a folder (with what is in it) out of the volume into the folder dest. */
static int out_entry(struct job *j, const char *vdir, const struct fatimg_entry *e, const char *dest, int depth)
{
    static unsigned char buf[16384];
    char vp[300], dp[300];
    unsigned long at;
    FILE *f;
    if (job_stopped(j)) {
        j->r.stopped = 1;
        return -1;
    }
    snprintf(vp, sizeof vp, "%s%s%s", vdir, vdir[0] ? "\\" : "", e->dos);
    gui_path_join(dp, sizeof dp, dest, e->dos);
    if (e->dir) {
        struct fatimg_entry *in = NULL;
        int n, i, rc = 0;
        if (depth >= 16 || (!gui_is_dir(dp) && gui_mkdir(dp) != 0)) {
            snprintf(j->why, sizeof j->why, GUI_T("The folder %s could not be made."), dp);
            return -1;
        }
        if ((n = fatimg_list(im.v, vp, &in)) < 0) {
            snprintf(j->why, sizeof j->why, GUI_T("The folder %s is damaged."), vp);
            return -1;
        }
        for (i = 0; i < n && rc == 0; i++)
            rc = out_entry(j, vp, &in[i], dp, depth + 1);
        free(in);
        return rc;
    }
    if (!(f = gui_fopen(dp, "wb"))) {
        snprintf(j->why, sizeof j->why, GUI_T("%s could not be made."), dp);
        return -1;
    }
    for (at = 0; at < e->size;) {
        unsigned long want = e->size - at < sizeof buf ? e->size - at : sizeof buf;
        long got = fatimg_read(im.v, vp, at, buf, want);
        if (got <= 0 || fwrite(buf, 1, (size_t)got, f) != (size_t)got) {
            snprintf(j->why, sizeof j->why, got <= 0 ? GUI_T("%s is damaged: it can't be read.") : GUI_T("%s could not be written."),
                     got <= 0 ? vp : dp);
            fclose(f);
            gui_unlink(dp);
            return -1;
        }
        at += (unsigned long)got;
    }
    if (fclose(f) != 0) {
        snprintf(j->why, sizeof j->why, GUI_T("%s could not be written."), dp);
        return -1;
    }
    gui_set_time(dp, e->date, e->time);
    j->files++;
    j->bytes += e->size;
    if (j->t)
        gui_task_progress(j->t, 0, 0, e->name);
    return 0;
}

/* ---- backups on another computer (bknet.c, apps/shared/backup.c): one connection kept while Disk Tools runs, a
   job's at a time; its questions (a host met first, a password) put on the window's thread */

#define SETS_MOST 200
static struct remote *conn;
static char conn_to[100];

struct ask {
    struct job *j;
    int password, again, answer;
    const char *user, *host, *fingerprint;
    char *out;
    unsigned room;
    glos_handle sig;
};

static void ask_ui(void *data)
{
    struct ask *a = data;
    char t[400];
    if (a->password) {
        snprintf(t, sizeof t, a->again ? GUI_T("That was not it. The password of %s on %s:")
                 : GUI_T("The password of %s on %s:"), a->user, a->host);
        a->out[0] = 0;
        a->answer = gui_ask_secret(win, GUI_T("Password"), t, a->out, (int)a->room);
    } else {
        snprintf(t, sizeof t, GUI_T("This PC has not met %s before. Its key's fingerprint is\n%s\nIs that the right computer?"),
                 a->host, a->fingerprint);
        a->answer = gui_message(win, GUI_T("Disk Tools"), t, GUI_MB_YESNO) == GUI_YES;
    }
    glos_signal_set(a->sig, 1);
}

static int ask(struct ask *a)                   /* (the job's thread) put, and its answer waited for */
{
    int which;
    a->answer = 0;
    if (!a->j->t || !(a->sig = glos_signal(0)))
        return 0;
    gui_task_post(a->j->t, ask_ui, a);
    glos_wait(&a->sig, 1, 0xFFFFFFFFu, &which);
    glos_close(a->sig);
    return a->answer;
}

static int ask_host(void *ctx, const char *host, const char *fingerprint)
{
    struct ask a;
    memset(&a, 0, sizeof a);
    a.j = ctx, a.host = host, a.fingerprint = fingerprint;
    say("backup new-host %s", host);
    return ask(&a);
}

static int ask_password(void *ctx, const char *user, const char *host, int again, char *out, unsigned room)
{
    struct ask a;
    memset(&a, 0, sizeof a);
    a.j = ctx, a.password = 1, a.again = again, a.user = user, a.host = host, a.out = out, a.room = room;
    say("backup password %s", again ? "again" : "asked");
    return ask(&a);
}

static const struct remote_ui asking = { ask_host, ask_password };

static int bk_tick(void *ctx, const char *path, unsigned long long done, unsigned long long total)
{
    struct job *j = ctx;
    char text[160];
    if (!j->t)
        return 0;
    if (path[0])
        snprintf(text, sizeof text, GUI_T("%s (%lu of %lu KB)"), path, (unsigned long)(done >> 10), (unsigned long)(total >> 10));
    else
        snprintf(text, sizeof text, GUI_T("The list of files"));
    gui_task_progress(j->t, (int)(done >> 10), (int)(total >> 10) ? (int)(total >> 10) : 1, text);
    return gui_task_cancelled(j->t);
}

/* A backup's job, on its thread (or headless, with no task): connected (again, if the last one went), then done. */
static void bk_work(struct job *j)
{
    struct bk_store st;
    char temp[300];
    if (conn && strcmp(conn_to, j->computer)) {
        remote_close(conn);
        conn = NULL;
    }
    if (!conn) {
        conn = bknet_open(j->computer, j->t ? &asking : NULL, j, &j->err, j->why, sizeof j->why);
        snprintf(conn_to, sizeof conn_to, "%s", j->computer);
        if (!conn) {
            j->rc = BK_E_STORE;
            if (!j->why[0])
                snprintf(j->why, sizeof j->why, "%s", remote_error(j->err));
            return;
        }
    }
    bknet_store(&st, conn);
    gui_path_join(temp, sizeof temp, glos_home(), "BACKUP.TMP");
    memset(&j->bk, 0, sizeof j->bk);
    j->bk.store = &st, j->bk.where = j->where, j->bk.temp = temp, j->bk.changes = j->changes;
    j->bk.progress = bk_tick, j->bk.ctx = j;
    if (j->kind == J_BACKUP)
        j->rc = bk_backup(&j->bk, j->from);
    else if (j->kind == J_RESTORE)
        j->rc = bk_restore(&j->bk, j->set, j->path);
    else {
        j->nsets = (j->sets = malloc(SETS_MOST * sizeof *j->sets)) ? bk_sets(&st, j->where, j->sets, SETS_MOST) : -1;
        j->rc = j->nsets < 0 ? BK_E_NONE : BK_OK;
        if (j->nsets < 0)
            snprintf(j->why, sizeof j->why, "there is no folder %s there", j->where);
    }
    if (j->bk.why[0] && !j->why[0])
        snprintf(j->why, sizeof j->why, "%s", j->bk.why);
    if (j->rc == BK_E_STORE) {                  /* (the connection may have gone: a new one next time) */
        remote_close(conn);
        conn = NULL;
    }
}

static void work(struct gui_task *t, void *arg)
{
    struct job *j = arg;
    int i;
    j->t = t;
    if (j->kind == J_BACKUP || j->kind == J_SETS || j->kind == J_RESTORE) {
        bk_work(j);
        return;
    }
    switch (j->kind) {
    case J_READ: j->rc = diskjob_to_file(dev_io, &j->bios, j->sectors, j->run, j->f, tick, j, &j->r); break;
    case J_WRITE: j->rc = diskjob_from_file(dev_io, &j->bios, j->sectors, j->run, j->f, 1, tick, j, &j->r); break;
    case J_CHECK: j->rc = diskjob_check(dev_io, &j->bios, j->sectors, j->run, tick, j, &j->r); break;
    case J_FORMAT: j->rc = make_volume(j, dev_format, &j->bios); break;
    case J_NEW: j->rc = new_image(j); break;
    case J_EXTRACT:
        for (i = 0, j->rc = 0; i < j->nitems && j->rc == 0; i++)
            j->rc = out_entry(j, j->base, &j->items[i], j->path, 0);
        break;
    }
    if (j->r.why[0] && !j->why[0])
        snprintf(j->why, sizeof j->why, "%s", j->r.why);
}

static void bad_text(const struct diskjob_report *r, char *out, size_t room)    /* "5, 20, 999 ..." */
{
    unsigned long i, n = r->bad < 16 ? r->bad : 16;
    size_t at = 0;
    out[0] = 0;
    for (i = 0; i < n && at + 16 < room; i++)
        at += (size_t)snprintf(out + at, room - at, "%s%lu", i ? ", " : "", r->bad_list[i]);
    if (r->bad > n && at + 8 < room)
        snprintf(out + at, room - at, " ...");
}

static void restore_choose(void);
static struct { char computer[100], where[200]; char (*sets)[BK_SET]; int n; } listed;    /* (J_SETS's, for the next) */

static void bk_say(const struct job *j)         /* (headless too) what a backup's job came to */
{
    if (j->kind == J_BACKUP && j->rc == BK_OK)
        say("backup set=%s files=%u copied=%u kept=%u failed=%u folders=%u bytes=%llu", j->bk.set, j->bk.files,
            j->bk.copied, j->bk.kept, j->bk.failed, j->bk.folders, j->bk.bytes);
    else if (j->kind == J_RESTORE && j->rc == BK_OK)
        say("restored set=%s files=%u copied=%u failed=%u folders=%u to %s", j->bk.set, j->bk.files, j->bk.copied,
            j->bk.failed, j->bk.folders, j->path);
    else if (j->kind == J_SETS && j->rc == BK_OK)
        say("sets %d newest=%s", j->nsets, j->nsets > 0 ? j->sets[0] : "-");
    else
        say("%s failed %d %s", j->kind == J_BACKUP ? "backup" : j->kind == J_RESTORE ? "restore" : "sets", j->rc, j->why);
}

static void job_done(struct gui_window *owner, void *arg, int cancelled)
{
    struct job *j = arg;
    char name[40] = "", text[600], bad[200];
    int file_bad = 0, row = gui_list_get(dlist), choose = 0;
    (void)owner;
    if (j->f)
        file_bad = fclose(j->f) != 0;
    if (j->d) {                         /* what the disk holds now, behind what is said of the job */
        disk_name(j->d, name, sizeof name);
        if (j->kind == J_WRITE || j->kind == J_FORMAT)
            dos_forget();
        look(j->d);
        fill(row);
        looked_say(j->d);
    }
    if (cancelled || j->r.stopped) {
        if (j->kind == J_READ || j->kind == J_NEW)
            gui_unlink(j->path);        /* (a piece of an image is no image) */
        say("stopped %d", j->kind);
        gui_message(win, GUI_T("Stopped"), j->kind == J_WRITE ? GUI_T("Stopped. The disk has a part of the image: it is not to be used as it is.")
                                                              : GUI_T("Stopped."), GUI_MB_OK);
    } else if (j->kind == J_READ) {
        if (j->rc != 0 || file_bad) {
            gui_unlink(j->path);
            say("failed read %s", j->why);
            snprintf(text, sizeof text, GUI_T("No image was made. %s"), j->why[0] ? j->why : GUI_T("The file could not be written."));
        } else if (j->r.bad) {
            bad_text(&j->r, bad, sizeof bad);
            snprintf(text, sizeof text, GUI_T("The image of %s is in %s.\n\n%lu of its %lu sectors could not be read and are zeros "
                                              "in the image: %s."), name, j->path, j->r.bad, j->sectors, bad);
        } else {
            snprintf(text, sizeof text, GUI_T("The image of %s is in %s: %lu sectors, all of them read."), name, j->path, j->sectors);
        }
        if (j->rc == 0 && !file_bad)
            say("read disk=%02X sectors=%lu bad=%lu to %s", j->bios, j->sectors, j->r.bad, j->path);
        gui_message(win, GUI_T("The Disk's Image"), text, GUI_MB_OK);
    } else if (j->kind == J_WRITE) {
        if (j->rc == 0) {
            snprintf(text, sizeof text, GUI_T("%s has the image %s: %lu sectors written, and read back the same.%s"), name, j->path,
                     j->sectors, is_floppy(j->d) ? "" : GUI_T("\n\nRestart the PC before DOS uses this disk: it knows the disk as it was."));
            say("wrote disk=%02X sectors=%lu bad=0 verified=1", j->bios, j->sectors);
        } else if (j->r.bad) {
            bad_text(&j->r, bad, sizeof bad);
            snprintf(text, sizeof text, GUI_T("%s has NOT got the image whole: %lu sectors could not be written, or were not read "
                                              "back as written: %s."), name, j->r.bad, bad);
            say("wrote disk=%02X sectors=%lu bad=%lu verified=0", j->bios, j->sectors, j->r.bad);
        } else {
            snprintf(text, sizeof text, GUI_T("Nothing was written. %s"), j->why);
            say("failed write %s", j->why);
        }
        gui_message(win, GUI_T("The Image Written"), text, GUI_MB_OK);
    } else if (j->kind == J_CHECK) {
        if (j->r.bad) {
            bad_text(&j->r, bad, sizeof bad);
            snprintf(text, sizeof text, GUI_T("%lu of %s's %lu sectors could not be read: %s."), j->r.bad, name, j->sectors, bad);
        } else {
            snprintf(text, sizeof text, GUI_T("Every one of %s's %lu sectors could be read."), name, j->sectors);
        }
        say("checked disk=%02X sectors=%lu bad=%lu", j->bios, j->sectors, j->r.bad);
        gui_message(win, GUI_T("The Disk Checked"), text, GUI_MB_OK);
    } else if (j->kind == J_FORMAT) {
        if (j->rc == 0)
            say("formatted disk=%02X fat=%d label=%s", j->bios, j->fat, j->label[0] ? j->label : "-");
        else
            say("failed format %s", j->why);
        snprintf(text, sizeof text, j->rc == 0 ? GUI_T("%s is formatted: an empty disk.") : GUI_T("%s was not formatted. %s"), name, j->why);
        gui_message(win, GUI_T("The Disk Formatted"), text, GUI_MB_OK);
    } else if (j->kind == J_BACKUP || j->kind == J_RESTORE || j->kind == J_SETS) {
        bk_say(j);
        if (j->kind == J_SETS && j->rc == BK_OK) {
            free(listed.sets);
            listed.sets = j->sets, listed.n = j->nsets, j->sets = NULL;
            snprintf(listed.computer, sizeof listed.computer, "%s", j->computer);
            snprintf(listed.where, sizeof listed.where, "%s", j->where);
            choose = 1;
        } else if (j->rc != BK_OK) {
            snprintf(text, sizeof text, j->kind == J_BACKUP ? GUI_T("The backup was not made. %s.")
                     : j->kind == J_RESTORE ? GUI_T("The backup was not restored whole. %s.")
                     : GUI_T("The backups could not be listed. %s."), j->why);
            gui_message(win, GUI_T("Backups"), text, GUI_MB_OK);
        } else if (j->kind == J_BACKUP) {
            snprintf(text, sizeof text, GUI_T("The folder %s is backed up to %s, as %s: %u files copied (%lu KB)%s%s."),
                     j->from, j->computer, j->bk.set, j->bk.copied, (unsigned long)(j->bk.bytes >> 10),
                     j->bk.kept ? GUI_T(", the rest unchanged since the last") : "",
                     j->bk.failed ? GUI_T("; some files could not be read") : "");
            gui_message(win, GUI_T("Backed Up"), text, GUI_MB_OK);
        } else {
            snprintf(text, sizeof text, GUI_T("The backup %s is in %s: %u files%s."), j->bk.set, j->path, j->bk.copied,
                     j->bk.failed ? GUI_T(" (some could not be had)") : "");
            gui_message(win, GUI_T("Restored"), text, GUI_MB_OK);
        }
    } else if (j->kind == J_NEW) {
        if (j->rc != 0 || file_bad) {
            gui_unlink(j->path);
            say("failed new %s", j->why);
            snprintf(text, sizeof text, GUI_T("No image was made. %s"), j->why);
        } else {
            say("made %s files=%lu bytes=%lu left=%lu sectors=%lu", gui_path_name(j->path), j->files, j->bytes, j->left, j->sectors);
            if (j->left)
                snprintf(text, sizeof text, GUI_T("The image %s has %lu files.\n\n%lu files or folders were left out: their names "
                                                  "are not DOS's (eight letters and three)."), j->path, j->files, j->left);
            else
                snprintf(text, sizeof text, GUI_T("The image %s has the folder's %lu files."), j->path, j->files);
        }
        gui_message(win, GUI_T("The New Image"), text, GUI_MB_OK);
    } else {
        if (j->rc != 0) {
            say("failed copy %s", j->why);
            snprintf(text, sizeof text, GUI_T("%lu files were copied; then: %s"), j->files, j->why);
            gui_message(im.w ? im.w : win, GUI_T("Files Copied Out"), text, GUI_MB_OK);
        } else {
            say("copied out files=%lu bytes=%lu to %s", j->files, j->bytes, j->path);
            snprintf(text, sizeof text, GUI_T("%lu files copied to %s"), j->files, j->path);
            if (im.status)
                gui_set_text(im.status, text);
        }
    }
    free(j->items);
    free(j->sets);
    free(j);
    running = NULL;
    if (choose)                                 /* (the sets listed: which one, and where to) */
        restore_choose();
}

static void job_start(struct job *j, const char *title)
{
    running = j;
    if (j->d && im.w && !im.f && im.bios == j->bios)   /* (the volume shown is that disk's: not after this) */
        im_close();
    if (!gui_task_start(j->kind == J_EXTRACT && im.w ? im.w : win, title, work, j, job_done, GUI_TASK_DIALOG)) {
        work(NULL, j);                  /* (no threads to be had: done here) */
        job_done(win, j, 0);
    }
}

/* ---- the commands */

static const char *const image_kinds[] = { "Disk images|*.IMG;*.IMA;*.DSK;*.VFD", "All files|*.*", NULL };

static int choose_image(struct gui_window *owner, const char *title, int save, char *path, int room)
{
    struct gui_file_opts o;
    memset(&o, 0, sizeof o);
    o.filters = image_kinds;
    return gui_file_dialog_ex(owner, title, save, path, room, &o);
}

/* A floppy's disk looked at for a job on it: 0 if there is none to work on (said). */
static int floppy_ready(struct disk *d, int row)
{
    char text[200], size[24], drive[24];
    if (!is_floppy(d))
        return 1;
    look(d);
    fill(row);
    if (d->looked == L_UNREAD) {
        say("no disk %02X", d->bios);
        gui_message(win, GUI_T("The Disk in the Drive"), GUI_T("The disk can't be read: is there one in the drive?\n\nA disk never formatted "
                                                    "needs a full format first, which Disk Tools does not do yet (DOS's FORMAT does)."),
                    GUI_MB_OK);
        return 0;
    }
    if (d->looked == L_OTHER) {
        size_text(d->sectors, size, sizeof size);
        size_text(d->total, drive, sizeof drive);
        snprintf(text, sizeof text, GUI_T("This is a %s disk in a %s drive. Disk Tools works on disks of their drive's own size "
                                          "only, so far."), size, drive);
        say("other size %02X sectors=%lu", d->bios, d->sectors);
        gui_message(win, GUI_T("The Disk in the Drive"), text, GUI_MB_OK);
        return 0;
    }
    return 1;
}

/* Can the disk be written at all? Its first sector read and written back as it was. 0, and why said. */
static int can_write(struct disk *d)
{
    unsigned char sec[512];
    char text[160];
    int e = dev_io(&d->bios, 0, sec, 1, 0);
    if (!e)
        e = dev_io(&d->bios, 0, sec, 1, 1);
    if (!e)
        return 1;
    if (e == E_OWN)
        snprintf(text, sizeof text, "%s", GUI_T("GLOS runs from this disk: it is never written."));
    else if (e == 3)
        snprintf(text, sizeof text, "%s", GUI_T("The disk is write-protected."));
    else
        snprintf(text, sizeof text, GUI_T("The disk can't be written (the BIOS says error %02Xh)."), e);
    say("refused write disk=%02X error=%X", d->bios, e);
    gui_message(win, GUI_T("The Disk Can't Be Written"), text, GUI_MB_OK);
    return 0;
}

static struct job *job_new(int kind, struct disk *d)
{
    struct job *j = calloc(1, sizeof *j);
    if (j) {
        j->kind = kind;
        j->d = d;
        j->bios = d ? d->bios : 0;
        j->sectors = d ? d->sectors : 0;
        if (d && is_floppy(d))          /* a track at a call (2.88 MB: half of one, a call's most being 32) */
            j->run = spt_of(d->total) > 32 ? spt_of(d->total) / 2 : spt_of(d->total);
    }
    return j;
}

static void to_image(struct disk *d, int row)
{
    static char path[300] = "C:\\DISK.IMG";
    struct job *j;
    if (!floppy_ready(d, row))
        return;
    if (d->sectors > 0x3FFFFFul) {      /* 2 GB less a sector: the most one of DOS's files holds */
        say("too large %02X", d->bios);
        gui_message(win, GUI_T("The Disk's Size"), GUI_T("The disk is larger than 2 GB: its image does not fit one of DOS's files. "
                                                    "Images in parts come with a later version."), GUI_MB_OK);
        return;
    }
    if (!choose_image(win, GUI_T("The Disk's Image: Save As"), 1, path, sizeof path) || !(j = job_new(J_READ, d)))
        return;
    snprintf(j->path, sizeof j->path, "%s", path);
    if (!(j->f = gui_fopen(path, "wb"))) {
        gui_message(win, GUI_T("No File"), GUI_T("The file could not be made."), GUI_MB_OK);
        free(j);
        return;
    }
    job_start(j, GUI_T("Reading the Disk"));
}

static void from_image(struct disk *d, int row)
{
    static char path[300] = "C:\\";
    char name[40], size[24], other[24], text[400];
    struct job *j;
    FILE *f;
    long bytes;
    if (!floppy_ready(d, row) || !choose_image(win, GUI_T("The Image to Write"), 0, path, sizeof path))
        return;
    disk_name(d, name, sizeof name);
    if (!(f = gui_fopen(path, "rb"))) {
        gui_message(win, GUI_T("No File"), GUI_T("The image can't be read."), GUI_MB_OK);
        return;
    }
    fseek(f, 0, SEEK_END);
    bytes = ftell(f);
    if (bytes <= 0 || bytes % 512 || (unsigned long)bytes / 512 != d->total) {
        fclose(f);
        size_text(bytes > 0 ? (unsigned long)bytes / 512 : 0, size, sizeof size);
        size_text(d->total, other, sizeof other);
        snprintf(text, sizeof text, GUI_T("The image is of %s and %s holds %s: they must be the same."), size, name, other);
        say("refused image bytes=%ld disk=%02X", bytes, d->bios);
        gui_message(win, GUI_T("The Image's Size"), text, GUI_MB_OK);
        return;
    }
    size_text(d->total, size, sizeof size);
    snprintf(text, sizeof text, GUI_T("Everything on %s (%s) will be replaced by the image %s.\n\nGo on?"), name, size, path);
    if (gui_message(win, GUI_T("Write an Image"), text, GUI_MB_YESNO) != GUI_YES) {
        fclose(f);
        return;
    }
    if (!is_floppy(d)) {                /* a hard disk: asked once more, by what is on it */
        disk_about(d, other, sizeof other);
        snprintf(text, sizeof text, GUI_T("%s is a hard disk of %s (%s).\n\nWhat is on it now will be lost for good. Write the image "
                                          "over it?"), name, size, other);
        if (gui_message(win, GUI_T("Write an Image: Sure?"), text, GUI_MB_YESNO) != GUI_YES) {
            fclose(f);
            return;
        }
    }
    if (!can_write(d) || !(j = job_new(J_WRITE, d))) {
        fclose(f);
        return;
    }
    j->sectors = d->total;
    j->f = f;
    snprintf(j->path, sizeof j->path, "%s", path);
    job_start(j, GUI_T("Writing the Image"));
}

static struct gui_widget *f_label;
static char f_got[12];

static void format_done(struct gui_widget *b, int what)
{
    (void)what;                         /* (the button's own number is b->id) */
    if (b->id == 1)                        /* (read while the dialog is there) */
        snprintf(f_got, sizeof f_got, "%s", gui_get_text(f_label));
    gui_end_modal(b->win, b->id == 1 ? GUI_OK : GUI_CANCEL_R);
}

static void format(struct disk *d, int row)
{
    struct gui_window *dlg;
    struct gui_widget *box, *line;
    struct job *j;
    char name[40], text[200], size[24];
    unsigned i;
    look(d);                            /* (a disk that can't be read is not one a quick format helps) */
    fill(row);
    if (d->looked == L_UNREAD || d->looked == L_OTHER) {
        floppy_ready(d, row);
        return;
    }
    disk_name(d, name, sizeof name);
    size_text(d->total, size, sizeof size);
    dlg = gui_window(GUI_T("Format"), 380, 0, GUI_W_DIALOG);
    dlg->owner = win;
    box = gui_vbox(dlg->root, 6, 8);
    snprintf(text, sizeof text, GUI_T("%s: an empty %s disk is made of the one in the drive."), name, size);
    gui_label(box, text);
    gui_label(box, GUI_T("Everything on it will be lost."));
    f_label = gui_field(gui_row(gui_form(box), GUI_T("&Label:"), NULL), 12);
    gui_set_text(f_label, d->label);
    line = gui_hbox(box, 6, 0);
    gui_space(line, 0, 0);
    gui_button(line, GUI_T("&Format"), format_done, 1)->flags |= GUI_DEFAULT;
    gui_button(line, GUI_T("Cancel"), format_done, 2)->flags |= GUI_CANCEL;
    gui_focus(f_label);
    if (gui_modal(dlg) != GUI_OK || !can_write(d) || !(j = job_new(J_FORMAT, d)))
        return;
    j->sectors = d->total;
    for (i = 0; f_got[i] && i < 11; i++)        /* DOS's label: capitals, and none of the signs a name can't have */
        j->label[i] = (char)(f_got[i] >= 'a' && f_got[i] <= 'z' ? f_got[i] - 32 : strchr("\"*+,./:;<=>?[\\]|", f_got[i]) ? '_' : f_got[i]);
    job_start(j, GUI_T("Formatting"));
}

static void inside(struct disk *d, int row)
{
    struct fatimg *v;
    char why[96], name[40], text[200];
    if (!floppy_ready(d, row))
        return;
    disk_name(d, name, sizeof name);
    if (!(v = fatimg_open(dev_io, &d->bios, why, sizeof why))) {
        snprintf(text, sizeof text, GUI_T("%s has no file system Disk Tools reads (FAT12 or FAT16): %s"), name, why);
        say("not opened %02X", d->bios);
        gui_message(win, GUI_T("No File System"), text, GUI_MB_OK);
        return;
    }
    im_open(v, NULL, d->bios, name, "C:\\");
}

static void open_image(void)
{
    static char path[300] = "C:\\";
    struct fatimg *v;
    char why[96], text[300];
    FILE *f;
    if (!choose_image(win, GUI_T("Open an Image"), 0, path, sizeof path))
        return;
    if (!(f = gui_fopen(path, "rb"))) {
        gui_message(win, GUI_T("No File"), GUI_T("The image can't be read."), GUI_MB_OK);
        return;
    }
    if (!(v = fatimg_open(file_io, f, why, sizeof why))) {
        fclose(f);
        say("not opened %s", gui_path_name(path));
        snprintf(text, sizeof text, GUI_T("%s has no file system Disk Tools reads (FAT12 or FAT16): %s"), gui_path_name(path), why);
        gui_message(win, GUI_T("No File System"), text, GUI_MB_OK);
        return;
    }
    snprintf(text, sizeof text, "%s", path);
    gui_path_up(text);
    im_open(v, f, 0, gui_path_name(path), text);
}

static struct gui_widget *n_folder, *n_size, *n_label;
static struct { char folder[300], label[12]; int size; } n_got;
static const unsigned long n_sizes[] = { 720, 1440, 2400, 2880, 5760 };

static void new_done(struct gui_widget *b, int what)
{
    (void)what;                         /* (the button's own number is b->id) */
    if (b->id == 3) {
        char p[300];
        snprintf(p, sizeof p, "%s", gui_get_text(n_folder));
        if (gui_folder_dialog(b->win, GUI_T("The Folder for the Image"), p, sizeof p))
            gui_set_text(n_folder, p);
        return;
    }
    if (b->id == 1) {
        snprintf(n_got.folder, sizeof n_got.folder, "%s", gui_get_text(n_folder));
        snprintf(n_got.label, sizeof n_got.label, "%s", gui_get_text(n_label));
        n_got.size = gui_value(n_size);
    }
    gui_end_modal(b->win, b->id == 1 ? GUI_OK : GUI_CANCEL_R);
}

static void new_from_folder(void)
{
    static char path[300] = "C:\\NEW.IMG";
    struct gui_window *dlg;
    struct gui_widget *box, *form, *line;
    struct job *j;
    unsigned i;
    dlg = gui_window(GUI_T("New Image from a Folder"), 440, 0, GUI_W_DIALOG);
    dlg->owner = win;
    box = gui_vbox(dlg->root, 6, 8);
    form = gui_form(box);
    line = gui_row(form, GUI_T("&Folder:"), NULL);
    n_folder = gui_field(line, 30);
    gui_set_text(n_folder, n_got.folder[0] ? n_got.folder : "C:\\");
    gui_button(line, GUI_T("&Choose..."), new_done, 3);
    n_size = gui_choice(gui_row(form, GUI_T("&Size:"), NULL), 12);
    gui_choice_add(n_size, "360 KB"), gui_choice_add(n_size, "720 KB"), gui_choice_add(n_size, "1.2 MB");
    gui_choice_add(n_size, "1.44 MB"), gui_choice_add(n_size, "2.88 MB");
    gui_set_value(n_size, 3);
    n_label = gui_field(gui_row(form, GUI_T("&Label:"), NULL), 12);
    line = gui_hbox(box, 6, 0);
    gui_space(line, 0, 0);
    gui_button(line, GUI_T("OK"), new_done, 1)->flags |= GUI_DEFAULT;
    gui_button(line, GUI_T("Cancel"), new_done, 2)->flags |= GUI_CANCEL;
    gui_focus(n_folder);
    if (gui_modal(dlg) != GUI_OK)
        return;
    if (!gui_is_dir(n_got.folder)) {
        say("no folder %s", n_got.folder);
        gui_message(win, GUI_T("No Such Folder"), GUI_T("There is no such folder."), GUI_MB_OK);
        return;
    }
    if (!choose_image(win, GUI_T("The New Image: Save As"), 1, path, sizeof path) || !(j = job_new(J_NEW, NULL)))
        return;
    j->sectors = n_sizes[n_got.size >= 0 && n_got.size < 5 ? n_got.size : 3];
    snprintf(j->path, sizeof j->path, "%s", path);
    snprintf(j->from, sizeof j->from, "%s", n_got.folder);
    for (i = 0; n_got.label[i] && i < 11; i++)
        j->label[i] = (char)(n_got.label[i] >= 'a' && n_got.label[i] <= 'z' ? n_got.label[i] - 32
                             : strchr("\"*+,./:;<=>?[\\]|", n_got.label[i]) ? '_' : n_got.label[i]);
    if (!(j->f = gui_fopen(path, "w+b"))) {
        gui_message(win, GUI_T("No File"), GUI_T("The file could not be made."), GUI_MB_OK);
        free(j);
        return;
    }
    job_start(j, GUI_T("Making the Image"));
}

/* ---- backups: the dialogs */

static struct gui_widget *b_folder, *b_computer, *b_where, *b_what, *b_sets, *b_to, *b_again, *b_at, *b_day;
static struct { char folder[260], computer[100], where[200], to[260], at[8]; int what, set, again, day; } b_got;
static const char *const day_names[7] = { "sun", "mon", "tue", "wed", "thu", "fri", "sat" };

/* The time typed: HH:MM (or H:MM), into minutes of the day; -1 not one. */
static int at_minutes(const char *t)
{
    int h = 0, m = 0, k = 0;
    while (*t >= '0' && *t <= '9' && k < 2)
        h = h * 10 + (*t++ - '0'), k++;
    if (!k || *t++ != ':' || !(t[0] >= '0' && t[0] <= '9' && t[1] >= '0' && t[1] <= '9') || t[2])
        return -1;
    m = (t[0] - '0') * 10 + (t[1] - '0');
    return h < 24 && m < 60 ? h * 60 + m : -1;
}

static void where_for(const char *folder, char *out, size_t room)      /* glos-backups/ and the folder's name */
{
    const char *name = gui_path_name(folder);
    snprintf(out, room, "glos-backups/%s", name && name[0] && strcmp(name, "\\") ? name : "C");
}

static int b_check(struct gui_window *owner)    /* the computer named rightly: 1 */
{
    char host[64], user[32], key[100];
    unsigned port;
    if (bknet_computer(b_got.computer, host, sizeof host, &port, user, sizeof user, key, sizeof key) == 0 && b_got.where[0])
        return 1;
    say("backup no computer \"%s\" where \"%s\"", b_got.computer, b_got.where);
    gui_message(owner, GUI_T("Backups"), GUI_T("Name the computer by a saved session of the Terminal's, or as "
                                             "user@computer:port, and the folder there."), GUI_MB_OK);
    return 0;
}

static void backup_done(struct gui_widget *b, int what)
{
    (void)what;                                 /* (the button's own number is b->id) */
    if (b->id == 3) {
        char p[260], w[200];
        snprintf(p, sizeof p, "%s", gui_get_text(b_folder));
        if (gui_folder_dialog(b->win, GUI_T("The Folder to Back Up"), p, sizeof p)) {
            gui_set_text(b_folder, p);
            where_for(p, w, sizeof w);
            gui_set_text(b_where, w);
        }
        return;
    }
    if (b->id == 1) {
        snprintf(b_got.folder, sizeof b_got.folder, "%s", b_folder ? gui_get_text(b_folder) : "");
        snprintf(b_got.computer, sizeof b_got.computer, "%s", gui_get_text(b_computer));
        snprintf(b_got.where, sizeof b_got.where, "%s", gui_get_text(b_where));
        b_got.what = b_what ? gui_value(b_what) : 0;
        b_got.again = b_again ? gui_value(b_again) : 0;
        b_got.day = b_day ? gui_value(b_day) : 1;
        snprintf(b_got.at, sizeof b_got.at, "%s", b_at ? gui_get_text(b_at) : "");
        if (b_got.again >= 2 && at_minutes(b_got.at) < 0) {
            say("backup no time \"%s\"", b_got.at);
            gui_message(b->win, GUI_T("Backups"), GUI_T("The time is written as hours and minutes: 21:30."), GUI_MB_OK);
            gui_focus(b_at);
            return;
        }
        if (b_folder && !gui_is_dir(b_got.folder)) {    /* (said with the dialog still there, to be put right) */
            say("no folder %s", b_got.folder);
            gui_message(b->win, GUI_T("No Such Folder"), GUI_T("There is no such folder."), GUI_MB_OK);
            return;
        }
        if (!b_check(b->win))
            return;
    }
    gui_end_modal(b->win, b->id == 1 ? GUI_OK : GUI_CANCEL_R);
}

static void computer_row(struct gui_widget *form)       /* the computer: a saved session's name, or an address */
{
    char names[16][24];
    int n = bknet_sessions(names, 16);
    b_computer = gui_field(gui_row(form, GUI_T("&Computer:"), NULL), 30);
    gui_tip(b_computer, GUI_T("A saved session's name (the Terminal's Sessions), or user@computer:port"));
    gui_set_text(b_computer, b_got.computer[0] ? b_got.computer : n > 0 ? names[0] : "");
}

/* The backup asked for, again at its times (API 2.0's SHELL SCHEDULE: GLOS starts DISKS -backup then, with no
   window): "backup-" and the folder's name. 0, or -1 (said). */
static int schedule_backup(void)
{
    char name[24], when[24], command[200], dname[16];
    const char *f = gui_path_name(b_got.folder);
    int i, k = 0, r;
    for (i = 0; f && f[i] && k < 15; i++)       /* (a name's letters: letters and digits) */
        if ((f[i] >= 'A' && f[i] <= 'Z') || (f[i] >= 'a' && f[i] <= 'z') || (f[i] >= '0' && f[i] <= '9'))
            dname[k++] = f[i];
    dname[k] = 0;
    snprintf(name, sizeof name, "backup-%s", k ? dname : "C");
    if (b_got.again == 1)
        snprintf(when, sizeof when, "start");
    else if (b_got.again == 2)
        snprintf(when, sizeof when, "daily %02d:%02d", at_minutes(b_got.at) / 60, at_minutes(b_got.at) % 60);
    else
        snprintf(when, sizeof when, "weekly %s %02d:%02d", day_names[b_got.day >= 0 && b_got.day < 7 ? b_got.day : 1],
                 at_minutes(b_got.at) / 60, at_minutes(b_got.at) % 60);
    {
        char exe[260];
        gui_path_join(exe, sizeof exe, glos_home(), "DISKS.EXE");
        r = snprintf(command, sizeof command, "%s -backup %s %s %s%s", exe, b_got.folder, b_got.computer, b_got.where,
                     b_got.what == 1 ? " -changes" : "");
    }
    if (r > 160) {
        say("schedule too long %d", r);
        gui_message(win, GUI_T("Backups"), GUI_T("That backup can't be scheduled: its folders' names are too long "
                                                 "(160 letters in all, at most)."), GUI_MB_OK);
        return -1;
    }
    if ((r = glos_schedule_set(name, when, command)) != 0) {
        say("schedule refused %s %d", name, r);
        gui_message(win, GUI_T("Backups"), r == GLOS_E_ROOM ? GUI_T("GLOS has as many things scheduled as it can keep: "
                                                                    "take one away first (Backup, Scheduled Backups).")
                                                            : GUI_T("GLOS would not schedule that backup."), GUI_MB_OK);
        return -1;
    }
    say("scheduled %s %s", name, when);
    return 0;
}

/* Backup, Scheduled Backups: the schedule's backups (its "backup-" names), their times, folders, computers and
   last runs; one taken off the schedule. */
static struct gui_widget *s_list;
static char s_names[GLOS_SCHEDULE_MAX][24];
static int s_n;

static void scheduled_fill(void)
{
    char *list = malloc(8192), *p, *nl;
    unsigned length = 0;
    s_n = 0;
    gui_list_clear(s_list);
    if (list && glos_schedule_list(list, 8192, &length) == 0)
        for (p = list; *p && s_n < GLOS_SCHEDULE_MAX; p = nl + 1) {
            char *f[5], *e, folder[100] = "", to[160] = "", last[40];
            const char *cells[4];
            int k = 0;
            if (!(nl = strchr(p, '\n')))
                break;
            *nl = 0;
            f[k++] = p;
            for (e = p; *e && k < 5; e++)
                if (*e == '\t')
                    *e = 0, f[k++] = e + 1;
            if (k < 5 || strncmp(f[0], "backup-", 7))
                continue;
            if ((e = strstr(f[2], " -backup ")) != NULL) {     /* FOLDER COMPUTER WHERE */
                char a[100] = "", b[100] = "", c[100] = "";
                sscanf(e + 9, "%99s %99s %99s", a, b, c);
                snprintf(folder, sizeof folder, "%s", a);
                snprintf(to, sizeof to, "%s: %s", b, c);
            }
            {
                time_t t = (time_t)strtoul(f[4], NULL, 10);
                struct tm *tm = t ? localtime(&t) : NULL;
                if (tm)
                    strftime(last, sizeof last, "%Y-%m-%d %H:%M", tm);
                else
                    snprintf(last, sizeof last, "%s", GUI_T("not yet"));
            }
            snprintf(s_names[s_n++], 24, "%s", f[0]);
            cells[0] = folder, cells[1] = f[1], cells[2] = to, cells[3] = last;
            gui_list_add(s_list, cells, NULL, NULL);
        }
    free(list);
    if (s_n)
        gui_list_set(s_list, 0);
    say("scheduled list %d", s_n);
}

static void scheduled_done(struct gui_widget *b, int what)
{
    (void)what;
    if (b->id == 3) {                           /* Remove */
        int row = gui_list_get(s_list);
        if (row >= 0 && row < s_n) {
            int r = glos_schedule_remove(s_names[row]);
            say("unscheduled %s %d", s_names[row], r);
            scheduled_fill();
        }
        return;
    }
    gui_end_modal(b->win, GUI_OK);
}

static void scheduled(void)
{
    static const char *const heads[] = { GUI_N("Folder"), GUI_N("When"), GUI_N("To"), GUI_N("Last backed up") };
    static const int widths[] = { 100, 110, 160, 110 };
    struct gui_window *dlg;
    struct gui_widget *box, *line;
    dlg = gui_window(GUI_T("Scheduled Backups"), 520, 0, GUI_W_DIALOG);
    dlg->owner = win;
    box = gui_vbox(dlg->root, 6, 8);
    s_list = gui_list(box, 4, heads, widths);
    gui_size(s_list, 0, 120);
    gui_label(box, GUI_T("GLOS backs these up at their times, with no window (a computer it has met before)."));
    line = gui_hbox(box, 6, 0);
    gui_button(line, GUI_T("&Remove"), scheduled_done, 3);
    gui_space(line, 0, 0);
    gui_button(line, GUI_T("Close"), scheduled_done, 1)->flags |= GUI_DEFAULT | GUI_CANCEL;
    scheduled_fill();
    gui_focus(s_list);
    gui_modal(dlg);
    s_list = NULL;
}

static void backup_ask(void)
{
    struct gui_window *dlg;
    struct gui_widget *box, *form, *line;
    struct job *j;
    char w[200];
    dlg = gui_window(GUI_T("Back Up a Folder"), 460, 0, GUI_W_DIALOG);
    dlg->owner = win;
    box = gui_vbox(dlg->root, 6, 8);
    form = gui_form(box);
    line = gui_row(form, GUI_T("&Folder:"), NULL);
    b_folder = gui_field(line, 30);
    gui_set_text(b_folder, b_got.folder[0] ? b_got.folder : "C:\\");
    gui_button(line, GUI_T("C&hoose..."), backup_done, 3);
    computer_row(form);
    b_where = gui_field(gui_row(form, GUI_T("Folder &there:"), NULL), 30);
    where_for(gui_get_text(b_folder), w, sizeof w);
    gui_set_text(b_where, b_got.where[0] ? b_got.where : w);
    b_what = gui_choice(gui_row(form, GUI_T("&What:"), NULL), 24);
    gui_choice_add(b_what, GUI_T("Every file"));
    gui_choice_add(b_what, GUI_T("What changed since the last"));
    gui_set_value(b_what, b_got.what);
    b_again = gui_choice(gui_row(form, GUI_T("&Again:"), NULL), 24);
    gui_choice_add(b_again, GUI_T("No: this once"));
    gui_choice_add(b_again, GUI_T("Each time GLOS starts"));
    gui_choice_add(b_again, GUI_T("Every day"));
    gui_choice_add(b_again, GUI_T("Every week"));
    gui_set_value(b_again, b_got.again);
    line = gui_row(form, GUI_T("Ti&me:"), NULL);
    b_at = gui_field(line, 6);
    gui_set_text(b_at, b_got.at[0] ? b_got.at : "21:00");
    gui_label(line, GUI_T("On &day:"));
    b_day = gui_choice(line, 12);
    {
        static const char *const days[7] = { GUI_N("Sunday"), GUI_N("Monday"), GUI_N("Tuesday"), GUI_N("Wednesday"),
                                             GUI_N("Thursday"), GUI_N("Friday"), GUI_N("Saturday") };
        int d;
        for (d = 0; d < 7; d++)
            gui_choice_add(b_day, GUI_T(days[d]));
    }
    gui_set_value(b_day, b_got.again ? b_got.day : 1);
    gui_tip(b_at, GUI_T("Every day, or every week on the day chosen: at this time"));
    line = gui_hbox(box, 6, 0);
    gui_space(line, 0, 0);
    gui_button(line, GUI_T("OK"), backup_done, 1)->flags |= GUI_DEFAULT;
    gui_button(line, GUI_T("Cancel"), backup_done, 2)->flags |= GUI_CANCEL;
    gui_focus(b_folder);
    if (gui_modal(dlg) != GUI_OK) {
        b_folder = b_what = b_again = b_at = b_day = NULL;
        say("backup left");
        return;
    }
    b_folder = b_what = b_again = b_at = b_day = NULL;
    if (b_got.again && schedule_backup() != 0)
        return;
    if (!(j = job_new(J_BACKUP, NULL)))
        return;
    snprintf(j->from, sizeof j->from, "%s", b_got.folder);
    snprintf(j->computer, sizeof j->computer, "%s", b_got.computer);
    snprintf(j->where, sizeof j->where, "%s", b_got.where);
    j->changes = b_got.what == 1;
    job_start(j, GUI_T("Backing Up"));
}

static void restore_ask(void)                   /* the computer and the folder there: its sets listed (J_SETS) */
{
    struct gui_window *dlg;
    struct gui_widget *box, *form, *line;
    struct job *j;
    dlg = gui_window(GUI_T("Restore a Backup"), 460, 0, GUI_W_DIALOG);
    dlg->owner = win;
    box = gui_vbox(dlg->root, 6, 8);
    form = gui_form(box);
    computer_row(form);
    b_where = gui_field(gui_row(form, GUI_T("Folder &there:"), NULL), 30);
    gui_set_text(b_where, b_got.where[0] ? b_got.where : "glos-backups/");
    line = gui_hbox(box, 6, 0);
    gui_space(line, 0, 0);
    gui_button(line, GUI_T("OK"), backup_done, 1)->flags |= GUI_DEFAULT;
    gui_button(line, GUI_T("Cancel"), backup_done, 2)->flags |= GUI_CANCEL;
    gui_focus(b_computer);
    b_folder = b_what = b_again = b_at = b_day = NULL;
    if (gui_modal(dlg) != GUI_OK) {
        say("restore left");
        return;
    }
    if (!(j = job_new(J_SETS, NULL)))
        return;
    snprintf(j->computer, sizeof j->computer, "%s", b_got.computer);
    snprintf(j->where, sizeof j->where, "%s", b_got.where);
    job_start(j, GUI_T("Looking for Backups"));
}

static void sets_done(struct gui_widget *b, int what)
{
    (void)what;
    if (b->id == 3) {
        char p[260];
        snprintf(p, sizeof p, "%s", gui_get_text(b_to));
        if (gui_folder_dialog(b->win, GUI_T("Restore Into"), p, sizeof p))
            gui_set_text(b_to, p);
        return;
    }
    if (b->id == 1) {
        b_got.set = gui_list_get(b_sets);
        snprintf(b_got.to, sizeof b_got.to, "%s", gui_get_text(b_to));
    }
    gui_end_modal(b->win, b->id == 1 ? GUI_OK : GUI_CANCEL_R);
}

static void restore_choose(void)                /* one of the sets listed, and where to */
{
    static const char *const heads[] = { GUI_N("Backup made") };
    static const int widths[] = { 300 };
    struct gui_window *dlg;
    struct gui_widget *box, *line;
    struct job *j;
    int i;
    if (!listed.n) {
        gui_message(win, GUI_T("Backups"), GUI_T("There are no backups in that folder."), GUI_MB_OK);
        return;
    }
    dlg = gui_window(GUI_T("Restore a Backup"), 400, 0, GUI_W_DIALOG);
    dlg->owner = win;
    box = gui_vbox(dlg->root, 6, 8);
    gui_label(box, GUI_T("&Backups, the newest first:"));
    b_sets = gui_list(box, 1, heads, widths);
    gui_size(b_sets, 0, 140);
    for (i = 0; i < listed.n; i++) {            /* (20261122-163000: 2026-11-22 16:30:00) */
        char t[48];
        const char *c[1];
        const char *n = listed.sets[i];
        snprintf(t, sizeof t, "%.4s-%.2s-%.2s %.2s:%.2s:%.2s%s", n, n + 4, n + 6, n + 9, n + 11, n + 13, n + 15);
        c[0] = t;
        gui_list_add(b_sets, c, NULL, NULL);
    }
    gui_list_set(b_sets, 0);
    line = gui_row(gui_form(box), GUI_T("&Into:"), NULL);
    b_to = gui_field(line, 26);
    gui_set_text(b_to, b_got.to[0] ? b_got.to : "C:\\RESTORED");
    gui_button(line, GUI_T("C&hoose..."), sets_done, 3);
    gui_label(box, GUI_T("Files there of the same names are written over."));
    line = gui_hbox(box, 6, 0);
    gui_space(line, 0, 0);
    gui_button(line, GUI_T("Restore"), sets_done, 1)->flags |= GUI_DEFAULT;
    gui_button(line, GUI_T("Cancel"), sets_done, 2)->flags |= GUI_CANCEL;
    gui_focus(b_sets);
    say("sets shown %d", listed.n);
    if (gui_modal(dlg) != GUI_OK || b_got.set < 0 || b_got.set >= listed.n || !b_got.to[0]) {
        say("restore left");
        return;
    }
    if (!(j = job_new(J_RESTORE, NULL)))
        return;
    snprintf(j->computer, sizeof j->computer, "%s", listed.computer);
    snprintf(j->where, sizeof j->where, "%s", listed.where);
    snprintf(j->set, sizeof j->set, "%s", listed.sets[b_got.set]);
    snprintf(j->path, sizeof j->path, "%s", b_got.to);
    job_start(j, GUI_T("Restoring"));
}

static void command(struct gui_window *w, int id)
{
    struct disk *d = current();
    int row = gui_list_get(dlist);
    (void)w;
    if (running)
        return;
    switch (id) {
    case C_TOIMAGE:
        if (d)
            to_image(d, row);
        break;
    case C_FROMIMAGE:
        if (d)
            from_image(d, row);
        break;
    case C_CHECK:
        if (d && floppy_ready(d, row)) {
            struct job *j = job_new(J_CHECK, d);
            if (j)
                job_start(j, GUI_T("Checking the Disk"));
        }
        break;
    case C_FORMAT:
        if (d && is_floppy(d))
            format(d, row);
        break;
    case C_INSIDE:
        if (d && is_floppy(d))
            inside(d, row);
        break;
    case C_REFRESH:
        find_disks();
        if (row >= 0 && row < ndisks && is_floppy(&disks[row]))
            look(&disks[row]);
        fill(row);
        say("disks %d", ndisks);
        if (row >= 0 && row < ndisks)
            looked_say(&disks[row]);
        break;
    case C_OPEN: open_image(); break;
    case C_NEW: new_from_folder(); break;
    case C_BACKUP: backup_ask(); break;
    case C_RESTORE: restore_ask(); break;
    case C_SCHEDULED: scheduled(); break;
    case C_EXIT:
        im_close();
        gui_close(win);
        break;
    }
}

static void update(struct gui_window *w)
{
    struct disk *d = current();
    gui_command_enable(w, C_TOIMAGE, d && !running);
    gui_command_enable(w, C_FROMIMAGE, d && d->bios != 0x80 && !running);
    gui_command_enable(w, C_CHECK, d && !running);
    gui_command_enable(w, C_FORMAT, d && is_floppy(d) && !running);
    gui_command_enable(w, C_INSIDE, d && is_floppy(d) && !running);
    gui_command_enable(w, C_OPEN, !running);
    gui_command_enable(w, C_NEW, !running);
    gui_command_enable(w, C_BACKUP, !running);
    gui_command_enable(w, C_RESTORE, !running);
    gui_command_enable(w, C_SCHEDULED, !running);
}

static void dlist_on(struct gui_widget *w, int what)
{
    (void)w;
    if (what == GUI_ON_SELECT)
        show_disk();
    else if (what == GUI_ON_ACTIVATE && current() && is_floppy(current()))
        command(win, C_INSIDE);
}

static void on_close(struct gui_window *w)
{
    if (running)
        return;
    im_close();
    gui_close(w);
}

static void build(void)
{
    static const char *const heads[] = { GUI_N("Disk"), GUI_N("Size"), GUI_N("What is on it") };
    static const int widths[] = { 110, 70, 360 };
    static const char *const pheads[] = { GUI_N("No."), GUI_N("Kind"), GUI_N("Starts at sector"), GUI_N("Size"), GUI_N("Starts the PC") };
    static const int pwidths[] = { 40, 130, 120, 90, 100 };
    struct gui_menubar *bar;
    struct gui_menu *m;
    struct gui_widget *tb;
    win = gui_window(GUI_T("Disk Tools"), 600, 380, 0);
    win->on_command = command;
    win->on_update = update;
    win->on_close = on_close;
    gui_command(win, C_TOIMAGE, GUI_T("&Image of the Disk..."), "Ctrl+S", gui_glyph("save"), GUI_T("Read the disk into an image file"));
    gui_command(win, C_FROMIMAGE, GUI_T("&Write an Image to the Disk..."), "Ctrl+W", gui_glyph("upload"),
                GUI_T("Write an image file to the disk"));
    gui_command(win, C_CHECK, GUI_T("&Check the Disk"), "Ctrl+K", gui_glyph("check"), GUI_T("Read every sector of the disk"));
    gui_command(win, C_FORMAT, GUI_T("&Format..."), NULL, NULL, NULL);
    gui_command(win, C_INSIDE, GUI_T("&Look Inside"), "Enter", gui_glyph("folder"), GUI_T("The floppy's folders and files"));
    gui_command(win, C_REFRESH, GUI_T("Look &Again"), "F5", gui_glyph("refresh"), GUI_T("Look at the disks again"));
    gui_command(win, C_OPEN, GUI_T("&Open an Image..."), "Ctrl+O", gui_glyph("open"), GUI_T("Look inside an image file"));
    gui_command(win, C_NEW, GUI_T("&New Image from a Folder..."), "Ctrl+N", gui_glyph("new"), GUI_T("Make an image of a folder's files"));
    gui_command(win, C_EXIT, GUI_T("E&xit"), "Alt+F4", NULL, NULL);
    gui_command(win, C_BACKUP, GUI_T("&Back Up a Folder..."), "Ctrl+B", gui_glyph("upload"),
                GUI_T("Copy a folder to another computer"));
    gui_command(win, C_RESTORE, GUI_T("&Restore a Backup..."), "Ctrl+R", NULL, GUI_T("Copy a backup back to this PC"));
    gui_command(win, C_SCHEDULED, GUI_T("&Scheduled Backups..."), NULL, NULL, GUI_T("The backups GLOS makes at their times"));
    bar = gui_menubar(win);
    m = gui_menu(bar, GUI_T("&Disk"));
    gui_menu_command(m, C_TOIMAGE), gui_menu_command(m, C_FROMIMAGE);
    gui_menu_sep(m);
    gui_menu_command(m, C_CHECK), gui_menu_command(m, C_FORMAT), gui_menu_command(m, C_INSIDE);
    gui_menu_sep(m);
    gui_menu_command(m, C_REFRESH);
    gui_menu_sep(m);
    gui_menu_command(m, C_EXIT);
    m = gui_menu(bar, GUI_T("&Image"));
    gui_menu_command(m, C_OPEN), gui_menu_command(m, C_NEW);
    m = gui_menu(bar, GUI_T("&Backup"));
    gui_menu_command(m, C_BACKUP), gui_menu_command(m, C_RESTORE);
    gui_menu_sep(m);
    gui_menu_command(m, C_SCHEDULED);
    tb = gui_toolbar(win);
    gui_toolbar_command(tb, C_TOIMAGE), gui_toolbar_command(tb, C_FROMIMAGE), gui_toolbar_command(tb, C_CHECK);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_OPEN), gui_toolbar_command(tb, C_NEW);
    gui_toolbar_sep(tb);
    gui_toolbar_command(tb, C_REFRESH);
    dlist = gui_list(win->root, 3, heads, widths);
    gui_grow(dlist, 1);
    gui_on(dlist, dlist_on, 0);
    about = gui_label(gui_hbox(win->root, 6, 4), "");
    plist = gui_list(win->root, 5, pheads, pwidths);
    gui_grow(plist, 1);
    status = gui_status(win);
    gui_set_text(status, GUI_T("The disk GLOS runs from is never written"));
}

/* DISKS -backup FOLDER COMPUTER WHERE [-changes] / DISKS -restore COMPUTER WHERE SET|newest FOLDER: no window (a
   computer not met before is not believed: Disk Tools' window, or SSH.EXE, meets it first). 0 done, 1 not. */
static int headless(int argc, char **argv)
{
    struct job j;
    int restore = !strcmp(argv[1], "-restore");
    memset(&j, 0, sizeof j);
    if (!restore && argc >= 5) {
        j.kind = J_BACKUP;
        snprintf(j.from, sizeof j.from, "%s", argv[2]);
        snprintf(j.computer, sizeof j.computer, "%s", argv[3]);
        snprintf(j.where, sizeof j.where, "%s", argv[4]);
        j.changes = argc > 5 && !strcmp(argv[5], "-changes");
    } else if (restore && argc >= 6) {
        j.kind = J_SETS;
        snprintf(j.computer, sizeof j.computer, "%s", argv[2]);
        snprintf(j.where, sizeof j.where, "%s", argv[3]);
        snprintf(j.path, sizeof j.path, "%s", argv[5]);
        bk_work(&j);
        bk_say(&j);
        if (j.rc != BK_OK || j.nsets <= 0) {
            free(j.sets);
            if (conn)
                remote_close(conn);
            return 1;
        }
        snprintf(j.set, sizeof j.set, "%s", strcmp(argv[4], "newest") ? argv[4] : j.sets[0]);
        free(j.sets), j.sets = NULL;
        j.kind = J_RESTORE;
    } else {
        printf("DISKS -backup FOLDER COMPUTER WHERE [-changes]\nDISKS -restore COMPUTER WHERE SET|newest FOLDER\n");
        return 2;
    }
    bk_work(&j);
    bk_say(&j);
    if (conn)
        remote_close(conn);
    return j.rc == BK_OK ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc > 1 && (!strcmp(argv[1], "-backup") || !strcmp(argv[1], "-restore")))
        return headless(argc, argv);
    if (gui_init(argc, argv) != 0) {
        printf("DISKS no GLOS\n");
        return 1;
    }
    build();
    find_disks();
    fill(0);
    gui_show(win, 1);
    gui_focus(dlist);
    say("disks %d", ndisks);
    gui_run();
    if (conn)
        remote_close(conn);
    printf("DISKS end\n");
    return 0;
}
