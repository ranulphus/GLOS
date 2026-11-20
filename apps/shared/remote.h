/* A folder tree on another computer, over SFTP (Files' remote folders; phase F, the owner's choice of 2026-11-20:
   docs/v1/apps/files.md): one connection, logged in as ssh logs in (the same key and known hosts, in SSH\
   beside the program), whose calls wait for their answers. Every call is one thread's at a time (a job's or a
   lister's: never the window's own, which would stand still while the network is slow). */
#ifndef REMOTE_H
#define REMOTE_H

enum {
    REMOTE_OK = 0,
    REMOTE_E_NAME = -1,                 /* no such host */
    REMOTE_E_CONNECT = -2,              /* it does not answer, or refuses */
    REMOTE_E_HOSTKEY = -3,              /* its key is not the one known, or the user would not have it */
    REMOTE_E_LOGIN = -4,                /* the login refused (or given up) */
    REMOTE_E_GONE = -5,                 /* the connection ended */
    REMOTE_E_MEMORY = -6,
    REMOTE_E_FAILED = -7,               /* the other computer said no (no such file, not allowed ...) */
    REMOTE_E_LOCAL = -8                 /* the file here could not be read or written */
};

/* What a connection may have to ask, on the thread that connects (an app shows a dialog and waits). */
struct remote_ui {
    /* A host met for the first time: its key's fingerprint ("SHA256:..."). 1: it is the right one (kept with the
       known hosts); 0: no. */
    int (*new_host)(void *ctx, const char *host, const char *fingerprint);
    /* The key did not log in (or there is none): a password. 1: given; 0: given up. `again`: the last was wrong. */
    int (*password)(void *ctx, const char *user, const char *host, int again, char *out, unsigned room);
};

struct remote;

/* dir: where the key (ID_ED25) and the known hosts (KNOWNHST.TXT) are; key_file: another key's file in that
   one's place (a saved session's), or NULL. why: a line for the user when it fails (the other side's own words
   where it gave some). */
struct remote *remote_open(const char *host, unsigned port, const char *user, const char *dir, const char *key_file,
                           const struct remote_ui *ui, void *ctx, int *err, char *why, unsigned room);
void remote_close(struct remote *r);
const char *remote_home(const struct remote *r);        /* where a login begins ("/home/ross"), "/" if not told */
const char *remote_why(const struct remote *r);         /* the last failure, in the other side's words */

struct remote_entry {
    const char *name;
    int dir;
    unsigned long long size;
    unsigned long mtime;                /* Unix's (0: not told) */
};
/* A folder's entries, as they come ("." and ".." left out); each: 0 to go on. */
int remote_list(struct remote *r, const char *dir, int (*each)(void *ctx, const struct remote_entry *e), void *ctx);
int remote_stat(struct remote *r, const char *path, struct remote_entry *e);    /* (e->name is not set) */
/* progress: 0 to go on, anything else stops the transfer (REMOTE_E_GONE is not said for that: REMOTE_E_FAILED). */
int remote_get(struct remote *r, const char *path, const char *local,
               void (*progress)(void *ctx, unsigned long long done, unsigned long long total), void *ctx);
int remote_put(struct remote *r, const char *local, const char *path,
               void (*progress)(void *ctx, unsigned long long done, unsigned long long total), void *ctx);
int remote_mkdir(struct remote *r, const char *path);
int remote_remove(struct remote *r, const char *path, int dir);        /* (a folder must be empty) */
int remote_rename(struct remote *r, const char *from, const char *to);
const char *remote_error(int err);      /* a few words for the user */
/* How local files are opened (as fopen does): the C library's way unless an app says otherwise. */
void remote_files(void *(*open)(const char *path, const char *mode));

#endif
