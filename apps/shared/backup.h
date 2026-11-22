/* Backups of a folder to another computer (Disk Tools; phase F, docs/v1/apps/disk-tools.md: "folders backed up
   to an SFTP server: full, or only what changed since the last (by size and date) ... restore: browse a backup and
   copy files back"). The backups of a folder live in a folder of their own there (`where`), a set each run:

       where/20261122-163000/          the files copied by that run, in their folders
       where/20261122-163000/MANIFEST.TXT

   A set's manifest names every file the folder had when it ran, with its size, its DOS date and time, and the set
   that holds its bytes: its own, or (a run of what changed) the one before that held them. Any set is restored
   from its manifest alone. The manifest is written last: a set without one (a run stopped or failed) is no
   backup, and is passed over.

   The other computer is a store: a few calls (remote.c's, in Disk Tools; a folder here, in the host's tests). */
#ifndef BACKUP_H
#define BACKUP_H

enum {
    BK_OK = 0,
    BK_E_LOCAL = -1,                    /* the folder here could not be read, or a file written */
    BK_E_STORE = -2,                    /* the other computer said no, or went */
    BK_E_STOPPED = -3,                  /* the user stopped it */
    BK_E_NONE = -4,                     /* no such set, or its manifest is not one */
    BK_E_MEMORY = -5
};

struct bk_store {
    void *ctx;
    int (*mkdir)(void *ctx, const char *path);         /* 0 made, or there already; else < 0 */
    int (*put)(void *ctx, const char *local, const char *path);        /* 0, or < 0 */
    int (*get)(void *ctx, const char *path, const char *local);
    /* A folder's entries ("." and ".." left out): each(arg, name, dir), 0 to go on; 0, or < 0 (no such folder). */
    int (*list)(void *ctx, const char *dir, int (*each)(void *arg, const char *name, int dir), void *arg);
};

#define BK_SET 24                       /* a set's name: YYYYMMDD-HHMMSS, and -2 ... if two came in one second */

struct bk_job {
    const struct bk_store *store;
    const char *where;                  /* the backups' folder there ("glos-backups/DATA"; made if need be) */
    const char *temp;                   /* a file here the manifests pass through */
    int changes;                        /* (a backup) 1: only what changed since the last set; 0: everything */
    /* Before each file (path: as the manifest has it), and as it goes: nonzero stops the job. */
    int (*progress)(void *ctx, const char *path, unsigned long long done, unsigned long long total);
    void *ctx;
    /* Out: */
    char set[BK_SET];                   /* the set made (or restored) */
    unsigned files, copied, kept, failed, folders;      /* the folder's files; those copied; those an older set
                                                           holds; those that could not be read or written; folders */
    unsigned long long bytes;           /* bytes copied */
    char why[200];                      /* a failure's words */
};

/* The folder (C:\DATA) backed up: a new set. BK_OK (some files may still have failed: j->failed), or a BK_E_. */
int bk_backup(struct bk_job *j, const char *folder);
/* The complete sets there, newest first, into sets (room at most): how many; < 0 none could be listed. */
int bk_sets(const struct bk_store *st, const char *where, char (*sets)[BK_SET], int room);
/* A set restored into the folder `to` (made if need be; its files written over): BK_OK or a BK_E_. */
int bk_restore(struct bk_job *j, const char *set, const char *to);

/* (The tests'.) A set's name for a time: YYYYMMDD-HHMMSS. */
void bk_set_name(char *out, unsigned room, int year, int month, int day, int hour, int min, int sec);

#endif
