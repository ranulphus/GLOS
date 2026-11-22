/* Disk Tools' backups on another computer (phase F; apps/shared/backup.h's sets, over apps/shared/remote.c's
   SFTP): the computer is a saved session's name (the Terminal's Sessions: its host, port, user and key) or an
   address, [user@]host[:port]; the key and the known hosts are SSH.EXE's (GLOSSSH, or SSH\ in GLOS's folder). */
#ifndef BKNET_H
#define BKNET_H

#include "../shared/backup.h"
#include "../shared/remote.h"

/* The computer named: its parts, and the key a session gives it ("" SSH's own). 0, or -1 (neither a session's
   name nor an address). */
int bknet_computer(const char *computer, char *host, unsigned hroom, unsigned *port, char *user, unsigned uroom,
                   char *key, unsigned kroom);
/* Connected to (ui: its questions; NULL: none can be asked, and a host not known is refused): the connection, or
   NULL with *err (a REMOTE_E_) and why. */
struct remote *bknet_open(const char *computer, const struct remote_ui *ui, void *ctx, int *err, char *why, unsigned room);
/* A store of backup.h's over the connection: a path not from the top is the login's folder's. */
void bknet_store(struct bk_store *st, struct remote *r);
/* The saved sessions' names that are SSH's, for a list: how many (room at most). */
int bknet_sessions(char (*names)[24], int room);

#endif
