/* Folders on other computers, for Files (phase F; the owner's choice of 2026-11-20, docs/v1/apps/files.md): a
   remote GLOS or Linux PC over SFTP (apps/shared/remote.c), shown like a drive. A folder there is written as an
   address, sftp://[user@]host[:port][/path]; one connection is kept for each computer and login while Files runs,
   shared by the windows that show it and the jobs that copy to and from it. */
#ifndef SITE_H
#define SITE_H

#include <glos/gui.h>

#define SITE_NAME 200

struct site_ent {
    char name[SITE_NAME];               /* as the other computer has it (UTF-8, as a rule) */
    unsigned char dir;
    unsigned long long size;
    unsigned short date, time;          /* DOS's, of its Unix time (0: not told) */
};

int site_is(const char *path);          /* 1: an address of a folder on another computer */
/* An address's parts (user "" if none, path "" for the login's own folder): 0, or -1 (no address). */
int site_parse(const char *url, char *user, int uroom, char *host, int hroom, unsigned *port, char *path, int proom);
void site_join(char *out, int room, const char *url, const char *name);         /* a thing in that folder */
int site_up(char *url);                 /* the folder that one is in: 0, or -1 (it is the top) */
const char *site_last(const char *url); /* its last part (a file's name), or the whole address at the top */

/* (The window's thread.) The folder `url` asked for: connected to if it is not yet (its questions, a new host's
   key and a password, asked in dialogs over `win`), then listed, both on a worker's thread; done is called on
   the window's thread when it is over: err 0, the address as it is now (the login's folder named, for an address
   with no path) and its entries (malloc'd: the callee's to free), or err (a REMOTE_E_) and why. */
typedef void (*site_done)(void *ctx, int err, const char *why, const char *url, struct site_ent *ents, int n);
int site_list(struct gui_window *win, const char *url, site_done done, void *ctx);      /* 0, or -1: no threads */

/* (A job's thread, or the window's for one quick thing.) On a computer already connected to: REMOTE_OK, or a
   REMOTE_E_ (REMOTE_E_GONE where there is no connection: the folder is shown again first). */
int site_read(const char *url, struct site_ent **ents, int *n);         /* a folder's entries (malloc'd) */
int site_get(const char *url, const char *local, void (*progress)(void *ctx, unsigned long long done, unsigned long long
             total), void *ctx);
int site_put(const char *local, const char *url, void (*progress)(void *ctx, unsigned long long done, unsigned long long
             total), void *ctx);
int site_mkdir(const char *url);
int site_remove(const char *url, int dir);
int site_rename(const char *from, const char *to);
int site_exists(const char *url, int *dir);                             /* 1 and whether it is a folder, 0, or -1 */
/* The saved sessions (the Terminal's Sessions; apps/shared/sessions.h): their names, how many (room at most);
   one's computer as an address, sftp://[user@]host[:port]: 0, or -1 (no session of that name). A session's own
   key is the one its computer is logged in to with. */
int site_sessions(const char **names, int room, int every);    /* every 0: the SSH ones, which have folders */
int site_session(const char *name, char *url, int room);
void site_end(void);                    /* every connection closed (Files is going) */
void site_keys(const char *dir);        /* where the key and the known hosts are, if not SSH\ beside the program */

#endif
