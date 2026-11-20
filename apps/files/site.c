/* Folders on other computers, for Files (site.h): the connections kept, an address taken apart and put together,
 * a folder listed on a worker's thread with the connection's questions asked on the window's.
 *
 * A connection is one thread's at a time: each has a lock, taken for a call's length (a folder's listing, a
 * file's whole transfer). The key and the known hosts are SSH.EXE's own (SSH\ beside the programs), so a
 * computer that `ssh` has logged in to needs no question here, and the other way about. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>

#include "../shared/remote.h"
#include "../shared/sessions.h"
#include "files.h"
#include "site.h"

struct site {
    struct site *next;
    char user[40], host[80];
    unsigned port;
    struct remote *r;
    glos_handle lock;
};

static struct site *sites;
static glos_handle sites_lock;          /* the list's own */

int site_is(const char *path)
{
    return (path[0] == 's' || path[0] == 'S') && (path[1] == 'f' || path[1] == 'F') && (path[2] == 't' || path[2] == 'T')
        && (path[3] == 'p' || path[3] == 'P') && !strncmp(path + 4, "://", 3) && path[7];
}

int site_parse(const char *url, char *user, int uroom, char *host, int hroom, unsigned *port, char *path, int proom)
{
    const char *p, *at, *slash, *colon;
    if (!site_is(url))
        return -1;
    p = url + 7;
    slash = strchr(p, '/');
    if (!slash)
        slash = p + strlen(p);
    at = memchr(p, '@', (size_t)(slash - p));
    user[0] = 0;
    if (at) {
        snprintf(user, (size_t)uroom, "%.*s", (int)(at - p), p);
        p = at + 1;
    }
    colon = memchr(p, ':', (size_t)(slash - p));
    *port = 22;
    if (colon) {
        *port = (unsigned)atoi(colon + 1);
        if (!*port || *port > 65535)
            return -1;
    }
    snprintf(host, (size_t)hroom, "%.*s", (int)((colon ? colon : slash) - p), p);
    if (!host[0])
        return -1;
    snprintf(path, (size_t)proom, "%s", slash);
    while (strlen(path) > 1 && path[strlen(path) - 1] == '/')   /* (no '/' at its end, but for the top's own) */
        path[strlen(path) - 1] = 0;
    return 0;
}

static void site_make(char *out, int room, const char *user, const char *host, unsigned port, const char *path)
{
    char p[16] = "";
    if (port != 22)
        snprintf(p, sizeof p, ":%u", port);
    snprintf(out, (size_t)room, "sftp://%s%s%s%s%s", user, user[0] ? "@" : "", host, p, path);
}

void site_join(char *out, int room, const char *url, const char *name)
{
    size_t n = strlen(url);
    snprintf(out, (size_t)room, "%s%s%s", url, n && url[n - 1] == '/' ? "" : "/", name);
}

static char *path_of(char *url)         /* where its path begins (its end, if it has none) */
{
    char *slash = strchr(url + 7, '/');
    return slash ? slash : url + strlen(url);
}

int site_up(char *url)
{
    char *path = path_of(url), *last;
    if (!site_is(url) || !path[0] || !path[1])
        return -1;
    last = strrchr(path, '/');
    if (last == path)
        path[1] = 0;                            /* (the top itself: "/") */
    else
        *last = 0;
    return 0;
}

const char *site_last(const char *url)
{
    const char *path = strchr(url + 7, '/'), *last;
    if (!path || !path[1])
        return url;
    last = strrchr(path, '/');
    return last + 1;
}

/* A Unix time as DOS's date and time (what the lists show), by the calendar alone: no zone is known. */
static void dos_time(unsigned long t, unsigned short *date, unsigned short *tm)
{
    unsigned long days = t / 86400, secs = t % 86400;
    unsigned y = 1970, m;
    static const unsigned char len[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    *date = *tm = 0;
    if (!t)
        return;
    for (;;) {
        unsigned n = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        if (days < n)
            break;
        days -= n, y++;
    }
    for (m = 0; m < 12; m++) {
        unsigned n = len[m] + (m == 1 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0) ? 1u : 0u);
        if (days < n)
            break;
        days -= n;
    }
    if (y < 1980 || y > 2107)
        return;
    *date = (unsigned short)((y - 1980) << 9 | (m + 1) << 5 | (unsigned)(days + 1));
    *tm = (unsigned short)(secs / 3600 << 11 | secs / 60 % 60 << 5 | secs % 60 / 2);
}

/* ---- the connections */

