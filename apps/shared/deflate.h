/* Bytes packed as a deflate stream (RFC 1951, what a ZIP's entries and gzip hold; loader/inflate.c unpacks it):
   for the ZIPs Files writes (apps/shared/arc.c). Matches are looked for in the last 32 KB through a hash of three
   bytes; each block of some 32 KB is written with codes made for it, with the fixed codes, or as it is, whichever
   is shortest. The state is the caller's (deflate_state_size() bytes, about 270 KB): two streams may be packed
   at once. One code for the app and for the host's test (tests/host/deflate_test.c). */
#ifndef DEFLATE_H
#define DEFLATE_H

struct defl;

unsigned deflate_state_size(void);
/* A stream begun. level: 1 quick (a 486's), 2 better. put: the packed bytes as they are made (0, or anything
   else: nothing more is made, and deflate_data and deflate_end return it). */
void deflate_begin(struct defl *d, int level, int (*put)(void *ctx, const void *p, unsigned n), void *ctx);
int deflate_data(struct defl *d, const void *p, unsigned long n);       /* more of the bytes */
int deflate_end(struct defl *d);                                        /* the last of the stream written */

#endif
