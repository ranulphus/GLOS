/* The disk cache's store (kernel/drv/cstore.c; docs/disk-cache.md): pure,
 * no kernel calls, so tests/host/cache_test.c builds it as it is. */
#ifndef K_CSTORE_H
#define K_CSTORE_H
#include "types.h"

#define CS_NIL      0xFFFFFFFFu
#define CB_USED     1u                  /* the frame holds a block */
#define CB_PINNED   2u                  /* being copied: not to be taken */

/* A block's key: the drive's index (0-3) and its first sector / 8. */
#define CS_KEY(drive, lba)  (((u32)(drive) << 30) | (u32)(((lba) >> 3) & 0x3FFFFFFFu))
#define CS_DRIVE(key)       ((key) >> 30)
#define CS_LBA(key)         (((key) & 0x3FFFFFFFu) << 3)

/* A frame's descriptor, indexed by the frame's number. */
struct cblk {
    u32 key;
    u32 hnext;                          /* the next in its hash chain (a frame number), CS_NIL */
    u32 prev, next;                     /* its list: towards the newest, the oldest */
    u8 valid;                           /* a bit for each of the block's 8 sectors */
    u8 seg;                             /* 0 probation, 1 protected */
    u8 flags;                           /* CB_* */
    u8 pad;
};

struct cstore {
    struct cblk *b;
    u32 nframes;
    u32 *h, hbits;                      /* 1 << hbits chains */
    u32 head[2], tail[2], n[2];         /* each segment: newest, oldest, how many */
};

void cs_init(struct cstore *s, struct cblk *b, u32 nframes, u32 *h, u32 hbits);
u32 cs_find(const struct cstore *s, u32 key);           /* the block's frame, or CS_NIL */
void cs_insert(struct cstore *s, u32 key, u32 frame);   /* a new block, no sector valid yet, in probation */
void cs_hit(struct cstore *s, u32 frame);               /* read again: to protected's newest */
void cs_remove(struct cstore *s, u32 frame);            /* the frame no longer a block */
u32 cs_victim(const struct cstore *s);                  /* the oldest unpinned block (probation first), or CS_NIL */
static inline u32 cs_blocks(const struct cstore *s) { return s->n[0] + s->n[1]; }

/* INT 13h's addresses, with the BIOS's own geometry (AH=08h). */
struct cgeo { u32 cyls, heads, spt; };
int cs_chs_lba(const struct cgeo *g, u32 cx, u32 dh, u32 *lba);    /* AH=02h/03h's CX and DH: 0, or -1 off the disk */
void cs_lba_chs(const struct cgeo *g, u32 lba, u32 *cx, u32 *dh);  /* and back */
#endif
