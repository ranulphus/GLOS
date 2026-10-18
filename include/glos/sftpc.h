/* GLOS's SFTP client (M10 step D; sdk/sshc/sftpc.c): SFTP version 3
 * (draft-ietf-secsh-filexfer-02, as OpenSSH's sftp-server speaks it) on an
 * sshc "sftp" subsystem channel. Each call waits for its answers by calling
 * the app's pump, which reads the network into sshc_input(); the channel's
 * data goes to sftpc_input(). Transfers keep 8 requests of 32 KB in flight. */
#ifndef GLOS_SFTPC_H
#define GLOS_SFTPC_H
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sftpc;
struct sshc_chan;

struct sftpc_attrs {
    int has_size, has_perm, has_time;
    uint64_t size;
    uint32_t perm, mtime;               /* perm with the file type bits (040000 a directory) */
};

/* pump(arg, ms): the network once, waiting up to ms: 0, or -1 (the connection is gone). */
struct sftpc *sftpc_new(struct sshc_chan *ch, int (*pump)(void *arg, unsigned ms), void *arg);   /* INIT sent */
void sftpc_input(struct sftpc *s, const uint8_t *data, uint32_t len);   /* the channel's stdout */
int sftpc_start(struct sftpc *s, unsigned ms);  /* the server's VERSION: 0, or -1 */
const char *sftpc_why(const struct sftpc *s);
void sftpc_free(struct sftpc *s);

int sftpc_realpath(struct sftpc *s, const char *path, char *out, size_t room);
int sftpc_stat(struct sftpc *s, const char *path, struct sftpc_attrs *a);
/* Each entry of a directory, "." and ".." included: f's return 0 to go on. */
int sftpc_ls(struct sftpc *s, const char *path, int (*f)(void *arg, const char *name, const char *longname,
                                                          const struct sftpc_attrs *a), void *arg);
int sftpc_get(struct sftpc *s, const char *remote, FILE *local, void (*progress)(void *arg, uint64_t done, uint64_t
              total), void *arg);       /* 0, or -1 */
int sftpc_put(struct sftpc *s, FILE *local, const char *remote, void (*progress)(void *arg, uint64_t done, uint64_t
              total), void *arg);
int sftpc_remove(struct sftpc *s, const char *path);
int sftpc_mkdir(struct sftpc *s, const char *path);
int sftpc_rmdir(struct sftpc *s, const char *path);
int sftpc_rename(struct sftpc *s, const char *from, const char *to);

#ifdef __cplusplus
}
#endif
#endif