static struct site *site_find(const char *user, const char *host, unsigned port)
{
    struct site *s;
    if (!sites_lock)
        sites_lock = glos_lock();
    glos_take(sites_lock, 0xFFFFFFFFu);
    for (s = sites; s; s = s->next)
        if (s->port == port && !strcmp(s->host, host) && !strcmp(s->user, user))
            break;
    glos_give(sites_lock);
    return s;
}

static void whose(char *user, size_t room)      /* an address that names nobody: the login `ssh` would use */
{
    const char *u = getenv("USER");
    if (!user[0])
        snprintf(user, room, "%s", u && *u ? u : "glos");
}

static struct site *site_of(const char *url, char *path, int proom)     /* an address's connection, if there is one */
{
    char user[40], host[80];
    unsigned port;
    struct site *s;
    if (site_parse(url, user, sizeof user, host, sizeof host, &port, path, proom) != 0)
        return NULL;
    whose(user, sizeof user);
    s = site_find(user, host, port);
    return s && s->r ? s : NULL;
}

static char keys_at[260];               /* (FILES -browse DIR -sshdir FOLDER: the tests' own) */

void site_keys(const char *dir)
{
    snprintf(keys_at, sizeof keys_at, "%s", dir);
}

static void ssh_dir(char *out, size_t room)     /* where the key and the known hosts are: as SSH.EXE has it */
{
    const char *e = keys_at[0] ? keys_at : getenv("GLOSSSH");
    if (e && *e)
        snprintf(out, room, "%s", e);
    else
        snprintf(out, room, "%s\\SSH", files_home);
}

/* ---- the saved sessions (apps/shared/sessions.h: the Terminal's Sessions keeps them, SESSIONS.CFG beside the
   key): a computer is asked for by a session's name, and a session's own key logs in to its computer. Read
   again each time they are asked for (the Terminal may have changed them). */
static struct session *sess;
static int nsess;

static void sess_read(void)                     /* (the window's thread) */
{
    char dir[260], path[300];
    if (!sites_lock)
        sites_lock = glos_lock();
    glos_take(sites_lock, 0xFFFFFFFFu);
    if (!sess)
        sess = malloc(SESSIONS_MAX * sizeof *sess);
    ssh_dir(dir, sizeof dir);
    sessions_path(path, sizeof path, dir);
    sessions_files(gui_fopen);
    nsess = sess ? sessions_load(path, sess, SESSIONS_MAX) : 0;
    glos_give(sites_lock);
}

int site_sessions(const char **names, int room)
{
    int i;
    sess_read();
    for (i = 0; i < nsess && i < room; i++)
        names[i] = sess[i].name;
    return i;
}

int site_session(const char *name, char *url, int room)
{
    char port[12] = "";
    int i;
    sess_read();
    if ((i = sessions_find(sess, nsess, name)) < 0)
        return -1;
    if (sess[i].port && sess[i].port != 22)
        snprintf(port, sizeof port, ":%d", sess[i].port);
    snprintf(url, (size_t)room, "sftp://%s%s%s%s", sess[i].user, sess[i].user[0] ? "@" : "", sess[i].host, port);
    return 0;
}

/* The key's file a saved session names for that computer, port and user (a session with no user is anyone's):
   into out, "" if none does. (A worker's thread.) */
static void session_key(const char *user, const char *host, unsigned port, char *out, size_t room)
{
    int i;
    out[0] = 0;
    glos_take(sites_lock, 0xFFFFFFFFu);
    for (i = 0; i < nsess; i++)
        if (sess[i].key[0] && !strcmp(sess[i].host, host) && (unsigned)(sess[i].port ? sess[i].port : 22) == port
            && (!sess[i].user[0] || !strcmp(sess[i].user, user))) {
            snprintf(out, room, "%s", sess[i].key);
            break;
        }
    glos_give(sites_lock);
}

/* ---- a folder asked for: connected, listed */

struct ask {                            /* a question of the connection's, put to the user on the window's thread */
    struct gui_window *win;
    int password, again, answer;
    const char *user, *host, *fingerprint;
    char *out;
    unsigned room;
    glos_handle sig;
};

struct req {
    struct gui_window *win;
    struct gui_task *task;
    site_done done;
    void *ctx;
    char url[300], now[300], why[200];
    int err, n, room;
    struct site_ent *ents;
};

