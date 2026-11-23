/* Disk Tools' backups on another computer: bknet.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glos/glos.h>
#include <glos/gui.h>

#include "../shared/sessions.h"
#include "bknet.h"

static void ssh_dir(char *out, unsigned room)   /* as SSH.EXE has it */
{
    const char *e = getenv("GLOSSSH");
    if (e && *e)
        snprintf(out, room, "%s", e);
    else
        gui_path_join(out, (int)room, glos_home(), "SSH");
}

static struct session *sessions_read(int *n)    /* (malloc'd; the caller frees) */
{
    char dir[260], path[300];
    struct session *s = malloc(SESSIONS_MAX * sizeof *s);
    *n = 0;
    if (!s)
        return NULL;
    ssh_dir(dir, sizeof dir);
    sessions_path(path, sizeof path, dir);
    *n = sessions_load(path, s, SESSIONS_MAX);
    return s;
}

int bknet_sessions(char (*names)[24], int room)
{
    int n, i, k = 0;
    struct session *s = sessions_read(&n);
    for (i = 0; s && i < n && k < room; i++)
        if (s[i].kind == SESSION_SSH)
            snprintf(names[k++], 24, "%s", s[i].name);
    free(s);
    return k;
}

int bknet_computer(const char *computer, char *host, unsigned hroom, unsigned *port, char *user, unsigned uroom,
                   char *key, unsigned kroom)
{
    const char *at, *colon;
    int n, i;
    struct session *s;
    host[0] = user[0] = key[0] = 0, *port = 22;
    while (*computer == ' ')
        computer++;
    if (!*computer)
        return -1;
    if ((s = sessions_read(&n)) != NULL && (i = sessions_find(s, n, computer)) >= 0 && s[i].kind == SESSION_SSH) {
        snprintf(host, hroom, "%s", s[i].host);
        snprintf(user, uroom, "%s", s[i].user);
        snprintf(key, kroom, "%s", s[i].key);
        *port = s[i].port ? (unsigned)s[i].port : 22;
        free(s);
        return host[0] ? 0 : -1;
    }
    free(s);
    if (strchr(computer, ' ') || strchr(computer, '/') || strchr(computer, '\\'))
        return -1;
    if ((at = strchr(computer, '@')) != NULL) {
        snprintf(user, uroom, "%.*s", (int)(at - computer), computer);
        computer = at + 1;
    }
    colon = strchr(computer, ':');
    snprintf(host, hroom, "%.*s", colon ? (int)(colon - computer) : (int)strlen(computer), computer);
    if (colon) {
        char *e;
        long p = strtol(colon + 1, &e, 10);
        if (*e || p < 1 || p > 65535)
            return -1;
        *port = (unsigned)p;
    }
    return host[0] ? 0 : -1;
}

static int no_new_host(void *ctx, const char *host, const char *fingerprint)
{
    (void)ctx, (void)host, (void)fingerprint;
    return 0;                                   /* (nobody to ask: a computer not met is not believed) */
}

static int no_password(void *ctx, const char *user, const char *host, int again, char *out, unsigned room)
{
    (void)ctx, (void)user, (void)host, (void)again, (void)out, (void)room;
    return 0;
}

static const struct remote_ui nobody = { no_new_host, no_password };

struct remote *bknet_open(const char *computer, const struct remote_ui *ui, void *ctx, int *err, char *why, unsigned room)
{
    char host[64], user[32], key[100], dir[260];
    unsigned port;
    why[0] = 0;
    if (bknet_computer(computer, host, sizeof host, &port, user, sizeof user, key, sizeof key) != 0) {
        *err = REMOTE_E_NAME;
        snprintf(why, room, "%s is neither a saved session nor a computer's address", computer);
        return NULL;
    }
    ssh_dir(dir, sizeof dir);
    return remote_open(host, port, user, dir, key[0] ? key : NULL, ui ? ui : &nobody, ctx, err, why, room);
}

/* ---- the store */

static void full(struct remote *r, const char *path, char *out, size_t room)    /* from the top */
{
    if (path[0] == '/')
        snprintf(out, room, "%s", path);
    else {
        const char *h = remote_home(r);
        size_t n = strlen(h);
        snprintf(out, room, "%s%s%s", h, n && h[n - 1] == '/' ? "" : "/", path);
    }
}

static int st_mkdir(void *ctx, const char *path)
{
    char p[400];
    struct remote_entry e;
    full(ctx, path, p, sizeof p);
    if (remote_stat(ctx, p, &e) == REMOTE_OK)
        return e.dir ? 0 : -1;
    return remote_mkdir(ctx, p) == REMOTE_OK ? 0 : -1;
}

static int st_put(void *ctx, const char *local, const char *path)
{
    char p[400];
    full(ctx, path, p, sizeof p);
    return remote_put(ctx, local, p, NULL, NULL) == REMOTE_OK ? 0 : -1;
}

static int st_get(void *ctx, const char *path, const char *local)
{
    char p[400];
    full(ctx, path, p, sizeof p);
    return remote_get(ctx, p, local, NULL, NULL) == REMOTE_OK ? 0 : -1;
}

struct each_ctx { int (*each)(void *arg, const char *name, int dir); void *arg; };

static int st_one(void *ctx, const struct remote_entry *e)
{
    struct each_ctx *c = ctx;
    return c->each(c->arg, e->name, e->dir);
}

static int st_list(void *ctx, const char *dir, int (*each)(void *arg, const char *name, int dir), void *arg)
{
    char p[400];
    struct each_ctx c;
    c.each = each, c.arg = arg;
    full(ctx, dir, p, sizeof p);
    return remote_list(ctx, p, st_one, &c) == REMOTE_OK ? 0 : -1;
}

static int st_remove(void *ctx, const char *path, int dir)
{
    char p[400];
    full(ctx, path, p, sizeof p);
    return remote_remove(ctx, p, dir) == REMOTE_OK ? 0 : -1;
}

void bknet_store(struct bk_store *st, struct remote *r)
{
    st->ctx = r;
    st->mkdir = st_mkdir, st->put = st_put, st->get = st_get, st->list = st_list, st->remove = st_remove;
}
