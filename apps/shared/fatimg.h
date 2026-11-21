/* A FAT volume read, and added to: for Disk Tools (phase F; docs/v1/apps/disk-tools.md): an image file's, a
   floppy's. FAT12 and FAT16 as Microsoft's specification lays them out; long names are read. GLOS's own small
   reader (the plan's: for images, not the disks DOS has), over a call that reads and writes sectors: one code for
   the app and for the host's tests, which hold what it makes against mtools and fsck.fat. */
#ifndef FATIMG_H
#define FATIMG_H

/* `count` sectors of 512 bytes from sector `lba` of the volume read into data, or (write nonzero) written from
   it: 0, or anything else if it could not be done. */
typedef int (*fatimg_io)(void *ctx, unsigned long lba, unsigned char *data, unsigned count, int write);

struct fatimg;

/* A volume opened: its first sector read and held to the rules, its FAT read. NULL and why (a line). */
struct fatimg *fatimg_open(fatimg_io io, void *ctx, char *why, int room);
/* Closed: if it was added to, its FATs written. 0; -1: they could not be written. */
int fatimg_close(struct fatimg *v);

struct fatimg_info {
    int fat;                            /* 12 or 16 */
    unsigned long sectors, clusters, free_clusters;
    unsigned cluster_bytes, root_entries;
    char label[12];                     /* the volume's name ("" for none) */
};
void fatimg_info(struct fatimg *v, struct fatimg_info *out);

struct fatimg_entry {
    char name[256];                     /* its long name where it has one (UTF-8), else its DOS name as it is shown */
    char dos[13];                       /* its DOS name: NAME.EXT */
    unsigned long size;
    unsigned short date, time;          /* DOS's */
    unsigned char dir, attr;
};
/* A folder's entries ("" the root's; "SUB\DEEPER"; names by either of their forms, in any case of letters), in the
   folder's own order, without "." and "..": malloc'd, how many; -1: no such folder, or it is damaged. */
int fatimg_list(struct fatimg *v, const char *path, struct fatimg_entry **out);
/* n bytes of a file from byte `at` into buf: how many were read (fewer at its end); -1: no such file, or damage. */
long fatimg_read(struct fatimg *v, const char *path, unsigned long at, void *buf, unsigned long n);

/* Added to (names in DOS's form, NAME.EXT, eight and three: capitals are made of small letters): */
/* A folder made in a folder that is there, dated so (DOS's date and time; 0: none). 0; -1: its name is not
   DOS's, it is there, or there is no room. */
int fatimg_mkdir(struct fatimg *v, const char *path, unsigned short date, unsigned short time);
/* A new file with those bytes, dated so. 0; -1: its name is not DOS's, it is there; -2: no room (nothing of it
   stays). */
int fatimg_add(struct fatimg *v, const char *path, const void *data, unsigned long n, unsigned short date, unsigned short time);

#endif