static void ask_ui(void *data)
{
    struct ask *a = data;
    char t[400];
    if (a->password) {
        snprintf(t, sizeof t, a->again ? GUI_T("That was not it. The password of %s on %s:")
                 : GUI_T("The password of %s on %s:"), a->user, a->host);
        a->out[0] = 0;
        a->answer = gui_ask_secret(a->win, GUI_T("Password"), t, a->out, (int)a->room);
    } else {
        snprintf(t, sizeof t, GUI_T("This PC has not met %s before. Its key's fingerprint is\n%s\nIs that the right computer?"),
                 a->host, a->fingerprint);
        a->answer = gui_message(a->win, GUI_T("Files"), t, GUI_MB_YESNO) == GUI_YES;
    }
    glos_signal_set(a->sig, 1);
}

static int ask(struct req *q, struct ask *a)    /* (the worker's thread) the question put, its answer waited for */
{
    int which;
    a->win = q->win;
    a->answer = 0;
    if (!(a->sig = glos_signal(0)))
        return 0;
    gui_task_post(q->task, ask_ui, a);
    glos_wait(&a->sig, 1, 0xFFFFFFFFu, &which);
    glos_close(a->sig);
    return a->answer;
}

static int ask_host(void *ctx, const char *host, const char *fingerprint)
{
    struct ask a;
    memset(&a, 0, sizeof a);
    a.host = host, a.fingerprint = fingerprint;
    printf("FILES site new-host %s\n", host);
    fflush(stdout);
    return ask(ctx, &a);
}

static int ask_password(void *ctx, const char *user, const char *host, int again, char *out, unsigned room)
{
    struct ask a;
    memset(&a, 0, sizeof a);
    a.password = 1, a.again = again, a.user = user, a.host = host, a.out = out, a.room = room;
    printf("FILES site password %s\n", again ? "again" : "asked");
    fflush(stdout);
    return ask(ctx, &a);
}

static const struct remote_ui ui = { ask_host, ask_password };

struct gather {
    struct site_ent *ents;
    int n, room, bad;
};

static int gather_one(void *ctx, const struct remote_entry *e)
{
    struct gather *g = ctx;
    struct site_ent *d;
    if (g->n == g->room) {
        struct site_ent *m = realloc(g->ents, (size_t)(g->room ? g->room * 2 : 64) * sizeof *m);
        if (!m) {
            g->bad = 1;
            return 1;
        }
        g->ents = m, g->room = g->room ? g->room * 2 : 64;
    }
    d = &g->ents[g->n++];
    memset(d, 0, sizeof *d);
    snprintf(d->name, sizeof d->name, "%s", e->name);
    d->dir = (unsigned char)(e->dir != 0);
    d->size = e->dir ? 0 : e->size;
    dos_time(e->mtime, &d->date, &d->time);
    return 0;
}

static int by_name(const void *a, const void *b)        /* folders first, then by name, as a drive's are */
{
    const struct site_ent *x = a, *y = b;
    if (x->dir != y->dir)
        return y->dir - x->dir;
    return strcmp(x->name, y->name);
}

static int listed(struct site *s, const char *path, struct site_ent **ents, int *n)     /* (its lock held) */
{
    struct gather g = { NULL, 0, 0, 0 };
    int r = remote_list(s->r, path, gather_one, &g);
    if (r != REMOTE_OK || g.bad) {
        free(g.ents);
        return g.bad ? REMOTE_E_MEMORY : r;
    }
    qsort(g.ents, (size_t)g.n, sizeof *g.ents, by_name);
    *ents = g.ents, *n = g.n;
    return REMOTE_OK;
}

static void list_work(struct gui_task *t, void *arg)
{
    struct req *q = arg;
    char user[40], host[80], path[260], dir[260], key[120];
    unsigned port;
    struct site *s;
    q->task = t;
    q->err = REMOTE_E_NAME;
    if (site_parse(q->url, user, sizeof user, host, sizeof host, &port, path, sizeof path) != 0) {
        snprintf(q->why, sizeof q->why, "%s", "that is no address of a folder (sftp://user@computer/folder)");
        return;
    }
    whose(user, sizeof user);
    s = site_find(user, host, port);
    if (!s) {
        if (!(s = calloc(1, sizeof *s)) || !(s->lock = glos_lock())) {
            free(s);
            q->err = REMOTE_E_MEMORY;
            return;
        }
        snprintf(s->user, sizeof s->user, "%s", user);
        snprintf(s->host, sizeof s->host, "%s", host);
        s->port = port;
        glos_take(sites_lock, 0xFFFFFFFFu);
        s->next = sites, sites = s;
        glos_give(sites_lock);
    }
    glos_take(s->lock, 0xFFFFFFFFu);
    session_key(user, host, port, key, sizeof key);
    if (!s->r) {
        ssh_dir(dir, sizeof dir);
        s->r = remote_open(host, port, user, dir, key, &ui, q, &q->err, q->why, sizeof q->why);
        printf("FILES site %s %s\n", s->r ? "connected" : "refused", host);
        fflush(stdout);
    }
    if (s->r) {
        if (!path[0])
            snprintf(path, sizeof path, "%s", remote_home(s->r));
        q->err = listed(s, path, &q->ents, &q->n);
        if (q->err == REMOTE_E_GONE) {          /* the connection had ended (the other side's time-out): once more */
            remote_close(s->r);
            ssh_dir(dir, sizeof dir);
            s->r = remote_open(host, port, user, dir, key, &ui, q, &q->err, q->why, sizeof q->why);
            if (s->r)
                q->err = listed(s, path, &q->ents, &q->n);
        }
        if (q->err != REMOTE_OK && s->r && !q->why[0])
            snprintf(q->why, sizeof q->why, "%s", remote_why(s->r)[0] ? remote_why(s->r) : remote_error(q->err));
        site_make(q->now, sizeof q->now, user, host, port, path);      /* (whose login it is: always said) */
    }
    glos_give(s->lock);
}

