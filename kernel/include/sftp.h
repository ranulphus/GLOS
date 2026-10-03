/* The SFTP server (kernel/ssh/sftp.c), one session per "sftp" subsystem
 * channel, in the ssh thread. */
#ifndef K_SFTP_H
#define K_SFTP_H
#include "types.h"

struct sftp;
struct ssh_chan;

struct sftp *sftp_open(struct ssh_chan *ch);    /* 0 without memory */
void sftp_input(struct sftp *s, const u8 *d, u32 n);    /* the client's bytes */
void sftp_eof(struct sftp *s);                  /* the client has sent all it will */
void sftp_close(struct sftp *s);                /* the channel has gone */
int sftp_service(struct sftp *s);               /* each turn: 1 waiting on DOS, 0 idle, -1 over: free it */
void sftp_free(struct sftp *s);

#endif
