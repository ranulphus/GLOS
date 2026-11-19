/* Archives read as folders (Files; phase E, docs/v1/apps/files.md): what is in one, and its files unpacked.
   ZIP (stored and deflated), TAR (ustar, GNU's and pax's long names), GZ, and a TAR inside a GZ; the kind is
   told by the file's bytes, not its name. ZIPs are written too (below). One code for the app and for the host's
   tests (tests/host/arc_test.c, arcw_test.c).

   An archive's names have '/' between their parts, whatever it used, and no part "." or ".."; an entry whose
   name would leave the archive's folder (a ".." part) is not listed at all. A `struct arc` is one thread's at a
   time; two of them may be used at once (each has its own file and its own unpacker's state). The archive's
   file is open only inside these calls: between them it may be written again or taken away (what the list
   says is then no longer so: the archive is opened again). */
#ifndef ARC_H
#define ARC_H

enum {
    ARC_OK = 0,
    ARC_E_OPEN = -1,                    /* the file cannot be read */
    ARC_E_KIND = -2,                    /* no archive of a kind read here */
    ARC_E_DAMAGED = -3,                 /* cut short, or its bytes make no sense */
    ARC_E_MEMORY = -4,
    ARC_E_METHOD = -5,                  /* packed in a way not unpacked here (a ZIP's implode, shrink, bzip2 ...) */
    ARC_E_LOCKED = -6,                  /* encrypted */
    ARC_E_CRC = -7,                     /* unpacked, and not what was packed */
    ARC_E_STOP = -8,                    /* the caller's function said stop */
    ARC_E_BIG = -9                      /* of a size or form not read here (4 GB and more, in several parts) */
};

struct arc;

struct arc_entry {
    const char *name;                   /* in the archive: "DOCS/READ.ME" */
    unsigned long size;                 /* unpacked; a folder's is 0 */
    unsigned long packed;
    unsigned short date, time;          /* as DOS has them (0: not known) */
    unsigned char dir;                  /* a folder */
    signed char how;                    /* ARC_OK, or why its bytes cannot be had: ARC_E_METHOD, ARC_E_LOCKED */
};

int arc_name_is(const char *name);                      /* by its name's end, a kind read here: 1 */
struct arc *arc_open(const char *path, int *err);       /* its list read; NULL and *err (may be NULL) */
void arc_close(struct arc *a);
const char *arc_kind(const struct arc *a);              /* "ZIP", "TAR", "GZ", "TAR.GZ" */
unsigned arc_count(const struct arc *a);
const struct arc_entry *arc_at(const struct arc *a, unsigned i);        /* in the archive's order */
int arc_find(const struct arc *a, const char *name);    /* the entry of that name: its number, or -1 */

/* What is in a folder of the archive ("" its top): each thing once, in the names' order, folders that only
   other entries' names imply among them (e NULL for those). each: 0, or anything else to stop. How many. */
int arc_list(const struct arc *a, const char *dir,
             int (*each)(void *ctx, const char *name, int is_dir, const struct arc_entry *e), void *ctx);

/* One entry's bytes, in pieces, to data (0, or anything else to stop). ARC_OK when all of it came and was what
   was packed. */
int arc_extract(struct arc *a, unsigned i, int (*data)(void *ctx, const void *p, unsigned n), void *ctx);

/* Every entry in the archive's order, in one pass (a TAR inside a GZ is unpacked once for all its files, where
   arc_extract would begin at its start for each). begin: 1 its bytes are wanted, 0 not, less than 0 stop. data
   as above. end: the entry is over, with ARC_OK or what was wrong with it. Folders are told to begin too (and
   to end at once). ARC_OK if the archive was gone through to its end. */
struct arc_sink {
    int (*begin)(void *ctx, unsigned i, const struct arc_entry *e);
    int (*data)(void *ctx, const void *p, unsigned n);
    void (*end)(void *ctx, unsigned i, int err);
};
int arc_walk(struct arc *a, const struct arc_sink *s, void *ctx);

/* The path an entry is given on a DOS disc, under the folder it is unpacked into ("DOCS\\LONGNA~1.TXT"): each
   part of its name made one DOS can have (eight characters, a dot and three; capitals; '_' for what DOS has
   not), and no two things of one folder alike: the later of two is numbered as Windows numbers them. The same
   for every entry of a folder, however many of them are unpacked. NULL: no memory. */
const char *arc_dos_path(struct arc *a, unsigned i);
/* The same for a folder of the archive, whether it has an entry of its own or is only in its files' names
   (out: 0), or -1: there is no such thing in the archive. */
int arc_dos_of(struct arc *a, const char *name, char *out, unsigned room);
/* 1 if the archive has such a folder ("" its top: always), by an entry or by its files' names. */
int arc_has_folder(const struct arc *a, const char *name);
/* One part of a name as DOS can have it (out: 13 bytes), numbered if n is not 0 ("LONGNA~1.TXT"). */
void arc_dos_part(const char *part, unsigned len, unsigned n, char *out);

/* ---- ZIPs written (apps/shared/deflate.c packs). Names are as they are to be in the archive ('/' between their
   parts); a file is begun, given its bytes and ended, and the archive is good only once arcw_close has said
   ARC_OK. Anything that fails (the disc full: ARC_E_OPEN) stays failed: arcw_close then leaves the disc as it
   was (a new archive is taken away, one that was added to is as it was before). */
struct arcw;
struct arcw *arcw_open(const char *path, int add, int level, int *err);    /* add 0: a new ZIP, over what is
                                           there; 1: more entries for the one there (a new one if there is none).
                                           level: deflate's (1 quick, 2 better) */
int arcw_folder(struct arcw *w, const char *name, unsigned date, unsigned time);
int arcw_begin(struct arcw *w, const char *name, unsigned date, unsigned time);
int arcw_data(struct arcw *w, const void *p, unsigned n);
int arcw_end(struct arcw *w);
int arcw_close(struct arcw *w, int keep);   /* keep 0: given up, the disc as it was. What became of it. */

/* Entries taken out of a ZIP: the archive is written again beside itself without those drop says 1 for (by the
   name they are listed by), their packed bytes as they are, and put in the old one's place. How many went, or an
   error (the old archive is as it was). */
int arc_zip_remove(const char *path, int (*drop)(void *ctx, const char *name), void *ctx);

/* How files are opened (as fopen does), taken away and renamed here: the C library's own way unless an app
   says otherwise, once, before anything else (the toolkit's gui_fopen, gui_unlink and gui_rename know where
   DOS's paths are when the app runs on the host). */
void arc_files(void *(*open)(const char *path, const char *mode), int (*unlink)(const char *path),
               int (*rename)(const char *from, const char *to));

unsigned long arc_crc32(unsigned long crc, const void *p, unsigned n);  /* (crc 0 to begin) */
const char *arc_error(int err);                         /* a few words for the user */

#endif