static void list_done(struct gui_window *owner, void *arg, int cancelled)
{
    struct req *q = arg;
    (void)owner;
    if (cancelled && q->err == REMOTE_OK) {
        free(q->ents);
        q->ents = NULL, q->n = 0;
        q->err = REMOTE_E_FAILED;
        snprintf(q->why, sizeof q->why, "%s", "it was given up");
    }
    q->done(q->ctx, q->err, q->why, q->now, q->ents, q->n);
    free(q);
}

int site_list(struct gui_window *win, const char *url, site_done done, void *ctx)
{
    struct req *q = calloc(1, sizeof *q);
    if (!q)
        return -1;
    sess_read();                                /* (a session's own key is its computer's: as they are now) */
    q->win = win, q->done = done, q->ctx = ctx;
    snprintf(q->url, sizeof q->url, "%s", url);
    /* (No dialog of the task's own: it would come up over the connection's questions, a second into them, and
       take the keyboard from them. The view's status bar says what is being waited for.) */
    if (!gui_task_start(win, GUI_T("Connecting"), list_work, q, list_done, 0)) {
        free(q);
        return -1;
    }
    return 0;
}

/* ---- one thing done on a computer connected to */

#define WITH(url)                                           \
    char path[260];                                         \
    struct site *s = site_of(url, path, sizeof path);       \
    int r;                                                  \
    if (!s)                                                 \
        return REMOTE_E_GONE;                               \
    glos_take(s->lock, 0xFFFFFFFFu)
#define DONE()                                              \
    glos_give(s->lock);                                     \
    return r

int site_read(const char *url, struct site_ent **ents, int *n)
{
    WITH(url);
    r = listed(s, path[0] ? path : remote_home(s->r), ents, n);
    DONE();
}

int site_get(const char *url, const char *local, void (*progress)(void *ctx, unsigned long long done, unsigned long long
             total), void *ctx)
{
    WITH(url);
    r = remote_get(s->r, path, local, progress, ctx);
    DONE();
}

int site_put(const char *local, const char *url, void (*progress)(void *ctx, unsigned long long done, unsigned long long
             total), void *ctx)
{
    WITH(url);
    r = remote_put(s->r, local, path, progress, ctx);
    DONE();
}

int site_mkdir(const char *url)
{
    WITH(url);
    r = remote_mkdir(s->r, path);
    DONE();
}

int site_remove(const char *url, int dir)
{
    WITH(url);
    r = remote_remove(s->r, path, dir);
    DONE();
}

int site_rename(const char *from, const char *to)
{
    char user[40], host[80], to_path[260];
    unsigned port;
    WITH(from);
    r = site_parse(to, user, sizeof user, host, sizeof host, &port, to_path, sizeof to_path) != 0 ? REMOTE_E_FAILED
        : remote_rename(s->r, path, to_path);
    DONE();
}

int site_exists(const char *url, int *dir)
{
    struct remote_entry e;
    WITH(url);
    r = remote_stat(s->r, path, &e);
    if (dir)
        *dir = r == REMOTE_OK && e.dir;
    r = r == REMOTE_OK ? 1 : r == REMOTE_E_FAILED ? 0 : -1;
    DONE();
}

void site_end(void)
{
    struct site *s, *next;
    for (s = sites; s; s = next) {
        next = s->next;
        if (s->r)
            remote_close(s->r);
        free(s);
    }
    sites = NULL;
}
